#include "Raycast.h"
#include <cmath>

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

} // namespace physics::Raycast
