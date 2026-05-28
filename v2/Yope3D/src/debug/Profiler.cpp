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
    int         contact_count;
    int         archetype_count;
    int         archetype_migrations;
    int         scope_n;
};

// Thread-local buffer flushes to disk in batches; the global mutex is taken
// once per batch instead of once per scope. ~256 records ≈ one physics step's
// worth of scopes, so per-step latency cost is a single mutex acquire.
constexpr size_t TLS_FLUSH_SIZE  = 256;

std::mutex                   g_mutex;
std::ofstream                g_file;
std::atomic<uint64_t>        g_physicsStep{0};
std::atomic<uint64_t>        g_renderStep {0};
std::atomic<const char*>     g_scene{"unknown"};
std::atomic<int>             g_objectCount        {0};
std::atomic<int>             g_islandCount        {0};
std::atomic<int>             g_contactCount       {0};
std::atomic<int>             g_archetypeCount     {0};
std::atomic<int>             g_archetypeMigrations{0};
std::chrono::high_resolution_clock::time_point g_epoch;

uint64_t stepFor(const char* thread) {
    return (thread[0] == 'p') ? g_physicsStep.load(std::memory_order_relaxed)
                              : g_renderStep .load(std::memory_order_relaxed);
}

void writeRecords(const std::vector<Record>& records) {
    for (const auto& r : records) {
        g_file << r.thread               << ','
               << r.step                 << ','
               << r.stage                << ','
               << r.duration_us          << ','
               << r.timestamp_s          << ','
               << r.scene                << ','
               << r.object_count         << ','
               << r.island_count         << ','
               << r.contact_count        << ','
               << r.archetype_count      << ','
               << r.archetype_migrations << ','
               << r.scope_n              << '\n';
    }
    g_file.flush();
}

// Thread-local record buffer. Destructor (runs at thread exit) flushes any
// remaining records under the global mutex — keeps the main-thread mutex
// out of the per-scope hot path while still capturing data from worker
// threads that exit before shutdown().
struct ThreadLocalBuf {
    std::vector<Record> records;

    ThreadLocalBuf() {
        records.reserve(TLS_FLUSH_SIZE * 2);
    }
    ~ThreadLocalBuf() {
        if (records.empty()) return;
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_file.is_open()) writeRecords(records);
        records.clear();
    }
};

thread_local ThreadLocalBuf g_tlb;

void appendRecord(const Record& r) {
    g_tlb.records.push_back(r);
    if (g_tlb.records.size() >= TLS_FLUSH_SIZE) {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_file.is_open()) writeRecords(g_tlb.records);
        g_tlb.records.clear();
    }
}

Record makeRecord(const char* stage, const char* thread,
                  double duration_us, double timestamp_s, int scope_n) {
    return Record{
        thread, stage,
        stepFor(thread),
        duration_us, timestamp_s,
        g_scene.load(std::memory_order_relaxed),
        g_objectCount        .load(std::memory_order_relaxed),
        g_islandCount        .load(std::memory_order_relaxed),
        g_contactCount       .load(std::memory_order_relaxed),
        g_archetypeCount     .load(std::memory_order_relaxed),
        g_archetypeMigrations.load(std::memory_order_relaxed),
        scope_n
    };
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

    g_file << "thread,step,stage,duration_us,timestamp_s,scene,"
              "object_count,island_count,contact_count,"
              "archetype_count,archetype_migrations,scope_n\n";
    g_file.flush();

    g_epoch = std::chrono::high_resolution_clock::now();
}

void shutdown() {
    // Drain the main thread's TLB synchronously (other threads must already
    // have been joined — their TLBs flushed via ~ThreadLocalBuf on join).
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_file.is_open()) return;
    writeRecords(g_tlb.records);
    g_tlb.records.clear();
    g_file.close();
}

void flush() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_file.is_open()) return;
    writeRecords(g_tlb.records);
    g_tlb.records.clear();
}

void advanceStep(const char* thread) {
    if (thread[0] == 'p') g_physicsStep.fetch_add(1, std::memory_order_relaxed);
    else                  g_renderStep .fetch_add(1, std::memory_order_relaxed);
}

void setScene              (const char* n) { g_scene              .store(n, std::memory_order_relaxed); }
void setObjectCount        (int n)         { g_objectCount        .store(n, std::memory_order_relaxed); }
void setIslandCount        (int n)         { g_islandCount        .store(n, std::memory_order_relaxed); }
void setContactCount       (int n)         { g_contactCount       .store(n, std::memory_order_relaxed); }
void setArchetypeCount     (int n)         { g_archetypeCount     .store(n, std::memory_order_relaxed); }
void setArchetypeMigrations(int n)         { g_archetypeMigrations.store(n, std::memory_order_relaxed); }

void emitRecord(const char* stage, const char* thread, double duration_us, int scope_n) {
    double ts = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - g_epoch).count();
    appendRecord(makeRecord(stage, thread, duration_us, ts, scope_n));
}

// ---- Scope ----

Scope::Scope(const char* stage, const char* thread, int n)
    : stage_(stage), thread_(thread), n_(n)
    , start_(std::chrono::high_resolution_clock::now())
{}

Scope::~Scope() {
    auto end           = std::chrono::high_resolution_clock::now();
    double duration_us = std::chrono::duration<double, std::micro>(end - start_).count();
    double timestamp_s = std::chrono::duration<double>(end - g_epoch).count();
    appendRecord(makeRecord(stage_, thread_, duration_us, timestamp_s, n_));
}

} // namespace Profiler

#endif // !NDEBUG
