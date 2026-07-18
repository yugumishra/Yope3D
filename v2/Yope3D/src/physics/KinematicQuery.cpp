#include "KinematicQuery.h"
#include "Raycast.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include "../math/Mat3.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace physics::KinematicQuery {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isTangible(ecs::Entity e, ecs::Registry& reg) {
    if (reg.has<ecs::Fixed>(e)) return true;
    if (auto* h = reg.get<ecs::Hull>(e)) return h->tangible;
    return false;
}

// Closest point on an axis-aligned capsule segment to a world point Q.
// Capsule is Y-axis-aligned: cylinder spans [pos.y-hh, pos.y+hh], xz = pos.xz.
static math::Vec3 closestOnSegment(math::Vec3 pos, float hh, math::Vec3 Q) {
    float py = std::max(pos.y - hh, std::min(pos.y + hh, Q.y));
    return {pos.x, py, pos.z};
}

// Face normal of an AABB hit from a given world-space hit point.
static math::Vec3 aabbFaceNormal(math::Vec3 hitPt, math::Vec3 center, math::Vec3 extent) {
    float fx = (hitPt.x - center.x) / extent.x;
    float fy = (hitPt.y - center.y) / extent.y;
    float fz = (hitPt.z - center.z) / extent.z;
    float ax = std::abs(fx), ay = std::abs(fy), az = std::abs(fz);
    if (ax >= ay && ax >= az) return {fx > 0.f ? 1.f : -1.f, 0.f, 0.f};
    if (ay >= ax && ay >= az) return {0.f, fy > 0.f ? 1.f : -1.f, 0.f};
    return {0.f, 0.f, fz > 0.f ? 1.f : -1.f};
}

// Face normal of an OBB hit: transform hit point to local space, get AABB normal,
// then rotate back to world space.
static math::Vec3 obbFaceNormal(math::Vec3 hitPt, math::Vec3 center,
                                math::Vec3 extent, const math::Mat3& rot) {
    math::Vec3 local = rot.transpose() * (hitPt - center);
    math::Vec3 ln    = aabbFaceNormal(local, math::Vec3{0,0,0}, extent);
    return rot * ln;
}

// ---------------------------------------------------------------------------
// capsuleOverlap
// ---------------------------------------------------------------------------

