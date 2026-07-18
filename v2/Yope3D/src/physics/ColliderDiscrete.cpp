#include "ColliderDiscrete.h"
#include "PhysicsConstants.h"
#include "CompoundShape.h"
#include "KinematicQuery.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include "../debug/Profiler.h"
#include <atomic>
#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>
#include <vector>
#include <type_traits>

namespace physics::ColliderDiscrete {

// Read once per solveIsland call on every pool worker; written from the main
// thread (script/editor toggle). Relaxed is enough — a tick either sees the old
// or the new value, and both are valid simulations.
std::atomic<bool> g_warmStart{ true };

void setWarmStartEnabled(bool on) { g_warmStart.store(on, std::memory_order_relaxed); }
bool warmStartEnabled()           { return g_warmStart.load(std::memory_order_relaxed); }

namespace {

// Cross-product ("skew-symmetric") matrix S such that S*x == v.cross(x).
// Column-major (m[col*3+row], matching Mat3's convention elsewhere in this file).
math::Mat3 skew(const math::Vec3& v) {
    math::Mat3 s;
    s.m[0] = 0.0f;  s.m[3] = -v.z; s.m[6] =  v.y;
    s.m[1] =  v.z;  s.m[4] = 0.0f; s.m[7] = -v.x;
    s.m[2] = -v.y;  s.m[5] =  v.x; s.m[8] = 0.0f;
    return s;
}

// K = (invMassA + invMassB) * I3 - [rA]x * IinvA * [rA]x - [rB]x * IinvB * [rB]x
// (same effective-mass shape as the contact solver's scalar effN, generalized
// to a 3x3 block since a ball socket has no preferred axis). Shared by every
// joint type below — all of them retain the point-to-point block as their
// linear-DOF-removing core.
math::Mat3 computePointBlockK(const math::Vec3& rA, const math::Vec3& rB,
                              const ecs::Hull* ha, const ecs::Hull* hb) {
    math::Mat3 skewA = skew(rA), skewB = skew(rB);
    math::Mat3 termA = skewA * ha->inertiaTensorWorld * skewA;
    math::Mat3 termB = skewB * hb->inertiaTensorWorld * skewB;
    math::Mat3 K;
    for (int i = 0; i < 9; ++i) K.m[i] = -termA.m[i] - termB.m[i];
    float invSum = ha->inverseMass + hb->inverseMass;
    K.m[0] += invSum; K.m[4] += invSum; K.m[8] += invSum;
    return K.inverse();
}

// Effective mass for a pure-angular (no lever arm) scalar constraint row along
// a world-space axis: eff = axis.(IinvA*axis) + axis.(IinvB*axis).
float angularEffectiveMass(const math::Vec3& axis, const math::Mat3& IinvA, const math::Mat3& IinvB) {
    float eff = axis.dot(IinvA * axis) + axis.dot(IinvB * axis);
    return (eff > 1e-6f) ? 1.0f / eff : 0.0f;
}

// Swing-twist decomposition of a relative rotation qRel about world-space
// `axis`: qRel = swing * twist, twist a pure rotation about axis. Standard
// technique (project qRel's vector part onto axis to get the twist quat,
// swing = qRel * ~twist). twistAngle needs no explicit normalization —
// atan2 is invariant to positive scaling of both arguments, and (proj, qRel.w)
// scale identically to the normalized twist quat's (v.axis, w).
struct SwingTwist { float twistAngle; math::Vec3 swingAxis; float swingAngle; };

SwingTwist decomposeSwingTwist(const math::Quat& qRelIn, const math::Vec3& axis) {
    // Canonicalize to the w >= 0 cover: q and -q are the same rotation, but the
    // angle formulas below are not sign-invariant — an uncanonicalized qRel past
    // 180 deg reads as ~2pi-of-swing / +-2pi-of-twist with an inverted correction
    // direction, which feeds the limit rows garbage.
    math::Quat qRel = (qRelIn.w < 0.0f)
        ? math::Quat{-qRelIn.x, -qRelIn.y, -qRelIn.z, -qRelIn.w}
        : qRelIn;

    math::Vec3 v{qRel.x, qRel.y, qRel.z};
    float proj = v.dot(axis);
    float twistAngle = 2.0f * std::atan2(proj, qRel.w);

    math::Vec3 twistV = axis * proj;
    math::Quat twist{twistV.x, twistV.y, twistV.z, qRel.w};
    float tlen = std::sqrt(twist.x*twist.x + twist.y*twist.y + twist.z*twist.z + twist.w*twist.w);
    math::Quat twistN = (tlen > 1e-8f)
        ? math::Quat{twist.x/tlen, twist.y/tlen, twist.z/tlen, twist.w/tlen}
        : math::Quat{0.0f, 0.0f, 0.0f, 1.0f};
    math::Quat swing = qRel * (~twistN);
    if (swing.w < 0.0f) swing = math::Quat{-swing.x, -swing.y, -swing.z, -swing.w};

    float swingW = std::max(-1.0f, std::min(1.0f, swing.w));
    float swingAngle = 2.0f * std::acos(swingW);
    math::Vec3 swingAxis{swing.x, swing.y, swing.z};
    float slen = std::sqrt(swingAxis.dot(swingAxis));
    swingAxis = (slen > 1e-6f) ? swingAxis * (1.0f / slen) : math::Vec3{0.0f, 0.0f, 1.0f};
    return {twistAngle, swingAxis, swingAngle};
}

// Shared shape for a single velocity-level unilateral angular limit row
// (Hinge's angle limit, ConeTwist's swing/twist limits) — same accumulate-
// clamp-apply idiom as a contact normal row, just with a pure angular
// (lever-arm-free) Jacobian. `dir` must already point in the "decreases the
// violation" direction; only active once `violation` (current angle already
// past the bound) is positive, so it does nothing while safely inside the
// limit and only engages to stop the rotation from digging the violation
// deeper *this same substep* — the position-level counterpart
// (solveAngularLimitRow, below) only runs once per substep after
// integration, so without this a fast spin can blow well past the limit for
// several substeps before the position pull-back wins the tug-of-war.
template <class ApplyFn>
void solveVelocityAngularLimitRow(ecs::Hull* ha, ecs::Hull* hb, const math::Vec3& dir,
                                  float violation, float W, float& lambda, ApplyFn&& applyImpulses) {
    if (violation <= 0.0f) { lambda = 0.0f; return; }
    float cdot = (hb->omega - ha->omega).dot(dir);
    float dl = W * -cdot;
    float oldL = lambda;
    lambda = std::max(0.0f, lambda + dl);
    float applied = lambda - oldL;
    if (std::abs(applied) < 1e-10f) return;
    ha->angularImpulse += dir * -applied;
    hb->angularImpulse += dir *  applied;
    applyImpulses(ha);
    applyImpulses(hb);
}

// ---- PointToPointJoint: 3x3 coupled block, bilateral (push AND pull). ----
// Reuses the contact solver's effective-mass-formula shape generalized to a
// 3x3 block (a ball socket has no preferred axis, so independent scalar rows
// per axis converge far slower under warm start than one coupled solve), and
// its accumulate-impulse idiom with lo=-inf/hi=+inf (no clamp) instead of the
// contact normal's [0,inf) unilateral clamp.

void precomputePointToPoint(PointToPointJoint& j, ecs::Registry& reg) {
    auto* ha = reg.get<ecs::Hull>(j.a);   auto* hb = reg.get<ecs::Hull>(j.b);
    auto* tfa = reg.get<Transform>(j.a);  auto* tfb = reg.get<Transform>(j.b);
    if (!ha || !hb || !tfa || !tfb) { j.K = math::Mat3::zero(); return; }

    math::Mat3 Ra = math::Mat3::rotation(tfa->rotation);
    math::Mat3 Rb = math::Mat3::rotation(tfb->rotation);
    j.rA = Ra * j.localAnchorA;
    j.rB = Rb * j.localAnchorB;
    j.K = computePointBlockK(j.rA, j.rB, ha, hb);
}

template <class ApplyFn>
void warmStartPointToPoint(PointToPointJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    if (!ha || !hb) return;
    j.lambda  = j.lambda * 0.999f;   // same decay factor as contacts' normal warm-start
    j.lambdaP = {};                  // position accumulator never carries across frames

    const math::Vec3& imp = j.lambda;
    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -imp; ha->angularImpulse += j.rA.cross(-imp); }
    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  imp; hb->angularImpulse += j.rB.cross( imp); }
    applyImpulses(ha);
    applyImpulses(hb);
}

template <class ApplyFn>
void solveVelocityPointToPoint(PointToPointJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    if (!ha || !hb) return;

    math::Vec3 relVel = hb->velocity + hb->omega.cross(j.rB)
                       - ha->velocity - ha->omega.cross(j.rA);
    math::Vec3 dLambda = j.K * (relVel * -1.0f);   // bilateral: no clamp
    j.lambda += dLambda;

    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -dLambda; ha->angularImpulse += j.rA.cross(-dLambda); }
    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  dLambda; hb->angularImpulse += j.rB.cross( dLambda); }
    applyImpulses(ha);
    applyImpulses(hb);
}

