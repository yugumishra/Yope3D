#include "Engine.h"
#include "math/Math.h"
#include "rendering/Light.h"
#include "assets/Primitives.h"
#include "physics/Barrier.h"
#include "physics/BoundedBarrier.h"
#include "physics/CSphere.h"
#include "physics/CAABB.h"
#include "physics/COBB.h"
#include <GLFW/glfw3.h>
#include <string>
#include <random>
#include <ctime>
#include <cmath>

// ---- Stress-test arena ----
// LMB (hold) = launch objects toward camera crosshair
// LEFT/RIGHT arrow = cycle spawn type (sphere / AABB / OBB)
// WASD + mouse = fly camera
// Objects are kept in a 50×50×50 box (barriers at ±25 on X/Z, floor at 0, ceiling at 50).

static constexpr float ARENA_HALF = 10.0f;
static constexpr float CEILING    = 5.0f;
static constexpr float SPAWN_SPEED = 18.0f;  // m/s launch velocity
static constexpr float SPAWN_RATE  = 0.10f;  // seconds between spawns while holding LMB

// --- RNG ---
static std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));

static float randF(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

static math::Vec3 randomUnitVec() {
    math::Vec3 v;
    do { v = {randF(-1,1), randF(-1,1), randF(-1,1)}; }
    while (v.dot(v) < 1e-4f);
    float len = std::sqrt(v.dot(v));
    return v * (1.0f / len);
}

static const char* typeName(int t) {
    switch (t) { case 0: return "Sphere"; case 1: return "AABB"; case 2: return "OBB"; }
    return "?";
}

// ---- init ---------------------------------------------------------------

bool Engine::init() {
    input = std::make_unique<Input>();
    if (!glfwInit()) return false;

    int screenW = 1920, screenH = 1080;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor())
        if (const GLFWvidmode* mode = glfwGetVideoMode(primary))
            { screenW = mode->width; screenH = mode->height; }

    window = std::make_unique<Window>("Yope3D — Physics Stress Test", screenW, screenH);
    window->init(input.get());
    window->setIcon("textures/tnail.png");
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    gpu      = std::make_unique<GpuDevice>(*window);
    renderer = std::make_unique<Renderer>(*gpu, *window);
    assets   = std::make_unique<AssetManager>();
    assets->init(*gpu, renderer->getCommandPool(), renderer->getTextureSetLayout());
    world    = std::make_unique<World>();
    camera   = std::make_unique<Camera>(screenW, screenH, math::toRadians(70.0f));

    // Lights: directional fill + flashlight (follows camera in update)
    DirectionalLight dir{};
    dir.direction[0] = -0.4f; dir.direction[1] = -1.0f; dir.direction[2] = -0.6f;
    dir.color[0] = 0.85f; dir.color[1] = 0.9f; dir.color[2] = 1.0f;
    dir.intensity = 0.4f;
    world->addLight(dir);

    FlashLight flash{};
    flash.color[0] = flash.color[1] = flash.color[2] = 1.0f;
    flash.intensity = 1.0f;
    flash.innerConeAngle = math::toRadians(18.0f);
    flash.outerConeAngle = math::toRadians(35.0f);
    flash.constant = 1.0f; flash.linear = 0.03f; flash.quadratic = 0.005f;
    world->addLight(flash);

    lastTime = glfwGetTime();
    loadScene(0);
    return true;
}

// ---- loadScene ----------------------------------------------------------
// Sets up the static arena (floor + 5 wall barriers + visuals).
// Called once at startup; sceneIndex is repurposed as spawn type.

void Engine::loadScene(int /*index*/) {
    world->resetPhysics(*gpu);

    // Ghost player sphere at camera (fixed, no gravity, not tangible so objects pass through)
    playerSphere = world->addSphere(1.0f, 0.5f, camera->getPosition());
    playerSphere->fix();
    playerSphere->disableGravity();
    playerSphere->setTangible(false);

    // ---- Barriers (CCD) ----
    world->addBarrier(physics::Barrier({ 0,  1,  0}, {0, 0, 0}));           // floor  y=0
    world->addBarrier(physics::Barrier({ 0, -1,  0}, {0, CEILING, 0}));     // ceiling y=50
    world->addBarrier(physics::Barrier({ 1,  0,  0}, {-ARENA_HALF, 0, 0})); // -X wall
    world->addBarrier(physics::Barrier({-1,  0,  0}, { ARENA_HALF, 0, 0})); // +X wall
    world->addBarrier(physics::Barrier({ 0,  0,  1}, {0, 0, -ARENA_HALF})); // -Z wall
    world->addBarrier(physics::Barrier({ 0,  0, -1}, {0, 0,  ARENA_HALF})); // +Z wall

    // ---- Visuals ----
    // Floor slab — top face at y=0
    {
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({ARENA_HALF, 0.5f, ARENA_HALF}));
        if (m) {
            m->color[0] = 0.32f; m->color[1] = 0.28f; m->color[2] = 0.24f; m->state = 0;
            m->modelMatrix = math::Mat4::translate({0, -0.5f, 0});
        }
    }
    // Four thin wall slabs — just enough to see the arena boundaries.
    // Each wall panel: half-thickness 0.4, half-height CEILING/2, half-width ARENA_HALF.
    float wH = CEILING * 0.5f;
    auto addWall = [&](math::Vec3 pos, math::Vec3 ext) {
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect(ext));
        if (m) {
            m->color[0] = 0.22f; m->color[1] = 0.22f; m->color[2] = 0.28f; m->state = 0;
            m->modelMatrix = math::Mat4::translate(pos);
        }
    };
    addWall({-ARENA_HALF - 0.4f, wH, 0},    {0.4f, wH, ARENA_HALF});
    addWall({ ARENA_HALF + 0.4f, wH, 0},    {0.4f, wH, ARENA_HALF});
    addWall({0, wH, -ARENA_HALF - 0.4f},    {ARENA_HALF, wH, 0.4f});
    addWall({0, wH,  ARENA_HALF + 0.4f},    {ARENA_HALF, wH, 0.4f});
}

