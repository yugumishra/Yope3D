#include "Engine.h"
#include "math/Math.h"
#include "rendering/Light.h"
#include "assets/Primitives.h"
#include <GLFW/glfw3.h>
#include <string>

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

    // Persistent lights (never reset between scenes)
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
    loadScene(0);
    return true;
}

void Engine::loadScene(int index) {
    world->resetPhysics(*gpu);

    // Player sphere always lives at camera position, fixed/no-gravity
    playerSphere = world->addSphere(1.0f, 0.5f, camera->getPosition());
    playerSphere->fix();
    playerSphere->disableGravity();

    // ---- Shared helpers ----

    // Icosphere with solid color
    auto addBall = [&](float r, float g, float b) -> RenderMesh* {
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::icosphere(0.5f));
        if (m) { m->color[0] = r; m->color[1] = g; m->color[2] = b; m->state = 0; }
        return m;
    };

    // CCD infinite-plane floor (barrier at y=0 + visual rect).
    // Sphere of radius 0.5 rests at y=0.5.
    // rect({100,1,100}) scales ±0.5 cube → full 100×1×100; translate -0.5 → top face at y=0.
    auto addBarrierFloor = [&]() {
        world->addBarrier(physics::Barrier({0,1,0}, {0,0,0}));
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({50.0f, 0.5f, 50.0f}));
        if (m) {
            m->color[0] = 0.35f; m->color[1] = 0.30f; m->color[2] = 0.25f; m->state = 0;
            m->modelMatrix = math::Mat4::translate({0, -0.5f, 0});
        }
    };

    // Discrete static-AABB floor: full extent {50,0.5,50} → top face at y=0.25.
    // Sphere rests at y=0.75.  rect takes FULL extent (physics also uses full extents).
    auto addAABBFloor = [&]() {
        auto* floor = world->addStaticAABB({0, 0, 0}, {50, 0.5f, 50});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({50.0f, 0.5f, 50.0f}));
        if (m) { m->color[0] = 0.35f; m->color[1] = 0.30f; m->color[2] = 0.25f; m->state = 0; }
        if (floor && m) floor->linkedMesh = m;
    };

    // Floating horizontal bounded-barrier panel at given y, half-extent s (panel covers ±s).
    // rect({2s, 0.1, 2s}) = full 2s×0.1×2s; translate so top face sits exactly at panelY.
    auto addPanel = [&](float panelY, float s) {
        world->addBarrier(physics::BoundedBarrier({0,1,0}, {0,panelY,0}, s, s, {1,0,0}));
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({s, 0.05f, s}));
        if (m) {
            m->color[0] = 0.5f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0;
            m->modelMatrix = math::Mat4::translate({0, panelY - 0.05f, 0});
        }
    };

    static const char* kNames[SCENE_COUNT] = {
        // CCD / Barrier
        "CCD/Barrier 1/5 — Drop onto floor",
        "CCD/Barrier 2/5 — High-speed drop",
        "CCD/Barrier 3/5 — Shallow angle approach",
        "CCD/Barrier 4/5 — Resting stability",
        "CCD/Barrier 5/5 — Rolling",
        // CCD / BoundedBarrier
        "CCD/BoundedBarrier 1/4 — Center hit",
        "CCD/BoundedBarrier 2/4 — Edge approach",
        "CCD/BoundedBarrier 3/4 — Edge miss",
        "CCD/BoundedBarrier 4/4 — Resting stability",
        // Discrete / Sphere-Sphere
        "Sphere-Sphere 1/5 — Head-on collision",
        "Sphere-Sphere 2/5 — Glancing collision",
        "Sphere-Sphere 3/5 — One fixed",
        "Sphere-Sphere 4/5 — Resting stack",
        "Sphere-Sphere 5/5 — High-speed",
        // Discrete / Sphere-AABB
        "Sphere-AABB 1/5 — Drop onto AABB top",
        "Sphere-AABB 2/5 — Hit AABB side",
        "Sphere-AABB 3/5 — One fixed",
        "Sphere-AABB 4/5 — Sphere inside AABB",
        "Sphere-AABB 5/5 — High-speed",
        // Springs
        "Spring 1/4 — Two free spheres",
        "Spring 2/4 — One fixed anchor",
        "Spring 3/4 — Compressed",
        "Spring 4/4 — Stretched",
        // CCD / AABB vs Barrier
        "CCD/AABB-Barrier 1/4 — Drop onto floor",
        "CCD/AABB-Barrier 2/4 — High-speed drop",
        "CCD/AABB-Barrier 3/4 — Resting stability",
        "CCD/AABB-Barrier 4/4 — Non-axis-aligned barrier",
        // CCD / AABB vs BoundedBarrier
        "CCD/AABB-BoundedBarrier 1/3 — Center hit",
        "CCD/AABB-BoundedBarrier 2/3 — Edge miss",
        "CCD/AABB-BoundedBarrier 3/3 — Resting stability",
        // Discrete / AABB vs AABB
        "AABB-AABB 1/3 — Drop onto static AABB",
        "AABB-AABB 2/3 — Two dynamic collide",
        "AABB-AABB 3/3 — Stacking",
        // Comparison
        "CCD/Sphere-Barrier (diagonal) — compare vs scene 27",
        // OBB
        "OBB 1/5 — Drop onto barrier floor",
        "OBB 2/5 — Sphere vs OBB",
        "OBB 3/5 — OBB onto static AABB floor",
        "OBB 4/5 — OBB vs OBB head-on",
        "OBB 5/5 — Resting stability",
    };

    window->setTitle(std::string("[") + std::to_string(index + 1) + "/" +
                     std::to_string(SCENE_COUNT) + "] " + kNames[index]);

    switch (index) {

    // ---- CCD: Sphere vs Barrier ----------------------------------------

    case 0: {   // Drop onto floor — falls from rest, bounces, settles
        addBarrierFloor();
        auto* s = world->addSphere(1.0f, 0.5f, {0, 8, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 1: {   // High-speed drop — large height, no tunneling
        addBarrierFloor();
        auto* s = world->addSphere(1.0f, 0.5f, {0, 40, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 2: {   // Shallow angle approach — mostly horizontal velocity
        addBarrierFloor();
        auto* s = world->addSphere(1.0f, 0.5f, {-12, 1.5f, 0});
        s->setVelocity({6.0f, -0.5f, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 3: {   // Resting stability — placed at rest position, must not jitter/sink
        addBarrierFloor();
        auto* s = world->addSphere(1.0f, 0.5f, {0, 0.5f, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 4: {   // Rolling — horizontal velocity while resting on floor
        addBarrierFloor();
        auto* s = world->addSphere(1.0f, 0.5f, {-6, 0.5f, 0});
        s->setVelocity({3.0f, 0, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }

    // ---- CCD: Sphere vs BoundedBarrier ---------------------------------
    // Panel: y=4, half-extent=3 (6×6 m), normal up, orient +X
    // Sphere rests with center at y=4.5; floor barrier catches misses.

    case 5: {   // Center hit
        addBarrierFloor();
        addPanel(4.0f, 3.0f);
        auto* s = world->addSphere(1.0f, 0.5f, {0, 10, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 6: {   // Edge approach — sphere inside the 3 m boundary
        addBarrierFloor();
        addPanel(4.0f, 3.0f);
        auto* s = world->addSphere(1.0f, 0.5f, {2.0f, 10, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 7: {   // Edge miss — sphere outside boundary, must pass through
        addBarrierFloor();
        addPanel(4.0f, 3.0f);
        auto* s = world->addSphere(1.0f, 0.5f, {3.5f, 10, 0});
        s->linkedMesh = addBall(1.0f, 0.3f, 0.3f);  // red = "should fall through"
        break;
    }
    case 8: {   // Resting stability on bounded panel
        addBarrierFloor();
        addPanel(4.0f, 3.0f);
        auto* s = world->addSphere(1.0f, 0.5f, {0, 4.5f, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }

    // ---- Discrete: Sphere vs Sphere ------------------------------------

    case 9: {   // Head-on — equal-mass spheres approaching directly
        addBarrierFloor();
        auto* a = world->addSphere(1.0f, 0.5f, {-5, 3, 0});
        a->setVelocity({4.0f, 0, 0});
        a->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        auto* b = world->addSphere(1.0f, 0.5f, {5, 3, 0});
        b->setVelocity({-4.0f, 0, 0});
        b->linkedMesh = addBall(1.0f, 0.4f, 0.2f);
        break;
    }
    case 10: {  // Glancing — off-center, both deflect at angles
        addBarrierFloor();
        auto* a = world->addSphere(1.0f, 0.5f, {-5, 3, 0.35f});
        a->setVelocity({4.0f, 0, 0});
        a->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        auto* b = world->addSphere(1.0f, 0.5f, {5, 3, 0});
        b->setVelocity({-4.0f, 0, 0});
        b->linkedMesh = addBall(1.0f, 0.4f, 0.2f);
        break;
    }
    case 11: {  // One fixed — dynamic sphere hits immovable sphere
        addBarrierFloor();
        auto* fixed = world->addSphere(1.0f, 0.5f, {0, 3, 0});
        fixed->fix();
        fixed->linkedMesh = addBall(0.8f, 0.8f, 0.2f);
        auto* dyn = world->addSphere(1.0f, 0.5f, {-2, 3, 0});
        dyn->setVelocity({5.0f, 0, 0});
        dyn->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 12: {  // Resting stack — sphere balanced on top of fixed sphere
        addBarrierFloor();
        auto* base = world->addSphere(1.0f, 0.5f, {0, 0.5f, 0});
        base->fix();
        base->linkedMesh = addBall(0.8f, 0.8f, 0.2f);
        auto* top = world->addSphere(1.0f, 0.5f, {0, 3.0f, 0});
        top->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 13: {  // High-speed — fast sphere must not tunnel
        addBarrierFloor();
        auto* fast = world->addSphere(1.0f, 0.5f, {-15, 3, 0});
        fast->setVelocity({25.0f, 0, 0});
        fast->linkedMesh = addBall(1.0f, 0.2f, 0.2f);
        auto* still = world->addSphere(1.0f, 0.5f, {0, 3, 0});
        still->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }

    // ---- Discrete: Sphere vs AABB -------------------------------------
    // AABB floor: top face at y=0.5, sphere rests at y=1.0.

    case 14: {  // Drop onto AABB top
        addAABBFloor();
        auto* platform = world->addStaticAABB({0, 3.0f, 0}, {2, 0.5f, 2});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({2.0f, 0.5f, 2.0f}));
            if (m) { m->color[0] = 0.7f; m->color[1] = 0.5f; m->color[2] = 0.3f; m->state = 0; }
            if (platform && m) platform->linkedMesh = m;
        }
        auto* s = world->addSphere(1.0f, 0.5f, {0, 10, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 15: {  // Hit AABB side — sphere rolls off platform edge and hits wall
        addAABBFloor();
        auto* wall = world->addStaticAABB({6, 2.5f, 0}, {0.5f, 2.5f, 3.0f});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.5f, 2.5f, 3.0f}));
            if (m) { m->color[0] = 0.7f; m->color[1] = 0.5f; m->color[2] = 0.3f; m->state = 0; }
            if (wall && m) wall->linkedMesh = m;
        }
        auto* s = world->addSphere(1.0f, 0.5f, {-4, 1.0f, 0});
        s->setVelocity({5.0f, 0, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 16: {  // One fixed — dynamic sphere hits static AABB floor only
        addAABBFloor();
        auto* s = world->addSphere(1.0f, 0.5f, {0, 8, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 17: {  // Sphere inside AABB at start — must be ejected without explosion
        addBarrierFloor();
        auto* box = world->addStaticAABB({0, 3, 0}, {2, 2, 2});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({2.0f, 2.0f, 2.0f}));
            if (m) { m->color[0] = 0.7f; m->color[1] = 0.3f; m->color[2] = 0.7f; m->state = 0; }
            if (box && m) box->linkedMesh = m;
        }
        auto* s = world->addSphere(1.0f, 0.5f, {0, 3, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 18: {  // High-speed — fast sphere vs thin AABB wall, no tunneling
        addBarrierFloor();
        auto* wall = world->addStaticAABB({6, 3, 0}, {0.2f, 3.0f, 3.0f});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.2f, 3.0f, 3.0f}));
            if (m) { m->color[0] = 0.7f; m->color[1] = 0.5f; m->color[2] = 0.3f; m->state = 0; }
            if (wall && m) wall->linkedMesh = m;
        }
        auto* s = world->addSphere(1.0f, 0.5f, {-10, 3, 0});
        s->setVelocity({30.0f, 0, 0});
        s->linkedMesh = addBall(1.0f, 0.2f, 0.2f);
        break;
    }

    // ---- Springs -------------------------------------------------------

    case 19: {  // Two free spheres — at 6 apart, rest=2; spring pulls, oscillates
        addBarrierFloor();
        auto* a = world->addSphere(1.0f, 0.5f, {-3, 5, 0});
        a->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        auto* b = world->addSphere(1.0f, 0.5f, {3, 5, 0});
        b->linkedMesh = addBall(1.0f, 0.4f, 0.2f);
        world->addSpring(a, b, 5.0f, 2.0f);
        break;
    }
    case 20: {  // One fixed anchor — sphere bobs on spring below fixed point
        addBarrierFloor();
        auto* anchor = world->addSphere(1.0f, 0.5f, {0, 8, 0});
        anchor->fix();
        anchor->linkedMesh = addBall(0.8f, 0.8f, 0.2f);
        auto* bob = world->addSphere(1.0f, 0.5f, {0, 3, 0});
        bob->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        world->addSpring(anchor, bob, 5.0f, 3.0f);
        break;
    }
    case 21: {  // Compressed — spheres 1 apart, rest=3; spring pushes them apart
        addBarrierFloor();
        auto* a = world->addSphere(1.0f, 0.5f, {-0.5f, 5, 0});
        a->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        auto* b = world->addSphere(1.0f, 0.5f, {0.5f, 5, 0});
        b->linkedMesh = addBall(1.0f, 0.4f, 0.2f);
        world->addSpring(a, b, 5.0f, 3.0f);
        break;
    }
    case 22: {  // Stretched — spheres 8 apart, rest=3; spring pulls them together
        addBarrierFloor();
        auto* a = world->addSphere(1.0f, 0.5f, {-4, 5, 0});
        a->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        auto* b = world->addSphere(1.0f, 0.5f, {4, 5, 0});
        b->linkedMesh = addBall(1.0f, 0.4f, 0.2f);
        world->addSpring(a, b, 5.0f, 3.0f);
        break;
    }

    // ---- CCD: AABB vs Barrier ---------------------------------------------

    case 23: {  // Drop onto floor — AABB falls, bounces, settles stably
        addBarrierFloor();
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 8, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }
    case 24: {  // High-speed drop — large height, no tunneling
        addBarrierFloor();
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 50, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 1.0f; m->color[1] = 0.3f; m->color[2] = 0.2f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }
    case 25: {  // Resting stability — AABB placed at rest height, must not jitter/drift
        addBarrierFloor();
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 0.5f, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }
    case 26: {  // Non-axis-aligned barrier — 45-degree ramp, AABB slides/deflects
        world->addBarrier(physics::Barrier(math::Vec3{1,1,0}.normalize(), {0,0,0}));
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({30.0f, 0.5f, 30.0f}));
            if (m) {
                m->color[0] = 0.35f; m->color[1] = 0.30f; m->color[2] = 0.25f; m->state = 0;
                m->modelMatrix = math::Mat4::translate({-0.354f, -0.354f, 0.0f});
                m->modelMatrix.setRotationScale(math::Mat3::rotation({0,0,1}, math::toRadians(-45.0f)));
            }
        }
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {-4, 8, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }

    // ---- CCD: AABB vs BoundedBarrier --------------------------------------
    // Panel: y=4, half-extent=3. AABB half-height=0.5 → rests with center at y=4.5.

    case 27: {  // Center hit — AABB drops onto middle of panel
        addBarrierFloor();
        addPanel(4.0f, 3.0f);
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 10, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }
    case 28: {  // Edge miss — AABB drops outside panel boundary, must fall through
        addBarrierFloor();
        addPanel(4.0f, 3.0f);
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {4.0f, 10, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 1.0f; m->color[1] = 0.3f; m->color[2] = 0.3f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }
    case 29: {  // Resting stability — AABB placed at rest height on panel
        addBarrierFloor();
        addPanel(4.0f, 3.0f);
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 4.5f, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }

    // ---- Discrete: AABB vs AABB -------------------------------------------

    case 30: {  // Drop onto static AABB — dynamic AABB falls onto fixed floor AABB
        addAABBFloor();
        auto* box = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 8, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
        if (box && m) box->linkedMesh = m;
        break;
    }
    case 31: {  // Two dynamic AABBs collide — pushed toward each other, separate correctly
        addBarrierFloor();
        auto* a = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {-5, 3, 0});
        a->setVelocity({4.0f, 0, 0});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.5f, 0.5f, 0.5f}));
            if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
            if (a && m) a->linkedMesh = m;
        }
        auto* b = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {5, 3, 0});
        b->setVelocity({-4.0f, 0, 0});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.5f, 0.5f, 0.5f}));
            if (m) { m->color[0] = 1.0f; m->color[1] = 0.4f; m->color[2] = 0.2f; m->state = 0; }
            if (b && m) b->linkedMesh = m;
        }
        break;
    }
    case 32: {  // Stacking — one dynamic AABB resting on another dynamic AABB on floor
        addBarrierFloor();
        auto* bottom = world->addAABB({1.0f, 0.5f, 1.0f}, 2.0f, {0, 0.5f, 0});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({1.0f, 0.5f, 1.0f}));
            if (m) { m->color[0] = 0.8f; m->color[1] = 0.8f; m->color[2] = 0.2f; m->state = 0; }
            if (bottom && m) bottom->linkedMesh = m;
        }
        auto* top = world->addAABB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 8, 0});
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.5f, 0.5f, 0.5f}));
            if (m) { m->color[0] = 0.3f; m->color[1] = 0.7f; m->color[2] = 0.4f; m->state = 0; }
            if (top && m) top->linkedMesh = m;
        }
        break;
    }

    case 33: {  // Sphere on same 45-degree barrier as scene 26 — visual comparison
        world->addBarrier(physics::Barrier(math::Vec3{1,1,0}.normalize(), {0,0,0}));
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({30.0f, 0.5f, 30.0f}));
            if (m) {
                m->color[0] = 0.35f; m->color[1] = 0.30f; m->color[2] = 0.25f; m->state = 0;
                m->modelMatrix = math::Mat4::translate({-0.354f, -0.354f, 0.0f});
                m->modelMatrix.setRotationScale(math::Mat3::rotation({0,0,1}, math::toRadians(-45.0f)));
            }
        }
        auto* s = world->addSphere(1.0f, 0.5f, {-4, 8, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }

    // ---- OBB scenes --------------------------------------------------------

    case 34: {  // OBB drops onto barrier floor — CCD bounce + spin
        addBarrierFloor();
        auto* o = world->addOBB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 8, 0});
        // Slight initial rotation so it doesn't land perfectly flat
        o->setRotation(math::Quat::fromAxisAngle({0,0,1}, math::toRadians(20.0f)));
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.8f; m->color[1] = 0.4f; m->color[2] = 0.1f; m->state = 0; }
        if (o && m) o->linkedMesh = m;
        break;
    }
    case 35: {  // Sphere fires at stationary OBB — deflects with spin
        addBarrierFloor();
        auto* o = world->addOBB({0.6f, 0.6f, 0.6f}, 2.0f, {0, 3, 0});
        o->setRotation(math::Quat::fromAxisAngle({0,1,0}, math::toRadians(30.0f)));
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.6f, 0.6f, 0.6f}));
            if (m) { m->color[0] = 0.8f; m->color[1] = 0.4f; m->color[2] = 0.1f; m->state = 0; }
            if (o && m) o->linkedMesh = m;
        }
        auto* s = world->addSphere(1.0f, 0.5f, {-6, 3, 0});
        s->setVelocity({6.0f, 0, 0});
        s->linkedMesh = addBall(0.2f, 0.6f, 1.0f);
        break;
    }
    case 36: {  // OBB drops onto static AABB floor — discrete response
        addAABBFloor();
        auto* o = world->addOBB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 8, 0});
        o->setRotation(math::Quat::fromAxisAngle({0,0,1}, math::toRadians(15.0f)));
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.8f; m->color[1] = 0.4f; m->color[2] = 0.1f; m->state = 0; }
        if (o && m) o->linkedMesh = m;
        break;
    }
    case 37: {  // Two OBBs approaching head-on
        addBarrierFloor();
        auto* oa = world->addOBB({0.5f, 0.5f, 0.5f}, 1.0f, {-5, 3, 0});
        oa->setVelocity({4.0f, 0, 0});
        oa->setRotation(math::Quat::fromAxisAngle({0,1,0}, math::toRadians(20.0f)));
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.5f, 0.5f, 0.5f}));
            if (m) { m->color[0] = 0.8f; m->color[1] = 0.4f; m->color[2] = 0.1f; m->state = 0; }
            if (oa && m) oa->linkedMesh = m;
        }
        auto* ob = world->addOBB({0.5f, 0.5f, 0.5f}, 1.0f, {5, 3, 0});
        ob->setVelocity({-4.0f, 0, 0});
        ob->setRotation(math::Quat::fromAxisAngle({0,1,0}, math::toRadians(-20.0f)));
        {
            auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                           Primitives::rect({0.5f, 0.5f, 0.5f}));
            if (m) { m->color[0] = 0.2f; m->color[1] = 0.5f; m->color[2] = 0.9f; m->state = 0; }
            if (ob && m) ob->linkedMesh = m;
        }
        break;
    }
    case 38: {  // OBB resting stability — placed at rest height on barrier floor
        addBarrierFloor();
        auto* o = world->addOBB({0.5f, 0.5f, 0.5f}, 1.0f, {0, 0.5f, 0});
        auto* m = world->addRenderMesh(*gpu, renderer->getCommandPool(),
                                       Primitives::rect({0.5f, 0.5f, 0.5f}));
        if (m) { m->color[0] = 0.8f; m->color[1] = 0.4f; m->color[2] = 0.1f; m->state = 0; }
        if (o && m) o->linkedMesh = m;
        break;
    }

    default: break;
    }
}

