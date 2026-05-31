#include "editor/serialization/ComponentSerializers.h"
#ifdef YOPE_EDITOR
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

} // namespace compser
#endif
