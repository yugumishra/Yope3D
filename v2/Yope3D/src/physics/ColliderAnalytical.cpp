#include "ColliderAnalytical.h"
#include "PhysicsConstants.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include <cmath>
#include <algorithm>

namespace physics {

namespace ColliderDiscrete {

// ============================================================================
// Sphere — Sphere   (normal convention: from a toward b)
// ============================================================================
bool analyticalSphereSphere(const SphereGeom& a, const SphereGeom& b, ContactManifold& m) {
    math::Vec3 diff   = b.getPosition() - a.getPosition();
    float      distSq = diff.dot(diff);
    float      rSum   = a.getRadius() + b.getRadius();
    if (distSq > rSum * rSum) return false;

    float dist = std::sqrt(distSq);
    if (dist < 1e-7f) [[unlikely]] {
        m.normal      = {1.0f, 0.0f, 0.0f};
        m.penetration = rSum;
    } else [[likely]] {
        float invDist = 1.0f / dist;
        m.normal      = diff * invDist;
        m.penetration = rSum - dist;
    }
    m.numContacts      = 1;
    m.contactPoints[0] = a.getPosition() + m.normal * a.getRadius();
    m.depths[0]        = m.penetration;
    return true;
}

// ============================================================================
// Sphere — AABB   (normal convention: AABB → sphere; callers flip when sphere is 'a')
// ============================================================================

bool analyticalSphereAABB(const SphereGeom& sphere, const AABBGeom& aabb, ContactManifold& m) {
    math::Vec3 sp = sphere.getPosition();
    math::Vec3 ap = aabb.getPosition();
    math::Vec3 ae = aabb.getScales();
    float      r  = sphere.getRadius();

    math::Vec3 lo = ap - ae;
    math::Vec3 hi = ap + ae;

    math::Vec3 closest = {
        std::max(lo.x, std::min(sp.x, hi.x)),
        std::max(lo.y, std::min(sp.y, hi.y)),
        std::max(lo.z, std::min(sp.z, hi.z))
    };

    math::Vec3 diff   = sp - closest;
    float      distSq = diff.dot(diff);
    if (distSq > r * r) return false;

    m.numContacts      = 1;
    m.contactPoints[0] = closest;

    if (distSq > 1e-8f) {
        float dist    = std::sqrt(distSq);
        m.normal      = diff * (1.0f / dist);
        m.penetration = r - dist;
        m.depths[0]   = m.penetration;
    } else {
        float depths[6] = {
            hi.x - sp.x, sp.x - lo.x,
            hi.y - sp.y, sp.y - lo.y,
            hi.z - sp.z, sp.z - lo.z
        };
        const math::Vec3 normals[6] = {
            {1,0,0},{-1,0,0},
            {0,1,0},{0,-1,0},
            {0,0,1},{0,0,-1}
        };
        float minDepth = depths[0];
        for (int i = 1; i < 6; i++)
            if (depths[i] < minDepth) minDepth = depths[i];

        math::Vec3 n = {};
        for (int i = 0; i < 6; i++)
            if (depths[i] <= minDepth + 1e-4f)
                n += normals[i];
        float len = std::sqrt(n.dot(n));
        n = (len > 1e-7f) ? n * (1.0f / len) : math::Vec3{0.0f, 1.0f, 0.0f};

        m.normal           = n;
        m.penetration      = r + minDepth;
        m.depths[0]        = m.penetration;
        m.contactPoints[0] = sp + n * minDepth;
    }
    return true;
}

// ============================================================================
// AABB — AABB   (normal convention: from a toward b)
// ============================================================================

bool analyticalAABBAABB(const AABBGeom& a, const AABBGeom& b, ContactManifold& m) {
    math::Vec3 posA = a.getPosition(), posB = b.getPosition();
    math::Vec3 eA   = a.getScales(),   eB   = b.getScales();

    float ovX = (eA.x + eB.x) - std::abs(posA.x - posB.x);
    float ovY = (eA.y + eB.y) - std::abs(posA.y - posB.y);
    float ovZ = (eA.z + eB.z) - std::abs(posA.z - posB.z);

    if (ovX <= 0.0f || ovY <= 0.0f || ovZ <= 0.0f) return false;

    int axis;
    if (ovX <= ovY && ovX <= ovZ) {
        m.normal      = {posA.x < posB.x ? 1.0f : -1.0f, 0.0f, 0.0f};
        m.penetration = ovX;
        axis = 0;
    } else if (ovY <= ovX && ovY <= ovZ) {
        m.normal      = {0.0f, posA.y < posB.y ? 1.0f : -1.0f, 0.0f};
        m.penetration = ovY;
        axis = 1;
    } else {
        m.normal      = {0.0f, 0.0f, posA.z < posB.z ? 1.0f : -1.0f};
        m.penetration = ovZ;
        axis = 2;
    }

    float loA[3] = {posA.x - eA.x, posA.y - eA.y, posA.z - eA.z};
    float hiA[3] = {posA.x + eA.x, posA.y + eA.y, posA.z + eA.z};
    float loB[3] = {posB.x - eB.x, posB.y - eB.y, posB.z - eB.z};
    float hiB[3] = {posB.x + eB.x, posB.y + eB.y, posB.z + eB.z};
    float lo[3], hi[3];
    for (int i = 0; i < 3; i++) {
        lo[i] = std::max(loA[i], loB[i]);
        hi[i] = std::min(hiA[i], hiB[i]);
    }

    int u = (axis + 1) % 3;
    int v = (axis + 2) % 3;
    float planeAxis = 0.5f * (lo[axis] + hi[axis]);
    bool uFlat = (hi[u] - lo[u]) < 1e-5f;
    bool vFlat = (hi[v] - lo[v]) < 1e-5f;
    float uVals[2] = {lo[u], hi[u]};
    float vVals[2] = {lo[v], hi[v]};
    int uN = uFlat ? 1 : 2;
    int vN = vFlat ? 1 : 2;
    if (uFlat) uVals[0] = 0.5f * (lo[u] + hi[u]);
    if (vFlat) vVals[0] = 0.5f * (lo[v] + hi[v]);

    int n = 0;
    for (int iu = 0; iu < uN; iu++) {
        for (int iv = 0; iv < vN; iv++) {
            math::Vec3 p{};
            float comp[3];
            comp[axis] = planeAxis;
            comp[u]    = uVals[iu];
            comp[v]    = vVals[iv];
            p.x = comp[0]; p.y = comp[1]; p.z = comp[2];
            m.contactPoints[n] = p;
            m.depths[n]        = m.penetration;
            n++;
        }
    }
    m.numContacts = n;
    return true;
}

// ============================================================================
// Sphere — OBB   (normal convention: OBB → sphere)
// ============================================================================

bool analyticalSphereOBB(const SphereGeom& sphere, const OBBGeom& obb, ContactManifold& m) {
    math::Vec3 sp  = sphere.getPosition();
    math::Vec3 op  = obb.getPosition();
    math::Vec3 ext = obb.getScales();
    float      r   = sphere.getRadius();

    math::Mat3 Rt    = obb.getRotTransform().transpose();
    math::Vec3 local = Rt * (sp - op);

    math::Vec3 clamped = {
        std::max(-ext.x, std::min(local.x, ext.x)),
        std::max(-ext.y, std::min(local.y, ext.y)),
        std::max(-ext.z, std::min(local.z, ext.z))
    };

    math::Vec3 worldClosest = op + obb.getRotTransform() * clamped;
    math::Vec3 diff         = sp - worldClosest;
    float      distSq       = diff.dot(diff);
    if (distSq >= r * r) return false;

    m.numContacts = 1;
    if (distSq > 1e-8f) {
        float dist        = std::sqrt(distSq);
        m.normal          = diff * (1.0f / dist);
        m.penetration     = r - dist;
        m.depths[0]       = m.penetration;
        m.contactPoints[0] = worldClosest;
    } else {
        float depths[6] = {
            ext.x + local.x, ext.x - local.x,
            ext.y + local.y, ext.y - local.y,
            ext.z + local.z, ext.z - local.z
        };
        math::Vec3 localNormals[6] = {
            {-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}
        };
        int best = 0;
        for (int i = 1; i < 6; i++)
            if (depths[i] < depths[best]) best = i;

        m.normal          = obb.getRotTransform() * localNormals[best];
        m.penetration     = r + depths[best];
        m.depths[0]       = m.penetration;
        m.contactPoints[0] = sp - m.normal * r;
    }
    return true;
}

// ============================================================================
// AABB — OBB   15-axis SAT   (normal convention: from AABB (a) toward OBB (b))
// ============================================================================

bool analyticalAABBOBB(const AABBGeom& a, const OBBGeom& b, ContactManifold& m) {
    math::Vec3 aPos = a.getPosition(), aExt = a.getScales();
    math::Vec3 bPos = b.getPosition(), bExt = b.getScales();
    auto       bAxes = b.getOBBAxes();
    math::Vec3 diff  = bPos - aPos;

    const math::Vec3 worldAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
    float            extArr[3]    = {aExt.x, aExt.y, aExt.z};

    float      minOverlap = 1e30f;
    math::Vec3 bestAxis;
    int        axisType = 0;
    int        axisIdx  = 0;

    auto testAxis = [&](math::Vec3 n, int type, int idx) -> bool {
        float lenSq = n.dot(n);
        if (lenSq < 1e-8f) return true;
        n = n * (1.0f / std::sqrt(lenSq));

        float projA      = std::abs(n.x)*aExt.x + std::abs(n.y)*aExt.y + std::abs(n.z)*aExt.z;
        float projB      = b.projectOnto(n);
        float centerDist = std::abs(diff.dot(n));
        float overlap    = projA + projB - centerDist;
        if (overlap < 0.0f) return false;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            bestAxis   = n;
            axisType   = type;
            axisIdx    = idx;
        }
        return true;
    };

