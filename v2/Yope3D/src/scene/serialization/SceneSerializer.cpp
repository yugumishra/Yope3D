#include "scene/serialization/SceneSerializer.h"
#include "scene/serialization/JsonWriter.h"
#include "scene/serialization/JsonParser.h"
#include "scene/serialization/ComponentSerializers.h"
#include "scene/ComponentSnapshot.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "ecs/TypeId.h"
#include "world/World.h"
#include "world/Transform.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include "assets/AssetManager.h"
#include "assets/GltfLoader.h"
#include "Engine.h"
#ifdef YOPE_EDITOR
#include "editor/panels/ConsolePanel.h"
#endif
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <map>
#include <set>

// Table of component serializers in display order.
struct CompSerEntry {
    ecs::TypeId typeId;
    const char* name;
    void (*serialize)(const void*, JsonWriter&);
    bool (*deserialize)(const JsonNode&, void*);
};

static std::vector<CompSerEntry> buildSerTable() {
    return {
        { ecs::typeId<ecs::Name>(),                  "Name",                  compser::serializeName,                  compser::deserializeName                  },
        { ecs::typeId<Transform>(),                  "Transform",             compser::serializeTransform,             compser::deserializeTransform             },
        { ecs::typeId<ecs::Hull>(),                  "Hull",                  compser::serializeHull,                  compser::deserializeHull                  },
        { ecs::typeId<ecs::SphereForm>(),            "SphereForm",            compser::serializeSphereForm,            compser::deserializeSphereForm            },
        { ecs::typeId<ecs::AABBForm>(),              "AABBForm",              compser::serializeAABBForm,              compser::deserializeAABBForm              },
        { ecs::typeId<ecs::OBBForm>(),               "OBBForm",               compser::serializeOBBForm,               compser::deserializeOBBForm               },
        { ecs::typeId<ecs::CapsuleForm>(),           "CapsuleForm",           compser::serializeCapsuleForm,           compser::deserializeCapsuleForm           },
        { ecs::typeId<ecs::CylinderForm>(),          "CylinderForm",          compser::serializeCylinderForm,          compser::deserializeCylinderForm          },
        { ecs::typeId<ecs::CompoundCollider>(),      "CompoundCollider",      compser::serializeCompoundCollider,      compser::deserializeCompoundCollider      },
        { ecs::typeId<ecs::MeshRenderer>(),          "MeshRenderer",          compser::serializeMeshRenderer,          compser::deserializeMeshRenderer          },
        { ecs::typeId<ecs::Material>(),              "Material",              compser::serializeMaterial,              compser::deserializeMaterial              },
        { ecs::typeId<ecs::LightSource>(),           "LightSource",           compser::serializeLightSource,           compser::deserializeLightSource           },
        { ecs::typeId<ecs::SpringConstraint>(),      "SpringConstraint",      compser::serializeSpringConstraint,      compser::deserializeSpringConstraint      },
        { ecs::typeId<ecs::Parent>(),                "Parent",                compser::serializeParent,                compser::deserializeParent                },
        { ecs::typeId<ecs::AudioSource>(),           "AudioSource",           compser::serializeAudioSource,           compser::deserializeAudioSource           },
        { ecs::typeId<ecs::ScriptComponent>(),       "ScriptComponent",       compser::serializeScriptComponent,       compser::deserializeScriptComponent       },
        { ecs::typeId<ecs::UITransform>(),           "UITransform",           compser::serializeUITransform,           compser::deserializeUITransform           },
        { ecs::typeId<ecs::UIBackground>(),          "UIBackground",          compser::serializeUIBackground,          compser::deserializeUIBackground          },
        { ecs::typeId<ecs::UITexturedBackground>(),  "UITexturedBackground",  compser::serializeUITexturedBackground,  compser::deserializeUITexturedBackground  },
        { ecs::typeId<ecs::UICurvedBackground>(),    "UICurvedBackground",    compser::serializeUICurvedBackground,    compser::deserializeUICurvedBackground    },
        { ecs::typeId<ecs::UIText>(),                "UIText",                compser::serializeUIText,                compser::deserializeUIText                },
        { ecs::typeId<ecs::TextLabel3D>(),           "TextLabel3D",           compser::serializeTextLabel3D,           compser::deserializeTextLabel3D           },
    };
}

