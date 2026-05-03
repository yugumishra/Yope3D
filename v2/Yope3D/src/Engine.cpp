#include "Engine.h"

#include <GLFW/glfw3.h>   // glfwInit / glfwGetPrimaryMonitor / glfwGetVideoMode

// ---------------------------------------------------------------------------
// Engine::init
// ---------------------------------------------------------------------------
// Construction order matters for destruction (RAII, reverse order):
//   1. Input   — pure data, no external dependencies
//   2. Window  — depends on GLFW (calls glfwInit internally)
//
// Milestone 3 will insert GpuDevice (Vulkan instance/device) here, then
// Renderer (pipelines, swapchain) which depends on both GpuDevice and Window.

bool Engine::init() {
    // ---- Input -----------------------------------------------------------
    input = std::make_unique<Input>();

    // ---- Screen dimensions -----------------------------------------------
    // Query the primary monitor so the window starts at half-screen size,
    // mirroring the Java Launch + Window pattern.
    // glfwInit() must be called before glfwGetPrimaryMonitor(); we do it here
    // rather than inside the Window constructor so we can use the result.
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
    // Window constructor calls glfwInit() again — that is safe; GLFW counts
    // init/terminate calls.  Alternatively, expose a static Window::initGLFW()
    // and call it only once in Milestone 3 when the architecture is settled.

    // ---- Window ----------------------------------------------------------
    window = std::make_unique<Window>("Yope3D", screenW, screenH);

    // Pass input so Window can register its callbacks to forward into it.
    window->init(input.get());

    //set icon here
    window->setIcon("textures/tnail.png");

    gpu      = std::make_unique<GpuDevice>(*window);
    renderer = std::make_unique<Renderer>(*gpu, *window);
    // Milestone 7: audio  = std::make_unique<AudioSystem>();
    // Milestone 8: assets = std::make_unique<AssetManager>();
    // Milestone 8: Instantiate and init() the active Script via ScriptFactory.

    return true;
}

// ---------------------------------------------------------------------------
// Engine::update
// ---------------------------------------------------------------------------
// Called once per frame after beginFrame() and before render().
// Responsible for advancing all simulation and scripting state.

void Engine::update() {
    // Milestone 2: nothing to update yet — Window/Input have no per-frame
    // logic; they are driven entirely by GLFW callbacks (pollEvents).

    // Milestone 3: nothing rendering-side yet either.

    // Milestone 6:
    //   world->advance(dt);

    // Milestone 8:
    //   for each script: script->update(dt);
}

// ---------------------------------------------------------------------------
// Engine::render
// ---------------------------------------------------------------------------
// Called once per frame after update().

void Engine::render() {
    // Milestone 2: nothing to render — no Vulkan context exists yet.

    renderer->drawFrame(*gpu, *window);
}

// ---------------------------------------------------------------------------
// Engine::cleanup
// ---------------------------------------------------------------------------
// Subsystems must be destroyed in reverse construction order.
// unique_ptr reset() is explicit here for clarity; falling off the destructor
// would do the same thing but the ordering would be implicit.

void Engine::cleanup() {
    // Milestone 8: script->cleanup()
    // Milestone 7: audio.reset();
    // Milestone 8: assets.reset();
    renderer->waitIdle(*gpu);   // flushes GPU, destroys pipeline/swapchain/sync objects
    renderer.reset();

    gpu.reset();     // logical device (vkDeviceWaitIdle), then surface, then instance
    window.reset();  // destroys GLFW window + calls glfwTerminate
    input.reset();   // pure data, safe last
}