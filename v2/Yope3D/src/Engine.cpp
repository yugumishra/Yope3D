#include "Engine.h"
#include "platform/BundlePaths.h"
#include "scripting/Config.h"
#include "scripting/Script.h"
#include "scene/SceneManager.h"
#include "physics/PhysicsConstants.h"
#include "math/Math.h"
#include "ui/UIManager.h"
#include "ui/UIInput.h"
#include "ui/Background.h"
#include "ui/TexturedBackground.h"
#include "gpu/Buffer.h"   // BufferUploadBatch (async-load mesh upload batching)
#include "debug/Profiler.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "audio/Source.h"
#include "assets/AssetResolve.h"
#include <GLFW/glfw3.h>
#include <string>
#include <chrono>
#include <cmath>     // std::sin for splash animation
#include <cstdlib>   // YOPE_PROFILE_DURATION env var
#include <algorithm> // std::clamp / std::max for the splash-logo timeline

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
    // The animated line logo is drawn as world-space debug lines in the 3D pass
    // over the dark clear. The renderer suppresses ALL scene drawing during load
    // (setSuppressScene) so the building scene stays hidden without an opaque UI
    // cover — which would otherwise hide the logo. The UI pass adds only a slim
    // bottom progress bar over it.
    splashTrack_ = uiManager->addBackground({0.34f, 0.900f}, {0.66f, 0.908f},
                                            {1.0f, 1.0f, 1.0f, 0.12f}, 1);
    splashFill_  = uiManager->addBackground({0.34f, 0.900f}, {0.34f, 0.908f},
                                            {0.85f, 0.87f, 0.95f, 0.75f}, 2);
    renderer->setSuppressScene(true);

    // Save the camera so the loaded scene isn't left with the splash framing.
    savedCamPos_ = camera->getPosition();
    savedCamRot_ = camera->getRotation();
    savedCamFov_ = camera->getFov();

    // Load the packed logo binary on a worker (one buffer, no parsing — ready in
    // ~ms). Both clips live in it, so the reveal + tumble are available at once.
    logoReady_.store(false, std::memory_order_release);
    logoLoadThread_ = std::thread([this] {
        if (logo_.loadFromMemory(assets::readBytes("logo/logo.bin")))
            logoReady_.store(true, std::memory_order_release);
    });
#endif

    // Parse on a worker thread — parseScene touches no World/registry/GPU state.
    parseThread_ = std::thread([this] {
        parsed_ = SceneSerializer::parseScene(scenePath_.c_str());
        parseDone_.store(true, std::memory_order_release);
    });
}

