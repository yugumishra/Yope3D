#include "Engine.h"
#include "platform/BundlePaths.h"
#include "scripting/Config.h"
#include "scripting/Script.h"
#include "scene/SceneManager.h"
#include "physics/PhysicsConstants.h"
#include "math/Math.h"
#include "ui/UIManager.h"
#include "ui/Background.h"
#include "ui/TexturedBackground.h"
#include "gpu/Buffer.h"   // BufferUploadBatch (async-load mesh upload batching)
#include "debug/Profiler.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "audio/Source.h"
#include <GLFW/glfw3.h>
#include <string>
#include <chrono>
#include <cmath>     // std::sin for splash animation
#include <cstdlib>   // YOPE_PROFILE_DURATION env var

Engine::~Engine() = default;

bool Engine::init(const std::string& sceneOverride) {
    input = std::make_unique<Input>();
    if (!glfwInit()) return false;

    const std::string resDir = bundleResourcesDir();

#ifdef __APPLE__
    // When running from a .app bundle the Vulkan loader won't find MoltenVK at
    // its system-installed location. Point it at our bundled ICD JSON before
    // VkInstance creation.
    if (!resDir.empty()) {
        std::string icd = resDir + "/vulkan/icd.d/MoltenVK_icd.json";
        setenv("VK_ICD_FILENAMES", icd.c_str(), 1);
    }
#endif

    Config cfg = Config::load(resDir.empty() ? "yope3d.cfg" : resDir + "/yope3d.cfg");
    if (!sceneOverride.empty()) cfg.startupScene = sceneOverride;

    int screenW = 1920, screenH = 1080;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor())
        if (const GLFWvidmode* mode = glfwGetVideoMode(primary))
            { screenW = mode->width; screenH = mode->height; }
    if (cfg.width  > 0) screenW = cfg.width;
    if (cfg.height > 0) screenH = cfg.height;

    window = std::make_unique<Window>(
#ifdef YOPE_EDITOR
        "Yope3D Editor"
#else
        "Yope3D"
#endif
        , screenW, screenH);
    window->init(input.get());
    window->setIcon("textures/tnail.png");
#ifdef YOPE_EDITOR
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
#else
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#endif

    gpu      = std::make_unique<GpuDevice>(*window);
    renderer = std::make_unique<Renderer>(*gpu, *window);
    assets   = std::make_unique<AssetManager>();
    assets->init(*gpu, renderer->getCommandPool(), renderer->getTextureSetLayout(),
                 renderer->getMaterialSetLayout());

    world = std::make_unique<World>();
    world->init(*gpu, renderer->getCommandPool());
    world->setAssetManager(assets.get());

    camera = std::make_unique<Camera>(screenW, screenH, math::toRadians(70.0f));

    audio = std::make_unique<AudioSystem>();
    audio->init();
    world->setAudioSystem(audio.get());
    Listener::setGain(1.0f);

    uiManager = std::make_unique<UIManager>();
    uiManager->init(*gpu, renderer->getCommandPool(), renderer->getTextureSetLayout(),
                    static_cast<float>(screenW), static_cast<float>(screenH));
    renderer->setUIManager(uiManager.get());

    sceneManager = std::make_unique<SceneManager>(*world, audio.get(), assets.get());

#ifdef YOPE_PYTHON
    python = std::make_unique<PythonInterpreter>();
    python->init(
        resDir.empty() ? YOPE_SCRIPTS_DIR : resDir + "/scripts",
        resDir.empty() ? ""               : resDir + "/python"
    );
#endif

    scriptCtx_.world         = world.get();
    scriptCtx_.camera        = camera.get();
    scriptCtx_.input         = input.get();
    scriptCtx_.audio         = audio.get();
    scriptCtx_.assets        = assets.get();
    scriptCtx_.window        = window.get();
    scriptCtx_.ui            = uiManager.get();
    scriptCtx_.renderMode    = &renderMode_;
    scriptCtx_.sceneManager  = sceneManager.get();

#ifdef YOPE_PYTHON
    python->bindContext(scriptCtx_);
