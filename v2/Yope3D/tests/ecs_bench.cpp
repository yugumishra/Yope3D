// ECS iteration benchmark — the measurement artifact behind the archetype-ECS
// article. Four storage strategies run the same three-pass frame over
// byte-identical populations (same seed, same values, same math):
//
//   view      — registry.view<...>() per pass, the actual engine path
//   get       — the SAME registry, entities visited in shuffled order via
//               get<T>(). Storage is identical to `view`, so the gap is
//               traversal order (locality) plus get()'s lookup cost.
//   oop       — vector<unique_ptr<GameObject>> with each component a separate
//               heap allocation and virtual per-pass methods — the old-engine
//               design. Allocations are interleaved with freed decoys and the
//               pointer array is shuffled, so a clean heap can't accidentally
//               hand back near-contiguous objects and undersell the gap.
//   fat_aos   — vector<FatGameObject> with all components inline by value:
//               contiguous, but cold fields ride through the cache anyway.
//
// Entities carry a realistic complement: 3 hot components (Transform, Hull,
// Bounds) + 4 cold ones (name, render state, script slot, audio slot),
// mirrored exactly across all four layouts. The frame runs three passes over
// different subsets — integrate (Transform+Hull), bounds update
// (Transform+Bounds), snapshot publish (Transform -> external matrix buffer)
// — the way a real tick's stages are separated by solver work, so no layout
// gets to merge them.
//
// Two modes, both reported:
//   hot   — back-to-back frames, everything cache-resident: best case for
//           every layout (and the most flattering one for pointer chasing).
//   evict — a 32 MB buffer walk between frames models the render thread,
//           scripts, and UI evicting entity data each frame.
//
// A second bench times archetype migration (tag add/remove, memcpy of all 7
// components) against flipping Hull.asleep — the measured version of the
// Sleeping tag->flag story.
//
// Output: ecs_bench_sweep.csv + ecs_bench_migration.csv in the output dir
// (argv[1], default "."), consumed by tools/articleAnalysis/plot_ecs_bench.py.
//
// Release builds only: the binary refuses to run without NDEBUG, so a
// debug-build number can never end up in an article.

#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/Transform.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <pthread.h>
#endif

