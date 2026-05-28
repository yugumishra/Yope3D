#include "Profiler.h"

#ifndef NDEBUG

#include <vector>
#include <mutex>
#include <fstream>
#include <string>
#include <atomic>
#include <ctime>

namespace Profiler {

namespace {

struct Record {
    const char* thread;
    const char* stage;
    uint64_t    step;
    double      duration_us;
    double      timestamp_s;
    const char* scene;
    int         object_count;
    int         island_count;
};

constexpr size_t FLUSH_THRESHOLD = 500;

std::mutex              g_mutex;
std::vector<Record>     g_buffer;
std::ofstream           g_file;
std::atomic<uint64_t>   g_physicsStep{0};
std::atomic<uint64_t>   g_renderStep{0};
std::atomic<const char*> g_scene{"unknown"};
std::atomic<int>         g_objectCount{0};
std::atomic<int>         g_islandCount{0};
std::chrono::high_resolution_clock::time_point g_epoch;

uint64_t stepFor(const char* thread) {
    return (thread[0] == 'p') ? g_physicsStep.load(std::memory_order_relaxed)
                               : g_renderStep .load(std::memory_order_relaxed);
}

void writeRecords(const std::vector<Record>& records) {
    for (const auto& r : records) {
        g_file << r.thread        << ','
               << r.step          << ','
               << r.stage         << ','
               << r.duration_us   << ','
               << r.timestamp_s   << ','
               << r.scene         << ','
               << r.object_count  << ','
               << r.island_count  << '\n';
    }
    g_file.flush();
}

} // anonymous namespace

void init(const char* outputPath) {
    std::lock_guard<std::mutex> lk(g_mutex);

    std::string path = outputPath;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".csv")
        path = path.substr(0, path.size() - 4);
    path += '_';
    path += std::to_string(static_cast<long long>(std::time(nullptr)));
    path += ".csv";

    g_file.open(path, std::ios::out | std::ios::trunc);
    if (!g_file.is_open()) return;

    g_file << "thread,step,stage,duration_us,timestamp_s,scene,object_count,island_count\n";
    g_file.flush();

    g_buffer.reserve(FLUSH_THRESHOLD * 2);
    g_epoch = std::chrono::high_resolution_clock::now();
}

void shutdown() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_file.is_open()) return;
    writeRecords(g_buffer);
    g_buffer.clear();
    g_file.close();
}

void flush() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_file.is_open() || g_buffer.empty()) return;
    writeRecords(g_buffer);
    g_buffer.clear();
}

void advanceStep(const char* thread) {
    if (thread[0] == 'p') g_physicsStep.fetch_add(1, std::memory_order_relaxed);
    else                   g_renderStep .fetch_add(1, std::memory_order_relaxed);
}

void setScene(const char* name) {
    g_scene.store(name, std::memory_order_relaxed);
}

void setObjectCount(int count) {
    g_objectCount.store(count, std::memory_order_relaxed);
}

void setIslandCount(int count) {
    g_islandCount.store(count, std::memory_order_relaxed);
}

// ---- Scope ----

Scope::Scope(const char* stage, const char* thread)
    : stage_(stage), thread_(thread)
    , start_(std::chrono::high_resolution_clock::now())
{}

Scope::~Scope() {
    auto end = std::chrono::high_resolution_clock::now();
    double   duration_us  = std::chrono::duration<double, std::micro>(end - start_).count();
    double   timestamp_s  = std::chrono::duration<double>(end - g_epoch).count();
    uint64_t step         = stepFor(thread_);
    const char* scene     = g_scene.load(std::memory_order_relaxed);
    int      object_count = g_objectCount.load(std::memory_order_relaxed);
    int      island_count = g_islandCount.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_file.is_open()) return;
    g_buffer.push_back({thread_, stage_, step, duration_us, timestamp_s, scene, object_count, island_count});
    if (g_buffer.size() >= FLUSH_THRESHOLD)
        writeRecords(g_buffer), g_buffer.clear();
}

} // namespace Profiler

#endif // !NDEBUG
