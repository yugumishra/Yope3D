// ============================================================================
// Kinematic query + collision-filtering tests (headless).
//
// Covers the surface the Python stub groups under "Kinematic queries" —
// yope3d.raycast / capsule_overlap / capsule_cast — plus the collision-layer
// filtering that gates narrowphase. These back the stub's headline recipes:
// the crosshair raycast (shooting/picking), the kinematic capsule controller
// (_resolve / _grounded), and layer-scoped collision setups.
//
// Everything here is intentionally *behavioural*: each case pins a contract a
// behavior script can rely on, including the places where the current
// implementation deliberately (or incidentally) does nothing — those are
// marked DOCUMENTED GAP so a future change to them fails loudly rather than
// silently altering what scripts observe.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../src/physics/KinematicQuery.h"
#include "../src/physics/Raycast.h"
#include "../src/physics/CollisionLayers.h"
#include "../src/physics/ColliderDiscrete.h"
#include "../src/ecs/Registry.h"
#include "../src/ecs/Components.h"
#include "../src/world/Transform.h"
#include "../src/math/Vec3.h"
#include "../src/math/Mat3.h"
#include "../src/math/Math.h"
#include <cmath>
#include <vector>

using namespace math;
using namespace Catch::Matchers;
namespace KQ = physics::KinematicQuery;

// ============================================================================
// Fixtures
// ============================================================================

namespace {

Quat rotZ(float degrees) {
    const float h = 0.5f * toRadians(degrees);
    return Quat{0.0f, 0.0f, std::sin(h), std::cos(h)};
}

// Dynamic sphere body — tangible via Hull.
ecs::Entity makeSphere(ecs::Registry& reg, Vec3 pos, float r, bool tangible = true) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {r,r,r}});
    ecs::Hull hc;
    hc.tangible = tangible;
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::SphereForm>(e, {r});
    return e;
}

// Immovable axis-aligned box — the floor/wall of every controller test.
ecs::Entity makeStaticAABB(ecs::Registry& reg, Vec3 pos, Vec3 ext) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {1,1,1}});
    ecs::Hull hc;
    hc.mass = 0.0f; hc.inverseMass = 0.0f; hc.gravity = false;
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::AABBForm>(e, {ext});
    reg.add<ecs::Fixed>(e);
    return e;
}

ecs::Entity makeStaticOBB(ecs::Registry& reg, Vec3 pos, Vec3 ext, Quat rot) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, rot, {1,1,1}});
    ecs::Hull hc;
    hc.mass = 0.0f; hc.inverseMass = 0.0f; hc.gravity = false;
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::OBBForm>(e, {ext});
    reg.add<ecs::Fixed>(e);
    return e;
}

ecs::Entity makeCapsuleBody(ecs::Registry& reg, Vec3 pos, float r, float hh) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {1,1,1}});
    reg.add<ecs::Hull>(e, ecs::Hull{});
    reg.add<ecs::CapsuleForm>(e, {r, hh});
    return e;
}

ecs::Entity makeCylinderBody(ecs::Registry& reg, Vec3 pos, float r, float hh) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {1,1,1}});
    reg.add<ecs::Hull>(e, ecs::Hull{});
    reg.add<ecs::CylinderForm>(e, {r, hh});
    return e;
}

// A mesh-only visual entity: Transform + a shape Form but NO Hull and NO Fixed.
// This is what a template/prefab child looks like.
ecs::Entity makeUntangibleSphereNoHull(ecs::Registry& reg, Vec3 pos, float r) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {r,r,r}});
    reg.add<ecs::SphereForm>(e, {r});
    return e;
}

} // namespace

// ============================================================================
// Tangibility gate — shared by raycast / capsuleOverlap / capsuleCast
//
// isTangible(): Fixed tag => tangible; else Hull.tangible; else NOT tangible.
// The "else" is the subtle one: a Form with no Hull and no Fixed is invisible
// to every kinematic query.
// ============================================================================

TEST_CASE("Query gate: Form without Hull or Fixed is invisible to raycast", "[kq][tangible]") {
    ecs::Registry reg;
    makeUntangibleSphereNoHull(reg, {5,0,0}, 1.0f);

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    CHECK_FALSE(hit.hit);
    CHECK(hit.entity == ecs::NullEntity);
}

TEST_CASE("Query gate: Fixed tag alone makes an entity tangible (no Hull needed)", "[kq][tangible]") {
    ecs::Registry reg;
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{{5,0,0}, {0,0,0,1}, {1,1,1}});
    reg.add<ecs::SphereForm>(e, {1.0f});
    reg.add<ecs::Fixed>(e);   // tangible purely by tag

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    REQUIRE(hit.hit);
    CHECK(hit.entity == e);
}