namespace {

constexpr float    DT                 = 1.0f / 240.0f;   // engine tick
constexpr float    GRAVITY_Y          = -9.81f;
constexpr uint32_t SEED               = 42;
constexpr size_t   UPDATES_PER_SAMPLE = 1u << 18;   // ~256k entity-updates per hot sample
constexpr size_t   MAX_EVICT_FRAMES   = 48;         // cap: eviction dominates runtime
constexpr int      WARMUP_SAMPLES     = 3;
constexpr int      TIMED_SAMPLES      = 11;

const std::vector<size_t> SWEEP_N = {
    1'000, 2'000, 4'000, 8'000, 16'000, 32'000,
    64'000, 128'000, 256'000, 512'000, 1'000'000,
};

// ---------------------------------------------------------------------------
// Components. Hot: Transform + ecs::Hull (the real engine types) + Bounds.
// Cold: the realistic baggage a game object carries. Mirrored byte-for-byte
// across every layout.
// ---------------------------------------------------------------------------

struct BoundsC     { math::Vec3 min, max; };
struct NameC       { char name[64] = {}; };
struct RenderStateC{ float state[24] = {}; };
struct ScriptSlotC { uint64_t slot[4] = {}; };
struct AudioSlotC  { float gain[8] = {}; };

// Snapshot destination — the render-facing model matrix, column-major.
struct Mat4Snap { float m[16]; };

// ---------------------------------------------------------------------------
// The three passes — shared inline functions so every contender provably runs
// identical math.
// ---------------------------------------------------------------------------

// Pass 1: semi-implicit Euler + quaternion derivative.
inline void passIntegrate(Transform& tf, ecs::Hull& h, float dt) {
    if (h.gravity) h.velocity.y += GRAVITY_Y * dt;
    h.velocity *= (1.0f - h.linearDamping * dt);
    h.omega    *= (1.0f - h.angularDamping * dt);
    tf.position += h.velocity * dt;

    math::Quat w{h.omega.x, h.omega.y, h.omega.z, 0.0f};
    math::Quat dq = w * tf.rotation;
    tf.rotation.x += 0.5f * dq.x * dt;
    tf.rotation.y += 0.5f * dq.y * dt;
    tf.rotation.z += 0.5f * dq.z * dt;
    tf.rotation.w += 0.5f * dq.w * dt;
    float len = std::sqrt(tf.rotation.x * tf.rotation.x + tf.rotation.y * tf.rotation.y +
                          tf.rotation.z * tf.rotation.z + tf.rotation.w * tf.rotation.w);
    tf.rotation.x /= len;
    tf.rotation.y /= len;
    tf.rotation.z /= len;
    tf.rotation.w /= len;
}

// Pass 2: world-space AABB from position + scale (broadphase feed).
inline void passBounds(const Transform& tf, BoundsC& b) {
    math::Vec3 half = tf.scale * 0.5f;
    b.min = tf.position - half;
    b.max = tf.position + half;
}

// Pass 3: compose TRS directly into the snapshot slot. Deliberately the cheap
// composition (quat->3x3, no generic mat4 multiplies) so the measurement stays
// about storage layout, not matrix-math throughput.
inline void passSnapshot(const Transform& tf, Mat4Snap& out) {
    const math::Quat& q = tf.rotation;
    float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    float xx = q.x * x2, yy = q.y * y2, zz = q.z * z2;
    float xy = q.x * y2, xz = q.x * z2, yz = q.y * z2;
    float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    const math::Vec3& s = tf.scale;
    out.m[0]  = (1.0f - (yy + zz)) * s.x;
    out.m[1]  = (xy + wz) * s.x;
    out.m[2]  = (xz - wy) * s.x;
    out.m[3]  = 0.0f;
    out.m[4]  = (xy - wz) * s.y;
    out.m[5]  = (1.0f - (xx + zz)) * s.y;
    out.m[6]  = (yz + wx) * s.y;
    out.m[7]  = 0.0f;
    out.m[8]  = (xz + wy) * s.z;
    out.m[9]  = (yz - wx) * s.z;
    out.m[10] = (1.0f - (xx + yy)) * s.z;
    out.m[11] = 0.0f;
    out.m[12] = tf.position.x;
    out.m[13] = tf.position.y;
    out.m[14] = tf.position.z;
    out.m[15] = 1.0f;
}

// ---------------------------------------------------------------------------
// Population — every contender replays the same RNG stream, so component
// values are byte-identical across storage strategies.
// ---------------------------------------------------------------------------

struct BodyInit {
    Transform tf;
    ecs::Hull hull;
};

BodyInit makeBody(std::mt19937& rng) {
    std::uniform_real_distribution<float> pos(-100.0f, 100.0f);
    std::uniform_real_distribution<float> vel(-10.0f, 10.0f);
    std::uniform_real_distribution<float> ang(-2.0f, 2.0f);
    BodyInit b;
    b.tf.position = {pos(rng), pos(rng), pos(rng)};
    b.hull.velocity = {vel(rng), vel(rng), vel(rng)};
    b.hull.omega    = {ang(rng), ang(rng), ang(rng)};
    return b;
}

// ---------------------------------------------------------------------------
// Contenders
// ---------------------------------------------------------------------------

struct ArchWorld {
    ecs::Registry reg;
    std::vector<ecs::Entity> entities;
    std::vector<Mat4Snap> snapshot;

    explicit ArchWorld(size_t n) : snapshot(n) {
        std::mt19937 rng(SEED);
        entities.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            BodyInit b = makeBody(rng);
            ecs::Entity e = reg.create();
            reg.add<Transform>(e, b.tf);
            reg.add<ecs::Hull>(e, b.hull);
            reg.add<BoundsC>(e);
            reg.add<NameC>(e);
            reg.add<RenderStateC>(e);
            reg.add<ScriptSlotC>(e);
            reg.add<AudioSlotC>(e);
            entities.push_back(e);
        }
    }