#endif

    if (cfg.startupScene.empty()) {
        std::fprintf(stderr,
            "Engine: yope3d.cfg is missing 'startupScene='. Refusing to launch with no scene.\n");
        return false;
    }
    // Resolve relative scene paths against the assets directory so the runtime
    // works regardless of CWD. Absolute paths and already-prefixed paths are
    // left untouched.
    std::string scenePath = cfg.startupScene;
    if (!scenePath.empty() && scenePath[0] != '/' &&
        scenePath.rfind("assets/", 0) != 0) {
        scenePath = std::string(YOPE_ASSETS_DIR) + "/" + scenePath;
    }
    // Runtime mode: instantiate + init() scripts immediately.
    // Editor mode: instantiate is deferred until Play press, but the scene's
    //              entities still load so the editor can show / edit them.
    const bool initScriptsOnLoad =
#ifdef YOPE_EDITOR
        false;
#else
        true;
#endif
    YOPE_PROF_INIT("yope_profile.csv");

    lastTime = glfwGetTime();

    // Optional fixed-duration auto-exit for tools/run_scaling_sweep.sh.
    if (const char* s = std::getenv("YOPE_PROFILE_DURATION")) {
        double d = std::atof(s);
        if (d > 0.0) profileEndTime_ = lastTime + d;
    }

    // Parse the startup scene on a background thread and build the loading splash.
    // The main loop starts immediately; pumpSceneLoad() commits the scene in
    // batches and starts the physics thread once the load completes.
    beginAsyncLoad(scenePath, initScriptsOnLoad);

    return true;
}

void Engine::startPhysicsThread() {
    physicsThread_ = std::thread([this] {
        using namespace std::chrono_literals;
        double last    = glfwGetTime();
        float  accum   = 0.0f;
        while (!stopPhysics_.load(std::memory_order_relaxed)) {
            double now = glfwGetTime();
            float  dt  = std::min(static_cast<float>(now - last), 0.05f);
            last = now;
            accum = std::min(accum + dt, physics::MAX_PHYSICS_ACCUMULATOR);
            while (accum >= physics::PHYSICS_DT) {
                world->advance(physics::PHYSICS_DT);
                accum -= physics::PHYSICS_DT;
            }
            std::this_thread::sleep_for(100us);
        }
    });
}

// ---------------------------------------------------------------------------
// Asynchronous startup-scene load
// ---------------------------------------------------------------------------

void Engine::beginAsyncLoad(const std::string& scenePath, bool initScripts) {
    scenePath_         = scenePath;
    initScriptsOnLoad_ = initScripts;
    committedEntities_ = 0;
    totalEntities_     = 0;
    parseDone_.store(false, std::memory_order_release);
    loadPhase_   = LoadPhase::Parsing;
    splashStart_ = glfwGetTime();

    // Build the loading splash. Reuses the existing UI system (already initialized
    // above), which renders over the empty world via the standalone UI pass. A
    // full-screen opaque background hides the grey clear and the scene as it builds.
    //
    // Runtime only: the UI pass renders into the swapchain, so this fills the whole
    // window. In the editor the UI pass renders into the offscreen ViewportTarget
    // (which would put the splash *inside* the viewport panel), so the editor draws
    // its own window-level ImGui overlay during load instead — see EditorApp.
#ifndef YOPE_EDITOR
    Texture* logo = nullptr;
    try { logo = assets->loadTexture("textures/tnail.png"); } catch (...) {}
    splashBg_    = uiManager->addBackground({0.0f, 0.0f}, {1.0f, 1.0f},
                                            {0.06f, 0.07f, 0.09f, 1.0f}, -100);
    splashLogo_  = uiManager->addTexturedBackground({0.42f, 0.34f}, {0.58f, 0.50f},
                                                    {1.0f, 1.0f, 1.0f, 1.0f}, logo, 0);
    splashTrack_ = uiManager->addBackground({0.32f, 0.600f}, {0.68f, 0.616f},
                                            {1.0f, 1.0f, 1.0f, 0.15f}, 1);
    splashFill_  = uiManager->addBackground({0.32f, 0.600f}, {0.32f, 0.616f},
                                            {0.85f, 0.87f, 0.95f, 0.9f}, 2);
#endif

    // Parse on a worker thread — parseScene touches no World/registry/GPU state.
    parseThread_ = std::thread([this] {
        parsed_ = SceneSerializer::parseScene(scenePath_.c_str());
        parseDone_.store(true, std::memory_order_release);
    });
}

