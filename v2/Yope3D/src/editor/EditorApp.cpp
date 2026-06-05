#include "editor/EditorApp.h"
#include "editor/panels/ViewportPanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/StatsPanel.h"
#include "editor/panels/WorldSettingsPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "scene/ComponentSnapshot.h"
#include "editor/commands/EntityLifecycleCommands.h"
#include "editor/inspectors/InspectorRegistry.h"
#include "scene/serialization/SceneSerializer.h"
#include "platform/FileWatcher.h"
#include "gpu/Swapchain.h"
#include <ImGuizmo.h>
#include "audio/Listener.h"
#include "assets/Primitives.h"
#include "rendering/Light.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <string_view>
#include <vector>

bool EditorApp::init() {
    if (!engine_.init()) return false;
    engine_.world->setPaused(true);

    const Swapchain& sc = engine_.renderer->getSwapchain();

    imguiPass_ = std::make_unique<RenderPass>(
        RenderPass::createImGuiPass(engine_.gpu->device(), sc.imageFormat()));

    

    imguiBackend_.init(*engine_.gpu, *engine_.window,
                       imguiPass_->get(),
                       sc.imageCount(), sc.imageViews(), sc.extent(),
                       engine_.renderer->getCommandPool(), theme_);

    uint32_t vpW = std::max(1u, sc.extent().width  / 2);
    uint32_t vpH = std::max(1u, sc.extent().height / 2);
    viewportTarget_ = ViewportTarget(*engine_.gpu,
                                     engine_.renderer->getOffscreenGamePass(),
                                     engine_.renderer->getOffscreenRaytracePass(),
                                     sc.imageFormat(),
                                     engine_.renderer->getDepthFormat(),
                                     vpW, vpH);

    ctx_.registry      = &engine_.world->getRegistry();
    ctx_.engine        = &engine_;
    ctx_.world         = engine_.world.get();
    ctx_.selection     = &selection_;
    ctx_.history       = &history_;
    ctx_.theme         = &theme_;
    ctx_.editorCamera  = engine_.camera.get();
    ctx_.viewportTarget = &viewportTarget_;
    ctx_.idBufferPass  = &idBufferPass_;
    ctx_.playMode      = &playMode_;
    ctx_.onTogglePlay  = [this]() { requestTogglePlay(); };
    ctx_.onViewportResize = [this](uint32_t w, uint32_t h) { pendingVpW_ = w; pendingVpH_ = h; };
    ctx_.onViewportMaximize = [this](bool max) {
        if (max) {
            savedPanelVisibility_.clear();
            for (size_t i = 1; i < panels_.size(); ++i) {
                savedPanelVisibility_.push_back(panels_[i]->visible);
                panels_[i]->visible = false;
            }
        } else {
            for (size_t i = 1; i < panels_.size(); ++i)
                if (i - 1 < savedPanelVisibility_.size())
                    panels_[i]->visible = savedPanelVisibility_[i - 1];
            savedPanelVisibility_.clear();
        }
    };
    ctx_.onNewScene    = [this]() { pendingNewScene_ = true; };
    ctx_.onDeleteEntity = [this](ecs::Entity e) { pendingDeleteEntities_.push_back(e); };

    idBufferPass_.init(*engine_.gpu,
                       engine_.renderer->getUBOSetLayout(),
                       engine_.renderer->getCommandPool(),
                       engine_.renderer->getDepthFormat(),
                       vpW, vpH);

    registerAllInspectors();

    // Escape deselects in the editor instead of closing the window.
    engine_.window->setEscapeCloses(false);

    panels_.push_back(std::make_unique<ViewportPanel>());
    panels_.push_back(std::make_unique<HierarchyPanel>());
    panels_.push_back(std::make_unique<InspectorPanel>());
    {
        auto ab = std::make_unique<AssetBrowserPanel>();
        assetBrowser_ = ab.get();
        panels_.push_back(std::move(ab));
    }
    panels_.push_back(std::make_unique<StatsPanel>());
    panels_.push_back(std::make_unique<WorldSettingsPanel>());
    panels_.push_back(std::make_unique<ConsolePanel>());

    // Start FileWatcher on assets directory
    std::string assetsDir = YOPE_ASSETS_DIR;
    fileWatcher_.watch(assetsDir, [this](const std::string& p) {
        if (engine_.assets) engine_.assets->onFileChanged(p);
        if (assetBrowser_) assetBrowser_->onAssetModified(p);
        Console::log("Asset changed: " + p, LogSeverity::Info);
    });

    lastTime_   = glfwGetTime();
    pendingVpW_ = vpW;
    pendingVpH_ = vpH;
    return true;
}

