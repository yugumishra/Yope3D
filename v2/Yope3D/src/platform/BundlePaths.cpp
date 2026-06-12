#include "platform/BundlePaths.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <filesystem>
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

#else
std::string bundleResourcesDir() { return ""; }
#endif