void Engine::updateSplash() {
    if (!splashLogo_ || !splashFill_) return;
    const float t     = static_cast<float>(glfwGetTime() - splashStart_);
    const float pulse = 0.5f + 0.5f * std::sin(t * 3.0f);

    // Logo: subtle alpha + scale pulse (no shader change — the UI textured path
    // already multiplies the sampled texel by the per-vertex tint).
    const float a  = 0.72f + 0.28f * pulse;
    const float s  = 1.0f + 0.02f * pulse;
    const float cx = 0.5f, cy = 0.42f, hw = 0.08f, hh = 0.08f;
    splashLogo_->setBounds({cx - hw * s, cy - hh * s}, {cx + hw * s, cy + hh * s});
    splashLogo_->setColor({1.0f, 1.0f, 1.0f, a});

    // Progress bar: entity-commit fraction fills up to 90%; the last 10% waits on
    // texture streaming (indeterminate — pulse the fill so it doesn't look frozen).
    const float entityFrac = totalEntities_ > 0
                               ? static_cast<float>(committedEntities_) / static_cast<float>(totalEntities_)
                               : 1.0f;
    const float progress = (loadPhase_ == LoadPhase::Streaming) ? 0.92f : 0.90f * entityFrac;
    const float x0 = 0.32f, x1 = 0.68f;
    splashFill_->setBounds({x0, 0.600f}, {x0 + (x1 - x0) * progress, 0.616f});
    if (loadPhase_ == LoadPhase::Streaming)
        splashFill_->setColor({0.85f, 0.87f, 0.95f, 0.55f + 0.45f * pulse});
}

void Engine::finishAsyncLoad() {
    // Publish one snapshot so every mesh's modelMatrix is set from its transform
    // before the splash is removed — otherwise the first frame or two (before the
    // physics thread produces its first snapshot) would draw meshes at the origin.
    world->publishSnapshot();
    world->newSnapshotReady_.store(false, std::memory_order_release);
    world->syncRenderMeshesFromFront();

    // Remove the splash UI.
    uiManager->remove(splashFill_);
    uiManager->remove(splashTrack_);
    uiManager->remove(splashLogo_);
    uiManager->remove(splashBg_);
    splashFill_ = splashTrack_ = splashBg_ = nullptr;
    splashLogo_ = nullptr;

    // Adopt the scene path + instantiate runtime scripts (editor defers to Play).
    sceneManager->onAsyncLoadComplete(scriptCtx_, scenePath_, initScriptsOnLoad_);

    // Release the parsed-scene buffers (snapshots retain CPU mesh copies).
    parsed_ = SceneSerializer::ParsedScene{};

    startPhysicsThread();
    loadPhase_ = LoadPhase::Done;
}

