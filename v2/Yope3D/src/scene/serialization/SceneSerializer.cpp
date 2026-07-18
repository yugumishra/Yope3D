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
#include "world/TransformHierarchy.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include "assets/AssetManager.h"
#include "assets/AssetResolve.h"
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
#include <unordered_map>
#include <unordered_set>

// Custom meshes with fewer vertices than this are inlined as JSON arrays
// instead of going to the .meshbin sidecar — cheap enough that a second
// file on disk (and the sequential-read coupling to entity order) isn't worth it.
static constexpr size_t kMeshBinVertexThreshold = 64;

// A template reference chain deeper than this is almost certainly a mistake
// (or a cycle the path-stack check somehow missed) — a hard backstop so a
// pathological (non-cyclic) chain fails fast instead of blowing the stack.
static constexpr int kMaxTemplateDepth = 32;

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
        { ecs::typeId<ecs::TemplateInstance>(),      "TemplateInstance",      compser::serializeTemplateInstance,      compser::deserializeTemplateInstance      },
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
        { ecs::typeId<ecs::PointJointConstraint>(),  "PointJointConstraint",  compser::serializePointJointConstraint,  compser::deserializePointJointConstraint  },
        { ecs::typeId<ecs::HingeJointConstraint>(),  "HingeJointConstraint",  compser::serializeHingeJointConstraint,  compser::deserializeHingeJointConstraint  },
        { ecs::typeId<ecs::ConeTwistJointConstraint>(), "ConeTwistJointConstraint", compser::serializeConeTwistJointConstraint, compser::deserializeConeTwistJointConstraint },
        { ecs::typeId<ecs::Parent>(),                "Parent",                compser::serializeParent,                compser::deserializeParent                },
        { ecs::typeId<ecs::AudioSource>(),           "AudioSource",           compser::serializeAudioSource,           compser::deserializeAudioSource           },
        { ecs::typeId<ecs::ScriptComponent>(),       "ScriptComponent",       compser::serializeScriptComponent,       compser::deserializeScriptComponent       },
        { ecs::typeId<ecs::UITransform>(),           "UITransform",           compser::serializeUITransform,           compser::deserializeUITransform           },
        { ecs::typeId<ecs::UIBackground>(),          "UIBackground",          compser::serializeUIBackground,          compser::deserializeUIBackground          },
        { ecs::typeId<ecs::UITexturedBackground>(),  "UITexturedBackground",  compser::serializeUITexturedBackground,  compser::deserializeUITexturedBackground  },
        { ecs::typeId<ecs::UICurvedBackground>(),    "UICurvedBackground",    compser::serializeUICurvedBackground,    compser::deserializeUICurvedBackground    },
        { ecs::typeId<ecs::UIText>(),                "UIText",                compser::serializeUIText,                compser::deserializeUIText                },
        { ecs::typeId<ecs::UIButton>(),              "UIButton",              compser::serializeUIButton,              compser::deserializeUIButton              },
        { ecs::typeId<ecs::TextLabel3D>(),           "TextLabel3D",           compser::serializeTextLabel3D,           compser::deserializeTextLabel3D           },
        { ecs::typeId<ecs::AnimationPlayer>(),       "AnimationPlayer",       compser::serializeAnimationPlayer,       compser::deserializeAnimationPlayer       },
    };
}