    void frame(float dt) {
        for (auto [e, tf, h] : reg.view<Transform, ecs::Hull>())
            passIntegrate(tf, h, dt);
        for (auto [e, tf, b] : reg.view<Transform, BoundsC>())
            passBounds(tf, b);
        size_t i = 0;
        for (auto [e, tf] : reg.view<Transform>())
            passSnapshot(tf, snapshot[i++]);
    }

    void frameGet(const std::vector<ecs::Entity>& order, float dt) {
        for (ecs::Entity e : order)
            passIntegrate(*reg.get<Transform>(e), *reg.get<ecs::Hull>(e), dt);
        for (ecs::Entity e : order)
            passBounds(*reg.get<Transform>(e), *reg.get<BoundsC>(e));
        size_t i = 0;
        for (ecs::Entity e : order)
            passSnapshot(*reg.get<Transform>(e), snapshot[i++]);
    }

    double checksum() {
        double sum = 0.0;
        for (auto [e, tf, b] : reg.view<Transform, BoundsC>())
            sum += tf.position.x + tf.position.y + tf.position.z + b.min.x + b.max.y;
        for (const auto& m : snapshot)
            sum += m.m[12] + m.m[13] + m.m[14];
        return sum;
    }
};

// The old-engine design: heap-scattered components, virtual per-pass dispatch.
struct OopComponent {
    virtual ~OopComponent() = default;
};
struct OopTransform : OopComponent { Transform tf; };
struct OopBody      : OopComponent { ecs::Hull hull; };
struct OopBounds    : OopComponent { BoundsC b; };
struct OopName      : OopComponent { NameC v; };
struct OopRender    : OopComponent { RenderStateC v; };
struct OopScript    : OopComponent { ScriptSlotC v; };
struct OopAudio     : OopComponent { AudioSlotC v; };

struct GameObject {
    std::vector<std::unique_ptr<OopComponent>> components;
    OopTransform* tf     = nullptr;   // cached, as the old engine did
    OopBody*      body   = nullptr;
    OopBounds*    bounds = nullptr;

    virtual ~GameObject() = default;
    virtual void updatePhysics(float dt) { passIntegrate(tf->tf, body->hull, dt); }
    virtual void updateBounds()          { passBounds(tf->tf, bounds->b); }
    virtual void writeSnapshot(Mat4Snap& out) { passSnapshot(tf->tf, out); }
};

struct OopWorld {
    std::vector<std::unique_ptr<GameObject>> objects;
    std::vector<Mat4Snap> snapshot;

    explicit OopWorld(size_t n) : snapshot(n) {
        std::mt19937 rng(SEED);
        // Separate stream: decoy draws must not perturb the body values,
        // which have to be byte-identical to the other contenders'.
        std::mt19937 decoyRng(SEED + 2);
        std::uniform_int_distribution<size_t> decoySize(16, 256);
        std::vector<std::unique_ptr<char[]>> decoys;
        decoys.reserve(n);
        objects.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            BodyInit b = makeBody(rng);
            auto obj = std::make_unique<GameObject>();
            auto tf = std::make_unique<OopTransform>();
            auto body = std::make_unique<OopBody>();
            auto bounds = std::make_unique<OopBounds>();
            tf->tf = b.tf;
            body->hull = b.hull;
            obj->tf = tf.get();
            obj->body = body.get();
            obj->bounds = bounds.get();
            obj->components.push_back(std::move(tf));
            obj->components.push_back(std::move(body));
            obj->components.push_back(std::move(bounds));
            obj->components.push_back(std::make_unique<OopName>());
            obj->components.push_back(std::make_unique<OopRender>());
            obj->components.push_back(std::make_unique<OopScript>());
            obj->components.push_back(std::make_unique<OopAudio>());
            objects.push_back(std::move(obj));
            // Interleaved decoy allocation keeps consecutive objects from
            // landing adjacent in a clean heap.
            decoys.push_back(std::make_unique<char[]>(decoySize(decoyRng)));
        }
        decoys.clear();
        // Traversal order must not match allocation order.
        std::mt19937 shuffleRng(SEED + 1);
        std::shuffle(objects.begin(), objects.end(), shuffleRng);
    }

    void frame(float dt) {
        for (auto& obj : objects)
            obj->updatePhysics(dt);
        for (auto& obj : objects)
            obj->updateBounds();
        size_t i = 0;
        for (auto& obj : objects)
            obj->writeSnapshot(snapshot[i++]);
    }

    double checksum() const {
        double sum = 0.0;
        for (const auto& obj : objects) {
            const Transform& tf = obj->tf->tf;
            const BoundsC& b = obj->bounds->b;
            sum += tf.position.x + tf.position.y + tf.position.z + b.min.x + b.max.y;
        }
        for (const auto& m : snapshot)
            sum += m.m[12] + m.m[13] + m.m[14];
        return sum;
    }
};

