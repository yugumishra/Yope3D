#pragma once

// Per-stage timing instrumentation — debug builds only.
// In release (NDEBUG defined) all macros expand to nothing, zero overhead.
//
// Usage:
//   YOPE_PROF_INIT("yope_profile.csv");          // once at startup
//   YOPE_PROF_SET_SCENE("Stress Test");           // call on every scene load
//   YOPE_PROF_SET_OBJECT_COUNT(n);                // call each physics step
//   YOPE_PROF_STEP("physics");                    // increment step counter
//   { YOPE_PROF_SCOPE("broadphase_sap", "physics"); ... }
//   YOPE_PROF_SHUTDOWN();                         // flush + close on exit
//
// CSV columns: thread, step, stage, duration_us, timestamp_s, scene, object_count

#ifndef NDEBUG

#include <chrono>

namespace Profiler {

void init    (const char* outputPath);
void shutdown();
void flush   ();
void advanceStep    (const char* thread);
void setScene       (const char* name);   // string literal only — pointer stored directly
void setObjectCount (int count);
void setIslandCount (int count);

struct Scope {
    Scope(const char* stage, const char* thread);
    ~Scope();
private:
    const char* stage_;
    const char* thread_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace Profiler

#define YOPE_PROF_INIT(path)                ::Profiler::init(path)
#define YOPE_PROF_SHUTDOWN()                ::Profiler::shutdown()
#define YOPE_PROF_STEP(thread)              ::Profiler::advanceStep(thread)
#define YOPE_PROF_SCOPE(stage, thread)      ::Profiler::Scope _ypscope_##__LINE__{stage, thread}
#define YOPE_PROF_SET_SCENE(name)           ::Profiler::setScene(name)
#define YOPE_PROF_SET_OBJECT_COUNT(n)       ::Profiler::setObjectCount(static_cast<int>(n))
#define YOPE_PROF_SET_ISLAND_COUNT(n)       ::Profiler::setIslandCount(static_cast<int>(n))

#else // NDEBUG

#define YOPE_PROF_INIT(path)
#define YOPE_PROF_SHUTDOWN()
#define YOPE_PROF_STEP(thread)
#define YOPE_PROF_SCOPE(stage, thread)
#define YOPE_PROF_SET_SCENE(name)
#define YOPE_PROF_SET_OBJECT_COUNT(n)
#define YOPE_PROF_SET_ISLAND_COUNT(n)

#endif // NDEBUG
