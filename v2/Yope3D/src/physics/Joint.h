#pragma once
#include "../ecs/Entity.h"
#include "../math/Vec3.h"
#include "../math/Mat3.h"
#include <variant>
#include <utility>
#include <type_traits>

namespace physics {

// ============================================================================
// Bilateral (hard, can push AND pull) constraints, solved inside the same
// island-parallel PGS loop as contacts (ColliderDiscrete::solveIsland). Unlike
// ActiveContact — which is rebuilt fresh from geometry every substep and
// warm-starts via a hash-keyed ContactCache because manifold identity is
// ephemeral — a Joint is a persistent, authored object (World::joints_ owns it
// via unique_ptr for pointer stability, mirroring physics::Spring). Its warm
// start accumulator (lambda) therefore lives directly on the object and simply
// carries over frame to frame; no cache lookup is needed.
//
// One variant alternative per joint type (mirrors ShapeVariant/std::visit in
// ColliderAnalytical.cpp) rather than a flat ActiveContact-style struct: joint
// types don't share a common fixed shape the way contact manifolds do.
//
// Fields are split into persistent (authored: entities, local-space anchors/
// axes, limits, warm-start lambda) and ephemeral (recomputed every solveIsland
// call: world lever arms, effective mass/K).
// ============================================================================

// Point-to-point (ball socket): removes all 3 linear DOF between two anchor
// points, leaves all rotational DOF free. Bilateral, unclamped (lo=-inf,hi=+inf).
struct PointToPointJoint {
    ecs::Entity a = ecs::NullEntity, b = ecs::NullEntity;
    math::Vec3  localAnchorA{}, localAnchorB{};   // persistent: body-local offsets from each COM
    // Multiplies SPLIT_BETA for this joint's position-correction pass only —
    // 1.0 matches the contact solver's default pace. Anchors far apart at
    // creation time (e.g. the editor's default midpoint anchor) couple large
    // lever arms into the 3x3 K block via real inertia, which visibly slows
    // convergence; raise this to snap the gap closed faster at the cost of
    // more aggressive per-substep correction.
    float       correctionStiffness = 1.0f;

    // Ephemeral (recomputed each solveIsland call from current Transform/Hull):
    math::Vec3  rA{}, rB{};        // world lever arms
    math::Mat3  K{};               // 3x3 effective mass block (already inverted)

    // Persistent warm-start accumulator (survives across frames).
    math::Vec3  lambda{};
    // Split-impulse position-correction accumulator (pseudo-velocity only,
    // reset implicitly each solveIsland call the same way ActiveContact::lambdaP
    // is never warm-started — position error doesn't carry a "history" worth
    // reusing the way velocity impulses do).
    math::Vec3  lambdaP{};
};

// Hinge (revolute): PointToPointJoint's 3x3 point block plus 2 angular-lock
// rows (locks rotation to the shared axis) plus an optional angle limit. The
// limit is enforced at BOTH the velocity level (unilateral clamp, every one
// of the 24 velocity iterations — stops further rotation into the violation
// the instant it's detected) and the position level (split-impulse, pulls any
// remaining error back). Velocity-only would never fully close residual
// position error (it only zeroes the *rate*, same reason contacts need their
// own split-impulse pass on top of the velocity clamp); position-only (the
// original design) left nothing resisting a fast spin until *after* it had
// already blown through the limit, since the position pass only runs once
// per substep after integration — a hard hit could visibly overshoot for
// several substeps before the position pull-back won the tug-of-war.
struct HingeJoint {
    ecs::Entity a = ecs::NullEntity, b = ecs::NullEntity;
    math::Vec3  localAnchorA{}, localAnchorB{};
    math::Vec3  localAxisA{0, 0, 1}, localAxisB{0, 0, 1};   // hinge axis, each body's local frame
    bool        limitEnabled = false;
    float       lowerAngle = 0.0f, upperAngle = 0.0f;       // radians, about the hinge axis
    float       correctionStiffness = 1.0f;

