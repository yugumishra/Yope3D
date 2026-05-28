#include "Spring.h"
#include "PhysicsConstants.h"
#include "../ecs/Components.h"
#include "../ecs/Registry.h"
#include "../world/Transform.h"
#include <cmath>

namespace physics {

static constexpr float PI = 3.14159265359f;

Spring::Spring(ecs::Entity first, ecs::Entity second, float k, float rest)
    : first_(first), second_(second), k(k), restLength(rest) {}

void Spring::update(float dt, ecs::Registry& reg) {
    auto* tfA = reg.get<Transform>(first_);
    auto* tfB = reg.get<Transform>(second_);
    auto* hA  = reg.get<ecs::Hull>(first_);
    auto* hB  = reg.get<ecs::Hull>(second_);
    if (!tfA || !tfB || !hA || !hB) return;

    math::Vec3 delta = tfA->position - tfB->position;
    float length = delta.length();
    if (length < 1e-7f) return;

    float displacement = length - restLength;
    delta = delta * ((1.0f / length) * displacement * k);
    delta *= (1 - SPRING_DAMPING_COEFF);

    hA->velocity += delta * (-dt * hA->inverseMass);
    hB->velocity += delta * ( dt * hB->inverseMass);
}

void Spring::syncProxies(ecs::Registry& reg) const {
    if (proxies_.empty()) return;
    auto* tfA = reg.get<Transform>(first_);
    auto* tfB = reg.get<Transform>(second_);
    if (!tfA || !tfB) return;
    math::Vec3 a = tfA->position;
    math::Vec3 b = tfB->position;
    int n = static_cast<int>(proxies_.size());
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i + 1) / static_cast<float>(n + 1);
        if (auto* tf = reg.get<Transform>(proxies_[i]))
            tf->position = a + (b - a) * t;
    }
}

Spring::SpringTransform Spring::computeSpringTransform(const ecs::Registry& reg) const {
    math::Vec3 start{}, end{1, 0, 0};
    if (auto* tf = reg.get<Transform>(first_))  start = tf->position;
    if (auto* tf = reg.get<Transform>(second_)) end   = tf->position;

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
