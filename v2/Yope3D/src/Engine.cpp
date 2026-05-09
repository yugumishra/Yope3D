#include "Engine.h"
#include "math/Math.h"
#include "rendering/Light.h"
#include "assets/Primitives.h"
#include <GLFW/glfw3.h>

bool Engine::init() {
    input = std::make_unique<Input>();

    if (!glfwInit())
        return false;

    int screenW = 1920, screenH = 1080;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor()) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(primary)) {
            screenW = mode->width;
            screenH = mode->height;
        }
    }

    window = std::make_unique<Window>("Yope3D", screenW, screenH);
    window->init(input.get());
    window->setIcon("textures/tnail.png");
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    gpu      = std::make_unique<GpuDevice>(*window);
    renderer = std::make_unique<Renderer>(*gpu, *window);
    assets   = std::make_unique<AssetManager>();
    assets->init(*gpu, renderer->getCommandPool(), renderer->getTextureSetLayout());
    world    = std::make_unique<World>();

    float fov = math::toRadians(60);
    camera = std::make_unique<Camera>(screenW, screenH, fov);

    // ---- Physics validation scene (Milestone 6) ----

    // No floor barrier — floor collision handled by the static CAABB below via sphere-AABB discrete

    // Box room (10×10×10, centred at y=5)
    //world->addBarrierHull({50, 50, 50}, {0, 5, 0});

    // Broad-phase octree
    //world->initCollisionTree({-15, -5, -15}, {15, 25, 15}, 3);

    // Static CAABB floor block
    auto* floorBlock = world->addStaticAABB({0, -1.5f, 0}, {50, 0.5f, 50});
    {
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({50.0f, 0.5f, 50.0f}));
        m->color[0] = 0.35f; m->color[1] = 0.30f; m->color[2] = 0.25f;
        m->state = 0;
        floorBlock->linkedMesh = m;
        // tangible=true (default): discrete sphere-AABB handles floor contact
    }

    // Three spheres at different heights, each linked to an icosphere mesh
    auto* sphere0 = world->addSphere(1.0f, 0.5f, {-10, 8, 0});
    //auto* sphere1 = world->addSphere(1.0f, 0.5f, {1, 5, 0});
    //auto* sphere2 = world->addSphere(1.0f, 0.5f, {-1, 3, 0});

    auto addIco = [&]() -> RenderMesh* {
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::icosphere(0.5f));
        if (m) {
            try {
                m->texture = assets->loadTexture(*gpu, "textures/test.png");
                m->color[0] = m->color[1] = m->color[2] = 1.0f;
                m->state = 1;
            } catch (...) {
                m->color[0] = 0.2f; m->color[1] = 0.6f; m->color[2] = 1.0f;
                m->state = 0;
            }
        }
        return m;
    };

    sphere0->linkedMesh = addIco();
    //sphere1->linkedMesh = addIco();
    //sphere2->linkedMesh = addIco();

    // Spring connecting first two spheres
    //world->addSpring(sphere0, sphere1, 8.0f, 1.2f);

    // Player collider — fixed, no gravity, follows camera each frame
    playerSphere = world->addSphere(1.0f, 0.5f, camera->getPosition());
    //playerSphere->fix();
    //playerSphere->disableGravity();

    // Sphere-AABB test: dynamic box falling in the path of sphere1/sphere2
    /*auto* testBox = world->addAABB({0.7f, 0.7f, 0.7f}, 2.0f, {0, 4, 0});
    {
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({1.4f, 1.4f, 1.4f}));
        m->color[0] = 1.0f; m->color[1] = 0.5f; m->color[2] = 0.1f;
        m->state = 0;
        testBox->linkedMesh = m;
    }*/

    // Lights (kept from previous milestone)
    DirectionalLight dirLight{};
    dirLight.direction[0] = -0.5f; dirLight.direction[1] = -1.0f; dirLight.direction[2] = -0.5f;
    dirLight.color[0] = 0.8f; dirLight.color[1] = 0.85f; dirLight.color[2] = 1.0f;
    dirLight.intensity = 0.3f;
    world->addLight(dirLight);

    SpotLight spotLight{};
    spotLight.position[0] = -3.0f; spotLight.position[1] = 2.0f;
    spotLight.direction[0] = 1.0f; spotLight.direction[1] = -0.5f;
    spotLight.color[0] = 1.0f; spotLight.color[1] = 0.2f; spotLight.color[2] = 0.2f;
    spotLight.intensity = 1.0f;
    spotLight.innerConeAngle = math::toRadians(15.0f);
    spotLight.outerConeAngle = math::toRadians(30.0f);
    spotLight.constant = 1.0f; spotLight.linear = 0.09f; spotLight.quadratic = 0.032f;
    world->addLight(spotLight);

    FlashLight flashLight{};
    flashLight.color[0] = flashLight.color[1] = flashLight.color[2] = 1.0f;
    flashLight.intensity = 0.5f;
    flashLight.innerConeAngle = math::toRadians(20.0f);
    flashLight.outerConeAngle = math::toRadians(40.0f);
    flashLight.constant = 1.0f; flashLight.linear = 0.09f; flashLight.quadratic = 0.032f;
    world->addLight(flashLight);

    lastTime = glfwGetTime();
    return true;
}

void Engine::update() {
    double now = glfwGetTime();
    float  dt  = static_cast<float>(now - lastTime);
    lastTime   = now;
    if (dt > 0.1f) dt = 0.1f;

    camera->update(*input, dt);
    if (window->wasResized())
        camera->WindowChanged(window->getWidth(), window->getHeight());

    playerSphere->fixPosition(camera->getPosition());
    //playerSphere->setVelocity({});  // kinematic: position-driven, don't carry solver velocity
    world->advance(dt);

    // Sync physics hulls → render mesh model matrices
    for (auto& hull : world->getHulls()) {
        if (hull->linkedMesh)
            hull->linkedMesh->modelMatrix = hull->getModelMatrix();
    }
}

void Engine::render() {
    renderer->drawFrame(*gpu, *window, *camera, *world, *assets);
}

void Engine::cleanup() {
    camera.reset();
    renderer->waitIdle(*gpu);
    if (assets) {
        assets->cleanup(gpu->device());
        assets.reset();
    }
    world->cleanup(*gpu);
    world.reset();
    renderer.reset();
    gpu.reset();
    window.reset();
    input.reset();
}
