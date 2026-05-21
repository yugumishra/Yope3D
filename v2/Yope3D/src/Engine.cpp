#include "Engine.h"
#include "math/Math.h"
#include "rendering/Light.h"
#include "assets/Primitives.h"
#include "physics/CSphere.h"
#include "physics/CAABB.h"
#include "physics/COBB.h"
#include <GLFW/glfw3.h>
#include <string>
#include <random>
#include <ctime>
#include <cmath>

// ---- Shared constants -------------------------------------------------------

static constexpr float SPAWN_SPEED = 18.0f;
static constexpr float SPAWN_RATE  = 0.05f;

// Pyramid
static constexpr float PYR_HALF    = 0.45f;   // OBB half-extent
static constexpr float PYR_SPACING = 1.0f;    // center-to-center within row

// Spring cloth
static constexpr int   GRID_N      = 20;
static constexpr float GRID_STEP   = 1.0f;    // rest length / initial spacing
static constexpr float NODE_HALF   = 0.45f;   // OBB half-extent for spring nodes
static constexpr float NODE_MASS   = 1.0f;
static constexpr float SPRING_K    = 200.0f;

// Stress test arena
static constexpr float STRESS_HALF    = 20.0f;
static constexpr float STRESS_CEILING = 25.0f;

// ---- Helpers ----------------------------------------------------------------

static std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));

static float randF(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

static math::Vec3 randomUnitVec() {
    math::Vec3 v;
    do { v = {randF(-1,1), randF(-1,1), randF(-1,1)}; }
    while (v.dot(v) < 1e-4f);
    return v * (1.0f / std::sqrt(v.dot(v)));
}

static const char* typeName(int t) {
    switch (t) { case 0: return "Sphere"; case 1: return "AABB"; case 2: return "OBB"; }
    return "?";
}

static const char* sceneName(int s) {
    switch (s) {
    case 0:  return "Pyramid (Small)";
    case 1:  return "Pyramid (Medium)";
    case 2:  return "Pyramid (Large)";
    case 3:  return "Spring [Sphere] — Top Row Fixed";
    case 4:  return "Spring [AABB]   — Top Row Fixed";
    case 5:  return "Spring [OBB]    — Top Row Fixed";
    case 6:  return "Spring [Sphere] — 4 Corners";
    case 7:  return "Spring [AABB]   — 4 Corners";
    case 8:  return "Spring [OBB]    — 4 Corners";
    case 9:  return "Spring [Sphere] — 2 Top Corners";
    case 10: return "Spring [AABB]   — 2 Top Corners";
    case 11: return "Spring [OBB]    — 2 Top Corners";
    case 12: return "Stress Test";
    }
    return "?";
}

// ---- Engine::init -----------------------------------------------------------

bool Engine::init() {
    input = std::make_unique<Input>();
    if (!glfwInit()) return false;

    int screenW = 1920, screenH = 1080;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor())
        if (const GLFWvidmode* mode = glfwGetVideoMode(primary))
            { screenW = mode->width; screenH = mode->height; }

    window = std::make_unique<Window>("Yope3D", screenW, screenH);
    window->init(input.get());
    window->setIcon("textures/tnail.png");
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    gpu      = std::make_unique<GpuDevice>(*window);
    renderer = std::make_unique<Renderer>(*gpu, *window);
    assets   = std::make_unique<AssetManager>();
    assets->init(*gpu, renderer->getCommandPool(), renderer->getTextureSetLayout());
    world    = std::make_unique<World>();
    world->layers.add("default");
    world->layers.add("spring_proxy");
    camera   = std::make_unique<Camera>(screenW, screenH, math::toRadians(70.0f));

    // Bright directional light — no flashlight
    DirectionalLight dir{};
    dir.direction[0] = -0.4f; dir.direction[1] = -1.0f; dir.direction[2] = -0.6f;
    dir.color[0] = 0.95f; dir.color[1] = 0.95f; dir.color[2] = 1.0f;
    dir.intensity = 0.9f;
    world->addLight(dir);

    lastTime = glfwGetTime();
    loadScene(sceneIndex);
    return true;
}

// ---- addFloorMesh -----------------------------------------------------------

void Engine::addFloorMesh(float halfW, float halfD) {
    auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                   Primitives::rect({halfW, 0.5f, halfD}));
    if (m) {
        m->color[0] = 0.32f; m->color[1] = 0.28f; m->color[2] = 0.24f; m->state = 0;
        m->modelMatrix = math::Mat4::translate({0.0f, -0.5f, 0.0f});
    }
}

// ---- Engine::loadScene (dispatcher) ----------------------------------------

