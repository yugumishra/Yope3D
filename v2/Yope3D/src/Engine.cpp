#include "Engine.h"
#include "math/Math.h"
#include "rendering/Light.h"
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
    world    = std::make_unique<World>();

    // 90° FOV in radians.
    float fov = math::toRadians(60);
    camera = std::make_unique<Camera>(screenW, screenH, fov);

    // ----------------------------- TEST SCENE INIT FOR MILESTONE 4C -----------------------------

    // Add the default cube mesh to the world.
    static const std::vector<Vertex> kDefaultVertices = {
        {{-0.5f, -0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},

        {{-0.5f, -0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,-0.5f}, { 0.0f, 0.0f,-1.0f}, {1.0f, 0.0f}},

        {{-0.5f,  0.5f,-0.5f}, { 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f, 0.5f}, { 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, 0.5f}, { 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f,-0.5f}, { 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},

        {{-0.5f, -0.5f,-0.5f}, { 0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, 0.5f}, { 0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.5f}, { 0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f,-0.5f}, { 0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},

        {{ 0.5f, -0.5f,-0.5f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f, 0.5f}, { 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.5f}, { 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f,-0.5f}, { 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},

        {{-0.5f, -0.5f,-0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f,  0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f,  0.5f,-0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    };
    static const std::vector<uint32_t> kDefaultIndices = {
        0, 1, 2, 0, 3, 1,
        4, 5, 6, 4, 7, 5,
        8, 9, 10, 8, 11, 9,
        12, 13, 14, 12, 15, 13,
        16, 17, 18, 16, 19, 17,
        20, 21, 22, 20, 23, 21,
    };

    static const std::vector<Vertex> planeVertices = {
        {{-500.0f,-1.0f,-500.0f}, { 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 500.0f,-1.0f, 500.0f}, { 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-500.0f,-1.0f, 500.0f}, { 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 500.0f,-1.0f,-500.0f}, { 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    };
    static const std::vector<uint32_t> planeIndices = {
        0, 1, 2, 0, 3, 1,
    };
    world->addRenderMesh(*gpu, renderer->getCommandPool(), kDefaultVertices, kDefaultIndices);
    world->addRenderMesh(*gpu, renderer->getCommandPool(), planeVertices, planeIndices);

    // Add test lights for milestone 4c lighting (variable-length SSBO)

    // Point light: warm white, above the cube
    /*
    PointLight pointLight{};
    pointLight.position[0] = 2.0f;
    pointLight.position[1] = 3.0f;
    pointLight.position[2] = 2.0f;
    pointLight.color[0] = 1.0f;
    pointLight.color[1] = 0.9f;
    pointLight.color[2] = 0.8f;
    pointLight.intensity = 1.2f;
    pointLight.constant = 1.0f;
    pointLight.linear = 0.09f;
    pointLight.quadratic = 0.032f;
    world->addLight(pointLight);

    // Directional light (sun-like): cool white from above-right
    DirectionalLight dirLight{};
    dirLight.direction[0] = -0.5f;
    dirLight.direction[1] = -1.0f;
    dirLight.direction[2] = -0.5f;
    dirLight.color[0] = 0.8f;
    dirLight.color[1] = 0.85f;
    dirLight.color[2] = 1.0f;
    dirLight.intensity = 0.7f;
    world->addLight(dirLight);*/

    // Spot light: red, positioned to the left
    SpotLight spotLight{};
    spotLight.position[0] = -3.0f;
    spotLight.position[1] = 2.0f;
    spotLight.position[2] = 0.0f;
    spotLight.direction[0] = 1.0f;  // pointing right
    spotLight.direction[1] = -0.5f; // slightly down
    spotLight.direction[2] = 0.0f;
    spotLight.color[0] = 1.0f;
    spotLight.color[1] = 0.2f;
    spotLight.color[2] = 0.2f;
    spotLight.intensity = 1.0f;
    spotLight.innerConeAngle = math::toRadians(15.0f);
    spotLight.outerConeAngle = math::toRadians(30.0f);
    spotLight.constant = 1.0f;
    spotLight.linear = 0.09f;
    spotLight.quadratic = 0.032f;
    world->addLight(spotLight);

    // Flash light (camera light): attached to camera for debugging
    FlashLight flashLight{};
    flashLight.color[0] = 0.3f;
    flashLight.color[1] = 0.6f;
    flashLight.color[2] = 1.0f;  // blue tint
    flashLight.intensity = 0.5f;
    flashLight.innerConeAngle = math::toRadians(20.0f);
    flashLight.outerConeAngle = math::toRadians(40.0f);
    flashLight.constant = 1.0f;
    flashLight.linear = 0.09f;
    flashLight.quadratic = 0.032f;
    world->addLight(flashLight);/**/


    // ----------------------------- TEST SCENE INIT FOR MILESTONE 4C -----------------------------

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
    renderer->drawFrame(*gpu, *window, *camera, *world);
}

// ---------------------------------------------------------------------------
// Engine::cleanup
// ---------------------------------------------------------------------------

void Engine::cleanup() {
    // Milestone 8: script->cleanup()
    // Milestone 7: audio.reset();
    // Milestone 8: assets.reset();
    camera.reset();
    // CRITICAL: Call waitIdle BEFORE destroying world buffers. The GPU may still be
    // referencing RenderMesh/light buffers in flight, so we must flush all pending
    // work before cleanup() destroys them.
    renderer->waitIdle(*gpu);
    world->cleanup(*gpu);
    world.reset();
    renderer.reset();
    gpu.reset();
    window.reset();
    input.reset();
}
