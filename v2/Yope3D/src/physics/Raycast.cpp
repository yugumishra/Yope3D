#include "Raycast.h"
#include "initializer_list"
#include <cmath>
#include <array>

namespace physics::Raycast {

float raycastSphere(math::Vec3 ray, math::Vec3 start,
                    math::Vec3 center, float radius)
{
    math::Vec3 diff     = start - center;
    float raySq         = ray.dot(ray);
    float rayDotDiff    = ray.dot(diff);
    float det           = rayDotDiff * rayDotDiff
                        - raySq * (diff.dot(diff) - radius * radius);
    if (det < 0.0f) return -1.0f;

    float sqrtDet = std::sqrt(det);
    float t1 = (-rayDotDiff - sqrtDet) / raySq;
    float t2 = (-rayDotDiff + sqrtDet) / raySq;
    return (t1 < t2) ? t1 : t2;
}

float raycastAABB(math::Vec3 ray, math::Vec3 start,
                  math::Vec3 pos, math::Vec3 extent)
{
    constexpr float MISS = std::numeric_limits<float>::min();
    float result = MISS;

    auto tryHit = [&](float k, float ha, float hb,
                       float aMin, float aMax, float bMin, float bMax) {
        if (k < 0.0f) return;
        if (ha >= aMin && ha <= aMax && hb >= bMin && hb <= bMax) {
            if (result == MISS || k < result) result = k;
        }
    };

    if (std::abs(ray.x) > 1e-7f) {
        for (float s : {-1.0f, 1.0f}) {
            float k  = (pos.x + s * extent.x - start.x) / ray.x;
            float hy = start.y + k * ray.y;
            float hz = start.z + k * ray.z;
            tryHit(k, hy, hz, pos.y - extent.y, pos.y + extent.y,
                               pos.z - extent.z, pos.z + extent.z);
        }
    }
    if (std::abs(ray.y) > 1e-7f) {
        for (float s : {-1.0f, 1.0f}) {
            float k  = (pos.y + s * extent.y - start.y) / ray.y;
            float hx = start.x + k * ray.x;
            float hz = start.z + k * ray.z;
            tryHit(k, hx, hz, pos.x - extent.x, pos.x + extent.x,
                               pos.z - extent.z, pos.z + extent.z);
        }
    }
    if (std::abs(ray.z) > 1e-7f) {
        for (float s : {-1.0f, 1.0f}) {
            float k  = (pos.z + s * extent.z - start.z) / ray.z;
            float hx = start.x + k * ray.x;
            float hy = start.y + k * ray.y;
            tryHit(k, hx, hy, pos.x - extent.x, pos.x + extent.x,
                               pos.y - extent.y, pos.y + extent.y);
        }
    }

    return result;
}

float raycastOBB(math::Vec3 ray, math::Vec3 start,
                 math::Vec3 pos, math::Vec3 extent,
                 const std::array<math::Vec3, 3>& axes)
{
    constexpr float MISS = std::numeric_limits<float>::min();
    math::Vec3 d = pos - start;

    float tEnter = -std::numeric_limits<float>::max();
    float tExit  =  std::numeric_limits<float>::max();

    const float hs[3] = { extent.x, extent.y, extent.z };
    for (int i = 0; i < 3; ++i) {
        float e = axes[i].dot(d);
        float f = axes[i].dot(ray);
        if (std::abs(f) > 1e-7f) {
            float t1 = (e - hs[i]) / f;
            float t2 = (e + hs[i]) / f;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tEnter) tEnter = t1;
            if (t2 < tExit)  tExit  = t2;
            if (tEnter > tExit) return MISS;
        } else if (std::abs(e) > hs[i]) {
            return MISS;
        }
    }

    if (tExit < 0.0f) return MISS;
    return tEnter >= 0.0f ? tEnter : tExit;
}

float raycastCapsule(math::Vec3 ray, math::Vec3 start,
                     math::Vec3 center, float radius,
                     float halfHeight, math::Vec3 up)
{
    // Standard analytic ray-vs-capsule intersection: quadratic against the
    // infinite cylinder through the segment [pa,pb], clipped to the segment's
    // extent; falls back to the two end-cap spheres both for genuine cap hits
    // and for the degenerate case where the ray runs parallel to the axis
    // (the cylinder quadratic's `a` term vanishes).
    math::Vec3 pa = center - up * halfHeight;
    math::Vec3 pb = center + up * halfHeight;
    math::Vec3 ba = pb - pa;
    math::Vec3 oa = start - pa;

    float baba = ba.dot(ba);
    float bard = ba.dot(ray);
    float baoa = ba.dot(oa);
    float rdoa = ray.dot(oa);
    float oaoa = oa.dot(oa);

    float a = baba - bard * bard;
    if (std::abs(a) > 1e-8f) {
        float b = baba * rdoa - baoa * bard;
        float c = baba * oaoa - baoa * baoa - radius * radius * baba;
        float h = b * b - a * c;
        if (h >= 0.0f) {
            float t = (-b - std::sqrt(h)) / a;
            float y = baoa + t * bard;
            if (t >= 0.0f && y > 0.0f && y < baba) return t;   // cylindrical body
        }
    }

    float bestT = -1.0f;
    for (const math::Vec3& capCenter : {pa, pb}) {
        float t = raycastSphere(ray, start, capCenter, radius);
        if (t < 0.0f) continue;
        if (bestT < 0.0f || t < bestT) bestT = t;
    }
    return bestT;
}

} // namespace physics::Raycast