// ---- spawnObject --------------------------------------------------------

void Engine::spawnObject() {
    math::Vec3 forward = camera->getForward();
    math::Vec3 origin  = camera->getPosition() + forward * 1.5f;
    math::Vec3 vel     = forward * SPAWN_SPEED;

    // Random orientation (uniform axis-angle)
    math::Quat rot = math::Quat::fromAxisAngle(randomUnitVec(), randF(0, math::PI * 2.0f));

    // Random size — uniform half-extent in [0.3, 0.65]
    float s = randF(0.3f, 0.65f);

    switch (sceneIndex) {
    case 0: {   // Sphere
        auto* h = world->addSphere(1.0f, s, origin);
        if (h) h->setVelocity(vel);
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::icosphere(s));
        if (m) { m->color[0] = 0.2f; m->color[1] = 0.5f; m->color[2] = 1.0f; m->state = 0; }
        if (h && m) h->linkedMesh = m;
        break;
    }
    case 1: {   // AABB
        auto* h = world->addAABB({s, s, s}, 1.0f, origin);
        if (h) h->setVelocity(vel);
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({s, s, s}));
        if (m) { m->color[0] = 0.3f; m->color[1] = 0.85f; m->color[2] = 0.4f; m->state = 0; }
        if (h && m) h->linkedMesh = m;
        break;
    }
    case 2: {   // OBB — vary Y extent for more interesting shapes
        float sy = s * randF(0.5f, 1.8f);
        auto* h = world->addOBB({s, sy, s}, 1.0f, origin);
        if (h) { h->setVelocity(vel); h->setRotation(rot); }
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({s, sy, s}));
        if (m) { m->color[0] = 1.0f; m->color[1] = 0.5f; m->color[2] = 0.1f; m->state = 0; }
        if (h && m) h->linkedMesh = m;
        break;
    }
    }
}

// ---- update -------------------------------------------------------------

void Engine::update() {
    double now = glfwGetTime();
    float  dt  = static_cast<float>(now - lastTime);
    lastTime   = now;
    if (dt > 0.05f) dt = 0.05f;

    camera->update(*input, dt);
    if (window->wasResized())
        camera->WindowChanged(window->getWidth(), window->getHeight());

    // Cycle spawn type with arrow keys (rising edge)
    bool rightNow = input->isKeyDown(GLFW_KEY_RIGHT);
    bool leftNow  = input->isKeyDown(GLFW_KEY_LEFT);
    if (rightNow && !rightWasDown)
        sceneIndex = (sceneIndex + 1) % SCENE_COUNT;
    else if (leftNow && !leftWasDown)
        sceneIndex = (sceneIndex + SCENE_COUNT - 1) % SCENE_COUNT;
    rightWasDown = rightNow;
    leftWasDown  = leftNow;

    // Spawn on LMB hold (rate-limited)
    spawnCooldown -= dt;
    if (input->isLMBDown() && spawnCooldown <= 0.0f) {
        spawnObject();
        spawnCooldown = SPAWN_RATE;
    }

    // Update title: type + live object count
    int objCount = static_cast<int>(world->getHulls().size()) - 1; // exclude player ghost
    window->setTitle(
        std::string("Stress Test | ") + typeName(sceneIndex) +
        " | Objects: " + std::to_string(objCount) +
        " | LMB=spawn  LEFT/RIGHT=cycle  WASD=move"
    );

    playerSphere->fixPosition(camera->getPosition());
    playerSphere->setVelocity({});
    world->advance(dt);

    for (auto& hull : world->getHulls()) {
        if (hull->linkedMesh)
            hull->linkedMesh->modelMatrix = hull->getModelMatrix();
    }
}

// ---- render / cleanup ---------------------------------------------------

void Engine::render() {
    renderer->drawFrame(*gpu, *window, *camera, *world, *assets);
}

void Engine::cleanup() {
    camera.reset();
    renderer->waitIdle(*gpu);
    if (assets) { assets->cleanup(gpu->device()); assets.reset(); }
    world->cleanup(*gpu);
    world.reset();
    renderer.reset();
    gpu.reset();
    window.reset();
    input.reset();
}
