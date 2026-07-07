#include "World.h"
#include "../gpu/GpuDevice.h"
#include "../assets/Primitives.h"
#include "../assets/GltfLoader.h"
#include "../assets/AssetManager.h"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cassert>
#include <filesystem>
#include "../audio/AudioSystem.h"
#include "../physics/ColliderDiscrete.h"
#include "../physics/IslandDetector.h"
#include "../physics/ThreadPool.h"
#include "../physics/PhysicsConstants.h"
#include "../ecs/Components.h"
#include "TransformHierarchy.h"
#include "../scripting/Script.h"
#include "../debug/Profiler.h"
#include <cmath>
#include <cfloat>
#include <thread>
#include <algorithm>
#include <mutex>
#include <variant>

// ---- Local math helpers ----

static math::Mat3 quatToMat3(const math::Quat& q) {
    float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    math::Mat3 m;
    // column-major: m[col*3 + row]
    m.m[0] = 1-2*(yy+zz); m.m[1] = 2*(xy+wz);   m.m[2] = 2*(xz-wy);
    m.m[3] = 2*(xy-wz);   m.m[4] = 1-2*(xx+zz); m.m[5] = 2*(yz+wx);
    m.m[6] = 2*(xz+wy);   m.m[7] = 2*(yz-wx);   m.m[8] = 1-2*(xx+yy);
    return m;
}

static math::Quat normalizeQuat(math::Quat q) {
    float lenSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (lenSq > 1e-12f) {
        float inv = 1.0f / std::sqrt(lenSq);
        q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    }
    return q;
}

// ---- Primitive type detection (for raytracer parametric intersection) ----

static void setPrimitiveInfo(RenderMesh* rm, const LoadedMesh& mesh) {
    if (mesh.name == "Icosphere" || mesh.name == "Sphere") {
        rm->primitiveType = (mesh.name == "Icosphere") ? PrimitiveType::Icosphere
                                                       : PrimitiveType::Sphere;
        float maxR2 = 0.f;
        for (const auto& v : mesh.vertices)
            maxR2 = std::max(maxR2, v.position[0]*v.position[0]
                                  + v.position[1]*v.position[1]
                                  + v.position[2]*v.position[2]);
        rm->primitiveExtents = {std::sqrt(maxR2), 0.f, 0.f};
    } else if (mesh.name == "Rect" || mesh.name == "Cube") {
        rm->primitiveType = (mesh.name == "Rect") ? PrimitiveType::Rect : PrimitiveType::Cube;
        math::Vec3 e{};
        for (const auto& v : mesh.vertices) {
            e.x = std::max(e.x, std::abs(v.position[0]));
            e.y = std::max(e.y, std::abs(v.position[1]));
            e.z = std::max(e.z, std::abs(v.position[2]));
        }
        rm->primitiveExtents = e;
    } else if (mesh.name == "Plane") {
        rm->primitiveType = PrimitiveType::Plane;
        float hx = 0.f;
        for (const auto& v : mesh.vertices)
            hx = std::max(hx, std::abs(v.position[0]));
        rm->primitiveExtents = {hx, 0.f, 0.f};
    } else if (mesh.name == "Capsule") {
        rm->primitiveType = PrimitiveType::Capsule;
        float maxXZ2 = 0.f, maxY = 0.f;
        for (const auto& v : mesh.vertices) {
            maxXZ2 = std::max(maxXZ2, v.position[0]*v.position[0] + v.position[2]*v.position[2]);
            maxY   = std::max(maxY,   std::abs(v.position[1]));
        }
        float r = std::sqrt(maxXZ2);
        rm->primitiveExtents = {r, maxY - r, 0.f};  // {radius, halfHeight, 0}
    } else if (mesh.name == "Cylinder") {
        rm->primitiveType = PrimitiveType::Cylinder;
        float maxXZ2 = 0.f, maxY = 0.f;
        for (const auto& v : mesh.vertices) {
            maxXZ2 = std::max(maxXZ2, v.position[0]*v.position[0] + v.position[2]*v.position[2]);
            maxY   = std::max(maxY,   std::abs(v.position[1]));
        }
        rm->primitiveExtents = {std::sqrt(maxXZ2), maxY, 0.f};  // {radius, halfHeight, 0}
    }

    if (rm->primitiveType != PrimitiveType::Custom) {
        rm->cpuVertices.clear(); rm->cpuVertices.shrink_to_fit();
        rm->cpuIndices.clear();  rm->cpuIndices.shrink_to_fit();
    }
}

// ---- World lifecycle ----

World::World()  = default;
World::~World() = default;

void World::init(GpuDevice& gpu, VkCommandPool pool) {
    gpu_  = &gpu;
    pool_ = pool;
}

int World::getThreadCount() const {
    return threadPool_ ? static_cast<int>(threadPool_->size()) : 0;
}

int World::getHullCount() {
    int count = 0;
    for (auto [e, hc] : registry_.view<ecs::Hull>())
        ++count;
    return count;
}

// ---- Physics-only factory methods ----

ecs::Entity World::addSphere(float mass, float radius, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1.0f, 1.0f, 1.0f}});
    ecs::Hull hc;
    hc.mass        = mass;
    hc.inverseMass = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    float invI = (mass > 0.0f && radius > 0.0f)
                 ? 1.0f / (0.4f * mass * radius * radius) : 0.0f;
    hc.inverseInertia = math::Mat3::scale({invI, invI, invI});
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::SphereForm>(e, {radius});
    finalizeEntity(e, "Sphere");
    return e;
}

ecs::Entity World::addOBB(math::Vec3 extent, float mass, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1.0f, 1.0f, 1.0f}});
    ecs::Hull hc;
    hc.mass        = mass;
    hc.inverseMass = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    if (mass > 0.0f) {
        float ex = extent.x, ey = extent.y, ez = extent.z;
        // extent is half-extents, so I_x = (1/3)*m*(b²+c²), inverse = 3/(m*(b²+c²))
        hc.inverseInertia = math::Mat3::scale({
            3.0f / (mass * (ey*ey + ez*ez)),
            3.0f / (mass * (ex*ex + ez*ez)),
            3.0f / (mass * (ex*ex + ey*ey))
        });
    }
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::OBBForm>(e, {extent});
    finalizeEntity(e, "OBB");
    return e;
}

ecs::Entity World::addAABB(math::Vec3 extent, float mass, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1.0f, 1.0f, 1.0f}});
    ecs::Hull hc;
    hc.mass           = mass;
    hc.inverseMass    = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    hc.inverseInertia = math::Mat3::zero();  // AABB has no angular dynamics
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::AABBForm>(e, {extent});
    finalizeEntity(e, "AABB");
    return e;
}

ecs::Entity World::addStaticAABB(math::Vec3 pos, math::Vec3 extent) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1.0f, 1.0f, 1.0f}});
    ecs::Hull hc;
    hc.mass           = 0.0f;
    hc.inverseMass    = 0.0f;
    hc.inverseInertia = math::Mat3::zero();
    hc.gravity        = false;
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::AABBForm>(e, {extent});
    registry_.add<ecs::Fixed>(e);
    finalizeEntity(e, "StaticAABB");
    return e;
}

// ---------------------------------------------------------------------------
// Attach / detach physics body on an existing entity (editor "Add Component")
// ---------------------------------------------------------------------------

static ecs::Hull makeHull(float mass) {
    ecs::Hull h;
    h.mass        = mass;
    h.inverseMass = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    h.gravity     = (mass > 0.0f);
    return h;
}

// Returns true when the entity's mesh has geometry already baked into its vertices
// (drag-dropped OBJ). For these, tf->scale must not be overwritten by the collider
// extent — that would scale the already-world-sized vertices a second time.
static bool hasCustomMesh(ecs::Registry& reg, ecs::Entity e) {
    if (auto* mr = reg.get<ecs::MeshRenderer>(e))
        return mr->mesh && mr->mesh->primitiveType == PrimitiveType::Custom;
    return false;
}

void World::attachSphereCollider(ecs::Entity e, float mass, float radius, bool isStatic) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return;
    if (registry_.has<ecs::Hull>(e)) return;  // already has a physics body

    // v1 invariant: physics bodies are hierarchy roots. The editor's AddColliderCommand
    // auto-unparents before attaching, so a Parent here means a caller bypassed that path.
    assert(!registry_.has<ecs::Parent>(e) && "physics body must be a hierarchy root");
    ecs::Hull h = makeHull(isStatic ? 0.0f : mass);
    if (!isStatic && mass > 0.0f && radius > 0.0f) {
        float invI = 1.0f / (0.4f * mass * radius * radius);
        h.inverseInertia = math::Mat3::scale({invI, invI, invI});
    }
    registry_.add<ecs::Hull>(e, h);
    registry_.add<ecs::SphereForm>(e, {radius});
    if (isStatic) registry_.add<ecs::Fixed>(e);
    if (!hasCustomMesh(registry_, e))
        if (auto* tf = registry_.get<Transform>(e)) tf->scale = {radius, radius, radius};
    if (debugPhysics) rebuildDebugMeshes();
}

void World::attachAABBCollider(ecs::Entity e, float mass, math::Vec3 extent, bool isStatic) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return;
    if (registry_.has<ecs::Hull>(e)) return;

    // v1 invariant: physics bodies are hierarchy roots. The editor's AddColliderCommand
    // auto-unparents before attaching, so a Parent here means a caller bypassed that path.
    assert(!registry_.has<ecs::Parent>(e) && "physics body must be a hierarchy root");
    ecs::Hull h = makeHull(isStatic ? 0.0f : mass);
    h.inverseInertia = math::Mat3::zero();  // AABB has no angular dynamics
    registry_.add<ecs::Hull>(e, h);
    registry_.add<ecs::AABBForm>(e, {extent});
    if (isStatic) registry_.add<ecs::Fixed>(e);
    if (!hasCustomMesh(registry_, e))
        if (auto* tf = registry_.get<Transform>(e)) tf->scale = extent;
    if (debugPhysics) rebuildDebugMeshes();
}