void solvePositionPointToPoint(PointToPointJoint& j, ecs::Registry& reg, float dt) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    auto* tfa = reg.get<Transform>(j.a); auto* tfb = reg.get<Transform>(j.b);
    if (!ha || !hb || !tfa || !tfb) return;
    bool aFixed = reg.has<ecs::Fixed>(j.a);
    bool bFixed = reg.has<ecs::Fixed>(j.b);

    math::Vec3 cStar = (hb->pseudoVel + hb->pseudoOmega.cross(j.rB))
                      - (ha->pseudoVel + ha->pseudoOmega.cross(j.rA));
    math::Vec3 posError = (tfb->position + j.rB) - (tfa->position + j.rA);
    // Target relative velocity that decays the gap exponentially (Baumgarte),
    // NOT positive-fed-back with it — sign flips relative to the contact
    // solver's unilateral bias, which instead targets a minimum *separating*
    // speed for a positive penetration depth.
    math::Vec3 bias = posError * (-SPLIT_BETA * j.correctionStiffness / dt);

    math::Vec3 dLambdaP = j.K * (bias - cStar);
    j.lambdaP += dLambdaP;

    if (!aFixed) {
        ha->pseudoVel   += -dLambdaP * ha->inverseMass;
        ha->pseudoOmega += ha->inertiaTensorWorld * j.rA.cross(-dLambdaP);
    }
    if (!bFixed) {
        hb->pseudoVel   +=  dLambdaP * hb->inverseMass;
        hb->pseudoOmega += hb->inertiaTensorWorld * j.rB.cross( dLambdaP);
    }
}

// ---- HingeJoint: PointToPoint's 3x3 block + 2 angular-lock rows (removes 2 of
// the 3 rotational DOF, leaving rotation free only about the shared axis) +
// an optional angle limit. Limit is position-only (split-impulse), not also
// velocity-clamped — see the struct comment in Joint.h for the rationale.

void precomputeHinge(HingeJoint& j, ecs::Registry& reg) {
    auto* ha = reg.get<ecs::Hull>(j.a);   auto* hb = reg.get<ecs::Hull>(j.b);
    auto* tfa = reg.get<Transform>(j.a);  auto* tfb = reg.get<Transform>(j.b);
    if (!ha || !hb || !tfa || !tfb) {
        j.K = math::Mat3::zero(); j.Wang1 = j.Wang2 = j.Wlimit = 0.0f;
        return;
    }

    math::Mat3 Ra = math::Mat3::rotation(tfa->rotation);
    math::Mat3 Rb = math::Mat3::rotation(tfb->rotation);
    j.rA = Ra * j.localAnchorA;
    j.rB = Rb * j.localAnchorB;
    j.K = computePointBlockK(j.rA, j.rB, ha, hb);

    j.axisWorld = Ra * j.localAxisA;
    float alen = std::sqrt(j.axisWorld.dot(j.axisWorld));
    if (alen > 1e-6f) j.axisWorld = j.axisWorld * (1.0f / alen);

    // Same T1/T2-from-normal idiom as the contact solver's tangent basis
    // (ColliderDiscrete.cpp precompute above), applied to the hinge axis.
    j.perp1 = (std::abs(j.axisWorld.x) < 0.9f)
            ? j.axisWorld.cross({1.0f, 0.0f, 0.0f})
            : j.axisWorld.cross({0.0f, 1.0f, 0.0f});
    float p1len = std::sqrt(j.perp1.dot(j.perp1));
    if (p1len > 1e-7f) j.perp1 = j.perp1 * (1.0f / p1len);
    j.perp2 = j.axisWorld.cross(j.perp1);

    j.Wang1  = angularEffectiveMass(j.perp1,     ha->inertiaTensorWorld, hb->inertiaTensorWorld);
    j.Wang2  = angularEffectiveMass(j.perp2,     ha->inertiaTensorWorld, hb->inertiaTensorWorld);
    j.Wlimit = angularEffectiveMass(j.axisWorld, ha->inertiaTensorWorld, hb->inertiaTensorWorld);

    math::Quat qRel = tfb->rotation * (~tfa->rotation);
    j.currentAngle = decomposeSwingTwist(qRel, j.axisWorld).twistAngle;

    // decomposeSwingTwist returns the twist in (-pi, pi] — a branch cut at
    // +-pi. An asymmetric hinge range near that cut (e.g. the elbow's [0, 2.4])
    // lets a fast overshoot cross pi, whereupon the measured angle jumps from
    // ~+pi to ~-pi: the joint reads its over-flexion as a NEGATIVE angle below
    // the lower limit, and the unilateral limit row then drives it the WRONG
    // way (deeper past the limit instead of back). Unwrap into the 2pi window
    // centred on the limit range so any reachable overshoot stays a continuous,
    // correctly-signed violation. (Only meaningful when the limit is enabled;
    // harmless otherwise.)
    if (j.limitEnabled) {
        constexpr float PI     = 3.14159265358979323846f;
        constexpr float TWO_PI = 2.0f * PI;
        float mid = 0.5f * (j.lowerAngle + j.upperAngle);
        while (j.currentAngle - mid >  PI) j.currentAngle -= TWO_PI;
        while (j.currentAngle - mid < -PI) j.currentAngle += TWO_PI;
    }
}

template <class ApplyFn>
void warmStartHinge(HingeJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    if (!ha || !hb) return;
    j.lambda     = j.lambda     * 0.999f;
    j.lambdaAng1 = j.lambdaAng1 * 0.999f;
    j.lambdaAng2 = j.lambdaAng2 * 0.999f;
    j.lambdaP        = {};   // position accumulators never carry across frames
    j.lambdaLimit    = 0.0f;
    j.lambdaLimitVel = 0.0f;

    const math::Vec3& imp = j.lambda;
    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -imp; ha->angularImpulse += j.rA.cross(-imp); }
    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  imp; hb->angularImpulse += j.rB.cross( imp); }
    // Angular-lock rows: pure angular impulse, no lever arm / no linear component.
    ha->angularImpulse += j.perp1 * -j.lambdaAng1;
    hb->angularImpulse += j.perp1 *  j.lambdaAng1;
    ha->angularImpulse += j.perp2 * -j.lambdaAng2;
    hb->angularImpulse += j.perp2 *  j.lambdaAng2;
    applyImpulses(ha);
    applyImpulses(hb);
}

template <class ApplyFn>
void solveVelocityHinge(HingeJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    if (!ha || !hb) return;

    math::Vec3 relVel = hb->velocity + hb->omega.cross(j.rB)
                       - ha->velocity - ha->omega.cross(j.rA);
    math::Vec3 dLambda = j.K * (relVel * -1.0f);
    j.lambda += dLambda;
    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -dLambda; ha->angularImpulse += j.rA.cross(-dLambda); }
    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  dLambda; hb->angularImpulse += j.rB.cross( dLambda); }
    applyImpulses(ha);
    applyImpulses(hb);

    if (j.Wang1 > 0.0f) {
        float cdot = (hb->omega - ha->omega).dot(j.perp1);
        float dl = j.Wang1 * -cdot;
        j.lambdaAng1 += dl;
        ha->angularImpulse += j.perp1 * -dl;
        hb->angularImpulse += j.perp1 *  dl;
        applyImpulses(ha);
        applyImpulses(hb);
    }
    if (j.Wang2 > 0.0f) {
        float cdot = (hb->omega - ha->omega).dot(j.perp2);
        float dl = j.Wang2 * -cdot;
        j.lambdaAng2 += dl;
        ha->angularImpulse += j.perp2 * -dl;
        hb->angularImpulse += j.perp2 *  dl;
        applyImpulses(ha);
        applyImpulses(hb);
    }

    if (j.limitEnabled && j.Wlimit > 0.0f) {
        float violation = 0.0f;
        math::Vec3 dir{};
        if (j.currentAngle > j.upperAngle)      { violation = j.currentAngle - j.upperAngle; dir = j.axisWorld * -1.0f; }
        else if (j.currentAngle < j.lowerAngle) { violation = j.lowerAngle - j.currentAngle; dir = j.axisWorld; }
        solveVelocityAngularLimitRow(ha, hb, dir, violation, j.Wlimit, j.lambdaLimitVel, applyImpulses);
    } else {
        j.lambdaLimitVel = 0.0f;
    }
}

