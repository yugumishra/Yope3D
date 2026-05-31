#pragma once
#include "Engine.h"
#include "rendering/ViewportTarget.h"
#include "gpu/RenderPass.h"
#include "editor/EditorContext.h"
#include "editor/Selection.h"
#include "editor/CommandHistory.h"
#include "editor/EditorPanel.h"
#include "editor/ImGuiBackend.h"
#include "editor/EditorTheme.h"
#include "editor/commands/ComponentSnapshot.h"
#include "editor/picking/IdBufferPass.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "platform/FileWatcher.h"
#include "ecs/Entity.h"
#include "world/RenderMesh.h"
#include "world/Transform.h"
#include "ecs/Components.h"
#include <memory>
#include <vector>

// Clipboard reuses ComponentSnapshot so copy/paste and delete/undo share the same restore logic.
using ClipboardEntry = ComponentSnapshot;

class EditorApp {
public:
    bool init();
    void run();
    void cleanup();

    void requestTogglePlay() { pendingTogglePlay_ = true; }
    bool isPlayMode() const  { return playMode_; }

private:
    void tick();
    void buildMenuBar();
    void doTogglePlay();
    void copySelected();
    void pasteClipboard();

    Engine engine_;

    ImGuiBackend   imguiBackend_;
    EditorTheme    theme_;
    ViewportTarget viewportTarget_;

    Selection      selection_;
    CommandHistory history_;

    std::vector<std::unique_ptr<EditorPanel>> panels_;
    EditorContext ctx_;

    std::unique_ptr<RenderPass> imguiPass_;
    IdBufferPass idBufferPass_;
    FileWatcher       fileWatcher_;
    AssetBrowserPanel* assetBrowser_ = nullptr;  // non-owning; owned by panels_

    bool   playMode_ = false;
    double lastTime_ = 0.0;
    std::string currentSceneFile_;

    uint32_t pendingVpW_ = 0, pendingVpH_ = 0;

    bool pendingNewScene_   = false;
    bool pendingTogglePlay_ = false;
    bool pendingUndo_       = false;
    bool pendingRedo_       = false;

    // Paste offset accumulates per paste so repeated Ctrl+V lands further away.
    // Reset to zero whenever the clipboard is refreshed (copy).
    math::Vec3 pasteAccum_{0.f, 0.f, 0.f};

    std::vector<ecs::Entity>    pendingDeleteEntities_;
    std::vector<ClipboardEntry> clipboard_;
};
