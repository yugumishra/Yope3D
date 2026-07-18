#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <cstdint>

enum class LogSeverity : uint8_t { Info = 0, Warning, Error };

// Thread-safe in-process log sink.
// The editor ConsolePanel reads from it each frame.
// The runtime routes entries to stderr.
class Console {
public:
    struct Entry {
        std::string msg;
        LogSeverity severity;
    };

    static void log(const char* msg, LogSeverity sev = LogSeverity::Info);
    static void log(const std::string& msg, LogSeverity sev = LogSeverity::Info);

    static std::mutex            mutex_;
    static std::deque<Entry>     entries_;
    static constexpr size_t      kMaxEntries = 10000;
};