void Engine::update() {
    double now = glfwGetTime();
    float  dt  = static_cast<float>(now - lastTime);
    lastTime   = now;
    if (dt > 0.1f) dt = 0.1f;

    camera->update(*input, dt);
    if (window->wasResized())
        camera->WindowChanged(window->getWidth(), window->getHeight());

    // Scene cycling: RIGHT advances, LEFT goes back (both wrap).
    // isKeyPressed clears flags before update() sees them (beginFrame ordering bug),
    // so use isKeyDown + manual rising-edge detection instead.
    bool rightNow = input->isKeyDown(GLFW_KEY_RIGHT);
    bool leftNow  = input->isKeyDown(GLFW_KEY_LEFT);
    if (rightNow && !rightWasDown) {
        sceneIndex = (sceneIndex + 1) % SCENE_COUNT;
        gpu->syncDevice();   // wait for in-flight GPU work; does NOT tear down the renderer
        loadScene(sceneIndex);
    } else if (leftNow && !leftWasDown) {
        sceneIndex = (sceneIndex - 1 + SCENE_COUNT) % SCENE_COUNT;
        gpu->syncDevice();
        loadScene(sceneIndex);
    }
    rightWasDown = rightNow;
    leftWasDown  = leftNow;

    playerSphere->fixPosition(camera->getPosition());
    playerSphere->setVelocity({});
    world->advance(dt);

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
