#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../src/physics/Raycast.h"
#include "../src/physics/BroadphaseSAP.h"
#include "../src/physics/ColliderDiscrete.h"
#include "../src/physics/PhysicsConstants.h"
#include "../src/ecs/Registry.h"
#include "../src/ecs/Components.h"
#include "../src/world/Transform.h"
#include "../src/math/Vec3.h"
#include "../src/math/Math.h"
#include <limits>
#include <cmath>
#include <vector>

using namespace math;
using namespace Catch::Matchers;

// ============================================================================
// Raycast — Sphere
// ============================================================================

TEST_CASE("Raycast sphere direct hit", "[raycast][sphere]") {
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {5,0,0}, 1.0f);
    CHECK_THAT(t, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("Raycast sphere miss", "[raycast][sphere]") {
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {5,3,0}, 1.0f);
    CHECK(t == -1.0f);
}

TEST_CASE("Raycast sphere tangent", "[raycast][sphere]") {
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {5,1,0}, 1.0f);
    CHECK_THAT(t, WithinAbs(5.0f, 0.01f));
}

TEST_CASE("Raycast sphere from inside returns negative t", "[raycast][sphere]") {
    float t = physics::Raycast::raycastSphere({1,0,0}, {0,0,0}, {0,0,0}, 2.0f);
    CHECK(t < 0.0f);
}

// ============================================================================
// Raycast — AABB
// ============================================================================

TEST_CASE("Raycast AABB hit +x face", "[raycast][aabb]") {
    float k = physics::Raycast::raycastAABB({1,0,0}, {0,0,0}, {5,0,0}, {1,1,1});
    CHECK_THAT(k, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("Raycast AABB hit -y face", "[raycast][aabb]") {
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
    CHECK(k == MISS);
}

TEST_CASE("Raycast AABB corner approach", "[raycast][aabb]") {
    constexpr float MISS = std::numeric_limits<float>::min();
    Vec3 dir = Vec3{1,1,1}.normalize();
    float k = physics::Raycast::raycastAABB(dir, {-5,-5,-5}, {0,0,0}, {1,1,1});
    CHECK(k != MISS);
    CHECK(k > 0.0f);
}

// ============================================================================
// ECS Hull — fixed entity has zero inertiaTensorWorld
// ============================================================================

TEST_CASE("Fixed ECS entity has zero inertiaTensorWorld", "[ecs][hull][fixed]") {
    ecs::Registry reg;
    ecs::Entity e = reg.create();

    ecs::Hull hc;
    hc.mass = 1.0f;
    hc.inverseMass = 1.0f;
    // Non-zero body-space inertia (sphere formula)
    float invI = 1.0f / (0.4f * 1.0f * 0.5f * 0.5f);
    hc.inverseInertia = math::Mat3::scale({invI, invI, invI});
    reg.add<ecs::Hull>(e, hc);
    reg.add<Transform>(e, Transform{{0,5,0}, {0,0,0,1}, {0.5f,0.5f,0.5f}});
    reg.add<ecs::Fixed>(e);

    // Fixed entities should have zero inertiaTensorWorld (set by advance pre-compute)
    if (auto* h = reg.get<ecs::Hull>(e)) {
        h->inertiaTensorWorld = math::Mat3::scale({0,0,0});  // advance() zeros this for Fixed entities
        for (int i = 0; i < 9; ++i)
            CHECK(h->inertiaTensorWorld.m[i] == 0.0f);
    }
}

// ============================================================================
// ECS Hull — AABB has zero inverseInertia (no rotation)
// ============================================================================

TEST_CASE("AABB entity has zero inverseInertia", "[ecs][hull][aabb]") {
    ecs::Registry reg;
    ecs::Entity e = reg.create();

    ecs::Hull hc;
    hc.mass = 2.0f;
    hc.inverseMass = 0.5f;
    hc.inverseInertia = math::Mat3::scale({0,0,0});  // AABB: no angular dynamics
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::AABBForm>(e, {{1,1,1}});

    auto* h = reg.get<ecs::Hull>(e);
    REQUIRE(h != nullptr);
    for (int i = 0; i < 9; ++i)
        CHECK(h->inverseInertia.m[i] == 0.0f);
}

// ============================================================================
// ECS SAP broadphase — Entity-based
// ============================================================================

static ecs::Entity makeSphereEntity(ecs::Registry& reg, math::Vec3 pos, float r, bool tangible = true) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {r,r,r}});
    ecs::Hull hc;
    hc.tangible = tangible;
    hc.mass = 1.0f;
    hc.inverseMass = 1.0f;
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::SphereForm>(e, {r});
    return e;
}

TEST_CASE("ECS SAP: overlapping spheres produce a pair", "[sap][ecs]") {
    ecs::Registry reg;
    ecs::Entity e0 = makeSphereEntity(reg, {0,0,0}, 1.0f);
    ecs::Entity e1 = makeSphereEntity(reg, {1,0,0}, 1.0f);
    std::vector<ecs::Entity> entities = {e0, e1};

    physics::BroadphaseSAP sap;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> pairs;
    sap.collectPairs(entities, reg, pairs);

    bool found = false;
    for (auto& [a, b] : pairs)
        if ((a == e0 && b == e1) || (a == e1 && b == e0))
            found = true;
    CHECK(found);
}

TEST_CASE("ECS SAP: distant entities produce no pair", "[sap][ecs]") {
    ecs::Registry reg;
    ecs::Entity e0 = makeSphereEntity(reg, {0,0,0}, 0.5f);
    ecs::Entity e1 = makeSphereEntity(reg, {100,0,0}, 0.5f);
    std::vector<ecs::Entity> entities = {e0, e1};

    physics::BroadphaseSAP sap;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> pairs;
    sap.collectPairs(entities, reg, pairs);
    CHECK(pairs.empty());
}

TEST_CASE("ECS SAP: nearby pair found, distant pair excluded", "[sap][ecs]") {
    ecs::Registry reg;
    ecs::Entity e0 = makeSphereEntity(reg, {0,0,0}, 0.5f);
    ecs::Entity e1 = makeSphereEntity(reg, {0.5f,0,0}, 0.5f);
    ecs::Entity e2 = makeSphereEntity(reg, {50,0,0}, 0.5f);
    std::vector<ecs::Entity> entities = {e0, e1, e2};

    physics::BroadphaseSAP sap;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> pairs;
    sap.collectPairs(entities, reg, pairs);

    bool nearFound = false, farFound = false;
    for (auto& [a, b] : pairs) {
        if ((a == e0 || a == e1) && (b == e0 || b == e1)) nearFound = true;
        if (a == e2 || b == e2) farFound = true;
    }
    CHECK(nearFound);
    CHECK_FALSE(farFound);
}
