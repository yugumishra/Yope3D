#include "Engine.h"
#include "scripting/Config.h"
#include "scripting/ScriptFactory.h"
#include "physics/PhysicsConstants.h"
#include "math/Math.h"
#include <GLFW/glfw3.h>
#include <string>
#include <chrono>

bool Engine::init() {
    input = std::make_unique<Input>();
    if (!glfwInit()) return false;

    Config cfg = Config::load("yope.cfg");

    int screenW = 1920, screenH = 1080;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor())
        if (const GLFWvidmode* mode = glfwGetVideoMode(primary))
            { screenW = mode->width; screenH = mode->height; }
    if (cfg.width  > 0) screenW = cfg.width;
    if (cfg.height > 0) screenH = cfg.height;

    window = std::make_unique<Window>("Yope3D", screenW, screenH);
    window->init(input.get());
    window->setIcon("textures/tnail.png");
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    gpu      = std::make_unique<GpuDevice>(*window);
    renderer = std::make_unique<Renderer>(*gpu, *window);
    assets   = std::make_unique<AssetManager>();
    assets->init(*gpu, renderer->getCommandPool(), renderer->getTextureSetLayout());

    world = std::make_unique<World>();
    world->init(*gpu, renderer->getCommandPool());

    camera = std::make_unique<Camera>(screenW, screenH, math::toRadians(70.0f));

    audio = std::make_unique<AudioSystem>();
    audio->init();
    Listener::setGain(1.0f);

    scriptCtx_.world  = world.get();
    scriptCtx_.camera = camera.get();
    scriptCtx_.input  = input.get();
    scriptCtx_.audio  = audio.get();
    scriptCtx_.assets = assets.get();
    scriptCtx_.window = window.get();

    script_ = ScriptFactory::create(cfg.script);
    script_->init(scriptCtx_);

    lastTime = glfwGetTime();

    physicsThread_ = std::thread([this] {
        using namespace std::chrono_literals;
        double last    = glfwGetTime();
        float  accum   = 0.0f;
        while (!stopPhysics_.load(std::memory_order_relaxed)) {
            double now = glfwGetTime();
            float  dt  = std::min(static_cast<float>(now - last), 0.05f);
            last = now;
            accum = std::min(accum + dt, physics::MAX_PHYSICS_ACCUMULATOR);
            while (accum >= physics::PHYSICS_DT) {
                world->advance(physics::PHYSICS_DT);
                accum -= physics::PHYSICS_DT;
            }
            std::this_thread::sleep_for(100us);
        }
    });

    return true;
}

void Engine::update() {
    double now = glfwGetTime();
    float  dt  = static_cast<float>(now - lastTime);
    lastTime   = now;
    if (dt > 0.05f) dt = 0.05f;

    fpsAccum += dt;
    ++fpsFrames;
    if (fpsAccum >= 0.5f) {
        displayFps = static_cast<int>(fpsFrames / fpsAccum + 0.5f);
        fpsAccum  = 0.0f;
        fpsFrames = 0;
    }

    if (window->wasResized())
        camera->WindowChanged(window->getWidth(), window->getHeight());

    script_->update(scriptCtx_, dt);

    // Listener tracks camera (updated after script may have moved it).
    Listener::setPosition(camera->getPosition());
    Listener::setOrientation(camera->getForward(), {0.0f, 1.0f, 0.0f});
}

void Engine::render() {
    if (world->newSnapshotReady_.exchange(false, std::memory_order_acquire))
        world->syncRenderMeshesFromFront();
    renderer->drawFrame(*gpu, *window, *camera, *world, *assets);
}

void Engine::cleanup() {
    stopPhysics_.store(true, std::memory_order_release);
    physicsThread_.join();

    script_.reset();
    audio.reset();
    camera.reset();
    renderer->waitIdle(*gpu);
    if (assets) { assets->cleanup(gpu->device()); assets.reset(); }
    world->cleanup();
    world.reset();
    renderer.reset();
    gpu.reset();
    window.reset();
    input.reset();
}
