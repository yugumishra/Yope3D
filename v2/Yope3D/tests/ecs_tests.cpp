#include <catch2/catch_test_macros.hpp>
#include "ecs/Registry.h"
#include "world/TransformHierarchy.h"
#include <cmath>

using namespace ecs;

// ---------------------------------------------------------------------------
// Test component types
// ---------------------------------------------------------------------------

struct Position { float x, y, z; };
struct Velocity { float x, y, z; };
struct Health   { int value; };
struct Tag      {};              // zero-content tag (sizeof == 1 in C++)
struct Marker   {};              // second tag for exclusion tests

// ---------------------------------------------------------------------------
// Entity lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("create returns a valid entity", "[ecs][entity]") {
    Registry reg;
    Entity e = reg.create();
    REQUIRE(reg.valid(e));
}

TEST_CASE("destroy invalidates the handle", "[ecs][entity]") {
    Registry reg;
    Entity e = reg.create();
    reg.destroy(e);
    REQUIRE_FALSE(reg.valid(e));
}

TEST_CASE("NullEntity is always invalid", "[ecs][entity]") {
    Registry reg;
    REQUIRE_FALSE(reg.valid(NullEntity));
}

TEST_CASE("generation recycling: old handle stale, new handle valid", "[ecs][entity]") {
    Registry reg;
    Entity e1 = reg.create();
    uint32_t savedId = e1.id;
    reg.destroy(e1);

    Entity e2 = reg.create(); // reuses the slot
    REQUIRE(e2.id == savedId);
    REQUIRE_FALSE(reg.valid(e1));   // old generation
    REQUIRE(reg.valid(e2));         // new generation
}

TEST_CASE("multiple creates and destroys maintain correct count", "[ecs][entity]") {
    Registry reg;
    auto a = reg.create();
    auto b = reg.create();
    auto c = reg.create();
    REQUIRE(reg.entityCount() == 3);
    reg.destroy(b);
    REQUIRE(reg.entityCount() == 2);
    reg.destroy(a);
    reg.destroy(c);
    REQUIRE(reg.entityCount() == 0);
}

// ---------------------------------------------------------------------------
// Single-component add / get / has / remove
// ---------------------------------------------------------------------------

TEST_CASE("add and get round-trip", "[ecs][component]") {
    Registry reg;
    Entity e = reg.create();
    reg.add<Position>(e, {1.0f, 2.0f, 3.0f});
    auto* p = reg.get<Position>(e);
    REQUIRE(p != nullptr);
    REQUIRE(p->x == 1.0f);
    REQUIRE(p->y == 2.0f);
    REQUIRE(p->z == 3.0f);
}

TEST_CASE("add returns a writable reference", "[ecs][component]") {
    Registry reg;
    Entity e = reg.create();
    auto& p = reg.add<Position>(e);
    p.x = 42.0f;
    REQUIRE(reg.get<Position>(e)->x == 42.0f);
}

TEST_CASE("has returns true after add, false before", "[ecs][component]") {
    Registry reg;
    Entity e = reg.create();
    REQUIRE_FALSE(reg.has<Position>(e));
    reg.add<Position>(e);
    REQUIRE(reg.has<Position>(e));
}

TEST_CASE("remove clears the component", "[ecs][component]") {
    Registry reg;
    Entity e = reg.create();
    reg.add<Position>(e);
    reg.remove<Position>(e);
    REQUIRE_FALSE(reg.has<Position>(e));
    REQUIRE(reg.get<Position>(e) == nullptr);
}

TEST_CASE("get returns nullptr for absent component", "[ecs][component]") {
    Registry reg;
    Entity e = reg.create();
    REQUIRE(reg.get<Health>(e) == nullptr);
}

TEST_CASE("get returns nullptr for invalid entity", "[ecs][component]") {
    Registry reg;
    Entity e = reg.create();
    reg.destroy(e);
    REQUIRE(reg.get<Position>(e) == nullptr);
}

