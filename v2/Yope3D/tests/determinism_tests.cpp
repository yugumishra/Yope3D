// ============================================================================
// Determinism — Layer 0: same-binary, run-to-run reproducibility of the REAL
// World::advance() loop, headless (no GPU).
//
// This links the whole engine and drives the genuine physics step — NOT a
// re-implementation. Each case builds an identical scene twice via C++
// factories, runs the real advance() for N steps, captures the full dynamic
// state (Transform pos/rot + Hull vel/omega) as raw float bits at several
// checkpoints, and asserts the two runs are BIT-IDENTICAL.
//
// What this proves if it passes: the default (thread-pool parallel) physics
// path is reproducible run-to-run on this binary/machine — the headline
// determinism property, and §10b's "first artifact", backed by evidence rather
// than the code-level argument (disjoint islands + read-only statics + no
// cross-island reduction).
//
// Scope boundary (honest): two Worlds in ONE process controls for ASLR, so it
// catches ordering/accumulation nondeterminism but NOT a cross-process
// pointer-hash divergence (that needs two separate launches — a CI-script
// concern). And it runs NO scripts, so script-level nondeterminism
// (time.time(), unseeded random in Python behaviors) is out of scope until the
// headless service/Python seams land. This is the sim core, not a scripted game.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include "world/World.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "world/Transform.h"
#include "math/Vec3.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <functional>

using math::Vec3;

