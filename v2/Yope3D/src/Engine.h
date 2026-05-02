#pragma once

#include <memory>
#include "platform/Window.h"
#include "platform/Input.h"

// ---------------------------------------------------------------------------
// Engine
//
// Top-level context struct.  Replaces the Java 'Launch' static grab-bag with
// an explicitly owned, construction-ordered set of subsystems.
//
// Milestone 2:  Window + Input only.
// Milestone 3:  GpuDevice, Swapchain, Renderer added to init/render/cleanup.
// Milestone 6:  World (physics) added to update.
// Milestone 7:  AudioSystem added.
// Milestone 8:  AssetManager, ScriptContext, Script instantiation added.
// ---------------------------------------------------------------------------

struct Engine {
    std::unique_ptr<Window> window;
    std::unique_ptr<Input>  input;

    // Milestone 3+: add these when Vulkan bootstrap is done.
    // std::unique_ptr<GpuDevice>    gpu;
    // std::unique_ptr<Renderer>     renderer;
    // std::unique_ptr<World>        world;
    // std::unique_ptr<AudioSystem>  audio;
    // std::unique_ptr<AssetManager> assets;

    bool init();
    void update();
    void render();
    void cleanup();
};