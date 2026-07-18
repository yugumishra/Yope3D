#pragma once
#include <string>

// ---------------------------------------------------------------------------
// TaskProgress
//
// Tiny global registry so any time-intensive background task (texture
// streaming today; future asset imports, bakes, etc.) can report progress for
// display in the editor's menu bar status strip. Thread-safe: report()/end()
// may be called from a worker thread, current() is read from the UI thread.
// ---------------------------------------------------------------------------

namespace TaskProgress {

struct Snapshot {
    bool        active = false;
    std::string label;
    int         completed = 0;
    int         total     = 0;
    double      elapsedSeconds = 0.0;
};

// Reports (completed, total) for a named task, starting its elapsed-time clock
// the first time `label` is reported (or re-reported after an end()). Safe to
// call every frame with the same label as progress advances.
void report(const std::string& label, int completed, int total);

// Clears the task so current() reports inactive again.
void end(const std::string& label);

// Snapshot of the currently active task, if any.
Snapshot current();

} // namespace TaskProgress
