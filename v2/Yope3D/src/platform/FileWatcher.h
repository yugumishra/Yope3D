#pragma once
#ifdef YOPE_EDITOR
#include <functional>
#include <string>
#include <thread>

// Watches a directory recursively and fires a callback when any file changes.
// Runs on a background thread. Thread-safe callback delivery.
class FileWatcher {
public:
    using Callback = std::function<void(const std::string& changedAbsPath)>;

    FileWatcher() = default;
    ~FileWatcher() { stop(); }

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // Start watching dir. Callback is fired from a background thread.
    void watch(const std::string& dir, Callback cb);

    // Stop watching. Blocks until the background thread exits.
    void stop();

private:
    void run(std::string dir, Callback cb);

    std::thread thread_;
    bool        running_ = false;
#if defined(__APPLE__)
    void* runLoop_ = nullptr;  // CFRunLoopRef (void* to avoid CoreFoundation in header)
#elif defined(_WIN32)
    void* stopEvent_ = nullptr;  // HANDLE
#endif
};
#endif
