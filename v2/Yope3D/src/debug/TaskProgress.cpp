#include "TaskProgress.h"
#include <mutex>
#include <chrono>

namespace TaskProgress {
namespace {

using clock = std::chrono::steady_clock;

std::mutex mtx;
bool       active = false;
std::string label;
int        completed = 0;
int        total = 0;
clock::time_point startTime;

} // namespace

void report(const std::string& newLabel, int newCompleted, int newTotal) {
    std::lock_guard<std::mutex> lk(mtx);
    if (!active || label != newLabel) {
        label = newLabel;
        startTime = clock::now();
    }
    active = true;
    completed = newCompleted;
    total = newTotal;
}

void end(const std::string& endedLabel) {
    std::lock_guard<std::mutex> lk(mtx);
    if (active && label == endedLabel) active = false;
}

Snapshot current() {
    std::lock_guard<std::mutex> lk(mtx);
    Snapshot s;
    s.active = active;
    if (active) {
        s.label = label;
        s.completed = completed;
        s.total = total;
        s.elapsedSeconds = std::chrono::duration<double>(clock::now() - startTime).count();
    }
    return s;
}

} // namespace TaskProgress