void solvePositionHinge(HingeJoint& j, ecs::Registry& reg, float dt) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    auto* tfa = reg.get<Transform>(j.a); auto* tfb = reg.get<Transform>(j.b);
    if (!ha || !hb || !tfa || !tfb) return;
    bool aFixed = reg.has<ecs::Fixed>(j.a);
    bool bFixed = reg.has<ecs::Fixed>(j.b);

    math::Vec3 cStar = (hb->pseudoVel + hb->pseudoOmega.cross(j.rB))
                      - (ha->pseudoVel + ha->pseudoOmega.cross(j.rA));
    math::Vec3 posError = (tfb->position + j.rB) - (tfa->position + j.rA);
    math::Vec3 bias = posError * (-SPLIT_BETA * j.correctionStiffness / dt);
    math::Vec3 dLambdaP = j.K * (bias - cStar);
    j.lambdaP += dLambdaP;
    if (!aFixed) { ha->pseudoVel += -dLambdaP * ha->inverseMass; ha->pseudoOmega += ha->inertiaTensorWorld * j.rA.cross(-dLambdaP); }
    if (!bFixed) { hb->pseudoVel +=  dLambdaP * hb->inverseMass; hb->pseudoOmega += hb->inertiaTensorWorld * j.rB.cross( dLambdaP); }

    if (!j.limitEnabled || j.Wlimit <= 0.0f) return;

    // Unilateral, position-only: `dir` is the direction that DECREASES the
    // violation when the correction impulse is applied positively — same
    // accumulate-clamp-apply shape as a contact normal row, just with a pure
    // angular (lever-arm-free) Jacobian instead of a point-contact one.
    float violation = 0.0f;
    math::Vec3 dir{};
    if (j.currentAngle > j.upperAngle)      { violation = j.currentAngle - j.upperAngle; dir = j.axisWorld * -1.0f; }
    else if (j.currentAngle < j.lowerAngle) { violation = j.lowerAngle - j.currentAngle; dir = j.axisWorld; }

    if (violation <= 0.0f) { j.lambdaLimit = 0.0f; return; }

    float cStarAng = (hb->pseudoOmega - ha->pseudoOmega).dot(dir);
    float biasAng  = (SPLIT_BETA * j.correctionStiffness / dt) * violation;
    float dl = j.Wlimit * (biasAng - cStarAng);
    float oldL = j.lambdaLimit;
    j.lambdaLimit = std::max(0.0f, j.lambdaLimit + dl);
    float applied = j.lambdaLimit - oldL;
    if (!aFixed) ha->pseudoOmega += ha->inertiaTensorWorld * (dir * -applied);
    if (!bFixed) hb->pseudoOmega += hb->inertiaTensorWorld * (dir *  applied);
}

// ---- ConeTwistJoint: PointToPoint's 3x3 block + swing-cone limit + twist
// limit, each enforced at both the velocity level (solveVelocityAngularLimitRow,
// above) and the position level (solveAngularLimitRow, below) — see HingeJoint's
// comment in Joint.h for why both passes are needed.

void precomputeConeTwist(ConeTwistJoint& j, ecs::Registry& reg) {
    auto* ha = reg.get<ecs::Hull>(j.a);   auto* hb = reg.get<ecs::Hull>(j.b);
    auto* tfa = reg.get<Transform>(j.a);  auto* tfb = reg.get<Transform>(j.b);
    if (!ha || !hb || !tfa || !tfb) {
        j.K = math::Mat3::zero(); j.Wswing = j.Wtwist = 0.0f;
        return;
    }

    math::Mat3 Ra = math::Mat3::rotation(tfa->rotation);
    math::Mat3 Rb = math::Mat3::rotation(tfb->rotation);
    j.rA = Ra * j.localAnchorA;
    j.rB = Rb * j.localAnchorB;
    j.K = computePointBlockK(j.rA, j.rB, ha, hb);

    j.twistAxisWorld = Ra * j.localTwistAxisA;
    float alen = std::sqrt(j.twistAxisWorld.dot(j.twistAxisWorld));
    if (alen > 1e-6f) j.twistAxisWorld = j.twistAxisWorld * (1.0f / alen);

    math::Quat qRel = tfb->rotation * (~tfa->rotation);
    SwingTwist st = decomposeSwingTwist(qRel, j.twistAxisWorld);
    j.twistAngle  = st.twistAngle;
    j.swingAxis   = st.swingAxis;
    j.swingAngle  = st.swingAngle;

    j.Wswing = angularEffectiveMass(j.swingAxis,     ha->inertiaTensorWorld, hb->inertiaTensorWorld);
    j.Wtwist = angularEffectiveMass(j.twistAxisWorld, ha->inertiaTensorWorld, hb->inertiaTensorWorld);
}

template <class ApplyFn>
void warmStartConeTwist(ConeTwistJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    if (!ha || !hb) return;
    j.lambda = j.lambda * 0.999f;
    j.lambdaP = {};
    j.lambdaSwing = j.lambdaTwistPos = j.lambdaTwistNeg = 0.0f;
    j.lambdaSwingVel = j.lambdaTwistPosVel = j.lambdaTwistNegVel = 0.0f;

    const math::Vec3& imp = j.lambda;
    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -imp; ha->angularImpulse += j.rA.cross(-imp); }
    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  imp; hb->angularImpulse += j.rB.cross( imp); }
    applyImpulses(ha);
    applyImpulses(hb);
}

template <class ApplyFn>
void solveVelocityConeTwist(ConeTwistJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    if (!ha || !hb) return;

    math::Vec3 relVel = hb->velocity + hb->omega.cross(j.rB)
                       - ha->velocity - ha->omega.cross(j.rA);
    math::Vec3 dLambda = j.K * (relVel * -1.0f);
    j.lambda += dLambda;
    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -dLambda; ha->angularImpulse += j.rA.cross(-dLambda); }
    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  dLambda; hb->angularImpulse += j.rB.cross( dLambda); }
    applyImpulses(ha);
    applyImpulses(hb);

    if (j.Wswing > 0.0f)
        solveVelocityAngularLimitRow(ha, hb, j.swingAxis * -1.0f,
                                     j.swingAngle - j.swingLimit, j.Wswing, j.lambdaSwingVel, applyImpulses);
    else
        j.lambdaSwingVel = 0.0f;

    if (j.Wtwist > 0.0f) {
        solveVelocityAngularLimitRow(ha, hb, j.twistAxisWorld * -1.0f,
                                     j.twistAngle - j.twistLimit, j.Wtwist, j.lambdaTwistPosVel, applyImpulses);
        solveVelocityAngularLimitRow(ha, hb, j.twistAxisWorld,
                                     -j.twistLimit - j.twistAngle, j.Wtwist, j.lambdaTwistNegVel, applyImpulses);
    } else {
        j.lambdaTwistPosVel = j.lambdaTwistNegVel = 0.0f;
    }
}

// Shared shape for a single position-only unilateral angular limit row (used
// 3x below: swing, twist-positive, twist-negative). `dir` must already point
// in the "decreases the violation" direction; `lambda` is that row's own
// accumulator.
void solveAngularLimitRow(ecs::Hull* ha, ecs::Hull* hb, bool aFixed, bool bFixed,
                          const math::Vec3& dir, float violation, float W, float dt,
                          float correctionStiffness, float& lambda) {
    if (violation <= 0.0f) { lambda = 0.0f; return; }
    float cStarAng = (hb->pseudoOmega - ha->pseudoOmega).dot(dir);
    float biasAng  = (SPLIT_BETA * correctionStiffness / dt) * violation;
    float dl = W * (biasAng - cStarAng);
    float oldL = lambda;
    lambda = std::max(0.0f, lambda + dl);
    float applied = lambda - oldL;
    if (!aFixed) ha->pseudoOmega += ha->inertiaTensorWorld * (dir * -applied);
    if (!bFixed) hb->pseudoOmega += hb->inertiaTensorWorld * (dir *  applied);
}

void solvePositionConeTwist(ConeTwistJoint& j, ecs::Registry& reg, float dt) {
    auto* ha = reg.get<ecs::Hull>(j.a); auto* hb = reg.get<ecs::Hull>(j.b);
    auto* tfa = reg.get<Transform>(j.a); auto* tfb = reg.get<Transform>(j.b);
    if (!ha || !hb || !tfa || !tfb) return;
    bool aFixed = reg.has<ecs::Fixed>(j.a);
    bool bFixed = reg.has<ecs::Fixed>(j.b);

    math::Vec3 cStar = (hb->pseudoVel + hb->pseudoOmega.cross(j.rB))
                      - (ha->pseudoVel + ha->pseudoOmega.cross(j.rA));
    math::Vec3 posError = (tfb->position + j.rB) - (tfa->position + j.rA);
    math::Vec3 bias = posError * (-SPLIT_BETA * j.correctionStiffness / dt);
    math::Vec3 dLambdaP = j.K * (bias - cStar);
    j.lambdaP += dLambdaP;
    if (!aFixed) { ha->pseudoVel += -dLambdaP * ha->inverseMass; ha->pseudoOmega += ha->inertiaTensorWorld * j.rA.cross(-dLambdaP); }
    if (!bFixed) { hb->pseudoVel +=  dLambdaP * hb->inverseMass; hb->pseudoOmega += hb->inertiaTensorWorld * j.rB.cross( dLambdaP); }

    // Swing: single-sided (swingAngle is always >= 0 by construction of the decomposition).
    if (j.Wswing > 0.0f)
        solveAngularLimitRow(ha, hb, aFixed, bFixed, j.swingAxis * -1.0f,
                             j.swingAngle - j.swingLimit, j.Wswing, dt, j.correctionStiffness, j.lambdaSwing);
    else
        j.lambdaSwing = 0.0f;

    // Twist: two symmetric half-rows, one per bound.
    if (j.Wtwist > 0.0f) {
        solveAngularLimitRow(ha, hb, aFixed, bFixed, j.twistAxisWorld * -1.0f,
                             j.twistAngle - j.twistLimit, j.Wtwist, dt, j.correctionStiffness, j.lambdaTwistPos);
        solveAngularLimitRow(ha, hb, aFixed, bFixed, j.twistAxisWorld,
                             -j.twistLimit - j.twistAngle, j.Wtwist, dt, j.correctionStiffness, j.lambdaTwistNeg);
    } else {
        j.lambdaTwistPos = j.lambdaTwistNeg = 0.0f;
    }
}

