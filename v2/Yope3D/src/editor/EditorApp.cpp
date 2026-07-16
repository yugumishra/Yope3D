#include "editor/EditorApp.h"
#include "editor/FileDialog.h"
#include <filesystem>
#ifdef YOPE_PYTHON
#include "scripting/python/BehaviorRegistry.h"
#endif
#include "editor/panels/ViewportPanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/StatsPanel.h"
#include "editor/panels/WorldSettingsPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/SceneScriptPanel.h"
#include "editor/panels/GJKTestPanel.h"
#include "editor/panels/GJKStepperPanel.h"
#include "scene/ComponentSnapshot.h"
#include "world/TransformHierarchy.h"
#include "editor/commands/EntityLifecycleCommands.h"
#include <unordered_set>
#include "editor/inspectors/InspectorRegistry.h"
#include "scene/serialization/SceneSerializer.h"
#include "platform/FileWatcher.h"
#include "gpu/Swapchain.h"
#include <ImGuizmo.h>
#include "audio/Listener.h"
#include "assets/Primitives.h"
#include "rendering/Light.h"
#include "debug/TaskProgress.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <string_view>
#include <vector>
#include <cstdio>

bool EditorApp::init(const std::string& sceneOverride) {
    if (!engine_.init(sceneOverride)) return false;
    // Mirror the cfg/--scene-resolved startup scene into currentSceneFile_ so
    // the header bar and Save Scene reflect it, same as the Open Scene menu
    // does at the pendingLoadScenePath_ handler below.
    currentSceneFile_ = engine_.startupScenePath();
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
    panels_.push_back(std::make_unique<SceneScriptPanel>());
    panels_.push_back(std::make_unique<GJKTestPanel>());
    panels_.push_back(std::make_unique<GJKStepperPanel>());

    // Start FileWatcher on assets directory
    std::string assetsDir = YOPE_ASSETS_DIR;
    fileWatcher_.watch(assetsDir, [this](const std::string& p) {
        if (engine_.assets) engine_.assets->onFileChanged(p);
        if (assetBrowser_) assetBrowser_->onAssetModified(p);
        Console::log("Asset changed: " + p, LogSeverity::Info);
    });

    // Also watch the scripts directory for .py hot-reload highlighting
    std::string scriptsDir = YOPE_SCRIPTS_DIR;
    scriptsWatcher_.watch(scriptsDir, [this](const std::string& p) {
        if (p.find("__pycache__") != std::string::npos) return;
        if (assetBrowser_) assetBrowser_->onAssetModified(p);
        Console::log("Script changed: " + p, LogSeverity::Info);
    });

#ifdef YOPE_PYTHON
    BehaviorRegistry::refresh(std::string(YOPE_SCRIPTS_DIR) + "/behaviors");
#endif

    FileDialog::init();

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

void EditorApp::drawLoadingOverlay() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList*    dl = ImGui::GetForegroundDrawList();

    // Opaque full-window backdrop (covers dockspace + panels + viewport).
    const ImVec2 p0 = vp->Pos;
    const ImVec2 p1 = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    dl->AddRectFilled(p0, p1, IM_COL32(15, 18, 23, 255));

    const ImVec2 c = vp->GetCenter();

    const char* title = "Loading scene…";
    const ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - 34.0f), IM_COL32(235, 238, 245, 255), title);

    // Progress bar reflecting committed / total entities (mesh upload progress).
    const int   done  = engine_.loadCommitted();
    const int   total = engine_.loadTotal();
    const float frac  = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
    const float bw = 320.0f, bh = 8.0f;
    const ImVec2 b0(c.x - bw * 0.5f, c.y - 4.0f);
    const ImVec2 b1(c.x + bw * 0.5f, c.y - 4.0f + bh);
    dl->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 38), 4.0f);
    dl->AddRectFilled(b0, ImVec2(b0.x + bw * frac, b1.y), IM_COL32(200, 205, 230, 235), 4.0f);

    char buf[64];
    if (total > 0) std::snprintf(buf, sizeof(buf), "%d / %d meshes", done, total);
    else           std::snprintf(buf, sizeof(buf), "Preparing…");
    const ImVec2 cs = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(c.x - cs.x * 0.5f, c.y + 16.0f), IM_COL32(150, 156, 170, 255), buf);
}

