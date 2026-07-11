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
        h->inertiaTensorWorld = math::Mat3::zero();  // advance() zeros this for Fixed entities
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
    hc.inverseInertia = math::Mat3::zero();  // AABB: no angular dynamics
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

static ecs::Entity makeOBBEntity(ecs::Registry& reg, math::Vec3 pos, math::Vec3 ext,
                                 math::Quat rot = {0, 0, 0, 1}) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, rot, {1, 1, 1}});
    ecs::Hull hc;
    hc.tangible = true;
    hc.mass = 1.0f;
    hc.inverseMass = 1.0f;
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::OBBForm>(e, {ext});
    return e;
}

TEST_CASE("ECS SAP: rotated OBB AABBs are rotation-fattened", "[sap][ecs][obb]") {
    // Two unit-half-extent cubes rotated 45deg about Z, centers 2.6 apart on X.
    // Unrotated boxes span x +-1 (gap: 2.6 > 2.0), but the rotated world AABB
    // half-extent is sqrt(2) (~1.414) so the true boxes overlap (2.83 > 2.6).
    // The pre-fix broadphase used the unrotated extents and missed this pair.
    const float h  = 0.5f * math::toRadians(45.0f);
    math::Quat rotZ{0.0f, 0.0f, std::sin(h), std::cos(h)};

    ecs::Registry reg;
    ecs::Entity e0 = makeOBBEntity(reg, {0, 0, 0},    {1, 1, 1}, rotZ);
    ecs::Entity e1 = makeOBBEntity(reg, {2.6f, 0, 0}, {1, 1, 1}, rotZ);
    std::vector<ecs::Entity> entities = {e0, e1};

    physics::BroadphaseSAP sap;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> pairs;
    sap.collectPairs(entities, reg, pairs);

    bool found = false;
    for (auto& [a, b] : pairs)
        if ((a == e0 && b == e1) || (a == e1 && b == e0))
            found = true;
    CHECK(found);

    // Negative control: same centers, unrotated — no overlap, no pair.
    ecs::Registry reg2;
    ecs::Entity f0 = makeOBBEntity(reg2, {0, 0, 0},    {1, 1, 1});
    ecs::Entity f1 = makeOBBEntity(reg2, {2.6f, 0, 0}, {1, 1, 1});
    std::vector<ecs::Entity> entities2 = {f0, f1};
    pairs.clear();
    sap.collectPairs(entities2, reg2, pairs);
    CHECK(pairs.empty());
}

// ============================================================================
// Static compound collider + mid-phase BVH
// ============================================================================
#include "../src/physics/CompoundShape.h"

// A 3x3 grid of unit OBB floor tiles (identity rotation) centered at y=0,
// spanning x,z in [-3,3]; each tile is 1x0.5x1 half-extents at 2-unit spacing.
static std::vector<physics::SubShape> makeFloorTiles() {
    std::vector<physics::SubShape> shapes;
    for (int ix = -1; ix <= 1; ++ix)
        for (int iz = -1; iz <= 1; ++iz) {
            physics::SubShape s;
            s.type     = physics::SubShapeType::OBB;
            s.localPos = { ix * 2.0f, 0.0f, iz * 2.0f };
            s.localRot = math::Mat3{};                 // identity
            s.extent   = { 1.0f, 0.5f, 1.0f };
            s.aabbMin  = s.localPos - s.extent;
            s.aabbMax  = s.localPos + s.extent;
            shapes.push_back(s);
        }
    return shapes;
}

static ecs::Entity makeCompoundEntity(ecs::Registry& reg, physics::CompiledCollider* col) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{{0,0,0}, {0,0,0,1}, {1,1,1}});
    ecs::Hull hc;
    hc.mass = 0.0f; hc.inverseMass = 0.0f;
    hc.inverseInertia = math::Mat3::zero();
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::Fixed>(e);
    ecs::CompoundCollider cc; cc.compiled = col;
    reg.add<ecs::CompoundCollider>(e, cc);
    return e;
}