// ---- SuspensionJoint: 1-DOF unilateral spring/damper along the wheel's
// world-space "up" axis, chassis-only ("ground" is an implicit Fixed body —
// no Hull lookup, no reaction impulse on it, exactly like a contact against
// a Fixed entity already skips that side). Unlike every joint above, this is
// NOT an iterative equality constraint — it's a direct force (same idiom as
// physics::Spring's one-shot write), so the entire impulse is computed once
// in warmStartSuspension (called once per substep, before the 24-iteration
// loop) rather than accumulated across velocity iterations; solveVelocitySuspension
// is intentionally a no-op. The effective-mass formula (precomputeSuspension)
// still reuses the contact normal row's exact shape.

void precomputeSuspension(SuspensionJoint& j, ecs::Registry& reg) {
    auto* ha = reg.get<ecs::Hull>(j.chassis);
    auto* tf = reg.get<Transform>(j.chassis);
    if (!ha || !tf || !j.grounded) { j.W = 0.0f; return; }

    j.rChassis = j.hitPoint - tf->position;
    math::Vec3 angA = (ha->inertiaTensorWorld * j.rChassis.cross(j.worldUp)).cross(j.rChassis);
    float effN = ha->inverseMass + angA.dot(j.worldUp);
    j.W = (effN > 1e-6f) ? 1.0f / effN : 0.0f;
}

template <class ApplyFn>
void warmStartSuspension(SuspensionJoint& j, ecs::Registry& reg, float dt, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.chassis);
    j.lambda = 0.0f;   // fresh spring force every substep — see struct comment in Joint.h
    if (!ha || !j.grounded || j.W <= 0.0f) return;

    math::Vec3 pointVel = ha->velocity + ha->omega.cross(j.rChassis);
    float vn = pointVel.dot(j.worldUp);
    float compression = j.restLength - j.currentLength;   // positive = spring compressed
    float forceMag = std::max(0.0f, j.stiffness * compression - j.damping * vn);   // Hooke's law; can't pull
    j.lambda = forceMag * dt;

    math::Vec3 imp = j.worldUp * j.lambda;
    if (ha->inverseMass > 0.0f) { ha->linearImpulse += imp; ha->angularImpulse += j.rChassis.cross(imp); }
    applyImpulses(ha);
}

// ---- WheelFrictionJoint: lateral (target 0, pure grip) + longitudinal
// (target = wheel surface speed, drive/brake) friction rows at the wheel
// contact, both bounded by [-mu*Fz,+mu*Fz] using the paired SuspensionJoint's
// this-substep lambda — reuses the contact friction row's exact
// accumulate-clamp shape (ColliderDiscrete.cpp's contact solve, `cone = c.mu
// * c.lambda[i]`), just with wheel-orientation-derived tangent directions
// instead of a manifold's T1/T2.

void precomputeWheelFriction(WheelFrictionJoint& j, ecs::Registry& reg) {
    auto* ha = reg.get<ecs::Hull>(j.chassis);
    auto* tf = reg.get<Transform>(j.chassis);
    if (!ha || !tf || !j.suspension || !j.suspension->grounded) { j.Wlong = j.Wlat = 0.0f; return; }

    j.rChassis = j.suspension->hitPoint - tf->position;   // same contact point the suspension uses

    math::Mat3 R = math::Mat3::rotation(tf->rotation);
    math::Vec3 chassisFwd = R * math::Vec3{0.0f, 0.0f, 1.0f};
    math::Mat3 steerRot = math::Mat3::rotation(j.suspension->worldUp, j.steerAngle);
    math::Vec3 wheelFwd = steerRot * chassisFwd;

    // Project the steered forward direction onto the ground plane (perp to
    // the raycast hit normal) for the actual contact-plane longitudinal axis;
    // lateral is perpendicular to both.
    const math::Vec3& n = j.suspension->hitNormal;
    math::Vec3 longDir = wheelFwd - n * wheelFwd.dot(n);
    float llen = std::sqrt(longDir.dot(longDir));
    j.longDir = (llen > 1e-6f) ? longDir * (1.0f / llen) : chassisFwd;
    j.latDir  = n.cross(j.longDir);

    math::Vec3 angLong = (ha->inertiaTensorWorld * j.rChassis.cross(j.longDir)).cross(j.rChassis);
    float effLong = ha->inverseMass + angLong.dot(j.longDir);
    j.Wlong = (effLong > 1e-6f) ? 1.0f / effLong : 0.0f;

    math::Vec3 angLat = (ha->inertiaTensorWorld * j.rChassis.cross(j.latDir)).cross(j.rChassis);
    float effLat = ha->inverseMass + angLat.dot(j.latDir);
    j.Wlat = (effLat > 1e-6f) ? 1.0f / effLat : 0.0f;
}

template <class ApplyFn>
void warmStartWheelFriction(WheelFrictionJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.chassis);
    if (!ha || !j.suspension || !j.suspension->grounded || j.Wlong <= 0.0f) {
        j.lambdaLong = j.lambdaLat = 0.0f;
        return;
    }
    j.lambdaLong *= 0.995f;   // same decay factor as contact friction's warm-start
    j.lambdaLat  *= 0.995f;
    math::Vec3 imp = j.longDir * j.lambdaLong + j.latDir * j.lambdaLat;
    if (ha->inverseMass > 0.0f) { ha->linearImpulse += imp; ha->angularImpulse += j.rChassis.cross(imp); }
    applyImpulses(ha);
}

template <class ApplyFn>
void solveVelocityWheelFriction(WheelFrictionJoint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    auto* ha = reg.get<ecs::Hull>(j.chassis);
    if (!ha || !j.suspension || !j.suspension->grounded) return;
    float Fz = j.suspension->lambda;
    if (Fz <= 0.0f) return;

    math::Vec3 contactVel = ha->velocity + ha->omega.cross(j.rChassis);

    if (j.driven && j.Wlong > 0.0f) {
        float targetSpeed = j.wheelAngularVel * j.radius;
        float vLong = contactVel.dot(j.longDir);
        float dl = j.Wlong * (targetSpeed - vLong);
        float cone = j.muLong * Fz;
        float oldL = j.lambdaLong;
        j.lambdaLong = std::max(-cone, std::min(cone, j.lambdaLong + dl));
        float applied = j.lambdaLong - oldL;
        if (std::abs(applied) > 1e-10f) {
            math::Vec3 imp = j.longDir * applied;
            if (ha->inverseMass > 0.0f) { ha->linearImpulse += imp; ha->angularImpulse += j.rChassis.cross(imp); }
            applyImpulses(ha);
        }
    }
    if (j.Wlat > 0.0f) {
        float vLat = contactVel.dot(j.latDir);
        float dl = j.Wlat * -vLat;
        float cone = j.muLat * Fz;
        float oldL = j.lambdaLat;
        j.lambdaLat = std::max(-cone, std::min(cone, j.lambdaLat + dl));
        float applied = j.lambdaLat - oldL;
        if (std::abs(applied) > 1e-10f) {
            math::Vec3 imp = j.latDir * applied;
            if (ha->inverseMass > 0.0f) { ha->linearImpulse += imp; ha->angularImpulse += j.rChassis.cross(imp); }
            applyImpulses(ha);
        }
    }
}

// ---- Type-dispatching entry points (std::visit + if constexpr, mirrors the
// analyticalBoolean() dispatch idiom in ColliderAnalytical.cpp). Each new
// joint type added in later phases gets one more `if constexpr` branch here
// rather than a new call site at every one of solveIsland's four passes. ----

void precomputeJoint(Joint& j, ecs::Registry& reg) {
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PointToPointJoint>) precomputePointToPoint(v, reg);
        else if constexpr (std::is_same_v<T, HingeJoint>)   precomputeHinge(v, reg);
        else if constexpr (std::is_same_v<T, ConeTwistJoint>) precomputeConeTwist(v, reg);
        else if constexpr (std::is_same_v<T, SuspensionJoint>) precomputeSuspension(v, reg);
        else if constexpr (std::is_same_v<T, WheelFrictionJoint>) precomputeWheelFriction(v, reg);
    }, j);
}

