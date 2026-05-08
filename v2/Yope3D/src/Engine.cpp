#include "Engine.h"
#include "math/Math.h"
#include "rendering/Light.h"
#include "assets/Primitives.h"
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
    assets   = std::make_unique<AssetManager>();
    assets->init(*gpu, renderer->getCommandPool(), renderer->getTextureSetLayout());
    world    = std::make_unique<World>();

    // 90° FOV in radians.
    float fov = math::toRadians(60);
    camera = std::make_unique<Camera>(screenW, screenH, fov);

    // ----------------------------- TEST SCENE INIT FOR MILESTONE 4C -----------------------------

    // Add meshes to the world using Primitives.
    //auto* cubeMesh  = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::cube());
    auto* planeMesh = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::plane());
    auto* sphereMesh = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::icosphere());

    // Configure the cube mesh: textured (try to load test.png)
    /*
    if (cubeMesh) {
        try {
            cubeMesh->texture = assets->loadTexture(*gpu, "textures/test.png");
            cubeMesh->color[0] = 1.0f;
            cubeMesh->color[1] = 1.0f;
            cubeMesh->color[2] = 1.0f;  // white tint for texture modulation
            cubeMesh->state = 1;  // STATE_TEXTURED
        } catch (const std::exception& e) {
            // If texture load fails, fall back to solid color
            cubeMesh->color[0] = 0.0f;
            cubeMesh->color[1] = 1.0f;
            cubeMesh->color[2] = 1.0f;
            cubeMesh->state = 0;  // STATE_SOLID
        }
    }*/

    // Configure the plane mesh: solid color
    if (planeMesh) {
        planeMesh->color[0] = 0.3f;
        planeMesh->color[1] = 0.3f;
        planeMesh->color[2] = 0.3f;
        planeMesh->state = 0;  // STATE_SOLID
    }

    if (sphereMesh) {
        try {
            sphereMesh->texture = assets->loadTexture(*gpu, "textures/test.png");
            sphereMesh->color[0] = 1.0f;
            sphereMesh->color[1] = 1.0f;
            sphereMesh->color[2] = 1.0f;  // white tint for texture modulation
            sphereMesh->state = 1;  // STATE_TEXTURED
        } catch (const std::exception& e) {
            // If texture load fails, fall back to solid color
            sphereMesh->color[0] = 0.0f;
            sphereMesh->color[1] = 1.0f;
            sphereMesh->color[2] = 1.0f;
            sphereMesh->state = 0;  // STATE_SOLID
        }
    }

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
    world->addLight(pointLight);*/

    // Directional light (sun-like): cool white from above-right
    DirectionalLight dirLight{};
    dirLight.direction[0] = -0.5f;
    dirLight.direction[1] = -1.0f;
    dirLight.direction[2] = -0.5f;
    dirLight.color[0] = 0.8f;
    dirLight.color[1] = 0.85f;
    dirLight.color[2] = 1.0f;
    dirLight.intensity = 0.3f;
    world->addLight(dirLight);

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
    flashLight.color[0] = 1.0f;
    flashLight.color[1] = 1.0f;
    flashLight.color[2] = 1.0f;
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
    renderer->drawFrame(*gpu, *window, *camera, *world, *assets);
}

// ---------------------------------------------------------------------------
// Engine::cleanup
// ---------------------------------------------------------------------------

void Engine::cleanup() {
    // Milestone 8: script->cleanup()
    camera.reset();
    // CRITICAL: Call waitIdle BEFORE destroying GPU resources. The GPU may still be
    // referencing buffers and textures in flight, so we must flush all pending
    // work before cleanup() destroys them.
    renderer->waitIdle(*gpu);
    // Assets must be cleaned up before the renderer is destroyed because textures
    // reference Vulkan objects that are managed by the GPU.
    if (assets) {
        assets->cleanup(gpu->device());
        assets.reset();
    }
    world->cleanup(*gpu);
    world.reset();
    renderer.reset();
    // Milestone 7: audio.reset();
    gpu.reset();
    window.reset();
    input.reset();
}
