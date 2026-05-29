#include "editor/EditorApp.h"
#include "editor/panels/ViewportPanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/StatsPanel.h"
#include "editor/panels/WorldSettingsPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "gpu/Swapchain.h"
#include "audio/Listener.h"
#include "assets/Primitives.h"
#include "rendering/Light.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <string_view>

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
    ctx_.playMode      = &playMode_;
    ctx_.onTogglePlay  = [this]() { requestTogglePlay(); };
    ctx_.onViewportResize = [this](uint32_t w, uint32_t h) { pendingVpW_ = w; pendingVpH_ = h; };
    ctx_.onNewScene    = [this]() { pendingNewScene_ = true; };
    ctx_.onDeleteEntity = [this](ecs::Entity e) { pendingDeleteEntities_.push_back(e); };

    panels_.push_back(std::make_unique<ViewportPanel>());
    panels_.push_back(std::make_unique<HierarchyPanel>());
    panels_.push_back(std::make_unique<InspectorPanel>());
    panels_.push_back(std::make_unique<StatsPanel>());
    panels_.push_back(std::make_unique<WorldSettingsPanel>());
    panels_.push_back(std::make_unique<ConsolePanel>());

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
    // --- Flush deferred ops (BEFORE command buffer recording opens) ---
    if (pendingTogglePlay_) { pendingTogglePlay_ = false; doTogglePlay(); }

    if (pendingNewScene_) {
        pendingNewScene_ = false;
        if (playMode_) { engine_.world->restoreFromPlay(); playMode_ = false; }
        engine_.world->resetPhysics();
    }

    for (auto e : pendingDeleteEntities_) {
        if (selection_.primary() == e) selection_.clear();
        engine_.world->removeEntity(e);
    }
    pendingDeleteEntities_.clear();

    // --- Pending viewport resize ---
    if (pendingVpW_ != viewportTarget_.width() || pendingVpH_ != viewportTarget_.height()) {
        engine_.gpu->syncDevice();
        viewportTarget_.resize(*engine_.gpu, pendingVpW_, pendingVpH_);
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

    // --- ImGui → swapchain ---
    imguiBackend_.newFrame();

    // --- Global keyboard shortcuts (routed globally so they fire regardless of panel focus) ---
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) copySelected();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, ImGuiInputFlags_RouteGlobal)) pasteClipboard();
    if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
        if (ctx_.selection && ctx_.onDeleteEntity) {
            for (auto e : ctx_.selection->get())
                ctx_.onDeleteEntity(e);
            ctx_.selection->clear();
        }
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
        panel->draw(ctx_);

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
        ImGui::MenuItem("Open Scene...", nullptr, false, false);
        ImGui::MenuItem("Save Scene",    nullptr, false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Copy",  "Cmd+C", false, !selection_.get().empty())) copySelected();
        if (ImGui::MenuItem("Paste", "Cmd+V", false, !clipboard_.empty()))       pasteClipboard();
        ImGui::Separator();
        ImGui::MenuItem("Undo", "Cmd+Z", false, false);
        ImGui::MenuItem("Redo", "Cmd+Y", false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        for (auto& p : panels_) {
            // Viewport is always visible — skip it from the toggle list
            if (std::string_view(p->name()) == "Viewport") continue;
            ImGui::MenuItem(p->name(), nullptr, &p->visible);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::Text("Yope3D Editor — Phase 1 MVP");
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void EditorApp::cleanup() {
    engine_.gpu->syncDevice();
    viewportTarget_.destroy(engine_.gpu->device());
    imguiBackend_.cleanup(engine_.gpu->device());
    imguiPass_.reset();
    engine_.cleanup();
}

void EditorApp::doTogglePlay() {
    if (!playMode_) {
        engine_.world->snapshotForPlay();
        playMode_ = true;
    } else {
        engine_.world->restoreFromPlay();
        playMode_ = false;
        selection_.clear();  // play-mode entities no longer exist after restore
    }
}

void EditorApp::copySelected() {
    clipboard_.clear();
    auto& reg = engine_.world->getRegistry();
    for (auto e : selection_.get()) {
        if (!reg.valid(e)) continue;
        ClipboardEntry entry;
        if (auto* tf = reg.get<Transform>(e))          { entry.hasTransform = true; entry.transform = *tf; }
        if (auto* h  = reg.get<ecs::Hull>(e))          { entry.hasHull = true; entry.hull = *h; }
        if (                reg.has<ecs::Fixed>(e))      { entry.hasFixed = true; }
        if (auto* s  = reg.get<ecs::SphereForm>(e))    { entry.hasSphere = true; entry.sphere = *s; }
        if (auto* a  = reg.get<ecs::AABBForm>(e))      { entry.hasAABB = true; entry.aabb = *a; }
        if (auto* o  = reg.get<ecs::OBBForm>(e))       { entry.hasOBB = true; entry.obb = *o; }
        if (auto* ls = reg.get<ecs::LightSource>(e))   { entry.hasLight = true; entry.light = *ls; }
        if (auto* n  = reg.get<ecs::Name>(e))          { entry.hasName = true; entry.name = *n; }
        if (auto* mr = reg.get<ecs::MeshRenderer>(e); mr && mr->mesh) {
            entry.hasMesh      = true;
            entry.meshColor[0] = mr->mesh->color[0];
            entry.meshColor[1] = mr->mesh->color[1];
            entry.meshColor[2] = mr->mesh->color[2];
            entry.primType     = mr->mesh->primitiveType;
            entry.primExtents  = mr->mesh->primitiveExtents;
            entry.cpuVerts     = mr->mesh->cpuVertices;
            entry.cpuInds      = mr->mesh->cpuIndices;
        }
        clipboard_.push_back(std::move(entry));
    }
}

void EditorApp::pasteClipboard() {
    selection_.clear();
    static constexpr math::Vec3 kOffset{0.5f, 0.0f, 0.5f};

    for (auto& entry : clipboard_) {
        ecs::Entity e = ecs::NullEntity;
        math::Vec3 pos = entry.hasTransform ? entry.transform.position + kOffset : kOffset;

        if (entry.hasSphere && entry.hasHull) {
            e = engine_.world->addSphere(entry.hull.mass, entry.sphere.radius, pos);
        } else if (entry.hasOBB && entry.hasHull) {
            e = engine_.world->addOBB(entry.obb.extent, entry.hull.mass, pos);
        } else if (entry.hasAABB && entry.hasHull && entry.hasFixed) {
            e = engine_.world->addStaticAABB(pos, entry.aabb.extent);
        } else if (entry.hasAABB && entry.hasHull) {
            e = engine_.world->addAABB(entry.aabb.extent, entry.hull.mass, pos);
        } else if (entry.hasLight) {
            ecs::LightSource ls = entry.light;
            if (ls.type == 0) {
                PointLight pl{}; pl.color[0]=ls.color[0]; pl.color[1]=ls.color[1]; pl.color[2]=ls.color[2];
                pl.intensity=ls.intensity; pl.constant=ls.constant; pl.linear=ls.linear; pl.quadratic=ls.quadratic;
                pl.position[0]=ls.position[0]+kOffset.x; pl.position[1]=ls.position[1]; pl.position[2]=ls.position[2]+kOffset.z;
                e = engine_.world->addLight(pl);
            } else if (ls.type == 1) {
                DirectionalLight dl{}; dl.color[0]=ls.color[0]; dl.color[1]=ls.color[1]; dl.color[2]=ls.color[2];
                dl.intensity=ls.intensity; dl.direction[0]=ls.direction[0]; dl.direction[1]=ls.direction[1]; dl.direction[2]=ls.direction[2];
                e = engine_.world->addLight(dl);
            } else if (ls.type == 2) {
                SpotLight sl{}; sl.color[0]=ls.color[0]; sl.color[1]=ls.color[1]; sl.color[2]=ls.color[2];
                sl.intensity=ls.intensity; sl.constant=ls.constant; sl.linear=ls.linear; sl.quadratic=ls.quadratic;
                sl.innerConeAngle=ls.innerConeAngle; sl.outerConeAngle=ls.outerConeAngle;
                sl.position[0]=ls.position[0]+kOffset.x; sl.position[1]=ls.position[1]; sl.position[2]=ls.position[2]+kOffset.z;
                sl.direction[0]=ls.direction[0]; sl.direction[1]=ls.direction[1]; sl.direction[2]=ls.direction[2];
                e = engine_.world->addLight(sl);
            }
            if (engine_.world->getRegistry().valid(e)) selection_.add(e);
            continue;
        }

        if (!engine_.world->getRegistry().valid(e)) continue;

        // Copy hull properties (damping, friction, restitution, gravity flag etc.)
        if (entry.hasHull) {
            if (auto* h = engine_.world->getRegistry().get<ecs::Hull>(e)) *h = entry.hull;
        }
        if (entry.hasFixed && !engine_.world->getRegistry().has<ecs::Fixed>(e))
            engine_.world->getRegistry().add<ecs::Fixed>(e);

        // Restore transform scale and rotation
        if (entry.hasTransform) {
            if (auto* tf = engine_.world->getRegistry().get<Transform>(e)) {
                tf->rotation = entry.transform.rotation;
                tf->scale    = entry.transform.scale;
            }
        }

        // Recreate mesh
        if (entry.hasMesh) {
            RenderMesh* rm = nullptr;
            if (!entry.cpuVerts.empty()) {
                rm = engine_.world->attachMesh(e, entry.cpuVerts, entry.cpuInds);
            } else {
                math::Vec3 sc = entry.hasTransform ? entry.transform.scale : math::Vec3{1,1,1};
                switch (entry.primType) {
                    case PrimitiveType::Sphere:
                    case PrimitiveType::Icosphere:
                        rm = engine_.world->attachMesh(e, Primitives::sphere(1.0f)); break;
                    case PrimitiveType::Cube:
                    case PrimitiveType::Rect:
                    case PrimitiveType::Plane:
                        rm = engine_.world->attachMesh(e, Primitives::rect({1.0f,1.0f,1.0f})); break;
                    default: break;
                }
            }
            if (rm) {
                rm->color[0] = entry.meshColor[0];
                rm->color[1] = entry.meshColor[1];
                rm->color[2] = entry.meshColor[2];
                rm->transformReady = true;
            }
        }

        if (entry.hasName) {
            if (auto* n = engine_.world->getRegistry().get<ecs::Name>(e)) *n = entry.name;
        }

        selection_.add(e);
    }
}
