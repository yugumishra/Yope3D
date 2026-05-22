#include "Spring.h"
#include "PhysicsConstants.h"
#include <cmath>

namespace physics {

static constexpr float PI = 3.14159265359f;

Spring::Spring(Hull* a, Hull* b, float k, float rest)
    : first(a), second(b), k(k), restLength(rest) {}

void Spring::update(float dt) {
    math::Vec3 delta = first->getPosition() - second->getPosition();
    float length = delta.length();
    if (length < 1e-7f) return;

    float displacement = length - restLength;
    delta = delta * ((1.0f / length) * displacement * k);
    delta *= (1 - SPRING_DAMPING_COEFF);

    math::Vec3  firstVel = first->getVelocity() + (delta * (-dt / first->getMass()));
    math::Vec3 secondVel =second->getVelocity() + (delta * ( dt /second->getMass()));

    first->setVelocity(firstVel);
    second->setVelocity(secondVel);
}

void Spring::syncProxies() const {
    if (proxies.empty()) return;
    math::Vec3 a = first->getPosition();
    math::Vec3 b = second->getPosition();
    int n = (int)proxies.size();
    for (int i = 0; i < n; ++i) {
        float t = (float)(i + 1) / (float)(n + 1); // evenly spaced, never at endpoints
        proxies[i]->setPosition(a + (b - a) * t);
    }
}

math::Mat4 Spring::computeModelMatrix() const {
    math::Vec3 start = first->getPosition();
    // Local X axis = vector from first to second (magnitude = spring length).
    math::Vec3 fwd   = second->getPosition() - start;

    math::Vec3 ref = {0.0f, 1.0f, 0.0f};
    if (std::abs(fwd.normalize().dot(ref)) > 0.99f) ref = {1.0f, 0.0f, 0.0f};

    math::Vec3 right = fwd.cross(ref).normalize();
    math::Vec3 up    = fwd.cross(right).normalize();

    math::Mat4 mat;
    // Column 0: local X (spring direction, scaled by length)
    mat.m[0] = fwd.x;   mat.m[1] = fwd.y;   mat.m[2] = fwd.z;   mat.m[3]  = 0.0f;
    // Column 1: local Y
    mat.m[4] = right.x; mat.m[5] = right.y; mat.m[6] = right.z; mat.m[7]  = 0.0f;
    // Column 2: local Z
    mat.m[8] = up.x;    mat.m[9] = up.y;    mat.m[10] = up.z;   mat.m[11] = 0.0f;
    // Column 3: translation = first body position
    mat.m[12] = start.x; mat.m[13] = start.y; mat.m[14] = start.z; mat.m[15] = 1.0f;
    return mat;
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

        // Helix center in local spring space (X in [0,1])
        math::Vec3 center = { t, coilRadius * st, coilRadius * ct };

        // Helix tangent (unnormalized): d(center)/dtheta scaled
        math::Vec3 T = math::Vec3{ 1.0f, coilRadius * omega * ct, -coilRadius * omega * st }.normalize();
        // Inward normal toward helix axis
        math::Vec3 N = math::Vec3{ 0.0f, -st, -ct };
        // Binormal, then re-orthogonalise N for a clean frame
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