// Contiguous, but every cold byte rides through the cache with the hot ones.
struct FatGameObject {
    Transform    tf;
    ecs::Hull    hull;
    BoundsC      bounds{};
    NameC        name;
    RenderStateC render;
    ScriptSlotC  script;
    AudioSlotC   audio;
};

struct FatWorld {
    std::vector<FatGameObject> objects;
    std::vector<Mat4Snap> snapshot;

    explicit FatWorld(size_t n) : snapshot(n) {
        std::mt19937 rng(SEED);
        objects.resize(n);
        for (auto& o : objects) {
            BodyInit b = makeBody(rng);
            o.tf = b.tf;
            o.hull = b.hull;
        }
    }

    void frame(float dt) {
        for (auto& o : objects)
            passIntegrate(o.tf, o.hull, dt);
        for (auto& o : objects)
            passBounds(o.tf, o.bounds);
        size_t i = 0;
        for (auto& o : objects)
            passSnapshot(o.tf, snapshot[i++]);
    }

    double checksum() const {
        double sum = 0.0;
        for (const auto& o : objects)
            sum += o.tf.position.x + o.tf.position.y + o.tf.position.z +
                   o.bounds.min.x + o.bounds.max.y;
        for (const auto& m : snapshot)
            sum += m.m[12] + m.m[13] + m.m[14];
        return sum;
    }
};

// ---------------------------------------------------------------------------
// Cache eviction — models the rest of the frame (render thread, scripts, UI)
// competing for cache between physics passes. 32 MB covers L2/SLC.
// ---------------------------------------------------------------------------

uint8_t g_evictSink = 0;

void evictCaches() {
    static std::vector<uint8_t> buf(32u << 20);
    for (size_t i = 0; i < buf.size(); i += 64)
        buf[i]++;
    g_evictSink += buf[0];
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

struct SampleResult {
    double nsPerEntity;
    double checksum;
};

// Runs warmup + timed samples of `frameFn` (one call = one 3-pass frame over N
// entities), returns median ns/entity/frame. Frame counts depend only on N and
// mode, so checksums are comparable across contenders. In evict mode the
// buffer walk runs between frames and is excluded from the timing.
template <typename FrameFn, typename ChecksumFn>
SampleResult timeContender(size_t n, bool evict, FrameFn&& frameFn,
                           ChecksumFn&& checksumFn) {
    const size_t framesPerSample =
        evict ? std::min<size_t>(MAX_EVICT_FRAMES,
                                 std::max<size_t>(1, UPDATES_PER_SAMPLE / n))
              : std::max<size_t>(1, UPDATES_PER_SAMPLE / n);
    std::vector<double> samples;
    samples.reserve(TIMED_SAMPLES);
    for (int s = 0; s < WARMUP_SAMPLES + TIMED_SAMPLES; ++s) {
        double ns = 0.0;
        for (size_t i = 0; i < framesPerSample; ++i) {
            if (evict) evictCaches();
            auto t0 = std::chrono::steady_clock::now();
            frameFn();
            auto t1 = std::chrono::steady_clock::now();
            ns += std::chrono::duration<double, std::nano>(t1 - t0).count();
        }
        if (s >= WARMUP_SAMPLES)
            samples.push_back(ns / (double(framesPerSample) * double(n)));
    }
    std::sort(samples.begin(), samples.end());
    return {samples[samples.size() / 2], checksumFn()};
}

// ---------------------------------------------------------------------------
// Machine metadata (cache geometry drives the cliff markers in the plot)
// ---------------------------------------------------------------------------

#ifdef __APPLE__
std::string sysctlString(const char* name) {
    char buf[256] = {};
    size_t len = sizeof(buf);
    if (sysctlbyname(name, buf, &len, nullptr, 0) != 0) return "unknown";
    return buf;
}
uint64_t sysctlU64(const char* name) {
    uint64_t v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, nullptr, 0) != 0) return 0;
    return v;
}
#endif