// `dt` is only consumed by SuspensionJoint (converts its spring/damper force
// to a one-shot impulse — see warmStartSuspension's comment); every other
// type ignores it.
template <class ApplyFn>
void warmStartJoint(Joint& j, ecs::Registry& reg, float dt, ApplyFn&& applyImpulses) {
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PointToPointJoint>) warmStartPointToPoint(v, reg, applyImpulses);
        else if constexpr (std::is_same_v<T, HingeJoint>)   warmStartHinge(v, reg, applyImpulses);
        else if constexpr (std::is_same_v<T, ConeTwistJoint>) warmStartConeTwist(v, reg, applyImpulses);
        else if constexpr (std::is_same_v<T, SuspensionJoint>) warmStartSuspension(v, reg, dt, applyImpulses);
        else if constexpr (std::is_same_v<T, WheelFrictionJoint>) warmStartWheelFriction(v, reg, applyImpulses);
    }, j);
}

template <class ApplyFn>
void solveVelocityJoint(Joint& j, ecs::Registry& reg, ApplyFn&& applyImpulses) {
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PointToPointJoint>) solveVelocityPointToPoint(v, reg, applyImpulses);
        else if constexpr (std::is_same_v<T, HingeJoint>)   solveVelocityHinge(v, reg, applyImpulses);
        else if constexpr (std::is_same_v<T, ConeTwistJoint>) solveVelocityConeTwist(v, reg, applyImpulses);
        else if constexpr (std::is_same_v<T, WheelFrictionJoint>) solveVelocityWheelFriction(v, reg, applyImpulses);
        // SuspensionJoint: no-op — its entire impulse is a one-shot force
        // already applied in warmStartSuspension (see Joint.h's struct comment).
    }, j);
}

void solvePositionJoint(Joint& j, ecs::Registry& reg, float dt) {
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PointToPointJoint>) solvePositionPointToPoint(v, reg, dt);
        else if constexpr (std::is_same_v<T, HingeJoint>)   solvePositionHinge(v, reg, dt);
        else if constexpr (std::is_same_v<T, ConeTwistJoint>) solvePositionConeTwist(v, reg, dt);
        // Suspension/WheelFriction: no position-level pass — the raycast IS
        // the ground-contact geometry, refreshed exactly each substep, so
        // there's no accumulated anchor drift to correct the way a two-body
        // joint's linear DOF can drift.
    }, j);
}

} // namespace

// ============================================================================
// Per-shape-pair narrowphase timing (A4).
// Each detect() call falls into one of 6 buckets (sph_sph, sph_aabb, sph_obb,
// aabb_aabb, aabb_obb, obb_obb). NPHASE_TIME(bucket) creates a thread-local
// RAII timer when YOPE_PROF_ENABLED; otherwise it expands to nothing.
// emitNarrowphaseProfile() pushes 6 records (even at count==0) so the CSV
// has a stable 6-row footprint per step for easy pandas pivot.
// (PairBucket/kBucketStage/NPHASE_TIME live in ColliderTypes.h — shared with
// detectGJK() in ColliderGJK.cpp, which records into the same accumulator.)
// ============================================================================

void resetNarrowphaseTiming() {
#ifdef YOPE_PROF_ENABLED
    g_npTiming = {};
#endif
}

void emitNarrowphaseProfile() {
#ifdef YOPE_PROF_ENABLED
    for (int i = 0; i < NP_BUCKETS; ++i)
        YOPE_PROF_EMIT(kBucketStage[i], "physics", g_npTiming.us[i], g_npTiming.n[i]);
#endif
}

// Maps a pair of ShapeVariant::index() values (normalized ai<=bi) to its
// timing bucket. 5×5 table: indices 0=Sphere, 1=AABB, 2=OBB, 3=Capsule, 4=Cylinder.
PairBucket getBucket(int ai, int bi) {
    if (ai > bi) std::swap(ai, bi);
    constexpr PairBucket table[5][5] = {
        { NP_SPH_SPH,   NP_SPH_AABB,  NP_SPH_OBB,   NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_SPH_AABB,  NP_AABB_AABB, NP_AABB_OBB,  NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_SPH_OBB,   NP_AABB_OBB,  NP_OBB_OBB,   NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_GJK_OTHER, NP_GJK_OTHER, NP_GJK_OTHER,  NP_GJK_OTHER, NP_GJK_OTHER },
        { NP_GJK_OTHER, NP_GJK_OTHER, NP_GJK_OTHER,  NP_GJK_OTHER, NP_GJK_OTHER },
    };
    if (ai < 0 || ai >= 5 || bi < 0 || bi >= 5) return NP_GJK_OTHER;
    return table[ai][bi];
}

// ============================================================================
// Global PGS (Projected Gauss-Seidel) solver — two-phase detect / solve.
// Phase 1: detect() appends an ActiveContact for each colliding pair.
// Phase 2: solveIsland() precomputes, warm-starts, iterates velocity
//          constraints, writes the cache, then runs split-impulse position pass.
// Normal convention: from a toward b throughout.
// ============================================================================