TEST_CASE("Compound BVH: build invariants", "[compound][bvh]") {
    auto shapes = makeFloorTiles();
    physics::CompiledCollider col;
    physics::buildCompoundBvh(shapes, col, 2);

    REQUIRE(col.subShapes.size() == 9);
    REQUIRE(!col.nodes.empty());

    // Root bounds must enclose every sub-shape.
    for (const auto& s : col.subShapes) {
        CHECK(s.aabbMin.x >= col.localMin.x - 1e-4f);
        CHECK(s.aabbMax.x <= col.localMax.x + 1e-4f);
        CHECK(s.aabbMin.z >= col.localMin.z - 1e-4f);
        CHECK(s.aabbMax.z <= col.localMax.z + 1e-4f);
    }

    // Every sub-shape index must be reachable through exactly one leaf.
    std::vector<int> hits(col.subShapes.size(), 0);
    for (const auto& n : col.nodes)
        if (n.count > 0)
            for (int i = n.first; i < n.first + n.count; ++i) hits[i]++;
    for (int h : hits) CHECK(h == 1);
}

TEST_CASE("Compound BVH: descent matches brute force", "[compound][bvh]") {
    auto shapes = makeFloorTiles();
    physics::CompiledCollider col;
    physics::buildCompoundBvh(shapes, col, 2);

    // Query AABB overlapping only the +x,+z tile (index-agnostic; compare sets).
    math::Vec3 qmn{1.2f, -0.4f, 1.2f}, qmx{2.8f, 0.4f, 2.8f};
    auto boxOverlap = [](const math::Vec3& amn, const math::Vec3& amx,
                         const math::Vec3& bmn, const math::Vec3& bmx) {
        return !(amn.x > bmx.x || amx.x < bmn.x ||
                 amn.y > bmx.y || amx.y < bmn.y ||
                 amn.z > bmx.z || amx.z < bmn.z);
    };

    std::vector<int> brute;
    for (size_t i = 0; i < col.subShapes.size(); ++i)
        if (boxOverlap(qmn, qmx, col.subShapes[i].aabbMin, col.subShapes[i].aabbMax))
            brute.push_back((int)i);

    // Replicate detectCompound's descent using public node fields.
    std::vector<int> descent;
    int32_t stack[64]; int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        const physics::BvhNode& n = col.nodes[stack[--sp]];
        if (!boxOverlap(qmn, qmx, n.aabbMin, n.aabbMax)) continue;
        if (n.count > 0) {
            for (int i = n.first; i < n.first + n.count; ++i)
                if (boxOverlap(qmn, qmx, col.subShapes[i].aabbMin, col.subShapes[i].aabbMax))
                    descent.push_back(i);
        } else {
            if (n.right >= 0) stack[sp++] = n.right;
            if (n.left  >= 0) stack[sp++] = n.left;
        }
    }

    std::sort(brute.begin(), brute.end());
    std::sort(descent.begin(), descent.end());
    CHECK(brute == descent);
    CHECK(!brute.empty());
}

TEST_CASE("Compound narrowphase: sphere resting on a tile generates an upward contact", "[compound][narrowphase]") {
    auto shapes = makeFloorTiles();
    physics::CompiledCollider col;
    physics::buildCompoundBvh(shapes, col, 2);

    ecs::Registry reg;
    ecs::Entity floorE  = makeCompoundEntity(reg, &col);
    // Sphere r=0.5 centered at (2, 0.9, 2): bottom at 0.4 < tile top 0.5 => penetration.
    ecs::Entity sphereE = makeSphereEntity(reg, {2.0f, 0.9f, 2.0f}, 0.5f);

    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    physics::ColliderDiscrete::detect(floorE, sphereE, reg, contacts);

    REQUIRE(!contacts.empty());
    const auto& c = contacts.front();
    CHECK(c.a == floorE);
    CHECK(c.b == sphereE);
    // Normal points compound -> sphere, i.e. up (+Y).
    CHECK(c.manifold.normal.y > 0.5f);
    CHECK(c.manifold.numContacts > 0);
    // shapeKey identifies which sub-shape it came from.
    CHECK(c.shapeKey >= 0);
    CHECK(c.shapeKey < (int)col.subShapes.size());
}

TEST_CASE("Compound narrowphase: sphere over a gap generates no contact", "[compound][narrowphase]") {
    auto shapes = makeFloorTiles();
    physics::CompiledCollider col;
    physics::buildCompoundBvh(shapes, col, 2);

    ecs::Registry reg;
    ecs::Entity floorE  = makeCompoundEntity(reg, &col);
    // Far above every tile.
    ecs::Entity sphereE = makeSphereEntity(reg, {0.0f, 10.0f, 0.0f}, 0.5f);

    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    physics::ColliderDiscrete::detect(sphereE, floorE, reg, contacts);   // reversed order
    CHECK(contacts.empty());
}