    // Ephemeral:
    math::Vec3  rA{}, rB{};
    math::Mat3  K{};                    // point block
    math::Vec3  axisWorld{};            // world-space hinge axis (from body A)
    math::Vec3  perp1{}, perp2{};       // axes perpendicular to axisWorld, for the 2 angular-lock rows
    float       Wang1 = 0.0f, Wang2 = 0.0f;
    float       Wlimit = 0.0f;          // angular effective mass about axisWorld
    float       currentAngle = 0.0f;    // twist of B relative to A about axisWorld

    // Persistent warm-start accumulators:
    math::Vec3  lambda{};               // point block (velocity)
    math::Vec3  lambdaP{};              // point block (position, not warm-started across frames)
    float       lambdaAng1 = 0.0f, lambdaAng2 = 0.0f;
    float       lambdaLimit    = 0.0f;  // >=0, position-only (split-impulse)
    float       lambdaLimitVel = 0.0f;  // >=0, velocity-level clamp; not warm-started (see warmStartHinge)
};

// Cone-twist (swing-twist): PointToPointJoint's 3x3 point block plus a swing
// cone limit and a twist limit, each enforced at both the velocity and
// position level — same rationale as HingeJoint's limit above.
struct ConeTwistJoint {
    ecs::Entity a = ecs::NullEntity, b = ecs::NullEntity;
    math::Vec3  localAnchorA{}, localAnchorB{};
    math::Vec3  localTwistAxisA{0, 0, 1}, localTwistAxisB{0, 0, 1};   // each body's "bone" axis
    float       swingLimit = 0.785398f;   // half-angle, radians (~45 deg)
    float       twistLimit = 0.785398f;   // radians
    float       correctionStiffness = 1.0f;

    // Ephemeral:
    math::Vec3  rA{}, rB{};
    math::Mat3  K{};
    math::Vec3  twistAxisWorld{};   // world-space twist axis (from body A)
    math::Vec3  swingAxis{};   float swingAngle = 0.0f;   float Wswing = 0.0f;
    float       twistAngle = 0.0f;  float Wtwist = 0.0f;

    // Persistent warm-start accumulators:
    math::Vec3  lambda{};
    math::Vec3  lambdaP{};
    float       lambdaSwing    = 0.0f;   // >=0, position-only (split-impulse)
    float       lambdaTwistPos = 0.0f;   // >=0, position-only (twist > +twistLimit)
    float       lambdaTwistNeg = 0.0f;   // >=0, position-only (twist < -twistLimit)
    float       lambdaSwingVel    = 0.0f;   // >=0, velocity-level; not warm-started
    float       lambdaTwistPosVel = 0.0f;   // >=0, velocity-level; not warm-started
    float       lambdaTwistNegVel = 0.0f;   // >=0, velocity-level; not warm-started
};

// Suspension (raycast wheel, vehicle): single-body-vs-raycast — the "other
// side" is whatever the wheel's ground ray hits, treated as an implicit
// Fixed body (no Hull lookup, no reaction on it) exactly like contacts treat
// a Fixed entity. Not an iterative equality constraint: it's a direct spring/
// damper FORCE (same idiom as physics::Spring's one-shot write, computed
// once per substep — see warmStartSuspension), clamped to push-only, so it
// doesn't fit solveIsland's 24-iteration accumulate-clamp shape the way the
// two-body joints above do; solveVelocitySuspension is intentionally a no-op.
struct SuspensionJoint {
    ecs::Entity chassis = ecs::NullEntity;
    math::Vec3  localWheelPos{};        // wheel mount point, chassis-local
    math::Vec3  localUp{0, 1, 0};       // chassis-local "up" — ray casts along -worldUp
    float       restLength = 0.4f;
    float       maxTravel  = 0.3f;      // ray max distance = restLength + maxTravel
    float       stiffness  = 40000.0f;  // N/m
    float       damping    = 4000.0f;   // N*s/m

