#include "scene/serialization/ComponentSerializers.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "world/RenderMesh.h"
#include <cstring>
#include <cstdio>
#include <vector>

namespace compser {

// ---- Transform ----

void serializeTransform(const void* comp, JsonWriter& w) {
    auto* t = static_cast<const Transform*>(comp);
    w.writeFloat3("position", t->position.x, t->position.y, t->position.z);
    w.writeFloat4("rotation", t->rotation.x, t->rotation.y, t->rotation.z, t->rotation.w);
    w.writeFloat3("scale",    t->scale.x, t->scale.y, t->scale.z);
}

bool deserializeTransform(const JsonNode& n, void* comp) {
    auto* t = static_cast<Transform*>(comp);
    auto readF3 = [&](const char* key, float& x, float& y, float& z) {
        if (!n.contains(key)) return;
        auto& arr = n[key].asArray();
        if (arr.size() >= 3) { x = arr[0].asFloat(); y = arr[1].asFloat(); z = arr[2].asFloat(); }
    };
    auto readF4 = [&](const char* key, float& x, float& y, float& z, float& w) {
        if (!n.contains(key)) return;
        auto& arr = n[key].asArray();
        if (arr.size() >= 4) { x = arr[0].asFloat(); y = arr[1].asFloat(); z = arr[2].asFloat(); w = arr[3].asFloat(); }
    };
    readF3("position", t->position.x, t->position.y, t->position.z);
    readF4("rotation", t->rotation.x, t->rotation.y, t->rotation.z, t->rotation.w);
    readF3("scale",    t->scale.x, t->scale.y, t->scale.z);
    return true;
}

// ---- Hull ----

void serializeHull(const void* comp, JsonWriter& w) {
    auto* h = static_cast<const ecs::Hull*>(comp);
    w.writeFloat("mass",           h->mass);
    w.writeFloat("linearDamping",  h->linearDamping);
    w.writeFloat("angularDamping", h->angularDamping);
    w.writeFloat("friction",       h->friction);
    w.writeFloat("restitution",    h->restitution);
    w.writeBool ("gravity",        h->gravity);
    w.writeBool ("tangible",       h->tangible);
    w.writeBool ("sleepingEnabled", h->sleepingEnabled);
    w.writeBool ("isTrigger",      h->isTrigger);
    w.writeBool ("kinematic",      h->kinematic);
    w.writeUInt ("collisionLayer", h->collisionLayer);
    w.writeUInt ("collisionMask",  h->collisionMask);
    // Opt-in observer membership — 0 for almost every body, so only emit when set.
    if (h->observeLayers != 0) w.writeUInt("observeLayers", h->observeLayers);
    w.writeFloat3("velocity", h->velocity.x, h->velocity.y, h->velocity.z);
    w.writeFloat3("omega",    h->omega.x,    h->omega.y,    h->omega.z);
}

bool deserializeHull(const JsonNode& n, void* comp) {
    auto* h = static_cast<ecs::Hull*>(comp);
    if (n.contains("mass"))           h->mass           = n["mass"].asFloat();
    if (n.contains("linearDamping"))  h->linearDamping  = n["linearDamping"].asFloat();
    if (n.contains("angularDamping")) h->angularDamping = n["angularDamping"].asFloat();
    if (n.contains("friction"))       h->friction       = n["friction"].asFloat();
    if (n.contains("restitution"))    h->restitution    = n["restitution"].asFloat();
    if (n.contains("gravity"))        h->gravity        = n["gravity"].asBool();
    if (n.contains("tangible"))       h->tangible       = n["tangible"].asBool();
    if (n.contains("sleepingEnabled")) h->sleepingEnabled = n["sleepingEnabled"].asBool();
    if (n.contains("isTrigger"))      h->isTrigger      = n["isTrigger"].asBool();
    if (n.contains("kinematic"))      h->kinematic      = n["kinematic"].asBool();
    if (n.contains("collisionLayer")) h->collisionLayer = n["collisionLayer"].asUInt();
    if (n.contains("collisionMask"))  h->collisionMask  = n["collisionMask"].asUInt();
    if (n.contains("observeLayers"))  h->observeLayers  = n["observeLayers"].asUInt();
    if (n.contains("velocity")) {
        auto& arr = n["velocity"].asArray();
        if (arr.size() >= 3) { h->velocity.x = arr[0].asFloat(); h->velocity.y = arr[1].asFloat(); h->velocity.z = arr[2].asFloat(); }
    }
    if (n.contains("omega")) {
        auto& arr = n["omega"].asArray();
        if (arr.size() >= 3) { h->omega.x = arr[0].asFloat(); h->omega.y = arr[1].asFloat(); h->omega.z = arr[2].asFloat(); }
    }
    if (h->mass > 0.f) h->inverseMass = 1.f / h->mass;
    return true;
}

// ---- SphereForm ----

void serializeSphereForm(const void* comp, JsonWriter& w) {
    w.writeFloat("radius", static_cast<const ecs::SphereForm*>(comp)->radius);
}

bool deserializeSphereForm(const JsonNode& n, void* comp) {
    if (n.contains("radius")) static_cast<ecs::SphereForm*>(comp)->radius = n["radius"].asFloat();
    return true;
}

// ---- AABBForm ----

void serializeAABBForm(const void* comp, JsonWriter& w) {
    auto* a = static_cast<const ecs::AABBForm*>(comp);
    w.writeFloat3("extent", a->extent.x, a->extent.y, a->extent.z);
}

bool deserializeAABBForm(const JsonNode& n, void* comp) {
    auto* a = static_cast<ecs::AABBForm*>(comp);
    if (n.contains("extent")) {
        auto& arr = n["extent"].asArray();
        if (arr.size() >= 3) { a->extent.x = arr[0].asFloat(); a->extent.y = arr[1].asFloat(); a->extent.z = arr[2].asFloat(); }
    }
    return true;
}

// ---- OBBForm ----

void serializeOBBForm(const void* comp, JsonWriter& w) {
    auto* o = static_cast<const ecs::OBBForm*>(comp);
    w.writeFloat3("extent", o->extent.x, o->extent.y, o->extent.z);
}

bool deserializeOBBForm(const JsonNode& n, void* comp) {
    auto* o = static_cast<ecs::OBBForm*>(comp);
    if (n.contains("extent")) {
        auto& arr = n["extent"].asArray();
        if (arr.size() >= 3) { o->extent.x = arr[0].asFloat(); o->extent.y = arr[1].asFloat(); o->extent.z = arr[2].asFloat(); }
    }
    return true;
}

// ---- CapsuleForm ----

void serializeCapsuleForm(const void* comp, JsonWriter& w) {
    auto* c = static_cast<const ecs::CapsuleForm*>(comp);
    w.writeFloat("radius",     c->radius);
    w.writeFloat("halfHeight", c->halfHeight);
}

bool deserializeCapsuleForm(const JsonNode& n, void* comp) {
    auto* c = static_cast<ecs::CapsuleForm*>(comp);
    if (n.contains("radius"))     c->radius     = n["radius"].asFloat();
    if (n.contains("halfHeight")) c->halfHeight = n["halfHeight"].asFloat();
    return true;
}

// ---- CylinderForm ----

void serializeCylinderForm(const void* comp, JsonWriter& w) {
    auto* c = static_cast<const ecs::CylinderForm*>(comp);
    w.writeFloat("radius",     c->radius);
    w.writeFloat("halfHeight", c->halfHeight);
}

bool deserializeCylinderForm(const JsonNode& n, void* comp) {
    auto* c = static_cast<ecs::CylinderForm*>(comp);
    if (n.contains("radius"))     c->radius     = n["radius"].asFloat();
    if (n.contains("halfHeight")) c->halfHeight = n["halfHeight"].asFloat();
    return true;
}

// ---- CompoundCollider ----
// The .bcbvh asset path plus the bake-time density/isStatic choice are
// persisted; `compiled` is re-resolved by World::loadCompoundCollider on load
// (see ComponentSnapshot::restore).

void serializeCompoundCollider(const void* comp, JsonWriter& w) {
    auto* cc = static_cast<const ecs::CompoundCollider*>(comp);
    if (cc->assetPath[0]) w.writeString("assetPath", cc->assetPath);
    w.writeFloat("density",  cc->density);
    w.writeBool ("isStatic", cc->isStatic);
}

bool deserializeCompoundCollider(const JsonNode& n, void* comp) {
    auto* cc = static_cast<ecs::CompoundCollider*>(comp);
    if (n.contains("assetPath"))
        std::strncpy(cc->assetPath, n["assetPath"].asString().c_str(), sizeof(cc->assetPath) - 1);
    if (n.contains("density"))  cc->density  = n["density"].asFloat();
    if (n.contains("isStatic")) cc->isStatic = n["isStatic"].asBool();
    return true;
}

// ---- MeshRenderer ----
// Known primitives: store type + extents so they can be regenerated on load.
// Custom meshes (drag-dropped OBJs): store just the source file path — the
// geometry is reloaded from disk on scene open.  Packing vertex/index arrays
// into JSON produces enormous files for real assets and is avoided entirely.

void serializeMeshRenderer(const void* comp, JsonWriter& w) {
    auto* mr = static_cast<const ecs::MeshRenderer*>(comp);
    if (!mr->mesh) return;
    w.writeFloat3("color", mr->mesh->color[0], mr->mesh->color[1], mr->mesh->color[2]);
    w.writeInt("primitiveType", static_cast<int>(mr->mesh->primitiveType));
    w.writeFloat3("primitiveExtents", mr->mesh->primitiveExtents.x,
                  mr->mesh->primitiveExtents.y, mr->mesh->primitiveExtents.z);

    if (mr->mesh->primitiveType == PrimitiveType::Custom) {
        if (!mr->mesh->sourcePath.empty()) {
            w.writeString("sourcePath", mr->mesh->sourcePath.c_str());
        } else {
            // Custom geometry with no source path cannot be reloaded (verts/indices
            // are not packed into JSON). This node saves as an empty stub. Scripts
            // that generate meshes at runtime should tag the entity ecs::Transient
            // so it is excluded from saves instead of persisting broken.
            std::fprintf(stderr,
                "MeshRenderer: custom mesh has no sourcePath -- it will not reload "
                "from this save (tag the entity Transient to exclude it).\n");
        }
    }
}

bool deserializeMeshRenderer(const JsonNode& /*n*/, void* /*comp*/) {
    // MeshRenderer.mesh is recreated by SceneSerializer; nothing to do here.
    return true;
}

// ---- Material ----
void serializeMaterial(const void* comp, JsonWriter& w) {
    auto* m = static_cast<const ecs::Material*>(comp);
    if (m->albedoPath[0])     w.writeString("albedoPath",     m->albedoPath);
    if (m->normalPath[0])     w.writeString("normalPath",     m->normalPath);
    if (m->metalRoughPath[0]) w.writeString("metalRoughPath", m->metalRoughPath);
    if (m->occlusionPath[0])  w.writeString("occlusionPath",  m->occlusionPath);
    if (m->emissivePath[0])   w.writeString("emissivePath",   m->emissivePath);
    w.writeFloat4("albedoFactor",   m->albedoFactor[0], m->albedoFactor[1],
                                    m->albedoFactor[2], m->albedoFactor[3]);
    w.writeFloat ("metallicFactor",  m->metallicFactor);
    w.writeFloat ("roughnessFactor", m->roughnessFactor);
    w.writeFloat3("emissiveFactor",  m->emissiveFactor[0], m->emissiveFactor[1], m->emissiveFactor[2]);
    w.writeFloat ("normalScale",     m->normalScale);
}
bool deserializeMaterial(const JsonNode& n, void* comp) {
    auto* m = static_cast<ecs::Material*>(comp);
    auto str = [&](const char* key, char* dst, size_t cap) {
        if (n.contains(key)) { std::strncpy(dst, n[key].asString().c_str(), cap - 1); dst[cap - 1] = '\0'; }
    };
    str("albedoPath",     m->albedoPath,     sizeof(m->albedoPath));
    str("normalPath",     m->normalPath,     sizeof(m->normalPath));
    str("metalRoughPath", m->metalRoughPath, sizeof(m->metalRoughPath));
    str("occlusionPath",  m->occlusionPath,  sizeof(m->occlusionPath));
    str("emissivePath",   m->emissivePath,   sizeof(m->emissivePath));
    if (n.contains("albedoFactor")) {
        auto& a = n["albedoFactor"].asArray();
        if (a.size() >= 4) for (int i = 0; i < 4; ++i) m->albedoFactor[i] = a[i].asFloat();
    }
    if (n.contains("metallicFactor"))  m->metallicFactor  = n["metallicFactor"].asFloat();
    if (n.contains("roughnessFactor")) m->roughnessFactor = n["roughnessFactor"].asFloat();
    if (n.contains("emissiveFactor")) {
        auto& a = n["emissiveFactor"].asArray();
        if (a.size() >= 3) for (int i = 0; i < 3; ++i) m->emissiveFactor[i] = a[i].asFloat();
    }
    if (n.contains("normalScale")) m->normalScale = n["normalScale"].asFloat();
    m->resolved = nullptr;   // force re-resolve against the live MaterialCache
    return true;
}

// ---- LightSource ----

void serializeLightSource(const void* comp, JsonWriter& w) {
    auto* ls = static_cast<const ecs::LightSource*>(comp);
    w.writeInt("type", ls->type);
    w.writeFloat3("color", ls->color[0], ls->color[1], ls->color[2]);
    w.writeFloat("intensity", ls->intensity);
    w.writeFloat3("position",  ls->position[0],  ls->position[1],  ls->position[2]);
    w.writeFloat3("direction", ls->direction[0], ls->direction[1], ls->direction[2]);
    w.writeFloat("constant",       ls->constant);
    w.writeFloat("linear",         ls->linear);
    w.writeFloat("quadratic",      ls->quadratic);
    w.writeFloat("innerConeAngle", ls->innerConeAngle);
    w.writeFloat("outerConeAngle", ls->outerConeAngle);
    w.writeBool("castsShadow", ls->castsShadow);
}

bool deserializeLightSource(const JsonNode& n, void* comp) {
    auto* ls = static_cast<ecs::LightSource*>(comp);
    auto readF3 = [](const JsonNode& arr, float* out) {
        if (arr.isArray() && arr.asArray().size() >= 3) {
            out[0] = arr.asArray()[0].asFloat();
            out[1] = arr.asArray()[1].asFloat();
            out[2] = arr.asArray()[2].asFloat();
        }
    };
    if (n.contains("type"))         ls->type      = n["type"].asInt();
    if (n.contains("color"))        readF3(n["color"], ls->color);
    if (n.contains("intensity"))    ls->intensity = n["intensity"].asFloat();
    if (n.contains("position"))     readF3(n["position"],  ls->position);
    if (n.contains("direction"))    readF3(n["direction"], ls->direction);
    if (n.contains("constant"))     ls->constant       = n["constant"].asFloat();
    if (n.contains("linear"))       ls->linear         = n["linear"].asFloat();
    if (n.contains("quadratic"))    ls->quadratic      = n["quadratic"].asFloat();
    if (n.contains("innerConeAngle")) ls->innerConeAngle = n["innerConeAngle"].asFloat();
    if (n.contains("outerConeAngle")) ls->outerConeAngle = n["outerConeAngle"].asFloat();
    if (n.contains("castsShadow"))    ls->castsShadow    = n["castsShadow"].asBool();
    return true;
}

// ---- Name ----

void serializeName(const void* comp, JsonWriter& w) {
    w.writeString("value", static_cast<const ecs::Name*>(comp)->value);
}

bool deserializeName(const JsonNode& n, void* comp) {
    if (n.contains("value")) {
        auto* nm = static_cast<ecs::Name*>(comp);
        std::strncpy(nm->value, n["value"].asString().c_str(), sizeof(nm->value) - 1);
    }
    return true;
}

// ---- TemplateInstance ----

void serializeTemplateInstance(const void* comp, JsonWriter& w) {
    w.writeString("sourcePath", static_cast<const ecs::TemplateInstance*>(comp)->sourcePath);
}

bool deserializeTemplateInstance(const JsonNode& n, void* comp) {
    if (n.contains("sourcePath")) {
        auto* ti = static_cast<ecs::TemplateInstance*>(comp);
        std::strncpy(ti->sourcePath, n["sourcePath"].asString().c_str(), sizeof(ti->sourcePath) - 1);
    }
    return true;
}

// ---- SpringConstraint ----

void serializeSpringConstraint(const void* comp, JsonWriter& w) {
    auto* sc = static_cast<const ecs::SpringConstraint*>(comp);
    w.writeFloat("k", sc->k);
    w.writeFloat("restLength", sc->restLength);
    // targetId is *not* written here — SceneSerializer::save patches in the
    // target's fileId after the generic write, because that mapping is only
    // known at save-loop time (per-instance runtime ID → iteration-order fileId).
}

bool deserializeSpringConstraint(const JsonNode& n, void* comp) {
    auto* sc = static_cast<ecs::SpringConstraint*>(comp);
    if (n.contains("k"))          sc->k          = n["k"].asFloat();
    if (n.contains("restLength")) sc->restLength = n["restLength"].asFloat();
    // targetId is resolved after all entities load; left as-is here.
    return true;
}

// ---- PointJointConstraint ----

void serializePointJointConstraint(const void* comp, JsonWriter& w) {
    auto* pj = static_cast<const ecs::PointJointConstraint*>(comp);
    w.writeFloat3("localAnchorA", pj->localAnchorA.x, pj->localAnchorA.y, pj->localAnchorA.z);
    w.writeFloat3("localAnchorB", pj->localAnchorB.x, pj->localAnchorB.y, pj->localAnchorB.z);
    // targetId is *not* written here — same reasoning as SpringConstraint's
    // targetId: SceneSerializer::save patches it in with the save-loop's
    // runtime→fileId mapping.
}

bool deserializePointJointConstraint(const JsonNode& n, void* comp) {
    auto* pj = static_cast<ecs::PointJointConstraint*>(comp);
    auto readF3 = [&](const char* key, math::Vec3& v) {
        if (!n.contains(key)) return;
        auto& arr = n[key].asArray();
        if (arr.size() >= 3) { v.x = arr[0].asFloat(); v.y = arr[1].asFloat(); v.z = arr[2].asFloat(); }
    };
    readF3("localAnchorA", pj->localAnchorA);
    readF3("localAnchorB", pj->localAnchorB);
    // targetId is resolved after all entities load; left as-is here.
    return true;
}

// ---- HingeJointConstraint ----

void serializeHingeJointConstraint(const void* comp, JsonWriter& w) {
    auto* hj = static_cast<const ecs::HingeJointConstraint*>(comp);
    w.writeFloat3("localAnchorA", hj->localAnchorA.x, hj->localAnchorA.y, hj->localAnchorA.z);
    w.writeFloat3("localAnchorB", hj->localAnchorB.x, hj->localAnchorB.y, hj->localAnchorB.z);
    w.writeFloat3("localAxisA",   hj->localAxisA.x,   hj->localAxisA.y,   hj->localAxisA.z);
    w.writeFloat3("localAxisB",   hj->localAxisB.x,   hj->localAxisB.y,   hj->localAxisB.z);
    w.writeBool ("limitEnabled", hj->limitEnabled);
    w.writeFloat("lowerAngle",   hj->lowerAngle);
    w.writeFloat("upperAngle",   hj->upperAngle);
    // targetId patched in by SceneSerializer::save (same as PointJointConstraint's).
}

bool deserializeHingeJointConstraint(const JsonNode& n, void* comp) {
    auto* hj = static_cast<ecs::HingeJointConstraint*>(comp);
    auto readF3 = [&](const char* key, math::Vec3& v) {
        if (!n.contains(key)) return;
        auto& arr = n[key].asArray();
        if (arr.size() >= 3) { v.x = arr[0].asFloat(); v.y = arr[1].asFloat(); v.z = arr[2].asFloat(); }
    };
    readF3("localAnchorA", hj->localAnchorA);
    readF3("localAnchorB", hj->localAnchorB);
    readF3("localAxisA",   hj->localAxisA);
    readF3("localAxisB",   hj->localAxisB);
    if (n.contains("limitEnabled")) hj->limitEnabled = n["limitEnabled"].asBool();
    if (n.contains("lowerAngle"))   hj->lowerAngle   = n["lowerAngle"].asFloat();
    if (n.contains("upperAngle"))   hj->upperAngle   = n["upperAngle"].asFloat();
    // targetId is resolved after all entities load; left as-is here.
    return true;
}

// ---- ConeTwistJointConstraint ----

void serializeConeTwistJointConstraint(const void* comp, JsonWriter& w) {
    auto* cj = static_cast<const ecs::ConeTwistJointConstraint*>(comp);
    w.writeFloat3("localAnchorA",    cj->localAnchorA.x,    cj->localAnchorA.y,    cj->localAnchorA.z);
    w.writeFloat3("localAnchorB",    cj->localAnchorB.x,    cj->localAnchorB.y,    cj->localAnchorB.z);
    w.writeFloat3("localTwistAxisA", cj->localTwistAxisA.x, cj->localTwistAxisA.y, cj->localTwistAxisA.z);
    w.writeFloat3("localTwistAxisB", cj->localTwistAxisB.x, cj->localTwistAxisB.y, cj->localTwistAxisB.z);
    w.writeFloat("swingLimit", cj->swingLimit);
    w.writeFloat("twistLimit", cj->twistLimit);
    // targetId patched in by SceneSerializer::save (same as PointJointConstraint's).
}

bool deserializeConeTwistJointConstraint(const JsonNode& n, void* comp) {
    auto* cj = static_cast<ecs::ConeTwistJointConstraint*>(comp);
    auto readF3 = [&](const char* key, math::Vec3& v) {
        if (!n.contains(key)) return;
        auto& arr = n[key].asArray();
        if (arr.size() >= 3) { v.x = arr[0].asFloat(); v.y = arr[1].asFloat(); v.z = arr[2].asFloat(); }
    };
    readF3("localAnchorA",    cj->localAnchorA);
    readF3("localAnchorB",    cj->localAnchorB);
    readF3("localTwistAxisA", cj->localTwistAxisA);
    readF3("localTwistAxisB", cj->localTwistAxisB);
    if (n.contains("swingLimit")) cj->swingLimit = n["swingLimit"].asFloat();
    if (n.contains("twistLimit")) cj->twistLimit = n["twistLimit"].asFloat();
    // targetId is resolved after all entities load; left as-is here.
    return true;
}

// ---- Parent ----
// The parent Entity ref, like SpringConstraint's target, is a fileId cross-reference
// resolved by SceneSerializer after all entities exist. This body writes/reads
// nothing; SceneSerializer::save patches in "parentId" and load resolves it.

void serializeParent(const void* comp, JsonWriter& w) {
    (void)comp; (void)w;
}

bool deserializeParent(const JsonNode& n, void* comp) {
    (void)n; (void)comp;
    return true;
}

// ---- AudioSource ----
// The Source* (OpenAL handle) is non-owning and gets recreated on load from
// the asset path. Live OpenAL parameters (gain/pitch/loop) live on the
// component to survive both undo and save/load.

void serializeAudioSource(const void* comp, JsonWriter& w) {
    auto* as = static_cast<const ecs::AudioSource*>(comp);
    if (as->path[0]) w.writeString("path", as->path);
    w.writeFloat("gain",  as->gain);
    w.writeFloat("pitch", as->pitch);
    w.writeBool ("loop",  as->loop);
    w.writeBool ("autoplay", as->autoplay);
    w.writeInt  ("bus", as->bus);
}

bool deserializeAudioSource(const JsonNode& n, void* comp) {
    auto* as = static_cast<ecs::AudioSource*>(comp);
    if (n.contains("path"))     std::strncpy(as->path, n["path"].asString().c_str(), sizeof(as->path) - 1);
    if (n.contains("gain"))     as->gain     = n["gain"].asFloat();
    if (n.contains("pitch"))    as->pitch    = n["pitch"].asFloat();
    if (n.contains("loop"))     as->loop     = n["loop"].asBool();
    if (n.contains("autoplay")) as->autoplay = n["autoplay"].asBool();
    if (n.contains("bus"))      as->bus      = n["bus"].asInt();
    // Source* is rebuilt by SceneSerializer after the entity is created.
    return true;
}

// ---- ScriptComponent ----
// scriptClass is the registered name. paramsBlob is the raw JSON snippet of
// the script's per-instance params. The live Script* is *not* serialized — it
// only exists at runtime / play-mode time and is recreated from these strings.

void serializeScriptComponent(const void* comp, JsonWriter& w) {
    auto* sc = static_cast<const ecs::ScriptComponent*>(comp);
    if (sc->scriptClass[0]) w.writeString("scriptClass", sc->scriptClass);
    if (sc->paramsBlob[0])  w.writeString("paramsBlob",  sc->paramsBlob);
}

bool deserializeScriptComponent(const JsonNode& n, void* comp) {
    auto* sc = static_cast<ecs::ScriptComponent*>(comp);
    if (n.contains("scriptClass")) {
        const std::string& s = n["scriptClass"].asString();
        if (s.size() >= sizeof(sc->scriptClass)) {
            std::fprintf(stderr,
                "ScriptComponent: scriptClass '%s' exceeds %zu bytes -- script left unset.\n",
                s.c_str(), sizeof(sc->scriptClass));
        } else {
            std::strncpy(sc->scriptClass, s.c_str(), sizeof(sc->scriptClass) - 1);
        }
    }
    if (n.contains("paramsBlob")) {
        const std::string& s = n["paramsBlob"].asString();
        // A truncated blob is not lossy JSON -- it is unparseable, which kills
        // the entity's whole script on load. Leave the default "{}" (parseable,
        // default params) rather than write garbage, and say so loudly.
        if (s.size() >= sizeof(sc->paramsBlob)) {
            std::fprintf(stderr,
                "ScriptComponent: paramsBlob for '%s' is %zu bytes, exceeds the %zu-byte "
                "limit -- params dropped (kept default \"{}\").\n",
                sc->scriptClass[0] ? sc->scriptClass : "<unknown>",
                s.size(), sizeof(sc->paramsBlob));
        } else {
            std::strncpy(sc->paramsBlob, s.c_str(), sizeof(sc->paramsBlob) - 1);
        }
    }
    sc->instance = nullptr;
    return true;
}

// ---- UITransform ----

void serializeUITransform(const void* comp, JsonWriter& w) {
    auto* t = static_cast<const ecs::UITransform*>(comp);
    w.writeFloat("minX",    t->minX);
    w.writeFloat("minY",    t->minY);
    w.writeFloat("maxX",    t->maxX);
    w.writeFloat("maxY",    t->maxY);
    w.writeInt  ("depth",   t->depth);
    w.writeBool ("visible", t->visible);
    w.writeInt  ("anchor",      t->anchor);
    w.writeInt  ("sizeMode",    t->sizeMode);
    w.writeFloat("pixelWidth",  t->pixelWidth);
    w.writeFloat("pixelHeight", t->pixelHeight);
    w.writeFloat("offsetXPx",   t->offsetXPx);
    w.writeFloat("offsetYPx",   t->offsetYPx);
    w.writeFloat("opacity",     t->opacity);
}

bool deserializeUITransform(const JsonNode& n, void* comp) {
    auto* t = static_cast<ecs::UITransform*>(comp);
    if (n.contains("minX"))    t->minX    = n["minX"].asFloat();
    if (n.contains("minY"))    t->minY    = n["minY"].asFloat();
    if (n.contains("maxX"))    t->maxX    = n["maxX"].asFloat();
    if (n.contains("maxY"))    t->maxY    = n["maxY"].asFloat();
    if (n.contains("depth"))   t->depth   = n["depth"].asInt();
    if (n.contains("visible")) t->visible = n["visible"].asBool();
    if (n.contains("anchor"))      t->anchor      = n["anchor"].asInt();
    if (n.contains("sizeMode"))    t->sizeMode    = n["sizeMode"].asInt();
    if (n.contains("pixelWidth"))  t->pixelWidth  = n["pixelWidth"].asFloat();
    if (n.contains("pixelHeight")) t->pixelHeight = n["pixelHeight"].asFloat();
    if (n.contains("offsetXPx"))   t->offsetXPx   = n["offsetXPx"].asFloat();
    if (n.contains("offsetYPx"))   t->offsetYPx   = n["offsetYPx"].asFloat();
    if (n.contains("opacity"))     t->opacity     = n["opacity"].asFloat();
    return true;
}

// ---- UIBackground ----

void serializeUIBackground(const void* comp, JsonWriter& w) {
    auto* bg = static_cast<const ecs::UIBackground*>(comp);
    w.writeFloat4("color", bg->r, bg->g, bg->b, bg->a);
}

bool deserializeUIBackground(const JsonNode& n, void* comp) {
    auto* bg = static_cast<ecs::UIBackground*>(comp);
    if (n.contains("color")) {
        auto& arr = n["color"].asArray();
        if (arr.size() >= 4) {
            bg->r = arr[0].asFloat(); bg->g = arr[1].asFloat();
            bg->b = arr[2].asFloat(); bg->a = arr[3].asFloat();
        }
    }
    return true;
}

// ---- UITexturedBackground ----

void serializeUITexturedBackground(const void* comp, JsonWriter& w) {
    auto* bg = static_cast<const ecs::UITexturedBackground*>(comp);
    if (bg->path[0]) w.writeString("path", bg->path);
    w.writeFloat4("tint", bg->tintR, bg->tintG, bg->tintB, bg->tintA);
}

bool deserializeUITexturedBackground(const JsonNode& n, void* comp) {
    auto* bg = static_cast<ecs::UITexturedBackground*>(comp);
    if (n.contains("path"))
        std::strncpy(bg->path, n["path"].asString().c_str(), sizeof(bg->path) - 1);
    if (n.contains("tint")) {
        auto& arr = n["tint"].asArray();
        if (arr.size() >= 4) {
            bg->tintR = arr[0].asFloat(); bg->tintG = arr[1].asFloat();
            bg->tintB = arr[2].asFloat(); bg->tintA = arr[3].asFloat();
        }
    }
    // texture* is runtime-only; re-loaded from path at scene load time.
    return true;
}

// ---- UICurvedBackground ----

void serializeUICurvedBackground(const void* comp, JsonWriter& w) {
    auto* bg = static_cast<const ecs::UICurvedBackground*>(comp);
    w.writeFloat4("color", bg->r, bg->g, bg->b, bg->a);
    w.writeFloat("curvature", bg->curvature);
}

bool deserializeUICurvedBackground(const JsonNode& n, void* comp) {
    auto* bg = static_cast<ecs::UICurvedBackground*>(comp);
    if (n.contains("color")) {
        auto& arr = n["color"].asArray();
        if (arr.size() >= 4) {
            bg->r = arr[0].asFloat(); bg->g = arr[1].asFloat();
            bg->b = arr[2].asFloat(); bg->a = arr[3].asFloat();
        }
    }
    if (n.contains("curvature")) bg->curvature = n["curvature"].asFloat();
    return true;
}

// ---- UIText ----

void serializeUIText(const void* comp, JsonWriter& w) {
    auto* ut = static_cast<const ecs::UIText*>(comp);
    if (ut->fontPath[0]) w.writeString("fontPath",  ut->fontPath);
    if (ut->text[0])     w.writeString("text",       ut->text);
    w.writeFloat4("color",       ut->cr, ut->cg, ut->cb, ut->ca);
    w.writeInt   ("displayPx",   ut->displayPx);
    w.writeInt   ("alignment",   ut->alignment);
    w.writeBool  ("autoSize",    ut->autoSize);
}

bool deserializeUIText(const JsonNode& n, void* comp) {
    auto* ut = static_cast<ecs::UIText*>(comp);
    if (n.contains("fontPath"))
        std::strncpy(ut->fontPath, n["fontPath"].asString().c_str(), sizeof(ut->fontPath) - 1);
    if (n.contains("text"))
        std::strncpy(ut->text, n["text"].asString().c_str(), sizeof(ut->text) - 1);
    if (n.contains("color")) {
        auto& arr = n["color"].asArray();
        if (arr.size() >= 4) {
            ut->cr = arr[0].asFloat(); ut->cg = arr[1].asFloat();
            ut->cb = arr[2].asFloat(); ut->ca = arr[3].asFloat();
        }
    }
    if (n.contains("displayPx")) ut->displayPx = n["displayPx"].asInt();
    if (n.contains("alignment")) ut->alignment  = n["alignment"].asInt();
    if (n.contains("autoSize"))  ut->autoSize   = n["autoSize"].asBool();
    // autoSizedText is a runtime cache, not persisted — left empty so the box
    // gets (re)fit against the current atlas/text on the next render after load.
    return true;
}

// ---- UIButton ----

void serializeUIButton(const void* comp, JsonWriter& w) {
    auto* b = static_cast<const ecs::UIButton*>(comp);
    w.writeFloat4("normalColor",   b->normalR,   b->normalG,   b->normalB,   b->normalA);
    w.writeFloat4("hoverColor",    b->hoverR,    b->hoverG,    b->hoverB,    b->hoverA);
    w.writeFloat4("pressedColor",  b->pressedR,  b->pressedG,  b->pressedB,  b->pressedA);
    w.writeFloat4("disabledColor", b->disabledR, b->disabledG, b->disabledB, b->disabledA);
    w.writeBool  ("enabled", b->enabled);
}

bool deserializeUIButton(const JsonNode& n, void* comp) {
    auto* b = static_cast<ecs::UIButton*>(comp);
    auto readColor = [&](const char* key, float& r, float& g, float& bl, float& a) {
        if (!n.contains(key)) return;
        auto& arr = n[key].asArray();
        if (arr.size() >= 4) {
            r = arr[0].asFloat(); g = arr[1].asFloat();
            bl = arr[2].asFloat(); a = arr[3].asFloat();
        }
    };
    readColor("normalColor",   b->normalR,   b->normalG,   b->normalB,   b->normalA);
    readColor("hoverColor",    b->hoverR,    b->hoverG,    b->hoverB,    b->hoverA);
    readColor("pressedColor",  b->pressedR,  b->pressedG,  b->pressedB,  b->pressedA);
    readColor("disabledColor", b->disabledR, b->disabledG, b->disabledB, b->disabledA);
    if (n.contains("enabled")) b->enabled = n["enabled"].asBool();
    return true;
}

void serializeTextLabel3D(const void* comp, JsonWriter& w) {
    auto* t = static_cast<const ecs::TextLabel3D*>(comp);
    if (t->fontPath[0]) w.writeString("fontPath", t->fontPath);
    if (t->text[0])     w.writeString("text",     t->text);
    w.writeFloat4("color",      t->cr, t->cg, t->cb, t->ca);
    w.writeFloat ("sizeMeters", t->sizeMeters);
    w.writeInt   ("billboard",  t->billboard);
}
bool deserializeTextLabel3D(const JsonNode& n, void* comp) {
    auto* t = static_cast<ecs::TextLabel3D*>(comp);
    if (n.contains("fontPath"))
        std::strncpy(t->fontPath, n["fontPath"].asString().c_str(), sizeof(t->fontPath) - 1);
    if (n.contains("text"))
        std::strncpy(t->text, n["text"].asString().c_str(), sizeof(t->text) - 1);
    if (n.contains("color")) {
        auto& arr = n["color"].asArray();
        if (arr.size() >= 4) {
            t->cr = arr[0].asFloat(); t->cg = arr[1].asFloat();
            t->cb = arr[2].asFloat(); t->ca = arr[3].asFloat();
        }
    }
    if (n.contains("sizeMeters")) t->sizeMeters = n["sizeMeters"].asFloat();
    if (n.contains("billboard"))  t->billboard  = n["billboard"].asInt();
    return true;
}

void serializeAnimationPlayer(const void* comp, JsonWriter& w) {
    auto* a = static_cast<const ecs::AnimationPlayer*>(comp);
    if (a->clip[0]) w.writeString("clip", a->clip);
    w.writeFloat("speed", a->speed);
    w.writeInt  ("loop",  a->loop);
    // time/playing are live playback state, not authored — always saved at rest
    // (time=0, playing=0) so a scene never loads mid-clip.
}
bool deserializeAnimationPlayer(const JsonNode& n, void* comp) {
    auto* a = static_cast<ecs::AnimationPlayer*>(comp);
    if (n.contains("clip"))
        std::strncpy(a->clip, n["clip"].asString().c_str(), sizeof(a->clip) - 1);
    if (n.contains("speed")) a->speed = n["speed"].asFloat();
    if (n.contains("loop"))  a->loop  = n["loop"].asInt();
    a->time    = 0.0f;
    a->playing = 0;
    return true;
}

} // namespace compser
