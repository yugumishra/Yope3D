#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include "platform/Window.h"
#include "platform/Input.h"
#include "gpu/GpuDevice.h"
#include "rendering/Renderer.h"
#include "rendering/Camera.h"
#include "rendering/RenderMode.h"
#include "rendering/LogoClip.h"
#include "world/World.h"
#include "assets/AssetManager.h"
#include "audio/AudioSystem.h"
#include "audio/Listener.h"
#include "scripting/ScriptContext.h"
#include "scene/SceneManager.h"
#include "scene/serialization/SceneSerializer.h"   // ParsedScene (async load state)
#include "ui/UIManager.h"
#ifdef YOPE_PYTHON
#include "scripting/python/PythonInterpreter.h"
#endif

class Background;
class TexturedBackground;

struct Engine {
    std::unique_ptr<Window>        window;
    std::unique_ptr<Input>         input;
    std::unique_ptr<GpuDevice>     gpu;
    std::unique_ptr<Renderer>      renderer;
    std::unique_ptr<Camera>        camera;
    std::unique_ptr<World>         world;
    std::unique_ptr<AssetManager>  assets;
    std::unique_ptr<AudioSystem>   audio;
    std::unique_ptr<UIManager>     uiManager;
    std::unique_ptr<SceneManager>  sceneManager;
#ifdef YOPE_PYTHON
    std::unique_ptr<PythonInterpreter> python;
#endif

    ScriptContext scriptCtx_;

    std::thread       physicsThread_;
    std::atomic<bool> stopPhysics_{ false };

    // ---- Asynchronous startup-scene load ----
    // The startup scene is parsed on parseThread_ (off the main thread) into
    // parsed_, then committed to the registry in budgeted batches on the main
    // thread by pumpSceneLoad(). A loading splash renders while this runs, and the
    // physics thread + runtime script init() are deferred until it completes.
    // Outro = load work is done, but the splash holds/fades out (min display +
    // fade) before finishAsyncLoad tears it down. Runtime only.
    enum class LoadPhase { Parsing, Committing, Streaming, Outro, Done };
    LoadPhase                    loadPhase_ = LoadPhase::Done;
    std::thread                  parseThread_;
    std::atomic<bool>            parseDone_{ false };
    SceneSerializer::ParsedScene parsed_;
    std::string                  scenePath_;
    bool                         initScriptsOnLoad_ = true;
    int                          committedEntities_ = 0;
    int                          totalEntities_     = 0;

    // Splash UI (owned by uiManager; raw pointers for per-frame animation + teardown).
    Background*         splashBg_    = nullptr;
    TexturedBackground* splashLogo_  = nullptr;
    Background*         splashTrack_ = nullptr;
    Background*         splashFill_  = nullptr;
    double             splashStart_ = 0.0;

    // Animated line-logo splash (runtime): baked clips streamed on a worker and
    // fed to the debug-line stroke pipeline while the scene loads. See updateSplash.
    LogoBundle         logo_;                  // packed part1 (reveal) + part2 (tumble)
    std::atomic<bool>  logoReady_{ false };
    std::thread        logoLoadThread_;
    double             outroStart_      = 0.0; // when LoadPhase::Outro was entered
    bool               splashOutroDone_ = false;
    math::Vec3         savedCamPos_{};         // camera state to restore post-load
    math::Vec3         savedCamRot_{};
    float              savedCamFov_     = 0.0f;

    double lastTime  = 0.0;

    // Phase E sweep harness. If YOPE_PROFILE_DURATION env var is > 0, the
    // window auto-closes after that many wall-clock seconds. profileEndTime_
    // is set in init() to (start + duration); update() checks each frame.
    double profileEndTime_ = 0.0;

    float fpsAccum   = 0.0f;
    int   fpsFrames  = 0;
    int   displayFps = 0;

    RenderMode renderMode_ = RenderMode::RASTER;

    ~Engine();

    // sceneOverride: non-empty replaces cfg.startupScene (the --scene CLI arg).
    bool init(const std::string& sceneOverride = "");
    void update();
    void render();
    void cleanup();

    // The script-facing half of update(): scene-swap flush, UI input routing +
    // callback dispatch, ticking every live Script::update(), collision-event
    // dispatch, ui_update (hover/blink), pending UI texture uploads, and tweens.
    // Factored out of update() so EditorApp can drive it during Play — the
    // editor doesn't call Engine::update() (it drives its own render loop), so
    // without this call scripts were instantiated + init()'d by doTogglePlay()
    // but never ticked, an editor-only gap that's existed since Python
    // scripting landed. dt is wall-clock seconds, already clamped by the caller.
    void updateScripts(float dt);

    // Drive the asynchronous startup-scene load one step. Call once per frame,
    // before render(), from both the runtime and editor loops. No-op once the
    // scene is fully loaded.
    void pumpSceneLoad();

    // True only after the startup scene has fully loaded (entities committed +
    // embedded textures streamed). Used to gate editor Play and scene swaps.
    bool isSceneLoaded() const { return loadPhase_ == LoadPhase::Done; }

    // Startup-load progress (for the editor's window-level loading overlay).
    int  loadCommitted() const { return committedEntities_; }
    int  loadTotal()     const { return totalEntities_; }

    // Resolved path of the scene passed to init() (cfg startupScene or --scene
    // override), absolute or asset-relative per beginAsyncLoad's resolution.
    // Lets EditorApp mirror this into its own currentSceneFile_ tracking after
    // a cfg-driven startup load, same as it does for the Open Scene menu.
    const std::string& startupScenePath() const { return scenePath_; }

private:
    // Kick off the background parse of scenePath and build the loading splash.
    void beginAsyncLoad(const std::string& scenePath, bool initScripts);
    // Spawn the fixed-timestep physics thread (deferred until the scene loads).
    void startPhysicsThread();
    // Per-frame splash animation + progress bar from committed/streamed counts.
    void updateSplash();
    // Tear down the splash, start physics, init runtime scripts. → LoadPhase::Done.
    void finishAsyncLoad();
};
