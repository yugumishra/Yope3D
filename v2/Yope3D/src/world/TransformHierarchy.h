#pragma once
#include "Transform.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../math/Mat3.h"
#include <vector>

// Transform hierarchy composition (TRS-triple, no matrices / no shear — see plan).
// `Transform` on an entity is LOCAL to its `ecs::Parent`'s frame; entities with no
// valid Parent are world-space (identity composition), so all root content and every
// physics body (roots by the v1 invariant) is unaffected.
namespace hierarchy {

// Component-wise divide (local scale recovery). Guards against zero parent scale.
inline math::Vec3 divideSafe(const math::Vec3& a, const math::Vec3& b) {
    return {
        b.x != 0.0f ? a.x / b.x : 0.0f,
        b.y != 0.0f ? a.y / b.y : 0.0f,
        b.z != 0.0f ? a.z / b.z : 0.0f,
    };
}

// Compose a child's world Transform onto a parent's world Transform.
inline Transform compose(const Transform& parentWorld, const Transform& local) {
    Transform w;
    w.scale    = parentWorld.scale.hadamard(local.scale);
    w.rotation = parentWorld.rotation * local.rotation;
    // world pos = parentPos + R(parentRot) * (parentScale ⊙ localPos)
    math::Vec3 scaled = parentWorld.scale.hadamard(local.position);
    w.position = parentWorld.position + math::Mat3::rotation(parentWorld.rotation) * scaled;
    return w;
}

// Invert `compose`: given a parent's world Transform and a desired world Transform,
// recover the local Transform such that compose(parentWorld, local) == world.
inline Transform toLocal(const Transform& parentWorld, const Transform& world) {
    Transform l;
    l.scale    = divideSafe(world.scale, parentWorld.scale);
    l.rotation = (~parentWorld.rotation) * world.rotation;
    math::Vec3 rel = world.position - parentWorld.position;
    // Undo parent rotation (transpose == conjugate rotation), then parent scale.
    math::Vec3 unrot = math::Mat3::rotation(~parentWorld.rotation) * rel;
    l.position = divideSafe(unrot, parentWorld.scale);
    return l;
}

// Depth-capped Parent-chain walker (cycle-safe). `worldTransform` seeds the cap.
inline Transform worldTransformCapped(const ecs::Registry& reg, ecs::Entity e, int depth) {
    const Transform* tf = reg.get<Transform>(e);
    Transform local = tf ? *tf : Transform{};
    if (depth <= 0) return local;
    const ecs::Parent* p = reg.has<ecs::Parent>(e) ? reg.get<ecs::Parent>(e) : nullptr;
    if (!p || p->parent == ecs::NullEntity || !reg.valid(p->parent))
        return local;
    return compose(worldTransformCapped(reg, p->parent, depth - 1), local);
}

// Resolve an entity's world Transform by walking its Parent chain. Returns the raw
// local Transform if the entity has no valid parent.
inline Transform worldTransform(const ecs::Registry& reg, ecs::Entity e) {
    return worldTransformCapped(reg, e, 64);
}

// True if `e` is `ancestor` or lies anywhere below it — the cycle guard for reparenting.
inline bool isDescendantOf(const ecs::Registry& reg, ecs::Entity e, ecs::Entity ancestor) {
    int depth = 64;
    ecs::Entity cur = e;
    while (reg.valid(cur) && depth-- > 0) {
        if (cur == ancestor) return true;
        const ecs::Parent* p = reg.has<ecs::Parent>(cur) ? reg.get<ecs::Parent>(cur) : nullptr;
        if (!p || p->parent == ecs::NullEntity) break;
        cur = p->parent;
    }
    return false;
}

// Collect `root` plus all descendants in parent-before-child (topological) order.
// No reverse index — scans view<Parent>() per BFS level; fine at editor scale.
inline void collectSubtree(ecs::Registry& reg, ecs::Entity root, std::vector<ecs::Entity>& out) {
    size_t frontier = out.size();
    out.push_back(root);
    while (frontier < out.size()) {
        ecs::Entity cur = out[frontier++];
        for (auto [child, p] : reg.view<ecs::Parent>())
            if (p.parent == cur) out.push_back(child);
    }
}

} // namespace hierarchy