namespace SceneSerializer {

// Compares two ComponentSnapshots' has-flags (never the values themselves) —
// the structural-divergence gate for TemplateInstance-aware saving: "did this
// live instance add/remove a child or a component relative to its base
// template," as opposed to merely changing a field's value (which overrides
// handle). hasTemplateInstance is intentionally excluded — a nested
// instance's own provenance marker isn't part of its "shape."
static bool snapshotHasSameShape(const ComponentSnapshot& a, const ComponentSnapshot& b) {
    return a.hasTransform == b.hasTransform && a.hasHull == b.hasHull && a.hasFixed == b.hasFixed &&
           a.hasSphere == b.hasSphere && a.hasAABB == b.hasAABB && a.hasOBB == b.hasOBB &&
           a.hasCapsule == b.hasCapsule && a.hasCylinder == b.hasCylinder &&
           a.hasCompoundCollider == b.hasCompoundCollider && a.hasLight == b.hasLight &&
           a.hasName == b.hasName && a.hasAudio == b.hasAudio && a.hasScript == b.hasScript &&
           a.hasSpring == b.hasSpring && a.hasPointJoint == b.hasPointJoint &&
           a.hasHingeJoint == b.hasHingeJoint && a.hasConeTwistJoint == b.hasConeTwistJoint &&
           a.hasParent == b.hasParent && a.hasMesh == b.hasMesh && a.hasMaterial == b.hasMaterial &&
           a.hasUITransform == b.hasUITransform && a.hasUIBackground == b.hasUIBackground &&
           a.hasUITexturedBackground == b.hasUITexturedBackground &&
           a.hasUICurvedBackground == b.hasUICurvedBackground && a.hasUIText == b.hasUIText &&
           a.hasUIButton == b.hasUIButton && a.hasTextLabel3D == b.hasTextLabel3D &&
           a.hasAnimationPlayer == b.hasAnimationPlayer;
}

// liveSubtree/baseEntities must both be parent-before-child, root-first order
// (collectSubtree's contract, matching how a template was itself saved) —
// compared positionally, entity-count first.
static bool sameShape(ecs::Registry& reg, World& world,
                      const std::vector<ecs::Entity>& liveSubtree,
                      const std::vector<ParsedScene::Ent>& baseEntities) {
    if (liveSubtree.size() != baseEntities.size()) return false;
    for (size_t i = 0; i < liveSubtree.size(); ++i) {
        ComponentSnapshot liveSnap = snapshotEntity(liveSubtree[i], reg, world);
        if (!snapshotHasSameShape(liveSnap, baseEntities[i].snap)) return false;
    }
    return true;
}

// Serializes just the components on `live` whose value differs from `base`'s
// corresponding component into ov, as {"CompName": {...}} entries — the
// override computation for "Save as Template"/"Save Scene" re-saving a live
// TemplateInstance. Comparison is by serialized-JSON-string equality (reuses
// the ordinary compser::serializeX functions verbatim — no per-component
// equality operators needed). Only components present on `live` are
// considered; identical ones are omitted (inherit from base).
static void computeOverrides(const ComponentSnapshot& live, const ComponentSnapshot& base, JsonWriter& ov) {
    auto diff = [&](bool liveHas, bool baseHas, const char* name,
                    const void* livePtr, const void* basePtr,
                    void (*serialize)(const void*, JsonWriter&)) {
        if (!liveHas) return;
        JsonWriter lw; lw.beginObject(); serialize(livePtr, lw); lw.endObject();
        std::string liveJson = lw.str(), baseJson;
        if (baseHas) { JsonWriter bw; bw.beginObject(); serialize(basePtr, bw); bw.endObject(); baseJson = bw.str(); }
        if (liveJson == baseJson) return;
        ov.writeKey(name);
        ov.beginObject();
        serialize(livePtr, ov);
        ov.endObject();
    };

    diff(live.hasTransform, base.hasTransform, "Transform", &live.transform, &base.transform, compser::serializeTransform);
    diff(live.hasHull, base.hasHull, "Hull", &live.hull, &base.hull, compser::serializeHull);
    diff(live.hasSphere, base.hasSphere, "SphereForm", &live.sphere, &base.sphere, compser::serializeSphereForm);
    diff(live.hasAABB, base.hasAABB, "AABBForm", &live.aabb, &base.aabb, compser::serializeAABBForm);
    diff(live.hasOBB, base.hasOBB, "OBBForm", &live.obb, &base.obb, compser::serializeOBBForm);
    diff(live.hasCapsule, base.hasCapsule, "CapsuleForm", &live.capsule, &base.capsule, compser::serializeCapsuleForm);
    diff(live.hasCylinder, base.hasCylinder, "CylinderForm", &live.cylinder, &base.cylinder, compser::serializeCylinderForm);
    diff(live.hasLight, base.hasLight, "LightSource", &live.light, &base.light, compser::serializeLightSource);
    diff(live.hasName, base.hasName, "Name", &live.name, &base.name, compser::serializeName);
    diff(live.hasAudio, base.hasAudio, "AudioSource", &live.audio, &base.audio, compser::serializeAudioSource);
    diff(live.hasScript, base.hasScript, "ScriptComponent", &live.script, &base.script, compser::serializeScriptComponent);
    diff(live.hasMaterial, base.hasMaterial, "Material", &live.material, &base.material, compser::serializeMaterial);
    diff(live.hasUITransform, base.hasUITransform, "UITransform", &live.uiTransform, &base.uiTransform, compser::serializeUITransform);
    diff(live.hasUIBackground, base.hasUIBackground, "UIBackground", &live.uiBackground, &base.uiBackground, compser::serializeUIBackground);
    diff(live.hasUITexturedBackground, base.hasUITexturedBackground, "UITexturedBackground", &live.uiTexturedBackground, &base.uiTexturedBackground, compser::serializeUITexturedBackground);
    diff(live.hasUICurvedBackground, base.hasUICurvedBackground, "UICurvedBackground", &live.uiCurvedBackground, &base.uiCurvedBackground, compser::serializeUICurvedBackground);
    diff(live.hasUIText, base.hasUIText, "UIText", &live.uiText, &base.uiText, compser::serializeUIText);
    diff(live.hasUIButton, base.hasUIButton, "UIButton", &live.uiButton, &base.uiButton, compser::serializeUIButton);
    diff(live.hasTextLabel3D, base.hasTextLabel3D, "TextLabel3D", &live.textLabel3D, &base.textLabel3D, compser::serializeTextLabel3D);
    diff(live.hasAnimationPlayer, base.hasAnimationPlayer, "AnimationPlayer", &live.animationPlayer, &base.animationPlayer, compser::serializeAnimationPlayer);
    // Spring/Joint overrides deliberately excluded — see applyComponentOverrides.

    // MeshRenderer: bespoke, not via the generic diff() lambda above —
    // ComponentSnapshot stores mesh data as top-level fields (meshColor/
    // primType/primExtents), not an ecs::MeshRenderer-shaped struct, so there's
    // no single pointer to hand compser::serializeMeshRenderer (which anyway
    // reads from a live RenderMesh*, not a snapshot). Custom-mesh geometry
    // (cpuVerts/cpuInds/meshSourcePath) is intentionally not diffed/overridden
    // here — re-pointing a template instance at different mesh data is rare
    // enough to not need v1 support; primitive shape/size/color covers the
    // common "recolor/resize this instance" case.
    if (live.hasMesh) {
        bool colorDiff = !base.hasMesh ||
            live.meshColor[0] != base.meshColor[0] || live.meshColor[1] != base.meshColor[1] || live.meshColor[2] != base.meshColor[2];
        bool typeDiff = !base.hasMesh || live.primType != base.primType;
        bool extentsDiff = !base.hasMesh ||
            live.primExtents.x != base.primExtents.x || live.primExtents.y != base.primExtents.y || live.primExtents.z != base.primExtents.z;
        if (colorDiff || typeDiff || extentsDiff) {
            ov.writeKey("MeshRenderer");
            ov.beginObject();
            ov.writeFloat3("color", live.meshColor[0], live.meshColor[1], live.meshColor[2]);
            ov.writeInt("primitiveType", static_cast<int>(live.primType));
            ov.writeFloat3("primitiveExtents", live.primExtents.x, live.primExtents.y, live.primExtents.z);
            ov.endObject();
        }
    }
}

// Writes the "entities" JSON array for exactly the given entity list (order
// determines fileId, 0..N-1) plus any bulky custom-mesh geometry to the
// .meshbin sidecar at binPath. Shared by whole-scene save() and template
// saveEntities() — the only difference between them is what surrounds this
// array (world-settings keys or not) and what exemptRoot is (see below).
//
// exemptRoot: never treated as a template reference even if it carries
// ecs::TemplateInstance — save() passes NullEntity (a scene should always
// save an instance placed in it as a templateRef, no exemption); saveEntities()
// passes the subtree root being saved (entities.front(), by "Save as
// Template"'s convention) so "save this instance as a new template" actually
// captures its current state instead of just re-emitting a reference to the
// template it already came from.
static bool writeEntitiesArray(JsonWriter& w, const std::vector<ecs::Entity>& entities,
                               ecs::Registry& reg, World& world, const std::string& binPath,
                               ecs::Entity exemptRoot) {
    auto table = buildSerTable();

    // Build a runtime-id → fileId map so SpringConstraint (and Parent/other
    // joints) can write the target's *fileId* (stable across runs) instead of
    // its runtime ID (which is regenerated on load).
    std::map<uint32_t, uint32_t> runtimeToFile;
    {
        uint32_t fid = 0;
        for (ecs::Entity e : entities) runtimeToFile[e.id] = fid++;
    }

    // Bulky custom-mesh geometry (>= kMeshBinVertexThreshold verts) goes to a binary
    // sidecar (<scene>.meshbin), not the JSON — a scene of high-poly meshes would be
    // gigabytes of text otherwise. Blobs are written in entity-iteration order; the
    // JSON stores only per-mesh {vc, ic} counts, and load reads the sidecar
    // sequentially in the same order. Primitives and tiny custom meshes never touch
    // it, and it's opened lazily so scenes with nothing bulky don't get one at all.
    std::ofstream bin;
    auto ensureBinOpen = [&]() -> std::ofstream& {
        if (!bin.is_open()) {
            bin.open(binPath, std::ios::binary);
            if (bin) { const char hdr[8] = {'Y','S','M','B', 1, 0, 0, 0}; bin.write(hdr, 8); }
        }
        return bin;
    };

    // Entities whose subtree is implied by an earlier templateRef node emitted
    // below, and must not also be independently inlined.
    std::unordered_set<uint32_t> skip;

    // Entities
    w.beginArray("entities");
    for (ecs::Entity e : entities) {
        if (skip.count(e.id)) continue;

        // Template-instance root: emit a reference instead of inlining, unless
        // this is the exempt root or its live subtree has structurally
        // diverged from its base template (a child/component added or
        // removed — a value-only change is exactly what overrides capture).
        ecs::TemplateInstance* ti = (e == exemptRoot) ? nullptr : reg.get<ecs::TemplateInstance>(e);
        if (ti) {
            std::vector<ecs::Entity> instSub;
            hierarchy::collectSubtree(reg, e, instSub);
            ParsedScene base = parseScene(ti->sourcePath);
            if (base.ok && !base.entities.empty() && sameShape(reg, world, instSub, base.entities)) {
                w.beginArrayObject();
                w.writeUInt("fileId", runtimeToFile[e.id]);
                w.writeString("templateRef", ti->sourcePath);
                if (auto* p = reg.get<ecs::Parent>(e)) {
                    auto pit = runtimeToFile.find(p->parent.id);
                    w.writeUInt("parentId", pit != runtimeToFile.end() ? pit->second : UINT32_MAX);
                }
                if (auto* tf = reg.get<Transform>(e)) {
                    w.writeKey("Transform");
                    w.beginObject();
                    compser::serializeTransform(tf, w);
                    w.endObject();
                }
                ComponentSnapshot liveRootSnap = snapshotEntity(e, reg, world);
                w.writeKey("overrides");
                w.beginObject();
                computeOverrides(liveRootSnap, base.entities[0].snap, w);
                w.endObject();
                w.endObject();  // entity

                for (ecs::Entity d : instSub) if (d.id != e.id) skip.insert(d.id);
                continue;
            }
#ifdef YOPE_EDITOR
            Console::log(std::string("Entity diverged from its template (") + ti->sourcePath +
                        ") -- saved as a plain subtree; Unpack to make this permanent and silence this warning.",
                        LogSeverity::Warning);
#endif
            // fall through: inline this entity + its subtree normally, same as any plain entity.
        }

        w.beginArrayObject();
        w.writeUInt("fileId", runtimeToFile[e.id]);
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
            // PointJointConstraint's target reference — same reasoning as SpringConstraint's.
            if (entry.typeId == ecs::typeId<ecs::PointJointConstraint>()) {
                auto* pj = static_cast<const ecs::PointJointConstraint*>(comp);
                auto it = runtimeToFile.find(pj->target.id);
                w.writeUInt("targetId", it != runtimeToFile.end() ? it->second : UINT32_MAX);
            }
            if (entry.typeId == ecs::typeId<ecs::HingeJointConstraint>()) {
                auto* hj = static_cast<const ecs::HingeJointConstraint*>(comp);
                auto it = runtimeToFile.find(hj->target.id);
                w.writeUInt("targetId", it != runtimeToFile.end() ? it->second : UINT32_MAX);
            }
            if (entry.typeId == ecs::typeId<ecs::ConeTwistJointConstraint>()) {
                auto* cj = static_cast<const ecs::ConeTwistJointConstraint*>(comp);
                auto it = runtimeToFile.find(cj->target.id);
                w.writeUInt("targetId", it != runtimeToFile.end() ? it->second : UINT32_MAX);
            }
            // Parent's parent Entity — same fileId cross-reference as SpringConstraint.
            if (entry.typeId == ecs::typeId<ecs::Parent>()) {
                auto* p = static_cast<const ecs::Parent*>(comp);
                auto it = runtimeToFile.find(p->parent.id);
                w.writeUInt("parentId", it != runtimeToFile.end() ? it->second : UINT32_MAX);
            }
            // Custom-mesh geometry → binary sidecar, except tiny meshes (below
            // kMeshBinVertexThreshold) which are cheap enough to inline as JSON
            // arrays and don't justify a second file on disk.
            if (entry.typeId == ecs::typeId<ecs::MeshRenderer>()) {
                auto* mr = static_cast<const ecs::MeshRenderer*>(comp);
                if (mr->mesh && mr->mesh->primitiveType == PrimitiveType::Custom
                    && mr->mesh->sourcePath.empty() && !mr->mesh->cpuVertices.empty()) {
                    const auto& verts = mr->mesh->cpuVertices;
                    const auto& inds  = mr->mesh->cpuIndices;
                    if (verts.size() >= kMeshBinVertexThreshold && ensureBinOpen()) {
                        bin.write(reinterpret_cast<const char*>(verts.data()),
                                  static_cast<std::streamsize>(verts.size() * sizeof(Vertex)));
                        bin.write(reinterpret_cast<const char*>(inds.data()),
                                  static_cast<std::streamsize>(inds.size() * sizeof(uint32_t)));
                        w.writeKey("geom");
                        w.beginObject();
                        w.writeUInt("vc", static_cast<unsigned>(verts.size()));
                        w.writeUInt("ic", static_cast<unsigned>(inds.size()));
                        w.endObject();
                    } else {
                        // Legacy inline format: flat [pos.xyz, normal.xyz, uv.xy] per vertex.
                        std::vector<float> flat;
                        flat.reserve(verts.size() * 8);
                        for (const auto& v : verts) {
                            flat.insert(flat.end(), {v.position[0], v.position[1], v.position[2],
                                                      v.normal[0],   v.normal[1],   v.normal[2],
                                                      v.uv[0],       v.uv[1]});
                        }
                        w.writePackedFloats("vertices", flat.data(), flat.size());
                        w.writePackedUInts("indices", inds.data(), inds.size());
                    }
                }
            }
            w.endObject();
        }

        w.endObject();  // entity
    }
    w.endArray();  // entities