void Engine::loadScene(int index) {
    if (hasRendered)
        gpu->syncDevice();   // drain GPU before destroying in-flight buffers (not a full renderer teardown)
    switch (index) {
    case 0: loadPyramid(4); break;
    case 1: loadPyramid(7); break;
    case 2: loadPyramid(10); break;
    case 3:  loadSpringCloth(0, 0); break;   // top row,   sphere
    case 4:  loadSpringCloth(0, 1); break;   // top row,   AABB
    case 5:  loadSpringCloth(0, 2); break;   // top row,   OBB
    case 6:  loadSpringCloth(1, 0); break;   // 4 corners, sphere
    case 7:  loadSpringCloth(1, 1); break;   // 4 corners, AABB
    case 8:  loadSpringCloth(1, 2); break;   // 4 corners, OBB
    case 9:  loadSpringCloth(2, 0); break;   // 2 corners, sphere
    case 10: loadSpringCloth(2, 1); break;   // 2 corners, AABB
    case 11: loadSpringCloth(2, 2); break;   // 2 corners, OBB
    case 12: loadStressTest();      break;
    }
}

// ---- Engine::loadPyramid ---------------------------------------------------
// Staggered 2-D pyramid of OBBs (row i has baseN-i boxes, offset by 0.5 per row).
// No walls — just a static CAABB floor.

void Engine::loadPyramid(int baseN) {
    world->resetPhysics(*gpu);

    playerSphere = world->addSphere(1.0f, 0.5f, camera->getPosition());
    playerSphere->fix(); playerSphere->disableGravity(); playerSphere->setTangible(false);

    float halfFloor = (baseN + 15) * 1.0f;
    world->addStaticAABB({0.0f, -0.5f, 0.0f}, {halfFloor, 0.5f, 15.0f});
    addFloorMesh(halfFloor, 15.0f);

    for (int row = 0; row < baseN; row++) {
        int   count = baseN - row;
        float y     = PYR_HALF + row * (2.0f * PYR_HALF - 0.012f);
        float t     = (baseN > 1) ? (float)row / (baseN - 1) : 0.5f;
        for (int j = 0; j < count; j++) {
            float x = -(count - 1) * PYR_SPACING * 0.5f + j * PYR_SPACING;
            auto* h  = world->addOBB({PYR_HALF, PYR_HALF, PYR_HALF}, 1.0f, {x, y, 0.0f});
            auto* rm = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                            Primitives::rect({PYR_HALF, PYR_HALF, PYR_HALF}));
            if (rm) {
                rm->color[0] = 0.2f + t * 0.7f;
                rm->color[1] = 0.45f - t * 0.15f;
                rm->color[2] = 0.9f  - t * 0.7f;
                rm->state = 0;
            }
            if (h && rm) h->linkedMesh = rm;
        }
    }

    float camZ = baseN + 4.0f;
    float camY = baseN * 0.7f;
    camera->setPosition({0.0f, camY, camZ});
    camera->setRotation({0.0f, 0.0f, 0.0f});
}

// ---- Engine::loadSpringCloth -----------------------------------------------
// variant:   0=top-row fixed  1=4-corners fixed  2=2-top-corners fixed
// shapeType: 0=Sphere         1=AABB              2=OBB