void World::attachOBBCollider(ecs::Entity e, float mass, math::Vec3 extent, bool isStatic) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return;
    if (registry_.has<ecs::Hull>(e)) return;

    // v1 invariant: physics bodies are hierarchy roots. The editor's AddColliderCommand
    // auto-unparents before attaching, so a Parent here means a caller bypassed that path.
    assert(!registry_.has<ecs::Parent>(e) && "physics body must be a hierarchy root");
    ecs::Hull h = makeHull(isStatic ? 0.0f : mass);
    if (!isStatic && mass > 0.0f) {
        float ex = extent.x, ey = extent.y, ez = extent.z;
        h.inverseInertia = math::Mat3::scale({
            3.0f / (mass * (ey*ey + ez*ez)),
            3.0f / (mass * (ex*ex + ez*ez)),
            3.0f / (mass * (ex*ex + ey*ey))
        });
    }
    registry_.add<ecs::Hull>(e, h);
    registry_.add<ecs::OBBForm>(e, {extent});
    if (isStatic) registry_.add<ecs::Fixed>(e);
    if (!hasCustomMesh(registry_, e))
        if (auto* tf = registry_.get<Transform>(e)) tf->scale = extent;
    if (debugPhysics) rebuildDebugMeshes();
}

void World::attachCapsuleCollider(ecs::Entity e, float mass, float radius, float halfHeight, bool isStatic) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return;
    if (registry_.has<ecs::Hull>(e)) return;

    // v1 invariant: physics bodies are hierarchy roots. The editor's AddColliderCommand
    // auto-unparents before attaching, so a Parent here means a caller bypassed that path.
    assert(!registry_.has<ecs::Parent>(e) && "physics body must be a hierarchy root");
    ecs::Hull h = makeHull(isStatic ? 0.0f : mass);
    if (!isStatic && mass > 0.0f && radius > 0.0f && halfHeight > 0.0f) {
        float I = mass * (radius * radius * 0.25f + halfHeight * halfHeight / 3.0f);
        float invI = (I > 0.0f) ? 1.0f / I : 0.0f;
        h.inverseInertia = math::Mat3::scale({invI, invI, invI});
    }
    registry_.add<ecs::Hull>(e, h);
    registry_.add<ecs::CapsuleForm>(e, {radius, halfHeight});
    if (isStatic) registry_.add<ecs::Fixed>(e);
    if (debugPhysics) rebuildDebugMeshes();
}

void World::attachCylinderCollider(ecs::Entity e, float mass, float radius, float halfHeight, bool isStatic) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return;
    if (registry_.has<ecs::Hull>(e)) return;

    // v1 invariant: physics bodies are hierarchy roots. The editor's AddColliderCommand
    // auto-unparents before attaching, so a Parent here means a caller bypassed that path.
    assert(!registry_.has<ecs::Parent>(e) && "physics body must be a hierarchy root");
    ecs::Hull h = makeHull(isStatic ? 0.0f : mass);
    if (!isStatic && mass > 0.0f && radius > 0.0f && halfHeight > 0.0f) {
        float I = mass * (radius * radius * 0.25f + halfHeight * halfHeight / 3.0f);
        float invI = (I > 0.0f) ? 1.0f / I : 0.0f;
        h.inverseInertia = math::Mat3::scale({invI, invI, invI});
    }
    registry_.add<ecs::Hull>(e, h);
    registry_.add<ecs::CylinderForm>(e, {radius, halfHeight});
    if (isStatic) registry_.add<ecs::Fixed>(e);
    if (debugPhysics) rebuildDebugMeshes();
}

// ---- Static compound collider ----

physics::CompiledCollider* World::buildCompoundCollider(const std::string& key,
                                                        std::vector<physics::SubShape> shapes,
                                                        int leafSize) {
    std::lock_guard lk(structureMtx_);
    auto compiled = std::make_unique<physics::CompiledCollider>();
    physics::buildCompoundBvh(shapes, *compiled, leafSize);
    physics::CompiledCollider* raw = compiled.get();
    compoundColliderCache_[key] = std::move(compiled);
    return raw;
}

physics::CompiledCollider* World::loadCompoundCollider(const std::string& assetRelPath, bool forceReload) {
    std::lock_guard lk(structureMtx_);
    auto it = compoundColliderCache_.find(assetRelPath);
    if (it != compoundColliderCache_.end() && !forceReload) return it->second.get();

    std::string fullPath = (std::filesystem::path(YOPE_ASSETS_DIR) / assetRelPath).string();
    physics::CompiledCollider loaded;
    if (!physics::readBcbvh(fullPath, loaded)) return nullptr;

    if (it != compoundColliderCache_.end()) {
        // In-place update: keep the object's address stable so pointers already
        // handed out via ecs::CompoundCollider::compiled stay valid.
        *it->second = std::move(loaded);
        return it->second.get();
    }
    auto compiled = std::make_unique<physics::CompiledCollider>(std::move(loaded));
    physics::CompiledCollider* raw = compiled.get();
    compoundColliderCache_[assetRelPath] = std::move(compiled);
    return raw;
}

ecs::Entity World::attachCompoundCollider(ecs::Entity e, physics::CompiledCollider* compiled,
                                          const std::string& assetPath,
                                          float mass, bool isStatic, float density) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return e;

    if (!registry_.has<Transform>(e)) registry_.add<Transform>(e);
    if (!registry_.has<ecs::Hull>(e)) registry_.add<ecs::Hull>(e, makeHull(0.0f));

    // Always (re-)apply the mass-derived fields — not just on first attach —
    // so the editor's "Regenerate" action can flip static<->dynamic and/or
    // refresh mass/inertia from a re-bake without disturbing velocity/damping/
    // friction/etc. on an already-live body.
    ecs::Hull* h = registry_.get<ecs::Hull>(e);
    if (isStatic) {
        h->mass = 0.0f; h->inverseMass = 0.0f;
        h->inverseInertia = math::Mat3::zero();
        h->gravity = false;
    } else {
        float bodyMass = (mass > 0.0f) ? mass : (compiled ? compiled->totalMass : 0.0f);
        h->mass        = bodyMass;
        h->inverseMass = (bodyMass > 0.0f) ? 1.0f / bodyMass : 0.0f;
        h->inverseInertia = compiled ? compiled->inverseInertiaLocal : math::Mat3::zero();
        h->gravity = true;
    }
    if (isStatic) { if (!registry_.has<ecs::Fixed>(e)) registry_.add<ecs::Fixed>(e); }
    else          { if (registry_.has<ecs::Fixed>(e))  registry_.remove<ecs::Fixed>(e); }

    ecs::CompoundCollider cc{};
    std::snprintf(cc.assetPath, sizeof(cc.assetPath), "%s", assetPath.c_str());
    cc.compiled  = compiled;
    cc.density   = density;
    cc.isStatic  = isStatic;
    if (registry_.has<ecs::CompoundCollider>(e)) *registry_.get<ecs::CompoundCollider>(e) = cc;
    else                                          registry_.add<ecs::CompoundCollider>(e, cc);
    if (debugPhysics) rebuildDebugMeshes();
    return e;
}

void World::detachPhysicsBody(ecs::Entity e) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return;
    if (registry_.has<ecs::Hull>(e))         registry_.remove<ecs::Hull>(e);
    if (registry_.has<ecs::SphereForm>(e))   registry_.remove<ecs::SphereForm>(e);
    if (registry_.has<ecs::AABBForm>(e))     registry_.remove<ecs::AABBForm>(e);
    if (registry_.has<ecs::OBBForm>(e))      registry_.remove<ecs::OBBForm>(e);
    if (registry_.has<ecs::CapsuleForm>(e))  registry_.remove<ecs::CapsuleForm>(e);
    if (registry_.has<ecs::CylinderForm>(e)) registry_.remove<ecs::CylinderForm>(e);
    if (registry_.has<ecs::CompoundCollider>(e)) registry_.remove<ecs::CompoundCollider>(e);
    if (registry_.has<ecs::Fixed>(e))        registry_.remove<ecs::Fixed>(e);
    if (registry_.has<ecs::Sleeping>(e))     registry_.remove<ecs::Sleeping>(e);
    // Stale ContactCache entries are harmless — entity is no longer in Hull view
    if (debugPhysics) rebuildDebugMeshes();
}

ecs::Entity World::addCapsule(float radius, float halfHeight, float mass, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1.0f, 1.0f, 1.0f}});
    ecs::Hull hc;
    hc.mass        = mass;
    hc.inverseMass = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    if (mass > 0.0f && radius > 0.0f && halfHeight > 0.0f) {
        // Approximate solid capsule inertia (about Y principal, uniform for all axes here)
        float I = mass * (radius * radius * 0.25f + halfHeight * halfHeight / 3.0f);
        float invI = (I > 0.0f) ? 1.0f / I : 0.0f;
        hc.inverseInertia = math::Mat3::scale({invI, invI, invI});
    }
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::CapsuleForm>(e, {radius, halfHeight});
    finalizeEntity(e, "Capsule");
    return e;
}

ecs::Entity World::addCylinder(float radius, float halfHeight, float mass, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1.0f, 1.0f, 1.0f}});
    ecs::Hull hc;
    hc.mass        = mass;
    hc.inverseMass = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    if (mass > 0.0f && radius > 0.0f && halfHeight > 0.0f) {
        // Approximate solid cylinder inertia (about Y axis; uniform here)
        float I = mass * (radius * radius * 0.25f + halfHeight * halfHeight / 3.0f);
        float invI = (I > 0.0f) ? 1.0f / I : 0.0f;
        hc.inverseInertia = math::Mat3::scale({invI, invI, invI});
    }
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::CylinderForm>(e, {radius, halfHeight});
    finalizeEntity(e, "Cylinder");
    return e;
}

