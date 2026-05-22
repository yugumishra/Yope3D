#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../src/physics/Raycast.h"
#include "../src/physics/BroadphaseSAP.h"
#include "../src/physics/CSphere.h"
#include "../src/physics/CAABB.h"
#include "../src/physics/COBB.h"
#include "../src/physics/Spring.h"
#include "../src/physics/ColliderDiscrete.h"
#include "../src/physics/ContactCache.h"
#include "../src/physics/PhysicsConstants.h"
#include "../src/math/Vec3.h"
#include "../src/math/Math.h"
#include <limits>
#include <cmath>
#include <vector>

using namespace math;
using namespace Catch::Matchers;

// ============================================================================
// Regression: objects must rest on a large fixed AABB floor regardless of how
// far from the floor's centre they land.
//
// A fixed body must report ZERO inverse inertia. When it accidentally reported
// the identity matrix (math::Mat3's default ctor), an off-centre contact's
// moment arm rA inflated the solver's effective-mass denominator (angA·n grows
// with |rA|^2), collapsing the contact impulse so objects sank/fell through the
// floor away from its centre. Drives detect()+solveAll() exactly like
// World::advance.
// ============================================================================
TEST_CASE("Objects rest on large AABB floor far from centre", "[discrete][offcenter]") {
    using namespace physics;
    const float dt = 1.0f / 60.0f;
    const Vec3  g  = {0.0f, GRAVITY_Y, 0.0f};

    // Floor top face at y = 0.5; drop each shape (half-extent / radius 0.5) far off-centre.
    auto restY = [&](Hull& shape) {
        CAABB floor({0,0,0}, {50.0f, 0.5f, 50.0f}); // (pos,ext) ctor => fixed
        ContactCache cache;
        for (int frame = 0; frame < 400; frame++) {
            std::vector<ColliderDiscrete::ActiveContact> contacts;
            ColliderDiscrete::detect(floor, shape, contacts); // floor = a, shape = b
            ColliderDiscrete::solveAll(contacts, dt, cache);
            shape.advance(dt, g);
            floor.advance(dt, g);
            Vec3 v = shape.getVelocity(), w = shape.getOmega();
            shape.tickSleep(v.dot(v), w.dot(w));
        }
        return shape.getPosition().y;
    };

    SECTION("sphere") {
        CSphere s(1.0f, 0.5f, {40.0f, 5.0f, 0.0f});
        CHECK_THAT(restY(s), WithinAbs(1.0f, 0.1f)); // rests with centre ~radius above top
    }
    SECTION("AABB") {
        CAABB b({0.5f, 0.5f, 0.5f}, 1.0f, {40.0f, 5.0f, 0.0f});
        CHECK_THAT(restY(b), WithinAbs(1.0f, 0.1f));
    }
    SECTION("OBB") {
        COBB o({0.5f, 0.5f, 0.5f}, 1.0f, {40.0f, 5.0f, 0.0f});
        CHECK_THAT(restY(o), WithinAbs(1.0f, 0.15f));
    }
}

// Guard the underlying invariant directly: a fixed body must report a ZERO inverse
// inertia tensor, both world-space and the cached local tensor (math::Mat3's default
// ctor is the identity, which is the trap this guards against).
TEST_CASE("Fixed hull has zero inverse inertia tensor", "[hull][fixed]") {
    // World-space tensor (what the solver uses) must be zero for every fixed shape.
    physics::CAABB fixedAABB({0,0,0}, {50.0f, 0.5f, 50.0f}); // (pos,ext) ctor => fixed
    physics::COBB  obb({0.5f, 0.5f, 0.5f}, 1.0f, {0, 3, 0});
    obb.fix();
    for (physics::Hull* h : {static_cast<physics::Hull*>(&fixedAABB),
                             static_cast<physics::Hull*>(&obb)}) {
        Mat3 world = h->getInverseInertiaTensorWorld();
        for (int i = 0; i < 9; i++) CHECK(world.m[i] == 0.0f);
    }

    // Cached local tensor built while fixed must also be zero (guards genInverseInertiaTensor's
    // `return {}` — which is the identity, not zero). The fixed CAABB ctor fixes before
    // computing the cache, so its local tensor exercises that path directly.
    Mat3 local = fixedAABB.getInverseInertiaTensor();
    for (int i = 0; i < 9; i++) CHECK(local.m[i] == 0.0f);
}

// ============================================================================
// Raycast — Sphere
// ============================================================================

TEST_CASE("Raycast sphere direct hit", "[raycast][sphere]") {
    // Ray along +x toward sphere at (5,0,0) r=1
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {5,0,0}, 1.0f);
    CHECK_THAT(t, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("Raycast sphere miss", "[raycast][sphere]") {
    // Ray along +x, sphere at (5,3,0) — too far in y
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {5,3,0}, 1.0f);
    CHECK(t == -1.0f);
}