// ============================================================================
// Trigger volumes (Hull.isTrigger) — narrowphase-level behavior
//
// The solver-skip / event-routing itself lives in World::advance() (not part of
// this headless target, which has no GpuDevice), so these tests cover what is
// testable at this level: isTrigger is orthogonal to tangible and does not
// suppress ColliderDiscrete::detect() the way tangible=false does.
// ============================================================================

static ecs::Entity makeStaticAABBEntity(ecs::Registry& reg, math::Vec3 pos, math::Vec3 ext,
                                         bool isTrigger) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {1,1,1}});
    ecs::Hull hc;
    hc.mass        = 0.0f;
    hc.inverseMass = 0.0f;
    hc.gravity     = false;
    hc.isTrigger   = isTrigger;
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::AABBForm>(e, {ext});
    reg.add<ecs::Fixed>(e);
    return e;
}

TEST_CASE("Trigger AABB still generates narrowphase contacts", "[trigger][narrowphase]") {
    ecs::Registry reg;
    ecs::Entity triggerE = makeStaticAABBEntity(reg, {0,0,0}, {1,1,1}, /*isTrigger=*/true);
    ecs::Entity sphereE  = makeSphereEntity(reg, {0,0,0}, 0.5f);  // fully inside the box

    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    physics::ColliderDiscrete::detect(triggerE, sphereE, reg, contacts);

    // isTrigger must not be conflated with tangible=false: overlap detection
    // (what World::advance() will route into triggerContacts_) still fires.
    REQUIRE(!contacts.empty());
    CHECK(contacts.front().a == triggerE);
    CHECK(contacts.front().b == sphereE);
}

TEST_CASE("Trigger AABB with no overlap generates no contact", "[trigger][narrowphase]") {
    ecs::Registry reg;
    ecs::Entity triggerE = makeStaticAABBEntity(reg, {0,0,0}, {1,1,1}, /*isTrigger=*/true);
    ecs::Entity sphereE  = makeSphereEntity(reg, {10,0,0}, 0.5f);  // far away

    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    physics::ColliderDiscrete::detect(triggerE, sphereE, reg, contacts);
    CHECK(contacts.empty());
}

TEST_CASE("isTrigger defaults false and is independent of tangible", "[trigger][hull]") {
    ecs::Hull hc;
    CHECK_FALSE(hc.isTrigger);
    CHECK(hc.tangible);  // default tangible=true is unaffected by adding isTrigger

    hc.isTrigger = true;
    CHECK(hc.tangible);  // setting isTrigger must not implicitly flip tangible
}

// ============================================================================
// Compound baker shape inference (physics::classifyAsSphere / meshVolume)
// ============================================================================

// A 2x2x2 axis-aligned cube (positions only — 8 verts, 12 triangles, outward winding).
static void makeCubeMeshData(std::vector<math::Vec3>& pos, std::vector<uint32_t>& idx) {
    pos = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},
    };
    idx = {
        0,2,1, 0,3,2,       // -Z
        4,5,6, 4,6,7,       // +Z
        0,1,5, 0,5,4,       // -Y
        3,7,6, 3,6,2,       // +Y
        0,4,7, 0,7,3,       // -X
        1,2,6, 1,6,5,       // +X
    };
}

// A UV sphere of radius r (outward-winding triangles).
static void makeUVSphereMeshData(float r, int rings, int segs,
                                 std::vector<math::Vec3>& pos, std::vector<uint32_t>& idx) {
    pos.clear(); idx.clear();
    for (int ring = 0; ring <= rings; ++ring) {
        float v = (float)ring / rings;
        float phi = v * math::PI;
        for (int seg = 0; seg <= segs; ++seg) {
            float u = (float)seg / segs;
            float theta = u * 2.0f * math::PI;
            pos.push_back({
                r * std::sin(phi) * std::cos(theta),
                r * std::cos(phi),
                r * std::sin(phi) * std::sin(theta)
            });
        }
    }
    int stride = segs + 1;
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segs; ++seg) {
            uint32_t a = ring * stride + seg;
            uint32_t b = a + stride;
            uint32_t c = a + 1;
            uint32_t d = b + 1;
            idx.insert(idx.end(), {a, b, c,  c, b, d});
        }
    }
}

