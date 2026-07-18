#pragma once
#ifdef YOPE_EDITOR
#include <optional>
#include <string>
#include <initializer_list>

struct FileFilter { const char* name; const char* spec; };

namespace FileDialog {
    void init();
    void quit();

    // Returns the absolute path chosen by the user, or nullopt on cancel/error.
    std::optional<std::string> openFile(std::initializer_list<FileFilter> filters,
                                        const char* defaultDir = nullptr);
    std::optional<std::string> saveFile(std::initializer_list<FileFilter> filters,
                                        const char* defaultDir = nullptr,
                                        const char* defaultName = nullptr);

    // Converts an absolute path to one relative to YOPE_ASSETS_DIR.
    // Returns the input unchanged if it is not under the assets directory.
    std::string toAssetRelative(const std::string& absPath);
}
#endif