// ---------------------------------------------------------------------------
// Multi-component and archetype migration
// ---------------------------------------------------------------------------

TEST_CASE("adding two components lands in correct archetype (sorted key)", "[ecs][archetype]") {
    Registry reg;
    Entity e = reg.create();
    reg.add<Position>(e);
    reg.add<Velocity>(e);
    REQUIRE(reg.has<Position>(e));
    REQUIRE(reg.has<Velocity>(e));
    REQUIRE(reg.get<Position>(e) != nullptr);
    REQUIRE(reg.get<Velocity>(e) != nullptr);
}

TEST_CASE("removing one component preserves the other", "[ecs][archetype]") {
    Registry reg;
    Entity e = reg.create();
    reg.add<Position>(e, {5.0f, 6.0f, 7.0f});
    reg.add<Health>(e, {100});
    reg.remove<Position>(e);
    REQUIRE_FALSE(reg.has<Position>(e));
    REQUIRE(reg.has<Health>(e));
    REQUIRE(reg.get<Health>(e)->value == 100);
}

TEST_CASE("component values survive archetype migration", "[ecs][archetype]") {
    Registry reg;
    Entity e = reg.create();
    reg.add<Position>(e, {1.0f, 2.0f, 3.0f});
    reg.add<Velocity>(e, {4.0f, 5.0f, 6.0f});
    reg.add<Health>(e, {50});
    reg.remove<Velocity>(e);

    REQUIRE(reg.get<Position>(e)->x == 1.0f);
    REQUIRE(reg.get<Health>(e)->value == 50);
    REQUIRE_FALSE(reg.has<Velocity>(e));
}

TEST_CASE("add order doesn't matter — same final archetype", "[ecs][archetype]") {
    Registry reg;
    Entity a = reg.create();
    Entity b = reg.create();
    reg.add<Position>(a); reg.add<Velocity>(a);
    reg.add<Velocity>(b); reg.add<Position>(b);
    REQUIRE(reg.has<Position>(a)); REQUIRE(reg.has<Velocity>(a));
    REQUIRE(reg.has<Position>(b)); REQUIRE(reg.has<Velocity>(b));
}

// ---------------------------------------------------------------------------
// SwapRemove correctness
// ---------------------------------------------------------------------------

TEST_CASE("swapRemove: removed entity gone, moved entity record updated", "[ecs][archetype]") {
    Registry reg;
    // Fill an archetype with 3 entities
    Entity a = reg.create(); reg.add<Position>(a, {1,0,0});
    Entity b = reg.create(); reg.add<Position>(b, {2,0,0});
    Entity c = reg.create(); reg.add<Position>(c, {3,0,0});

    reg.remove<Position>(b); // b migrates to empty archetype; c swaps into b's row

    REQUIRE(reg.has<Position>(a));
    REQUIRE_FALSE(reg.has<Position>(b));
    REQUIRE(reg.has<Position>(c));
    REQUIRE(reg.get<Position>(a)->x == 1.0f);
    REQUIRE(reg.get<Position>(c)->x == 3.0f);
}

TEST_CASE("destroy mid-archetype: moved entity's record updated", "[ecs][entity]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Health>(a, {10});
    Entity b = reg.create(); reg.add<Health>(b, {20});
    Entity c = reg.create(); reg.add<Health>(c, {30});

    reg.destroy(b); // b destroyed; c swap-removes into b's row

    REQUIRE(reg.valid(a)); REQUIRE(reg.get<Health>(a)->value == 10);
    REQUIRE_FALSE(reg.valid(b));
    REQUIRE(reg.valid(c)); REQUIRE(reg.get<Health>(c)->value == 30);
}

// ---------------------------------------------------------------------------
// View iteration
// ---------------------------------------------------------------------------

