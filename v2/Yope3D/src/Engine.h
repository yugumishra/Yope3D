#pragma once

#include <memory>
#include "platform/Window.h"
#include "platform/Input.h"
#include "gpu/GpuDevice.h"
#include "rendering/Renderer.h"
#include "rendering/Camera.h"
#include "world/World.h"
#include "assets/AssetManager.h"

// ---------------------------------------------------------------------------
// Engine
//
// Top-level context struct.  Replaces the Java 'Launch' static grab-bag with
// an explicitly owned, construction-ordered set of subsystems.
//
// Milestone 2:  Window + Input.
// Milestone 3:  GpuDevice (VkInstance, surface, physical/logical device).
// Milestone 4a: Renderer (pipelines, swapchain, render pass, RenderMesh).
// Milestone 4b: Camera (view/proj matrices, WASD + mouse-look).
// Milestone 6:  World (physics).
// Milestone 7:  AudioSystem.
// Milestone 8:  AssetManager, ScriptContext, Script.
// ---------------------------------------------------------------------------

struct Engine {
    std::unique_ptr<Window>        window;
    std::unique_ptr<Input>         input;
    std::unique_ptr<GpuDevice>     gpu;
    std::unique_ptr<Renderer>      renderer;
    std::unique_ptr<Camera>        camera;
    std::unique_ptr<World>         world;
    std::unique_ptr<AssetManager>  assets;

    double lastTime = 0.0;

    physics::CSphere* playerSphere = nullptr;

    int sceneIndex = 0;
    static constexpr int SCENE_COUNT = 34;
    void loadScene(int index);

    bool rightWasDown = false;
    bool leftWasDown  = false;

    // Milestone 7+:
    // std::unique_ptr<AudioSystem>  audio;

    bool init();
    void update();
    void render();
    void cleanup();
};