ecs::Entity World::addKinematicCapsule(float radius, float halfHeight, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {1.0f, 1.0f, 1.0f}});
    registry_.add<ecs::CapsuleForm>(e, ecs::CapsuleForm{radius, halfHeight});
    finalizeEntity(e, "KinematicCapsule");
    return e;
}

ecs::Entity World::addOBBFromMesh(const LoadedMesh& loadedMesh, float mass) {
    math::Vec3 mn = { FLT_MAX,  FLT_MAX,  FLT_MAX };
    math::Vec3 mx = {-FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& v : loadedMesh.vertices) {
        mn.x = std::min(mn.x, v.position[0]);
        mn.y = std::min(mn.y, v.position[1]);
        mn.z = std::min(mn.z, v.position[2]);
        mx.x = std::max(mx.x, v.position[0]);
        mx.y = std::max(mx.y, v.position[1]);
        mx.z = std::max(mx.z, v.position[2]);
    }
    math::Vec3 center     = (mn + mx) * 0.5f;
    math::Vec3 halfExtent = (mx - mn) * 0.5f;
    return addOBB(halfExtent, mass, center);
}

// ---- Visual-only factory methods ----

ecs::Entity World::addRenderObject(const std::vector<Vertex>& vertices,
                                    const std::vector<uint32_t>& indices) {
    std::lock_guard lk(structureMtx_);
    auto rm = activeUploadBatch_
        ? std::make_unique<RenderMesh>(*gpu_, *activeUploadBatch_, vertices, indices)
        : std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    rm->transformReady = true;
    RenderMesh* raw = rm.get();
    meshPool_.push_back(std::move(rm));
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e);
    registry_.add<ecs::MeshRenderer>(e, {raw});
    meshToEntity_[raw] = e;
    finalizeEntity(e, "Object");
    return e;
}

ecs::Entity World::addRenderObject(const LoadedMesh& mesh) {
    ecs::Entity e = addRenderObject(mesh.vertices, mesh.indices);
    if (auto* mr = registry_.get<ecs::MeshRenderer>(e))
        setPrimitiveInfo(mr->mesh, mesh);
    return e;
}

// Copy a parsed MaterialData (from OBJ/MTL or glTF) into an ecs::Material on e.
static void applyMaterialData(ecs::Registry& reg, ecs::Entity e, const MaterialData& md) {
    if (!md.hasMaterial) return;
    ecs::Material mat;
    auto cp = [](char* dst, const std::string& s) {
        std::strncpy(dst, s.c_str(), 255); dst[255] = '\0';
    };
    cp(mat.albedoPath,     md.albedoPath);
    cp(mat.normalPath,     md.normalPath);
    cp(mat.metalRoughPath, md.metalRoughPath);
    cp(mat.occlusionPath,  md.occlusionPath);
    cp(mat.emissivePath,   md.emissivePath);
    mat.albedoFactor[0] = md.albedoFactor.x; mat.albedoFactor[1] = md.albedoFactor.y;
    mat.albedoFactor[2] = md.albedoFactor.z; mat.albedoFactor[3] = md.albedoFactor.w;
    mat.metallicFactor  = md.metallicFactor;
    mat.roughnessFactor = md.roughnessFactor;
    mat.emissiveFactor[0] = md.emissiveFactor.x;
    mat.emissiveFactor[1] = md.emissiveFactor.y;
    mat.emissiveFactor[2] = md.emissiveFactor.z;
    mat.normalScale = md.normalScale;
    reg.add<ecs::Material>(e, mat);
}

// Shift a mesh's vertices so its AABB center is at the mesh origin; returns the
// original center (mesh-local). Imported meshes are re-centered so the entity's
// pivot sits at the geometry center — this makes "Snap to Mesh" colliders wrap the
// object (glTF node pivots are often at an object's base, not its center) and gives
// free rotation about the center of mass. No physics changes needed.
static math::Vec3 recenterMesh(LoadedMesh& m) {
    if (m.vertices.empty()) return {0.f, 0.f, 0.f};
    math::Vec3 mn{ FLT_MAX,  FLT_MAX,  FLT_MAX};
    math::Vec3 mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const auto& v : m.vertices) {
        mn.x = std::min(mn.x, v.position[0]); mx.x = std::max(mx.x, v.position[0]);
        mn.y = std::min(mn.y, v.position[1]); mx.y = std::max(mx.y, v.position[1]);
        mn.z = std::min(mn.z, v.position[2]); mx.z = std::max(mx.z, v.position[2]);
    }
    math::Vec3 c = (mn + mx) * 0.5f;
    for (auto& v : m.vertices) { v.position[0] -= c.x; v.position[1] -= c.y; v.position[2] -= c.z; }
    return c;
}

std::vector<ecs::Entity> World::addModel(const std::string& path) {
    std::string fullPath = (std::filesystem::path(YOPE_ASSETS_DIR) / path).string();
    return importModel(fullPath);
}

std::vector<ecs::Entity> World::importModel(const std::string& absPath) {
    AssetManager* assets = assets_;
    const std::string& fullPath = absPath;
    std::string ext = std::filesystem::path(absPath).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(c));

    // Helpers shared by both loader paths (structureMtx_ is recursive — safe to
    // re-acquire under the internal locks taken by addRenderObject).
    auto setName = [this](ecs::Entity e, const std::string& name) {
        if (name.empty()) return;
        std::lock_guard lk(structureMtx_);
        if (auto* n = registry_.get<ecs::Name>(e))
            std::strncpy(n->value, name.c_str(), sizeof(n->value) - 1);
    };
    auto setLocal = [this](ecs::Entity e, const Transform& local) {
        std::lock_guard lk(structureMtx_);
        if (auto* tf = registry_.get<Transform>(e)) *tf = local;
    };

    // Offset an entity's LOCAL transform so re-centering a mesh by `c` (mesh-local)
    // leaves it rendering in the same place: newLocalPos = pos + R*(scale ⊙ c).
    auto localWithMeshOffset = [](const Transform& base, const math::Vec3& c) {
        Transform t = base;
        t.position = base.position + math::Mat3::rotation(base.rotation) * base.scale.hadamard(c);
        return t;
    };

    std::vector<ecs::Entity> entities;

    if (ext == ".glb" || ext == ".gltf") {
        // Decode glTF-embedded/base64 images and register them as GPU textures.
        GltfLoader::RegisterImageFn reg;
        if (assets) {
            // Queue for background decode instead of decoding+uploading inline —
            // `data` is copied synchronously before this callback returns, so it's
            // safe even though GltfLoader may free/reuse its backing buffer after.
            // Materials are stamped with `key` immediately; MaterialCache binds the
            // 1x1 default until the streamer's per-frame pump makes it resident.
            reg = [assets](const std::string& key, const uint8_t* data, int len, bool srgb) -> std::string {
                assets->enqueueTextureDecode(key, srgb, data, len);
                return key;
            };
        }
        GltfLoader::LoadedModel model = GltfLoader::load(fullPath, reg);

        // A node that parents other nodes must keep its authored pivot — re-centering
        // it would shift its children. Only leaf mesh nodes get re-centered.
        std::vector<bool> hasChildren(model.nodes.size(), false);
        for (const auto& n : model.nodes)
            if (n.parent >= 0) hasChildren[n.parent] = true;

        // One entity per node (Option B: nodes carry local TRS, mesh verts are local).
        std::vector<ecs::Entity> nodeEntity(model.nodes.size(), ecs::NullEntity);
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            GltfLoader::LoadedNode& node = model.nodes[i];
            ecs::Entity e;
            if (node.meshes.size() == 1) {
                // Node carries its single primitive directly. Re-center to the geometry
                // center (unless it parents children, whose frames depend on this pivot).
                // Re-center BEFORE upload so the GPU mesh gets the centered verts.
                math::Vec3 c = hasChildren[i] ? math::Vec3{0, 0, 0}
                                              : recenterMesh(node.meshes[0]);
                e = addRenderObject(node.meshes[0]);
                { std::lock_guard lk(structureMtx_); applyMaterialData(registry_, e, node.meshes[0].material); }
                setLocal(e, localWithMeshOffset(node.local, c));
            } else {
                // Bare transform/group node (0 primitives, or a parent of N>1 prims).
                { std::lock_guard lk(structureMtx_);
                  e = registry_.create();
                  registry_.add<Transform>(e);
                  finalizeEntity(e, node.name.empty() ? "Node" : node.name.c_str()); }
                setLocal(e, node.local);
            }
            setName(e, node.name);
            nodeEntity[i] = e;
            entities.push_back(e);

            // N>1 primitives → one child entity per primitive, each re-centered so
            // its own pivot (and collider) lands on that primitive's geometry.
            if (node.meshes.size() > 1) {
                for (LoadedMesh& prim : node.meshes) {
                    math::Vec3 c = recenterMesh(prim);
                    ecs::Entity pe = addRenderObject(prim);
                    std::lock_guard lk(structureMtx_);
                    applyMaterialData(registry_, pe, prim.material);
                    if (auto* tf = registry_.get<Transform>(pe)) tf->position = c;
                    registry_.add<ecs::Parent>(pe, ecs::Parent{e});
                    entities.push_back(pe);
                }
            }
        }

        // Wire Parent links for non-root nodes (nodes are topologically ordered).
        {
            std::lock_guard lk(structureMtx_);
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                int p = model.nodes[i].parent;
                if (p >= 0 && registry_.valid(nodeEntity[p]))
                    registry_.add<ecs::Parent>(nodeEntity[i], ecs::Parent{nodeEntity[p]});
            }
        }
    } else {
        LoadedMesh m = ObjLoader::load(fullPath);
        math::Vec3 c = recenterMesh(m);
        ecs::Entity e = addRenderObject(m);   // Transform + MeshRenderer (locks internally)
        { std::lock_guard lk(structureMtx_);
          applyMaterialData(registry_, e, m.material);
          if (auto* tf = registry_.get<Transform>(e)) tf->position = c; }   // pivot at centroid
        entities.push_back(e);
    }

    // Group a multi-object import under one named root, so the whole model is a
    // single subtree in the hierarchy (select/move/delete it as a unit). A single
    // top-level object is left as its own root (no redundant wrapper).
    std::vector<ecs::Entity> topRoots;
    {
        std::lock_guard lk(structureMtx_);
        for (ecs::Entity e : entities)
            if (registry_.valid(e) && !registry_.has<ecs::Parent>(e)) topRoots.push_back(e);
    }
    if (topRoots.size() > 1) {
        std::string modelName = std::filesystem::path(absPath).stem().string();
        math::Vec3 avg{0.f, 0.f, 0.f};
        {
            std::lock_guard lk(structureMtx_);
            for (ecs::Entity e : topRoots)
                if (auto* tf = registry_.get<Transform>(e)) avg = avg + tf->position;
        }
        avg = avg * (1.0f / float(topRoots.size()));

        std::lock_guard lk(structureMtx_);
        ecs::Entity holder = registry_.create();
        registry_.add<Transform>(holder, Transform{avg, {0, 0, 0, 1}, {1, 1, 1}});
        finalizeEntity(holder, modelName.empty() ? "Model" : modelName.c_str());
        for (ecs::Entity e : topRoots) {
            if (auto* tf = registry_.get<Transform>(e)) tf->position = tf->position - avg;
            registry_.add<ecs::Parent>(e, ecs::Parent{holder});
        }
        entities.push_back(holder);
    }
    return entities;
}