namespace {

constexpr float DT = 1.0f / 240.0f;
constexpr int   WORDS_PER_ENTITY = 13;   // 3 pos + 4 rot + 3 vel + 3 omega

uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// Full per-entity state as raw float bit patterns (so -0.0 / NaN compare exactly).
void captureEntity(World& w, ecs::Entity e, std::vector<uint32_t>& out) {
    auto& reg = w.getRegistry();
    auto* tf = reg.get<Transform>(e);
    auto* h  = reg.get<ecs::Hull>(e);
    REQUIRE(tf != nullptr);
    REQUIRE(h  != nullptr);
    out.push_back(bits(tf->position.x)); out.push_back(bits(tf->position.y)); out.push_back(bits(tf->position.z));
    out.push_back(bits(tf->rotation.x)); out.push_back(bits(tf->rotation.y));
    out.push_back(bits(tf->rotation.z)); out.push_back(bits(tf->rotation.w));
    out.push_back(bits(h->velocity.x)); out.push_back(bits(h->velocity.y)); out.push_back(bits(h->velocity.z));
    out.push_back(bits(h->omega.x));    out.push_back(bits(h->omega.y));    out.push_back(bits(h->omega.z));
}

using Snapshot = std::vector<uint32_t>;                 // one checkpoint
using Trajectory = std::vector<Snapshot>;               // all checkpoints
using Builder = std::function<std::vector<ecs::Entity>(World&)>;

// Build a fresh World via `build`, run the REAL advance() up to the last
// checkpoint, snapshot tracked-entity state at each checkpoint. `serial`
// selects the serial-solve path (skips the ThreadPool) — used by the Layer 1
// parallel-vs-serial cross-check; defaults to the normal parallel path.
Trajectory runAndCapture(const Builder& build, const std::vector<int>& checkpoints,
                         bool serial = false) {
    World w;
    w.setSerialSolve(serial);
    std::vector<ecs::Entity> ents = build(w);
    Trajectory traj;
    size_t ci = 0;
    const int last = checkpoints.back();
    for (int step = 1; step <= last; ++step) {
        w.advance(DT);
        if (ci < checkpoints.size() && step == checkpoints[ci]) {
            Snapshot snap;
            snap.reserve(ents.size() * WORDS_PER_ENTITY);
            for (ecs::Entity e : ents) captureEntity(w, e, snap);
            traj.push_back(std::move(snap));
            ++ci;
        }
    }
    return traj;
}

// Assert two trajectories are bit-identical; on the first divergence, localize
// it to checkpoint / entity index / field.
void requireIdentical(const Trajectory& A, const Trajectory& B, const char* name) {
    REQUIRE(A.size() == B.size());
    REQUIRE(!A.empty());
    for (size_t c = 0; c < A.size(); ++c) {
        REQUIRE(A[c].size() == B[c].size());
        REQUIRE(A[c].size() > 0);
        for (size_t i = 0; i < A[c].size(); ++i) {
            if (A[c][i] != B[c][i]) {
                static const char* FIELD[WORDS_PER_ENTITY] = {
                    "pos.x","pos.y","pos.z","rot.x","rot.y","rot.z","rot.w",
                    "vel.x","vel.y","vel.z","omega.x","omega.y","omega.z"};
                INFO(name << ": divergence at checkpoint " << c
                     << ", entity #" << (i / WORDS_PER_ENTITY)
                     << ", field " << FIELD[i % WORDS_PER_ENTITY]
                     << " (0x" << std::hex << A[c][i] << " vs 0x" << B[c][i] << ")");
                CHECK(A[c][i] == B[c][i]);
                return;   // report the first divergence only
            }
        }
    }
    SUCCEED("bit-identical across all checkpoints: " << name);
}

// ---- Scenes (all constructed identically each call → identical entity ids) ---

std::vector<ecs::Entity> sceneBoxPyramid(World& w) {
    w.addStaticAABB({0, -0.5f, 0}, {20, 0.5f, 20});
    const Vec3 ext{0.5f, 0.5f, 0.5f};
    std::vector<ecs::Entity> e;
    e.push_back(w.addOBB(ext, 1.0f, {-1.05f, 0.5f, 0}));
    e.push_back(w.addOBB(ext, 1.0f, { 0.00f, 0.5f, 0}));
    e.push_back(w.addOBB(ext, 1.0f, { 1.05f, 0.5f, 0}));
    e.push_back(w.addOBB(ext, 1.0f, {-0.525f, 1.5f, 0}));
    e.push_back(w.addOBB(ext, 1.0f, { 0.525f, 1.5f, 0}));
    e.push_back(w.addOBB(ext, 1.0f, { 0.0f,   2.5f, 0}));
    return e;
}

std::vector<ecs::Entity> sceneSpherePile(World& w) {
    w.addStaticAABB({0, -0.5f, 0}, {20, 0.5f, 20});
    std::vector<ecs::Entity> e;
    for (int i = 0; i < 12; ++i)
        e.push_back(w.addSphere(1.0f, 0.5f,
            { (i % 4) * 0.6f - 1.0f, 1.0f + (i / 4) * 1.1f, ((i % 3) - 1) * 0.4f }));
    return e;
}

std::vector<ecs::Entity> scenePointJointPendulum(World& w) {
    // Fixed anchor (static AABB = Fixed, mass 0) + a bob hung off it by a point
    // joint, swinging under gravity.
    ecs::Entity anchor = w.addStaticAABB({0, 5, 0}, {0.3f, 0.3f, 0.3f});
    ecs::Entity bob    = w.addSphere(1.0f, 0.5f, {1.5f, 5, 0});
    w.addPointJoint(anchor, bob, {0, 5, 0});
    return { bob };   // anchor is fixed; track the moving body
}

std::vector<ecs::Entity> sceneKinematicRider(World& w) {
    ecs::Entity plat = w.addKinematicBox({0, 0.5f, 0}, {2.0f, 0.25f, 2.0f});
    w.getRegistry().get<ecs::Hull>(plat)->velocity = {0, 0.4f, 0};   // rises steadily
    ecs::Entity box = w.addOBB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 1.3f, 0});
    return { plat, box };
}

// Many separated 2-body clusters resting on ONE shared static floor → many
// islands that all reference the (read-only) floor. This is the scene that
// stresses the parallel island dispatch — the race-catching one.
std::vector<ecs::Entity> sceneManyIslands(World& w) {
    w.addStaticAABB({0, -0.5f, 0}, {80, 0.5f, 80});
    std::vector<ecs::Entity> e;
    for (int i = 0; i < 16; ++i) {
        float x = i * 3.0f - 24.0f;
        e.push_back(w.addSphere(1.0f, 0.5f, {x - 0.2f, 0.55f, 0}));
        e.push_back(w.addSphere(1.0f, 0.5f, {x + 0.2f, 0.55f, 0}));
    }
    return e;
}

