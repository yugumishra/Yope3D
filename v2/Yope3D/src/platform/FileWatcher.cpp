#include "platform/FileWatcher.h"
#ifdef YOPE_EDITOR

#if defined(__APPLE__)

#include <CoreServices/CoreServices.h>

static void fsEventsCallback(ConstFSEventStreamRef /*stream*/,
                             void* clientCallBackInfo,
                             size_t numEvents,
                             void* eventPaths,
                             const FSEventStreamEventFlags* /*eventFlags*/,
                             const FSEventStreamEventId* /*eventIds*/)
{
    auto* cb = static_cast<FileWatcher::Callback*>(clientCallBackInfo);
    char** paths = static_cast<char**>(eventPaths);
    for (size_t i = 0; i < numEvents; ++i)
        (*cb)(paths[i]);
}

void FileWatcher::watch(const std::string& dir, Callback cb) {
    stop();
    running_ = true;
    thread_ = std::thread([this, dir, cb = std::move(cb)]() mutable {
        // Copy callback onto heap so we can pass as clientCallBackInfo
        auto* cbPtr = new Callback(std::move(cb));

        CFStringRef pathStr = CFStringCreateWithCString(nullptr, dir.c_str(), kCFStringEncodingUTF8);
        CFArrayRef  paths   = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&pathStr), 1, nullptr);

        FSEventStreamContext ctx{};
        ctx.info = cbPtr;

        FSEventStreamRef stream = FSEventStreamCreate(
            nullptr,
            &fsEventsCallback,
            &ctx,
            paths,
            kFSEventStreamEventIdSinceNow,
            0.5,   // latency seconds
            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);

        CFRelease(paths);
        CFRelease(pathStr);

        CFRunLoopRef rl = CFRunLoopGetCurrent();
        runLoop_ = rl;

        FSEventStreamScheduleWithRunLoop(stream, rl, kCFRunLoopDefaultMode);
        FSEventStreamStart(stream);

        CFRunLoopRun();

        FSEventStreamStop(stream);
        FSEventStreamUnscheduleFromRunLoop(stream, rl, kCFRunLoopDefaultMode);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        delete cbPtr;
        runLoop_ = nullptr;
    });
}

void FileWatcher::stop() {
    if (!running_) return;
    running_ = false;
    if (runLoop_) CFRunLoopStop(static_cast<CFRunLoopRef>(runLoop_));
    if (thread_.joinable()) thread_.join();
}

#elif defined(_WIN32)

#include <windows.h>

void FileWatcher::watch(const std::string& dir, Callback cb) {
    stop();
    running_ = true;
    stopEvent_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this, dir, cb = std::move(cb)]() mutable {
        HANDLE hDir = CreateFileA(
            dir.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (hDir == INVALID_HANDLE_VALUE) return;

        char buf[65536];
        OVERLAPPED ov{};
        ov.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        HANDLE events[2] = {ov.hEvent, static_cast<HANDLE>(stopEvent_)};

        while (running_) {
            DWORD bytes = 0;
            ReadDirectoryChangesW(hDir,
                buf, sizeof(buf), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, &ov, nullptr);

            DWORD idx = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (idx == WAIT_OBJECT_0 + 1) break;  // stop event

            if (GetOverlappedResult(hDir, &ov, &bytes, FALSE) && bytes > 0) {
                auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf);
                while (true) {
                    char name[MAX_PATH];
                    WideCharToMultiByte(CP_UTF8, 0, info->FileName,
                                       info->FileNameLength / 2,
                                       name, sizeof(name), nullptr, nullptr);
                    std::string absPath = dir + "\\" + name;
                    cb(absPath);
                    if (info->NextEntryOffset == 0) break;
                    info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                        reinterpret_cast<char*>(info) + info->NextEntryOffset);
                }
            }
        }
        CloseHandle(ov.hEvent);
        CloseHandle(hDir);
    });
}

void FileWatcher::stop() {
    if (!running_) return;
    running_ = false;
    if (stopEvent_) SetEvent(static_cast<HANDLE>(stopEvent_));
    if (thread_.joinable()) thread_.join();
    if (stopEvent_) { CloseHandle(static_cast<HANDLE>(stopEvent_)); stopEvent_ = nullptr; }
}

#else

// Linux / other: stub (inotify deferred per plan)
void FileWatcher::watch(const std::string& /*dir*/, Callback /*cb*/) {}
void FileWatcher::stop() { if (thread_.joinable()) thread_.join(); }

#endif
#endif // YOPE_EDITOR