void Engine::loadSpringCloth(int variant, int shapeType) {
    world->resetPhysics(*gpu);

    playerSphere = world->addSphere(1.0f, 0.5f, camera->getPosition());
    playerSphere->fix(); playerSphere->disableGravity(); playerSphere->setTangible(false);

    float fh = (GRID_N + 1) * GRID_STEP * 2.0f;
    world->addStaticAABB({0.0f, -5.0f, 0.0f}, {fh, 5.0f, fh});
    addFloorMesh(fh, fh);

    bool horizontal = (variant == 1);
    float halfW = (GRID_N - 1) * GRID_STEP * 0.5f;
    float topY  = horizontal ? 25.0f : (GRID_N - 1) * GRID_STEP + 25.0f;

    physics::Hull* grid[GRID_N][GRID_N] = {};

    for (int j = 0; j < GRID_N; j++) {
        for (int i = 0; i < GRID_N; i++) {
            float cx, cy, cz;
            if (horizontal) {
                cx = -halfW + i * GRID_STEP;
                cy = topY;
                cz = -halfW + j * GRID_STEP;
            } else {
                cx = -halfW + i * GRID_STEP;
                cy = topY - j * GRID_STEP;
                cz = 0.0f;
            }

            physics::Hull* h = nullptr;
            RenderMesh*    rm = nullptr;
            float fi = (float)i / (GRID_N - 1);
            float fj = (float)j / (GRID_N - 1);

            if (shapeType == 0) {
                h  = world->addSphere(NODE_MASS, NODE_HALF, {cx, cy, cz});
                rm = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                          Primitives::icosphere(NODE_HALF, 1));
                if (rm) { rm->color[0] = 0.9f - 0.4f*fi; rm->color[1] = 0.3f + 0.4f*fj; rm->color[2] = 0.15f + 0.3f*fi; }
            } else if (shapeType == 1) {
                h  = world->addAABB({NODE_HALF, NODE_HALF, NODE_HALF}, NODE_MASS, {cx, cy, cz});
                rm = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                          Primitives::rect({NODE_HALF, NODE_HALF, NODE_HALF}));
                if (rm) { rm->color[0] = 0.1f + 0.2f*fj; rm->color[1] = 0.5f + 0.3f*fi; rm->color[2] = 0.8f - 0.3f*fj; }
            } else {
                h  = world->addOBB({NODE_HALF, NODE_HALF, NODE_HALF}, NODE_MASS, {cx, cy, cz});
                rm = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                          Primitives::rect({NODE_HALF, NODE_HALF, NODE_HALF}));
                if (rm) { rm->color[0] = fi; rm->color[1] = fj; rm->color[2] = 0.4f + 0.3f*(fi+fj)*0.5f; }
            }
            if (rm) rm->state = 0;
            if (h && rm) h->linkedMesh = rm;
            grid[i][j] = h;

            bool fix = false;
            if (variant == 0)
                fix = (j == 0);
            else if (variant == 1)
                fix = ((i == 0 || i == GRID_N-1) && (j == 0 || j == GRID_N-1));
            else if (variant == 2)
                fix = (j == 0 && (i == 0 || i == GRID_N-1));
            if (fix && h) h->fix();
        }
    }

    for (int j = 0; j < GRID_N; j++)
        for (int i = 0; i < GRID_N - 1; i++)
            world->addSpring(grid[i][j], grid[i+1][j], SPRING_K, GRID_STEP);

    for (int i = 0; i < GRID_N; i++)
        for (int j = 0; j < GRID_N - 1; j++)
            world->addSpring(grid[i][j], grid[i][j+1], SPRING_K, GRID_STEP);

    // Camera placement
    float dist = (GRID_N - 1) * GRID_STEP;
    if (horizontal) {
        camera->setPosition({0.0f, topY + dist, dist * 0.7f});
        camera->setRotation({-0.8f, 0.0f, 0.0f});
    } else {
        camera->setPosition({0.0f, topY * 0.5f, dist + 5.0f});
        camera->setRotation({0.0f, 0.0f, 0.0f});
    }
}

// ---- Engine::loadStressTest -------------------------------------------------

void Engine::loadStressTest() {
    world->resetPhysics(*gpu);

    playerSphere = world->addSphere(1.0f, 0.5f, camera->getPosition());
    playerSphere->fix(); playerSphere->disableGravity(); playerSphere->setTangible(false);

    // Floor + ceiling
    world->addStaticAABB({0.0f, -0.5f, 0.0f},                    {STRESS_HALF, 0.5f,  STRESS_HALF});
    world->addStaticAABB({0.0f, STRESS_CEILING + 0.5f, 0.0f},    {STRESS_HALF, 0.5f,  STRESS_HALF});
    addFloorMesh(STRESS_HALF, STRESS_HALF);

    // Walls: physics CAABB + render mesh share the same pos/ext so they align exactly.
    float wH = STRESS_CEILING * 0.5f;
    auto addWall = [&](math::Vec3 pos, math::Vec3 ext) {
        world->addStaticAABB(pos, ext);
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::rect(ext));
        if (m) {
            m->color[0] = 0.22f; m->color[1] = 0.22f; m->color[2] = 0.28f; m->state = 0;
            m->modelMatrix = math::Mat4::translate(pos);
        }
    };
    addWall({-STRESS_HALF - 0.4f, wH, 0},     {0.4f, wH, STRESS_HALF});
    addWall({ STRESS_HALF + 0.4f, wH, 0},     {0.4f, wH, STRESS_HALF});
    addWall({0, wH, -STRESS_HALF - 0.4f},     {STRESS_HALF, wH, 0.4f});
    addWall({0, wH,  STRESS_HALF + 0.4f},     {STRESS_HALF, wH, 0.4f});

    camera->setPosition({0.0f, 3.5f, STRESS_HALF - 2.0f});
    camera->setRotation({0.0f, 0.0f, 0.0f});
}

// ---- Engine::spawnObject ----------------------------------------------------

