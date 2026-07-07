#include "ColliderBaker.h"
#include "Transform.h"
#include "TransformHierarchy.h"
#include "RenderMesh.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include <cfloat>
#include <cmath>
#include <algorithm>

namespace ColliderBaker {

physics::CompiledCollider bakeFromEntity(ecs::Registry& reg, ecs::Entity root, int leafSize) {
    physics::CompiledCollider result;
    if (!reg.valid(root)) return result;

    std::vector<ecs::Entity> subtree;
    hierarchy::collectSubtree(reg, root, subtree);

    Transform rootWorld = hierarchy::worldTransform(reg, root);

    std::vector<physics::SubShape> shapes;
    for (ecs::Entity e : subtree) {
        auto* mr = reg.get<ecs::MeshRenderer>(e);
        if (!mr || !mr->mesh) continue;
        const std::vector<Vertex>& verts = mr->mesh->cpuVertices;
        if (verts.empty()) continue;

        math::Vec3 mn{ FLT_MAX,  FLT_MAX,  FLT_MAX};
        math::Vec3 mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (const auto& v : verts) {
            mn.x = std::min(mn.x, v.position[0]); mx.x = std::max(mx.x, v.position[0]);
            mn.y = std::min(mn.y, v.position[1]); mx.y = std::max(mx.y, v.position[1]);
            mn.z = std::min(mn.z, v.position[2]); mx.z = std::max(mx.z, v.position[2]);
        }
        math::Vec3 center         = (mn + mx) * 0.5f;
        math::Vec3 halfExtentMesh = (mx - mn) * 0.5f;
        if (halfExtentMesh.x <= 0.0f && halfExtentMesh.y <= 0.0f && halfExtentMesh.z <= 0.0f)
            continue;   // degenerate (point/line) geometry — nothing to collide with

        // Pose of this mesh entity relative to root's frame (root scale assumed 1 —
        // see header note). hierarchy::toLocal handles the full TRS chain, including
        // any parents between `e` and `root` and root's own ancestors (they cancel).
        Transform meshWorld = hierarchy::worldTransform(reg, e);
        Transform local     = hierarchy::toLocal(rootWorld, meshWorld);

        physics::SubShape s;

        // Reuse hierarchy::compose to place the mesh-local AABB center (a point,
        // identity rotation/scale) into root-local space via `local`'s full TRS.
        Transform centerOffset{ center, math::Quat{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f} };
        s.localPos = hierarchy::compose(local, centerOffset).position;

        // Shape inference — see physics::classifyAsSphere (CompoundShape.h) for
        // the discriminator and the extension point for capsule/cylinder.
        std::vector<math::Vec3> positions;
        positions.reserve(verts.size());
        for (const auto& v : verts) positions.push_back({v.position[0], v.position[1], v.position[2]});

        if (physics::classifyAsSphere(positions, mr->mesh->cpuIndices, halfExtentMesh)) {
            s.type     = physics::SubShapeType::Sphere;
            s.localRot = math::Mat3{};   // orientation irrelevant for a sphere
            math::Vec3 scaledExt = local.scale.hadamard(halfExtentMesh);
            float radius = (std::fabs(scaledExt.x) + std::fabs(scaledExt.y) + std::fabs(scaledExt.z)) / 3.0f;
            s.extent  = { radius, 0.0f, 0.0f };   // SubShape packing: Sphere extent.x = radius
            s.aabbMin = s.localPos - math::Vec3{radius, radius, radius};
            s.aabbMax = s.localPos + math::Vec3{radius, radius, radius};
        } else {
            s.type     = physics::SubShapeType::OBB;
            s.localRot = math::Mat3::rotation(local.rotation);
            math::Vec3 ext = local.scale.hadamard(halfExtentMesh);
            s.extent = { std::fabs(ext.x), std::fabs(ext.y), std::fabs(ext.z) };

            // Rotation-fattened local AABB — same formula as the broadphase/narrowphase
            // OBB-world-AABB conversions (BroadphaseSAP.cpp, ColliderDiscrete.cpp).
            const math::Mat3& R = s.localRot;
            math::Vec3 fat{
                std::fabs(R.m[0]) * s.extent.x + std::fabs(R.m[3]) * s.extent.y + std::fabs(R.m[6]) * s.extent.z,
                std::fabs(R.m[1]) * s.extent.x + std::fabs(R.m[4]) * s.extent.y + std::fabs(R.m[7]) * s.extent.z,
                std::fabs(R.m[2]) * s.extent.x + std::fabs(R.m[5]) * s.extent.y + std::fabs(R.m[8]) * s.extent.z,
            };
            s.aabbMin = s.localPos - fat;
            s.aabbMax = s.localPos + fat;
        }

        shapes.push_back(s);
    }

    if (shapes.empty()) return result;
    physics::buildCompoundBvh(shapes, result, leafSize);
    return result;
}

bool bakeToFile(ecs::Registry& reg, ecs::Entity root, const std::string& absPath, int leafSize) {
    physics::CompiledCollider col = bakeFromEntity(reg, root, leafSize);
    if (col.subShapes.empty()) return false;
    return physics::writeBcbvh(absPath, col);
}

} // namespace ColliderBaker
