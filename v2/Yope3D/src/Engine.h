#pragma once

#include <memory>
#include "platform/Window.h"
#include "platform/Input.h"
#include "gpu/GpuDevice.h"
#include "rendering/Renderer.h"
#include "rendering/Camera.h"

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
    std::unique_ptr<Window>   window;
    std::unique_ptr<Input>    input;
    std::unique_ptr<GpuDevice> gpu;
    std::unique_ptr<Renderer>  renderer;
    std::unique_ptr<Camera>    camera;

    // lastTime tracks the previous frame's timestamp for dt calculation.
    double lastTime = 0.0;

    // Milestone 6+:
    // std::unique_ptr<World>        world;
    // Milestone 7+:
    // std::unique_ptr<AudioSystem>  audio;
    // Milestone 8+:
    // std::unique_ptr<AssetManager> assets;

    bool init();
    void update();
    void render();
    void cleanup();
};