std::vector<OverlapResult> capsuleOverlap(math::Vec3 pos, float r, float hh,
                                           ecs::Registry& reg, ecs::Entity exclude)
{
    std::vector<OverlapResult> results;

    // Capsule axis endpoints
    const float segLo = pos.y - hh;
    const float segHi = pos.y + hh;

    // --- AABB entities ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::AABBForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;

        math::Vec3 c = tf.position;
        math::Vec3 ext = form.extent;

        float qx = std::max(c.x - ext.x, std::min(c.x + ext.x, pos.x));
        float qz = std::max(c.z - ext.z, std::min(c.z + ext.z, pos.z));

        float boxLo = c.y - ext.y, boxHi = c.y + ext.y;
        float qy, py;
        if (segHi < boxLo) { py = segHi; qy = boxLo; }
        else if (segLo > boxHi) { py = segLo; qy = boxHi; }
        else {
            qy = std::max(boxLo, std::min(boxHi, pos.y));
            py = std::max(segLo, std::min(segHi, qy));
        }

        math::Vec3 Q{qx, qy, qz};
        math::Vec3 P{pos.x, py, pos.z};
        math::Vec3 diff = P - Q;
        float dist = diff.length();
        if (dist < r) {
            math::Vec3 normal = (dist > 1e-5f) ? diff * (1.f / dist) : math::Vec3{0.f, 1.f, 0.f};
            results.push_back({normal, r - dist});
        }
    }

    // --- OBB entities ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::OBBForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;

        math::Mat3 rot  = math::Mat3::rotation(tf.rotation);
        math::Mat3 rotT = rot.transpose();
        math::Vec3 ext  = form.extent;

        math::Vec3 localBot = rotT * (math::Vec3{pos.x, pos.y - hh, pos.z} - tf.position);
        math::Vec3 localTop = rotT * (math::Vec3{pos.x, pos.y + hh, pos.z} - tf.position);
        math::Vec3 segDir   = localTop - localBot;

        const float A[3]  = { localBot.x, localBot.y, localBot.z };
        const float D[3]  = { segDir.x,   segDir.y,   segDir.z   };
        const float ex[3] = { ext.x,      ext.y,      ext.z      };

        // Collect all critical t values: endpoints + 6 slab-boundary crossings.
        float crits[8];
        int nC = 0;
        crits[nC++] = 0.f;
        crits[nC++] = 1.f;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(D[i]) > 1e-8f) {
                crits[nC++] = ( ex[i] - A[i]) / D[i];
                crits[nC++] = (-ex[i] - A[i]) / D[i];
            }
        }
        std::sort(crits, crits + nC);

        float bestDistSq = std::numeric_limits<float>::max();
        math::Vec3 bestP{}, bestQ{};

        auto evalT = [&](float t) {
            t = std::max(0.f, std::min(1.f, t));
            float Px = A[0]+t*D[0], Py = A[1]+t*D[1], Pz = A[2]+t*D[2];
            float Qx = std::max(-ex[0], std::min(ex[0], Px));
            float Qy = std::max(-ex[1], std::min(ex[1], Py));
            float Qz = std::max(-ex[2], std::min(ex[2], Pz));
            float dx = Px-Qx, dy = Py-Qy, dz = Pz-Qz;
            float dsq = dx*dx + dy*dy + dz*dz;
            if (dsq < bestDistSq) {
                bestDistSq = dsq;
                bestP = {Px, Py, Pz};
                bestQ = {Qx, Qy, Qz};
            }
        };

        // Test all critical t values.
        for (int i = 0; i < nC; ++i) evalT(crits[i]);

        // Between consecutive slab crossings the clamped-axis set is constant, so d²(t)
        // is a convex quadratic whose unconstrained interior minimum can beat both
        // boundaries — the endpoint/crossing tests above miss it. Test it explicitly.
        for (int i = 0; i + 1 < nC; ++i) {
            float tLo = crits[i], tHi = crits[i+1];
            if (tHi <= 0.f || tLo >= 1.f || tHi <= tLo + 1e-10f) continue;
            float tMid = 0.5f * (tLo + tHi);
            if (tMid < 0.f || tMid > 1.f) continue;

            // For each axis clamped at tMid, accumulate the numerator/denominator
            // of t* = Σ(s_i - A_i)*D_i / Σ D_i² (zero-derivative of the quadratic).
            float num = 0.f, den = 0.f;
            for (int j = 0; j < 3; ++j) {
                float Pj = A[j] + tMid * D[j];
                if (Pj > ex[j]) {
                    num += ( ex[j] - A[j]) * D[j];
                    den += D[j] * D[j];
                } else if (Pj < -ex[j]) {
                    num += (-ex[j] - A[j]) * D[j];
                    den += D[j] * D[j];
                }
            }
            if (den > 1e-12f) evalT(num / den);
        }

        float dist = std::sqrt(bestDistSq);
        if (dist >= r) continue;

        math::Vec3 localNormal;
        float depth;

        if (dist > 1e-5f) {
            // Normal case: push along the vector from closest OBB point to closest segment point.
            math::Vec3 diff{ bestP.x-bestQ.x, bestP.y-bestQ.y, bestP.z-bestQ.z };
            localNormal = diff * (1.f / dist);
            depth = r - dist;
        } else {
            // The capsule axis passes through the OBB interior — use SAT to find the
            // minimum-penetration axis. We test 3 face normals and 3 cross-products of
            // the capsule direction with each face normal (the complete capsule-OBB SAT set).
            math::Vec3 capMid{
                (localBot.x + localTop.x) * 0.5f,
                (localBot.y + localTop.y) * 0.5f,
                (localBot.z + localTop.z) * 0.5f
            };
            math::Vec3 capHalf{ segDir.x*0.5f, segDir.y*0.5f, segDir.z*0.5f };

            float segLen = segDir.length();
            math::Vec3 su = (segLen > 1e-8f) ? segDir * (1.f / segLen)
                                              : math::Vec3{0.f, 1.f, 0.f};

            math::Vec3 xAx{1.f,0.f,0.f}, yAx{0.f,1.f,0.f}, zAx{0.f,0.f,1.f};
            math::Vec3 candidates[6] = {
                xAx, yAx, zAx,
                su.cross(xAx),   // perpendicular to both capsule axis and each face normal
                su.cross(yAx),
                su.cross(zAx)
            };

            float minPen = std::numeric_limits<float>::max();
            localNormal  = {0.f, 1.f, 0.f};

            for (auto& ax : candidates) {
                float axLen = ax.length();
                if (axLen < 1e-8f) continue;
                ax = ax * (1.f / axLen);

                float obbHe    = std::abs(ax.x)*ex[0] + std::abs(ax.y)*ex[1] + std::abs(ax.z)*ex[2];
                float capCen   = capMid.dot(ax);
                float capHe    = std::abs(capHalf.dot(ax)) + r;
                float pen      = obbHe + capHe - std::abs(capCen);
                if (pen <= 0.f) continue; // guard against numerical noise

                if (pen < minPen) {
                    minPen      = pen;
                    localNormal = (capCen >= 0.f) ? ax : math::Vec3{-ax.x,-ax.y,-ax.z};
                }
            }
            depth = minPen;
        }

        results.push_back({ rot * localNormal, depth });
    }

    // --- Sphere entities ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::SphereForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;

        // Closest point on capsule segment to sphere center
        math::Vec3 P = closestOnSegment(pos, hh, tf.position);
        math::Vec3 diff = P - tf.position;
        float dist = diff.length() - form.radius;

        if (dist < r) {
            float totalDist = diff.length();
            math::Vec3 normal = (totalDist > 1e-5f) ? diff * (1.f / totalDist) : math::Vec3{0.f, 1.f, 0.f};
            results.push_back({normal, r - dist});
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// capsuleCast
// ---------------------------------------------------------------------------

CastResult capsuleCast(math::Vec3 pos, float r, float hh,
                       math::Vec3 dir, float maxDist,
                       ecs::Registry& reg, ecs::Entity exclude)
{
    constexpr float RAYCAST_MISS = std::numeric_limits<float>::min();

    // Ray origin: endpoint sphere center in the cast direction
    math::Vec3 origin = (dir.y <= 0.f)
        ? math::Vec3{pos.x, pos.y - hh, pos.z}
        : math::Vec3{pos.x, pos.y + hh, pos.z};

    CastResult best{maxDist, false, {0.f, 1.f, 0.f}};

    // For a planar face with outward normal `n`, a sphere of radius r travelling along
    // `dir` first contacts the face when the center is r/|n·dir| before the ray tip
    // reaches the face — not simply r before it.  Using r is only correct for
    // perpendicular impact; at 50° tilt the error is ~0.22 m per capsule-radius unit,
    // which causes a one-frame clip-through during step-climb snap-down.
    auto considerFace = [&](float rayT, math::Vec3 normal) {
        if (rayT == RAYCAST_MISS || rayT < 0.f) return;
        float nDotDir = std::abs(normal.dot(dir));
        float adj     = (nDotDir > 1e-4f) ? r / nDotDir : r;
        float contact = rayT - adj;
        if (contact < 0.f) contact = 0.f;
        if (contact < best.t) {
            best.t      = contact;
            best.hit    = true;
            best.normal = normal;
        }
    };

    // Sphere obstacles are spherically symmetric; keep the simpler r subtraction.
    auto considerSphere = [&](float rayT, math::Vec3 normal) {
        if (rayT == RAYCAST_MISS || rayT < 0.f) return;
        float contact = rayT - r;
        if (contact < 0.f) contact = 0.f;
        if (contact < best.t) {
            best.t      = contact;
            best.hit    = true;
            best.normal = normal;
        }
    };

    // --- AABB ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::AABBForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;
        float t = Raycast::raycastAABB(dir, origin, tf.position, form.extent);
        if (t == RAYCAST_MISS || t < 0.f || t > maxDist + r) continue;
        math::Vec3 hitPt = {origin.x + dir.x*t, origin.y + dir.y*t, origin.z + dir.z*t};
        considerFace(t, aabbFaceNormal(hitPt, tf.position, form.extent));
    }

    // --- OBB ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::OBBForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;
        math::Mat3 rot  = math::Mat3::rotation(tf.rotation);
        auto axes = std::array<math::Vec3,3>{{
            {rot.m[0], rot.m[1], rot.m[2]},
            {rot.m[3], rot.m[4], rot.m[5]},
            {rot.m[6], rot.m[7], rot.m[8]}
        }};
        float t = Raycast::raycastOBB(dir, origin, tf.position, form.extent, axes);
        if (t == RAYCAST_MISS || t < 0.f || t > maxDist + r) continue;
        math::Vec3 hitPt = {origin.x + dir.x*t, origin.y + dir.y*t, origin.z + dir.z*t};
        considerFace(t, obbFaceNormal(hitPt, tf.position, form.extent, rot));
    }

    // --- Sphere ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::SphereForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;
        float t = Raycast::raycastSphere(dir, origin, tf.position, form.radius);
        if (t < 0.f || t > maxDist + r) continue;
        math::Vec3 hitPt = {origin.x + dir.x*t, origin.y + dir.y*t, origin.z + dir.z*t};
        math::Vec3 normal = (hitPt - tf.position);
        float nl = normal.length();
        considerSphere(t, nl > 1e-5f ? normal * (1.f/nl) : math::Vec3{0,1,0});
    }

    return best;
}