void World::reregisterEmbeddedTextures(const std::string& glbAbsPath) {
    if (!assets_) return;
    AssetManager* assets = assets_;
    // Queue for background decode (see importModel's registerImage callback above
    // for why this is safe/deferred) — this is what used to block scene load for
    // 40-60s decoding+uploading every embedded image synchronously.
    GltfLoader::RegisterImageFn reg =
        [assets](const std::string& key, const uint8_t* data, int len, bool srgb) -> std::string {
            assets->enqueueTextureDecode(key, srgb, data, len);
            return key;
        };
    // Re-run the loader purely for its registerImage side-effects (geometry discarded).
    try { GltfLoader::load(glbAbsPath, reg); } catch (...) {}
}

// ---- Attach mesh to existing entity ----

RenderMesh* World::attachMesh(ecs::Entity e,
                               const std::vector<Vertex>& vertices,
                               const std::vector<uint32_t>& indices) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return nullptr;
    auto rm = activeUploadBatch_
        ? std::make_unique<RenderMesh>(*gpu_, *activeUploadBatch_, vertices, indices)
        : std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    RenderMesh* raw = rm.get();
    // Physics entities get transformReady set by publishSnapshot (to avoid 0,0,0 flicker).
    // Render-only entities are ready immediately.
    if (!registry_.has<ecs::Hull>(e))
        raw->transformReady = true;
    meshPool_.push_back(std::move(rm));
    if (registry_.has<ecs::MeshRenderer>(e))
        registry_.get<ecs::MeshRenderer>(e)->mesh = raw;
    else
        registry_.add<ecs::MeshRenderer>(e, {raw});
    meshToEntity_[raw] = e;
    return raw;
}

RenderMesh* World::attachMesh(ecs::Entity e, const LoadedMesh& mesh) {
    RenderMesh* rm = attachMesh(e, mesh.vertices, mesh.indices);
    if (rm) setPrimitiveInfo(rm, mesh);
    return rm;
}

// ---- Entity/mesh accessors ----

RenderMesh* World::getMesh(ecs::Entity e) {
    auto* mr = registry_.get<ecs::MeshRenderer>(e);
    return mr ? mr->mesh : nullptr;
}

// ---- Remove entity ----

void World::removeEntity(ecs::Entity e) {
    if (!registry_.valid(e)) return;
    std::lock_guard lk(structureMtx_);

    RenderMesh* mesh = nullptr;
    if (auto* mr = registry_.get<ecs::MeshRenderer>(e)) mesh = mr->mesh;

    // Delete any attached script instance. onUnload is NOT called here — that
    // hook requires a ScriptContext and runs only for orderly scene-level
    // teardown via SceneManager. Mid-frame entity removal is best-effort.
    if (auto* sc = registry_.get<ecs::ScriptComponent>(e); sc && sc->instance) {
        delete sc->instance;
        sc->instance = nullptr;
    }

    // Free the bound OpenAL Source if the entity owned one.
    if (audio_) {
        if (auto* as = registry_.get<ecs::AudioSource>(e); as && as->source) {
            audio_->removeSource(as->source);
            as->source = nullptr;
        }
    }

    // Cascade-delete children (entities parented to e). structureMtx_ is recursive,
    // so the recursive removeEntity calls are safe. Collect first, then remove — we
    // must not mutate archetypes while iterating the Parent view.
    {
        std::vector<ecs::Entity> children;
        for (auto [child, p] : registry_.view<ecs::Parent>())
            if (p.parent == e) children.push_back(child);
        for (ecs::Entity c : children) removeEntity(c);
    }

    // Remove springs that reference this entity; recursively remove their visual/proxy entities.
    std::vector<ecs::Entity> dependent;
    springs_.erase(std::remove_if(springs_.begin(), springs_.end(),
        [&](const std::unique_ptr<physics::Spring>& s) {
            if (s->first_ == e || s->second_ == e) {
                if (registry_.valid(s->visualMeshEntity_) && s->visualMeshEntity_ != e)
                    dependent.push_back(s->visualMeshEntity_);
                for (ecs::Entity p : s->proxies_)
                    if (registry_.valid(p) && p != e) dependent.push_back(p);
                return true;
            }
            return false;
        }), springs_.end());

    for (ecs::Entity dep : dependent) removeEntity(dep);

    // Remove proxy back-references in surviving springs.
    for (auto& s : springs_)
        s->proxies_.erase(std::remove(s->proxies_.begin(), s->proxies_.end(), e), s->proxies_.end());

    // Purge contact cache entries keyed by this entity.
    std::erase_if(contactCache_,
        [e](const auto& kv) { return kv.first.a == e || kv.first.b == e; });

    if (mesh) meshToEntity_.erase(mesh);

    // Move the mesh out of meshPool_ into the pending-destroy queue instead of
    // destroying immediately. The VkBuffers may still be referenced by the
    // current frame's command buffer (if removeEntity is called mid-recording,
    // e.g. from a panel context menu). flushPendingGpuDestroys() is called at
    // the start of the next tick, before recording opens, where a device sync
    // makes it safe to free the Vulkan resources.
    if (mesh) {
        auto it = std::find_if(meshPool_.begin(), meshPool_.end(),
            [mesh](const std::unique_ptr<RenderMesh>& m) { return m.get() == mesh; });
        if (it != meshPool_.end()) {
            pendingGpuDestroy_.push_back(std::move(*it));
            meshPool_.erase(it);
        }
    }

    registry_.destroy(e);

    if (debugPhysics) {
        destroyDebugMeshes();
        rebuildDebugMeshes();
    }
}

void World::flushPendingGpuDestroys() {
    if (pendingGpuDestroy_.empty()) return;
    // Wait for all in-flight GPU work to finish before freeing Vulkan buffers.
    // This is safe to call pre-recording (before vkBeginCommandBuffer) which is
    // the only time EditorApp invokes this function.
    if (gpu_) gpu_->syncDevice();
    for (auto& m : pendingGpuDestroy_)
        if (m) m->destroy(gpu_->device());
    pendingGpuDestroy_.clear();
}

// ---- Capsule render-mesh rebuild ----

void World::rebuildCapsuleMesh(ecs::Entity e) {
    if (!registry_.valid(e) || !gpu_) return;
    auto* cf = registry_.get<ecs::CapsuleForm>(e);
    if (!cf) return;

    LoadedMesh mesh = Primitives::capsule(cf->radius, cf->halfHeight);

    // Save old mesh color, then queue old GPU buffers for deferred destruction.
    float col[3] = {1.f, 1.f, 1.f};
    if (auto* mr = registry_.get<ecs::MeshRenderer>(e)) {
        if (mr->mesh) {
            col[0] = mr->mesh->color[0]; col[1] = mr->mesh->color[1]; col[2] = mr->mesh->color[2];
            auto it = std::find_if(meshPool_.begin(), meshPool_.end(),
                [mr](const auto& p) { return p.get() == mr->mesh; });
            if (it != meshPool_.end()) {
                meshToEntity_.erase(mr->mesh);
                pendingGpuDestroy_.push_back(std::move(*it));
                meshPool_.erase(it);
            }
        }
    }

    auto rm = std::make_unique<RenderMesh>(*gpu_, pool_, mesh.vertices, mesh.indices);
    RenderMesh* raw = rm.get();
    raw->color[0] = col[0]; raw->color[1] = col[1]; raw->color[2] = col[2];
    raw->transformReady = true;
    setPrimitiveInfo(raw, mesh);
    meshPool_.push_back(std::move(rm));
    if (auto* mr = registry_.get<ecs::MeshRenderer>(e)) mr->mesh = raw;
    else registry_.add<ecs::MeshRenderer>(e, {raw});
    meshToEntity_[raw] = e;
}

