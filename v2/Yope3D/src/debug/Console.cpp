#include "debug/Console.h"
#include <cstdio>

std::mutex            Console::mutex_;
std::deque<Console::Entry> Console::entries_;

void Console::log(const char* msg, LogSeverity sev) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (entries_.size() >= kMaxEntries)
            entries_.pop_front();
        entries_.push_back({ msg, sev });
    }
    // Runtime also echoes to stderr so non-editor builds see Python output.
#ifndef YOPE_EDITOR
    const char* prefix = (sev == LogSeverity::Error)   ? "[error] "
                        : (sev == LogSeverity::Warning) ? "[warn]  "
                                                        : "";
    std::fprintf(stderr, "%s%s\n", prefix, msg);
#endif
}

void Console::log(const std::string& msg, LogSeverity sev) {
    log(msg.c_str(), sev);
}