// ---- Joints: hinge (free + angle-limited/driven), cone-twist, multi-joint ----

std::vector<ecs::Entity> sceneHingeFree(World& w) {
    // Arm on a hinge about world Z, swinging as a pendulum under gravity.
    ecs::Entity anchor = w.addStaticAABB({0, 5, 0}, {0.3f, 0.3f, 0.3f});
    ecs::Entity arm    = w.addOBB({0.5f, 0.2f, 0.2f}, 1.0f, {1.2f, 5, 0});
    w.addHingeJoint(anchor, arm, {0, 5, 0}, {0, 0, 1});
    return { arm };
}

std::vector<ecs::Entity> sceneHingeLimited(World& w) {
    // Same hinge, but with a hard angle limit and a strong initial spin so the
    // arm slams into the limit — exercises the limit clamp row in the solver.
    ecs::Entity anchor = w.addStaticAABB({0, 5, 0}, {0.3f, 0.3f, 0.3f});
    ecs::Entity arm    = w.addOBB({0.5f, 0.2f, 0.2f}, 1.0f, {1.2f, 5, 0});
    w.getRegistry().get<ecs::Hull>(arm)->omega = {0, 0, 8.0f};   // set before the joint add
    w.addHingeJoint(anchor, arm, {0, 5, 0}, {0, 0, 1}, /*limitEnabled=*/true, -0.3f, 0.3f);
    return { arm };
}

std::vector<ecs::Entity> sceneConeTwist(World& w) {
    // A bone hung below the anchor, nudged sideways (into the swing cone) and
    // spun about its own axis (into the twist limit).
    ecs::Entity anchor = w.addStaticAABB({0, 5, 0}, {0.3f, 0.3f, 0.3f});
    ecs::Entity bone   = w.addOBB({0.2f, 0.6f, 0.2f}, 1.0f, {0, 4.0f, 0});
    auto* h = w.getRegistry().get<ecs::Hull>(bone);
    h->velocity = {1.0f, 0.0f, 0.0f};
    h->omega    = {0.0f, 3.0f, 0.0f};
    w.addConeTwistJoint(anchor, bone, {0, 5, 0}, {0, 1, 0}, /*swing=*/0.5f, /*twist=*/0.5f);
    return { bone };
}

std::vector<ecs::Entity> scenePointJointChain(World& w) {
    // Three bodies chained off a fixed anchor by point joints — multiple joints
    // interacting in one island (ragdoll-shaped solve).
    ecs::Entity anchor = w.addStaticAABB({0, 6, 0}, {0.3f, 0.3f, 0.3f});
    ecs::Entity b1 = w.addSphere(1.0f, 0.3f, {1.0f, 6, 0});
    ecs::Entity b2 = w.addSphere(1.0f, 0.3f, {2.0f, 6, 0});
    ecs::Entity b3 = w.addSphere(1.0f, 0.3f, {3.0f, 6, 0});
    w.addPointJoint(anchor, b1, {0.5f, 6, 0});
    w.addPointJoint(b1, b2, {1.5f, 6, 0});
    w.addPointJoint(b2, b3, {2.5f, 6, 0});
    return { b1, b2, b3 };
}

// ---- Springs (soft force applied post-solve in advance() step 5) ------------

std::vector<ecs::Entity> sceneSpringBob(World& w) {
    // A bob hung 2 m below the anchor by a stiff spring (rest 1 m) → oscillates.
    ecs::Entity anchor = w.addStaticAABB({0, 5, 0}, {0.3f, 0.3f, 0.3f});
    ecs::Entity bob    = w.addSphere(1.0f, 0.5f, {0, 3.0f, 0});
    w.addSpring(anchor, bob, /*k=*/50.0f, /*rest=*/1.0f);
    return { bob };
}