void Engine::updateSplash() {
    // Timeline / policy constants (agreed design). All reactive to elapsed time —
    // the load ETA is unknowable (texture streaming is indeterminate).
    constexpr float MIN_SPLASH = 3.0f;    // guarantee legibility; kills the fast-load flash
    constexpr float T_CUE      = 3.0f;    // idle time before the ball drops (long loads)
    constexpr float FADE_DUR   = 0.35f;   // outro cross-fade

    const double now = glfwGetTime();
    const float  t   = static_cast<float>(now - splashStart_);

    // Outro fade — only after MIN_SPLASH, so a fast load still shows the logo.
    float vis = 1.0f;
    splashOutroDone_ = false;
    if (loadPhase_ == LoadPhase::Outro) {
        const double fadeStart = std::max(outroStart_, splashStart_ + MIN_SPLASH);
        float fp = FADE_DUR > 0.0f ? static_cast<float>((now - fadeStart) / FADE_DUR) : 1.0f;
        fp  = std::clamp(fp, 0.0f, 1.0f);
        vis = 1.0f - fp;
        if (fp >= 1.0f) splashOutroDone_ = true;
    }

    // ---- animated line logo (world-space debug lines in the 3D pass) ----
    if (logoReady_.load(std::memory_order_acquire)) {
        const LogoClipView& c1 = logo_.part1;
        const LogoClipView& c2 = logo_.part2;
        const float p1 = c1.durationSec();
        const float p2 = c2.durationSec();

        // reveal -> idle(breathe, wait T_CUE) -> tumble, then loop.
        const LogoClipView* clip = &c1;
        int  frame = 0;
        bool idle  = false;
        const float period = p1 + T_CUE + p2;
        const float local  = std::fmod(t, period > 0.0f ? period : 1.0f);
        if (local < p1) {
            frame = static_cast<int>(local * c1.fps);
        } else if (local < p1 + T_CUE) {
            frame = c1.frameCount - 1; idle = true;          // standing == part2 frame 0
        } else {
            clip  = &c2;
            frame = static_cast<int>((local - p1 - T_CUE) * c2.fps);
        }
        frame = std::clamp(frame, 0, std::max(0, clip->frameCount - 1));

        // Frame the unit plane (matches scripts/behaviors/logo_playback.py).
        const float halfH = 0.5f, halfW = 0.5f * clip->refAspect;
        {
            const int   w  = window->getWidth(), h = window->getHeight();
            const float sa = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : clip->refAspect;
            const float vfov = 50.0f * 3.14159265f / 180.0f;
            const float tvh  = std::tan(vfov * 0.5f);
            const float cz   = std::max(halfH / tvh, halfW / (sa * tvh)) * 1.05f;
            camera->setFOV(vfov);
            camera->setPosition({0.0f, 0.0f, cz});
            camera->lookAt({0.0f, 0.0f, 0.0f});
        }

        // Emit the frame's segments (uint16 grid -> cam-view coords, which may be
        // off-screen so the sphere rolls in/out cleanly). Fade via rgb toward the
        // dark clear (0.05).
        world->clearDebugLines();
        if (frame + 1 < static_cast<int>(clip->frameStart.size())) {
            const uint32_t b = clip->frameStart[frame];
            const uint32_t e = clip->frameStart[frame + 1];
            const math::Vec3 col{vis, vis, vis};
            const float lo = clip->coordLo, scale = (clip->coordHi - clip->coordLo) / 65535.0f;
            for (uint32_t k = b; k + 3 < e; k += 4) {
                const float u0 = lo + clip->coords[k]     * scale, v0 = lo + clip->coords[k + 1] * scale;
                const float u1 = lo + clip->coords[k + 2] * scale, v1 = lo + clip->coords[k + 3] * scale;
                world->addDebugLine({(u0 - 0.5f) * 2.0f * halfW, (v0 - 0.5f) * 2.0f * halfH, 0.0f},
                                    {(u1 - 0.5f) * 2.0f * halfW, (v1 - 0.5f) * 2.0f * halfH, 0.0f}, col);
            }
        }

        // Breathe the stroke width while idle; steady while drawing / tumbling.
        const float base = 2.6f;
        const float wpx  = idle ? base * (1.0f + 0.22f * std::sin(static_cast<float>(now) * 3.0f)) : base;
        renderer->setDebugStrokeStyle(wpx, 1.4f);
    }

    // ---- progress bar (bottom) ----
    if (splashFill_) {
        const float pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
        const float entityFrac = totalEntities_ > 0
            ? static_cast<float>(committedEntities_) / static_cast<float>(totalEntities_) : 1.0f;
        const bool streaming = (loadPhase_ == LoadPhase::Streaming);
        const float progress = streaming ? 0.92f : 0.90f * entityFrac;
        const float x0 = 0.34f, x1 = 0.66f;
        splashFill_->setBounds({x0, 0.900f}, {x0 + (x1 - x0) * progress, 0.908f});
        splashFill_->setColor({0.85f, 0.87f, 0.95f, (0.55f + 0.35f * pulse) * vis});
    }
    if (splashTrack_) splashTrack_->setColor({1.0f, 1.0f, 1.0f, 0.12f * vis});
    if (splashBg_) splashBg_->setColor({0.05f, 0.05f, 0.05f, 1.0f * vis});
}

void Engine::finishAsyncLoad() {
    // Publish one snapshot so every mesh's modelMatrix is set from its transform
    // before the splash is removed — otherwise the first frame or two (before the
    // physics thread produces its first snapshot) would draw meshes at the origin.
    world->publishSnapshot();
    world->newSnapshotReady_.store(false, std::memory_order_release);
    world->syncRenderMeshesFromFront();

    // Remove the splash UI (some pointers may be null — the runtime splash only
    // builds the progress bar; the editor builds none of these).
    if (splashFill_)  uiManager->remove(splashFill_);
    if (splashTrack_) uiManager->remove(splashTrack_);
    if (splashLogo_)  uiManager->remove(splashLogo_);
    if (splashBg_)    uiManager->remove(splashBg_);
    splashFill_ = splashTrack_ = splashBg_ = nullptr;
    splashLogo_ = nullptr;

    // Tear down the animated line logo: stop feeding lines, reset the stroke
    // style, restore the camera the scene will now drive, and free the clips
    // (~100MB). (Runtime-only; the editor never started the loader thread.)
    if (logoLoadThread_.joinable()) logoLoadThread_.join();
    world->clearDebugLines();
    renderer->setDebugStrokeStyle(2.5f, 1.0f);
    renderer->setSuppressScene(false);
    if (savedCamFov_ > 0.0f) {
        camera->setPosition(savedCamPos_);
        camera->setRotation(savedCamRot_);
        camera->setFOV(savedCamFov_);
    }
    logo_.clear();

    // Adopt the scene path + instantiate runtime scripts (editor defers to Play).
    // Scripts may set their own camera here, overriding the restore above.
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
        if (!assets->isStreamingTextures()) {
            // Load work is done, but don't rip the splash out yet: the Outro
            // holds for MIN_SPLASH then cross-fades (updateSplash owns the timing).
            loadPhase_  = LoadPhase::Outro;
            outroStart_ = glfwGetTime();
        }
        return;
    }

    if (loadPhase_ == LoadPhase::Outro) {
        updateSplash();
        if (splashOutroDone_) finishAsyncLoad();
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

    updateScripts(dt);

    // Listener tracks camera (updated after script may have moved it).
    Listener::setPosition(camera->getPosition());
    Listener::setOrientation(camera->getForward(), {0.0f, 1.0f, 0.0f});

    // Advances gain fades and refills streaming music buffers.
    audio->update(dt);

    // Sync AudioSource positions from their entity Transforms so 3D audio follows
    // the editor's spatial layout. Source* may be null for unbound audio entities.
    for (auto [e, tf, as] : world->getRegistry().view<Transform, ecs::AudioSource>()) {
        if (as.source) as.source->setPosition(tf.position);
    }

    // Profile-sweep auto-exit. main loop checks window->shouldClose().
    if (profileEndTime_ > 0.0 && now >= profileEndTime_)
        glfwSetWindowShouldClose(window->getHandle(), GLFW_TRUE);
}