    return bin.is_open();
}

// Drops a stale .meshbin sidecar from a previous save when this save wrote
// nothing bulky, so it doesn't linger and get mistaken for live data.
static void cleanupStaleBin(const std::string& binPath, bool wroteBin) {
    if (wroteBin) return;
    std::error_code ec;
    std::filesystem::remove(binPath, ec);
}

bool save(const char* path, ecs::Registry& reg, World& world) {
    JsonWriter w;
    w.beginObject();

    w.writeInt("version", 1);

    // World settings
    w.writeFloat3("gravity", world.gravity.x, world.gravity.y, world.gravity.z);
    w.writeFloat("exposure", world.exposure);
    w.writeFloat("shadowBias", world.shadowBias);
    w.writeFloat("shadowNormalBias", world.shadowNormalBias);
    w.writeFloat("shadowPcfRadius", world.shadowPcfRadius);
    w.writeFloat("shadowOrthoHalfExtent", world.shadowOrthoHalfExtent);
    w.writeFloat("shadowOrthoFar", world.shadowOrthoFar);
    w.writeFloat("shadowSpotNear", world.shadowSpotNear);
    w.writeFloat("shadowSpotFar", world.shadowSpotFar);
    w.writeFloat("shadowPointNear", world.shadowPointNear);
    w.writeFloat("shadowPointFar", world.shadowPointFar);

    std::vector<ecs::Entity> entities;
    for (auto [e, _sel] : reg.view<ecs::EditorSelectable>()) entities.push_back(e);

    std::string binPath = std::filesystem::path(path).replace_extension(".meshbin").string();
    bool wroteBin = writeEntitiesArray(w, entities, reg, world, binPath, ecs::NullEntity);

    w.endObject();  // root

    cleanupStaleBin(binPath, wroteBin);

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << w.str();
#ifdef YOPE_EDITOR
    Console::log(std::string("Saved scene: ") + path, LogSeverity::Info);
#endif
    return true;
}