void EditorApp::run() {
    while (!engine_.window->shouldClose()) {
        engine_.window->pollEvents();
        engine_.input->beginFrame();
        tick();
    }
}

void EditorApp::tick() {
    // --- Flush GPU-deferred mesh destroys (MUST be first, before recording opens) ---
    // removeEntity() queues RenderMesh destruction here instead of destroying
    // immediately, because deletion can be triggered mid-recording (e.g. panel
    // context menus). syncDevice inside flushPendingGpuDestroys() ensures all
    // in-flight frames are done before the VkBuffers are freed.
    engine_.world->flushPendingGpuDestroys();

    // --- Flush deferred ops (BEFORE command buffer recording opens) ---
    if (pendingTogglePlay_) { pendingTogglePlay_ = false; doTogglePlay(); }

    if (pendingNewScene_) {
        pendingNewScene_ = false;
        if (playMode_) {
            engine_.sceneManager->teardownAllScripts(engine_.scriptCtx_);
            engine_.audio->stopAll();
            engine_.world->restoreFromPlay();
            playMode_ = false;
        }
        // Drop the live scripts before the registry is rebuilt.
        engine_.sceneManager->teardownAllScripts(engine_.scriptCtx_);
        engine_.audio->stopAll();
        engine_.world->resetPhysics();
    }

    if (!pendingLoadScenePath_.empty()) {
        std::string path = std::move(pendingLoadScenePath_);
        pendingLoadScenePath_.clear();
        if (playMode_) {
            engine_.sceneManager->teardownAllScripts(engine_.scriptCtx_);
            engine_.world->restoreFromPlay();
            playMode_ = false;
        }
        // syncDevice before the load so any in-flight frame using the old
        // RenderMeshes is fully retired before resetPhysics destroys them.
        engine_.gpu->syncDevice();
        auto err = engine_.sceneManager->loadSynchronous(
            path, engine_.scriptCtx_, /*initScripts=*/false);
        if (err.empty()) {
            currentSceneFile_ = path;
            selection_.clear();
        } else {
            Console::log("Scene load failed: " + err, LogSeverity::Error);
        }
    }

    for (auto e : pendingDeleteEntities_) {
        if (selection_.primary() == e) selection_.clear();
        engine_.world->removeEntity(e);
    }
    pendingDeleteEntities_.clear();

    // Undo/redo must run before the command buffer opens so entity deletion
    // doesn't destroy VkBuffers that are still referenced in the current frame.
    if (!playMode_) {
        if (pendingUndo_) { history_.undo(ctx_); pendingUndo_ = false; }
        if (pendingRedo_) { history_.redo(ctx_); pendingRedo_ = false; }
    }

    // --- Pending viewport resize ---
    if (pendingVpW_ != viewportTarget_.width() || pendingVpH_ != viewportTarget_.height()) {
        engine_.gpu->syncDevice();
        // The offscreen UI framebuffer references the OLD colorView; destroy it
        // before vt.resize() releases that view, then the next beginFrameForEditor
        // recreates it against the new view.
        engine_.renderer->notifyViewportResizing(engine_.gpu->device());
        viewportTarget_.resize(*engine_.gpu, pendingVpW_, pendingVpH_);
        idBufferPass_.resize(*engine_.gpu, viewportTarget_.depthView(), pendingVpW_, pendingVpH_);
        engine_.camera->WindowChanged(static_cast<int>(pendingVpW_),
                                      static_cast<int>(pendingVpH_));
    }

    double now = glfwGetTime();
    double dt  = std::min(now - lastTime_, 0.05);
    lastTime_  = now;

    engine_.fpsAccum += static_cast<float>(dt);
    ++engine_.fpsFrames;
    if (engine_.fpsAccum >= 0.5f) {
        engine_.displayFps = static_cast<int>(engine_.fpsFrames / engine_.fpsAccum + 0.5f);
        engine_.fpsAccum   = 0.0f;
        engine_.fpsFrames  = 0;
    }

    if (playMode_) {
        if (engine_.world->newSnapshotReady_.exchange(false, std::memory_order_acquire))
            engine_.world->syncRenderMeshesFromFront();
    } else {
        engine_.world->publishSnapshot();
        engine_.world->newSnapshotReady_.store(false, std::memory_order_release);
        engine_.world->syncRenderMeshesFromFront();
    }

    Listener::setPosition(engine_.camera->getPosition());
    Listener::setOrientation(engine_.camera->getForward(), {0.0f, 1.0f, 0.0f});

    // --- Set render mode before game pass ---
    engine_.renderer->setMode(engine_.renderMode_);

    // --- Game → offscreen ViewportTarget ---
    uint32_t imageIndex = engine_.renderer->beginFrameForEditor(
        *engine_.gpu, *engine_.window,
        *engine_.camera, *engine_.world, *engine_.assets, viewportTarget_);

    if (imageIndex == UINT32_MAX) {
        const Swapchain& sc = engine_.renderer->getSwapchain();
        imguiBackend_.onSwapchainRecreate(engine_.gpu->device(), sc.imageViews(), sc.extent());
        return;
    }

    // --- ID buffer pass (edit mode only) ---
    if (!playMode_) {
        idBufferPass_.record(engine_.renderer->currentCmdBuffer(),
                             *ctx_.registry,
                             engine_.renderer->getCurrentDescriptorSet(),
                             viewportTarget_.depthView(),
                             viewportTarget_.width(), viewportTarget_.height());
        idBufferPass_.pollResult(*ctx_.registry, selection_);
    }

    // --- ImGui → swapchain ---
    imguiBackend_.newFrame();
    ImGuizmo::BeginFrame();

    // --- Global keyboard shortcuts (routed globally so they fire regardless of panel focus) ---
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) copySelected();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, ImGuiInputFlags_RouteGlobal)) pasteClipboard();
    if (ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal)) selection_.clear();
    {
        // ImGuiKey_Delete  = forward-delete (fn+Del on Mac, Del on PC).
        // ImGuiKey_Backspace = "Delete" key on Apple keyboards — guard with
        // IsAnyItemActive so it doesn't fire while editing a text input field.
        bool wantDelete =
            ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal) ||
            (!ImGui::IsAnyItemActive() &&
             ImGui::Shortcut(ImGuiKey_Backspace, ImGuiInputFlags_RouteGlobal));
        if (wantDelete && !playMode_ && ctx_.selection && !ctx_.selection->get().empty()) {
            // Route through the command system so delete is undoable (Ctrl+Z restores).
            // Copy the selection first — executing a command may clear/modify it.
            std::vector<ecs::Entity> toDelete(ctx_.selection->get().begin(),
                                              ctx_.selection->get().end());
            for (auto e : toDelete)
                history_.execute(ctx_, std::make_unique<DeleteEntityCommand>(e));
            selection_.clear();
        }
    }
    if (!playMode_) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
            pendingUndo_ = true;
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
            pendingRedo_ = true;
    }

    // --- Full-window dockspace ---
    {
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##dockspace", nullptr, flags);
        ImGui::PopStyleVar(2);
        ImGui::DockSpace(ImGui::GetID("MainDockspace"), {0, 0},
                         ImGuiDockNodeFlags_PassthruCentralNode);
        buildMenuBar();
        ImGui::End();
    }

    for (auto& panel : panels_)
        if (panel->visible) panel->draw(ctx_);

    imguiBackend_.render(engine_.renderer->currentCmdBuffer(), imageIndex);

    bool swapchainRecreated = engine_.renderer->endFrameForEditor(
        *engine_.gpu, *engine_.window, imageIndex);

    if (swapchainRecreated) {
        const Swapchain& sc = engine_.renderer->getSwapchain();
        imguiBackend_.onSwapchainRecreate(engine_.gpu->device(), sc.imageViews(), sc.extent());
    }
}

