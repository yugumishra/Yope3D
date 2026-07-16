#include "GltfLoader.h"
#include "../scene/serialization/JsonParser.h"
#include "AssetResolve.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <array>
#include <cmath>

namespace {

using Bytes = std::vector<uint8_t>;
using Mat16 = std::array<float, 16>;   // column-major 4x4

// path is a full filesystem path (already resolved against YOPE_ASSETS_DIR by
// the caller, or a sibling of one via gltfDir/uri) — normalize it back to an
// assets/-relative key so embedded models/buffers are found too.
Bytes readFile(const std::string& path) {
    Bytes b = assets::readBytes(assets::normalizeToAssetsRelative(path));
    if (b.empty()) throw std::runtime_error("GltfLoader: cannot open " + path);
    return b;
}

uint32_t rdU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

Bytes base64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    Bytes out;
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(uint8_t((buf >> bits) & 0xFF)); }
    }
    return out;
}

// Decompose a column-major glTF node matrix into a TRS Transform. Shear is out of
// scope (assets are authored without it); negative scale is not special-cased.
Transform decomposeMatrix(const Mat16& m) {
    Transform t;
    t.position = { m[12], m[13], m[14] };
    math::Vec3 c0 = { m[0], m[1], m[2]  };
    math::Vec3 c1 = { m[4], m[5], m[6]  };
    math::Vec3 c2 = { m[8], m[9], m[10] };
    float sx = c0.length(), sy = c1.length(), sz = c2.length();
    t.scale = { sx, sy, sz };
    if (sx > 0) c0 = c0 / sx;
    if (sy > 0) c1 = c1 / sy;
    if (sz > 0) c2 = c2 / sz;
    math::Mat3 r;
    r.m[0] = c0.x; r.m[1] = c0.y; r.m[2] = c0.z;   // column 0
    r.m[3] = c1.x; r.m[4] = c1.y; r.m[5] = c1.z;   // column 1
    r.m[6] = c2.x; r.m[7] = c2.y; r.m[8] = c2.z;   // column 2
    t.rotation = math::Quat::fromMatrix(r);
    return t;
}

int componentByteSize(int ct) {
    switch (ct) {
        case 5120: case 5121: return 1;  // (U)BYTE
        case 5122: case 5123: return 2;  // (U)SHORT
        case 5125: case 5126: return 4;  // UINT / FLOAT
    }
    return 4;
}
int typeComponentCount(const std::string& t) {
    if (t == "SCALAR") return 1;
    if (t == "VEC2")   return 2;
    if (t == "VEC3")   return 3;
    if (t == "VEC4")   return 4;
    if (t == "MAT4")   return 16;
    return 1;
}

// ---------------------------------------------------------------------------
struct Ctx {
    const JsonNode*                  root = nullptr;
    std::vector<Bytes>               buffers;
    std::string                      gltfDir;     // absolute dir of the .gltf/.glb
    std::string                      absPath;
    GltfLoader::RegisterImageFn      registerImage;

    const JsonNode& arr(const char* name) const { return (*root)[name]; }

    // Read a vertex-attribute accessor as floats (normalised integers expanded).
    std::vector<float> readFloats(int accessorIdx, int& outComps) const {
        const JsonNode& acc = arr("accessors").asArray()[accessorIdx];
        int ct       = acc["componentType"].asInt();
        int count    = acc["count"].asInt();
        std::string type = acc["type"].asString();
        int comps    = typeComponentCount(type);
        bool normalized = acc.contains("normalized") && acc["normalized"].asBool();
        int accOff   = acc.contains("byteOffset") ? acc["byteOffset"].asInt() : 0;

        const JsonNode& bv = arr("bufferViews").asArray()[acc["bufferView"].asInt()];
        int bufIdx   = bv["buffer"].asInt();
        int bvOff    = bv.contains("byteOffset") ? bv["byteOffset"].asInt() : 0;
        int compSize = componentByteSize(ct);
        int stride   = bv.contains("byteStride") && bv["byteStride"].asInt() > 0
                         ? bv["byteStride"].asInt() : comps * compSize;

        const Bytes& buf = buffers.at(bufIdx);
        std::vector<float> out(static_cast<size_t>(count) * comps);
        for (int i = 0; i < count; ++i) {
            const uint8_t* base = buf.data() + bvOff + accOff + i * stride;
            for (int c = 0; c < comps; ++c) {
                const uint8_t* e = base + c * compSize;
                float v = 0.0f;
                switch (ct) {
                    case 5126: std::memcpy(&v, e, 4); break;                       // FLOAT
                    case 5125: { uint32_t u; std::memcpy(&u, e, 4); v = float(u); } break;
                    case 5123: { uint16_t u; std::memcpy(&u, e, 2); v = normalized ? u / 65535.0f : float(u); } break;
                    case 5121: { uint8_t  u = *e;                   v = normalized ? u / 255.0f   : float(u); } break;
                    case 5122: { int16_t  s; std::memcpy(&s, e, 2); v = normalized ? std::max(s / 32767.0f, -1.0f) : float(s); } break;
                    case 5120: { int8_t   s = int8_t(*e);          v = normalized ? std::max(s / 127.0f,   -1.0f) : float(s); } break;
                }
                out[i * comps + c] = v;
            }
        }
        outComps = comps;
        return out;
    }

