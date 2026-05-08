#include "Engine.h"
#include "math/Math.h"
#include <GLFW/glfw3.h>

// ---------------------------------------------------------------------------
// Engine::init
// ---------------------------------------------------------------------------

bool Engine::init() {
    input = std::make_unique<Input>();

    if (!glfwInit())
        return false;

    int screenW = 1920;
    int screenH = 1080;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor()) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(primary)) {
            screenW = mode->width;
            screenH = mode->height;
        }
    }

    window = std::make_unique<Window>("Yope3D", screenW, screenH);
    window->init(input.get());
    window->setIcon("textures/tnail.png");

    // Capture the cursor for FPS-style mouse look.
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    gpu      = std::make_unique<GpuDevice>(*window);
    renderer = std::make_unique<Renderer>(*gpu, *window);

    // 90° FOV in radians.
    float fov = math::toRadians(60);
    camera = std::make_unique<Camera>(screenW, screenH, fov);

    lastTime = glfwGetTime();

    // Milestone 7: audio  = std::make_unique<AudioSystem>();
    // Milestone 8: assets = std::make_unique<AssetManager>();
    return true;
}

// ---------------------------------------------------------------------------
// Engine::update
// ---------------------------------------------------------------------------

void Engine::update() {
    double now = glfwGetTime();
    float  dt  = static_cast<float>(now - lastTime);
    lastTime   = now;

    // Keep dt bounded to avoid huge jumps on the first frame or after a pause.
    if (dt > 0.1f) dt = 0.1f;

    camera->update(*input, dt);

    // Update camera aspect ratio if the window was resized.
    // The swapchain is recreated in render(); we only need the new ratio here.
    if (window->wasResized())
        camera->WindowChanged(window->getWidth(), window->getHeight());

    // Milestone 6: world->advance(dt);
    // Milestone 8: script->update(dt);
}

// ---------------------------------------------------------------------------
// Engine::render
// ---------------------------------------------------------------------------

void Engine::render() {
    renderer->drawFrame(*gpu, *window, *camera);
}

// ---------------------------------------------------------------------------
// Engine::cleanup
// ---------------------------------------------------------------------------

void Engine::cleanup() {
    // Milestone 8: script->cleanup()
    // Milestone 7: audio.reset();
    // Milestone 8: assets.reset();
    camera.reset();
    renderer->waitIdle(*gpu);
    renderer.reset();
    gpu.reset();
    window.reset();
    input.reset();
}
