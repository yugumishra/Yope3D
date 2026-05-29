#pragma once
#include "editor/EditorPanel.h"
#include <string>
#include <deque>
#include <mutex>
#include <cstdint>

enum class LogSeverity : uint8_t { Info = 0, Warning, Error };

// Static log sink — call Console::log() from anywhere (thread-safe).
// ConsolePanel reads from it each frame.
class Console {
public:
    struct Entry {
        std::string   msg;
        LogSeverity   severity;
    };

    static void log(const char* msg, LogSeverity sev = LogSeverity::Info);
    static void log(const std::string& msg, LogSeverity sev = LogSeverity::Info);

    static std::mutex              mutex_;
    static std::deque<Entry>       entries_;
    static constexpr size_t        kMaxEntries = 10000;
};

class ConsolePanel : public EditorPanel {
public:
    const char* name() const override { return "Console"; }
    void draw(EditorContext& ctx) override;

private:
    bool scrollToBottom_ = true;
    int  filterSeverity_ = 0;   // 0 = all, 1 = warnings+, 2 = errors only
};