    std::vector<uint32_t> readIndices(int accessorIdx) const {
        const JsonNode& acc = arr("accessors").asArray()[accessorIdx];
        int ct    = acc["componentType"].asInt();
        int count = acc["count"].asInt();
        int accOff= acc.contains("byteOffset") ? acc["byteOffset"].asInt() : 0;
        const JsonNode& bv = arr("bufferViews").asArray()[acc["bufferView"].asInt()];
        int bufIdx = bv["buffer"].asInt();
        int bvOff  = bv.contains("byteOffset") ? bv["byteOffset"].asInt() : 0;
        int compSize = componentByteSize(ct);
        const Bytes& buf = buffers.at(bufIdx);
        std::vector<uint32_t> out(count);
        for (int i = 0; i < count; ++i) {
            const uint8_t* e = buf.data() + bvOff + accOff + i * compSize;
            switch (ct) {
                case 5125: out[i] = rdU32(e); break;
                case 5123: { uint16_t u; std::memcpy(&u, e, 2); out[i] = u; } break;
                case 5121: out[i] = *e; break;
            }
        }
        return out;
    }

    // Resolve a glTF texture index to a path/key loadable via AssetManager.
    // External-file URIs become paths relative to YOPE_ASSETS_DIR; embedded /
    // base64 images are decoded and registered under a synthetic key.
    std::string resolveTexture(int texIdx, bool srgb) const {
        if (texIdx < 0 || !root->contains("textures")) return "";
        const JsonNode& tex = arr("textures").asArray()[texIdx];
        if (!tex.contains("source")) return "";
        int imgIdx = tex["source"].asInt();
        const JsonNode& img = arr("images").asArray()[imgIdx];

        if (img.contains("uri")) {
            const std::string uri = img["uri"].asString();
            if (uri.rfind("data:", 0) == 0) {
                // base64 data URI
                if (!registerImage) return "";
                auto comma = uri.find(',');
                Bytes imgBytes = base64Decode(uri.substr(comma + 1));
                return emitImage(imgBytes, imgIdx, srgb);
            }
            // External file: compute path relative to YOPE_ASSETS_DIR.
            std::filesystem::path full = std::filesystem::path(gltfDir) / uri;
            std::error_code ec;
            std::filesystem::path rel = std::filesystem::relative(full, std::filesystem::path(YOPE_ASSETS_DIR), ec);
            return ec ? uri : rel.generic_string();
        }
        if (img.contains("bufferView")) {
            // Embedded image (typical for .glb).
            if (!registerImage) return "";
            const JsonNode& bv = arr("bufferViews").asArray()[img["bufferView"].asInt()];
            int bufIdx = bv["buffer"].asInt();
            int off    = bv.contains("byteOffset") ? bv["byteOffset"].asInt() : 0;
            int len    = bv["byteLength"].asInt();
            const Bytes& buf = buffers.at(bufIdx);
            Bytes imgBytes(buf.begin() + off, buf.begin() + off + len);
            return emitImage(imgBytes, imgIdx, srgb);
        }
        return "";
    }

    std::string emitImage(const Bytes& encoded, int imgIdx, bool srgb) const {
        std::string key = absPath + "#img" + std::to_string(imgIdx);
        return registerImage(key, encoded.data(), int(encoded.size()), srgb);
    }