// ---- Springs ----

physics::Spring* World::addSpring(ecs::Entity a, ecs::Entity b, float k, float rest) {
    std::lock_guard lk(structureMtx_);
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    ecs::Entity springEnt = registry_.create();
    registry_.add<ecs::SpringConstraint>(springEnt, {b, k, rest});
    return springs_.back().get();
}

physics::Spring* World::addSpringWithProxies(ecs::Entity a, ecs::Entity b,
                                              float k, float rest,
                                              int proxyCount, float proxyRadius) {
    std::lock_guard lk(structureMtx_);
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs_.back().get();

    math::Vec3 posA{}, posB{};
    if (auto* tf = registry_.get<Transform>(a)) posA = tf->position;
    if (auto* tf = registry_.get<Transform>(b)) posB = tf->position;

    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        ecs::Entity proxyEnt = addSphere(1.0f, proxyRadius, posA + (posB - posA) * t);
        if (auto* phc = registry_.get<ecs::Hull>(proxyEnt)) {
            phc->inverseMass    = 0.0f;
            phc->inverseInertia = math::Mat3::zero();
            phc->velocity       = {};
            phc->omega          = {};
            phc->gravity        = false;
        }
        if (!registry_.has<ecs::Fixed>(proxyEnt))
            registry_.add<ecs::Fixed>(proxyEnt);
        s->proxies_.push_back(proxyEnt);
    }
    ecs::Entity springEnt = registry_.create();
    registry_.add<ecs::SpringConstraint>(springEnt, {b, k, rest});
    return s;
}

physics::Spring* World::addSpringWithMesh(ecs::Entity a, ecs::Entity b,
                                          float k, float rest, int coils,
                                          float coilRadius, float tubeRadius,
                                          int proxyCount, float proxyRadius) {
    std::lock_guard lk(structureMtx_);
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs_.back().get();

    auto [verts, inds] = physics::Spring::generateCoilMesh(coils, coilRadius, tubeRadius);
    ecs::Entity coilEnt = addRenderObject(verts, inds);
    s->visualMeshEntity_ = coilEnt;
    auto [spos, srot, sscale] = s->computeSpringTransform(registry_);
    if (auto* m = getMesh(coilEnt))
        m->modelMatrix = Transform{spos, srot, sscale}.getModelMatrix();

    math::Vec3 posA{}, posB{};
    if (auto* tf = registry_.get<Transform>(a)) posA = tf->position;
    if (auto* tf = registry_.get<Transform>(b)) posB = tf->position;

    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        ecs::Entity proxyEnt = addSphere(1.0f, proxyRadius, posA + (posB - posA) * t);
        if (auto* phc = registry_.get<ecs::Hull>(proxyEnt)) {
            phc->inverseMass    = 0.0f;
            phc->inverseInertia = math::Mat3::zero();
            phc->velocity       = {};
            phc->omega          = {};
            phc->gravity        = false;
        }
        if (!registry_.has<ecs::Fixed>(proxyEnt))
            registry_.add<ecs::Fixed>(proxyEnt);
        s->proxies_.push_back(proxyEnt);
    }
    ecs::Entity springEnt = registry_.create();
    registry_.add<ecs::SpringConstraint>(springEnt, {b, k, rest});
    if (auto* m = getMesh(coilEnt)) registry_.add<ecs::MeshRenderer>(springEnt, {m});
    return s;
}

void World::addSpringPhysics(ecs::Entity a, ecs::Entity b, float k, float rest) {
    std::lock_guard lk(structureMtx_);
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
}

void World::removeSpringBetween(ecs::Entity a, ecs::Entity b) {
    std::lock_guard lk(structureMtx_);
    springs_.erase(std::remove_if(springs_.begin(), springs_.end(),
        [&](const std::unique_ptr<physics::Spring>& s) {
            return (s->first_ == a && s->second_ == b) ||
                   (s->first_ == b && s->second_ == a);
        }), springs_.end());
}

// ---- Script physics helpers ----

void World::wake(ecs::Entity e) {
    std::lock_guard lk(structureMtx_);
    // Zero sleepFrames BEFORE removing the tag — remove<Sleeping> migrates the
    // archetype and memcpy-moves the Hull, invalidating any prior pointer.
    if (auto* h = registry_.get<ecs::Hull>(e)) h->sleepFrames = 0;
    if (registry_.has<ecs::Sleeping>(e)) registry_.remove<ecs::Sleeping>(e);
}

void World::applyImpulse(ecs::Entity e, math::Vec3 impulse) {
    std::lock_guard lk(structureMtx_);
    auto* h = registry_.get<ecs::Hull>(e);
    if (!h || h->inverseMass <= 0.0f) return;
    h->velocity += impulse * h->inverseMass;   // write before wake() may migrate the Hull
    wake(e);                                    // recursive mutex — safe to re-lock
}

void World::applyImpulseAt(ecs::Entity e, math::Vec3 impulse, math::Vec3 worldPoint) {
    std::lock_guard lk(structureMtx_);
    auto* h  = registry_.get<ecs::Hull>(e);
    auto* tf = registry_.get<Transform>(e);
    if (!h || !tf || h->inverseMass <= 0.0f) return;
    math::Vec3 r = worldPoint - tf->position;
    math::Mat3 R = math::Mat3::rotation(tf->rotation);
    math::Mat3 worldInvI = R * h->inverseInertia * R.transpose();
    h->velocity += impulse * h->inverseMass;
    h->omega    += worldInvI * r.cross(impulse);
    wake(e);
}

void World::addDebugLine(math::Vec3 a, math::Vec3 b, math::Vec3 color) {
    debugLines_.push_back({ a.x, a.y, a.z, color.x, color.y, color.z, 1.0f });
    debugLines_.push_back({ b.x, b.y, b.z, color.x, color.y, color.z, 1.0f });
}

// Order-independent 64-bit key for an entity pair (generation ignored — adequate
// for frame-to-frame event matching).
static uint64_t pairKey(ecs::Entity a, ecs::Entity b) {
    uint32_t lo = a.id < b.id ? a.id : b.id;
    uint32_t hi = a.id < b.id ? b.id : a.id;
    return (static_cast<uint64_t>(lo) << 32) | hi;
}

void World::detectCollisionEvents() {
    // Build this tick's contact-pair set, but only for pairs where at least one side
    // carries a behavior — keeps the diff/queue tiny and meaningful.
    std::unordered_map<uint64_t, std::pair<ecs::Entity, ecs::Entity>> current;
    current.reserve(advanceContacts_.size());
    for (const auto& c : advanceContacts_) {
        if (!registry_.has<ecs::ScriptComponent>(c.a) &&
            !registry_.has<ecs::ScriptComponent>(c.b)) continue;
        current.emplace(pairKey(c.a, c.b), std::make_pair(c.a, c.b));
    }

    std::vector<CollisionEvent> evs;
    for (const auto& [k, ab] : current)
        if (!prevContactPairs_.count(k)) evs.push_back({ ab.first, ab.second, true });
    for (const auto& [k, ab] : prevContactPairs_)
        if (!current.count(k)) evs.push_back({ ab.first, ab.second, false });

    prevContactPairs_ = std::move(current);

    if (!evs.empty()) {
        std::lock_guard<std::mutex> lk(collisionEventMtx_);
        for (auto& e : evs) collisionEvents_.push_back(e);
        // Safety cap in case nothing drains (e.g. behaviors but no consumer this frame).
        if (collisionEvents_.size() > 8192)
            collisionEvents_.erase(collisionEvents_.begin(),
                collisionEvents_.end() - 8192);
    }
}

std::vector<World::CollisionEvent> World::drainCollisionEvents() {
    std::lock_guard<std::mutex> lk(collisionEventMtx_);
    std::vector<CollisionEvent> out;
    out.swap(collisionEvents_);
    return out;
}

// ---- Lights ----

ecs::Entity World::addLight(const Light& light) {
    ecs::LightSource ls;
    const char* lightName = "Light";
    std::visit([&](const auto& l) {
        using T = std::decay_t<decltype(l)>;
        if constexpr (std::is_same_v<T, PointLight>) {
            ls.type = 0;  lightName = "PointLight";
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            std::copy(l.position, l.position + 3, ls.position);
            ls.constant = l.constant; ls.linear = l.linear; ls.quadratic = l.quadratic;
        } else if constexpr (std::is_same_v<T, DirectionalLight>) {
            ls.type = 1;  lightName = "DirLight";
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            std::copy(l.direction, l.direction + 3, ls.direction);
        } else if constexpr (std::is_same_v<T, SpotLight>) {
            ls.type = 2;  lightName = "SpotLight";
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            std::copy(l.position, l.position + 3, ls.position);
            std::copy(l.direction, l.direction + 3, ls.direction);
            ls.constant = l.constant; ls.linear = l.linear; ls.quadratic = l.quadratic;
            ls.innerConeAngle = l.innerConeAngle; ls.outerConeAngle = l.outerConeAngle;
        } else if constexpr (std::is_same_v<T, FlashLight>) {
            ls.type = 3;  lightName = "FlashLight";
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            ls.constant = l.constant; ls.linear = l.linear; ls.quadratic = l.quadratic;
            ls.innerConeAngle = l.innerConeAngle; ls.outerConeAngle = l.outerConeAngle;
        }
    }, light);

    ecs::Entity e = registry_.create();
    registry_.add<ecs::LightSource>(e, ls);
    lightEntities_.push_back(e);
    finalizeEntity(e, lightName);
    return e;
}

ecs::Entity World::addAudioSourceEntity(math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    Transform tf{};
    tf.position = pos;
    registry_.add<Transform>(e, tf);
    registry_.add<ecs::AudioSource>(e, {});  // empty: no Source* yet, user binds .wav later
    finalizeEntity(e, "AudioSource");
    return e;
}