std::vector<ecs::Entity> sceneSpringChain(World& w) {
    // A hanging chain of three masses on springs off a fixed anchor.
    ecs::Entity anchor = w.addStaticAABB({0, 6, 0}, {0.3f, 0.3f, 0.3f});
    ecs::Entity b1 = w.addSphere(1.0f, 0.3f, {0, 5.0f, 0});
    ecs::Entity b2 = w.addSphere(1.0f, 0.3f, {0, 4.0f, 0});
    ecs::Entity b3 = w.addSphere(1.0f, 0.3f, {0, 3.0f, 0});
    w.addSpring(anchor, b1, 40.0f, 1.0f);
    w.addSpring(b1, b2, 40.0f, 1.0f);
    w.addSpring(b2, b3, 40.0f, 1.0f);
    return { b1, b2, b3 };
}

} // namespace

// ============================================================================
// Sanity: the harness actually detects divergence (not vacuously green)
// ============================================================================

TEST_CASE("determinism harness detects a perturbed run as different", "[determinism][meta]") {
    const std::vector<int> cp{50, 200};
    Trajectory base = runAndCapture(sceneSpherePile, cp);
    Trajectory perturbed = runAndCapture([](World& w) {
        w.addStaticAABB({0, -0.5f, 0}, {20, 0.5f, 20});
        std::vector<ecs::Entity> e;
        for (int i = 0; i < 12; ++i)
            e.push_back(w.addSphere(1.0f, 0.5f,
                { (i % 4) * 0.6f - 1.0f + (i == 0 ? 1e-4f : 0.0f),   // nudge one body
                  1.0f + (i / 4) * 1.1f, ((i % 3) - 1) * 0.4f }));
        return e;
    }, cp);

    // At least one checkpoint word must differ — else the comparison is blind.
    bool anyDiff = false;
    for (size_t c = 0; c < base.size() && !anyDiff; ++c)
        for (size_t i = 0; i < base[c].size(); ++i)
            if (base[c][i] != perturbed[c][i]) { anyDiff = true; break; }
    CHECK(anyDiff);
}

// ============================================================================
// Bit-exact run-to-run reproducibility, per scene (the real check)
// ============================================================================

TEST_CASE("box pyramid replays bit-identically", "[determinism][stack]") {
    const std::vector<int> cp{120, 600, 1200};
    requireIdentical(runAndCapture(sceneBoxPyramid, cp),
                     runAndCapture(sceneBoxPyramid, cp), "box pyramid");
}

TEST_CASE("sphere pile replays bit-identically", "[determinism][pile]") {
    const std::vector<int> cp{120, 600, 1200};
    requireIdentical(runAndCapture(sceneSpherePile, cp),
                     runAndCapture(sceneSpherePile, cp), "sphere pile");
}

TEST_CASE("point-joint pendulum replays bit-identically", "[determinism][joint]") {
    const std::vector<int> cp{120, 600, 1500};
    requireIdentical(runAndCapture(scenePointJointPendulum, cp),
                     runAndCapture(scenePointJointPendulum, cp), "point-joint pendulum");
}

TEST_CASE("kinematic platform + rider replays bit-identically", "[determinism][kinematic]") {
    const std::vector<int> cp{120, 480, 960};
    requireIdentical(runAndCapture(sceneKinematicRider, cp),
                     runAndCapture(sceneKinematicRider, cp), "kinematic rider");
}

// ============================================================================
// Race amplification: the parallel island dispatch, run many times.
// Every repetition must match the first, bit-for-bit. A scheduling-dependent
// race in the thread-pool solve would surface here intermittently.
// ============================================================================

TEST_CASE("many-island parallel solve is reproducible across repetitions", "[determinism][parallel][race]") {
    const std::vector<int> cp{120, 400, 900};
    Trajectory ref = runAndCapture(sceneManyIslands, cp);
    for (int rep = 1; rep <= 8; ++rep) {
        INFO("repetition " << rep);
        requireIdentical(ref, runAndCapture(sceneManyIslands, cp), "many islands");
    }
}

// ============================================================================
// Joint & spring feature coverage — each constraint type through the real
// island solve, checked for bit-exact reproducibility.
// ============================================================================

TEST_CASE("hinge joint (free swing) replays bit-identically", "[determinism][joint][hinge]") {
    const std::vector<int> cp{120, 600, 1500};
    requireIdentical(runAndCapture(sceneHingeFree, cp),
                     runAndCapture(sceneHingeFree, cp), "hinge free");
}