    MaterialData material(int matIdx) const {
        MaterialData md;
        if (matIdx < 0 || !root->contains("materials")) return md;
        md.hasMaterial = true;
        const JsonNode& m = arr("materials").asArray()[matIdx];

        if (m.contains("pbrMetallicRoughness")) {
            const JsonNode& pbr = m["pbrMetallicRoughness"];
            if (pbr.contains("baseColorFactor")) {
                const auto& a = pbr["baseColorFactor"].asArray();
                md.albedoFactor = { a[0].asFloat(), a[1].asFloat(), a[2].asFloat(), a[3].asFloat() };
            }
            md.metallicFactor  = pbr.contains("metallicFactor")  ? pbr["metallicFactor"].asFloat()  : 1.0f;
            md.roughnessFactor = pbr.contains("roughnessFactor") ? pbr["roughnessFactor"].asFloat() : 1.0f;
            if (pbr.contains("baseColorTexture"))
                md.albedoPath = resolveTexture(pbr["baseColorTexture"]["index"].asInt(), true);
            if (pbr.contains("metallicRoughnessTexture"))
                md.metalRoughPath = resolveTexture(pbr["metallicRoughnessTexture"]["index"].asInt(), false);
        }
        if (m.contains("normalTexture")) {
            md.normalPath = resolveTexture(m["normalTexture"]["index"].asInt(), false);
            if (m["normalTexture"].contains("scale")) md.normalScale = m["normalTexture"]["scale"].asFloat();
        }
        if (m.contains("occlusionTexture"))
            md.occlusionPath = resolveTexture(m["occlusionTexture"]["index"].asInt(), false);
        if (m.contains("emissiveFactor")) {
            const auto& e = m["emissiveFactor"].asArray();
            md.emissiveFactor = { e[0].asFloat(), e[1].asFloat(), e[2].asFloat() };
        }
        if (m.contains("emissiveTexture"))
            md.emissivePath = resolveTexture(m["emissiveTexture"]["index"].asInt(), true);
        return md;
    }

    // Emit one LoadedMesh per primitive of mesh `meshIdx` in MESH-LOCAL space
    // (no baking — node placement is carried by LoadedNode::local instead).
    void emitMesh(int meshIdx, std::vector<LoadedMesh>& out) const {
        const JsonNode& mesh = arr("meshes").asArray()[meshIdx];
        for (const JsonNode& prim : mesh["primitives"].asArray()) {
            const JsonNode& attrs = prim["attributes"];
            if (!attrs.contains("POSITION")) continue;

            int comps = 0;
            std::vector<float> pos = readFloats(attrs["POSITION"].asInt(), comps);
            size_t vcount = pos.size() / 3;

            std::vector<float> nrm;
            if (attrs.contains("NORMAL")) { int c; nrm = readFloats(attrs["NORMAL"].asInt(), c); }
            std::vector<float> uv;
            if (attrs.contains("TEXCOORD_0")) { int c; uv = readFloats(attrs["TEXCOORD_0"].asInt(), c); }

            LoadedMesh lm;
            lm.vertices.resize(vcount);
            for (size_t i = 0; i < vcount; ++i) {
                Vertex& v = lm.vertices[i];
                v.position[0] = pos[i*3+0]; v.position[1] = pos[i*3+1]; v.position[2] = pos[i*3+2];
                if (!nrm.empty()) {
                    v.normal[0] = nrm[i*3+0]; v.normal[1] = nrm[i*3+1]; v.normal[2] = nrm[i*3+2];
                } else { v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1; }
                if (!uv.empty()) { v.uv[0] = uv[i*2+0]; v.uv[1] = uv[i*2+1]; }
            }

            if (prim.contains("indices")) lm.indices = readIndices(prim["indices"].asInt());
            else { lm.indices.resize(vcount); for (uint32_t i = 0; i < vcount; ++i) lm.indices[i] = i; }

            lm.material = material(prim.contains("material") ? prim["material"].asInt() : -1);
            if (mesh.contains("name")) lm.name = mesh["name"].asString();
            out.push_back(std::move(lm));
        }
    }