void EditorApp::tick() {
    // --- Flush GPU-deferred mesh destroys (MUST be first, before recording opens) ---
    // removeEntity() queues RenderMesh destruction here instead of destroying
    // immediately, because deletion can be triggered mid-recording (e.g. panel
    // context menus). syncDevice inside flushPendingGpuDestroys() ensures all
    // in-flight frames are done before the VkBuffers are freed.
    engine_.world->flushPendingGpuDestroys();

    // Drive the async startup-scene load (renders the loading splash over the
    // editor until the scene is committed + textures streamed; no-op afterwards).
    engine_.pumpSceneLoad();

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

    // Undo/redo and script-revert must run before the command buffer opens so
    // entity deletion doesn't destroy VkBuffers still referenced in the current frame.
    if (!playMode_) {
        if (pendingUndo_) { history_.undo(ctx_); pendingUndo_ = false; }
        if (pendingRedo_) { history_.redo(ctx_); pendingRedo_ = false; }
        if (ctx_.pendingScriptRevert) {
            ctx_.pendingScriptRevert = false;
            engine_.world->restoreScriptSnapshot();
        }
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
        // The editor drives its own loop instead of Engine::update(), so without
        // this call Play instantiated + init()'d every ScriptComponent (in
        // doTogglePlay) but never ticked update() on any of them — scripts sat
        // inert for the entire session. updateScripts also owns UI input
        // routing/callbacks, collision-event dispatch, ui_update, and the
        // contact-debug-line clear+emit, so the standalone block below is only
        // needed in edit mode (no scripts running there to clobber).
        engine_.updateScripts(static_cast<float>(dt));
        if (engine_.world->newSnapshotReady_.exchange(false, std::memory_order_acquire))
            engine_.world->syncRenderMeshesFromFront();
    } else {
        // Physics is paused in edit mode (World::advance early-returns), so the
        // animation-clip preview driven by the Inspector's AnimationPlayer
        // controls needs its own tick here — otherwise scrubbing/playing a clip
        // in the viewport would do nothing until Play is pressed.
        engine_.world->updateAnimations(static_cast<float>(dt));
        engine_.world->publishSnapshot();
        engine_.world->newSnapshotReady_.store(false, std::memory_order_release);
        engine_.world->syncRenderMeshesFromFront();

    }

    Listener::setPosition(engine_.camera->getPosition());
    Listener::setOrientation(engine_.camera->getForward(), {0.0f, 1.0f, 0.0f});

    // Upload textures streamed in from background decode (glb embedded images).
    // The editor drives its own render loop instead of Engine::render(), so this
    // pump has to be called here too — otherwise decoded textures pile up in the
    // streamer's ready queue and never reach the GPU/material sets.
    engine_.assets->pumpTextureUploads();

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

    // Full-window loading overlay while the startup scene's meshes commit. Drawn
    // on the foreground draw list so it covers the whole window (dockspace, panels
    // and viewport) — not just the viewport. Textures stream afterwards on the
    // menu-bar progress bar.
    if (!engine_.isSceneLoaded()) drawLoadingOverlay();

    imguiBackend_.render(engine_.renderer->currentCmdBuffer(), imageIndex);

    bool swapchainRecreated = engine_.renderer->endFrameForEditor(
        *engine_.gpu, *engine_.window, imageIndex);

    if (swapchainRecreated) {
        const Swapchain& sc = engine_.renderer->getSwapchain();
        imguiBackend_.onSwapchainRecreate(engine_.gpu->device(), sc.imageViews(), sc.extent());
    }
}

