#pragma once
#include "editor/EditorPanel.h"
#include <unordered_set>
#include <string>

class AssetBrowserPanel : public EditorPanel {
public:
    const char* name() const override { return "Asset Browser"; }
    void draw(EditorContext& ctx) override;

    // Called from EditorApp's FileWatcher callback (from background thread — thread-safe add).
    void onAssetModified(const std::string& absPath);

private:
    void drawDirectory(const std::string& dirPath, EditorContext& ctx);
    const char* fileTypeBadge(const std::string& ext) const;

    std::string  selectedPath_;
    std::unordered_set<std::string> modifiedPaths_;
};