    // Ephemeral — refreshed once per substep by refreshSuspensionRaycast(),
    // BEFORE solveIsland's own precompute pass runs (precomputeSuspension
    // just consumes these, it doesn't cast the ray itself):
    bool        grounded  = false;
    math::Vec3  hitPoint{}, hitNormal{};
    float       currentLength = 0.0f;
    math::Vec3  worldUp{};       // world-space localUp, recomputed each substep
    math::Vec3  rChassis{};      // lever arm from chassis COM to hitPoint
    float       W = 0.0f;

    // Persistent: this substep's applied spring/damper impulse magnitude
    // along worldUp (>=0) — read by paired WheelFrictionJoints as their Fz
    // bound. Recomputed fresh every substep (see warmStartSuspension), not
    // warm-started across frames — the raycast can change what's grounding
    // the wheel frame to frame, so there's no stable "history" to reuse.
    float       lambda = 0.0f;
};

// WheelFriction (raycast wheel, vehicle): lateral (pure grip, target 0) and
// longitudinal (drive/brake, target = wheel surface speed) friction rows at
// the wheel contact, bounded by [-mu*Fz, +mu*Fz] where Fz is that same
// substep's paired SuspensionJoint::lambda — the identical lambda-coupling
// idiom contact friction already uses against the contact normal row
// (ColliderDiscrete.cpp's solveIsland, `cone = c.mu * c.lambda[i]`).
struct WheelFrictionJoint {
    ecs::Entity chassis = ecs::NullEntity;
    // Raw pointer into World::joints_'s stable unique_ptr-owned storage (see
    // Joint.h's file comment) — valid as long as the paired SuspensionJoint
    // isn't independently erased. World::addVehicle always creates/removes
    // a wheel's Suspension+WheelFriction pair together, so this never
    // dangles in practice.
    SuspensionJoint* suspension = nullptr;
    float       steerAngle     = 0.0f;   // radians, about localUp, chassis-local
    float       wheelAngularVel = 0.0f;  // rad/s — drives the longitudinal slip target
    float       radius = 0.35f;          // wheel radius, converts angular velocity to surface speed
    float       muLong = 1.2f, muLat = 1.2f;
    // When false, the longitudinal (drive/brake) row is skipped entirely and
    // the wheel free-rolls (lateral grip only) — a non-driven wheel on a real
    // drivetrain, instead of braking toward wheelAngularVel*radius. Default
    // true preserves the "all wheels drive" behavior and existing tests.
    bool        driven = true;

    // Ephemeral, derived from the paired suspension's already-refreshed
    // raycast (hitPoint/hitNormal/worldUp) each substep:
    math::Vec3  rChassis{};
    math::Vec3  longDir{}, latDir{};
    float       Wlong = 0.0f, Wlat = 0.0f;

    // Persistent warm-start accumulators (decayed, like contact friction).
    float       lambdaLong = 0.0f, lambdaLat = 0.0f;
};

// Returns the entity pair a joint spans, for IslandDetector union-find /
// island membership. Two-body joints return their two entities; the
// single-body vehicle joints (Suspension/WheelFriction) return
// {chassis, chassis} — a harmless self-pair that still lets IslandDetector::build
// seed/populate an island for them (see IslandDetector.cpp's union-find,
// which no-ops a self-pair union but still needs it for island membership).
using Joint = std::variant<PointToPointJoint, HingeJoint, ConeTwistJoint,
                            SuspensionJoint, WheelFrictionJoint>;

inline std::pair<ecs::Entity, ecs::Entity> jointEntityPair(const Joint& j) {
    return std::visit([](const auto& v) -> std::pair<ecs::Entity, ecs::Entity> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, SuspensionJoint> || std::is_same_v<T, WheelFrictionJoint>)
            return { v.chassis, v.chassis };
        else
            return { v.a, v.b };
    }, j);
}

} // namespace physics