void writeMetadata(FILE* f) {
    fprintf(f, "# seed=%u dt=%.8f updates_per_sample=%zu timed_samples=%d\n",
            SEED, DT, UPDATES_PER_SAMPLE, TIMED_SAMPLES);
    fprintf(f, "# sizeof_transform=%zu sizeof_hull=%zu sizeof_bounds=%zu "
               "sizeof_mat4=%zu sizeof_fat_object=%zu components_per_entity=7\n",
            sizeof(Transform), sizeof(ecs::Hull), sizeof(BoundsC),
            sizeof(Mat4Snap), sizeof(FatGameObject));
#ifdef __APPLE__
    // Metadata values are space-separated key=value tokens; keep values
    // space-free (the plot script maps '_' back to ' ').
    std::string cpu = sysctlString("machdep.cpu.brand_string");
    std::replace(cpu.begin(), cpu.end(), ' ', '_');
    fprintf(f, "# cpu=%s memsize=%llu\n", cpu.c_str(),
            (unsigned long long)sysctlU64("hw.memsize"));
    fprintf(f, "# l1d=%llu l2=%llu\n",
            (unsigned long long)sysctlU64("hw.perflevel0.l1dcachesize"),
            (unsigned long long)sysctlU64("hw.perflevel0.l2cachesize"));
#endif
}

// ---------------------------------------------------------------------------
// The iteration sweep
// ---------------------------------------------------------------------------

bool checksumsMatch(double a, double b) {
    // Accumulation order differs between contenders (oop iterates shuffled),
    // so allow relative fp drift — anything larger means divergent workloads.
    double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
    return std::fabs(a - b) / scale < 1e-6;
}