TEST_CASE("Query gate: Hull.tangible=false hides a body from all three queries", "[kq][tangible]") {
    ecs::Registry reg;
    ecs::Entity ghost = makeSphere(reg, {0,0,0}, 1.0f, /*tangible=*/false);
    (void)ghost;

    // raycast straight at it
    auto hit = KQ::raycast({-5,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    CHECK_FALSE(hit.hit);

    // capsule sitting right on top of it
    auto overlaps = KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity);
    CHECK(overlaps.empty());

    // cast down through it
    auto cast = KQ::capsuleCast({0,5,0}, 0.5f, 1.0f, {0,-1,0}, 20.0f, reg, ecs::NullEntity);
    CHECK_FALSE(cast.hit);
}

TEST_CASE("Query gate: exclude skips exactly one entity", "[kq][tangible]") {
    ecs::Registry reg;
    ecs::Entity self  = makeSphere(reg, {3,0,0}, 1.0f);
    ecs::Entity other = makeSphere(reg, {8,0,0}, 1.0f);

    // Excluding the near body must fall through to the far one, not miss.
    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, self);
    REQUIRE(hit.hit);
    CHECK(hit.entity == other);
    CHECK_THAT(hit.t, WithinAbs(7.0f, 0.001f));
}

// ============================================================================
// KinematicQuery::raycast — the entity-returning ray (yope3d.raycast)
//
// The low-level Raycast::* primitives are covered in physics_tests.cpp; these
// cover the registry walk on top of them: entity identity, nearest-wins,
// maxDist clamping, direction normalization, and per-shape coverage.
// ============================================================================