TEST_CASE("Shape inference: cube is classified as a box, not a sphere", "[compound][infer]") {
    std::vector<math::Vec3> pos; std::vector<uint32_t> idx;
    makeCubeMeshData(pos, idx);
    float vol = physics::meshVolume(pos, idx);
    CHECK_THAT(vol, WithinAbs(8.0f, 0.01f));   // 2x2x2 cube
    bool isSphere = physics::classifyAsSphere(pos, idx, {1.0f, 1.0f, 1.0f});
    CHECK_FALSE(isSphere);
}

TEST_CASE("Shape inference: UV sphere is classified as a sphere", "[compound][infer]") {
    std::vector<math::Vec3> pos; std::vector<uint32_t> idx;
    makeUVSphereMeshData(1.0f, 24, 32, pos, idx);
    float vol = physics::meshVolume(pos, idx);
    float expected = (4.0f / 3.0f) * math::PI;   // r=1
    CHECK_THAT(vol, WithinAbs(expected, 0.05f));
    bool isSphere = physics::classifyAsSphere(pos, idx, {1.0f, 1.0f, 1.0f});
    CHECK(isSphere);
}

TEST_CASE("Shape inference: stretched sphere (ellipsoid) is NOT classified as a sphere", "[compound][infer]") {
    std::vector<math::Vec3> pos; std::vector<uint32_t> idx;
    makeUVSphereMeshData(1.0f, 24, 32, pos, idx);
    for (auto& p : pos) p.x *= 3.0f;   // stretch into an ellipsoid along X
    bool isSphere = physics::classifyAsSphere(pos, idx, {3.0f, 1.0f, 1.0f});
    CHECK_FALSE(isSphere);   // aspect ratio gate rejects it despite matching volume ratio
}

// ============================================================================
// Dynamic compound colliders: mass/COM/inertia math + compound-vs-compound
// narrowphase (Phase "dynamic compound colliders")
// ============================================================================

static physics::SubShape makeSphereSubShape(math::Vec3 pos, float r, float mass) {
    physics::SubShape s;
    s.type     = physics::SubShapeType::Sphere;
    s.localPos = pos;
    s.localRot = math::Mat3{};
    s.extent   = {r, 0.0f, 0.0f};
    s.aabbMin  = pos - math::Vec3{r, r, r};
    s.aabbMax  = pos + math::Vec3{r, r, r};
    s.mass     = mass;
    return s;
}

