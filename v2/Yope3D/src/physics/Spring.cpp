#include "Spring.h"
#include "Hull.h"
#include "PhysicsConstants.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include <cmath>

namespace physics {

static constexpr float PI = 3.14159265359f;

Spring::Spring(ecs::Entity first, ecs::Entity second, float k, float rest)
    : first_(first), second_(second), k(k), restLength(rest) {}

void Spring::update(float dt, ecs::Registry& reg) {
    auto* rA = reg.get<ecs::LegacyHullRef>(first_);
    auto* rB = reg.get<ecs::LegacyHullRef>(second_);
    if (!rA || !rB || !rA->ptr || !rB->ptr) return;
    Hull* a = rA->ptr;
    Hull* b = rB->ptr;

    math::Vec3 delta = a->getPosition() - b->getPosition();
    float length = delta.length();
    if (length < 1e-7f) return;

    float displacement = length - restLength;
    delta = delta * ((1.0f / length) * displacement * k);
    delta *= (1 - SPRING_DAMPING_COEFF);

    math::Vec3 aVel = a->getVelocity() + (delta * (-dt / a->getMass()));
    math::Vec3 bVel = b->getVelocity() + (delta * ( dt / b->getMass()));

    a->setVelocity(aVel);
    b->setVelocity(bVel);
}

void Spring::syncProxies(ecs::Registry& reg) const {
    if (proxies_.empty()) return;
    auto* rA = reg.get<ecs::LegacyHullRef>(first_);
    auto* rB = reg.get<ecs::LegacyHullRef>(second_);
    if (!rA || !rB || !rA->ptr || !rB->ptr) return;
    math::Vec3 a = rA->ptr->getPosition();
    math::Vec3 b = rB->ptr->getPosition();
    int n = static_cast<int>(proxies_.size());
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i + 1) / static_cast<float>(n + 1);
        math::Vec3 pos = a + (b - a) * t;
        // Update legacy Hull position so broadphase and integration stay consistent.
        if (auto* ref = reg.get<ecs::LegacyHullRef>(proxies_[i]))
            if (ref->ptr) ref->ptr->setPosition(pos);
        // Update ECS Transform so the Entity-based BroadphaseSAP sees current positions.
        if (auto* tf = reg.get<Transform>(proxies_[i]))
            tf->position = pos;
    }
}

Spring::SpringTransform Spring::computeSpringTransform(const ecs::Registry& reg) const {
    auto* rA = reg.get<ecs::LegacyHullRef>(first_);
    auto* rB = reg.get<ecs::LegacyHullRef>(second_);
    math::Vec3 start{}, end{1, 0, 0};
    if (rA && rA->ptr) start = rA->ptr->getPosition();
    if (rB && rB->ptr) end   = rB->ptr->getPosition();

    math::Vec3 fwd = end - start;
    float length = fwd.length();
    math::Vec3 dir = length > 1e-6f ? fwd * (1.f / length) : math::Vec3{1, 0, 0};

    math::Vec3 localX = {1, 0, 0};
    float cosA = std::clamp(localX.dot(dir), -1.0f, 1.0f);
    math::Quat rot;
    if (cosA > 0.9999f) {
        rot = {0, 0, 0, 1};
    } else if (cosA < -0.9999f) {
        rot = {0, 0, 1, 0};
    } else {
        math::Vec3 axis = localX.cross(dir).normalize();
        rot = math::Quat::fromAxisAngle(axis, std::acos(cosA));
    }
    return {start, rot, {length, 1.0f, 1.0f}};
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>>
Spring::generateCoilMesh(int coils, float coilRadius, float tubeRadius, int slices, int stacksPerCoil) {
    int totalStacks = stacksPerCoil * coils;
    float omega = coils * 2.0f * PI;

    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
    verts.reserve((totalStacks + 1) * (slices + 1));

    for (int i = 0; i <= totalStacks; ++i) {
        float t     = (float)i / totalStacks;
        float theta = t * omega;
        float ct = std::cos(theta), st = std::sin(theta);

        math::Vec3 center = { t, coilRadius * st, coilRadius * ct };

        math::Vec3 T = math::Vec3{ 1.0f, coilRadius * omega * ct, -coilRadius * omega * st }.normalize();
        math::Vec3 N = math::Vec3{ 0.0f, -st, -ct };
        math::Vec3 B = T.cross(N).normalize();
        N = B.cross(T).normalize();

        for (int j = 0; j <= slices; ++j) {
            float phi = (float)j / slices * 2.0f * PI;
            float cp = std::cos(phi), sp = std::sin(phi);

            math::Vec3 norm = N * cp + B * sp;
            math::Vec3 pos  = center + norm * tubeRadius;

            Vertex v;
            v.position[0] = pos.x;  v.position[1] = pos.y;  v.position[2] = pos.z;
            v.normal[0]   = norm.x; v.normal[1]   = norm.y; v.normal[2]   = norm.z;
            v.uv[0]       = (float)j / slices;
            v.uv[1]       = t * (float)coils;
            verts.push_back(v);
        }
    }

    for (int i = 0; i < totalStacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t a = (uint32_t)(i * (slices + 1) + j);
            uint32_t b = a + (uint32_t)(slices + 1);
            indices.push_back(a);     indices.push_back(b);     indices.push_back(a + 1);
            indices.push_back(a + 1); indices.push_back(b);     indices.push_back(b + 1);
        }
    }

    return { verts, indices };
}

} // namespace physics