void Engine::updateScripts(float dt) {
    // Route the pointer to ECS UI entities BEFORE scripts run this frame's
    // update() — otherwise a script polling ui_consumed_click()/ui_hovered()/
    // ui_hit_test() (or reacting to an on_ui_press/release callback fired
    // below) would see LAST frame's routing result instead of this frame's,
    // since script_update used to run first. That one-frame lag made a
    // same-frame "did my click land on a UI button" check unreliable (e.g. a
    // script polling ui_consumed_click() in update() right after clicking a
    // button would still see False for that frame). Mirrors the collision-
    // event drain below (resolve Script* under the structure lock, run the
    // callback outside it).
    {
        YOPE_PROF_SCOPE("ui_input_route", "render");
        // GLFW reports cursor position in screen points; getWidth()/getHeight()
        // are framebuffer pixels (they can differ by the display's content
        // scale on HiDPI/Retina screens) — convert before dividing so the
        // fraction stays correct regardless of window size/DPI.
        float scaleX, scaleY;
        window->getContentScale(scaleX, scaleY);
        float fx = (static_cast<float>(input->getCursorX()) * scaleX) / window->getWidth();
        float fy = (static_cast<float>(input->getCursorY()) * scaleY) / window->getHeight();

        constexpr int kNumMouseButtons = GLFW_MOUSE_BUTTON_LAST + 1;
        bool pressedEdge[kNumMouseButtons];
        bool releasedEdge[kNumMouseButtons];
        for (int b = 0; b < kNumMouseButtons; ++b) {
            pressedEdge[b]  = input->isMousePressed(b);
            releasedEdge[b] = input->isMouseReleased(b);
        }

        auto uiEvents = world->updateUIInput(fx, fy,
                                              static_cast<float>(window->getWidth()),
                                              static_cast<float>(window->getHeight()),
                                              pressedEdge, releasedEdge, kNumMouseButtons);
        if (!uiEvents.empty()) {
            auto& reg = world->getRegistry();
            for (auto& ev : uiEvents) {
                if (ev.type == UIInputEvent::Type::Click) continue; // Press+Release already dispatched
                Script* inst = nullptr;
                {
                    auto lock = world->lockStructure();
                    if (!reg.valid(ev.entity)) continue;
                    if (auto* sc = reg.get<ecs::ScriptComponent>(ev.entity)) inst = sc->instance;
                }
                if (!inst) continue;
                switch (ev.type) {
                    case UIInputEvent::Type::Press:   inst->onUIPress  (scriptCtx_, ev.entity); break;
                    case UIInputEvent::Type::Release: inst->onUIRelease(scriptCtx_, ev.entity); break;
                    case UIInputEvent::Type::Enter:   inst->onUIEnter  (scriptCtx_, ev.entity); break;
                    case UIInputEvent::Type::Leave:   inst->onUILeave  (scriptCtx_, ev.entity); break;
                    case UIInputEvent::Type::Click:   break;
                }
            }
        }

        // Dispatch this frame's typed codepoints to whichever UI entity holds
        // focus (set by the router above on press — see UIInputRouter::update).
        const auto& typedChars = input->getTypedChars();
        if (!typedChars.empty()) {
            ecs::Entity focused = world->uiFocused();
            if (focused != ecs::NullEntity) {
                Script* inst = nullptr;
                {
                    auto lock = world->lockStructure();
                    auto& reg = world->getRegistry();
                    if (reg.valid(focused)) {
                        if (auto* sc = reg.get<ecs::ScriptComponent>(focused)) inst = sc->instance;
                    }
                }
                if (inst) {
                    for (unsigned int cp : typedChars)
                        inst->onTextInput(scriptCtx_, focused, cp);
                }
            }
        }
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
        // During the async load, the splash logo owns the debug lines (set in
        // updateSplash, which runs in pumpSceneLoad before this) — don't wipe them.
        if (isSceneLoaded()) world->clearDebugLines();

        // Collect under the structure lock: view iteration on the render thread
        // would race with archetype migrations (e.g. Fixed-tag toggles) on the physics thread.
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
                // archetype migrations on the physics thread), then release it
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
    world->resolvePendingUITextures();
    world->updateTweens(dt);
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
    // May still be parsing / streaming the logo if the user quit during the splash.
    if (parseThread_.joinable())     parseThread_.join();
    if (logoLoadThread_.joinable())  logoLoadThread_.join();

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
