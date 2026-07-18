#ifdef YOPE_EDITOR
#include "editor/panels/SceneScriptPanel.h"
#include "editor/EditorContext.h"
#include "editor/FileDialog.h"
#include "editor/panels/ConsolePanel.h"
#include "Engine.h"
#include "world/World.h"
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef YOPE_PYTHON
#include "scripting/python/PythonInterpreter.h"
#endif

void SceneScriptPanel::draw(EditorContext& ctx) {
    if (!visible) return;
    ImGui::Begin("Scene Script", &visible);

#ifndef YOPE_PYTHON
    ImGui::TextDisabled("Python scripting not compiled in (YOPE_PYTHON not set).");
    ImGui::End();
    return;
#else
    bool inPlayMode = ctx.playMode && *ctx.playMode;

    // Toolbar
    if (ImGui::Button("Load File")) {
        if (auto picked = FileDialog::openFile({{"Python Script", "py"}}, YOPE_SCRIPTS_DIR)) {
            std::ifstream f(*picked);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                code_ = ss.str();
                filePath_ = *picked;
                snapshotTaken_ = false;
            } else {
                Console::log(std::string("[SceneScript] Cannot open: ") + *picked,
                             LogSeverity::Error);
            }
        }
    }
    ImGui::SameLine();

    // Run button — only available in edit mode
    if (inPlayMode) ImGui::BeginDisabled();
    if (ImGui::Button("Run")) {
        if (!snapshotTaken_) {
            ctx.world->takeScriptSnapshot();   // does NOT unpause physics
            snapshotTaken_ = true;
        }
        // Execute script in the context of the current edit-mode world
        Console::log("[SceneScript] Running script...", LogSeverity::Info);
        bool ok = ctx.engine->python->execString(code_);
        if (ok) Console::log("[SceneScript] Done.", LogSeverity::Info);
    }
    if (inPlayMode) ImGui::EndDisabled();

    ImGui::SameLine();

    // Revert — deferred to pre-recording so VkBuffers aren't destroyed mid-frame
    bool canRevert = snapshotTaken_;
    if (!canRevert) ImGui::BeginDisabled();
    if (ImGui::Button("Revert")) {
        ctx.pendingScriptRevert = true;
        snapshotTaken_ = false;
        Console::log("[SceneScript] Reverted.", LogSeverity::Info);
    }
    if (!canRevert) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Clear Snapshot")) {
        snapshotTaken_ = false;
    }

    ImGui::Separator();

    // Code editor
    constexpr size_t kBufSize = 1024 * 16;
    static char buf[kBufSize];
    // Sync from code_ to buf when code_ changes externally (file load)
    static std::string lastCode;
    if (lastCode != code_) {
        std::strncpy(buf, code_.c_str(), kBufSize - 1);
        buf[kBufSize - 1] = '\0';
        lastCode = code_;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (ImGui::InputTextMultiline("##scriptcode", buf, kBufSize,
                                  ImVec2(avail.x, avail.y - 4))) {
        code_ = buf;
        lastCode = code_;
    }

    ImGui::End();
#endif // YOPE_PYTHON
}
#endif // YOPE_EDITOR