TEST_CASE("hinge joint (angle-limited, driven into the limit) replays bit-identically",
          "[determinism][joint][hinge][limit]") {
    const std::vector<int> cp{60, 240, 720};
    requireIdentical(runAndCapture(sceneHingeLimited, cp),
                     runAndCapture(sceneHingeLimited, cp), "hinge limited");
}

TEST_CASE("cone-twist joint replays bit-identically", "[determinism][joint][conetwist]") {
    const std::vector<int> cp{120, 600, 1500};
    requireIdentical(runAndCapture(sceneConeTwist, cp),
                     runAndCapture(sceneConeTwist, cp), "cone-twist");
}

TEST_CASE("point-joint chain (multi-joint island) replays bit-identically",
          "[determinism][joint][chain]") {
    const std::vector<int> cp{120, 600, 1500};
    requireIdentical(runAndCapture(scenePointJointChain, cp),
                     runAndCapture(scenePointJointChain, cp), "point-joint chain");
}

TEST_CASE("spring (bouncing bob) replays bit-identically", "[determinism][spring]") {
    const std::vector<int> cp{120, 600, 1500};
    requireIdentical(runAndCapture(sceneSpringBob, cp),
                     runAndCapture(sceneSpringBob, cp), "spring bob");
}

TEST_CASE("spring chain replays bit-identically", "[determinism][spring][chain]") {
    const std::vector<int> cp{120, 600, 1500};
    requireIdentical(runAndCapture(sceneSpringChain, cp),
                     runAndCapture(sceneSpringChain, cp), "spring chain");
}

// ============================================================================
// Layer 1 — serial vs parallel equivalence.
//
// Reuses EVERY Layer 0 scene (one table below) rather than re-authoring any:
// the property under test is orthogonal to the scene, so the same contact /
// joint / spring / kinematic / many-island coverage that Layer 0 built now does
// double duty. The parallel path (default) and the serial-solve path
// (World::setSerialSolve) must produce bit-identical trajectories. If they do,
// island independence is confirmed empirically (§10b's "serial and parallel
// differ" caution is then too conservative); a divergence would mean the
// parallel solve touches cross-island shared state the serial one serializes.
// ============================================================================

namespace {
struct NamedScene { const char* name; Builder build; std::vector<int> checkpoints; };

std::vector<NamedScene> allScenes() {
    return {
        {"box pyramid",          sceneBoxPyramid,          {120, 600, 1200}},
        {"sphere pile",          sceneSpherePile,          {120, 600, 1200}},
        {"point-joint pendulum", scenePointJointPendulum,  {120, 600, 1500}},
        {"kinematic rider",      sceneKinematicRider,      {120, 480,  960}},
        {"many islands",         sceneManyIslands,         {120, 400,  900}},
        {"hinge free",           sceneHingeFree,           {120, 600, 1500}},
        {"hinge limited",        sceneHingeLimited,        { 60, 240,  720}},
        {"cone-twist",           sceneConeTwist,           {120, 600, 1500}},
        {"point-joint chain",    scenePointJointChain,     {120, 600, 1500}},
        {"spring bob",           sceneSpringBob,           {120, 600, 1500}},
        {"spring chain",         sceneSpringChain,         {120, 600, 1500}},
    };
}
} // namespace

TEST_CASE("serial solve == parallel solve, bit-identical, across every scene",
          "[determinism][parallel][serial]") {
    for (const auto& s : allScenes()) {
        INFO("scene: " << s.name);
        Trajectory parallel = runAndCapture(s.build, s.checkpoints, /*serial=*/false);
        Trajectory serial   = runAndCapture(s.build, s.checkpoints, /*serial=*/true);
        requireIdentical(parallel, serial, s.name);
    }
}

TEST_CASE("serial solve is itself reproducible run-to-run, across every scene",
          "[determinism][serial]") {
    // §10b makes serial the canonical headless path, so its own determinism
    // matters independently of the parallel comparison above.
    for (const auto& s : allScenes()) {
        INFO("scene: " << s.name);
        requireIdentical(runAndCapture(s.build, s.checkpoints, /*serial=*/true),
                         runAndCapture(s.build, s.checkpoints, /*serial=*/true), s.name);
    }
}
