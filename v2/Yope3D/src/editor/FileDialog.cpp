#ifdef YOPE_EDITOR
#include "editor/FileDialog.h"
#include <nfd.h>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace FileDialog {

void init() { NFD_Init(); }
void quit() { NFD_Quit(); }

std::optional<std::string> openFile(std::initializer_list<FileFilter> filters,
                                    const char* defaultDir) {
    std::vector<nfdu8filteritem_t> items;
    items.reserve(filters.size());
    for (const auto& f : filters)
        items.push_back({f.name, f.spec});

    nfdu8char_t* outPath = nullptr;
    nfdresult_t res = NFD_OpenDialogU8(&outPath,
                                       items.data(),
                                       static_cast<nfdfiltersize_t>(items.size()),
                                       defaultDir);
    if (res == NFD_OKAY) {
        std::string path = outPath;
        NFD_FreePathU8(outPath);
        return path;
    }
    return std::nullopt;
}

std::optional<std::string> saveFile(std::initializer_list<FileFilter> filters,
                                    const char* defaultDir,
                                    const char* defaultName) {
    std::vector<nfdu8filteritem_t> items;
    items.reserve(filters.size());
    for (const auto& f : filters)
        items.push_back({f.name, f.spec});

    nfdu8char_t* outPath = nullptr;
    nfdresult_t res = NFD_SaveDialogU8(&outPath,
                                       items.data(),
                                       static_cast<nfdfiltersize_t>(items.size()),
                                       defaultDir,
                                       defaultName);
    if (res == NFD_OKAY) {
        std::string path = outPath;
        NFD_FreePathU8(outPath);
        return path;
    }
    return std::nullopt;
}

std::string toAssetRelative(const std::string& absPath) {
    fs::path abs(absPath);
    fs::path root(YOPE_ASSETS_DIR);
    std::error_code ec;
    auto rel = fs::relative(abs, root, ec);
    if (ec || rel.empty() || rel.native().rfind("..", 0) == 0) return absPath;
    return rel.string();
}

} // namespace FileDialog
#endif