ecs::Entity World::addUIBackground(math::Vec2 min, math::Vec2 max, math::Vec4 color, int depth) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    ecs::UITransform uiTf{};
    uiTf.minX = min.x; uiTf.minY = min.y;
    uiTf.maxX = max.x; uiTf.maxY = max.y;
    uiTf.depth = depth;
    registry_.add<ecs::UITransform>(e, uiTf);
    ecs::UIBackground bg{};
    bg.r = color.x; bg.g = color.y; bg.b = color.z; bg.a = color.w;
    registry_.add<ecs::UIBackground>(e, bg);
    finalizeEntity(e, "UI Background");
    return e;
}

ecs::Entity World::addUITexturedBackground(math::Vec2 min, math::Vec2 max,
                                            math::Vec4 tint, const char* texPath, int depth) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    ecs::UITransform uiTf{};
    uiTf.minX = min.x; uiTf.minY = min.y;
    uiTf.maxX = max.x; uiTf.maxY = max.y;
    uiTf.depth = depth;
    registry_.add<ecs::UITransform>(e, uiTf);
    ecs::UITexturedBackground bg{};
    if (texPath) std::strncpy(bg.path, texPath, sizeof(bg.path) - 1);
    bg.tintR = tint.x; bg.tintG = tint.y; bg.tintB = tint.z; bg.tintA = tint.w;
    registry_.add<ecs::UITexturedBackground>(e, bg);
    finalizeEntity(e, "UI Textured BG");
    return e;
}

ecs::Entity World::addUICurvedBackground(math::Vec2 min, math::Vec2 max,
                                          math::Vec4 color, float curvature, int depth) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    ecs::UITransform uiTf{};
    uiTf.minX = min.x; uiTf.minY = min.y;
    uiTf.maxX = max.x; uiTf.maxY = max.y;
    uiTf.depth = depth;
    registry_.add<ecs::UITransform>(e, uiTf);
    ecs::UICurvedBackground bg{};
    bg.r = color.x; bg.g = color.y; bg.b = color.z; bg.a = color.w;
    bg.curvature = curvature;
    registry_.add<ecs::UICurvedBackground>(e, bg);
    finalizeEntity(e, "UI Curved BG");
    return e;
}

ecs::Entity World::addTextLabel3D(const char* fontPath, const char* text, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    Transform tf{};
    tf.position = pos;
    registry_.add<Transform>(e, tf);
    ecs::TextLabel3D t{};
    if (fontPath) std::strncpy(t.fontPath, fontPath, sizeof(t.fontPath) - 1);
    if (text)     std::strncpy(t.text, text, sizeof(t.text) - 1);
    registry_.add<ecs::TextLabel3D>(e, t);
    finalizeEntity(e, "Text Label");
    return e;
}

ecs::Entity World::addUIText(const char* fontPath, const char* text,
                              math::Vec2 min, math::Vec2 max, int depth) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    ecs::UITransform uiTf{};
    uiTf.minX = min.x; uiTf.minY = min.y;
    uiTf.maxX = max.x; uiTf.maxY = max.y;
    uiTf.depth = depth;
    registry_.add<ecs::UITransform>(e, uiTf);
    ecs::UIText ut{};
    if (fontPath) std::strncpy(ut.fontPath, fontPath, sizeof(ut.fontPath) - 1);
    if (text)     std::strncpy(ut.text, text, sizeof(ut.text) - 1);
    registry_.add<ecs::UIText>(e, ut);
    finalizeEntity(e, "UI Text");
    return e;
}

void World::removeLight(int index) {
    if (index >= 0 && index < static_cast<int>(lightEntities_.size())) {
        registry_.destroy(lightEntities_[index]);
        lightEntities_.erase(lightEntities_.begin() + index);
    }
}

void World::removeLight(ecs::Entity e) {
    std::lock_guard lk(structureMtx_);
    auto it = std::find(lightEntities_.begin(), lightEntities_.end(), e);
    if (it != lightEntities_.end()) lightEntities_.erase(it);
    if (registry_.valid(e)) removeEntity(e);   // full cleanup (mesh/springs/cache + destroy)
}

void World::setMeshVisible(ecs::Entity e, bool visible) {
    if (RenderMesh* m = getMesh(e)) m->visible = visible;
}

// ---- resetPhysics ----

void World::resetPhysics() {
    std::lock_guard lk(structureMtx_);
    if (gpu_) gpu_->syncDevice();

    destroyDebugMeshes();

    // Free any live script instances before the registry is rebuilt.
    // SceneManager calls destroyAllInstances first (so onUnload runs) — this
    // is the safety net for direct callers (e.g. editor "new scene" action).
    for (auto [e, sc] : registry_.view<ecs::ScriptComponent>()) {
        if (sc.instance) { delete sc.instance; sc.instance = nullptr; }
    }

    for (auto& m : meshPool_)
        if (m) m->destroy(gpu_->device());
    meshPool_.clear();
    springs_.clear();
    contactCache_.clear();
    meshToEntity_.clear();

    // Collision-event state must not survive a scene swap: the fresh registry
    // recycles entity ids from 0, so a stale pair set would fire spurious exits at
    // unrelated new entities and swallow genuine enters whose key collides.
    prevContactPairs_.clear();
    { std::lock_guard<std::mutex> elk(collisionEventMtx_); collisionEvents_.clear(); }

    {
        std::lock_guard slk(snapshotMtx_);
        snapshotFront_.clear();
        snapshotBack_.clear();
        springSnapshotFront_.clear();
        springSnapshotBack_.clear();
    }
    newSnapshotReady_.store(false, std::memory_order_release);

    registry_ = ecs::Registry{};
    lightEntities_.clear();
}

// ---- cleanup ----

void World::cleanup() {
    std::lock_guard lk(structureMtx_);
    if (gpu_) gpu_->syncDevice();
    for (auto& m : pendingGpuDestroy_)
        if (m) m->destroy(gpu_->device());
    pendingGpuDestroy_.clear();
    destroyDebugMeshes();
    for (auto [e, sc] : registry_.view<ecs::ScriptComponent>()) {
        if (sc.instance) { delete sc.instance; sc.instance = nullptr; }
    }
    for (auto& m : meshPool_)
        if (m) m->destroy(gpu_->device());
    meshPool_.clear();
    springs_.clear();
    contactCache_.clear();
    meshToEntity_.clear();
    registry_ = ecs::Registry{};
    lightEntities_.clear();
}

// ---- Simulation step ----

