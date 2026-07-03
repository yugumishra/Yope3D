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
#include "world/World.h"
#include "assets/AssetManager.h"
#include "audio/AudioSystem.h"
#include "audio/Listener.h"
#include "scripting/ScriptContext.h"
#include "scene/SceneManager.h"
#include "ui/UIManager.h"
#ifdef YOPE_PYTHON
#include "scripting/python/PythonInterpreter.h"
#endif

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

    double lastTime  = 0.0;

    // Phase E sweep harness. If YOPE_PROFILE_DURATION env var is > 0, the
    // window auto-closes after that many wall-clock seconds. profileEndTime_
    // is set in init() to (start + duration); update() checks each frame.
    double profileEndTime_ = 0.0;

    float fpsAccum   = 0.0f;
    int   fpsFrames  = 0;
    int   displayFps = 0;

    bool prevLMB_ = false;

    RenderMode renderMode_ = RenderMode::RASTER;

    ~Engine();

    // sceneOverride: non-empty replaces cfg.startupScene (the --scene CLI arg).
    bool init(const std::string& sceneOverride = "");
    void update();
    void render();
    void cleanup();
};