// ---------------------------------------------------------------------------
// raycast
// ---------------------------------------------------------------------------

RayHit raycast(math::Vec3 origin, math::Vec3 dir, float maxDist,
               ecs::Registry& reg, ecs::Entity exclude)
{
    constexpr float MISS = std::numeric_limits<float>::min();

    RayHit best;
    best.t = maxDist;

    float dl = dir.length();
    if (dl < 1e-8f) return best;
    math::Vec3 d = dir * (1.f / dl);

    auto consider = [&](float t, ecs::Entity e, math::Vec3 n) {
        if (t < 0.f || t > best.t) return;
        best.hit    = true;
        best.t      = t;
        best.entity = e;
        best.normal = n;
        best.point  = origin + d * t;
    };

    // --- AABB ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::AABBForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;
        float t = Raycast::raycastAABB(d, origin, tf.position, form.extent);
        if (t == MISS) continue;
        math::Vec3 hitPt = origin + d * t;
        consider(t, e, aabbFaceNormal(hitPt, tf.position, form.extent));
    }

    // --- OBB ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::OBBForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;
        math::Mat3 rot = math::Mat3::rotation(tf.rotation);
        auto axes = std::array<math::Vec3,3>{{
            {rot.m[0], rot.m[1], rot.m[2]},
            {rot.m[3], rot.m[4], rot.m[5]},
            {rot.m[6], rot.m[7], rot.m[8]}
        }};
        float t = Raycast::raycastOBB(d, origin, tf.position, form.extent, axes);
        if (t == MISS) continue;
        math::Vec3 hitPt = origin + d * t;
        consider(t, e, obbFaceNormal(hitPt, tf.position, form.extent, rot));
    }

    // --- Sphere ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::SphereForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;
        float t = Raycast::raycastSphere(d, origin, tf.position, form.radius);
        if (t < 0.f) continue;   // -1 miss, or negative t when origin is inside
        math::Vec3 hitPt = origin + d * t;
        math::Vec3 n = hitPt - tf.position;
        float nl = n.length();
        consider(t, e, nl > 1e-5f ? n * (1.f / nl) : math::Vec3{0,1,0});
    }

    // --- Capsule ---
    for (auto [e, tf, form] : reg.view<Transform, ecs::CapsuleForm>()) {
        if (e == exclude || !isTangible(e, reg)) continue;
        math::Mat3 rot = math::Mat3::rotation(tf.rotation);
        math::Vec3 up{rot.m[3], rot.m[4], rot.m[5]};   // local +Y -> world (CapsuleGeom convention)
        float t = Raycast::raycastCapsule(d, origin, tf.position, form.radius, form.halfHeight, up);
        if (t < 0.f) continue;
        math::Vec3 hitPt = origin + d * t;
        // Approximate normal: outward from the nearest point on the capsule's
        // central segment — exact for the cylindrical body, a fair approximation
        // right at the cap-to-cylinder seam (true sphere normal only on the caps).
        float py = std::max(-form.halfHeight, std::min(form.halfHeight, up.dot(hitPt - tf.position)));
        math::Vec3 axisPt = tf.position + up * py;
        math::Vec3 n = hitPt - axisPt;
        float nl = n.length();
        consider(t, e, nl > 1e-5f ? n * (1.f / nl) : math::Vec3{0,1,0});
    }

    return best;
}

} // namespace physics::KinematicQuery