int runSweep(const std::string& outPath) {
    FILE* f = fopen(outPath.c_str(), "w");
    if (!f) {
        fprintf(stderr, "cannot open %s for writing\n", outPath.c_str());
        return 1;
    }
    writeMetadata(f);
    fprintf(f, "layout,mode,n,ns_per_entity,entities_per_us,effective_gbps\n");

    // Useful bytes per entity per frame: pass 1 reads/writes Transform+Hull,
    // pass 2 Transform+Bounds, pass 3 Transform+matrix slot.
    const double hotBytes = 3.0 * sizeof(Transform) + sizeof(ecs::Hull) +
                            sizeof(BoundsC) + sizeof(Mat4Snap);

    for (size_t n : SWEEP_N) {
        for (bool evict : {false, true}) {
            const char* mode = evict ? "evict" : "hot";
            printf("N=%zu %s\n", n, mode);
            struct Row {
                const char* name;
                SampleResult r;
            };
            std::vector<Row> rows;

            {
                ArchWorld w(n);
                rows.push_back({"view", timeContender(
                    n, evict, [&] { w.frame(DT); }, [&] { return w.checksum(); })});
            }
            {
                ArchWorld w(n);
                std::vector<ecs::Entity> order = w.entities;
                std::mt19937 shuffleRng(SEED + 1);
                std::shuffle(order.begin(), order.end(), shuffleRng);
                rows.push_back({"get", timeContender(
                    n, evict, [&] { w.frameGet(order, DT); }, [&] { return w.checksum(); })});
            }
            {
                OopWorld w(n);
                rows.push_back({"oop", timeContender(
                    n, evict, [&] { w.frame(DT); }, [&] { return w.checksum(); })});
            }
            {
                FatWorld w(n);
                rows.push_back({"fat_aos", timeContender(
                    n, evict, [&] { w.frame(DT); }, [&] { return w.checksum(); })});
            }

            for (const auto& row : rows) {
                if (!checksumsMatch(row.r.checksum, rows[0].r.checksum)) {
                    fprintf(stderr,
                            "checksum mismatch at N=%zu (%s): %s=%.9g vs view=%.9g — "
                            "workloads have diverged, results are not comparable\n",
                            n, mode, row.name, row.r.checksum, rows[0].r.checksum);
                    fclose(f);
                    return 1;
                }
                double entitiesPerUs = 1000.0 / row.r.nsPerEntity;
                double gbps = hotBytes / row.r.nsPerEntity;   // bytes/ns == GB/s
                fprintf(f, "%s,%s,%zu,%.4f,%.2f,%.3f\n",
                        row.name, mode, n, row.r.nsPerEntity, entitiesPerUs, gbps);
                printf("  %-8s %8.2f ns/entity/frame  %8.1f entities/us  %6.2f GB/s\n",
                       row.name, row.r.nsPerEntity, entitiesPerUs, gbps);
            }
            fflush(f);
        }
    }
    fclose(f);
    printf("wrote %s\n", outPath.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// The migration bench — tag toggle vs. flag flip (the Sleeping story, measured)
// ---------------------------------------------------------------------------

struct BenchTag {};

int runMigrationBench(const std::string& outPath) {
    constexpr size_t N = 16'000;
    constexpr size_t TOGGLES = 100'000;

    FILE* f = fopen(outPath.c_str(), "w");
    if (!f) {
        fprintf(stderr, "cannot open %s for writing\n", outPath.c_str());
        return 1;
    }
    writeMetadata(f);
    fprintf(f, "mechanism,n,toggles,ns_per_toggle\n");

    ArchWorld w(N);
    std::mt19937 rng(SEED);
    std::uniform_int_distribution<size_t> pick(0, N - 1);

    // Tag toggle: each on/off pair is two archetype migrations, each of which
    // memcpy-moves all 7 components the entity owns.
    uint64_t migBefore = w.reg.archetypeMigrationCount();
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < TOGGLES; ++i) {
        ecs::Entity e = w.entities[pick(rng)];
        w.reg.add<BenchTag>(e);
        w.reg.remove<BenchTag>(e);
    }
    auto t1 = std::chrono::steady_clock::now();
    uint64_t migrations = w.reg.archetypeMigrationCount() - migBefore;
    if (migrations != 2 * TOGGLES) {
        fprintf(stderr, "expected %zu migrations, saw %llu\n",
                2 * TOGGLES, (unsigned long long)migrations);
        fclose(f);
        return 1;
    }
    double tagNs = std::chrono::duration<double, std::nano>(t1 - t0).count() / TOGGLES;

    // Flag flip: what Hull.asleep costs. No migrations run during this loop,
    // so caching Hull pointers up front is safe.
    std::vector<ecs::Hull*> hulls;
    hulls.reserve(N);
    for (ecs::Entity e : w.entities)
        hulls.push_back(w.reg.get<ecs::Hull>(e));
    std::mt19937 rng2(SEED);
    t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < TOGGLES; ++i) {
        ecs::Hull* h = hulls[pick(rng2)];
        h->asleep = !h->asleep;
        h->asleep = !h->asleep;   // pair, to mirror the tag on/off round trip
    }
    t1 = std::chrono::steady_clock::now();
    double flagNs = std::chrono::duration<double, std::nano>(t1 - t0).count() / TOGGLES;

    fprintf(f, "tag_migration,%zu,%zu,%.2f\n", N, TOGGLES, tagNs);
    fprintf(f, "flag_flip,%zu,%zu,%.4f\n", N, TOGGLES, flagNs);
    printf("migration bench: tag toggle %.1f ns, flag flip %.3f ns (x%.0f)\n",
           tagNs, flagNs, tagNs / flagNs);
    printf("wrote %s\n", outPath.c_str());
    fclose(f);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifndef NDEBUG
    fprintf(stderr,
            "yope_ecs_bench: this is a debug build — timings would be "
            "meaningless. Build and run from build/mac-release.\n");
    return 1;
#else
#ifdef __APPLE__
    // Prefer performance cores.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    std::string outDir = argc > 1 ? argv[1] : ".";
    if (int rc = runSweep(outDir + "/ecs_bench_sweep.csv"); rc != 0) return rc;
    return runMigrationBench(outDir + "/ecs_bench_migration.csv");
#endif
}