void EditorApp::buildMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene")) pendingNewScene_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", nullptr, false, !currentSceneFile_.empty())) {
            SceneSerializer::save(currentSceneFile_.c_str(),
                                  *ctx_.registry, *ctx_.world);
        }
        if (ImGui::MenuItem("Save Scene As...")) {
            namespace fs = std::filesystem;
            const char* defaultDir  = nullptr;
            const char* defaultName = "scene.json";
            std::string dirStr, nameStr;
            if (!currentSceneFile_.empty()) {
                fs::path p(currentSceneFile_);
                dirStr  = p.parent_path().string();
                nameStr = p.filename().string();
                if (!dirStr.empty())  defaultDir  = dirStr.c_str();
                if (!nameStr.empty()) defaultName = nameStr.c_str();
            }
            if (auto picked = FileDialog::saveFile({{"Scene", "json"}}, defaultDir, defaultName)) {
                currentSceneFile_ = *picked;
                SceneSerializer::save(picked->c_str(), *ctx_.registry, *ctx_.world);
            }
        }
        if (ImGui::MenuItem("Open Scene...")) {
            if (auto picked = FileDialog::openFile({{"Scene", "json"}}))
                pendingLoadScenePath_ = *picked;
        }
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

    // Right-aligned status strip: active task progress (if any) + scene + FPS.
    // The progress bar is universal — any long-running task (texture streaming
    // today) reports into TaskProgress and shows up here automatically.
    // Layout (left to right): subtitle (label + elapsed/ETA, never truncated) —
    // thin progress bar (panel-blue fill, % left-aligned on top) — scene | fps.
    {
        auto fmtSecs = [](double s) {
            char buf[32];
            if (s < 60.0) std::snprintf(buf, sizeof(buf), "%.0fs", s);
            else          std::snprintf(buf, sizeof(buf), "%dm%02ds", (int)(s / 60.0), (int)s % 60);
            return std::string(buf);
        };

        TaskProgress::Snapshot prog = TaskProgress::current();
        std::string scene  = currentSceneFile_.empty() ? "(no scene)" : currentSceneFile_;
        std::string status = scene + "  |  " + std::to_string(engine_.displayFps) + " fps";
        float statusW = ImGui::CalcTextSize(status.c_str()).x + 16.f;

        float barW = 0.0f;
        float subtitleW = 0.0f;
        float frac = 0.0f;
        std::string subtitle, pctText;
        constexpr float kGap = 10.0f;
        if (prog.active && prog.total > 0) {
            barW = 140.0f;
            frac = static_cast<float>(prog.completed) / static_cast<float>(prog.total);
            double eta = (frac > 0.01f) ? prog.elapsedSeconds * (1.0 - frac) / frac : 0.0;
            // Full detail lives in the subtitle — plenty of room in the header,
            // so this is never truncated.
            subtitle = prog.label + "  —  " + fmtSecs(prog.elapsedSeconds) + " elapsed, ~" +
                      fmtSecs(eta) + " remaining";
            pctText  = std::to_string((int)(frac * 100.0f + 0.5f)) + "%";
            subtitleW = ImGui::CalcTextSize(subtitle.c_str()).x;
        }

        float totalW  = statusW + (barW > 0.0f ? barW + subtitleW + kGap * 2.0f : 0.0f);
        float cursorX = ImGui::GetCursorPosX();
        float avail   = ImGui::GetContentRegionAvail().x;
        if (avail > totalW)
            ImGui::SetCursorPosX(cursorX + avail - totalW);

        if (barW > 0.0f) {
            // Subtitle sits directly against the bar's left edge (right-justified
            // within the group, not floating with a big gap).
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", subtitle.c_str());
            ImGui::SameLine(0.0f, kGap);

            // Reserve a normal full-height cell (so SameLine()/row alignment
            // matches the surrounding text), then draw a thinner bar by hand,
            // vertically centered within it — ImGui::ProgressBar always anchors
            // to the top of the line, which left a thin bar stuck at the top
            // instead of centered against the subtitle/status text beside it.
            float lineHeight = ImGui::GetFrameHeight();
            ImGui::Dummy(ImVec2(barW, lineHeight));
            ImVec2 cellMin = ImGui::GetItemRectMin();

            float barHeight = ImGui::GetFontSize() * 0.55f;
            float barY      = cellMin.y + (lineHeight - barHeight) * 0.5f;
            ImVec2 barMin(cellMin.x, barY);
            ImVec2 barMax(cellMin.x + barW, barY + barHeight);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            float rounding = ImGui::GetStyle().FrameRounding;
            dl->AddRectFilled(barMin, barMax, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
            float fillW = barW * frac;
            if (fillW > 0.0f) {
                ImDrawFlags flags = (frac >= 0.999f) ? ImDrawFlags_RoundCornersAll
                                                      : ImDrawFlags_RoundCornersLeft;
                dl->AddRectFilled(barMin, ImVec2(barMin.x + fillW, barMax.y),
                                  ImGui::GetColorU32(ImGuiCol_Button), rounding, flags);
            }

            // Percentage left-aligned on top of the bar (ImGui::ProgressBar's
            // built-in overlay text is center-only).
            ImVec2 textPos(barMin.x + 4.0f, cellMin.y + (lineHeight - ImGui::GetFontSize()) * 0.5f);
            dl->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), pctText.c_str());

            ImGui::SameLine(0.0f, kGap);
        }
        ImGui::TextDisabled("%s", status.c_str());
    }

    ImGui::EndMenuBar();
}