void solveIsland(std::vector<ActiveContact>& contacts, std::vector<Joint*>& joints, float dt,
                 ecs::Registry& reg, EntityContactCache& cache)
{
    auto getH  = [&](ecs::Entity e) -> ecs::Hull*   { return reg.get<ecs::Hull>(e); };
    auto getTf = [&](ecs::Entity e) -> Transform*   { return reg.get<Transform>(e); };
    auto isFixed = [&](ecs::Entity e) -> bool        { return reg.has<ecs::Fixed>(e); };

    // Apply accumulated impulses to velocity/omega; zero accumulators.
    // Takes the already-fetched Hull pointer to avoid a redundant registry lookup.
    auto applyImpulses = [](ecs::Hull* hc) {
        if (!hc) return;
        if (hc->inverseMass > 0.0f) {
            hc->velocity += hc->linearImpulse  * hc->inverseMass;
            hc->omega    += hc->inertiaTensorWorld * hc->angularImpulse;
        }
        hc->linearImpulse  = {};
        hc->angularImpulse = {};
    };

    // ---- Precompute ----
    for (auto& c : contacts) {
        auto* ha = getH(c.a);  auto* hb = getH(c.b);
        auto* tfa = getTf(c.a); auto* tfb = getTf(c.b);
        if (!ha || !hb || !tfa || !tfb) continue;

        math::Vec3 n = c.manifold.normal;
        c.T1 = (std::abs(n.x) < 0.9f)
             ? n.cross({1.0f, 0.0f, 0.0f})
             : n.cross({0.0f, 1.0f, 0.0f});
        float t1len = std::sqrt(c.T1.dot(c.T1));
        if (t1len > 1e-7f) c.T1 = c.T1 * (1.0f / t1len);
        c.T2  = n.cross(c.T1);
        c.mu  = std::sqrt(ha->friction * hb->friction);
        c.e   = std::sqrt(ha->restitution * hb->restitution);
        c.IinvA = ha->inertiaTensorWorld;
        c.IinvB = hb->inertiaTensorWorld;

        for (int i = 0; i < c.manifold.numContacts; i++) {
            c.rA[i] = c.manifold.contactPoints[i] - tfa->position;
            c.rB[i] = c.manifold.contactPoints[i] - tfb->position;

            math::Vec3 angA = (c.IinvA * c.rA[i].cross(n)).cross(c.rA[i]);
            math::Vec3 angB = (c.IinvB * c.rB[i].cross(n)).cross(c.rB[i]);
            float effN = ha->inverseMass + hb->inverseMass + angA.dot(n) + angB.dot(n);
            c.W[i] = (effN > 1e-6f) ? 1.0f / effN : 0.0f;

            math::Vec3 angT1A = (c.IinvA * c.rA[i].cross(c.T1)).cross(c.rA[i]);
            math::Vec3 angT1B = (c.IinvB * c.rB[i].cross(c.T1)).cross(c.rB[i]);
            float effT1 = ha->inverseMass + hb->inverseMass + angT1A.dot(c.T1) + angT1B.dot(c.T1);
            c.Wt1[i] = (effT1 > 1e-6f) ? 1.0f / effT1 : 0.0f;

            math::Vec3 angT2A = (c.IinvA * c.rA[i].cross(c.T2)).cross(c.rA[i]);
            math::Vec3 angT2B = (c.IinvB * c.rB[i].cross(c.T2)).cross(c.rB[i]);
            float effT2 = ha->inverseMass + hb->inverseMass + angT2A.dot(c.T2) + angT2B.dot(c.T2);
            c.Wt2[i] = (effT2 > 1e-6f) ? 1.0f / effT2 : 0.0f;

            math::Vec3 relVel0 = hb->velocity + hb->omega.cross(c.rB[i])
                               - ha->velocity - ha->omega.cross(c.rA[i]);
            float vn0 = relVel0.dot(n);
            c.neta[i] = (vn0 < -PGS_RESTITUTION_THRESHOLD) ? -c.e * vn0 : 0.0f;
        }
    }
    for (Joint* jp : joints) precomputeJoint(*jp, reg);

    // ---- Warm start ----
    // Gated by the global switch (see setWarmStartEnabled). With it off the
    // lambdas stay at zero here and the velocity loop below starts from scratch;
    // the cache is still *written* at the end of the step, so flipping the
    // switch back on picks straight up from the current impulses.
    const bool warmStart = warmStartEnabled();
    for (auto& c : contacts) {
        if (!warmStart) break;
        auto* ha = getH(c.a); auto* hb = getH(c.b);
        if (!ha || !hb) continue;
        math::Vec3 n = c.manifold.normal;
        for (int i = 0; i < c.manifold.numContacts; i++) {
            if (c.W[i] == 0.0f) continue;
            auto it = cache.find({c.a, c.b, c.shapeKey, c.manifold.featureIds[i]});
            if (it == cache.end()) continue;

            c.lambda[i]   = it->second.normal * 0.999f;
            c.lambdaT1[i] = it->second.t1     * 0.995f;
            c.lambdaT2[i] = it->second.t2     * 0.995f;
            float cone = c.mu * c.lambda[i];
            c.lambdaT1[i] = std::max(-cone, std::min(cone, c.lambdaT1[i]));
            c.lambdaT2[i] = std::max(-cone, std::min(cone, c.lambdaT2[i]));

            math::Vec3 imp = n * c.lambda[i] + c.T1 * c.lambdaT1[i] + c.T2 * c.lambdaT2[i];
            if (ha->inverseMass > 0.0f) { ha->linearImpulse += -imp; ha->angularImpulse += c.rA[i].cross(-imp); }
            if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  imp; hb->angularImpulse += c.rB[i].cross( imp); }
            applyImpulses(ha);
            applyImpulses(hb);
        }
    }
    for (Joint* jp : joints) warmStartJoint(*jp, reg, dt, applyImpulses);

    // ---- Velocity iterations (global) ----
    for (int iter = 0; iter < PGS_VELOCITY_ITERATIONS; iter++) {
        for (auto& c : contacts) {
            auto* ha = getH(c.a); auto* hb = getH(c.b);
            if (!ha || !hb) continue;
            math::Vec3 n = c.manifold.normal;

            for (int i = 0; i < c.manifold.numContacts; i++) {
                if (c.W[i] == 0.0f) continue;

                math::Vec3 relVel = hb->velocity + hb->omega.cross(c.rB[i])
                                  - ha->velocity - ha->omega.cross(c.rA[i]);

                // Normal
                float vn   = relVel.dot(n);
                float dL   = c.W[i] * -(vn - c.neta[i]);
                float oldL = c.lambda[i];
                c.lambda[i] = std::max(0.0f, c.lambda[i] + dL);
                float dl    = c.lambda[i] - oldL;
                if (std::abs(dl) > 1e-10f) {
                    math::Vec3 imp = n * dl;
                    if (ha->inverseMass > 0.0f) { ha->linearImpulse += -imp; ha->angularImpulse += c.rA[i].cross(-imp); }
                    if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  imp; hb->angularImpulse += c.rB[i].cross( imp); }
                    applyImpulses(ha);
                    applyImpulses(hb);
                    relVel = hb->velocity + hb->omega.cross(c.rB[i])
                           - ha->velocity - ha->omega.cross(c.rA[i]);
                }

                // T1 friction
                if (c.mu > 0.0f && c.Wt1[i] > 0.0f) {
                    float dLt1  = -c.Wt1[i] * relVel.dot(c.T1);
                    float oldT1 = c.lambdaT1[i];
                    float cone  = c.mu * c.lambda[i];
                    c.lambdaT1[i] = std::max(-cone, std::min(cone, c.lambdaT1[i] + dLt1));
                    float dT1 = c.lambdaT1[i] - oldT1;
                    if (std::abs(dT1) > 1e-10f) {
                        math::Vec3 fImp = c.T1 * dT1;
                        if (ha->inverseMass > 0.0f) { ha->linearImpulse += -fImp; ha->angularImpulse += c.rA[i].cross(-fImp); }
                        if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  fImp; hb->angularImpulse += c.rB[i].cross( fImp); }
                        applyImpulses(ha);
                        applyImpulses(hb);
                        relVel = hb->velocity + hb->omega.cross(c.rB[i])
                               - ha->velocity - ha->omega.cross(c.rA[i]);
                    }
                }

                // T2 friction
                if (c.mu > 0.0f && c.Wt2[i] > 0.0f) {
                    float dLt2  = -c.Wt2[i] * relVel.dot(c.T2);
                    float oldT2 = c.lambdaT2[i];
                    float cone  = c.mu * c.lambda[i];
                    c.lambdaT2[i] = std::max(-cone, std::min(cone, c.lambdaT2[i] + dLt2));
                    float dT2 = c.lambdaT2[i] - oldT2;
                    if (std::abs(dT2) > 1e-10f) {
                        math::Vec3 fImp = c.T2 * dT2;
                        if (ha->inverseMass > 0.0f) { ha->linearImpulse += -fImp; ha->angularImpulse += c.rA[i].cross(-fImp); }
                        if (hb->inverseMass > 0.0f) { hb->linearImpulse +=  fImp; hb->angularImpulse += c.rB[i].cross( fImp); }
                        applyImpulses(ha);
                        applyImpulses(hb);
                    }
                }
            }
        }
        for (Joint* jp : joints) solveVelocityJoint(*jp, reg, applyImpulses);
    }

    // ---- Cache write-back ----
    for (auto& c : contacts) {
        for (int i = 0; i < c.manifold.numContacts; i++) {
            if (c.W[i] == 0.0f) continue;
            cache[{c.a, c.b, c.shapeKey, c.manifold.featureIds[i]}] = {c.lambda[i], c.lambdaT1[i], c.lambdaT2[i]};
        }
    }

    // ---- Position iterations (split impulse) ----
    for (int iter = 0; iter < PGS_POSITION_ITERATIONS; iter++) {
        for (auto& c : contacts) {
            auto* ha = getH(c.a); auto* hb = getH(c.b);
            if (!ha || !hb) continue;
            math::Vec3 n = c.manifold.normal;
            for (int i = 0; i < c.manifold.numContacts; i++) {
                if (c.W[i] == 0.0f) continue;

                float cStarN = (hb->pseudoVel + hb->pseudoOmega.cross(c.rB[i])
                              - ha->pseudoVel - ha->pseudoOmega.cross(c.rA[i])).dot(n);
                float pseudoBias = (SPLIT_BETA / dt) * std::max(0.0f, c.manifold.depths[i] - SPLIT_SLOP);
                float dLp  = -c.W[i] * (cStarN - pseudoBias);
                float oldLp = c.lambdaP[i];
                c.lambdaP[i] = std::max(0.0f, c.lambdaP[i] + dLp);
                float dL = c.lambdaP[i] - oldLp;
                if (std::abs(dL) < 1e-10f) continue;

                math::Vec3 pImp = n * dL;
                if (!isFixed(c.a)) {
                    ha->pseudoVel   += -pImp * ha->inverseMass;
                    ha->pseudoOmega += c.IinvA * c.rA[i].cross(-pImp);
                }
                if (!isFixed(c.b)) {
                    hb->pseudoVel   +=  pImp * hb->inverseMass;
                    hb->pseudoOmega += c.IinvB * c.rB[i].cross( pImp);
                }
            }
        }
        for (Joint* jp : joints) solvePositionJoint(*jp, reg, dt);
    }
}

void refreshSuspensionRaycast(SuspensionJoint& j, ecs::Registry& reg) {
    auto* tf = reg.get<Transform>(j.chassis);
    if (!tf) { j.grounded = false; return; }

    math::Mat3 R = math::Mat3::rotation(tf->rotation);
    j.worldUp = R * j.localUp;
    float ulen = std::sqrt(j.worldUp.dot(j.worldUp));
    j.worldUp = (ulen > 1e-6f) ? j.worldUp * (1.0f / ulen) : math::Vec3{0.0f, 1.0f, 0.0f};

    math::Vec3 mountWorld = tf->position + R * j.localWheelPos;
    float maxDist = j.restLength + j.maxTravel;
    KinematicQuery::RayHit hit = KinematicQuery::raycast(mountWorld, j.worldUp * -1.0f, maxDist, reg, j.chassis);

    if (hit.hit) {
        j.grounded      = true;
        j.hitPoint       = hit.point;
        j.hitNormal      = hit.normal;
        j.currentLength  = hit.t;
    } else {
        j.grounded      = false;
        j.currentLength = maxDist;   // fully extended, no ground contact this substep
    }
}

