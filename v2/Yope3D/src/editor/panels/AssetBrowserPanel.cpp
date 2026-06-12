#include "editor/panels/AssetBrowserPanel.h"
#include "editor/EditorContext.h"
#include "assets/AssetManager.h"
#include "scripting/python/PythonInterpreter.h"
#include "Engine.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <unordered_set>

namespace fs = std::filesystem;

static std::mutex g_modifiedMutex;

void AssetBrowserPanel::onAssetModified(const std::string& absPath) {
    std::lock_guard<std::mutex> lock(g_modifiedMutex);
    modifiedPaths_.insert(absPath);
}

const char* AssetBrowserPanel::fileTypeBadge(const std::string& ext) const {
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") return "[OBJ]";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") return "[PNG]";
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")                    return "[WAV]";
    if (ext == ".py")                                                        return " [PY]";
    if (ext == ".json" || ext == ".cfg")                                     return "[CFG]";
    return "[---]";
}

void AssetBrowserPanel::drawDirectory(const std::string& dirPath, EditorContext& ctx) {
    std::error_code ec;
    fs::directory_iterator it(dirPath, ec);
    if (ec) return;

    // Collect entries, sort dirs first then files
    std::vector<fs::directory_entry> entries;
    for (auto& e : fs::directory_iterator(dirPath, ec)) {
        if (!ec) entries.push_back(e);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        bool ad = a.is_directory(), bd = b.is_directory();
        if (ad != bd) return ad > bd;
        return a.path().filename() < b.path().filename();
    });

    for (auto& entry : entries) {
        std::string absPath = entry.path().string();
        std::string filename = entry.path().filename().string();
        if (filename.empty() || filename[0] == '.') continue;
        // Hide editor-internal directories that aren't user-facing assets.
        static const std::unordered_set<std::string> kHiddenDirs = { "icons", "fonts" };
        if (entry.is_directory() && kHiddenDirs.count(filename)) continue;
        if (filename == "__pycache__" || filename == "__init__.py") continue;

        bool isModified = false;
        {
            std::lock_guard<std::mutex> lock(g_modifiedMutex);
            isModified = modifiedPaths_.count(absPath) > 0;
        }

        if (entry.is_directory()) {
            bool open = ImGui::TreeNode(filename.c_str());
            if (open) {
                drawDirectory(absPath, ctx);
                ImGui::TreePop();
            }
        } else {
            std::string ext = entry.path().extension().string();
            // Lowercase extension
            for (auto& c : ext) c = static_cast<char>(std::tolower(c));

            const char* badge = fileTypeBadge(ext);
            if (isModified)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.2f, 1.0f));

            bool selected = (selectedPath_ == absPath);
            if (ImGui::Selectable((std::string(badge) + " " + filename).c_str(), selected)) {
                selectedPath_ = absPath;
                // Clear modified flag on select
                std::lock_guard<std::mutex> lock(g_modifiedMutex);
                modifiedPaths_.erase(absPath);
            }

            if (isModified) ImGui::PopStyleColor();

            // Drag source: .obj for MeshRenderer, .wav/.ogg for AudioSource.
            // Payload type is "ASSET_PATH"; targets disambiguate by extension.
            bool draggable = (ext == ".obj" || ext == ".fbx" ||
                              ext == ".wav" || ext == ".ogg" || ext == ".mp3");
            if (draggable && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET_PATH", absPath.c_str(), absPath.size() + 1);
                ImGui::Text("Drag: %s", filename.c_str());
                ImGui::EndDragDropSource();
            }
        }
    }
}

void AssetBrowserPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("Asset Browser", &visible);

    // Toolbar
    if (!selectedPath_.empty() && ctx.engine) {
        fs::path sel(selectedPath_);
        std::string ext = sel.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        bool isPy = (ext == ".py");

        bool canReload = isPy ? (ctx.engine->python != nullptr)
                               : (ctx.engine->assets != nullptr);
        if (ImGui::Button("Reload") && canReload) {
            if (isPy) {
                // Derive dotted module name from path relative to scripts dir.
                // e.g. .../scripts/behaviors/foo.py → "behaviors.foo"
                fs::path scriptsRoot(YOPE_SCRIPTS_DIR);
                fs::path relPath = sel.lexically_relative(scriptsRoot).replace_extension("");
                std::string modName = relPath.generic_string();
                for (auto& c : modName) if (c == '/') c = '.';
                ctx.engine->python->reloadModule(modName);
            } else {
                ctx.engine->assets->onFileChanged(selectedPath_);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", selectedPath_.c_str());
    } else {
        ImGui::TextDisabled("Select a file to reload it");
    }

    ImGui::Separator();

    // File tree: assets + scripts roots
    std::string assetsDir  = YOPE_ASSETS_DIR;
    std::string scriptsDir = YOPE_SCRIPTS_DIR;
    if (ImGui::TreeNodeEx("assets", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawDirectory(assetsDir, ctx);
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawDirectory(scriptsDir, ctx);
        ImGui::TreePop();
    }

    ImGui::End();
}
