#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include "platform/Window.h"
#include "platform/Input.h"
#include "gpu/GpuDevice.h"
#include "rendering/Renderer.h"
#include "rendering/Camera.h"
#include "world/World.h"
#include "assets/AssetManager.h"
#include "audio/AudioSystem.h"
#include "audio/Listener.h"
#include "scripting/Script.h"
#include "scripting/ScriptContext.h"

struct Engine {
    std::unique_ptr<Window>        window;
    std::unique_ptr<Input>         input;
    std::unique_ptr<GpuDevice>     gpu;
    std::unique_ptr<Renderer>      renderer;
    std::unique_ptr<Camera>        camera;
    std::unique_ptr<World>         world;
    std::unique_ptr<AssetManager>  assets;
    std::unique_ptr<AudioSystem>   audio;
    std::unique_ptr<Script>        script_;

    ScriptContext scriptCtx_;

    std::thread       physicsThread_;
    std::atomic<bool> stopPhysics_{ false };

    double lastTime  = 0.0;

    float fpsAccum   = 0.0f;
    int   fpsFrames  = 0;
    int   displayFps = 0;

    bool init();
    void update();
    void render();
    void cleanup();
};
