#include "platform/BundlePaths.h"
#include <filesystem>
#include <cstdlib>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <climits>

std::string bundleResourcesDir() {
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return "";

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path exe = fs::canonical(buf, ec);
    if (ec) return "";

    // Expect layout: Foo.app/Contents/MacOS/<binary>
    fs::path macosDir    = exe.parent_path();
    fs::path contentsDir = macosDir.parent_path();
    if (macosDir.filename() != "MacOS" || contentsDir.filename() != "Contents")
        return "";

    return (contentsDir / "Resources").string();
}

std::string writableDataDir() {
    const char* home = std::getenv("HOME");
    if (!home) return "";
    std::filesystem::path dir = std::filesystem::path(home) / "Library" / "Application Support" / "Yope3D";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

#elif defined(_WIN32)
std::string bundleResourcesDir() { return ""; }

std::string writableDataDir() {
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) return "";
    std::filesystem::path dir = std::filesystem::path(appdata) / "Yope3D";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

#else
std::string bundleResourcesDir() { return ""; }

std::string writableDataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base;
    if (xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        if (!home) return "";
        base = std::filesystem::path(home) / ".local" / "share";
    }
    std::filesystem::path dir = base / "Yope3D";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}
#endif