TEST_CASE("raycast reports entity, point, normal and t for a sphere hit", "[kq][raycast]") {
    ecs::Registry reg;
    ecs::Entity target = makeSphere(reg, {5,0,0}, 1.0f);

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    REQUIRE(hit.hit);
    CHECK(hit.entity == target);
    CHECK_THAT(hit.t, WithinAbs(4.0f, 0.001f));           // 5 - radius
    CHECK_THAT(hit.point.x, WithinAbs(4.0f, 0.001f));     // origin + dir*t
    CHECK_THAT(hit.point.y, WithinAbs(0.0f, 0.001f));
    CHECK_THAT(hit.normal.x, WithinAbs(-1.0f, 0.001f));   // faces the ray
    CHECK_THAT(hit.normal.y, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("raycast miss leaves entity null and hit false", "[kq][raycast]") {
    ecs::Registry reg;
    makeSphere(reg, {5,20,0}, 1.0f);   // well off the ray

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    CHECK_FALSE(hit.hit);
    CHECK(hit.entity == ecs::NullEntity);
}

TEST_CASE("raycast returns the nearest hit regardless of creation order", "[kq][raycast]") {
    // Create the FAR body first so "nearest wins" can't pass by accident on
    // iteration order.
    ecs::Registry reg;
    ecs::Entity far  = makeSphere(reg, {8,0,0}, 1.0f);
    ecs::Entity near = makeSphere(reg, {3,0,0}, 1.0f);
    (void)far;

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    REQUIRE(hit.hit);
    CHECK(hit.entity == near);
    CHECK_THAT(hit.t, WithinAbs(2.0f, 0.001f));
}

TEST_CASE("raycast honours maxDist: a body just beyond it is not hit", "[kq][raycast]") {
    ecs::Registry reg;
    makeSphere(reg, {5,0,0}, 1.0f);    // near surface at t = 4

    CHECK_FALSE(KQ::raycast({0,0,0}, {1,0,0}, 3.9f, reg, ecs::NullEntity).hit);
    CHECK      (KQ::raycast({0,0,0}, {1,0,0}, 4.1f, reg, ecs::NullEntity).hit);
}

TEST_CASE("raycast normalizes dir — t is in world meters either way", "[kq][raycast]") {
    ecs::Registry reg;
    makeSphere(reg, {5,0,0}, 1.0f);

    auto unit  = KQ::raycast({0,0,0}, {1,0,0},  50.0f, reg, ecs::NullEntity);
    auto scaled = KQ::raycast({0,0,0}, {7,0,0}, 50.0f, reg, ecs::NullEntity);
    REQUIRE(unit.hit);
    REQUIRE(scaled.hit);
    CHECK_THAT(scaled.t, WithinAbs(unit.t, 0.001f));
    CHECK_THAT(scaled.t, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("raycast with a degenerate zero-length dir reports no hit", "[kq][raycast]") {
    ecs::Registry reg;
    makeSphere(reg, {5,0,0}, 1.0f);

    auto hit = KQ::raycast({0,0,0}, {0,0,0}, 50.0f, reg, ecs::NullEntity);
    CHECK_FALSE(hit.hit);
    CHECK(hit.entity == ecs::NullEntity);
}

TEST_CASE("raycast hits a rotated OBB and returns the rotated face normal", "[kq][raycast][obb]") {
    // Unit cube at (5,0,0) rotated 30 deg about Z. A +X ray along y=0 enters
    // through the local -X face; hand-solved slab entry is t = 3.84530 and the
    // world normal is rot30 * (-1,0,0) = (-cos30, -sin30, 0).
    ecs::Registry reg;
    ecs::Entity box = makeStaticOBB(reg, {5,0,0}, {1,1,1}, rotZ(30.0f));

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    REQUIRE(hit.hit);
    CHECK(hit.entity == box);
    CHECK_THAT(hit.t, WithinAbs(3.84530f, 0.005f));
    CHECK_THAT(hit.normal.x, WithinAbs(-0.86603f, 0.005f));
    CHECK_THAT(hit.normal.y, WithinAbs(-0.50000f, 0.005f));
    CHECK_THAT(hit.normal.z, WithinAbs(0.0f, 0.005f));
    // normal must be unit length
    CHECK_THAT(hit.normal.length(), WithinAbs(1.0f, 0.005f));
}

TEST_CASE("raycast hits a capsule body through its Transform up-axis", "[kq][raycast][capsule]") {
    ecs::Registry reg;
    ecs::Entity cap = makeCapsuleBody(reg, {5,0,0}, 1.0f, 2.0f);

    // Horizontal ray at y=0 strikes the cylindrical section at x = 4.
    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    REQUIRE(hit.hit);
    CHECK(hit.entity == cap);
    CHECK_THAT(hit.t, WithinAbs(4.0f, 0.001f));
    CHECK_THAT(hit.normal.x, WithinAbs(-1.0f, 0.01f));
}

TEST_CASE("raycast picks the nearest across mixed shape types", "[kq][raycast]") {
    ecs::Registry reg;
    makeStaticAABB(reg, {12,0,0}, {1,1,1});          // surface at t = 11
    ecs::Entity mid = makeSphere(reg, {6,0,0}, 1.0f); // surface at t = 5
    makeStaticOBB(reg, {20,0,0}, {1,1,1}, rotZ(0));   // surface at t = 19

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    REQUIRE(hit.hit);
    CHECK(hit.entity == mid);
    CHECK_THAT(hit.t, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("raycast AABB face normals are correct on all six faces", "[kq][raycast][aabb]") {
    struct Case { Vec3 origin, dir, expectNormal; };
    const Case cases[] = {
        {{-10, 0,  0}, { 1, 0, 0}, {-1,  0,  0}},
        {{ 10, 0,  0}, {-1, 0, 0}, { 1,  0,  0}},
        {{  0,-10, 0}, { 0, 1, 0}, { 0, -1,  0}},
        {{  0, 10, 0}, { 0,-1, 0}, { 0,  1,  0}},
        {{  0, 0,-10}, { 0, 0, 1}, { 0,  0, -1}},
        {{  0, 0, 10}, { 0, 0,-1}, { 0,  0,  1}},
    };
    for (const auto& c : cases) {
        ecs::Registry reg;
        makeStaticAABB(reg, {0,0,0}, {1,1,1});
        auto hit = KQ::raycast(c.origin, c.dir, 50.0f, reg, ecs::NullEntity);
        REQUIRE(hit.hit);
        CHECK_THAT(hit.normal.x, WithinAbs(c.expectNormal.x, 0.001f));
        CHECK_THAT(hit.normal.y, WithinAbs(c.expectNormal.y, 0.001f));
        CHECK_THAT(hit.normal.z, WithinAbs(c.expectNormal.z, 0.001f));
        CHECK_THAT(hit.t, WithinAbs(9.0f, 0.001f));
    }
}

// --- Documented coverage gaps in raycast --------------------------------------

TEST_CASE("raycast does not test cylinder bodies (DOCUMENTED GAP)", "[kq][raycast][gap]") {
    // KinematicQuery.h: "Coverage: sphere / AABB / OBB / capsule bodies.
    // Cylinder obstacles are not yet ray-tested." The stub repeats this.
    ecs::Registry reg;
    ecs::Entity cyl = makeCylinderBody(reg, {5,0,0}, 1.0f, 2.0f);

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    CHECK_FALSE(hit.hit);

    // Positive control: the fixture is otherwise ray-visible — swapping the
    // cylinder for a capsule of identical dimensions at the same spot hits.
    // Proves the miss above is about the shape type, not a broken entity.
    reg.remove<ecs::CylinderForm>(cyl);
    reg.add<ecs::CapsuleForm>(cyl, {1.0f, 2.0f});
    CHECK(KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity).hit);
}

TEST_CASE("raycast does not test compound colliders (DOCUMENTED GAP)", "[kq][raycast][gap]") {
    // A baked static level collider is Transform + Hull + Fixed +
    // CompoundCollider with NO shape Form, and every kinematic query iterates
    // Form views only — so baked level geometry is invisible to raycast even
    // though dynamic rigid bodies DO collide with it in narrowphase.
    ecs::Registry reg;
    ecs::Entity level = reg.create();
    reg.add<Transform>(level, Transform{{5,0,0}, {0,0,0,1}, {1,1,1}});
    reg.add<ecs::Hull>(level, ecs::Hull{});
    reg.add<ecs::CompoundCollider>(level, ecs::CompoundCollider{});
    reg.add<ecs::Fixed>(level);

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    CHECK_FALSE(hit.hit);

    // Positive control: give the same entity a shape Form and it becomes
    // visible — the miss above is purely the absence of a Form, so any future
    // compound-aware raycast will break this CHECK_FALSE and demand attention.
    reg.add<ecs::AABBForm>(level, {{1,1,1}});
    CHECK(KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity).hit);
}

TEST_CASE("raycast ignores collision layers entirely (DOCUMENTED GAP)", "[kq][raycast][gap][layers]") {
    // Layer/mask filtering lives in narrowphase (ColliderDiscrete::detect), not
    // in KinematicQuery — so a ray hits bodies it could never physically touch.
    ecs::Registry reg;
    ecs::Entity e = makeSphere(reg, {5,0,0}, 1.0f);
    auto* h = reg.get<ecs::Hull>(e);
    h->collisionLayer = 0;     // belongs to no layer
    h->collisionMask  = 0;     // collides with nothing

    auto hit = KQ::raycast({0,0,0}, {1,0,0}, 50.0f, reg, ecs::NullEntity);
    CHECK(hit.hit);            // still hit
    CHECK(hit.entity == e);
}

// ============================================================================
// capsuleOverlap — the controller's penetration resolver (_resolve recipe)
// ============================================================================

TEST_CASE("capsuleOverlap: clear of the floor yields no contacts", "[kq][overlap]") {
    ecs::Registry reg;
    makeStaticAABB(reg, {0,-1,0}, {10,1,10});   // top surface at y = 0

    // r=0.5, hh=1.0, center y=2 => bottom surface at y = 0.5, a 0.5 m gap.
    auto res = KQ::capsuleOverlap({0,2,0}, 0.5f, 1.0f, reg, ecs::NullEntity);
    CHECK(res.empty());
}

TEST_CASE("capsuleOverlap: exact touch is not an overlap", "[kq][overlap]") {
    ecs::Registry reg;
    makeStaticAABB(reg, {0,-1,0}, {10,1,10});   // top at y = 0

    // bottom surface exactly at y = 0 => dist == r, and the test is dist < r.
    auto res = KQ::capsuleOverlap({0,1.5f,0}, 0.5f, 1.0f, reg, ecs::NullEntity);
    CHECK(res.empty());
}

TEST_CASE("capsuleOverlap: depth equals the sink distance and normal points up", "[kq][overlap]") {
    ecs::Registry reg;
    makeStaticAABB(reg, {0,-1,0}, {10,1,10});   // top at y = 0

    // center y=1.2, hh=1, r=0.5 => bottom surface at y = -0.3 (sunk 0.3).
    auto res = KQ::capsuleOverlap({0,1.2f,0}, 0.5f, 1.0f, reg, ecs::NullEntity);
    REQUIRE(res.size() == 1);
    CHECK_THAT(res[0].depth, WithinAbs(0.3f, 0.001f));
    CHECK_THAT(res[0].normal.x, WithinAbs(0.0f, 0.001f));
    CHECK_THAT(res[0].normal.y, WithinAbs(1.0f, 0.001f));
    CHECK_THAT(res[0].normal.z, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("capsuleOverlap: pushing along normal*depth resolves the overlap", "[kq][overlap][resolve]") {
    // This is the contract the stub's _resolve() recipe depends on: collect
    // contacts, push along each normal by its depth, and the capsule ends up
    // clear. Here the capsule is wedged into BOTH a floor and a wall.
    ecs::Registry reg;
    makeStaticAABB(reg, {0,-1,0}, {10,1,10});     // floor, top at y = 0
    makeStaticAABB(reg, {1.5f,1,0}, {0.5f,2,5});  // wall, near face at x = 1

    const float r = 0.5f, hh = 1.0f;
    Vec3 pos{0.8f, 1.2f, 0};                       // sunk 0.3 into both

    auto initial = KQ::capsuleOverlap(pos, r, hh, reg, ecs::NullEntity);
    REQUIRE(initial.size() == 2);

    for (int i = 0; i < 8; ++i) {
        auto contacts = KQ::capsuleOverlap(pos, r, hh, reg, ecs::NullEntity);
        if (contacts.empty()) break;
        for (const auto& c : contacts) pos = pos + c.normal * c.depth;
    }

    CHECK(KQ::capsuleOverlap(pos, r, hh, reg, ecs::NullEntity).empty());
    CHECK(pos.y > 1.2f);    // pushed up out of the floor
    CHECK(pos.x < 0.8f);    // pushed back out of the wall
}

TEST_CASE("capsuleOverlap: sphere obstacle pushes away from the sphere centre", "[kq][overlap][sphere]") {
    ecs::Registry reg;
    makeSphere(reg, {0,0,0}, 1.0f);

    // Capsule centre offset on +X, overlapping the sphere.
    const float r = 0.5f, hh = 1.0f;
    auto res = KQ::capsuleOverlap({1.2f, 0, 0}, r, hh, reg, ecs::NullEntity);
    REQUIRE(res.size() == 1);
    // Closest segment point is (1.2, 0, 0); surface gap = 1.2 - 1.0 = 0.2,
    // so depth = r - 0.2 = 0.3 and the push is along +X.
    CHECK_THAT(res[0].depth, WithinAbs(0.3f, 0.001f));
    CHECK_THAT(res[0].normal.x, WithinAbs(1.0f, 0.001f));
    CHECK_THAT(res[0].normal.length(), WithinAbs(1.0f, 0.001f));
}

TEST_CASE("capsuleOverlap: rotated OBB returns a world-space normal", "[kq][overlap][obb]") {
    ecs::Registry reg;
    makeStaticOBB(reg, {0,0,0}, {2,0.25f,2}, rotZ(30.0f));  // a tilted slab

    // Drop the capsule onto the slab so it penetrates.
    auto res = KQ::capsuleOverlap({0, 0.6f, 0}, 0.5f, 0.5f, reg, ecs::NullEntity);
    REQUIRE(res.size() == 1);
    CHECK(res[0].depth > 0.0f);
    CHECK_THAT(res[0].normal.length(), WithinAbs(1.0f, 0.01f));
    // Slab's local +Y rotated by 30 deg about Z => (-sin30, cos30, 0).
    CHECK_THAT(res[0].normal.x, WithinAbs(-0.5f, 0.02f));
    CHECK_THAT(res[0].normal.y, WithinAbs(0.86603f, 0.02f));
}

TEST_CASE("capsuleOverlap: axis fully inside an OBB uses the SAT branch", "[kq][overlap][obb][sat]") {
    // When the capsule axis passes through the box interior the closest-point
    // distance collapses to ~0, and the code switches to a SAT minimum-
    // penetration search. It must still produce a unit normal and a positive
    // depth rather than a degenerate zero vector.
    ecs::Registry reg;
    makeStaticOBB(reg, {0,0,0}, {3,3,3}, rotZ(20.0f));

    auto res = KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity);
    REQUIRE(res.size() == 1);
    CHECK(res[0].depth > 0.0f);
    CHECK_THAT(res[0].normal.length(), WithinAbs(1.0f, 0.01f));
}

TEST_CASE("capsuleOverlap: reports one contact per overlapping body", "[kq][overlap]") {
    ecs::Registry reg;
    makeSphere(reg, {-0.6f, 0, 0}, 0.5f);
    makeSphere(reg, { 0.6f, 0, 0}, 0.5f);
    makeSphere(reg, { 50.0f, 0, 0}, 0.5f);   // far away, must not appear

    auto res = KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity);
    CHECK(res.size() == 2);
}

TEST_CASE("capsuleOverlap: exclude removes the controller's own body", "[kq][overlap]") {
    ecs::Registry reg;
    ecs::Entity self = makeSphere(reg, {0,0,0}, 1.0f);

    CHECK(KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity).size() == 1);
    CHECK(KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, self).empty());
}

TEST_CASE("capsuleOverlap ignores capsule and cylinder obstacles (DOCUMENTED GAP)", "[kq][overlap][gap]") {
    // capsuleOverlap iterates AABBForm / OBBForm / SphereForm only. A capsule
    // controller therefore walks straight through capsule and cylinder bodies.
    ecs::Registry reg;
    makeCapsuleBody(reg, {0,0,0}, 1.0f, 1.0f);
    ecs::Entity cyl = makeCylinderBody(reg, {0,0,0}, 1.0f, 1.0f);

    CHECK(KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity).empty());

    // Positive control: a sphere of the same radius in the same place IS seen.
    reg.remove<ecs::CylinderForm>(cyl);
    reg.add<ecs::SphereForm>(cyl, {1.0f});
    CHECK(KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity).size() == 1);
}