TEST_CASE("view iterates all matching entities", "[ecs][view]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Position>(a);
    Entity b = reg.create(); reg.add<Position>(b);
    Entity c = reg.create(); reg.add<Velocity>(c); // no Position

    std::vector<Entity> seen;
    for (auto [e, p] : reg.view<Position>())
        seen.push_back(e);

    REQUIRE(seen.size() == 2);
    REQUIRE(std::find(seen.begin(), seen.end(), a) != seen.end());
    REQUIRE(std::find(seen.begin(), seen.end(), b) != seen.end());
}

TEST_CASE("view with two components only yields entities with both", "[ecs][view]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Position>(a); reg.add<Velocity>(a);
    Entity b = reg.create(); reg.add<Position>(b);                       // no Velocity
    Entity c = reg.create(); reg.add<Velocity>(c);                       // no Position

    std::vector<Entity> seen;
    for (auto [e, p, v] : reg.view<Position, Velocity>())
        seen.push_back(e);

    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0] == a);
}

TEST_CASE("view over empty registry yields nothing", "[ecs][view]") {
    Registry reg;
    int count = 0;
    for (auto [e, p] : reg.view<Position>()) ++count;
    REQUIRE(count == 0);
}

TEST_CASE("view mutations through refs write back", "[ecs][view]") {
    Registry reg;
    Entity e = reg.create();
    reg.add<Position>(e, {0,0,0});

    for (auto [ent, p] : reg.view<Position>())
        p.x = 99.0f;

    REQUIRE(reg.get<Position>(e)->x == 99.0f);
}

TEST_CASE("view yields correct entity handles", "[ecs][view]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Health>(a, {1});
    Entity b = reg.create(); reg.add<Health>(b, {2});

    for (auto [e, h] : reg.view<Health>()) {
        if (e == a) REQUIRE(h.value == 1);
        if (e == b) REQUIRE(h.value == 2);
    }
}

// ---------------------------------------------------------------------------
// Exclusion filter
// ---------------------------------------------------------------------------

TEST_CASE("exclude filters out archetypes with the excluded type", "[ecs][view][exclude]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Position>(a); reg.add<Tag>(a); // has Tag
    Entity b = reg.create(); reg.add<Position>(b);                   // no Tag

    std::vector<Entity> seen;
    for (auto [e, p] : reg.view<Position>().exclude<Tag>())
        seen.push_back(e);

    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0] == b);
}

TEST_CASE("exclude with no matching archetypes yields empty view", "[ecs][view][exclude]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Position>(a); reg.add<Tag>(a);

    int count = 0;
    for (auto [e, p] : reg.view<Position>().exclude<Tag>()) ++count;
    REQUIRE(count == 0);
}

TEST_CASE("multiple excludes filter correctly", "[ecs][view][exclude]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Position>(a);
    Entity b = reg.create(); reg.add<Position>(b); reg.add<Tag>(b);
    Entity c = reg.create(); reg.add<Position>(c); reg.add<Marker>(c);
    Entity d = reg.create(); reg.add<Position>(d); reg.add<Tag>(d); reg.add<Marker>(d);

    std::vector<Entity> seen;
    for (auto [e, p] : reg.view<Position>().exclude<Tag, Marker>())
        seen.push_back(e);

    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0] == a);
}

TEST_CASE("tag component as exclusion: presence means sleeping", "[ecs][view][exclude]") {
    Registry reg;
    Entity awake  = reg.create(); reg.add<Velocity>(awake);
    Entity asleep = reg.create(); reg.add<Velocity>(asleep); reg.add<Tag>(asleep);

    int count = 0;
    for (auto [e, v] : reg.view<Velocity>().exclude<Tag>()) ++count;
    REQUIRE(count == 1);
}

// ---------------------------------------------------------------------------
// Tag components (zero-content structs)
// ---------------------------------------------------------------------------

TEST_CASE("tag component add/has/remove works", "[ecs][tag]") {
    Registry reg;
    Entity e = reg.create();
    REQUIRE_FALSE(reg.has<Tag>(e));
    reg.add<Tag>(e);
    REQUIRE(reg.has<Tag>(e));
    reg.remove<Tag>(e);
    REQUIRE_FALSE(reg.has<Tag>(e));
}