namespace SceneSerializer {

bool save(const char* path, ecs::Registry& reg, World& world) {
    auto table = buildSerTable();

    JsonWriter w;
    w.beginObject();

    w.writeInt("version", 1);

    // World settings
    w.writeFloat3("gravity", world.gravity.x, world.gravity.y, world.gravity.z);
    w.writeFloat("exposure", world.exposure);

    // Build a runtime-id → fileId map so SpringConstraint can write the
    // target's *fileId* (stable across runs) instead of its runtime ID
    // (which is regenerated on load).
    std::map<uint32_t, uint32_t> runtimeToFile;
    {
        uint32_t fid = 0;
        for (auto [e, _sel] : reg.view<ecs::EditorSelectable>())
            runtimeToFile[e.id] = fid++;
    }

    // Bulky custom-mesh geometry goes to a binary sidecar (<scene>.meshbin), not
    // the JSON — a scene of high-poly meshes would be gigabytes of text otherwise.
    // Blobs are written in entity-iteration order; the JSON stores only per-mesh
    // {vc, ic} counts, and load reads the sidecar sequentially in the same order.
    std::string binPath = std::filesystem::path(path).replace_extension(".meshbin").string();
    std::ofstream bin(binPath, std::ios::binary);
    if (bin) { const char hdr[8] = {'Y','S','M','B', 1, 0, 0, 0}; bin.write(hdr, 8); }

    // Entities
    w.beginArray("entities");
    uint32_t fileId = 0;
    for (auto [e, _sel] : reg.view<ecs::EditorSelectable>()) {
        w.beginArrayObject();
        w.writeUInt("fileId", fileId++);
        w.writeUInt("runtimeId", e.id);

        // Tag: fixed
        if (reg.has<ecs::Fixed>(e)) w.writeBool("isFixed", true);

        // Components
        for (auto& entry : table) {
            void* comp = reg.getRaw(e, entry.typeId);
            if (!comp) continue;
            w.writeKey(entry.name);
            w.beginObject();
            entry.serialize(comp, w);
            // SpringConstraint's target reference needs the save-loop's
            // runtime→file mapping, which the generic table can't see.
            if (entry.typeId == ecs::typeId<ecs::SpringConstraint>()) {
                auto* sc = static_cast<const ecs::SpringConstraint*>(comp);
                auto it = runtimeToFile.find(sc->target.id);
                w.writeUInt("targetId", it != runtimeToFile.end() ? it->second : UINT32_MAX);
            }
            // Parent's parent Entity — same fileId cross-reference as SpringConstraint.
            if (entry.typeId == ecs::typeId<ecs::Parent>()) {
                auto* p = static_cast<const ecs::Parent*>(comp);
                auto it = runtimeToFile.find(p->parent.id);
                w.writeUInt("parentId", it != runtimeToFile.end() ? it->second : UINT32_MAX);
            }
            // Custom-mesh geometry → binary sidecar; JSON keeps only the counts.
            if (entry.typeId == ecs::typeId<ecs::MeshRenderer>()) {
                auto* mr = static_cast<const ecs::MeshRenderer*>(comp);
                if (bin && mr->mesh && mr->mesh->primitiveType == PrimitiveType::Custom
                    && mr->mesh->sourcePath.empty() && !mr->mesh->cpuVertices.empty()) {
                    const auto& verts = mr->mesh->cpuVertices;
                    const auto& inds  = mr->mesh->cpuIndices;
                    bin.write(reinterpret_cast<const char*>(verts.data()),
                              static_cast<std::streamsize>(verts.size() * sizeof(Vertex)));
                    bin.write(reinterpret_cast<const char*>(inds.data()),
                              static_cast<std::streamsize>(inds.size() * sizeof(uint32_t)));
                    w.writeKey("geom");
                    w.beginObject();
                    w.writeUInt("vc", static_cast<unsigned>(verts.size()));
                    w.writeUInt("ic", static_cast<unsigned>(inds.size()));
                    w.endObject();
                }
            }
            w.endObject();
        }

        w.endObject();  // entity
    }
    w.endArray();  // entities

    w.endObject();  // root

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << w.str();
#ifdef YOPE_EDITOR
    Console::log(std::string("Saved scene: ") + path, LogSeverity::Info);
#endif
    return true;
}

// ---------------------------------------------------------------------------
// Parse (background-thread safe: no World / registry / GPU access).
// ---------------------------------------------------------------------------

ParsedScene parseScene(const char* path) {
    ParsedScene out;

    JsonNode root;
    try {
        root = parseJsonFile(path);
    } catch (const std::exception& ex) {
        out.error = std::string("Parse error: ") + ex.what();
        return out;
    }

    // Gravity
    if (root.contains("gravity")) {
        auto& arr = root["gravity"].asArray();
        if (arr.size() >= 3) {
            out.hasGravity = true;
            out.gravity = {arr[0].asFloat(), arr[1].asFloat(), arr[2].asFloat()};
        }
    }

    // Exposure (older scenes without the key keep the 1.0 default).
    out.exposure = root.contains("exposure") ? root["exposure"].asFloat() : 1.0f;

    if (!root.contains("entities")) { out.ok = true; return out; }

    // Open the geometry sidecar (<scene>.meshbin). Blobs are read sequentially in
    // the same entity order they were written; missing/short reads fall back to a
    // primitive (no crash), so pre-sidecar scenes just show placeholder cubes.
    std::string binPath = std::filesystem::path(path).replace_extension(".meshbin").string();
    std::ifstream meshBin(binPath, std::ios::binary);
    bool meshBinOk = false;
    if (meshBin.is_open()) {
        char hdr[8] = {};
        meshBin.read(hdr, 8);
        meshBinOk = meshBin.gcount() == 8 &&
                    hdr[0] == 'Y' && hdr[1] == 'S' && hdr[2] == 'M' && hdr[3] == 'B';
    }

    for (auto& entNode : root["entities"].asArray()) {
        ParsedScene::Ent ent;
        ComponentSnapshot& snap = ent.snap;

        if (entNode.contains("Transform")) {
            snap.hasTransform = true;
            compser::deserializeTransform(entNode["Transform"], &snap.transform);
        }
        if (entNode.contains("Hull")) {
            snap.hasHull = true;
            compser::deserializeHull(entNode["Hull"], &snap.hull);
        }
        if (entNode.contains("isFixed") && entNode["isFixed"].asBool())
            snap.hasFixed = true;
        if (entNode.contains("SphereForm")) {
            snap.hasSphere = true;
            compser::deserializeSphereForm(entNode["SphereForm"], &snap.sphere);
        }
        if (entNode.contains("AABBForm")) {
            snap.hasAABB = true;
            compser::deserializeAABBForm(entNode["AABBForm"], &snap.aabb);
        }
        if (entNode.contains("OBBForm")) {
            snap.hasOBB = true;
            compser::deserializeOBBForm(entNode["OBBForm"], &snap.obb);
        }
        if (entNode.contains("CapsuleForm")) {
            snap.hasCapsule = true;
            compser::deserializeCapsuleForm(entNode["CapsuleForm"], &snap.capsule);
        }
        if (entNode.contains("CylinderForm")) {
            snap.hasCylinder = true;
            compser::deserializeCylinderForm(entNode["CylinderForm"], &snap.cylinder);
        }
        if (entNode.contains("CompoundCollider")) {
            snap.hasCompoundCollider = true;
            compser::deserializeCompoundCollider(entNode["CompoundCollider"], &snap.compoundCollider);
        }
        if (entNode.contains("LightSource")) {
            snap.hasLight = true;
            compser::deserializeLightSource(entNode["LightSource"], &snap.light);
        }
        if (entNode.contains("Name")) {
            snap.hasName = true;
            compser::deserializeName(entNode["Name"], &snap.name);
        }
        if (entNode.contains("AudioSource")) {
            snap.hasAudio = true;
            compser::deserializeAudioSource(entNode["AudioSource"], &snap.audio);
        }
        if (entNode.contains("ScriptComponent")) {
            snap.hasScript = true;
            compser::deserializeScriptComponent(entNode["ScriptComponent"], &snap.script);
        }
        if (entNode.contains("UITransform")) {
            snap.hasUITransform = true;
            compser::deserializeUITransform(entNode["UITransform"], &snap.uiTransform);
        }
        if (entNode.contains("UIBackground")) {
            snap.hasUIBackground = true;
            compser::deserializeUIBackground(entNode["UIBackground"], &snap.uiBackground);
        }
        if (entNode.contains("UITexturedBackground")) {
            snap.hasUITexturedBackground = true;
            compser::deserializeUITexturedBackground(entNode["UITexturedBackground"], &snap.uiTexturedBackground);
        }
        if (entNode.contains("UICurvedBackground")) {
            snap.hasUICurvedBackground = true;
            compser::deserializeUICurvedBackground(entNode["UICurvedBackground"], &snap.uiCurvedBackground);
        }
        if (entNode.contains("UIText")) {
            snap.hasUIText = true;
            compser::deserializeUIText(entNode["UIText"], &snap.uiText);
        }
        if (entNode.contains("TextLabel3D")) {
            snap.hasTextLabel3D = true;
            compser::deserializeTextLabel3D(entNode["TextLabel3D"], &snap.textLabel3D);
        }
        if (entNode.contains("Material")) {
            snap.hasMaterial = true;
            compser::deserializeMaterial(entNode["Material"], &snap.material);
        }
        if (entNode.contains("SpringConstraint")) {
            snap.hasSpring = true;
            compser::deserializeSpringConstraint(entNode["SpringConstraint"], &snap.spring);
            // snap.spring.target is left invalid here; commitFinalize resolves it
            // via the fileId cross-reference and then calls addSpringPhysics.
            ent.hasSpringTarget    = true;
            ent.springTargetFileId = entNode["SpringConstraint"].contains("targetId")
                                       ? entNode["SpringConstraint"]["targetId"].asUInt() : UINT32_MAX;
        }
        // Parent link (resolved to an ecs::Parent component in commitFinalize).
        if (entNode.contains("Parent")) {
            ent.hasParentLink = true;
            ent.parentFileId  = entNode["Parent"].contains("parentId")
                                  ? entNode["Parent"]["parentId"].asUInt() : UINT32_MAX;
        }
        if (entNode.contains("MeshRenderer")) {
            snap.hasMesh = true;
            const auto& mr = entNode["MeshRenderer"];
            if (mr.contains("color")) {
                auto& c = mr["color"].asArray();
                if (c.size() >= 3) { snap.meshColor[0] = c[0].asFloat(); snap.meshColor[1] = c[1].asFloat(); snap.meshColor[2] = c[2].asFloat(); }
            }
            if (mr.contains("primitiveType"))   snap.primType = static_cast<PrimitiveType>(mr["primitiveType"].asInt());
            if (mr.contains("primitiveExtents")) {
                auto& pe = mr["primitiveExtents"].asArray();
                if (pe.size() >= 3) snap.primExtents = {pe[0].asFloat(), pe[1].asFloat(), pe[2].asFloat()};
            }
            // Custom mesh: load from source path (reference-based serialization).
            // Legacy scenes with packed vertex/index arrays are also handled for
            // backwards compatibility.
            if (snap.primType == PrimitiveType::Custom) {
                if (mr.contains("sourcePath") && mr["sourcePath"].isString())
                    snap.meshSourcePath = mr["sourcePath"].asString();
                else if (mr.contains("geom") && meshBinOk) {
                    // Read this mesh's blob from the sidecar (sequential, in order).
                    const auto& g = mr["geom"];
                    uint32_t vc = g.contains("vc") ? g["vc"].asUInt() : 0;
                    uint32_t ic = g.contains("ic") ? g["ic"].asUInt() : 0;
                    snap.cpuVerts.resize(vc);
                    snap.cpuInds.resize(ic);
                    if (vc) meshBin.read(reinterpret_cast<char*>(snap.cpuVerts.data()),
                                         static_cast<std::streamsize>(vc * sizeof(Vertex)));
                    if (ic) meshBin.read(reinterpret_cast<char*>(snap.cpuInds.data()),
                                         static_cast<std::streamsize>(ic * sizeof(uint32_t)));
                    if (!meshBin) { snap.cpuVerts.clear(); snap.cpuInds.clear(); }  // short read → fallback
                }
                else if (mr.contains("vertices") && mr.contains("indices")) {
                    const auto& varr = mr["vertices"].asArray();
                    snap.cpuVerts.clear();
                    for (size_t i = 0; i + 7 < varr.size(); i += 8) {
                        Vertex v{};
                        v.position[0] = varr[i+0].asFloat(); v.position[1] = varr[i+1].asFloat(); v.position[2] = varr[i+2].asFloat();
                        v.normal[0]   = varr[i+3].asFloat(); v.normal[1]   = varr[i+4].asFloat(); v.normal[2]   = varr[i+5].asFloat();
                        v.uv[0]       = varr[i+6].asFloat(); v.uv[1]       = varr[i+7].asFloat();
                        snap.cpuVerts.push_back(v);
                    }
                    const auto& iarr = mr["indices"].asArray();
                    snap.cpuInds.clear();
                    snap.cpuInds.reserve(iarr.size());
                    for (const auto& idx : iarr)
                        snap.cpuInds.push_back(static_cast<uint32_t>(idx.asInt()));
                }
            }
        }

        ent.fileId = entNode.contains("fileId") ? entNode["fileId"].asUInt() : UINT32_MAX;
        out.entities.push_back(std::move(ent));
    }

    // Collect embedded glTF images off-thread. Materials imported from a .glb store
    // their maps as synthetic "<glb>#imgN" keys whose pixel data lives only in the
    // source file; scan the parsed material snapshots for those keys and re-run the
    // (geometry-free, GPU-free) glTF loader purely to capture the encoded image
    // bytes. commitFinalize enqueues them for decode on the main thread — the
    // enqueue itself must not touch the AssetManager cache from this worker thread.
    {
        std::set<std::string> glbPaths;
        auto scan = [&](const char* p) {
            if (!p || !p[0]) return;
            std::string s(p);
            auto pos = s.find("#img");
            if (pos != std::string::npos) glbPaths.insert(s.substr(0, pos));
        };
        for (const auto& ent : out.entities) {
            if (!ent.snap.hasMaterial) continue;
            const auto& mat = ent.snap.material;
            scan(mat.albedoPath);   scan(mat.normalPath); scan(mat.metalRoughPath);
            scan(mat.occlusionPath); scan(mat.emissivePath);
        }
        for (const auto& g : glbPaths) {
            GltfLoader::RegisterImageFn collect =
                [&out](const std::string& key, const uint8_t* data, int len, bool srgb) -> std::string {
                    ParsedScene::GlbImage img;
                    img.key  = key;
                    img.srgb = srgb;
                    img.bytes.assign(data, data + (len > 0 ? len : 0));
                    out.glbImages.push_back(std::move(img));
                    return key;
                };
            try { GltfLoader::load(g, collect); } catch (...) {}
        }
    }

    out.ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// Commit (main thread: mutates the registry + issues GPU uploads).
// ---------------------------------------------------------------------------

void commitBegin(ParsedScene& ps, World& world) {
    if (ps.begun) return;
    world.resetPhysics();
    if (ps.hasGravity) world.gravity = ps.gravity;
    world.exposure = ps.exposure;
    ps.cursor = 0;
    ps.fileIdToEntity.clear();
    ps.begun = true;
}

size_t commitEntities(ParsedScene& ps, World& world, size_t maxEntities) {
    ecs::Registry& reg = world.getRegistry();
    size_t done = 0;
    for (; ps.cursor < ps.entities.size() && done < maxEntities; ++ps.cursor, ++done) {
        ParsedScene::Ent& ent = ps.entities[ps.cursor];
        ecs::Entity e = ent.snap.restore(world);
        if (reg.valid(e)) ps.fileIdToEntity[ent.fileId] = e;
    }
    return done;
}

void commitFinalize(ParsedScene& ps, World& world,
                    AudioSystem* audio, AssetManager* assets, bool startAudio) {
    ecs::Registry& reg = world.getRegistry();

    // Resolve SpringConstraint cross-references (target fileId → entity).
    for (const auto& ent : ps.entities) {
        if (!ent.hasSpringTarget) continue;
        auto it = ps.fileIdToEntity.find(ent.fileId);
        if (it == ps.fileIdToEntity.end() || !reg.valid(it->second)) continue;
        if (auto* sc = reg.get<ecs::SpringConstraint>(it->second)) {
            auto tit = ps.fileIdToEntity.find(ent.springTargetFileId);
            if (tit != ps.fileIdToEntity.end()) sc->target = tit->second;
        }
    }

    // Resolve Parent cross-references (mirrors SpringConstraint). Older scenes have
    // no "Parent" keys, so this is a no-op for them.
    for (const auto& ent : ps.entities) {
        if (!ent.hasParentLink) continue;
        auto it = ps.fileIdToEntity.find(ent.fileId);
        if (it == ps.fileIdToEntity.end() || !reg.valid(it->second)) continue;
        if (ent.parentFileId == UINT32_MAX) continue;
        auto pit = ps.fileIdToEntity.find(ent.parentFileId);
        if (pit != ps.fileIdToEntity.end() && reg.valid(pit->second) && !reg.has<ecs::Parent>(it->second))
            reg.add<ecs::Parent>(it->second, ecs::Parent{pit->second});
    }

    // Reconstruct physics springs from resolved SpringConstraint components.
    for (auto [e, sc] : reg.view<ecs::SpringConstraint>()) {
        if (reg.valid(sc.target))
            world.addSpringPhysics(e, sc.target, sc.k, sc.restLength);
    }

    // Enqueue embedded glTF textures collected during parseScene (main-thread:
    // enqueueTextureDecode reads the AssetManager cache, which the texture-streaming
    // pump mutates, so it must not run on the parse worker).
    if (assets) {
        for (const auto& img : ps.glbImages)
            assets->enqueueTextureDecode(img.key, img.srgb,
                                         img.bytes.data(), static_cast<int>(img.bytes.size()));
    }

    // Rebind AudioSource OpenAL handles from the saved path.
    if (audio) {
        for (auto [e, as] : reg.view<ecs::AudioSource>()) {
            if (as.source || as.path[0] == 0) continue;
            if (auto* sb = audio->loadSound(as.path)) {
                as.source = audio->createSource(sb);
                if (as.source) {
                    as.source->setGain(as.gain);
                    as.source->setPitch(as.pitch);
                    as.source->enableLooping(as.loop);
                    if (as.autoplay && startAudio) as.source->play();
                }
            }
        }
    }

    // Rebind UITexturedBackground GPU textures from the saved path.
    if (assets) {
        for (auto [e, tbg] : reg.view<ecs::UITexturedBackground>()) {
            if (tbg.texture || tbg.path[0] == 0) continue;
            tbg.texture = assets->loadTexture(tbg.path);
        }
    }
}

std::string load(const char* path, ecs::Registry& /*reg*/, World& world,
                 AudioSystem* audio, AssetManager* assets, bool startAudio) {
    ParsedScene ps = parseScene(path);
    if (!ps.ok) return ps.error;

    commitBegin(ps, world);
    commitEntities(ps, world, ps.entities.size());
    commitFinalize(ps, world, audio, assets, startAudio);

#ifdef YOPE_EDITOR
    Console::log(std::string("Loaded scene: ") + path, LogSeverity::Info);
#endif
    return "";
}

} // namespace SceneSerializer
