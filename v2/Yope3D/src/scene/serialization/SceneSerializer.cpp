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

std::string load(const char* path, ecs::Registry& reg, World& world,
                 AudioSystem* audio, AssetManager* assets, bool startAudio) {
    JsonNode root;
    try {
        root = parseJsonFile(path);
    } catch (const std::exception& ex) {
        return std::string("Parse error: ") + ex.what();
    }

    auto table = buildSerTable();

    // Clear existing scene
    world.resetPhysics();

    // Gravity
    if (root.contains("gravity")) {
        auto& arr = root["gravity"].asArray();
        if (arr.size() >= 3)
            world.gravity = {arr[0].asFloat(), arr[1].asFloat(), arr[2].asFloat()};
    }

    // Exposure (older scenes without the key keep the 1.0 default).
    world.exposure = root.contains("exposure") ? root["exposure"].asFloat() : 1.0f;

    if (!root.contains("entities")) return "";

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

    // Build file-id → entity mapping for SpringConstraint cross-references.
    std::map<uint32_t, ecs::Entity> fileIdToEntity;

    for (auto& entNode : root["entities"].asArray()) {
        // Use ComponentSnapshot to recreate the entity through World factories.
        ComponentSnapshot snap;

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
            // snap.spring.target is left invalid here; the second pass resolves it
            // via the fileId cross-reference and then calls addSpringPhysics.
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

        ecs::Entity e = snap.restore(world);
        if (!reg.valid(e)) continue;

        uint32_t fid = entNode.contains("fileId") ? entNode["fileId"].asUInt() : UINT32_MAX;
        fileIdToEntity[fid] = e;
    }

    // Resolve SpringConstraint cross-references
    // (second pass after all entities are created)
    uint32_t fid = 0;
    for (auto& entNode : root["entities"].asArray()) {
        if (!entNode.contains("SpringConstraint")) { ++fid; continue; }
        auto it = fileIdToEntity.find(fid);
        if (it == fileIdToEntity.end()) { ++fid; continue; }
        ecs::Entity e = it->second;
        if (!reg.valid(e)) { ++fid; continue; }
        if (auto* sc = reg.get<ecs::SpringConstraint>(e)) {
            uint32_t targetFileId = entNode["SpringConstraint"]["targetId"].asUInt();
            auto tit = fileIdToEntity.find(targetFileId);
            if (tit != fileIdToEntity.end()) sc->target = tit->second;
        }
        ++fid;
    }

    // Resolve Parent cross-references (second pass; mirrors SpringConstraint).
    // Older scenes have no "Parent" keys, so this is a no-op for them.
    {
        uint32_t pfid = 0;
        for (auto& entNode : root["entities"].asArray()) {
            if (entNode.contains("Parent")) {
                auto it = fileIdToEntity.find(pfid);
                uint32_t parentFileId = entNode["Parent"].contains("parentId")
                                          ? entNode["Parent"]["parentId"].asUInt() : UINT32_MAX;
                if (it != fileIdToEntity.end() && reg.valid(it->second) && parentFileId != UINT32_MAX) {
                    auto pit = fileIdToEntity.find(parentFileId);
                    if (pit != fileIdToEntity.end() && reg.valid(pit->second) && !reg.has<ecs::Parent>(it->second))
                        reg.add<ecs::Parent>(it->second, ecs::Parent{pit->second});
                }
            }
            ++pfid;
        }
    }

    // Reconstruct physics springs from SpringConstraint components.
    // The component stores the logical relationship; physics::Spring objects in
    // World::springs_ do the actual simulation. After cross-ref resolution above
    // we know target is a valid entity, so we can create the spring objects now.
    for (auto [e, sc] : reg.view<ecs::SpringConstraint>()) {
        if (reg.valid(sc.target))
            world.addSpringPhysics(e, sc.target, sc.k, sc.restLength);
    }

    // Re-register embedded glTF textures. Materials from imported .glb models store
    // their maps as synthetic "<glb>#imgN" keys; the pixel data lives only in the
    // source file, so a fresh load must re-decode it before the MaterialCache can
    // resolve those keys. External-file / .mtl textures load from disk as usual.
    if (assets) {
        std::set<std::string> glbPaths;
        auto scan = [&](const char* p) {
            if (!p || !p[0]) return;
            std::string s(p);
            auto pos = s.find("#img");
            if (pos != std::string::npos) glbPaths.insert(s.substr(0, pos));
        };
        for (auto [e, mat] : reg.view<ecs::Material>()) {
            scan(mat.albedoPath);   scan(mat.normalPath); scan(mat.metalRoughPath);
            scan(mat.occlusionPath); scan(mat.emissivePath);
        }
        for (const auto& g : glbPaths) world.reregisterEmbeddedTextures(g);
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

#ifdef YOPE_EDITOR
    Console::log(std::string("Loaded scene: ") + path, LogSeverity::Info);
#endif
    return "";
}

} // namespace SceneSerializer