void Engine::spawnObject() {
    math::Vec3 forward = camera->getForward();
    math::Vec3 origin  = camera->getPosition() + forward * 1.5f;
    math::Vec3 vel     = forward * SPAWN_SPEED;
    math::Quat rot     = math::Quat::fromAxisAngle(randomUnitVec(), randF(0, math::PI * 2.0f));
    float s =  (0.65f);

    switch (spawnType) {
    case 0: {
        auto* h  = world->addSphere(1.0f, s, origin);
        if (h) h->setVelocity(vel);
        auto* rm = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::icosphere(s, 1));
        if (rm) { rm->color[0] = 0.2f; rm->color[1] = 0.5f; rm->color[2] = 1.0f; rm->state = 0; }
        if (h && rm) h->linkedMesh = rm;
        break;
    }
    case 1: {
        auto* h  = world->addAABB({s, s, s}, 1.0f, origin);
        if (h) h->setVelocity(vel);
        auto* rm = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::rect({s, s, s}));
        if (rm) { rm->color[0] = 0.3f; rm->color[1] = 0.85f; rm->color[2] = 0.4f; rm->state = 0; }
        if (h && rm) h->linkedMesh = rm;
        break;
    }
    case 2: {
        float sy = s * randF(0.5f, 1.8f);
        auto* h  = world->addOBB({s, sy, s}, 1.0f, origin);
        if (h) { h->setVelocity(vel); h->setRotation(rot); }
        auto* rm = world->addRenderMesh(*gpu, renderer->getCommandPool(), Primitives::rect({s, sy, s}));
        if (rm) { rm->color[0] = 1.0f; rm->color[1] = 0.5f; rm->color[2] = 0.1f; rm->state = 0; }
        if (h && rm) h->linkedMesh = rm;
        break;
    }
    }
}

// ---- Engine::update ---------------------------------------------------------

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

    camera->update(*input, dt);
    if (window->wasResized())
        camera->WindowChanged(window->getWidth(), window->getHeight());

    // LEFT / RIGHT: switch scene
    bool rightNow = input->isKeyDown(GLFW_KEY_RIGHT);
    bool leftNow  = input->isKeyDown(GLFW_KEY_LEFT);
    if (rightNow && !rightWasDown) {
        sceneIndex = (sceneIndex + 1) % SCENE_COUNT;
        loadScene(sceneIndex);
    } else if (leftNow && !leftWasDown) {
        sceneIndex = (sceneIndex + SCENE_COUNT - 1) % SCENE_COUNT;
        loadScene(sceneIndex);
    }
    rightWasDown = rightNow;
    leftWasDown  = leftNow;

    // UP / DOWN: cycle spawn type
    bool upNow   = input->isKeyDown(GLFW_KEY_UP);
    bool downNow = input->isKeyDown(GLFW_KEY_DOWN);
    if (upNow && !upWasDown)
        spawnType = (spawnType + 1) % 3;
    else if (downNow && !downWasDown)
        spawnType = (spawnType + 3 - 1) % 3;
    upWasDown   = upNow;
    downWasDown = downNow;

    // LMB: spawn object (rate-limited)
    spawnCooldown -= dt;
    if (input->isLMBDown() && spawnCooldown <= 0.0f) {
        spawnObject();
        spawnCooldown = SPAWN_RATE;
    }

    // P: toggle physics debug overlay
    bool pNow = input->isKeyDown(GLFW_KEY_P);
    if (pNow && !pWasDown) {
        world->debugPhysics = !world->debugPhysics;
        if (world->debugPhysics)
            world->rebuildDebugMeshes(*gpu, renderer->getCommandPool());
        else {
            gpu->syncDevice();
            world->destroyDebugMeshes(*gpu);
        }
    }
    pWasDown = pNow;

    if (world->debugPhysics)
        world->syncDebugMeshes();

    int objCount = static_cast<int>(world->getHulls().size()) - 1;
    window->setTitle(
        std::to_string(displayFps) + " fps | " +
        std::string(sceneName(sceneIndex)) + " | " + typeName(spawnType) +
        " | Objects: " + std::to_string(objCount) +
        " | Islands: " + std::to_string(world->getIslandCount()) +
        " | Threads: " + std::to_string(world->getThreadCount()) +
        " | LMB=spawn  UP/DOWN=type  LEFT/RIGHT=scene  WASD=move" +
        (world->debugPhysics ? "  [P=debug]" : "  P=debug")
    );

    playerSphere->fixPosition(camera->getPosition());
    playerSphere->setVelocity({});

    physicsAccumulator_ += dt;
    physicsAccumulator_  = std::min(physicsAccumulator_, physics::MAX_PHYSICS_ACCUMULATOR);
    while (physicsAccumulator_ >= physics::PHYSICS_DT) {
        world->advance(physics::PHYSICS_DT);
        physicsAccumulator_ -= physics::PHYSICS_DT;
    }

    for (auto& hull : world->getHulls()) {
        if (hull->linkedMesh)
            hull->linkedMesh->modelMatrix = hull->getModelMatrix();
    }
}

// ---- Engine::render / cleanup -----------------------------------------------

void Engine::render() {
    renderer->drawFrame(*gpu, *window, *camera, *world, *assets);
    hasRendered = true;
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