// ============================================================================
// Geometry-only pair dispatch (shared by detect() semantics and detectCompound).
// Produces a manifold whose normal points from A toward B. Covers the same
// Sphere/AABB/OBB pairs detect() handles analytically; Capsule/Cylinder (and any
// pair without a closed-form routine) return false — matching detect()'s own
// coverage until GJK/EPA is wired in.
// ============================================================================
static bool detectGeomPair(const ShapeVariant& A, const ShapeVariant& B, ContactManifold& m) {
    auto* sa = std::get_if<SphereGeom>(&A); auto* aa = std::get_if<AABBGeom>(&A); auto* oa = std::get_if<OBBGeom>(&A);
    auto* sb = std::get_if<SphereGeom>(&B); auto* ab = std::get_if<AABBGeom>(&B); auto* ob = std::get_if<OBBGeom>(&B);

    // Each branch normalizes the analytical routine's native normal to A->B.
    // Routine conventions: SphereAABB/SphereOBB emit box->sphere; AABBOBB emits
    // aabb->obb; the symmetric routines emit first-arg->second-arg.
    if (sa && sb) return analyticalSphereSphere(*sa, *sb, m);
    if (sa && ab) { if (analyticalSphereAABB(*sa, *ab, m)) { m.normal = -m.normal; return true; } return false; }
    if (sa && ob) { if (analyticalSphereOBB (*sa, *ob, m)) { m.normal = -m.normal; return true; } return false; }
    if (aa && sb) return analyticalSphereAABB(*sb, *aa, m);
    if (aa && ab) return analyticalAABBAABB  (*aa, *ab, m);
    if (aa && ob) return analyticalAABBOBB   (*aa, *ob, m);
    if (oa && sb) return analyticalSphereOBB (*sb, *oa, m);
    if (oa && ab) { if (analyticalAABBOBB    (*ab, *oa, m)) { m.normal = -m.normal; return true; } return false; }
    if (oa && ob) return analyticalOBBOBB    (*oa, *ob, m);
    return false;
}

// Build a world-space ShapeVariant for an entity's single collider Form.
// Returns nullopt for shapes without an analytical narrowphase (capsule/cylinder)
// or entities with no recognized form.
static std::optional<ShapeVariant> buildEntityWorldGeom(ecs::Entity e, ecs::Registry& reg) {
    auto* tf = reg.get<Transform>(e);
    if (!tf) return std::nullopt;
    if (auto* sf = reg.get<ecs::SphereForm>(e)) return ShapeVariant{SphereGeom{tf->position, sf->radius}};
    if (auto* af = reg.get<ecs::AABBForm>(e))   return ShapeVariant{AABBGeom{tf->position, af->extent}};
    if (auto* of = reg.get<ecs::OBBForm>(e))
        return ShapeVariant{OBBGeom{tf->position, of->extent, math::Mat3::rotation(tf->rotation)}};
    return std::nullopt;
}

// Conservative world-space AABB of a Sphere/AABB/OBB geom (OBB rotation-fattened).
static void worldAABBofGeom(const ShapeVariant& g, math::Vec3& mn, math::Vec3& mx) {
    if (auto* s = std::get_if<SphereGeom>(&g)) {
        math::Vec3 r{s->radius, s->radius, s->radius};
        mn = s->pos - r; mx = s->pos + r;
    } else if (auto* a = std::get_if<AABBGeom>(&g)) {
        mn = a->pos - a->extent; mx = a->pos + a->extent;
    } else if (auto* o = std::get_if<OBBGeom>(&g)) {
        const math::Mat3& R = o->rot; const math::Vec3& x = o->extent;
        math::Vec3 f{
            std::fabs(R.m[0]) * x.x + std::fabs(R.m[3]) * x.y + std::fabs(R.m[6]) * x.z,
            std::fabs(R.m[1]) * x.x + std::fabs(R.m[4]) * x.y + std::fabs(R.m[7]) * x.z,
            std::fabs(R.m[2]) * x.x + std::fabs(R.m[5]) * x.y + std::fabs(R.m[8]) * x.z,
        };
        mn = o->pos - f; mx = o->pos + f;
    }
}

// Builds the world-space ShapeVariant for sub-shape `i` of a compound whose
// body transform is (Rbody, posBody). Returns nullopt for capsule/cylinder
// sub-shapes (no analytical pair yet).
static std::optional<ShapeVariant> buildSubShapeWorldGeom(const physics::CompiledCollider& col, int i,
                                                          const math::Mat3& Rbody, const math::Vec3& posBody) {
    const physics::SubShape& s = col.subShapes[i];
    math::Vec3 wpos = posBody + Rbody * s.localPos;
    switch (s.type) {
        case physics::SubShapeType::Sphere:
            return ShapeVariant{SphereGeom{wpos, s.extent.x}};
        case physics::SubShapeType::AABB:
        case physics::SubShapeType::OBB:
            return ShapeVariant{OBBGeom{wpos, s.extent, Rbody * s.localRot}};
        default:
            return std::nullopt;   // capsule/cylinder sub-shapes: no analytical pair yet
    }
}

// Descends `col`'s baked BVH (body-local frame defined by Rbody/posBody)
// against a world-space query AABB [wmn,wmx], invoking `fn(subIndex, subGeomWorld)`
// for every surviving leaf sub-shape with an analytical world geom. Shared by
// the compound-vs-single-shape and compound-vs-compound narrowphase paths.
template <class Fn>
static void descendCompoundBvh(const physics::CompiledCollider& col,
                               const math::Mat3& Rbody, const math::Vec3& posBody,
                               const math::Vec3& wmn, const math::Vec3& wmx, Fn&& fn) {
    // Query AABB (world) -> compound-local frame via the 8 corners.
    const math::Mat3 RbodyT = Rbody.transpose();
    constexpr float kInf = std::numeric_limits<float>::max();
    math::Vec3 qmn{kInf, kInf, kInf}, qmx{-kInf, -kInf, -kInf};
    for (int c = 0; c < 8; ++c) {
        math::Vec3 corner{ (c & 1) ? wmx.x : wmn.x, (c & 2) ? wmx.y : wmn.y, (c & 4) ? wmx.z : wmn.z };
        math::Vec3 local = RbodyT * (corner - posBody);
        qmn.x = std::min(qmn.x, local.x); qmn.y = std::min(qmn.y, local.y); qmn.z = std::min(qmn.z, local.z);
        qmx.x = std::max(qmx.x, local.x); qmx.y = std::max(qmx.y, local.y); qmx.z = std::max(qmx.z, local.z);
    }
    auto overlaps = [&](const BvhNode& n) {
        return !(qmn.x > n.aabbMax.x || qmx.x < n.aabbMin.x ||
                 qmn.y > n.aabbMax.y || qmx.y < n.aabbMin.y ||
                 qmn.z > n.aabbMax.z || qmx.z < n.aabbMin.z);
    };

    int32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const BvhNode& n = col.nodes[stack[--sp]];
        if (!overlaps(n)) continue;
        if (n.count > 0) {
            for (int i = n.first; i < n.first + n.count; ++i) {
                auto subGeom = buildSubShapeWorldGeom(col, i, Rbody, posBody);
                if (subGeom) fn(i, *subGeom);
            }
        } else {
            if (n.right >= 0 && sp < 64) stack[sp++] = n.right;
            if (n.left  >= 0 && sp < 64) stack[sp++] = n.left;
        }
    }
}

// Conservative world-space AABB of an entire compound body (root BVH bounds,
// rotation-fattened — same trick as worldAABBofGeom's OBB branch).
static void worldAABBofCompound(const physics::CompiledCollider& col,
                                const math::Mat3& Rbody, const math::Vec3& posBody,
                                math::Vec3& wmn, math::Vec3& wmx) {
    math::Vec3 center = (col.localMin + col.localMax) * 0.5f;
    math::Vec3 half   = (col.localMax - col.localMin) * 0.5f;
    math::Vec3 fat{
        std::fabs(Rbody.m[0]) * half.x + std::fabs(Rbody.m[3]) * half.y + std::fabs(Rbody.m[6]) * half.z,
        std::fabs(Rbody.m[1]) * half.x + std::fabs(Rbody.m[4]) * half.y + std::fabs(Rbody.m[7]) * half.z,
        std::fabs(Rbody.m[2]) * half.x + std::fabs(Rbody.m[5]) * half.y + std::fabs(Rbody.m[8]) * half.z,
    };
    math::Vec3 worldCenter = posBody + Rbody * center;
    wmn = worldCenter - fat;
    wmx = worldCenter + fat;
}