TEST_CASE("entity with only a tag component is valid", "[ecs][tag]") {
    Registry reg;
    Entity e = reg.create();
    reg.add<Tag>(e);
    REQUIRE(reg.valid(e));
    REQUIRE(reg.has<Tag>(e));
}

// ---------------------------------------------------------------------------
// Batch / stress
// ---------------------------------------------------------------------------

TEST_CASE("1000 entities with 3 components each — view iterates all", "[ecs][batch]") {
    Registry reg;
    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        Entity e = reg.create();
        reg.add<Position>(e, { static_cast<float>(i), 0, 0 });
        reg.add<Velocity>(e, { 0, static_cast<float>(i), 0 });
        reg.add<Health>(e,   { i });
    }

    int count = 0;
    float sumX = 0;
    for (auto [e, p, v, h] : reg.view<Position, Velocity, Health>()) {
        ++count;
        sumX += p.x;
    }
    REQUIRE(count == N);
    REQUIRE(sumX == static_cast<float>(N * (N-1) / 2));
}

TEST_CASE("destroy half of a batch, remainder iterates correctly", "[ecs][batch]") {
    Registry reg;
    const int N = 100;
    std::vector<Entity> entities;
    for (int i = 0; i < N; ++i) {
        Entity e = reg.create();
        reg.add<Health>(e, {i});
        entities.push_back(e);
    }
    // Destroy even-indexed entities
    for (int i = 0; i < N; i += 2)
        reg.destroy(entities[i]);

    int count = 0;
    for (auto [e, h] : reg.view<Health>()) ++count;
    REQUIRE(count == N / 2);
}

// ---------------------------------------------------------------------------
// Transform hierarchy (Parent) — composition, toLocal inverse, subtree ops
// ---------------------------------------------------------------------------

TEST_CASE("worldTransform composes parent chain; toLocal inverts it", "[ecs][hierarchy]") {
    Registry reg;
    Entity parent = reg.create();
    Transform pt;
    pt.position = {10.f, 0.f, 0.f};
    pt.rotation = math::Quat::fromAxisAngle(math::Vec3{0.f, 1.f, 0.f}, 1.5707963f); // +90° about Y
    pt.scale    = {2.f, 2.f, 2.f};
    reg.add<Transform>(parent, pt);

    Entity child = reg.create();
    Transform ct; ct.position = {1.f, 0.f, 0.f};   // local +X
    reg.add<Transform>(child, ct);
    reg.add<ecs::Parent>(child, ecs::Parent{parent});

    // local (1,0,0) → *scale2 = (2,0,0) → rot90°Y = (0,0,-2) → +parentPos = (10,0,-2)
    Transform w = hierarchy::worldTransform(reg, child);
    CHECK(std::fabs(w.position.x - 10.f) < 1e-4f);
    CHECK(std::fabs(w.position.y -  0.f) < 1e-4f);
    CHECK(std::fabs(w.position.z + 2.f)  < 1e-4f);
    CHECK(std::fabs(w.scale.x - 2.f)     < 1e-4f);

    // toLocal(parentWorld, childWorld) recovers the stored local transform.
    Transform local = hierarchy::toLocal(hierarchy::worldTransform(reg, parent), w);
    CHECK(std::fabs(local.position.x - 1.f) < 1e-4f);
    CHECK(std::fabs(local.position.y - 0.f) < 1e-4f);
    CHECK(std::fabs(local.position.z - 0.f) < 1e-4f);
    CHECK(std::fabs(local.scale.x - 1.f)    < 1e-4f);
}