TEST_CASE("Raycast sphere tangent", "[raycast][sphere]") {
    // Ray just grazes sphere: center at (5,1,0) r=1, ray along +x at y=0 grazes bottom
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {5,1,0}, 1.0f);
    // det = 0 → t should be approximately 5.0 (tangent point)
    CHECK_THAT(t, WithinAbs(5.0f, 0.01f));
}

TEST_CASE("Raycast sphere from inside returns negative t", "[raycast][sphere]") {
    // Origin inside sphere at (0,0,0) r=2 — should return negative t for back face
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {0,0,0}, 2.0f);
    CHECK(t < 0.0f); // t1 is negative when origin is inside
}

// ============================================================================
// Raycast — AABB
// ============================================================================

TEST_CASE("Raycast AABB hit +x face", "[raycast][aabb]") {
    // Ray along +x, box centred at (5,0,0) half-extents (1,1,1)
    float k = physics::Raycast::raycastAABB({1,0,0}, {0,0,0}, {5,0,0}, {1,1,1});
    CHECK_THAT(k, WithinAbs(4.0f, 0.001f)); // hits x=4 face
}

TEST_CASE("Raycast AABB hit -y face", "[raycast][aabb]") {
    // Ray along +y, box centred at (0,5,0)
    float k = physics::Raycast::raycastAABB({0,1,0}, {0,0,0}, {0,5,0}, {1,1,1});
    CHECK_THAT(k, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("Raycast AABB hit +z face", "[raycast][aabb]") {
    float k = physics::Raycast::raycastAABB({0,0,1}, {0,0,0}, {0,0,5}, {1,1,1});
    CHECK_THAT(k, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("Raycast AABB miss", "[raycast][aabb]") {
    constexpr float MISS = std::numeric_limits<float>::min();
    float k = physics::Raycast::raycastAABB({1,0,0}, {0,5,0}, {5,0,0}, {1,1,1});
    // y=5 is outside the box's y range [-1,1], so should miss
    CHECK(k == MISS);
}

TEST_CASE("Raycast AABB corner approach", "[raycast][aabb]") {
    constexpr float MISS = std::numeric_limits<float>::min();
    // Diagonal ray toward corner of a unit box at origin
    Vec3 dir = Vec3{1,1,1}.normalize();
    float k = physics::Raycast::raycastAABB(dir, {-5,-5,-5}, {0,0,0}, {1,1,1});
    CHECK(k != MISS);
    CHECK(k > 0.0f);
}

// ============================================================================
// SAP broadphase
// ============================================================================

TEST_CASE("SAP: overlapping spheres produce a pair", "[sap]") {
    auto h0 = std::make_unique<physics::CSphere>(1.0f, 1.0f, math::Vec3{0,0,0});
    auto h1 = std::make_unique<physics::CSphere>(1.0f, 1.0f, math::Vec3{1,0,0});
    std::vector<physics::Hull*> hulls = { h0.get(), h1.get() };

    physics::BroadphaseSAP sap;
    std::vector<std::pair<physics::Hull*, physics::Hull*>> pairs;
    sap.collectPairs(hulls, pairs);

    bool found = false;
    for (auto& [a, b] : pairs)
        if ((a == h0.get() && b == h1.get()) ||
            (a == h1.get() && b == h0.get()))
            found = true;
    CHECK(found);
}

TEST_CASE("SAP: distant hulls produce no pair", "[sap]") {
    auto h0 = std::make_unique<physics::CSphere>(1.0f, 0.5f, math::Vec3{  0,0,0});
    auto h1 = std::make_unique<physics::CSphere>(1.0f, 0.5f, math::Vec3{100,0,0});
    std::vector<physics::Hull*> hulls = { h0.get(), h1.get() };

    physics::BroadphaseSAP sap;
    std::vector<std::pair<physics::Hull*, physics::Hull*>> pairs;
    sap.collectPairs(hulls, pairs);

    CHECK(pairs.empty());
}

TEST_CASE("SAP: nearby pair found, distant pair excluded", "[sap]") {
    auto u0 = std::make_unique<physics::CSphere>(1.0f, 0.5f, math::Vec3{ 0, 0, 0});
    auto u1 = std::make_unique<physics::CSphere>(1.0f, 0.5f, math::Vec3{ 0.5f, 0, 0});
    auto u2 = std::make_unique<physics::CSphere>(1.0f, 0.5f, math::Vec3{50, 0, 0});
    std::vector<physics::Hull*> hulls = { u0.get(), u1.get(), u2.get() };

    physics::BroadphaseSAP sap;
    std::vector<std::pair<physics::Hull*, physics::Hull*>> pairs;
    sap.collectPairs(hulls, pairs);

    bool nearFound = false, farFound = false;
    physics::Hull* h0 = u0.get();
    physics::Hull* h1 = u1.get();
    physics::Hull* h2 = u2.get();
    for (auto& [a, b] : pairs) {
        if ((a == h0 || a == h1) && (b == h0 || b == h1)) nearFound = true;
        if (a == h2 || b == h2) farFound = true;
    }
    CHECK(nearFound);
    CHECK_FALSE(farFound);
}

// ============================================================================
// Integration — gravity
// ============================================================================

TEST_CASE("CSphere falls under gravity for 1 second at 60 fps", "[integration][gravity]") {
    physics::CSphere sphere(1.0f, 0.5f, {0, 10, 0});
    Vec3 g{0, -9.80665f, 0};
    float dt = 1.0f / 60.0f;

    for (int i = 0; i < 60; ++i)
        sphere.advance(dt, g);

    float y = sphere.getPosition().y;
    // Semi-implicit Euler after 1s: y ≈ 10 - 4.98 ≈ 5.02
    CHECK(y > 4.5f);
    CHECK(y < 5.5f);
}

TEST_CASE("CSphere without gravity flag does not fall", "[integration][gravity]") {
    physics::CSphere sphere(1.0f, 0.5f, {0, 5, 0});
    sphere.disableGravity();
    Vec3 g{0, -9.80665f, 0};
    float dt = 1.0f / 60.0f;

    for (int i = 0; i < 60; ++i)
        sphere.advance(dt, g);

    CHECK_THAT(sphere.getPosition().y, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("Fixed CSphere does not move under gravity", "[integration][fixed]") {
    physics::CSphere sphere(1.0f, 0.5f, {0, 5, 0});
    sphere.fix();
    Vec3 g{0, -9.80665f, 0};
    float dt = 1.0f / 60.0f;

    for (int i = 0; i < 60; ++i)
        sphere.advance(dt, g);

    CHECK_THAT(sphere.getPosition().y, WithinAbs(5.0f, 0.001f));
}

// ============================================================================
// Integration — CAABB angular invariants
// ============================================================================

TEST_CASE("CAABB getOmega always returns zero", "[integration][caabb]") {
    physics::CAABB box({1,1,1}, 2.0f, {0,5,0});
    Vec3 g{0, -9.80665f, 0};

    box.addAngularImpulse({100, 100, 100});
    box.applyImpulses();

    for (int i = 0; i < 10; ++i)
        box.advance(1.0f / 60.0f, g);

    Vec3 omega = box.getOmega();
    CHECK_THAT(omega.x, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(omega.y, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(omega.z, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("CAABB rotation stays identity after angular impulse", "[integration][caabb]") {
    physics::CAABB box({1,1,1}, 2.0f, {0,5,0});
    box.addAngularImpulse({50, 50, 50});
    box.applyImpulses();

    math::Quat rot = box.getRotation();
    CHECK_THAT(rot.x, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(rot.y, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(rot.z, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(rot.w, WithinAbs(1.0f, 1e-6f));
}

// ============================================================================
// Integration — Spring
// ============================================================================

TEST_CASE("Spring pulls two spheres toward rest length", "[integration][spring]") {
    // Two spheres 6 units apart; rest length = 2; spring k = 10.
    // They should move toward each other.
    physics::CSphere a(1.0f, 0.5f, {-3, 0, 0});
    physics::CSphere b(1.0f, 0.5f, { 3, 0, 0});
    a.disableGravity();
    b.disableGravity();

    physics::Spring spring(&a, &b, 10.0f, 2.0f);

    float dt = 1.0f / 60.0f;
    Vec3 g{0, 0, 0};
    for (int i = 0; i < 120; ++i) {
        spring.update(dt);
        a.advance(dt, g);
        b.advance(dt, g);
    }

    // After 2 seconds the separation should have decreased from 6 toward 2
    float sep = (b.getPosition() - a.getPosition()).length();
    CHECK(sep < 6.0f); // moved at all
    CHECK(sep > 0.1f); // haven't passed through each other
}

// ============================================================================
// Hull impulse accumulation
// ============================================================================

TEST_CASE("Linear impulse changes velocity", "[hull][impulse]") {
    physics::CSphere s(2.0f, 0.5f, {0,0,0});
    s.addImpulse({10, 0, 0});
    s.applyImpulses();

    // v = impulse / mass = 10 / 2 = 5
    CHECK_THAT(s.getVelocity().x, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("Angular impulse gives sphere rotation", "[hull][impulse]") {
    physics::CSphere s(1.0f, 1.0f, {0,0,0});
    // Apply angular impulse along z-axis
    s.addAngularImpulse({0, 0, 5.0f});
    s.applyImpulses();

    Vec3 omega = s.getOmega();
    CHECK(omega.z != 0.0f);
}

TEST_CASE("Fixed hull ignores impulses", "[hull][impulse]") {
    physics::CSphere s(1.0f, 0.5f, {0,0,0});
    s.fix();
    s.addImpulse({100, 0, 0});
    s.applyImpulses();
    CHECK_THAT(s.getVelocity().x, WithinAbs(0.0f, 1e-6f));
}
