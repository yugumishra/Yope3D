#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../src/physics/Raycast.h"
#include "../src/physics/CollisionTree.h"
#include "../src/physics/CSphere.h"
#include "../src/physics/CAABB.h"
#include "../src/physics/COBB.h"
#include "../src/physics/Spring.h"
#include "../src/math/Vec3.h"
#include "../src/math/Math.h"
#include <limits>
#include <cmath>

using namespace math;
using namespace Catch::Matchers;

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
// Octree — 8-corner regression
// ============================================================================

TEST_CASE("Octree: sphere straddling centre is found from both sides", "[octree]") {
    // Tree spans [-10,-10,-10] to [10,10,10], depth 2.
    // Sphere centred exactly at origin (tree midpoint) with radius 1 should appear
    // in queries from any quadrant — the 6-probe bug would miss it for off-axis queries.
    physics::CollisionTree tree({-10,-10,-10}, {10,10,10}, 2);

    physics::CSphere center(1.0f, 1.0f, {0,0,0});
    tree.addObject(&center);

    // Query from a sphere in the +x+y+z octant
    physics::CSphere query(1.0f, 0.5f, {4,4,4});
    auto results = tree.getObjects(&query);
    bool found = false;
    for (auto* h : results) if (h == &center) { found = true; break; }
    CHECK(found);

    // Query from the opposite octant
    physics::CSphere query2(1.0f, 0.5f, {-4,-4,-4});
    auto results2 = tree.getObjects(&query2);
    found = false;
    for (auto* h : results2) if (h == &center) { found = true; break; }
    CHECK(found);
}

TEST_CASE("Octree: returns only nearby hulls, excludes distant ones", "[octree]") {
    physics::CollisionTree tree({-20,-20,-20}, {20,20,20}, 3);

    physics::CSphere near1(1.0f, 0.5f, {1,0,0});
    physics::CSphere near2(1.0f, 0.5f, {-1,0,0});
    physics::CSphere far1 (1.0f, 0.5f, {18,18,18});

    tree.addObject(&near1);
    tree.addObject(&near2);
    tree.addObject(&far1);

    // Query hull at origin — should find near1 and near2 but not far1
    physics::CSphere query(1.0f, 0.5f, {0,0,0});
    auto results = tree.getObjects(&query);

    bool foundNear1 = false, foundNear2 = false, foundFar = false;
    for (auto* h : results) {
        if (h == &near1) foundNear1 = true;
        if (h == &near2) foundNear2 = true;
        if (h == &far1)  foundFar  = true;
    }
    CHECK(foundNear1);
    CHECK(foundNear2);
    CHECK_FALSE(foundFar);
}

TEST_CASE("Octree: query hull does not appear in its own results", "[octree]") {
    physics::CollisionTree tree({-10,-10,-10}, {10,10,10}, 2);
    physics::CSphere s(1.0f, 1.0f, {0,0,0});
    tree.addObject(&s);

    auto results = tree.getObjects(&s);
    for (auto* h : results)
        CHECK(h != &s);
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