TEST_CASE("root entity: worldTransform == local", "[ecs][hierarchy]") {
    Registry reg;
    Entity e = reg.create();
    Transform t; t.position = {3.f, 4.f, 5.f};
    reg.add<Transform>(e, t);
    Transform w = hierarchy::worldTransform(reg, e);
    CHECK(std::fabs(w.position.x - 3.f) < 1e-6f);
    CHECK(std::fabs(w.position.z - 5.f) < 1e-6f);
}

TEST_CASE("collectSubtree is parent-before-child; isDescendantOf detects cycles", "[ecs][hierarchy]") {
    Registry reg;
    Entity root = reg.create(); reg.add<Transform>(root);
    Entity a = reg.create(); reg.add<Transform>(a); reg.add<ecs::Parent>(a, ecs::Parent{root});
    Entity b = reg.create(); reg.add<Transform>(b); reg.add<ecs::Parent>(b, ecs::Parent{root});
    Entity c = reg.create(); reg.add<Transform>(c); reg.add<ecs::Parent>(c, ecs::Parent{a}); // grandchild

    std::vector<Entity> sub;
    hierarchy::collectSubtree(reg, root, sub);
    REQUIRE(sub.size() == 4);
    CHECK(sub[0] == root);

    auto idx = [&](Entity e) { for (size_t i = 0; i < sub.size(); ++i) if (sub[i] == e) return (int)i; return -1; };
    CHECK(idx(a) < idx(c));   // parent precedes child
    CHECK(idx(root) < idx(a));
    CHECK(idx(root) < idx(b));

    CHECK(hierarchy::isDescendantOf(reg, c, root));
    CHECK(hierarchy::isDescendantOf(reg, c, a));
    CHECK(hierarchy::isDescendantOf(reg, root, root));   // self
    CHECK_FALSE(hierarchy::isDescendantOf(reg, c, b));
    CHECK_FALSE(hierarchy::isDescendantOf(reg, root, c)); // ancestor is not descendant
}

// ---------------------------------------------------------------------------
// entitiesWith({}) — the all-entities enumeration the runtime save basis uses
// (SceneSerializer::collectSerializableEntities in a shipped build). An empty
// required set must match every archetype, and a tag filter (has<Tag>) must
// exclude tagged entities without dropping the rest — mirroring the Transient
// opt-out.
// ---------------------------------------------------------------------------

TEST_CASE("entitiesWith empty set returns all live entities across archetypes", "[ecs][view]") {
    Registry reg;
    Entity a = reg.create(); reg.add<Position>(a, {});                         // archetype {Position}
    Entity b = reg.create(); reg.add<Position>(b, {}); reg.add<Velocity>(b, {}); // {Position,Velocity}
    Entity c = reg.create(); reg.add<Health>(c, {});                          // {Health}
    Entity d = reg.create(); reg.add<Tag>(d);                                 // {Tag} — zero-size tag only

    auto all = reg.entitiesWith({});
    REQUIRE(all.size() == 4);

    auto contains = [&](Entity e) {
        for (Entity x : all) if (x == e) return true;
        return false;
    };
    CHECK(contains(a));
    CHECK(contains(b));
    CHECK(contains(c));
    CHECK(contains(d));   // an entity whose only component is a tag is still enumerated
}

TEST_CASE("entitiesWith empty set with a tag filter excludes only tagged entities", "[ecs][view]") {
    Registry reg;
    Entity keep1 = reg.create(); reg.add<Position>(keep1, {});
    Entity keep2 = reg.create(); reg.add<Health>(keep2, {});
    Entity drop  = reg.create(); reg.add<Position>(drop, {}); reg.add<Tag>(drop);   // "Transient"

    std::vector<Entity> saved;
    for (Entity e : reg.entitiesWith({}))
        if (!reg.has<Tag>(e)) saved.push_back(e);

    REQUIRE(saved.size() == 2);
    auto contains = [&](Entity e) {
        for (Entity x : saved) if (x == e) return true;
        return false;
    };
    CHECK(contains(keep1));
    CHECK(contains(keep2));
    CHECK_FALSE(contains(drop));
}