void World::advance(float dt) {
    if (paused_.load(std::memory_order_relaxed)) return;
    tickCount_.fetch_add(1, std::memory_order_relaxed);
    YOPE_PROF_STEP("physics");
    std::lock_guard lk(structureMtx_);

    // Per-step ECS structure snapshot — drives the archetype/migration plots
    // and lets analyze_profile.py spot archetype churn vs. step time.
    YOPE_PROF_SET_ARCHETYPE_COUNT(registry_.archetypeCount());
    YOPE_PROF_SET_ARCHETYPE_MIGRATIONS(registry_.archetypeMigrationCount());

    // Build entity list and pre-compute world-space inverse inertia tensors.
    // The *_query scope below measures pure view-walk cost (no work) so the
    // delta against entity_list_build isolates the math/lookups for the
    // AoS-vs-SoA Phase E decision (yope3d_phase2_design.md §5.9).
    {
        YOPE_PROF_SCOPE("entity_list_build_query", "physics");
        static volatile int sink = 0;
        int acc = 0;
        for (auto [e, hc] : registry_.view<ecs::Hull>()) {
            (void)e;
            acc += hc.sleepFrames;
        }
        sink = acc;
    }
    {
        YOPE_PROF_SCOPE("entity_list_build", "physics");
        advanceEntities_.clear();
        for (auto [e, hc] : registry_.view<ecs::Hull>()) {
            advanceEntities_.push_back(e);
            auto* tf = registry_.get<Transform>(e);
            if (!tf || registry_.has<ecs::Fixed>(e)) {
                hc.inertiaTensorWorld = math::Mat3::zero();
                continue;
            }
            math::Mat3 R = quatToMat3(tf->rotation);
            hc.inertiaTensorWorld = R * hc.inverseInertia * R.transpose();
        }
    }
    YOPE_PROF_SET_OBJECT_COUNT(advanceEntities_.size());

    // 0. Sync spring proxies.
    {
        YOPE_PROF_SCOPE("spring_proxy_sync", "physics");
        for (auto& s : springs_)
            s->syncProxies(registry_);
    }

    // 1. SAP broadphase.
    {
        YOPE_PROF_SCOPE("broadphase_sap", "physics");
        sap_.collectPairs(advanceEntities_, registry_, sapPairs_);
    }

    // 2. Narrow-phase detection.
    {
        YOPE_PROF_SCOPE("narrowphase_detect", "physics");
        physics::ColliderDiscrete::resetNarrowphaseTiming();
        advanceContacts_.clear();
        for (auto& [ea, eb] : sapPairs_) {
            bool aFixed = registry_.has<ecs::Fixed>(ea);
            bool bFixed = registry_.has<ecs::Fixed>(eb);
            bool aSleep = registry_.has<ecs::Sleeping>(ea);
            bool bSleep = registry_.has<ecs::Sleeping>(eb);
            if (aFixed && bFixed) continue;
            if (aSleep && bSleep) continue;
            if (aSleep && bFixed) continue;
            if (aFixed && bSleep) continue;
            physics::ColliderDiscrete::detect(ea, eb, registry_, advanceContacts_);
        }
        physics::ColliderDiscrete::emitNarrowphaseProfile();
    }
    YOPE_PROF_SET_CONTACT_COUNT(advanceContacts_.size());

    // 2b. Collision enter/exit events (only when a behavior is listening). Uses the
    // freshly-built advanceContacts_ — runs even when empty so separations fire exits.
    if (collisionEventsEnabled_.load(std::memory_order_relaxed))
        detectCollisionEvents();

    // 3. Island-partitioned PGS solve.
    lastIslandCount_ = 0;
    if (!advanceContacts_.empty()) {
        if (!threadPool_) {
            unsigned int n = std::max(1u, std::thread::hardware_concurrency() - 1u);
            threadPool_ = std::make_unique<ThreadPool>(n);
        }
        // Collect spring entity pairs so the island builder can merge contact
        // islands that are bridged only by a spring (not a direct collision).
        std::vector<std::pair<ecs::Entity, ecs::Entity>> springPairs;
        springPairs.reserve(springs_.size());
        for (const auto& s : springs_)
            springPairs.push_back({s->first_, s->second_});

        std::vector<physics::Island> islands;
        {
            YOPE_PROF_SCOPE("island_build", "physics");
            islandDetector_.build(advanceContacts_, contactCache_, islands, registry_, springPairs);
        }
        lastIslandCount_ = static_cast<int>(islands.size());
        YOPE_PROF_SET_ISLAND_COUNT(lastIslandCount_);
        {
            YOPE_PROF_SCOPE("pgs_solve", "physics");
            {
                YOPE_PROF_SCOPE("pgs_dispatch", "physics");
                for (auto& island : islands) {
                    const int islandContactN = static_cast<int>(island.contacts.size());
                    threadPool_->enqueue([&island, dt, &reg = registry_, islandContactN]() mutable {
                        // Worker-thread scope. Stamped with scope_n = contact count for this
                        // island, so analyze_profile.py can plot solve-time vs. island size
                        // and spot the single-giant-island case where parallelism collapses.
                        YOPE_PROF_SCOPE_N("pgs_island", "physics", islandContactN);
                        physics::ColliderDiscrete::solveIsland(island.contacts, dt, reg, island.localCache);
                    });
                }
            }
            {
                YOPE_PROF_SCOPE("pgs_wait", "physics");
                threadPool_->wait();
            }
        }
        physics::IslandDetector::mergeCache(islands, contactCache_);
    }

    // 4. Integration + sleep check.
    // Rewritten to iterate view<Hull, Transform> directly — the previous
    // pattern walked advanceEntities_ and did random get<Hull>/get<Transform>
    // lookups per entity, which the A1 query data showed cost ~26% of the
    // integration scope (0.18 µs/entity of pure lookup overhead). The view
    // walks archetypes contiguously, eliminating the random-access lookups.
    // Sleep tags are still applied AFTER the view loop — archetype mutation
    // during iteration is not safe.
    {
        // Query sentinel — measures pure contiguous view-walk cost. After the
        // rewrite this should drop sharply vs. the old version (which was
        // measuring random-lookup cost). Delta to `integration` is now closer
        // to "actual math" rather than "math + lookup overhead".
        YOPE_PROF_SCOPE("integration_query", "physics");
        static volatile int sink = 0;
        int acc = 0;
        for (auto [e, hc, tf] : registry_.view<ecs::Hull, Transform>()) {
            (void)e; (void)tf;
            acc += hc.sleepFrames;
        }
        sink = acc;
    }
    YOPE_PROF_SCOPE("integration", "physics");
    std::vector<ecs::Entity> toSleep;
    for (auto [e, hc, tf] : registry_.view<ecs::Hull, Transform>()) {
        if (!hc.tangible) continue;
        if (registry_.has<ecs::Fixed>(e) || registry_.has<ecs::Sleeping>(e)) {
            hc.pseudoVel   = {};
            hc.pseudoOmega = {};
            continue;
        }

        // Gravity (applied once per full step, same as Hull::advance isFirst guard)
        if (hc.gravity)
            hc.velocity += gravity * dt;

        // Damping — skip if decay would flip sign (matches Hull::advance: if linDecay > 0)
        float linDecay = 1.0f - hc.linearDamping  * dt;
        float angDecay = 1.0f - hc.angularDamping * dt;
        if (linDecay > 0.0f) hc.velocity = hc.velocity * linDecay;
        if (angDecay > 0.0f) hc.omega    = hc.omega    * angDecay;

        // Integrate position
        tf.position += hc.velocity * dt;

        // Integrate rotation — exact axis-angle left-multiply (matches Hull::advance)
        float omegaLen = hc.omega.length();
        if (omegaLen > 1e-7f) {
            float angle = omegaLen * dt;
            math::Quat dq = math::Quat::fromAxisAngle(hc.omega * (1.0f / omegaLen), angle);
            tf.rotation = dq * tf.rotation;
            tf.rotation = normalizeQuat(tf.rotation);
        }

        // Split-impulse pseudo-velocity correction (matches Hull::advance)
        tf.position += hc.pseudoVel * dt;
        constexpr float MAX_PSEUDO_OMEGA = 2.0f;
        float pOmLen = std::sqrt(hc.pseudoOmega.dot(hc.pseudoOmega));
        if (pOmLen > MAX_PSEUDO_OMEGA)
            hc.pseudoOmega = hc.pseudoOmega * (MAX_PSEUDO_OMEGA / pOmLen);
        if (pOmLen > 1e-7f) {
            float angle = pOmLen * dt;
            math::Quat dq = math::Quat::fromAxisAngle(hc.pseudoOmega * (1.0f / pOmLen), angle);
            tf.rotation = dq * tf.rotation;
            tf.rotation = normalizeQuat(tf.rotation);
        }
        hc.pseudoVel   = {};
        hc.pseudoOmega = {};

        // Sleep check
        if (hc.sleepingEnabled) {
            float linSq = hc.velocity.dot(hc.velocity);
            float angSq = hc.omega.dot(hc.omega);
            float linThresh = physics::SLEEP_LINEAR_THRESHOLD  * physics::SLEEP_LINEAR_THRESHOLD;
            float angThresh = physics::SLEEP_ANGULAR_THRESHOLD * physics::SLEEP_ANGULAR_THRESHOLD;
            if (linSq < linThresh && angSq < angThresh) {
                if (++hc.sleepFrames >= physics::SLEEP_FRAMES_REQUIRED) {
                    hc.velocity = {};
                    hc.omega    = {};
                    toSleep.push_back(e);
                }
            } else {
                hc.sleepFrames = 0;
            }
        }
    }
    // Add Sleeping tags outside view iteration (archetype mutation not safe inside).
    for (ecs::Entity e : toSleep)
        if (!registry_.has<ecs::Sleeping>(e))
            registry_.add<ecs::Sleeping>(e);

    // 5. Springs.
    {
        YOPE_PROF_SCOPE("spring_update", "physics");
        for (auto& s : springs_)
            s->update(dt, registry_);
    }

    { YOPE_PROF_SCOPE("publish_snapshot", "physics"); publishSnapshot(); }
}

// ---- Snapshot double-buffer ----

void World::publishSnapshot() {
    snapshotBack_.clear();
    for (auto [e, hc] : registry_.view<ecs::Hull>()) {
        auto* tf = registry_.get<Transform>(e);
        if (!tf) continue;
        RenderMesh* mesh = nullptr;
        if (auto* mr = registry_.get<ecs::MeshRenderer>(e)) mesh = mr->mesh;
        // Hull entities are hierarchy roots (v1 invariant) — world == local.
        snapshotBack_.push_back({ tf->position, tf->rotation, tf->scale, mesh, e });
    }
    // Render-only entities (no physics body) — model matrix driven by editor transforms.
    // These may be parented, so compose the full Parent chain to world space here.
    for (auto [e, mr, tf] : registry_.view<ecs::MeshRenderer, Transform>()) {
        if (registry_.has<ecs::Hull>(e)) continue;  // already covered above
        if (!mr.mesh) continue;
        Transform w = hierarchy::worldTransform(registry_, e);
        snapshotBack_.push_back({ w.position, w.rotation, w.scale, mr.mesh, e });
    }

    springSnapshotBack_.clear();
    for (auto& s : springs_) {
        if (!registry_.valid(s->visualMeshEntity_)) continue;
        auto* mr = registry_.get<ecs::MeshRenderer>(s->visualMeshEntity_);
        if (!mr || !mr->mesh) continue;
        auto [pos, rot, scale] = s->computeSpringTransform(registry_);
        springSnapshotBack_.push_back({pos, rot, scale, mr->mesh, s->visualMeshEntity_});
    }

    if (debugPhysics)
        syncDebugMeshes();

    {
        std::lock_guard lk(snapshotMtx_);
        std::swap(snapshotFront_, snapshotBack_);
        std::swap(springSnapshotFront_, springSnapshotBack_);
    }
    newSnapshotReady_.store(true, std::memory_order_release);
}

void World::syncRenderMeshesFromFront() {
    for (auto& s : snapshotFront_) {
        if (s.mesh) {
            s.mesh->modelMatrix    = Transform{s.pos, s.rot, s.scale}.getModelMatrix();
            s.mesh->transformReady = true;
        }
    }
    for (auto& ss : springSnapshotFront_) {
        ss.mesh->modelMatrix    = Transform{ss.pos, ss.rot, ss.scale}.getModelMatrix();
        ss.mesh->transformReady = true;
    }
}

// ---- Debug overlay ----