void EditorApp::buildMenuBar() {
    // Flags set inside menu scope; OpenPopup called after EndMenuBar at the
    // dockspace-window level so the IDs match BeginPopupModal below.
    static bool wantSaveAs    = false;
    static bool wantOpenScene = false;

    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene")) pendingNewScene_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", nullptr, false, !currentSceneFile_.empty())) {
            SceneSerializer::save(currentSceneFile_.c_str(),
                                  *ctx_.registry, *ctx_.world);
        }
        if (ImGui::MenuItem("Save Scene As...")) wantSaveAs    = true;
        if (ImGui::MenuItem("Open Scene..."))    wantOpenScene = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Copy",  "Cmd+C", false, !selection_.get().empty())) copySelected();
        if (ImGui::MenuItem("Paste", "Cmd+V", false, !clipboard_.empty()))       pasteClipboard();
        ImGui::Separator();
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history_.canUndo())) pendingUndo_ = true;
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history_.canRedo())) pendingRedo_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        for (auto& p : panels_) {
            if (std::string_view(p->name()) == "Viewport") continue;
            ImGui::MenuItem(p->name(), nullptr, &p->visible);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::Text("Yope3D Editor — Phase 1 MVP");
        ImGui::EndMenu();
    }

    // Right-aligned status strip: current scene + FPS
    {
        std::string scene = currentSceneFile_.empty() ? "(no scene)" : currentSceneFile_;
        std::string status = scene + "  |  " + std::to_string(engine_.displayFps) + " fps";
        float statusW = ImGui::CalcTextSize(status.c_str()).x + 16.f;
        float cursorX = ImGui::GetCursorPosX();
        float avail   = ImGui::GetContentRegionAvail().x;
        if (avail > statusW)
            ImGui::SetCursorPosX(cursorX + avail - statusW);
        ImGui::TextDisabled("%s", status.c_str());
    }

    ImGui::EndMenuBar();

    // ---- Dialogs: opened at dockspace-window scope (after EndMenuBar) ----
    // OpenPopup must be called at the same ID-stack level as BeginPopupModal.
    if (wantSaveAs)    { ImGui::OpenPopup("Save Scene As##dlg");  wantSaveAs    = false; }
    if (wantOpenScene) { ImGui::OpenPopup("Open Scene##dlg");     wantOpenScene = false; }

    // Save Scene As
    static char savePath[256] = "scene.json";
    if (ImGui::BeginPopupModal("Save Scene As##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("File path (relative to working directory):");
        ImGui::SetNextItemWidth(360.f);
        ImGui::InputText("##savepath", savePath, sizeof(savePath));
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            currentSceneFile_ = savePath;
            SceneSerializer::save(savePath, *ctx_.registry, *ctx_.world);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Open Scene
    static char openPath[256] = "scene.json";
    if (ImGui::BeginPopupModal("Open Scene##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("File path (relative to working directory):");
        ImGui::SetNextItemWidth(360.f);
        ImGui::InputText("##openpath", openPath, sizeof(openPath));
        ImGui::Spacing();
        if (ImGui::Button("Load", ImVec2(120, 0))) {
            // Defer — the actual load runs at the top of the next tick where
            // it's safe to destroy GPU resources. See pendingLoadScenePath_.
            pendingLoadScenePath_ = openPath;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorApp::cleanup() {
    fileWatcher_.stop();
    engine_.gpu->syncDevice();
    idBufferPass_.destroy(engine_.gpu->device());
    viewportTarget_.destroy(engine_.gpu->device());
    imguiBackend_.cleanup(engine_.gpu->device());
    imguiPass_.reset();
    engine_.cleanup();
}

void EditorApp::doTogglePlay() {
    if (!playMode_) {
        // Snapshot is taken with all ScriptComponent.instance == nullptr (edit-mode
        // invariant). Then we instantiate + init the live scripts.
        engine_.world->snapshotForPlay();
        engine_.sceneManager->instantiateAndInitAllScripts(engine_.scriptCtx_);
        // Start AudioSources marked autoplay — they were intentionally held silent
        // during edit-mode scene load (SceneSerializer::load with startAudio=false).
        for (auto [e, as] : engine_.world->getRegistry().view<ecs::AudioSource>())
            if (as.autoplay && as.source) as.source->play();
        playMode_ = true;
    } else {
        // Tear down scripts first (deletes Script* and nulls instance pointers)
        // so the registry snapshot restore — which memcpys the snapshot's
        // ScriptComponent rows with instance==nullptr — leaves no dangling state.
        engine_.sceneManager->teardownAllScripts(engine_.scriptCtx_);
        engine_.audio->stopAll();
        engine_.world->restoreFromPlay();
        playMode_ = false;
        selection_.clear();  // play-mode entities no longer exist after restore
    }
}

void EditorApp::copySelected() {
    clipboard_.clear();
    pasteAccum_ = {0.f, 0.f, 0.f};   // fresh copy resets paste offset accumulation
    auto& reg = engine_.world->getRegistry();
    for (auto e : selection_.get()) {
        if (!reg.valid(e)) continue;
        clipboard_.push_back(snapshotEntity(e, reg, *engine_.world));
    }
}

void EditorApp::pasteClipboard() {
    if (clipboard_.empty()) return;

    // Each paste steps the accumulator forward so repeated Ctrl+V never stacks.
    static constexpr math::Vec3 kStep{0.5f, 0.0f, 0.5f};
    pasteAccum_ = pasteAccum_ + kStep;

    // Within a multi-entity batch, apply a tiny diagonal spread per entity so
    // entities that were all at the same source position are individually visible
    // and selectable in edit mode (3 cm per slot — negligible for placed geometry).
    static constexpr float kSpread = 0.03f;

    std::vector<ComponentSnapshot> snaps = clipboard_;
    for (size_t i = 0; i < snaps.size(); ++i) {
        auto& s = snaps[i];
        math::Vec3 off = pasteAccum_ + math::Vec3{float(i) * kSpread, 0.f, float(i) * kSpread};
        if (s.hasTransform) s.transform.position = s.transform.position + off;
        if (s.hasLight && (s.light.type == 0 || s.light.type == 2)) {
            s.light.position[0] += off.x;
            s.light.position[2] += off.z;
        }
    }
    history_.execute(ctx_, std::make_unique<PasteEntitiesCommand>(std::move(snaps)));
}