    // Read a node's LOCAL TRS (matrix nodes are decomposed).
    Transform nodeLocal(const JsonNode& node) const {
        if (node.contains("matrix")) {
            const auto& m = node["matrix"].asArray();
            Mat16 mm{};
            for (int i = 0; i < 16; ++i) mm[i] = m[i].asFloat();
            return decomposeMatrix(mm);
        }
        Transform t;
        if (node.contains("translation")) { const auto& a = node["translation"].asArray(); t.position = { a[0].asFloat(), a[1].asFloat(), a[2].asFloat() }; }
        if (node.contains("rotation"))    { const auto& a = node["rotation"].asArray();    t.rotation = { a[0].asFloat(), a[1].asFloat(), a[2].asFloat(), a[3].asFloat() }; }
        if (node.contains("scale"))       { const auto& a = node["scale"].asArray();       t.scale    = { a[0].asFloat(), a[1].asFloat(), a[2].asFloat() }; }
        return t;
    }

    // Depth-first build: append this node (parent precedes child), then recurse.
    void traverse(int nodeIdx, int parentIdx, GltfLoader::LoadedModel& model) const {
        const JsonNode& node = arr("nodes").asArray()[nodeIdx];
        GltfLoader::LoadedNode ln;
        if (node.contains("name")) ln.name = node["name"].asString();
        ln.local  = nodeLocal(node);
        ln.parent = parentIdx;
        if (node.contains("mesh")) emitMesh(node["mesh"].asInt(), ln.meshes);

        int myIdx = static_cast<int>(model.nodes.size());
        model.nodes.push_back(std::move(ln));

        if (node.contains("children"))
            for (const JsonNode& ch : node["children"].asArray())
                traverse(ch.asInt(), myIdx, model);
    }
};

} // namespace

namespace GltfLoader {

LoadedModel load(const std::string& absPath, const RegisterImageFn& registerImage) {
    Bytes file = readFile(absPath);
    std::string jsonText;
    Bytes glbBin;
    bool isGlb = file.size() >= 4 && rdU32(file.data()) == 0x46546C67; // "glTF"

    if (isGlb) {
        size_t p = 12;  // skip 12-byte header
        while (p + 8 <= file.size()) {
            uint32_t len  = rdU32(&file[p]);
            uint32_t type = rdU32(&file[p + 4]);
            const uint8_t* data = &file[p + 8];
            if (type == 0x4E4F534A)      jsonText.assign(reinterpret_cast<const char*>(data), len); // JSON
            else if (type == 0x004E4942) glbBin.assign(data, data + len);                            // BIN
            p += 8 + len;
        }
    } else {
        jsonText.assign(file.begin(), file.end());
    }

    JsonNode root = parseJson(jsonText.c_str());

    Ctx ctx;
    ctx.root          = &root;
    ctx.absPath       = absPath;
    ctx.gltfDir       = std::filesystem::path(absPath).parent_path().string();
    ctx.registerImage = registerImage;

    // Resolve every buffer to a byte blob.
    if (root.contains("buffers")) {
        for (const JsonNode& b : root["buffers"].asArray()) {
            if (!b.contains("uri")) {
                ctx.buffers.push_back(glbBin);               // GLB binary chunk
            } else {
                const std::string uri = b["uri"].asString();
                if (uri.rfind("data:", 0) == 0) {
                    auto comma = uri.find(',');
                    ctx.buffers.push_back(base64Decode(uri.substr(comma + 1)));
                } else {
                    ctx.buffers.push_back(readFile((std::filesystem::path(ctx.gltfDir) / uri).string()));
                }
            }
        }
    }

    LoadedModel out;

    // Traverse the default scene's node hierarchy (fallback: each mesh as a root node).
    if (root.contains("scenes")) {
        int sceneIdx = root.contains("scene") ? root["scene"].asInt() : 0;
        const JsonNode& scene = root["scenes"].asArray()[sceneIdx];
        if (scene.contains("nodes"))
            for (const JsonNode& n : scene["nodes"].asArray()) ctx.traverse(n.asInt(), -1, out);
    } else if (root.contains("meshes")) {
        for (size_t i = 0; i < root["meshes"].asArray().size(); ++i) {
            LoadedNode ln;
            ctx.emitMesh(int(i), ln.meshes);   // identity local, root
            out.nodes.push_back(std::move(ln));
        }
    }

    return out;
}

} // namespace GltfLoader