void EditorApp::cleanup() {
    FileDialog::quit();
    fileWatcher_.stop();
    scriptsWatcher_.stop();
    engine_.gpu->syncDevice();
    idBufferPass_.destroy(engine_.gpu->device());
    viewportTarget_.destroy(engine_.gpu->device());
    imguiBackend_.cleanup(engine_.gpu->device());
    imguiPass_.reset();
    engine_.cleanup();
}

void EditorApp::doTogglePlay() {
    // Ignore Play until the startup scene has fully loaded (the loading splash is
    // still up and the registry is only partially populated).
    if (!engine_.isSceneLoaded()) return;
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
    clipboardIds_.clear();
    pasteAccum_ = {0.f, 0.f, 0.f};   // fresh copy resets paste offset accumulation
    auto& reg = engine_.world->getRegistry();
    // Expand each selected entity to its full subtree so children come along; dedup
    // by id (a parent and its child may both be selected). Order isn't critical —
    // restoreSubtree remaps Parent links in a second pass.
    std::unordered_set<uint32_t> seen;
    std::vector<ecs::Entity> ordered;
    for (auto e : selection_.get()) {
        if (!reg.valid(e)) continue;
        std::vector<ecs::Entity> sub;
        hierarchy::collectSubtree(reg, e, sub);
        for (ecs::Entity s : sub)
            if (seen.insert(s.id).second) ordered.push_back(s);
    }
    for (ecs::Entity e : ordered) {
        clipboard_.push_back(snapshotEntity(e, reg, *engine_.world));
        clipboardIds_.push_back(e);
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

    // Only offset pasted ROOTS (entities whose parent wasn't copied). Children keep
    // their local transform and follow their root via hierarchy composition.
    std::unordered_set<uint32_t> copied;
    for (ecs::Entity e : clipboardIds_) copied.insert(e.id);

    std::vector<ComponentSnapshot> snaps = clipboard_;
    size_t rootIdx = 0;
    for (auto& s : snaps) {
        bool isRoot = !s.hasParent || copied.find(s.parent.parent.id) == copied.end();
        if (!isRoot) continue;
        math::Vec3 off = pasteAccum_ + math::Vec3{float(rootIdx) * kSpread, 0.f, float(rootIdx) * kSpread};
        ++rootIdx;
        if (s.hasTransform) s.transform.position = s.transform.position + off;
        if (s.hasLight && (s.light.type == 0 || s.light.type == 2)) {
            s.light.position[0] += off.x;
            s.light.position[2] += off.z;
        }
    }
    history_.execute(ctx_, std::make_unique<PasteEntitiesCommand>(std::move(snaps), clipboardIds_));
}