bool saveEntities(const char* path, const std::vector<ecs::Entity>& entities,
                  ecs::Registry& reg, World& world) {
    JsonWriter w;
    w.beginObject();
    w.writeInt("version", 1);

    // entities.front() is exempt from templateRef treatment even if it already
    // carries ecs::TemplateInstance — "save this as a new template" must
    // always capture its current state, not just re-emit a reference to
    // whatever template it happened to already be an instance of.
    ecs::Entity exemptRoot = entities.empty() ? ecs::NullEntity : entities.front();
    std::string binPath = std::filesystem::path(path).replace_extension(".meshbin").string();
    bool wroteBin = writeEntitiesArray(w, entities, reg, world, binPath, exemptRoot);

    w.endObject();  // root

    cleanupStaleBin(binPath, wroteBin);

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << w.str();
#ifdef YOPE_EDITOR
    Console::log(std::string("Saved template: ") + path, LogSeverity::Info);
#endif
    return true;
}

// ---------------------------------------------------------------------------
// Parse (background-thread safe: no World / registry / GPU access).
// ---------------------------------------------------------------------------

namespace {

struct MeshBinCursor {
    const std::vector<uint8_t>& data;
    size_t offset = 0;
    bool read(void* dst, size_t n) {
        if (offset + n > data.size()) return false;
        std::memcpy(dst, data.data() + offset, n);
        offset += n;
        return true;
    }
};

// Result of parsing one file's own "entities" array (+ its own .meshbin
// sidecar) in isolation: fileIds here are LOCAL to that one file (0..N-1, 0 is
// always that file's root — collectSubtree/"Save as Template" guarantee this
// on write). A nested template reference gets exactly one of these per
// reference, which the caller then splices into its own numbering — see
// parseEntityNode's "templateRef" branch.
struct SubParse {
    std::vector<ParsedScene::Ent> entities;
    std::string error;
    bool ok = false;
};

bool parseEntityNode(const JsonNode& entNode, SubParse& out,
                     MeshBinCursor& meshBin, bool meshBinOk,
                     std::vector<std::string>& expandingStack, int depth,
                     uint32_t& nextFreeFileId);

// Processes an already-parsed document's "entities" array (+ its own .meshbin
// sidecar) into a SubParse. relPath locates the sidecar and is pushed onto
// expandingStack by the caller before this runs (parseScene seeds the stack
// with the top-level path itself; parseEntitiesFile does the same for a
// nested reference) — this function does not touch expandingStack itself.
SubParse parseEntitiesBody(const JsonNode& root, const std::string& relPath,
                          std::vector<std::string>& expandingStack, int depth) {
    SubParse out;
    if (!root.contains("entities")) { out.ok = true; return out; }
    if (depth > kMaxTemplateDepth) {
        out.error = "template nesting too deep (>" + std::to_string(kMaxTemplateDepth) + "): " + relPath;
        return out;
    }

    // Load the geometry sidecar (<file>.meshbin) as bytes (embedded or filesystem —
    // see assets::readBytes). Blobs are read sequentially in the same entity order
    // they were written; missing/short reads fall back to a primitive (no crash),
    // so pre-sidecar scenes just show placeholder cubes.
    std::string binRelPath = std::filesystem::path(relPath).replace_extension(".meshbin").generic_string();
    std::vector<uint8_t> meshBinData = assets::readBytes(binRelPath);
    MeshBinCursor meshBin{meshBinData};
    bool meshBinOk = false;
    if (!meshBinData.empty()) {
        char hdr[8] = {};
        meshBinOk = meshBin.read(hdr, 8) &&
                    hdr[0] == 'Y' && hdr[1] == 'S' && hdr[2] == 'M' && hdr[3] == 'B';
    }

    // Reserve a fileId counter above every explicitly-declared fileId in this
    // document (plain entities and templateRef placeholder fileIds alike) so
    // entities synthesized while expanding a templateRef (a referenced
    // template's non-root entities) can never collide with a fileId some
    // later sibling node in THIS SAME document declares.
    uint32_t nextFreeFileId = 0;
    for (auto& entNode : root["entities"].asArray())
        if (entNode.contains("fileId"))
            nextFreeFileId = std::max(nextFreeFileId, entNode["fileId"].asUInt() + 1);

    for (auto& entNode : root["entities"].asArray()) {
        if (!parseEntityNode(entNode, out, meshBin, meshBinOk, expandingStack, depth, nextFreeFileId))
            return out;   // out.error already set
    }

    out.ok = true;
    return out;
}

// Loads and parses relPath from scratch and processes it — used only for a
// nested template reference (the top-level document is already loaded by
// parseScene itself). Pushes relPath onto expandingStack for cycle detection:
// a template that (in)directly references itself fails cleanly instead of
// recursing forever.
SubParse parseEntitiesFile(const std::string& relPath,
                          std::vector<std::string>& expandingStack, int depth) {
    SubParse out;
    for (auto& p : expandingStack) {
        if (p == relPath) { out.error = "template cycle detected: " + relPath; return out; }
    }

    JsonNode root;
    try {
        std::string json = assets::readText(relPath);
        if (json.empty())
            throw std::runtime_error(std::string("cannot open: ") + relPath);
        root = parseJson(json.c_str());
    } catch (const std::exception& ex) {
        out.error = std::string("Parse error in ") + relPath + ": " + ex.what();
        return out;
    }

    expandingStack.push_back(relPath);
    out = parseEntitiesBody(root, relPath, expandingStack, depth);
    expandingStack.pop_back();
    return out;
}

// Re-invokes a component's existing (already patch-style, per-field
// n.contains(key)-gated) deserializer a second time over a sparse "overrides"
// object, on top of whatever the base template already populated. Only the
// keys present in ov get touched — this is the entire override mechanism,
// no diff/merge logic needed beyond this dispatch.
void applyComponentOverrides(const JsonNode& ov, ComponentSnapshot& snap) {
    if (ov.contains("Transform"))            compser::deserializeTransform(ov["Transform"], &snap.transform);
    if (ov.contains("Hull"))                 compser::deserializeHull(ov["Hull"], &snap.hull);
    if (ov.contains("SphereForm"))           compser::deserializeSphereForm(ov["SphereForm"], &snap.sphere);
    if (ov.contains("AABBForm"))             compser::deserializeAABBForm(ov["AABBForm"], &snap.aabb);
    if (ov.contains("OBBForm"))              compser::deserializeOBBForm(ov["OBBForm"], &snap.obb);
    if (ov.contains("CapsuleForm"))          compser::deserializeCapsuleForm(ov["CapsuleForm"], &snap.capsule);
    if (ov.contains("CylinderForm"))         compser::deserializeCylinderForm(ov["CylinderForm"], &snap.cylinder);
    if (ov.contains("LightSource"))          compser::deserializeLightSource(ov["LightSource"], &snap.light);
    if (ov.contains("Name"))                 compser::deserializeName(ov["Name"], &snap.name);
    if (ov.contains("AudioSource"))          compser::deserializeAudioSource(ov["AudioSource"], &snap.audio);
    if (ov.contains("ScriptComponent"))      compser::deserializeScriptComponent(ov["ScriptComponent"], &snap.script);
    if (ov.contains("Material"))             compser::deserializeMaterial(ov["Material"], &snap.material);
    if (ov.contains("UITransform"))          compser::deserializeUITransform(ov["UITransform"], &snap.uiTransform);
    if (ov.contains("UIBackground"))         compser::deserializeUIBackground(ov["UIBackground"], &snap.uiBackground);
    if (ov.contains("UITexturedBackground")) compser::deserializeUITexturedBackground(ov["UITexturedBackground"], &snap.uiTexturedBackground);
    if (ov.contains("UICurvedBackground"))   compser::deserializeUICurvedBackground(ov["UICurvedBackground"], &snap.uiCurvedBackground);
    if (ov.contains("UIText"))               compser::deserializeUIText(ov["UIText"], &snap.uiText);
    if (ov.contains("UIButton"))             compser::deserializeUIButton(ov["UIButton"], &snap.uiButton);
    if (ov.contains("TextLabel3D"))          compser::deserializeTextLabel3D(ov["TextLabel3D"], &snap.textLabel3D);
    if (ov.contains("AnimationPlayer"))      compser::deserializeAnimationPlayer(ov["AnimationPlayer"], &snap.animationPlayer);
    // Spring/Joint overrides are deliberately not supported: their "target" is a
    // fileId cross-reference into this already-renumbered document, and overriding
    // just the numeric fields (k/restLength/limits) without a target isn't a case
    // that's come up — add if a real need shows up.

    // MeshRenderer: bespoke, not compser::deserializeMeshRenderer (a no-op stub —
    // mesh recreation happens via the hand-rolled MeshRenderer parsing in
    // parseEntityNode, which is what these fields feed into on restore). Mirrors
    // that same field set (color/primitiveType/primitiveExtents); see
    // computeOverrides for why custom-mesh geometry isn't covered here.
    if (ov.contains("MeshRenderer")) {
        const auto& mrOv = ov["MeshRenderer"];
        if (mrOv.contains("color")) {
            auto& c = mrOv["color"].asArray();
            if (c.size() >= 3) { snap.meshColor[0] = c[0].asFloat(); snap.meshColor[1] = c[1].asFloat(); snap.meshColor[2] = c[2].asFloat(); }
        }
        if (mrOv.contains("primitiveType")) snap.primType = static_cast<PrimitiveType>(mrOv["primitiveType"].asInt());
        if (mrOv.contains("primitiveExtents")) {
            auto& pe = mrOv["primitiveExtents"].asArray();
            if (pe.size() >= 3) snap.primExtents = {pe[0].asFloat(), pe[1].asFloat(), pe[2].asFloat()};
        }
    }
}

bool parseEntityNode(const JsonNode& entNode, SubParse& out,
                     MeshBinCursor& meshBin, bool meshBinOk,
                     std::vector<std::string>& expandingStack, int depth,
                     uint32_t& nextFreeFileId) {
    if (entNode.contains("templateRef")) {
        std::string refPath = assets::normalizeToAssetsRelative(entNode["templateRef"].asString());
        SubParse nested = parseEntitiesFile(refPath, expandingStack, depth + 1);
        if (!nested.ok) { out.error = nested.error; return false; }
        if (nested.entities.empty()) { out.error = "template has no entities: " + refPath; return false; }

        uint32_t outerFileId   = entNode.contains("fileId")   ? entNode["fileId"].asUInt()   : UINT32_MAX;
        bool     hasParentLink = entNode.contains("parentId");
        uint32_t outerParentId = hasParentLink ? entNode["parentId"].asUInt() : UINT32_MAX;

        // Remap the nested file's local fileIds (0..M-1, 0 = its root) into this
        // document's numbering: the nested root takes over outerFileId (so any
        // sibling here pointing at outerFileId transparently resolves to it once
        // expanded); every other nested entity gets a fresh id from the counter
        // reserved above every fileId this document itself declares.
        std::unordered_map<uint32_t, uint32_t> remap;
        for (const auto& ne : nested.entities)
            remap[ne.fileId] = (ne.fileId == 0) ? outerFileId : nextFreeFileId++;
        auto remapId = [&](uint32_t id) -> uint32_t {
            if (id == UINT32_MAX) return UINT32_MAX;
            auto it = remap.find(id);
            // Cross-boundary reference (nested entity pointing outside its own
            // template) — unsupported, same limitation as Unity prefabs not
            // referencing external-scene objects. Drop rather than dangle.
            return it != remap.end() ? it->second : UINT32_MAX;
        };

        for (auto ne : nested.entities) {
            ne.fileId = remapId(ne.fileId);
            if (ne.hasParentLink)           ne.parentFileId               = remapId(ne.parentFileId);
            if (ne.hasSpringTarget)         ne.springTargetFileId         = remapId(ne.springTargetFileId);
            if (ne.hasPointJointTarget)     ne.pointJointTargetFileId     = remapId(ne.pointJointTargetFileId);
            if (ne.hasHingeJointTarget)     ne.hingeJointTargetFileId     = remapId(ne.hingeJointTargetFileId);
            if (ne.hasConeTwistJointTarget) ne.coneTwistJointTargetFileId = remapId(ne.coneTwistJointTargetFileId);

            if (ne.fileId == outerFileId) {
                // This is the nested root — splice it into the outer document's
                // hierarchy at the point of reference, then apply placement/overrides.
                ne.hasParentLink = hasParentLink;
                ne.parentFileId  = outerParentId;

                if (entNode.contains("Transform"))
                    compser::deserializeTransform(entNode["Transform"], &ne.snap.transform);
                if (entNode.contains("overrides"))
                    applyComponentOverrides(entNode["overrides"], ne.snap);
            }
            out.entities.push_back(std::move(ne));
        }
        return true;
    }

    // ---- plain entity node ----
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
    if (entNode.contains("TemplateInstance")) {
        snap.hasTemplateInstance = true;
        compser::deserializeTemplateInstance(entNode["TemplateInstance"], &snap.templateInstance);
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
    if (entNode.contains("UIButton")) {
        snap.hasUIButton = true;
        compser::deserializeUIButton(entNode["UIButton"], &snap.uiButton);
    }
    if (entNode.contains("TextLabel3D")) {
        snap.hasTextLabel3D = true;
        compser::deserializeTextLabel3D(entNode["TextLabel3D"], &snap.textLabel3D);
    }
    if (entNode.contains("AnimationPlayer")) {
        snap.hasAnimationPlayer = true;
        compser::deserializeAnimationPlayer(entNode["AnimationPlayer"], &snap.animationPlayer);
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
    if (entNode.contains("PointJointConstraint")) {
        snap.hasPointJoint = true;
        compser::deserializePointJointConstraint(entNode["PointJointConstraint"], &snap.pointJoint);
        // snap.pointJoint.target is left invalid here; commitFinalize resolves it
        // via the fileId cross-reference and then calls addPointJointPhysics.
        ent.hasPointJointTarget    = true;
        ent.pointJointTargetFileId = entNode["PointJointConstraint"].contains("targetId")
                                       ? entNode["PointJointConstraint"]["targetId"].asUInt() : UINT32_MAX;
    }
    if (entNode.contains("HingeJointConstraint")) {
        snap.hasHingeJoint = true;
        compser::deserializeHingeJointConstraint(entNode["HingeJointConstraint"], &snap.hingeJoint);
        ent.hasHingeJointTarget    = true;
        ent.hingeJointTargetFileId = entNode["HingeJointConstraint"].contains("targetId")
                                       ? entNode["HingeJointConstraint"]["targetId"].asUInt() : UINT32_MAX;
    }
    if (entNode.contains("ConeTwistJointConstraint")) {
        snap.hasConeTwistJoint = true;
        compser::deserializeConeTwistJointConstraint(entNode["ConeTwistJointConstraint"], &snap.coneTwistJoint);
        ent.hasConeTwistJointTarget    = true;
        ent.coneTwistJointTargetFileId = entNode["ConeTwistJointConstraint"].contains("targetId")
                                           ? entNode["ConeTwistJointConstraint"]["targetId"].asUInt() : UINT32_MAX;
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
                bool ok = true;
                if (vc) ok = ok && meshBin.read(snap.cpuVerts.data(), vc * sizeof(Vertex));
                if (ic) ok = ok && meshBin.read(snap.cpuInds.data(), ic * sizeof(uint32_t));
                if (!ok) { snap.cpuVerts.clear(); snap.cpuInds.clear(); }  // short read → fallback
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
    return true;
}

} // anonymous namespace

ParsedScene parseScene(const char* path) {
    ParsedScene out;

    // Scene paths arrive in a mixed convention (absolute, "assets/"-prefixed
    // from Python, or already assets/-relative) — normalize once so both the
    // JSON and its .meshbin sidecar can go through the embedded/filesystem
    // resolver uniformly.
    std::string relPath = assets::normalizeToAssetsRelative(path);

    JsonNode root;
    try {
        std::string json = assets::readText(relPath);
        if (json.empty())
            throw std::runtime_error(std::string("cannot open: ") + path);
        root = parseJson(json.c_str());
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

    // Shadow tuning (older scenes without these keys keep ParsedScene's defaults).
    if (root.contains("shadowBias"))            out.shadowBias            = root["shadowBias"].asFloat();
    if (root.contains("shadowNormalBias"))       out.shadowNormalBias      = root["shadowNormalBias"].asFloat();
    if (root.contains("shadowPcfRadius"))        out.shadowPcfRadius       = root["shadowPcfRadius"].asFloat();
    if (root.contains("shadowOrthoHalfExtent"))  out.shadowOrthoHalfExtent = root["shadowOrthoHalfExtent"].asFloat();
    if (root.contains("shadowOrthoFar"))         out.shadowOrthoFar        = root["shadowOrthoFar"].asFloat();
    if (root.contains("shadowSpotNear"))         out.shadowSpotNear        = root["shadowSpotNear"].asFloat();
    if (root.contains("shadowSpotFar"))          out.shadowSpotFar         = root["shadowSpotFar"].asFloat();
    if (root.contains("shadowPointNear"))        out.shadowPointNear       = root["shadowPointNear"].asFloat();
    if (root.contains("shadowPointFar"))         out.shadowPointFar        = root["shadowPointFar"].asFloat();

    if (!root.contains("entities")) { out.ok = true; return out; }

    std::vector<std::string> expandingStack{relPath};
    SubParse sub = parseEntitiesBody(root, relPath, expandingStack, /*depth=*/0);
    if (!sub.ok) { out.error = sub.error; return out; }
    out.entities = std::move(sub.entities);

    // Collect embedded glTF images off-thread. Materials imported from a .glb store
    // their maps as synthetic "<glb>#imgN" keys whose pixel data lives only in the
    // source file; scan the parsed material snapshots for those keys and re-run the
    // (geometry-free, GPU-free) glTF loader purely to capture the encoded image
    // bytes. commitFinalize enqueues them for decode on the main thread — the
    // enqueue itself must not touch the AssetManager cache from this worker thread.
    // Runs once over the fully-expanded entity list (including any spliced-in
    // template references), not per nested file, so a nested template's own glb
    // materials aren't scanned twice.
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
    world.exposure              = ps.exposure;
    world.shadowBias            = ps.shadowBias;
    world.shadowNormalBias      = ps.shadowNormalBias;
    world.shadowPcfRadius       = ps.shadowPcfRadius;
    world.shadowOrthoHalfExtent = ps.shadowOrthoHalfExtent;
    world.shadowOrthoFar        = ps.shadowOrthoFar;
    world.shadowSpotNear        = ps.shadowSpotNear;
    world.shadowSpotFar         = ps.shadowSpotFar;
    world.shadowPointNear       = ps.shadowPointNear;
    world.shadowPointFar        = ps.shadowPointFar;
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

// Resolves Parent/Spring/PointJoint/HingeJoint/ConeTwistJoint fileId cross-
// references into live Entity handles. Already scoped correctly by construction
// (every lookup goes through ps.fileIdToEntity, never the registry at large), so
// this same function is safe to share between whole-scene load and a live-world
// spawn — the difference between commitFinalize and commitFinalizeScoped is only
// in the physics-reconstruction step that follows, not this resolution step.
static void resolveCrossReferences(ParsedScene& ps, ecs::Registry& reg) {
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

    // Resolve PointJointConstraint cross-references (mirrors SpringConstraint).
    for (const auto& ent : ps.entities) {
        if (!ent.hasPointJointTarget) continue;
        auto it = ps.fileIdToEntity.find(ent.fileId);
        if (it == ps.fileIdToEntity.end() || !reg.valid(it->second)) continue;
        if (auto* pj = reg.get<ecs::PointJointConstraint>(it->second)) {
            auto tit = ps.fileIdToEntity.find(ent.pointJointTargetFileId);
            if (tit != ps.fileIdToEntity.end()) pj->target = tit->second;
        }
    }

    // Resolve HingeJointConstraint cross-references (mirrors SpringConstraint).
    for (const auto& ent : ps.entities) {
        if (!ent.hasHingeJointTarget) continue;
        auto it = ps.fileIdToEntity.find(ent.fileId);
        if (it == ps.fileIdToEntity.end() || !reg.valid(it->second)) continue;
        if (auto* hj = reg.get<ecs::HingeJointConstraint>(it->second)) {
            auto tit = ps.fileIdToEntity.find(ent.hingeJointTargetFileId);
            if (tit != ps.fileIdToEntity.end()) hj->target = tit->second;
        }
    }

    // Resolve ConeTwistJointConstraint cross-references (mirrors SpringConstraint).
    for (const auto& ent : ps.entities) {
        if (!ent.hasConeTwistJointTarget) continue;
        auto it = ps.fileIdToEntity.find(ent.fileId);
        if (it == ps.fileIdToEntity.end() || !reg.valid(it->second)) continue;
        if (auto* cj = reg.get<ecs::ConeTwistJointConstraint>(it->second)) {
            auto tit = ps.fileIdToEntity.find(ent.coneTwistJointTargetFileId);
            if (tit != ps.fileIdToEntity.end()) cj->target = tit->second;
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
}

void commitFinalize(ParsedScene& ps, World& world,
                    AudioSystem* audio, AssetManager* assets, bool startAudio) {
    ecs::Registry& reg = world.getRegistry();
    resolveCrossReferences(ps, reg);

    // Reconstruct physics springs from resolved SpringConstraint components.
    for (auto [e, sc] : reg.view<ecs::SpringConstraint>()) {
        if (reg.valid(sc.target))
            world.addSpringPhysics(e, sc.target, sc.k, sc.restLength);
    }

    // Reconstruct physics joints from resolved PointJointConstraint components.
    for (auto [e, pj] : reg.view<ecs::PointJointConstraint>()) {
        if (reg.valid(pj.target))
            world.addPointJointPhysics(e, pj.target, pj.localAnchorA, pj.localAnchorB);
    }
    for (auto [e, hj] : reg.view<ecs::HingeJointConstraint>()) {
        if (reg.valid(hj.target))
            world.addHingeJointPhysics(e, hj.target, hj.localAnchorA, hj.localAnchorB,
                                       hj.localAxisA, hj.localAxisB,
                                       hj.limitEnabled, hj.lowerAngle, hj.upperAngle);
    }
    for (auto [e, cj] : reg.view<ecs::ConeTwistJointConstraint>()) {
        if (reg.valid(cj.target))
            world.addConeTwistJointPhysics(e, cj.target, cj.localAnchorA, cj.localAnchorB,
                                           cj.localTwistAxisA, cj.localTwistAxisB,
                                           cj.swingLimit, cj.twistLimit);
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

void commitFinalizeScoped(ParsedScene& ps, World& world,
                         AudioSystem* audio, AssetManager* assets, bool startAudio) {
    ecs::Registry& reg = world.getRegistry();
    resolveCrossReferences(ps, reg);

    // Reconstruct physics springs/joints ONLY for entities just committed — see the
    // declaration comment in SceneSerializer.h for why this must not walk the whole
    // registry the way commitFinalize does.
    for (const auto& [fileId, e] : ps.fileIdToEntity) {
        if (!reg.valid(e)) continue;
        if (auto* sc = reg.get<ecs::SpringConstraint>(e)) {
            if (reg.valid(sc->target))
                world.addSpringPhysics(e, sc->target, sc->k, sc->restLength);
        }
        if (auto* pj = reg.get<ecs::PointJointConstraint>(e)) {
            if (reg.valid(pj->target))
                world.addPointJointPhysics(e, pj->target, pj->localAnchorA, pj->localAnchorB);
        }
        if (auto* hj = reg.get<ecs::HingeJointConstraint>(e)) {
            if (reg.valid(hj->target))
                world.addHingeJointPhysics(e, hj->target, hj->localAnchorA, hj->localAnchorB,
                                           hj->localAxisA, hj->localAxisB,
                                           hj->limitEnabled, hj->lowerAngle, hj->upperAngle);
        }
        if (auto* cj = reg.get<ecs::ConeTwistJointConstraint>(e)) {
            if (reg.valid(cj->target))
                world.addConeTwistJointPhysics(e, cj->target, cj->localAnchorA, cj->localAnchorB,
                                               cj->localTwistAxisA, cj->localTwistAxisB,
                                               cj->swingLimit, cj->twistLimit);
        }
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