void World::rebuildDebugMeshes() {
    destroyDebugMeshes();
    // Sphere, AABB/OBB, cylinder: shared unit meshes scaled per-entity in syncDebugMeshes.
    // Capsule: baked per-entity (can't non-uniformly scale hemispherical caps correctly).
    auto [boxV, boxI] = DebugShapes::makeBox();
    auto [sphV, sphI] = DebugShapes::makeSphere();
    auto [cylV, cylI] = DebugShapes::makeCylinder(1.0f, 1.0f);
    debugEntities_.clear();

    for (auto [e, hc] : registry_.view<ecs::Hull>()) {
        debugEntities_.push_back(e);
        if (!hc.tangible) {
            debugMeshes_.push_back(nullptr);
            continue;
        }
        if (auto* cc = registry_.get<ecs::CompoundCollider>(e)) {
            // Static compound collider: no single extent to scale a unit mesh by —
            // bake one merged debug mesh per sub-shape (in the body's local frame)
            // instead. syncDebugMeshes leaves scale at identity for this entity (no
            // Sphere/AABB/OBB/Cylinder form matches), so the baked local-space
            // vertices only need the body's Transform on top.
            std::vector<Vertex>   verts;
            std::vector<uint32_t> idx;
            if (cc->compiled) {
                for (const auto& sub : cc->compiled->subShapes) {
                    // TODO(capsule/cylinder inference): once ColliderBaker classifies
                    // those shapes too, branch here to DebugShapes::makeCapsule/makeCylinder
                    // sized from sub.extent (radius = extent.x, halfHeight = extent.y),
                    // oriented by sub.localRot (local +Y axis) — same pattern as Sphere below.
                    bool isSphere = (sub.type == physics::SubShapeType::Sphere);
                    auto [bv, bi] = isSphere ? DebugShapes::makeSphere() : DebugShapes::makeBox();
                    uint32_t base = static_cast<uint32_t>(verts.size());
                    for (auto v : bv) {
                        math::Vec3 scale = isSphere ? math::Vec3{sub.extent.x, sub.extent.x, sub.extent.x}
                                                    : sub.extent;
                        math::Vec3 local{ v.position[0] * scale.x,
                                         v.position[1] * scale.y,
                                         v.position[2] * scale.z };
                        math::Vec3 p = sub.localPos + sub.localRot * local;
                        v.position[0] = p.x; v.position[1] = p.y; v.position[2] = p.z;
                        math::Vec3 n = sub.localRot * math::Vec3{v.normal[0], v.normal[1], v.normal[2]};
                        v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
                        verts.push_back(v);
                    }
                    for (auto bIdx : bi) idx.push_back(base + bIdx);
                }
            }
            if (verts.empty()) { verts = boxV; idx = boxI; }   // not yet resolved — placeholder cube
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, verts, idx));
        } else if (registry_.has<ecs::SphereForm>(e)) {
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, sphV, sphI));
        } else if (auto* cf = registry_.get<ecs::CapsuleForm>(e)) {
            // Baked at actual dims; syncDebugMeshes applies identity scale so no distortion.
            auto [cv, ci] = DebugShapes::makeCapsule(cf->radius, cf->halfHeight);
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, cv, ci));
        } else if (registry_.has<ecs::CylinderForm>(e)) {
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, cylV, cylI));
        } else {
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, boxV, boxI));
        }
    }
    syncDebugMeshes();
}

void World::syncDebugMeshes() {
    // Debug overlays exist only for entities with a Hull, which are hierarchy roots
    // by the v1 invariant — so reading Transform directly here IS the world transform.
    for (size_t i = 0; i < debugEntities_.size() && i < debugMeshes_.size(); ++i) {
        if (!debugMeshes_[i]) continue;
        ecs::Entity e = debugEntities_[i];
        auto* tf = registry_.get<Transform>(e);
        if (!tf) continue;

        // Scale the debug mesh to match the render mesh:
        //   Capsule  → baked debug mesh, identity scale (same as baked render mesh).
        //   Cylinder → unit debug mesh scaled by {r,h,r} (same as unit render mesh).
        //   Others   → unit debug mesh scaled by their respective extents.
        math::Vec3 ext{1, 1, 1};
        if (auto* sf = registry_.get<ecs::SphereForm>(e))
            ext = {sf->radius, sf->radius, sf->radius};
        else if (auto* af = registry_.get<ecs::AABBForm>(e))
            ext = af->extent;
        else if (auto* of = registry_.get<ecs::OBBForm>(e))
            ext = of->extent;
        // CapsuleForm: debug mesh already baked at correct dims → keep ext={1,1,1}.
        else if (auto* cf = registry_.get<ecs::CylinderForm>(e))
            ext = {cf->radius, cf->halfHeight, cf->radius};

        math::Mat4 T;
        T.m[12] = tf->position.x;
        T.m[13] = tf->position.y;
        T.m[14] = tf->position.z;
        math::Mat4 R;
        R.setRotationScale(quatToMat3(tf->rotation));
        math::Mat4 S = math::Mat4::scale(ext);
        debugMeshes_[i]->modelMatrix = T * R * S;

        // Default overlay green, overridden by a per-entity verdict color if set.
        math::Vec3 col{0.0f, 1.0f, 0.2f};
        auto it = debugColorOverrides_.find(e.id);
        if (it != debugColorOverrides_.end()) col = it->second;
        debugMeshes_[i]->color[0] = col.x;
        debugMeshes_[i]->color[1] = col.y;
        debugMeshes_[i]->color[2] = col.z;
    }
}

void World::setDebugColor(ecs::Entity e, math::Vec3 color) {
    debugColorOverrides_[e.id] = color;
}

void World::clearDebugColors() {
    debugColorOverrides_.clear();
}

void World::destroyDebugMeshes() {
    if (gpu_) {
        gpu_->syncDevice();
        for (auto& m : debugMeshes_)
            if (m) m->destroy(gpu_->device());
    }
    debugMeshes_.clear();
    debugEntities_.clear();
}

void World::finalizeEntity(ecs::Entity e, const char* name) {
    ecs::Name n{};
    std::strncpy(n.value, name, sizeof(n.value) - 1);
    registry_.add<ecs::Name>(e, n);
#ifdef YOPE_EDITOR
    registry_.add<ecs::EditorSelectable>(e);
    registry_.add<ecs::EditorPickable>(e);
#endif
}

#ifdef YOPE_EDITOR
void World::snapshotForPlay() {
    prePlayMeshPoolSize_       = meshPool_.size();
    prePlaySpringCount_        = springs_.size();
    playSnapshot_.registry     = registry_.takeSnapshot();
    playSnapshot_.gravity      = gravity;
    playSnapshot_.layers       = layers;
    setPaused(false);
}

void World::restoreFromPlay() {
    setPaused(true);
    // advance() holds structureMtx_ for its entire duration. Acquiring it here
    // blocks until any in-flight physics step finishes, so we never restore the
    // registry while the physics thread is mid-step (which would make entities
    // created during play invalid and fire the "add: invalid entity" assertion).
    { std::lock_guard lk(structureMtx_); }
    if (gpu_) gpu_->syncDevice();

    // Destroy GPU resources for meshes added during play.
    for (size_t i = prePlayMeshPoolSize_; i < meshPool_.size(); ++i) {
        RenderMesh* rm = meshPool_[i].get();
        meshToEntity_.erase(rm);
        rm->destroy(gpu_->device());
    }
    meshPool_.resize(prePlayMeshPoolSize_);

    // Drop springs added during play.
    springs_.resize(prePlaySpringCount_);

    // Restore ECS state.
    registry_.restoreSnapshot(playSnapshot_.registry);
    gravity = playSnapshot_.gravity;
    layers  = playSnapshot_.layers;

    // Clear physics transient state — stale after a state rewind.
    advanceEntities_.clear();
    sapPairs_.clear();
    advanceContacts_.clear();
    contactCache_.clear();

    // Rebuild meshToEntity_ from the restored registry (covers any removeEntity calls during play).
    meshToEntity_.clear();
    for (auto [e, mr] : registry_.view<ecs::MeshRenderer>())
        if (mr.mesh) meshToEntity_[mr.mesh] = e;

    // Force-publish so RenderMesh model matrices reflect the restored transforms.
    publishSnapshot();
}

// Script-snapshot: same as snapshotForPlay/restoreFromPlay but physics stays paused.
// Called from the Scene Script panel in edit mode where the physics thread is already paused.
void World::takeScriptSnapshot() {
    prePlayMeshPoolSize_   = meshPool_.size();
    prePlaySpringCount_    = springs_.size();
    playSnapshot_.registry = registry_.takeSnapshot();
    playSnapshot_.gravity  = gravity;
    playSnapshot_.layers   = layers;
    // Do NOT call setPaused(false) — physics stays paused in edit mode.
}

void World::restoreScriptSnapshot() {
    // Physics is already paused (edit mode); no need to setPaused or wait for a step.
    if (gpu_) gpu_->syncDevice();

    for (size_t i = prePlayMeshPoolSize_; i < meshPool_.size(); ++i) {
        RenderMesh* rm = meshPool_[i].get();
        meshToEntity_.erase(rm);
        rm->destroy(gpu_->device());
    }
    meshPool_.resize(prePlayMeshPoolSize_);
    springs_.resize(prePlaySpringCount_);

    registry_.restoreSnapshot(playSnapshot_.registry);
    gravity = playSnapshot_.gravity;
    layers  = playSnapshot_.layers;

    advanceEntities_.clear();
    sapPairs_.clear();
    advanceContacts_.clear();
    contactCache_.clear();

    meshToEntity_.clear();
    for (auto [e, mr] : registry_.view<ecs::MeshRenderer>())
        if (mr.mesh) meshToEntity_[mr.mesh] = e;

    publishSnapshot();
}
#endif

void World::toggleProxies(bool enabled) {
    for (auto& s : springs_)
        for (ecs::Entity proxyEnt : s->proxies_)
            if (auto* hc = registry_.get<ecs::Hull>(proxyEnt))
                hc->tangible = enabled;
}