TEST_CASE("Compound mass properties: two-sphere dumbbell matches hand-computed inertia", "[compound][dynamic]") {
    const float m = 2.0f, r = 0.5f, d = 3.0f;
    std::vector<physics::SubShape> shapes = {
        makeSphereSubShape({ d, 0, 0}, r, m),
        makeSphereSubShape({-d, 0, 0}, r, m),
    };

    float totalMass = 0.0f;
    math::Mat3 invI = math::Mat3::zero();
    math::Vec3 pivot = physics::computeCompoundMassProperties(shapes, totalMass, invI);

    CHECK_THAT(totalMass, WithinAbs(2.0f * m, 1e-5f));
    CHECK_THAT(pivot.x, WithinAbs(0.0f, 1e-5f));
    CHECK_THAT(pivot.y, WithinAbs(0.0f, 1e-5f));
    CHECK_THAT(pivot.z, WithinAbs(0.0f, 1e-5f));

    // Hand-computed combined inertia about the (already-centered) COM: each
    // sphere contributes 2/5 m r^2 about its own center; parallel-axis adds
    // m*d^2 to the two axes perpendicular to the separation vector (x), and
    // nothing to the axis along it (d has no y/z component).
    float sphereI = 0.4f * m * r * r;
    float Ixx = 2.0f * sphereI;
    float Iyy = 2.0f * (sphereI + m * d * d);
    float Izz = Iyy;

    math::Mat3 combined = invI.inverse();   // back to the (non-inverted) tensor for readability
    CHECK_THAT(combined.m[0], WithinAbs(Ixx, 1e-3f));
    CHECK_THAT(combined.m[4], WithinAbs(Iyy, 1e-3f));
    CHECK_THAT(combined.m[8], WithinAbs(Izz, 1e-3f));
    // Off-diagonal terms vanish for this symmetric configuration.
    CHECK_THAT(combined.m[1], WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(combined.m[2], WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(combined.m[3], WithinAbs(0.0f, 1e-3f));
}

TEST_CASE("Compound mass properties: mass conservation", "[compound][dynamic]") {
    std::vector<physics::SubShape> shapes = {
        makeSphereSubShape({1, 0, 0}, 0.3f, 1.5f),
        makeSphereSubShape({0, 2, 0}, 0.6f, 4.0f),
        makeSphereSubShape({-1, -1, 0.5f}, 0.2f, 0.7f),
    };
    float expectedTotal = 0.0f;
    for (const auto& s : shapes) expectedTotal += s.mass;

    float totalMass = 0.0f;
    math::Mat3 invI = math::Mat3::zero();
    physics::computeCompoundMassProperties(shapes, totalMass, invI);

    CHECK_THAT(totalMass, WithinAbs(expectedTotal, 1e-5f));
}

TEST_CASE("Compound mass properties: COM recentering invariant", "[compound][dynamic]") {
    // Asymmetric mass distribution — centroid is nowhere near the origin pre-bake.
    std::vector<physics::SubShape> shapes = {
        makeSphereSubShape({0, 0, 0}, 0.5f, 1.0f),
        makeSphereSubShape({4, 0, 0}, 0.5f, 5.0f),
        makeSphereSubShape({0, 3, 0}, 0.5f, 2.0f),
    };
    float totalMass = 0.0f;
    math::Mat3 invI = math::Mat3::zero();
    math::Vec3 pivot = physics::computeCompoundMassProperties(shapes, totalMass, invI);

    // Post-bake, the mass-weighted centroid of the recentered shapes must be ~0.
    math::Vec3 recomputedCentroid{};
    for (const auto& s : shapes) recomputedCentroid = recomputedCentroid + s.localPos * s.mass;
    recomputedCentroid = recomputedCentroid * (1.0f / totalMass);
    CHECK_THAT(recomputedCentroid.x, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(recomputedCentroid.y, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(recomputedCentroid.z, WithinAbs(0.0f, 1e-4f));

    // pivotOffset is the centroid in the shapes' ORIGINAL frame — hand-computed.
    float expectedX = (0.0f * 1.0f + 4.0f * 5.0f + 0.0f * 2.0f) / 8.0f;
    float expectedY = (0.0f * 1.0f + 0.0f * 5.0f + 3.0f * 2.0f) / 8.0f;
    CHECK_THAT(pivot.x, WithinAbs(expectedX, 1e-4f));
    CHECK_THAT(pivot.y, WithinAbs(expectedY, 1e-4f));
}

// Two 1-subshape (single sphere) compounds — mirrors the existing static
// [compound][narrowphase] tests but exercises the aCompound&&bCompound path.
static ecs::Entity makeSphereCompoundEntity(ecs::Registry& reg, math::Vec3 worldPos,
                                            physics::CompiledCollider* col, bool fixed) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{worldPos, {0, 0, 0, 1}, {1, 1, 1}});
    ecs::Hull hc;
    hc.mass = fixed ? 0.0f : 1.0f;
    hc.inverseMass = fixed ? 0.0f : 1.0f;
    reg.add<ecs::Hull>(e, hc);
    if (fixed) reg.add<ecs::Fixed>(e);
    ecs::CompoundCollider cc; cc.compiled = col;
    reg.add<ecs::CompoundCollider>(e, cc);
    return e;
}

TEST_CASE("Compound-vs-compound narrowphase: overlapping single-sphere compounds collide", "[compound][dynamic]") {
    std::vector<physics::SubShape> shapesA = { makeSphereSubShape({0,0,0}, 0.5f, 1.0f) };
    std::vector<physics::SubShape> shapesB = { makeSphereSubShape({0,0,0}, 0.5f, 0.0f) };
    physics::CompiledCollider colA, colB;
    physics::buildCompoundBvh(shapesA, colA, 4);
    physics::buildCompoundBvh(shapesB, colB, 4);

    ecs::Registry reg;
    // A: dynamic body at the origin. B: static "level" compound offset by 0.8 —
    // spheres of radius 0.5 each overlap (0.5+0.5=1.0 > 0.8).
    ecs::Entity a = makeSphereCompoundEntity(reg, {0.0f, 0.0f, 0.0f}, &colA, false);
    ecs::Entity b = makeSphereCompoundEntity(reg, {0.8f, 0.0f, 0.0f}, &colB, true);

    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    physics::ColliderDiscrete::detect(a, b, reg, contacts);

    REQUIRE(!contacts.empty());
    const auto& c = contacts.front();
    CHECK(c.a == a);
    CHECK(c.b == b);
    CHECK(c.manifold.normal.x > 0.5f);   // a -> b points +x
    CHECK(c.shapeKey == 0);              // subA=0, subCountB=1, subB=0 -> 0*1+0
}

TEST_CASE("Compound-vs-compound narrowphase: far-apart single-sphere compounds do not collide", "[compound][dynamic]") {
    std::vector<physics::SubShape> shapesA = { makeSphereSubShape({0,0,0}, 0.5f, 1.0f) };
    std::vector<physics::SubShape> shapesB = { makeSphereSubShape({0,0,0}, 0.5f, 0.0f) };
    physics::CompiledCollider colA, colB;
    physics::buildCompoundBvh(shapesA, colA, 4);
    physics::buildCompoundBvh(shapesB, colB, 4);

    ecs::Registry reg;
    ecs::Entity a = makeSphereCompoundEntity(reg, {0.0f, 0.0f, 0.0f}, &colA, false);
    ecs::Entity b = makeSphereCompoundEntity(reg, {50.0f, 0.0f, 0.0f}, &colB, true);

    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    physics::ColliderDiscrete::detect(a, b, reg, contacts);
    CHECK(contacts.empty());
}

// ============================================================================
// Box–box face manifolds — reference-face clipping
//
// For face contact the patch corners are mostly clip intersection points that
// are corners of neither box. The old corner-containment collection gave a
// 2-point line manifold for a box straddling a support (no rocking resistance)
// and collapsed to a single center point at shallow resting penetrations.
// ============================================================================

namespace CD = physics::ColliderDiscrete;

static void patchSpread(const CD::ContactManifold& m, Vec3& lo, Vec3& hi) {
    lo = { 1e30f,  1e30f,  1e30f};
    hi = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < m.numContacts; ++i) {
        const Vec3& p = m.contactPoints[i];
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
}

TEST_CASE("OBB-OBB face manifold: box overhanging its support gets a full quad patch", "[narrowphase][manifold][obb]") {
    // Support (0.5)^3 at origin; a wide slab rests on top, overhanging on both
    // sides in x, penetrating 0.01. None of the slab's bottom corners lie
    // inside the support, so corner containment found zero candidates here.
    CD::OBBGeom support{{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, Mat3{}};
    CD::OBBGeom slab   {{0.0f, 0.74f, 0.0f}, {1.5f, 0.25f, 0.5f}, Mat3{}};

    CD::ContactManifold m;
    REQUIRE(CD::analyticalOBBOBB(support, slab, m));
    CHECK_THAT(m.penetration, WithinAbs(0.01f, 1e-4f));
    CHECK(m.normal.y > 0.9f);
    REQUIRE(m.numContacts == 4);

    // The patch must be the support's full top face (clip intersections), not
    // a line or a single midpoint.
    Vec3 lo, hi;
    patchSpread(m, lo, hi);
    CHECK_THAT(hi.x - lo.x, WithinAbs(1.0f, 1e-3f));
    CHECK_THAT(hi.z - lo.z, WithinAbs(1.0f, 1e-3f));
    for (int i = 0; i < 4; ++i)
        CHECK_THAT(m.depths[i], WithinAbs(0.01f, 1e-3f));
}

TEST_CASE("OBB-OBB face manifold: shallow resting penetration keeps 4 points", "[narrowphase][manifold][obb]") {
    // Penetration well below the old corner-containment threshold — the exact
    // resting state at 240 Hz that used to collapse to one center contact.
    CD::OBBGeom support{{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, Mat3{}};
    CD::OBBGeom box    {{0.1f, 0.9999f, 0.05f}, {0.5f, 0.5f, 0.5f}, Mat3{}};

    CD::ContactManifold m;
    REQUIRE(CD::analyticalOBBOBB(support, box, m));
    CHECK(m.normal.y > 0.9f);
    CHECK(m.numContacts == 4);
}

TEST_CASE("OBB-OBB face manifold: feature ids are distinct and stable under jitter", "[narrowphase][manifold][warmstart]") {
    CD::OBBGeom support{{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, Mat3{}};
    CD::OBBGeom slab   {{0.3f, 0.74f, 0.1f}, {1.5f, 0.25f, 0.5f}, Mat3{}};

    CD::ContactManifold m0;
    REQUIRE(CD::analyticalOBBOBB(support, slab, m0));
    REQUIRE(m0.numContacts >= 2);

    // Distinct within one manifold (each point warm-starts its own lambda).
    for (int i = 0; i < m0.numContacts; ++i)
        for (int j = i + 1; j < m0.numContacts; ++j)
            CHECK(m0.featureIds[i] != m0.featureIds[j]);

    // Stable across an FP-noise-sized wiggle: the same feature set must come
    // back regardless of array order (array order is depth-sorted noise).
    CD::OBBGeom slabJ = slab;
    slabJ.pos.y += 1e-6f;
    slabJ.pos.x += 1e-6f;
    CD::ContactManifold m1;
    REQUIRE(CD::analyticalOBBOBB(support, slabJ, m1));
    REQUIRE(m1.numContacts == m0.numContacts);
    for (int i = 0; i < m0.numContacts; ++i) {
        bool found = false;
        for (int j = 0; j < m1.numContacts; ++j)
            if (m1.featureIds[j] == m0.featureIds[i]) found = true;
        CHECK(found);
    }
}

TEST_CASE("AABB-OBB face manifold: 45-degree-rotated box on a large ground keeps its 4 corners", "[narrowphase][manifold][aabbobb]") {
    CD::AABBGeom ground{{0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 10.0f}};
    CD::OBBGeom  box{{0.0f, 0.495f, 0.0f}, {0.5f, 0.5f, 0.5f},
                     Mat3::rotation(Vec3{0.0f, 0.785398f, 0.0f})};

    CD::ContactManifold m;
    REQUIRE(CD::analyticalAABBOBB(ground, box, m));
    CHECK(m.normal.y > 0.9f);
    REQUIRE(m.numContacts == 4);
    // All four bottom corners of the rotated box survive the clip (the ground
    // face is much larger), giving a square patch of diagonal 2*sqrt(0.5).
    Vec3 lo, hi;
    patchSpread(m, lo, hi);
    CHECK_THAT(hi.x - lo.x, WithinAbs(1.41421f, 1e-3f));
    CHECK_THAT(hi.z - lo.z, WithinAbs(1.41421f, 1e-3f));
}

TEST_CASE("AABB-AABB manifold carries stable per-corner feature ids", "[narrowphase][manifold][warmstart]") {
    CD::AABBGeom ground{{0.0f, -0.5f, 0.0f}, {10.0f, 0.5f, 10.0f}};
    CD::AABBGeom box   {{0.0f, 0.49f, 0.0f}, {0.5f, 0.5f, 0.5f}};

    CD::ContactManifold m;
    REQUIRE(CD::analyticalAABBAABB(ground, box, m));
    REQUIRE(m.numContacts == 4);
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            CHECK(m.featureIds[i] != m.featureIds[j]);
}

// ============================================================================
// End-to-end resting stability: OBB pyramid at 240 Hz
//
// Mirrors World::advance's per-step order (detect -> solve -> integrate with
// gravity/damping/split-impulse) minus broadphase/islands/sleep, which don't
// affect a 7-body scene. Regression for the pyramid-collapse failure mode:
// corner-containment manifolds + index-keyed warm start ratcheted micro-jitter
// into sliding and topple within a couple of simulated seconds.
// ============================================================================

static ecs::Entity makePyramidBox(ecs::Registry& reg, Vec3 pos, Vec3 ext) {
    ecs::Entity e = reg.create();
    reg.add<Transform>(e, Transform{pos, {0,0,0,1}, {1,1,1}});
    ecs::Hull hc;
    hc.mass        = 1.0f;
    hc.inverseMass = 1.0f;
    // Box inertia from half-extents: I_x = (1/3) m (ey^2 + ez^2), etc.
    hc.inverseInertia = Mat3::scale({
        3.0f / (hc.mass * (ext.y*ext.y + ext.z*ext.z)),
        3.0f / (hc.mass * (ext.x*ext.x + ext.z*ext.z)),
        3.0f / (hc.mass * (ext.x*ext.x + ext.y*ext.y))});
    reg.add<ecs::Hull>(e, hc);
    reg.add<ecs::OBBForm>(e, {ext});
    return e;
}

TEST_CASE("OBB pyramid rests stably for 8 simulated seconds at 240 Hz", "[narrowphase][solver][stability]") {
    ecs::Registry reg;
    std::vector<ecs::Entity> ents;

    // Static ground (AABB) — exercises the AABB-OBB clipped path.
    {
        ecs::Entity g = reg.create();
        reg.add<Transform>(g, Transform{{0,-0.5f,0}, {0,0,0,1}, {1,1,1}});
        ecs::Hull hc;
        hc.mass = 0.0f; hc.inverseMass = 0.0f;
        hc.inverseInertia = Mat3::zero();
        reg.add<ecs::Hull>(g, hc);
        reg.add<ecs::AABBForm>(g, {{20.0f, 0.5f, 20.0f}});
        reg.add<ecs::Fixed>(g);
        ents.push_back(g);
    }

    // 3-2-1 pyramid of unit boxes (half-extent 0.5), each upper box straddling
    // two supports — the manifold shape that used to degrade to 2-point lines.
    const Vec3 ext{0.5f, 0.5f, 0.5f};
    std::vector<Vec3> starts;
    auto spawn = [&](float x, float y) {
        starts.push_back({x, y, 0.0f});
        ents.push_back(makePyramidBox(reg, starts.back(), ext));
    };
    spawn(-1.05f, 0.5f); spawn(0.0f, 0.5f); spawn(1.05f, 0.5f);
    spawn(-0.525f, 1.5f); spawn(0.525f, 1.5f);
    spawn(0.0f, 2.5f);

    physics::EntityContactCache cache;
    const float dt = physics::PHYSICS_DT;
    const Vec3  gravity{0.0f, physics::GRAVITY_Y, 0.0f};

    auto normalizeQuat = [](math::Quat q) {
        float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        if (len > 1e-7f) { q.x/=len; q.y/=len; q.z/=len; q.w/=len; }
        return q;
    };

    for (int step = 0; step < 1920; ++step) {
        // World-space inverse inertia (entity_list_build equivalent).
        for (ecs::Entity e : ents) {
            auto* hc = reg.get<ecs::Hull>(e);
            auto* tf = reg.get<Transform>(e);
            if (reg.has<ecs::Fixed>(e)) { hc->inertiaTensorWorld = Mat3::zero(); continue; }
            Mat3 R = Mat3::rotation(tf->rotation);
            hc->inertiaTensorWorld = R * hc->inverseInertia * R.transpose();
        }

        // Narrowphase over all pairs (7 bodies — broadphase unnecessary).
        std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
        for (size_t i = 0; i < ents.size(); ++i)
            for (size_t j = i + 1; j < ents.size(); ++j)
                physics::ColliderDiscrete::detect(ents[i], ents[j], reg, contacts);

        physics::ColliderDiscrete::solveIsland(contacts, dt, reg, cache);

        // Integration (mirrors World::advance).
        for (ecs::Entity e : ents) {
            if (reg.has<ecs::Fixed>(e)) continue;
            auto* hc = reg.get<ecs::Hull>(e);
            auto* tf = reg.get<Transform>(e);

            hc->velocity += gravity * dt;
            float linDecay = 1.0f - hc->linearDamping  * dt;
            float angDecay = 1.0f - hc->angularDamping * dt;
            if (linDecay > 0.0f) hc->velocity = hc->velocity * linDecay;
            if (angDecay > 0.0f) hc->omega    = hc->omega    * angDecay;

            tf->position += hc->velocity * dt;
            float omegaLen = std::sqrt(hc->omega.dot(hc->omega));
            if (omegaLen > 1e-7f) {
                math::Quat dq = math::Quat::fromAxisAngle(hc->omega * (1.0f / omegaLen), omegaLen * dt);
                tf->rotation = normalizeQuat(dq * tf->rotation);
            }

            tf->position += hc->pseudoVel * dt;
            float pOmLen = std::sqrt(hc->pseudoOmega.dot(hc->pseudoOmega));
            if (pOmLen > 2.0f) hc->pseudoOmega = hc->pseudoOmega * (2.0f / pOmLen);
            if (pOmLen > 1e-7f) {
                math::Quat dq = math::Quat::fromAxisAngle(hc->pseudoOmega * (1.0f / pOmLen), std::min(pOmLen, 2.0f) * dt);
                tf->rotation = normalizeQuat(dq * tf->rotation);
            }
            hc->pseudoVel = {}; hc->pseudoOmega = {};
        }
    }

    // Every box stays where it was placed (small settle allowance) and upright.
    for (size_t i = 0; i < starts.size(); ++i) {
        auto* tf = reg.get<Transform>(ents[i + 1]);   // ents[0] is the ground
        Vec3 d = tf->position - starts[i];
        INFO("box " << i << " drift (" << d.x << ", " << d.y << ", " << d.z << ")");
        CHECK(std::abs(d.x) < 0.1f);
        CHECK(std::abs(d.y) < 0.1f);
        CHECK(std::abs(d.z) < 0.1f);
        CHECK(std::abs(tf->rotation.w) > 0.99f);   // no toppling
    }
}
