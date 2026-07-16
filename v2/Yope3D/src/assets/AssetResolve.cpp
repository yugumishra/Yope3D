#include "assets/AssetResolve.h"
#include "platform/BundlePaths.h"
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef YOPE_EMBED_ASSETS
#include "generated/embedded_assets.h"
#endif

namespace assets {

namespace {

std::vector<uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    std::streamsize size = f.tellg();
    if (size < 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), size)) return {};
    return data;
}

std::vector<uint8_t> readFromFilesystem(const std::string& relPath) {
    // Bundle mode first (PARTIAL scope's loose files ship in Contents/Resources/assets).
    const std::string resDir = bundleResourcesDir();
    if (!resDir.empty()) {
        auto data = readFileBytes(std::filesystem::path(resDir) / "assets" / relPath);
        if (!data.empty()) return data;
    }
    // Dev builds / non-bundle runs: compile-time absolute path into the repo.
    return readFileBytes(std::filesystem::path(YOPE_ASSETS_DIR) / relPath);
}

} // namespace

std::vector<uint8_t> readBytes(const std::string& relPath) {
#ifdef YOPE_EMBED_ASSETS
    EmbeddedAsset asset = getEmbeddedAsset(relPath.c_str());
    if (asset.data) {
        return std::vector<uint8_t>(asset.data, asset.data + asset.size);
    }
#endif
    return readFromFilesystem(relPath);
}

std::string readText(const std::string& relPath) {
    auto bytes = readBytes(relPath);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string normalizeToAssetsRelative(const std::string& path) {
    if (path.rfind("assets/", 0) == 0) return path.substr(7);

    std::filesystem::path p(path);
    if (p.is_absolute()) {
        std::error_code ec;
        std::filesystem::path rel = std::filesystem::relative(p, std::filesystem::path(YOPE_ASSETS_DIR), ec);
        if (!ec) {
            std::string relStr = rel.generic_string();
            if (relStr.rfind("..", 0) != 0) return relStr;
        }
    }
    return path;
}

std::string resolveFilesystemPath(const std::string& relPath) {
    const std::string resDir = bundleResourcesDir();
    if (!resDir.empty()) {
        std::filesystem::path candidate = std::filesystem::path(resDir) / "assets" / relPath;
        if (std::filesystem::exists(candidate)) return candidate.string();
    }
    return (std::filesystem::path(YOPE_ASSETS_DIR) / relPath).string();
}

} // namespace assets
