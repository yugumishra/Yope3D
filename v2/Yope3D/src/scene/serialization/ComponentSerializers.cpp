#include "scene/serialization/ComponentSerializers.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "world/RenderMesh.h"
#include <cstring>
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
    w.writeFloat3("velocity", h->velocity.x, h->velocity.y, h->velocity.z);
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
    if (n.contains("velocity")) {
        auto& arr = n["velocity"].asArray();
        if (arr.size() >= 3) { h->velocity.x = arr[0].asFloat(); h->velocity.y = arr[1].asFloat(); h->velocity.z = arr[2].asFloat(); }
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
// Only the .bcbvh asset path is persisted; `compiled` is re-resolved by
// World::loadCompoundCollider on load (see ComponentSnapshot::restore).

void serializeCompoundCollider(const void* comp, JsonWriter& w) {
    auto* cc = static_cast<const ecs::CompoundCollider*>(comp);
    if (cc->assetPath[0]) w.writeString("assetPath", cc->assetPath);
}

bool deserializeCompoundCollider(const JsonNode& n, void* comp) {
    auto* cc = static_cast<ecs::CompoundCollider*>(comp);
    if (n.contains("assetPath"))
        std::strncpy(cc->assetPath, n["assetPath"].asString().c_str(), sizeof(cc->assetPath) - 1);
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

    if (mr->mesh->primitiveType == PrimitiveType::Custom &&
        !mr->mesh->sourcePath.empty()) {
        w.writeString("sourcePath", mr->mesh->sourcePath.c_str());
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
}

bool deserializeAudioSource(const JsonNode& n, void* comp) {
    auto* as = static_cast<ecs::AudioSource*>(comp);
    if (n.contains("path"))     std::strncpy(as->path, n["path"].asString().c_str(), sizeof(as->path) - 1);
    if (n.contains("gain"))     as->gain     = n["gain"].asFloat();
    if (n.contains("pitch"))    as->pitch    = n["pitch"].asFloat();
    if (n.contains("loop"))     as->loop     = n["loop"].asBool();
    if (n.contains("autoplay")) as->autoplay = n["autoplay"].asBool();
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
    if (n.contains("scriptClass"))
        std::strncpy(sc->scriptClass, n["scriptClass"].asString().c_str(),
                     sizeof(sc->scriptClass) - 1);
    if (n.contains("paramsBlob"))
        std::strncpy(sc->paramsBlob, n["paramsBlob"].asString().c_str(),
                     sizeof(sc->paramsBlob) - 1);
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
}

bool deserializeUITransform(const JsonNode& n, void* comp) {
    auto* t = static_cast<ecs::UITransform*>(comp);
    if (n.contains("minX"))    t->minX    = n["minX"].asFloat();
    if (n.contains("minY"))    t->minY    = n["minY"].asFloat();
    if (n.contains("maxX"))    t->maxX    = n["maxX"].asFloat();
    if (n.contains("maxY"))    t->maxY    = n["maxY"].asFloat();
    if (n.contains("depth"))   t->depth   = n["depth"].asInt();
    if (n.contains("visible")) t->visible = n["visible"].asBool();
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

} // namespace compser