// ============================================================================
// detectCompound() — narrowphase for a compound body vs a single-shape body.
// Descends the compound's baked BVH with the other body's query AABB (in
// compound-local space), then runs the analytical pair test per surviving
// sub-shape. Pushes one manifold per colliding sub-shape, tagged with the
// sub-shape index as shapeKey so warm-start caching stays per-sub-shape.
// Contacts are always pushed as (a = compound, b = other); normal points
// compound -> other (the a->b convention solveIsland expects).
// NOTE: assumes the compound body's Transform has unit scale (levels bake scale
// into sub-shapes); body rotation/translation are honored.
// ============================================================================
static void detectCompound(ecs::Entity compoundEnt, ecs::Entity other,
                           ecs::Registry& reg, std::vector<ActiveContact>& contacts)
{
    auto* cc = reg.get<ecs::CompoundCollider>(compoundEnt);
    if (!cc || !cc->compiled || cc->compiled->nodes.empty()) return;
    const physics::CompiledCollider& col = *cc->compiled;

    auto* tfBody = reg.get<Transform>(compoundEnt);
    if (!tfBody) return;
    const math::Mat3 Rbody  = math::Mat3::rotation(tfBody->rotation);
    const math::Vec3 posBody = tfBody->position;

    auto otherGeomOpt = buildEntityWorldGeom(other, reg);
    if (!otherGeomOpt) return;
    const ShapeVariant& otherGeom = *otherGeomOpt;

    math::Vec3 wmn, wmx;
    worldAABBofGeom(otherGeom, wmn, wmx);

    descendCompoundBvh(col, Rbody, posBody, wmn, wmx, [&](int i, const ShapeVariant& subGeom) {
        ContactManifold m;
        if (detectGeomPair(subGeom, otherGeom, m)) {
            ActiveContact ac; ac.a = compoundEnt; ac.b = other; ac.shapeKey = i; ac.manifold = m;
            contacts.push_back(ac);
        }
    });
}

// ============================================================================
// detectCompoundCompound() — narrowphase for compound-vs-compound (e.g. a
// dynamic multi-part prop landing on a static compound level). For each
// sub-shape of A, cheap-reject its world AABB against B's overall world AABB,
// then descend B's BVH with that sub-shape's AABB (same machinery as
// detectCompound, just invoked once per A sub-shape instead of once for a
// single external shape). Contacts are pushed as (a, b) with normal a -> b;
// shapeKey combines both sub-shape indices (subA * subCountB + subB) so
// warm-start caching stays per-sub-shape-pair.
// ============================================================================
static void detectCompoundCompound(ecs::Entity a, ecs::Entity b,
                                   ecs::Registry& reg, std::vector<ActiveContact>& contacts)
{
    auto* ccA = reg.get<ecs::CompoundCollider>(a);
    auto* ccB = reg.get<ecs::CompoundCollider>(b);
    if (!ccA || !ccA->compiled || ccA->compiled->nodes.empty()) return;
    if (!ccB || !ccB->compiled || ccB->compiled->nodes.empty()) return;
    const physics::CompiledCollider& colA = *ccA->compiled;
    const physics::CompiledCollider& colB = *ccB->compiled;

    auto* tfA = reg.get<Transform>(a);
    auto* tfB = reg.get<Transform>(b);
    if (!tfA || !tfB) return;
    const math::Mat3 RbodyA = math::Mat3::rotation(tfA->rotation);
    const math::Vec3 posA   = tfA->position;
    const math::Mat3 RbodyB = math::Mat3::rotation(tfB->rotation);
    const math::Vec3 posB   = tfB->position;

    math::Vec3 wBmn, wBmx;
    worldAABBofCompound(colB, RbodyB, posB, wBmn, wBmx);
    auto overlapsB = [&](const math::Vec3& mn, const math::Vec3& mx) {
        return !(mn.x > wBmx.x || mx.x < wBmn.x ||
                 mn.y > wBmx.y || mx.y < wBmn.y ||
                 mn.z > wBmx.z || mx.z < wBmn.z);
    };

    const int subCountB = static_cast<int>(colB.subShapes.size());
    for (int iA = 0; iA < static_cast<int>(colA.subShapes.size()); ++iA) {
        auto subAGeomOpt = buildSubShapeWorldGeom(colA, iA, RbodyA, posA);
        if (!subAGeomOpt) continue;
        const ShapeVariant& subAGeom = *subAGeomOpt;

        math::Vec3 amn, amx;
        worldAABBofGeom(subAGeom, amn, amx);
        if (!overlapsB(amn, amx)) continue;

        descendCompoundBvh(colB, RbodyB, posB, amn, amx, [&](int iB, const ShapeVariant& subBGeom) {
            ContactManifold m;
            if (detectGeomPair(subAGeom, subBGeom, m)) {
                ActiveContact ac; ac.a = a; ac.b = b;
                ac.shapeKey = iA * subCountB + iB;
                ac.manifold = m;
                contacts.push_back(ac);
            }
        });
    }
}

// ============================================================================
// detect() — ECS-based type dispatch. Reads positions and shapes from Registry.
// Normal convention: from a toward b.
// ============================================================================

void detect(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
            std::vector<ActiveContact>& contacts)
{
    auto* hca = reg.get<ecs::Hull>(ea);
    auto* hcb = reg.get<ecs::Hull>(eb);
    if (!hca || !hcb) return;
    if (!hca->tangible || !hcb->tangible) return;

    bool aFixed = reg.has<ecs::Fixed>(ea);
    bool bFixed = reg.has<ecs::Fixed>(eb);
    if (aFixed && bFixed) return;
    if (!(hca->collisionLayer & hcb->collisionMask) || !(hcb->collisionLayer & hca->collisionMask)) return;

    // Compound colliders (baked multi-shape bodies) take a separate BVH-driven path.
    bool aCompound = reg.has<ecs::CompoundCollider>(ea);
    bool bCompound = reg.has<ecs::CompoundCollider>(eb);
    if (aCompound && bCompound)  { detectCompoundCompound(ea, eb, reg, contacts); return; }
    if (aCompound && !bCompound) { detectCompound(ea, eb, reg, contacts); return; }
    if (bCompound && !aCompound) { detectCompound(eb, ea, reg, contacts); return; }

    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return;

    auto* sa = reg.get<ecs::SphereForm>(ea);
    auto* sb = reg.get<ecs::SphereForm>(eb);
    auto* aa = reg.get<ecs::AABBForm>(ea);
    auto* ab = reg.get<ecs::AABBForm>(eb);
    auto* ca = reg.get<ecs::OBBForm>(ea);
    auto* cb = reg.get<ecs::OBBForm>(eb);

    auto mSph  = [&](ecs::Entity e, float r) -> SphereGeom {
        return {reg.get<Transform>(e)->position, r};
    };
    auto mAABB = [&](ecs::Entity e, math::Vec3 ext) -> AABBGeom {
        return {reg.get<Transform>(e)->position, ext};
    };
    auto mOBB  = [&](ecs::Entity e, math::Vec3 ext) -> OBBGeom {
        auto* tf = reg.get<Transform>(e);
        return {tf->position, ext, math::Mat3::rotation(tf->rotation)};
    };

    ContactManifold m;
    auto push = [&](ecs::Entity pa, ecs::Entity pb, ContactManifold& cm) {
        ActiveContact c; c.a = pa; c.b = pb; c.manifold = cm;
        contacts.push_back(c);
    };

    if (sa && sb) { NPHASE_TIME(NP_SPH_SPH);   if (analyticalSphereSphere(mSph(ea,sa->radius), mSph(eb,sb->radius), m))    push(ea,eb,m); return; }
    if (sa && ab) { NPHASE_TIME(NP_SPH_AABB);  if (analyticalSphereAABB(mSph(ea,sa->radius), mAABB(eb,ab->extent), m))   { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (aa && sb) { NPHASE_TIME(NP_SPH_AABB);  if (analyticalSphereAABB(mSph(eb,sb->radius), mAABB(ea,aa->extent), m))     push(ea,eb,m); return; }
    if (aa && ab) { NPHASE_TIME(NP_AABB_AABB); if (analyticalAABBAABB(mAABB(ea,aa->extent), mAABB(eb,ab->extent), m))      push(ea,eb,m); return; }
    if (sa && cb) { NPHASE_TIME(NP_SPH_OBB);   if (analyticalSphereOBB(mSph(ea,sa->radius), mOBB(eb,cb->extent), m))    { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && sb) { NPHASE_TIME(NP_SPH_OBB);   if (analyticalSphereOBB(mSph(eb,sb->radius), mOBB(ea,ca->extent), m))      push(ea,eb,m); return; }
    if (aa && cb) { NPHASE_TIME(NP_AABB_OBB);  if (analyticalAABBOBB(mAABB(ea,aa->extent), mOBB(eb,cb->extent), m))       push(ea,eb,m); return; }
    if (ca && ab) { NPHASE_TIME(NP_AABB_OBB);  if (analyticalAABBOBB(mAABB(eb,ab->extent), mOBB(ea,ca->extent), m))     { m.normal=-m.normal; push(ea,eb,m); } return; }
    if (ca && cb) { NPHASE_TIME(NP_OBB_OBB);   if (analyticalOBBOBB(mOBB(ea,ca->extent), mOBB(eb,cb->extent), m))         push(ea,eb,m); return; }
}

} // namespace physics::ColliderDiscrete