TEST_CASE("capsuleOverlap ignores compound colliders (DOCUMENTED GAP)", "[kq][overlap][gap]") {
    // The stub sells "Generate Static Collider" as the thing that stops the
    // player walking through walls — but a kinematic capsule controller
    // resolving via capsuleOverlap cannot see a CompoundCollider at all.
    ecs::Registry reg;
    ecs::Entity level = reg.create();
    reg.add<Transform>(level, Transform{{0,0,0}, {0,0,0,1}, {1,1,1}});
    reg.add<ecs::Hull>(level, ecs::Hull{});
    reg.add<ecs::CompoundCollider>(level, ecs::CompoundCollider{});
    reg.add<ecs::Fixed>(level);

    CHECK(KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity).empty());

    // Positive control: the same entity with a shape Form does resolve.
    reg.add<ecs::AABBForm>(level, {{1,1,1}});
    CHECK(KQ::capsuleOverlap({0,0,0}, 0.5f, 1.0f, reg, ecs::NullEntity).size() == 1);
}

// ============================================================================
// capsuleCast — grounding / step probes (_grounded recipe)
// ============================================================================

TEST_CASE("capsuleCast: downward hit distance is measured from the capsule surface", "[kq][cast]") {
    ecs::Registry reg;
    makeStaticAABB(reg, {0,-1,0}, {10,1,10});   // top at y = 0

    // centre y=2, hh=1, r=0.5 => bottom surface at y = 0.5 => 0.5 m to floor.
    auto res = KQ::capsuleCast({0,2,0}, 0.5f, 1.0f, {0,-1,0}, 20.0f, reg, ecs::NullEntity);
    REQUIRE(res.hit);
    CHECK_THAT(res.t, WithinAbs(0.5f, 0.001f));
    CHECK_THAT(res.normal.y, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("capsuleCast: a miss reports t == maxDist and hit false", "[kq][cast]") {
    ecs::Registry reg;
    makeStaticAABB(reg, {0,-1,0}, {10,1,10});

    // Cast upward — nothing above.
    auto res = KQ::capsuleCast({0,2,0}, 0.5f, 1.0f, {0,1,0}, 7.5f, reg, ecs::NullEntity);
    CHECK_FALSE(res.hit);
    CHECK_THAT(res.t, WithinAbs(7.5f, 0.001f));
}

TEST_CASE("capsuleCast: grounded check on flat floor yields a near-vertical normal", "[kq][cast][grounded]") {
    // Mirrors the stub's _grounded(): hit && normal.y > 0.7.
    ecs::Registry reg;
    makeStaticAABB(reg, {0,-1,0}, {10,1,10});

    auto res = KQ::capsuleCast({0,1.55f,0}, 0.5f, 1.0f, {0,-1,0}, 0.1f, reg, ecs::NullEntity);
    REQUIRE(res.hit);
    CHECK(res.normal.y > 0.7f);
}

TEST_CASE("capsuleCast: steep slope fails the walkable-normal test", "[kq][cast][grounded]") {
    // A slab tilted 60 deg has an up-normal of cos60 = 0.5, below the 0.7
    // walkability threshold the recipe uses.
    ecs::Registry reg;
    makeStaticOBB(reg, {0,0,0}, {4,0.25f,4}, rotZ(60.0f));

    auto res = KQ::capsuleCast({0,3,0}, 0.5f, 1.0f, {0,-1,0}, 20.0f, reg, ecs::NullEntity);
    REQUIRE(res.hit);
    CHECK_THAT(std::abs(res.normal.y), WithinAbs(0.5f, 0.02f));
    CHECK_FALSE(res.normal.y > 0.7f);
}

TEST_CASE("capsuleCast: tilted face uses r/|n.dir|, not a flat r subtraction", "[kq][cast][tilt]") {
    // The implementation comment is explicit: for a tilted face the sphere
    // contacts r/|n.dir| before the ray tip reaches it, and using plain r
    // causes a one-frame clip-through during step-climb snap-down. Verify the
    // adjustment is actually applied by comparing against the raw ray t.
    ecs::Registry reg;
    const Quat rot = rotZ(45.0f);
    makeStaticOBB(reg, {0,0,0}, {4,0.25f,4}, rot);

    const float r = 0.5f, hh = 1.0f;
    const Vec3 centre{0, 4, 0};
    const Vec3 dir{0,-1,0};
    const Vec3 origin{centre.x, centre.y - hh, centre.z};   // bottom endpoint

    auto res = KQ::capsuleCast(centre, r, hh, dir, 20.0f, reg, ecs::NullEntity);
    REQUIRE(res.hit);

    // Raw ray distance to the same slab, computed independently.
    Mat3 m = Mat3::rotation(rot);
    std::array<Vec3,3> axes{{ {m.m[0],m.m[1],m.m[2]}, {m.m[3],m.m[4],m.m[5]}, {m.m[6],m.m[7],m.m[8]} }};
    float rayT = physics::Raycast::raycastOBB(dir, origin, {0,0,0}, {4,0.25f,4}, axes);
    REQUIRE(rayT > 0.0f);

    const float nDotDir = std::abs(res.normal.dot(dir));
    REQUIRE(nDotDir > 1e-4f);
    CHECK(nDotDir < 0.99f);                                   // genuinely tilted
    CHECK_THAT(res.t, WithinAbs(rayT - r / nDotDir, 0.005f)); // the documented formula
    CHECK(res.t < rayT - r);                                  // strictly earlier than naive r
}

TEST_CASE("capsuleCast: upward cast probes from the top endpoint (ceiling)", "[kq][cast]") {
    ecs::Registry reg;
    makeStaticAABB(reg, {0,5,0}, {10,0.5f,10});   // ceiling underside at y = 4.5

    // centre y=2, hh=1 => top endpoint y=3, top surface y=3.5 => 1.0 m of head room.
    auto res = KQ::capsuleCast({0,2,0}, 0.5f, 1.0f, {0,1,0}, 20.0f, reg, ecs::NullEntity);
    REQUIRE(res.hit);
    CHECK_THAT(res.t, WithinAbs(1.0f, 0.001f));
    CHECK_THAT(res.normal.y, WithinAbs(-1.0f, 0.001f));
}

TEST_CASE("capsuleCast: horizontal cast probes only from the bottom endpoint (DOCUMENTED GAP)",
          "[kq][cast][gap]") {
    // origin = (dir.y <= 0) ? bottom : top endpoint. A horizontal sweep has
    // dir.y == 0, so it probes from the BOTTOM sphere only — an obstacle level
    // with the capsule's upper half is missed entirely.
    const float r = 0.5f, hh = 1.0f;
    const Vec3 centre{0, 2, 0};      // bottom endpoint y=1, top endpoint y=3

    {   // obstacle at the TOP endpoint's height — missed
        ecs::Registry reg;
        makeStaticAABB(reg, {5,3,0}, {0.5f,0.5f,0.5f});
        auto res = KQ::capsuleCast(centre, r, hh, {1,0,0}, 20.0f, reg, ecs::NullEntity);
        CHECK_FALSE(res.hit);
    }
    {   // control: same obstacle at the BOTTOM endpoint's height — hit
        ecs::Registry reg;
        makeStaticAABB(reg, {5,1,0}, {0.5f,0.5f,0.5f});
        auto res = KQ::capsuleCast(centre, r, hh, {1,0,0}, 20.0f, reg, ecs::NullEntity);
        CHECK(res.hit);
    }
}

TEST_CASE("capsuleCast: exclude and tangibility are honoured", "[kq][cast]") {
    ecs::Registry reg;
    ecs::Entity floorE = makeStaticAABB(reg, {0,-1,0}, {10,1,10});

    CHECK(KQ::capsuleCast({0,2,0}, 0.5f, 1.0f, {0,-1,0}, 20.0f, reg, ecs::NullEntity).hit);
    CHECK_FALSE(KQ::capsuleCast({0,2,0}, 0.5f, 1.0f, {0,-1,0}, 20.0f, reg, floorE).hit);
}

// ============================================================================
// CollisionLayers — the named-layer registry (yope3d.world.layers)
// ============================================================================

TEST_CASE("CollisionLayers: add allocates ascending single bits", "[layers]") {
    physics::CollisionLayers layers;
    CHECK(layers.add("default") == (1u << 0));
    CHECK(layers.add("player")  == (1u << 1));
    CHECK(layers.add("debris")  == (1u << 2));
    CHECK(layers.count() == 3);
}

TEST_CASE("CollisionLayers: lookup by name, has(), and unknown-name throw", "[layers]") {
    physics::CollisionLayers layers;
    const uint32_t player = layers.add("player");

    CHECK(layers.has("player"));
    CHECK_FALSE(layers.has("enemy"));
    CHECK(layers["player"] == player);
    CHECK_THROWS(layers["enemy"]);
}

TEST_CASE("CollisionLayers: duplicate registration throws", "[layers]") {
    physics::CollisionLayers layers;
    layers.add("player");
    CHECK_THROWS(layers.add("player"));
    CHECK(layers.count() == 1);   // the failed add must not consume a slot
}

TEST_CASE("CollisionLayers: mask() ORs several named layers", "[layers]") {
    physics::CollisionLayers layers;
    const uint32_t a = layers.add("a");
    const uint32_t b = layers.add("b");
    layers.add("c");

    CHECK(layers.mask({"a", "b"}) == (a | b));
    CHECK(layers.mask({}) == physics::CollisionLayers::NONE);
}

TEST_CASE("CollisionLayers: all 32 slots are usable and the 33rd throws", "[layers]") {
    physics::CollisionLayers layers;
    uint32_t seen = 0;
    for (int i = 0; i < 32; ++i) {
        uint32_t bit = layers.add("layer" + std::to_string(i));
        CHECK(bit == (1u << i));
        seen |= bit;
    }
    CHECK(layers.count() == 32);
    CHECK(seen == physics::CollisionLayers::ALL);
    CHECK_THROWS(layers.add("one_too_many"));
}

TEST_CASE("CollisionLayers: ALL and NONE constants", "[layers]") {
    CHECK(physics::CollisionLayers::ALL  == 0xFFFFFFFFu);
    CHECK(physics::CollisionLayers::NONE == 0x00000000u);
    // Hull defaults must collide with everything out of the box.
    ecs::Hull h;
    CHECK(h.collisionLayer == physics::CollisionLayers::ALL);
    CHECK(h.collisionMask  == physics::CollisionLayers::ALL);
    // ...but be invisible to global collision observers until opted in.
    CHECK(h.observeLayers == physics::CollisionLayers::NONE);
}

// ============================================================================
// Narrowphase entry gates (ColliderDiscrete::detect)
//
// detect() is where tangible / static-static / layer-mask filtering actually
// happens. These pin the documented rule:
//   contact iff (A.layer & B.mask) && (B.layer & A.mask)
// ============================================================================

namespace {
// Two overlapping unit spheres 1.5 apart (0.5 m of penetration).
struct OverlapPair {
    ecs::Registry reg;
    ecs::Entity a, b;
    OverlapPair() {
        a = makeSphere(reg, {0,0,0},    1.0f);
        b = makeSphere(reg, {1.5f,0,0}, 1.0f);
    }
    size_t contactCount() {
        std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
        physics::ColliderDiscrete::detect(a, b, reg, contacts);
        return contacts.size();
    }
};
} // namespace

TEST_CASE("detect: overlapping spheres produce a contact by default", "[narrowphase][filter]") {
    OverlapPair p;
    CHECK(p.contactCount() > 0);
}

TEST_CASE("detect: tangible=false on either body suppresses the contact", "[narrowphase][filter]") {
    {
        OverlapPair p;
        p.reg.get<ecs::Hull>(p.a)->tangible = false;
        CHECK(p.contactCount() == 0);
    }
    {
        OverlapPair p;
        p.reg.get<ecs::Hull>(p.b)->tangible = false;
        CHECK(p.contactCount() == 0);
    }
}

TEST_CASE("detect: two Fixed bodies never contact each other", "[narrowphase][filter]") {
    OverlapPair p;
    p.reg.add<ecs::Fixed>(p.a);
    p.reg.add<ecs::Fixed>(p.b);
    CHECK(p.contactCount() == 0);

    // One Fixed body against a dynamic one still collides.
    OverlapPair q;
    q.reg.add<ecs::Fixed>(q.a);
    CHECK(q.contactCount() > 0);
}

TEST_CASE("detect: layer/mask filtering requires agreement in BOTH directions",
          "[narrowphase][filter][layers]") {
    const uint32_t L0 = 1u << 0, L1 = 1u << 1;

    SECTION("mutually visible layers collide") {
        OverlapPair p;
        auto* ha = p.reg.get<ecs::Hull>(p.a);
        auto* hb = p.reg.get<ecs::Hull>(p.b);
        ha->collisionLayer = L0; ha->collisionMask = L1;
        hb->collisionLayer = L1; hb->collisionMask = L0;
        CHECK(p.contactCount() > 0);
    }

    SECTION("disjoint layers do not collide") {
        OverlapPair p;
        auto* ha = p.reg.get<ecs::Hull>(p.a);
        auto* hb = p.reg.get<ecs::Hull>(p.b);
        ha->collisionLayer = L0; ha->collisionMask = L0;
        hb->collisionLayer = L1; hb->collisionMask = L1;
        CHECK(p.contactCount() == 0);
    }

    SECTION("one-directional visibility is not enough") {
        // A sees B, but B does not see A — the rule is an AND, so no contact.
        OverlapPair p;
        auto* ha = p.reg.get<ecs::Hull>(p.a);
        auto* hb = p.reg.get<ecs::Hull>(p.b);
        ha->collisionLayer = L0; ha->collisionMask = L1;   // A -> B ok
        hb->collisionLayer = L1; hb->collisionMask = L1;   // B -> A blocked
        CHECK(p.contactCount() == 0);
    }

    SECTION("mask of NONE opts a body out of all collision") {
        OverlapPair p;
        p.reg.get<ecs::Hull>(p.a)->collisionMask = physics::CollisionLayers::NONE;
        CHECK(p.contactCount() == 0);
    }
}
