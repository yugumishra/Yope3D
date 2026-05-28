#pragma once

// Per-stage timing instrumentation — debug builds only.
// In release (NDEBUG defined) all macros expand to nothing, zero overhead.
//
// Usage:
//   YOPE_PROF_INIT("yope_profile.csv");                  // once at startup
//   YOPE_PROF_SET_SCENE("Stress Test");                   // call on every scene load
//   YOPE_PROF_SET_OBJECT_COUNT(n);                        // call each physics step
//   YOPE_PROF_STEP("physics");                            // increment step counter
//   { YOPE_PROF_SCOPE("broadphase_sap", "physics"); ... } // anonymous scope
//   { YOPE_PROF_SCOPE_N("pgs_island", "physics", k); ... }// stamps scope_n=k
//   YOPE_PROF_EMIT("nphase_sph_sph", "physics", us, n);   // direct record emit
//   YOPE_PROF_SHUTDOWN();                                 // flush + close on exit
//
// CSV columns:
//   thread, step, stage, duration_us, timestamp_s, scene,
//   object_count, island_count, contact_count, archetype_count, archetype_migrations,
//   scope_n

#ifndef NDEBUG

#include <chrono>

namespace Profiler {

void init    (const char* outputPath);
void shutdown();
void flush   ();
void advanceStep            (const char* thread);
void setScene               (const char* name);   // string literal only — pointer stored directly
void setObjectCount         (int count);
void setIslandCount         (int count);
void setContactCount        (int count);
void setArchetypeCount      (int count);
void setArchetypeMigrations (int count);

// Direct record emission — for cases where a Scope object can't bracket the work
// (e.g. per shape-pair narrowphase timings aggregated inside detect() and emitted
// once after the loop). Does not increment the step counter.
void emitRecord(const char* stage, const char* thread, double duration_us, int scope_n);

struct Scope {
    Scope(const char* stage, const char* thread, int n = 0);
    ~Scope();
private:
    const char* stage_;
    const char* thread_;
    int         n_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace Profiler

// Token-pasting indirection so __LINE__ expands before concatenation.
#define _YOPE_PROF_CAT_INNER(a, b) a##b
#define _YOPE_PROF_CAT(a, b)       _YOPE_PROF_CAT_INNER(a, b)
#define _YOPE_PROF_VAR             _YOPE_PROF_CAT(_ypscope_, __LINE__)

#define YOPE_PROF_INIT(path)                ::Profiler::init(path)
#define YOPE_PROF_SHUTDOWN()                ::Profiler::shutdown()
#define YOPE_PROF_FLUSH()                   ::Profiler::flush()
#define YOPE_PROF_STEP(thread)              ::Profiler::advanceStep(thread)
#define YOPE_PROF_SCOPE(stage, thread)      ::Profiler::Scope _YOPE_PROF_VAR{stage, thread}
#define YOPE_PROF_SCOPE_N(stage, thread, n) ::Profiler::Scope _YOPE_PROF_VAR{stage, thread, static_cast<int>(n)}
#define YOPE_PROF_SET_SCENE(name)                  ::Profiler::setScene(name)
#define YOPE_PROF_SET_OBJECT_COUNT(n)              ::Profiler::setObjectCount(static_cast<int>(n))
#define YOPE_PROF_SET_ISLAND_COUNT(n)              ::Profiler::setIslandCount(static_cast<int>(n))
#define YOPE_PROF_SET_CONTACT_COUNT(n)             ::Profiler::setContactCount(static_cast<int>(n))
#define YOPE_PROF_SET_ARCHETYPE_COUNT(n)           ::Profiler::setArchetypeCount(static_cast<int>(n))
#define YOPE_PROF_SET_ARCHETYPE_MIGRATIONS(n)      ::Profiler::setArchetypeMigrations(static_cast<int>(n))
#define YOPE_PROF_EMIT(stage, thread, us, n)       ::Profiler::emitRecord(stage, thread, us, static_cast<int>(n))

#else // NDEBUG

#define YOPE_PROF_INIT(path)
#define YOPE_PROF_SHUTDOWN()
#define YOPE_PROF_FLUSH()
#define YOPE_PROF_STEP(thread)
#define YOPE_PROF_SCOPE(stage, thread)
#define YOPE_PROF_SCOPE_N(stage, thread, n)
#define YOPE_PROF_SET_SCENE(name)
#define YOPE_PROF_SET_OBJECT_COUNT(n)
#define YOPE_PROF_SET_ISLAND_COUNT(n)
#define YOPE_PROF_SET_CONTACT_COUNT(n)
#define YOPE_PROF_SET_ARCHETYPE_COUNT(n)
#define YOPE_PROF_SET_ARCHETYPE_MIGRATIONS(n)
#define YOPE_PROF_EMIT(stage, thread, us, n)

#endif // NDEBUG