void Engine::pumpSceneLoad() {
    if (loadPhase_ == LoadPhase::Done) return;

    if (loadPhase_ == LoadPhase::Parsing) {
        if (!parseDone_.load(std::memory_order_acquire)) { updateSplash(); return; }
        if (parseThread_.joinable()) parseThread_.join();
        if (!parsed_.ok) {
            std::fprintf(stderr, "Engine: startup scene parse failed: %s\n",
                         parsed_.error.c_str());
            finishAsyncLoad();   // proceed with an empty scene rather than hang
            return;
        }
        SceneSerializer::commitBegin(parsed_, *world);
        totalEntities_     = static_cast<int>(parsed_.entities.size());
        committedEntities_ = 0;
        loadPhase_         = LoadPhase::Committing;
        // fall through and commit the first batch this frame
    }

    if (loadPhase_ == LoadPhase::Committing) {
        // Commit entity snapshots (registry mutation + GPU mesh upload) within a
        // per-frame wall-clock budget so the splash keeps animating between batches.
        // Every mesh upload this frame is recorded into ONE command buffer and
        // flushed with a single fenced submit — instead of the blocking
        // vkQueueWaitIdle that RenderMesh's synchronous path does per vertex/index
        // buffer (2 drains × N meshes).
        BufferUploadBatch batch{};
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool        = renderer->getCommandPool();
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(gpu->device(), &ai, &batch.cmd);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(batch.cmd, &bi);
        world->setUploadBatch(&batch);

        constexpr double kBudgetMs = 6.0;
        const auto start = std::chrono::steady_clock::now();
        while (!SceneSerializer::commitDone(parsed_)) {
            SceneSerializer::commitEntities(parsed_, *world, 2);
            committedEntities_ = static_cast<int>(parsed_.cursor);
            const double el = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (el >= kBudgetMs) break;
        }

        world->setUploadBatch(nullptr);
        vkEndCommandBuffer(batch.cmd);
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &batch.cmd;
        VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(gpu->device(), &fi, nullptr, &fence);
        vkQueueSubmit(gpu->graphicsQueue(), 1, &si, fence);
        vkWaitForFences(gpu->device(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(gpu->device(), fence, nullptr);
        for (auto& s : batch.staging) s.destroy(gpu->device());
        vkFreeCommandBuffers(gpu->device(), renderer->getCommandPool(), 1, &batch.cmd);

        updateSplash();
        if (SceneSerializer::commitDone(parsed_)) {
            SceneSerializer::commitFinalize(parsed_, *world, audio.get(), assets.get(),
                                            initScriptsOnLoad_);
#ifdef YOPE_EDITOR
            // Editor: reveal the editor as soon as meshes are committed. Embedded
            // textures keep streaming in the background and report to the existing
            // menu-bar progress bar (TaskProgress) — no full-window wait for them.
            finishAsyncLoad();
#else
            // Runtime: hold the splash until textures finish so the scene doesn't
            // pop in with placeholder textures.
            loadPhase_ = LoadPhase::Streaming;
#endif
        }
        return;
    }

    if (loadPhase_ == LoadPhase::Streaming) {
        // Hold the splash until embedded-image textures finish streaming so the
        // scene doesn't pop in with placeholder textures.
        updateSplash();
        if (!assets->isStreamingTextures())
            finishAsyncLoad();
        return;
    }
}

void Engine::update() {
    YOPE_PROF_STEP("render");
    YOPE_PROF_SCOPE("total_frame", "render");

    double now = glfwGetTime();
    float  dt  = static_cast<float>(now - lastTime);
    lastTime   = now;
    if (dt > 0.05f) dt = 0.05f;

    fpsAccum += dt;
    ++fpsFrames;
    if (fpsAccum >= 0.5f) {
        displayFps = static_cast<int>(fpsFrames / fpsAccum + 0.5f);
        fpsAccum  = 0.0f;
        fpsFrames = 0;
    }

    if (window->wasResized()) {
        camera->WindowChanged(window->getWidth(), window->getHeight());
        uiManager->handleResize(static_cast<float>(window->getWidth()),
                                static_cast<float>(window->getHeight()));
    }

    {
        YOPE_PROF_SCOPE("script_update", "render");
        // Apply any queued scene swap before scripts run this frame. In editor mode
        // we don't auto-init the new scripts (Play handles that); in runtime mode
        // the new scripts get init() inside the flush.
#ifdef YOPE_EDITOR
        sceneManager->flush(scriptCtx_, /*initScripts=*/false);
#else
        sceneManager->flush(scriptCtx_, /*initScripts=*/true);
#endif
        // Per-frame debug lines: clear before scripts run so yope3d.draw_line()
        // accumulates fresh segments each frame (Renderer reads getDebugLines()).
        world->clearDebugLines();

        // Collect under the structure lock: view iteration on the render thread
        // would race with Sleeping-tag archetype migrations on the physics thread.
        // Scripts execute outside the lock — they call World methods that acquire
        // it themselves, and may freely create/destroy entities (two-pass pattern).
        std::vector<std::pair<ecs::Entity, Script*>> active;
        {
            auto lock = world->lockStructure();
            for (auto [e, sc] : world->getRegistry().view<ecs::ScriptComponent>())
                if (sc.instance) active.push_back({e, sc.instance});
        }
        for (auto& [e, inst] : active) inst->update(scriptCtx_, e, dt);

        // Collision events only matter when a behavior is live — enabling lazily keeps
        // script-less / stress scenes at zero cost (physics skips the whole diff).
        world->setCollisionEventsEnabled(!active.empty());

        // Drain physics-thread collision events and dispatch enter/exit to both
        // entities' behaviors. reg.valid guards against destruction between tick & drain.
        auto events = world->drainCollisionEvents();
        if (!events.empty()) {
            auto& reg = world->getRegistry();
            auto dispatch = [&](ecs::Entity self, ecs::Entity other, bool enter) {
                // Resolve the behavior under the structure lock (the reg.get races
                // Sleeping-tag migrations on the physics thread), then release it
                // before running Python — the Script* is a stable heap pointer.
                Script* inst = nullptr;
                {
                    auto lock = world->lockStructure();
                    if (!reg.valid(self)) return;
                    if (auto* sc = reg.get<ecs::ScriptComponent>(self)) inst = sc->instance;
                }
                if (!inst) return;
                if (enter) inst->onCollisionEnter(scriptCtx_, self, other);
                else       inst->onCollisionExit (scriptCtx_, self, other);
            };
            for (auto& ev : events) {
                dispatch(ev.a, ev.b, ev.enter);
                dispatch(ev.b, ev.a, ev.enter);
            }
        }
    }
    { YOPE_PROF_SCOPE("ui_update",     "render"); uiManager->update(dt); }

    // Dispatch UI click on LMB press edge (held→not tracked, only the falling edge).
    bool lmbNow = input->isLMBDown();
    if (lmbNow && !prevLMB_) {
        double mx, my;
        glfwGetCursorPos(window->getHandle(), &mx, &my);
        float fx = static_cast<float>(mx) / window->getWidth();
        float fy = static_cast<float>(my) / window->getHeight();
        uiManager->handleClick(fx, fy, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    }
    prevLMB_ = lmbNow;

    // Listener tracks camera (updated after script may have moved it).
    Listener::setPosition(camera->getPosition());
    Listener::setOrientation(camera->getForward(), {0.0f, 1.0f, 0.0f});

    // Sync AudioSource positions from their entity Transforms so 3D audio follows
    // the editor's spatial layout. Source* may be null for unbound audio entities.
    for (auto [e, tf, as] : world->getRegistry().view<Transform, ecs::AudioSource>()) {
        if (as.source) as.source->setPosition(tf.position);
    }

    // Profile-sweep auto-exit. main loop checks window->shouldClose().
    if (profileEndTime_ > 0.0 && now >= profileEndTime_)
        glfwSetWindowShouldClose(window->getHandle(), GLFW_TRUE);
}

void Engine::render() {
    if (world->newSnapshotReady_.exchange(false, std::memory_order_acquire)) {
        YOPE_PROF_SCOPE("snapshot_sync", "render");
        world->syncRenderMeshesFromFront();
    }
    // Upload textures streamed in from background decode (glb embedded images).
    // Must run on this thread — the graphics queue is externally synchronized
    // with drawFrame() below.
    assets->pumpTextureUploads();
    renderer->setMode(renderMode_);
    { YOPE_PROF_SCOPE("renderer_drawframe", "render");
      renderer->drawFrame(*gpu, *window, *camera, *world, *assets); }
}

void Engine::cleanup() {
    // Be tolerant of partial init: main calls cleanup even on init failure so
    // every subsystem that came up gets torn down properly.
    stopPhysics_.store(true, std::memory_order_release);
    if (physicsThread_.joinable()) physicsThread_.join();
    // May still be parsing if the user quit during the loading splash.
    if (parseThread_.joinable()) parseThread_.join();

    if (sceneManager) sceneManager->shutdown(scriptCtx_);
    sceneManager.reset();
#ifdef YOPE_PYTHON
    // Shut down Python AFTER all Script* instances are destroyed (sceneManager->shutdown above).
    if (python) { python->shutdown(); python.reset(); }
#endif
    audio.reset();
    camera.reset();
    if (renderer && gpu) renderer->waitIdle(*gpu);
    if (uiManager && gpu) { uiManager->cleanup(gpu->device()); }
    uiManager.reset();
    if (assets && gpu) { assets->cleanup(gpu->device()); }
    assets.reset();
    if (world) { world->cleanup(); world.reset(); }
    renderer.reset();
    gpu.reset();

    // Must come AFTER world.reset(): worker TLBs flush in ~ThreadLocalBuf which
    // takes the global mutex and writes to g_file. If we close g_file first,
    // the tail of the last few physics steps gets silently dropped.
    YOPE_PROF_SHUTDOWN();
    window.reset();
    input.reset();
}