    for (int i = 0; i < 3; i++) if (!testAxis(worldAxes[i], 0, i)) return false;
    for (int i = 0; i < 3; i++) if (!testAxis(bAxes[i],     1, i)) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (!testAxis(worldAxes[i].cross(bAxes[j]), 2, i*3+j)) return false;

    math::Vec3 normal = (diff.dot(bestAxis) >= 0.0f) ? bestAxis : -bestAxis;
    m.normal      = normal;
    m.penetration = minOverlap;

    struct Cand { math::Vec3 pt; float depth; };

    if (axisType == 0) {
        float faceD   = aPos.dot(normal) + extArr[axisIdx];
        int   sa0     = (axisIdx + 1) % 3;
        int   sa1     = (axisIdx + 2) % 3;
        auto  corners = b.worldSpaceCorners();

        Cand cands[8]; int numCands = 0;
        for (auto& c : corners) {
            float depth = faceD - c.dot(normal);
            if (depth <= 0.0f) continue;
            float d0 = std::abs(c.dot(worldAxes[sa0]) - aPos.dot(worldAxes[sa0]));
            float d1 = std::abs(c.dot(worldAxes[sa1]) - aPos.dot(worldAxes[sa1]));
            if (d0 > extArr[sa0] || d1 > extArr[sa1]) continue;
            cands[numCands++] = {c, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else if (axisType == 1) {
        math::Mat3 Rt       = b.getRotTransform().transpose();
        float      bExtArr[3] = {bExt.x, bExt.y, bExt.z};

        Cand cands[8]; int numCands = 0;
        for (int i = 0; i < 8; i++) {
            float sx = (i & 1) ? -1.0f : 1.0f;
            float sy = (i & 2) ? -1.0f : 1.0f;
            float sz = (i & 4) ? -1.0f : 1.0f;
            math::Vec3 corner = aPos + math::Vec3{sx*aExt.x, sy*aExt.y, sz*aExt.z};
            if (!b.inside(corner)) continue;
            math::Vec3 local    = Rt * (corner - bPos);
            float      localArr[3] = {local.x, local.y, local.z};
            float      depth    = bExtArr[axisIdx] - std::abs(localArr[axisIdx]);
            if (depth <= 0.0f) continue;
            cands[numCands++] = {corner, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else {
        // edge-edge: world axis ai on AABB crossed with OBB axis bi
        int   ai       = axisIdx / 3;
        int   bi       = axisIdx % 3;
        float aEArr[3] = {aExt.x, aExt.y, aExt.z};
        float bEArr[3] = {bExt.x, bExt.y, bExt.z};
        float nArr[3]  = {normal.x, normal.y, normal.z};

        math::Vec3 edgeCenterA = aPos;
        for (int k = 0; k < 3; ++k) {
            if (k == ai) continue;
            edgeCenterA += worldAxes[k] * (nArr[k] >= 0.0f ? aEArr[k] : -aEArr[k]);
        }
        math::Vec3 edgeCenterB = bPos;
        for (int k = 0; k < 3; ++k) {
            if (k == bi) continue;
            edgeCenterB += bAxes[k] * (normal.dot(bAxes[k]) >= 0.0f ? -bEArr[k] : bEArr[k]);
        }

        math::Vec3 segA  = worldAxes[ai] * aEArr[ai];
        math::Vec3 segB  = bAxes[bi]     * bEArr[bi];
        math::Vec3 d     = edgeCenterA - edgeCenterB;
        float      aSqr  = segA.dot(segA);
        float      bSqr  = segB.dot(segB);
        float      ab    = segA.dot(segB);
        float      aDiff = segA.dot(d);
        float      bDiff = segB.dot(d);
        float      det   = aSqr * bSqr - ab * ab;

        float tA = 0.0f, tB = bDiff / bSqr;
        if (std::abs(det) > 1e-8f) {
            tA = (ab * bDiff - bSqr * aDiff) / det;
            tB = (aSqr * bDiff - ab  * aDiff) / det;
        }
        tA           = std::max(-1.0f, std::min(1.0f, tA));
        float recomp = (ab * tA + bDiff) / bSqr;
        tB           = std::max(-1.0f, std::min(1.0f, recomp));
        recomp       = (ab * tB - aDiff) / aSqr;
        tA           = std::max(-1.0f, std::min(1.0f, recomp));

        m.contactPoints[0] = (edgeCenterA + segA * tA + edgeCenterB + segB * tB) * 0.5f;
        m.depths[0]        = m.penetration;
        m.numContacts      = 1;
    }
    return true;
}

// ============================================================================
// OBB — OBB   15-axis SAT   (normal convention: from OBB a toward OBB b)
// ============================================================================

bool analyticalOBBOBB(const OBBGeom& a, const OBBGeom& b, ContactManifold& m) {
    math::Vec3 aPos = a.getPosition(), aExt = a.getScales();
    math::Vec3 bPos = b.getPosition(), bExt = b.getScales();
    auto       aAxes = a.getOBBAxes();
    auto       bAxes = b.getOBBAxes();
    math::Vec3 diff  = bPos - aPos;

    float      minOverlap = 1e30f;
    math::Vec3 bestAxis;
    int        axisType = 0;
    int        axisIdx  = 0;

    auto testAxis = [&](math::Vec3 n, int type, int idx) -> bool {
        float lenSq = n.dot(n);
        if (lenSq < 1e-8f) return true;
        n = n * (1.0f / std::sqrt(lenSq));
        float projA      = a.projectOnto(n);
        float projB      = b.projectOnto(n);
        float centerDist = std::abs(diff.dot(n));
        float overlap    = projA + projB - centerDist;
        if (overlap < 0.0f) return false;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            bestAxis   = n;
            axisType   = type;
            axisIdx    = idx;
        }
        return true;
    };

    for (int i = 0; i < 3; i++) if (!testAxis(aAxes[i], 0, i)) return false;
    for (int i = 0; i < 3; i++) if (!testAxis(bAxes[i], 1, i)) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (!testAxis(aAxes[i].cross(bAxes[j]), 2, i*3+j)) return false;

    math::Vec3 normal = (diff.dot(bestAxis) >= 0.0f) ? bestAxis : -bestAxis;
    m.normal      = normal;
    m.penetration = minOverlap;

    struct Cand { math::Vec3 pt; float depth; };

    if (axisType == 0) {
        math::Mat3 RtA    = a.getRotTransform().transpose();
        float      aEArr[3] = {aExt.x, aExt.y, aExt.z};
        auto       bCorners = b.worldSpaceCorners();

        Cand cands[8]; int numCands = 0;
        for (auto& c : bCorners) {
            if (!a.inside(c)) continue;
            math::Vec3 local    = RtA * (c - aPos);
            float      lArr[3]  = {local.x, local.y, local.z};
            float      depth    = aEArr[axisIdx] - std::abs(lArr[axisIdx]);
            if (depth <= 0.0f) continue;
            cands[numCands++] = {c, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else if (axisType == 1) {
        math::Mat3 RtB    = b.getRotTransform().transpose();
        float      bEArr[3] = {bExt.x, bExt.y, bExt.z};
        auto       aCorners = a.worldSpaceCorners();

        Cand cands[8]; int numCands = 0;
        for (auto& c : aCorners) {
            if (!b.inside(c)) continue;
            math::Vec3 local    = RtB * (c - bPos);
            float      lArr[3]  = {local.x, local.y, local.z};
            float      depth    = bEArr[axisIdx] - std::abs(lArr[axisIdx]);
            if (depth <= 0.0f) continue;
            cands[numCands++] = {c, depth};
        }
        if (numCands == 0) {
            m.contactPoints[0] = (aPos + bPos) * 0.5f;
            m.depths[0]        = m.penetration;
            m.numContacts = 1;
        } else {
            for (int i = 1; i < numCands; i++) {
                Cand key = cands[i]; int j = i - 1;
                while (j >= 0 && cands[j].depth < key.depth) { cands[j+1] = cands[j]; j--; }
                cands[j+1] = key;
            }
            int k = std::min(numCands, 4);
            for (int i = 0; i < k; i++) {
                m.contactPoints[i] = cands[i].pt;
                m.depths[i]        = cands[i].depth;
            }
            m.numContacts = k;
        }
    } else {
        // edge-edge: OBB axis ai on A crossed with OBB axis bi on B
        int   ai       = axisIdx / 3;
        int   bi       = axisIdx % 3;
        float aEArr[3] = {aExt.x, aExt.y, aExt.z};
        float bEArr[3] = {bExt.x, bExt.y, bExt.z};

        math::Vec3 edgeCenterA = aPos;
        for (int k = 0; k < 3; ++k) {
            if (k == ai) continue;
            edgeCenterA += aAxes[k] * (normal.dot(aAxes[k]) >= 0.0f ? aEArr[k] : -aEArr[k]);
        }
        math::Vec3 edgeCenterB = bPos;
        for (int k = 0; k < 3; ++k) {
            if (k == bi) continue;
            edgeCenterB += bAxes[k] * (normal.dot(bAxes[k]) >= 0.0f ? -bEArr[k] : bEArr[k]);
        }

        math::Vec3 segA  = aAxes[ai] * aEArr[ai];
        math::Vec3 segB  = bAxes[bi] * bEArr[bi];
        math::Vec3 d     = edgeCenterA - edgeCenterB;
        float      aSqr  = segA.dot(segA);
        float      bSqr  = segB.dot(segB);
        float      ab    = segA.dot(segB);
        float      aDiff = segA.dot(d);
        float      bDiff = segB.dot(d);
        float      det   = aSqr * bSqr - ab * ab;

        float tA = 0.0f, tB = bDiff / bSqr;
        if (std::abs(det) > 1e-8f) {
            tA = (ab * bDiff - bSqr * aDiff) / det;
            tB = (aSqr * bDiff - ab  * aDiff) / det;
        }
        tA           = std::max(-1.0f, std::min(1.0f, tA));
        float recomp = (ab * tA + bDiff) / bSqr;
        tB           = std::max(-1.0f, std::min(1.0f, recomp));
        recomp       = (ab * tB - aDiff) / aSqr;
        tA           = std::max(-1.0f, std::min(1.0f, recomp));

        m.contactPoints[0] = (edgeCenterA + segA * tA + edgeCenterB + segB * tB) * 0.5f;
        m.depths[0]        = m.penetration;
        m.numContacts      = 1;
    }
    return true;
}

// ============================================================================
// Analytical pair detection (sphere-capsule, capsule-capsule)
// sphere-sphere is analyticalSphereSphere defined above (shared with detect()).
// ============================================================================

bool analyticalSphereCapsule(const SphereGeom& a, const CapsuleGeom& b, ContactManifold& m) {
    math::Vec3 sphereToCapsule = a.getPosition() - b.getPosition();
    const auto& mat = b.getRotTransform().m;
    math::Vec3 worldUp = { mat[3], mat[4], mat[5] };

    //calculate the critical t
    float tCrit = worldUp.dot(sphereToCapsule);
    //must clamp as well
    tCrit = std::max(-b.getHalfHeight(), std::min(b.getHalfHeight(), tCrit));

    //closest point in capsule t dimension is tCrit
    math::Vec3 component = worldUp * (tCrit);

    //closest point is this dot*worldUp + b.getPosition()
    //we can now proceed as normal psehre sphere
    math::Vec3 diff   = component - sphereToCapsule;
    float      distSq = diff.dot(diff);
    float      rSum   = a.getRadius() + b.getRadius();
    if (distSq > rSum * rSum) return false;

    float dist = std::sqrt(distSq);
    if (dist < 1e-7f) [[unlikely]]{
        m.normal      = {1.0f, 0.0f, 0.0f};
        m.penetration = rSum;
    } else [[likely]]{
        float invDist = 1.0f / dist;
        m.normal      = diff * invDist;
        m.penetration = rSum - dist;
    }
    m.numContacts      = 1;
    m.contactPoints[0] = a.getPosition() + m.normal * a.getRadius();
    m.depths[0]        = m.penetration;
    return true;
}
bool analyticalCapsuleCapsule(const CapsuleGeom& a, const CapsuleGeom& b, ContactManifold& m) {
    //this is a bit more complex/involved since we have to find the 2 points on the line segments
    //so parametrize the 2 line segments, minimize the distance between the 2 and use the result as the closest points
    //but then we can proceed as sphere sphere

    //compute halfHeight vectors for both
    const auto& matA = a.getRotTransform().m;
    math::Vec3 worldUpA = { matA[3], matA[4], matA[5] };
    worldUpA *= a.getHalfHeight();

    const auto& matB = b.getRotTransform().m;
    math::Vec3 worldUpB = { matB[3], matB[4], matB[5] };
    worldUpB *= b.getHalfHeight();

    //compute difference vector and other quantities to solve this quadratic
    math::Vec3 diff = a.getPosition() - b.getPosition();
    float aSqr = worldUpA.dot(worldUpA);
    float bSqr = worldUpB.dot(worldUpB);
    float ab = worldUpA.dot(worldUpB);
    float aDiff = worldUpA.dot(diff);
    float bDiff = worldUpB.dot(diff);

    //quadratic det
    float det = aSqr * bSqr - ab * ab;

    //now solve for the params of t for both (default values for parallel case are these)
    float tA = 0, tB = bDiff / bSqr;

    //check for non parallel
    if(std::abs(det) > EPSILON) [[likely]]{
        //non parallel, they will intersect, so compute ts normally
        tA = (ab * bDiff - bSqr * aDiff) / det;
        tB = (aSqr * bDiff - ab * aDiff) / det;
    }

    //now the first T, clamp to [-1,1]
    tA = std::max(-1.0f, std::min(1.0f, tA));

    //recompute the other with this clamped T (cant clamp both directly)
    float recomputed = (ab * tA + bDiff) / bSqr;
    tB = std::max(-1.0f, std::min(1.0f, recomputed));

    //now if tB was also clamped, recompute tA
    recomputed = (ab * tB - aDiff) / aSqr;
    tA = std::max(-1.0f, std::min(1.0f, recomputed));

    //now compute the difference between the two actual points (not the points themselves, avoid doing all that)
    math::Vec3 aComponent = worldUpA * tA;
    diff = worldUpB * tB - aComponent - diff;
    //now proceed as normal with sphere-sphere
    float      distSq = diff.dot(diff);
    float      rSum   = a.getRadius() + b.getRadius();
    if (distSq > rSum * rSum) return false;

    float dist = std::sqrt(distSq);
    if (dist < 1e-7f) [[unlikely]] {
        m.normal      = {1.0f, 0.0f, 0.0f};
        m.penetration = rSum;
    } else [[likely]] {
        float invDist = 1.0f / dist;
        m.normal      = diff * invDist;
        m.penetration = rSum - dist;
    }
    m.numContacts      = 1;
    m.contactPoints[0] = (a.getPosition() + aComponent) + m.normal * a.getRadius();
    m.depths[0]        = m.penetration;
    return true;
}

// Returns true when this pair should bypass GJK and use analytical detection.
// Pairs: (Sphere,Sphere)=(0,0), (Sphere,Capsule)=(0,3)/(3,0), (Capsule,Capsule)=(3,3)
bool isAnalyticalPair(const ShapeVariant& va, const ShapeVariant& vb) {
    int ai = (int)va.index(), bi = (int)vb.index();
    if (ai > bi) std::swap(ai, bi);
    // indices 0=Sphere, 1=AABB, 2=OBB: all combos use SAT/analytical
    // (0,3)=sph-cap, (3,3)=cap-cap: closed-form analytical
    return (ai <= 2 && bi <= 2) || (ai == 0 && bi == 3) || (ai == 3 && bi == 3);
}

bool analyticalBoolean(const ShapeVariant& va, const ShapeVariant& vb, ContactManifold& m) {
    // std::visit dispatches to the right stub; unused combos return false.
    return std::visit([&m](const auto& a, const auto& b) -> bool {
        using A = std::decay_t<decltype(a)>;
        using B = std::decay_t<decltype(b)>;
        // Closed-form analytical
        if constexpr (std::is_same_v<A, SphereGeom>  && std::is_same_v<B, SphereGeom>)
            return analyticalSphereSphere(a, b, m);
        else if constexpr (std::is_same_v<A, SphereGeom>  && std::is_same_v<B, CapsuleGeom>)
            return analyticalSphereCapsule(a, b, m);
        else if constexpr (std::is_same_v<A, CapsuleGeom> && std::is_same_v<B, SphereGeom>)
            return analyticalSphereCapsule(b, a, m);
        else if constexpr (std::is_same_v<A, CapsuleGeom> && std::is_same_v<B, CapsuleGeom>)
            return analyticalCapsuleCapsule(a, b, m);
        // SAT — normal convention throughout: from a toward b.
        // analyticalSphereAABB/OBB produce normal AABB/OBB→sphere, so flip when sphere is va (entity a).
        // analyticalAABBOBB produces normal AABB→OBB; flip when OBB is va.
        else if constexpr (std::is_same_v<A, SphereGeom> && std::is_same_v<B, AABBGeom>)
            { bool h = analyticalSphereAABB(a, b, m); if (h) m.normal = -m.normal; return h; }
        else if constexpr (std::is_same_v<A, AABBGeom>   && std::is_same_v<B, SphereGeom>)
            return analyticalSphereAABB(b, a, m);
        else if constexpr (std::is_same_v<A, SphereGeom> && std::is_same_v<B, OBBGeom>)
            { bool h = analyticalSphereOBB(a, b, m); if (h) m.normal = -m.normal; return h; }
        else if constexpr (std::is_same_v<A, OBBGeom>    && std::is_same_v<B, SphereGeom>)
            return analyticalSphereOBB(b, a, m);
        else if constexpr (std::is_same_v<A, AABBGeom>   && std::is_same_v<B, AABBGeom>)
            return analyticalAABBAABB(a, b, m);
        else if constexpr (std::is_same_v<A, AABBGeom>   && std::is_same_v<B, OBBGeom>)
            return analyticalAABBOBB(a, b, m);
        else if constexpr (std::is_same_v<A, OBBGeom>    && std::is_same_v<B, AABBGeom>)
            { bool h = analyticalAABBOBB(b, a, m); if (h) m.normal = -m.normal; return h; }
        else if constexpr (std::is_same_v<A, OBBGeom>    && std::is_same_v<B, OBBGeom>)
            return analyticalOBBOBB(a, b, m);
        else
            return false;
    }, va, vb);
}

bool satBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg) {
    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return false;

    auto* sa = reg.get<ecs::SphereForm>(ea); auto* sb = reg.get<ecs::SphereForm>(eb);
    auto* aa = reg.get<ecs::AABBForm>(ea);   auto* ab = reg.get<ecs::AABBForm>(eb);
    auto* ca = reg.get<ecs::OBBForm>(ea);    auto* cb = reg.get<ecs::OBBForm>(eb);

    auto mSph  = [&](ecs::Entity e, float r)        -> SphereGeom { return {reg.get<Transform>(e)->position, r};   };
    auto mAABB = [&](ecs::Entity e, math::Vec3 ext) -> AABBGeom   { return {reg.get<Transform>(e)->position, ext}; };
    auto mOBB  = [&](ecs::Entity e, math::Vec3 ext) -> OBBGeom    {
        auto* tf = reg.get<Transform>(e);
        return {tf->position, ext, math::Mat3::rotation(tf->rotation)};
    };

    ContactManifold m;
    if (sa && sb) return analyticalSphereSphere(mSph(ea,sa->radius), mSph(eb,sb->radius), m);
    if (sa && ab) return analyticalSphereAABB  (mSph(ea,sa->radius), mAABB(eb,ab->extent), m);
    if (aa && sb) return analyticalSphereAABB  (mSph(eb,sb->radius), mAABB(ea,aa->extent), m);
    if (aa && ab) return analyticalAABBAABB    (mAABB(ea,aa->extent), mAABB(eb,ab->extent), m);
    if (sa && cb) return analyticalSphereOBB   (mSph(ea,sa->radius), mOBB(eb,cb->extent), m);
    if (ca && sb) return analyticalSphereOBB   (mSph(eb,sb->radius), mOBB(ea,ca->extent), m);
    if (aa && cb) return analyticalAABBOBB     (mAABB(ea,aa->extent), mOBB(eb,cb->extent), m);
    if (ca && ab) return analyticalAABBOBB     (mAABB(eb,ab->extent), mOBB(ea,ca->extent), m);
    if (ca && cb) return analyticalOBBOBB      (mOBB(ea,ca->extent), mOBB(eb,cb->extent), m);
    return false;
}

} // namespace ColliderDiscrete
} // namespace physics
