#include "Script.h"
#include "ScriptFactory.h"
#include "ScriptContext.h"
#include "rendering/CameraController.h"
#include "world/World.h"
#include "physics/CollisionLayers.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include "audio/Listener.h"
#include "assets/Primitives.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "math/Math.h"
#include "ui/UIManager.h"
#include "ui/Background.h"
#include "ui/TextBox.h"
#include "ui/TextAtlas.h"
#include <GLFW/glfw3.h>
#include <string>
#include <random>
#include <ctime>
#include <cmath>

// ---- Constants ----

static constexpr float SPAWN_SPEED = 18.0f;
static constexpr float SPAWN_RATE  = 0.05f;

static constexpr float PYR_HALF    = 0.45f;
static constexpr float PYR_SPACING = 1.0f;

static constexpr int   GRID_N      = 20;
static constexpr float GRID_STEP   = 1.0f;
static constexpr float NODE_HALF   = 0.45f;
static constexpr float NODE_MASS   = 1.0f;
static constexpr float SPRING_K    = 300.0f;

static constexpr float STRESS_HALF    = 20.0f;
static constexpr float STRESS_CEILING = 25.0f;

static constexpr int SCENE_COUNT = 14;

// ---- Helpers ----

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
    case 3:  return "Spring [Sphere] - Top Row Fixed";
    case 4:  return "Spring [AABB]   - Top Row Fixed";
    case 5:  return "Spring [OBB]    - Top Row Fixed";
    case 6:  return "Spring [Sphere] - 4 Corners";
    case 7:  return "Spring [AABB]   - 4 Corners";
    case 8:  return "Spring [OBB]    - 4 Corners";
    case 9:  return "Spring [Sphere] - 2 Top Corners";
    case 10: return "Spring [AABB]   - 2 Top Corners";
    case 11: return "Spring [OBB]    - 2 Top Corners";
    case 12: return "Stress Test";
    case 13: return "Doppler Test";
    }
    return "?";
}

// ---- SandboxScript ----

class SandboxScript : public Script {
public:
    void init(ScriptContext& ctx) override;
    void update(ScriptContext& ctx, float dt) override;

private:
    ScriptContext*       ctx_      = nullptr;
    FpsCameraController  controller_;

    int  sceneIndex = 12;
    int  spawnType  = 0;

    ecs::Entity  playerEnt_     = ecs::NullEntity;
    Source*      ambientEmitter_ = nullptr;
    Source*      dopplerSource_  = nullptr;
    ecs::Entity  dopplerEnt_    = ecs::NullEntity;

    bool  rightWasDown = false, leftWasDown = false;
    bool  upWasDown    = false, downWasDown  = false;
    bool  pWasDown     = false, mWasDown     = false;
    bool  rWasDown     = false;
    bool  audioPaused_ = false;
    float spawnCooldown = 0.0f;

    float fpsAccum_  = 0.0f;
    int   fpsFrames_ = 0;
    int   fps_       = 0;

    TextAtlas*   atlas_        = nullptr;
    TextBox*     fpsLabel_    = nullptr;
    Background*  debugPanel_  = nullptr;
    TextBox*     debugLabel_  = nullptr;
    bool         debugVisible_ = true;
    bool         hWasDown      = false;

    void loadScene(int index);
    void loadPyramid(int baseN);
    void loadSpringCloth(int variant, int shapeType);
    void loadStressTest();
    void loadDopplerTest();
    void spawnObject();
    void addFloorMesh(float halfW, float halfD);
};

YOPE_REGISTER_SCRIPT(SandboxScript);

// ---- init ----

void SandboxScript::init(ScriptContext& ctx) {
    ctx_ = &ctx;

    ctx_->world->layers.add("default");
    ctx_->world->layers.add("spring_proxy");

    // Persistent directional light across all scenes.
    DirectionalLight dir{};
    dir.direction[0] = -0.4f; dir.direction[1] = -1.0f; dir.direction[2] = -0.6f;
    dir.color[0] = 0.95f; dir.color[1] = 0.95f; dir.color[2] = 1.0f;
    dir.intensity = 0.9f;
    ctx.world->addLight(dir);

    // Persistent looping ambient emitter at {0,2,0}.
    AudioSystem::SoundBuffer* sb = ctx_->audio->loadSound("audios/test.ogg");
    ambientEmitter_ = ctx_->audio->createSource(sb);
    ambientEmitter_->setPosition({0.0f, 2.0f, 0.0f});
    ambientEmitter_->setReferenceDistance(20.0f);
    ambientEmitter_->enableLooping(true);
    ambientEmitter_->play();

    loadScene(sceneIndex);

    ctx.window->setTitle("Yope3D");

    if (ctx.ui) {
        atlas_ = ctx.ui->loadAtlas("fonts/monaco.ttf", 128);

        auto* fpsPanel = ctx.ui->addBackground({0.01f, 0.001f}, {0.15f, 0.075f},
                                               {0.05f, 0.05f, 0.05f, 0.0f}, 0);
        fpsLabel_ = ctx.ui->addTextBox(fpsPanel, atlas_, "FPS: --", 1, 40);

        debugPanel_ = ctx.ui->addBackground({0.72f, 0.005f}, {0.99f, 0.27f},
                                            {0.04f, 0.04f, 0.04f, 0.70f}, 0);
        debugLabel_ = ctx.ui->addTextBox(debugPanel_, atlas_, "", 1, 24);
    }
}

// ---- update ----

void SandboxScript::update(ScriptContext& ctx, float dt) {
    // FPS counter + debug overlay text.
    fpsAccum_ += dt; ++fpsFrames_;
    if (fpsAccum_ >= 0.5f) {
        fps_ = static_cast<int>(fpsFrames_ / fpsAccum_ + 0.5f);
        fpsAccum_ = 0.0f; fpsFrames_ = 0;
        if (fpsLabel_) fpsLabel_->setText("FPS: " + std::to_string(fps_));
        if (debugLabel_ && debugVisible_) {
            int objCount = ctx.world->getHullCount() - 1;
            bool isRT = ctx.renderMode && (*ctx.renderMode == RenderMode::RAYTRACE);
            debugLabel_->setText(
                std::string("Scene: ") + sceneName(sceneIndex) +
                "  \nIslands: " + std::to_string(ctx.world->getIslandCount()) +
                "  Threads: " + std::to_string(ctx.world->getThreadCount()) + "\n" +
                "Objects: " + std::to_string(objCount) +
                "  Spawn Type: " + typeName(spawnType) +
                (ctx.world->debugPhysics ? "  [P: physics on]" : "  P: physics") +
                (audioPaused_           ? "  [M: unmute]"     : "  M: mute") + "\n" +
                "WASD: move  LMB: spawn  LEFT/RIGHT: scene  UP/DOWN: type  H: hide\n" +
                (isRT ? "[R: RAYTRACE]" : "R: raytrace")
            );
        }
    }

    // Camera control.
    controller_.update(*ctx.camera, *ctx.input, dt);

    // Player sphere follows camera (gives camera a collision presence in physics).
    if (playerEnt_ != ecs::NullEntity) {
        auto& reg = ctx.world->getRegistry();
        if (auto* tf = reg.get<Transform>(playerEnt_))
            tf->position = ctx.camera->getPosition();
        if (auto* hc = reg.get<ecs::Hull>(playerEnt_))
            hc->velocity = {};
    }

    // LEFT / RIGHT: switch scene.
    bool rightNow = ctx.input->isKeyDown(GLFW_KEY_RIGHT);
    bool leftNow  = ctx.input->isKeyDown(GLFW_KEY_LEFT);
    if (rightNow && !rightWasDown) {
        sceneIndex = (sceneIndex + 1) % SCENE_COUNT;
        loadScene(sceneIndex);
    } else if (leftNow && !leftWasDown) {
        sceneIndex = (sceneIndex + SCENE_COUNT - 1) % SCENE_COUNT;
        loadScene(sceneIndex);
    }
    rightWasDown = rightNow;
    leftWasDown  = leftNow;

    // UP / DOWN: cycle spawn shape type.
    bool upNow   = ctx.input->isKeyDown(GLFW_KEY_UP);
    bool downNow = ctx.input->isKeyDown(GLFW_KEY_DOWN);
    if (upNow   && !upWasDown)   spawnType = (spawnType + 1) % 3;
    if (downNow && !downWasDown) spawnType = (spawnType + 3 - 1) % 3;
    upWasDown   = upNow;
    downWasDown = downNow;

    // LMB: spawn object (rate-limited).
    spawnCooldown -= dt;
    if (ctx.input->isLMBDown() && spawnCooldown <= 0.0f) {
        spawnObject();
        spawnCooldown = SPAWN_RATE;
    }

    // M: toggle audio pause/resume.
    bool mNow = ctx.input->isKeyDown(GLFW_KEY_M);
    if (mNow && !mWasDown) {
        if (!audioPaused_) { ctx.audio->pauseAll();  audioPaused_ = true;  }
        else               { ctx.audio->resumeAll(); audioPaused_ = false; }
    }
    mWasDown = mNow;

    // H: toggle debug overlay.
    bool hNow = ctx.input->isKeyDown(GLFW_KEY_H);
    if (hNow && !hWasDown) {
        debugVisible_ = !debugVisible_;
        if (debugPanel_)  debugPanel_->setVisible(debugVisible_);
        if (debugLabel_)  debugLabel_->setVisible(debugVisible_);
    }
    hWasDown = hNow;

    // R: toggle raytrace / raster mode.
    bool rNow = ctx.input->isKeyDown(GLFW_KEY_R);
    if (rNow && !rWasDown && ctx.renderMode) {
        *ctx.renderMode = (*ctx.renderMode == RenderMode::RASTER)
                              ? RenderMode::RAYTRACE
                              : RenderMode::RASTER;
    }
    rWasDown = rNow;

    // P: toggle physics debug overlay.
    bool pNow = ctx.input->isKeyDown(GLFW_KEY_P);
    if (pNow && !pWasDown) {
        ctx.world->debugPhysics = !ctx.world->debugPhysics;
        if (ctx.world->debugPhysics)
            ctx.world->rebuildDebugMeshes();
        else
            ctx.world->destroyDebugMeshes();
    }
    pWasDown = pNow;

    // Sync Doppler source position/velocity each visual frame.
    if (dopplerSource_ && dopplerEnt_ != ecs::NullEntity) {
        auto& reg = ctx.world->getRegistry();
        auto* dtf = reg.get<Transform>(dopplerEnt_);
        auto* dhc = reg.get<ecs::Hull>(dopplerEnt_);
        if (dtf && dhc) {
            dopplerSource_->setPosition(dtf->position);
            dopplerSource_->setVelocity(dhc->velocity);
        }
    }
}

// ---- loadScene ----

void SandboxScript::loadScene(int index) {
    if (dopplerSource_) {
        ctx_->audio->removeSource(dopplerSource_);
        dopplerSource_ = nullptr;
    }
    dopplerEnt_ = ecs::NullEntity;
    playerEnt_  = ecs::NullEntity;

    ctx_->camera->setPosition({0.0f, 2.0f, 10.0f});
    ctx_->camera->setRotation({0.0f, 0.0f, 0.0f});

    if (ambientEmitter_ && !ambientEmitter_->isPlaying() && !audioPaused_)
        ambientEmitter_->play();

    ctx_->world->resetPhysics();

    switch (index) {
    case 0: loadPyramid(4);  break;
    case 1: loadPyramid(7);  break;
    case 2: loadPyramid(10); break;
    case 3:  loadSpringCloth(0, 0); break;
    case 4:  loadSpringCloth(0, 1); break;
    case 5:  loadSpringCloth(0, 2); break;
    case 6:  loadSpringCloth(1, 0); break;
    case 7:  loadSpringCloth(1, 1); break;
    case 8:  loadSpringCloth(1, 2); break;
    case 9:  loadSpringCloth(2, 0); break;
    case 10: loadSpringCloth(2, 1); break;
    case 11: loadSpringCloth(2, 2); break;
    case 12: loadStressTest();      break;
    case 13: loadDopplerTest();     break;
    }

    if (ctx_->world->debugPhysics) {
        ctx_->world->destroyDebugMeshes();
        ctx_->world->rebuildDebugMeshes();
    }
}

// ---- addFloorMesh ----

void SandboxScript::addFloorMesh(float halfW, float halfD) {
    ecs::Entity e = ctx_->world->addRenderObject(Primitives::rect({halfW, 0.5f, halfD}));
    if (auto* m = ctx_->world->getMesh(e)) {
        m->color[0] = 0.32f; m->color[1] = 0.28f; m->color[2] = 0.24f; m->state = 0;
        m->modelMatrix = math::Mat4::translate({0.0f, -0.5f, 0.0f});
    }
}

// ---- loadPyramid ----

static void fixPlayerSphere(ecs::Entity e, World* world) {
    auto& reg = world->getRegistry();
    if (auto* hc = reg.get<ecs::Hull>(e)) {
        hc->inverseMass    = 0.0f;
        hc->inverseInertia = math::Mat3::scale({0,0,0});
        hc->velocity       = {};
        hc->omega          = {};
        hc->gravity        = false;
        hc->tangible       = false;
    }
    if (!reg.has<ecs::Fixed>(e))
        reg.add<ecs::Fixed>(e);
}

void SandboxScript::loadPyramid(int baseN) {
    playerEnt_ = ctx_->world->addSphere(1.0f, 0.5f, ctx_->camera->getPosition());
    fixPlayerSphere(playerEnt_, ctx_->world);

    float halfFloor = (baseN + 15) * 1.0f;
    ctx_->world->addStaticAABB({0.0f, -0.5f, 0.0f}, {halfFloor, 0.5f, 15.0f});
    addFloorMesh(halfFloor, 15.0f);

    for (int row = 0; row < baseN; row++) {
        int   count = baseN - row;
        float y     = PYR_HALF + row * (2.0f * PYR_HALF - 0.012f);
        float t     = (baseN > 1) ? (float)row / (baseN - 1) : 0.5f;
        for (int j = 0; j < count; j++) {
            float x = -(count - 1) * PYR_SPACING * 0.5f + j * PYR_SPACING;
            ecs::Entity e = ctx_->world->addOBB({PYR_HALF, PYR_HALF, PYR_HALF}, 1.0f, {x, y, 0.0f});
            ctx_->world->attachMesh(e, Primitives::rect({PYR_HALF, PYR_HALF, PYR_HALF}));
            if (auto* m = ctx_->world->getMesh(e)) {
                m->color[0] = 0.2f + t * 0.7f;
                m->color[1] = 0.45f - t * 0.15f;
                m->color[2] = 0.9f  - t * 0.7f;
                m->state = 0;
            }
        }
    }

    float camZ = baseN + 4.0f;
    float camY = baseN * 0.7f;
    ctx_->camera->setPosition({0.0f, camY, camZ});
    ctx_->camera->setRotation({0.0f, 0.0f, 0.0f});
}

// ---- loadSpringCloth ----
// variant:   0=top-row fixed  1=4-corners fixed  2=2-top-corners fixed
// shapeType: 0=Sphere         1=AABB              2=OBB

void SandboxScript::loadSpringCloth(int variant, int shapeType) {
    playerEnt_ = ctx_->world->addSphere(1.0f, 0.5f, ctx_->camera->getPosition());
    fixPlayerSphere(playerEnt_, ctx_->world);

    float fh = (GRID_N + 1) * GRID_STEP * 2.0f;
    ctx_->world->addStaticAABB({0.0f, -5.0f, 0.0f}, {fh, 5.0f, fh});
    addFloorMesh(fh, fh);

    bool  horizontal = (variant == 1);
    float halfW = (GRID_N - 1) * GRID_STEP * 0.5f;
    float topY  = horizontal ? 25.0f : (GRID_N - 1) * GRID_STEP + 25.0f;

    ecs::Entity grid[GRID_N][GRID_N] = {};

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

            float fi = (float)i / (GRID_N - 1);
            float fj = (float)j / (GRID_N - 1);
            ecs::Entity e = ecs::NullEntity;

            if (shapeType == 0) {
                e = ctx_->world->addSphere(NODE_MASS, NODE_HALF, {cx, cy, cz});
                ctx_->world->attachMesh(e, Primitives::icosphere(NODE_HALF));
                if (auto* m = ctx_->world->getMesh(e)) {
                    m->color[0] = 0.9f - 0.4f*fi; m->color[1] = 0.3f + 0.4f*fj;
                    m->color[2] = 0.15f + 0.3f*fi; m->state = 0;
                }
            } else if (shapeType == 1) {
                e = ctx_->world->addAABB({NODE_HALF,NODE_HALF,NODE_HALF}, NODE_MASS, {cx,cy,cz});
                ctx_->world->attachMesh(e, Primitives::rect({NODE_HALF,NODE_HALF,NODE_HALF}));
                if (auto* m = ctx_->world->getMesh(e)) {
                    m->color[0] = 0.1f+0.2f*fj; m->color[1] = 0.5f+0.3f*fi;
                    m->color[2] = 0.8f-0.3f*fj; m->state = 0;
                }
            } else {
                e = ctx_->world->addOBB({NODE_HALF,NODE_HALF,NODE_HALF}, NODE_MASS, {cx,cy,cz});
                ctx_->world->attachMesh(e, Primitives::rect({NODE_HALF,NODE_HALF,NODE_HALF}));
                if (auto* m = ctx_->world->getMesh(e)) {
                    m->color[0] = fi; m->color[1] = fj;
                    m->color[2] = 0.4f + 0.3f*(fi+fj)*0.5f; m->state = 0;
                }
            }
            grid[i][j] = e;

            bool fix = false;
            if (variant == 0)      fix = (j == 0);
            else if (variant == 1) fix = ((i == 0 || i == GRID_N-1) && (j == 0 || j == GRID_N-1));
            else if (variant == 2) fix = (j == 0 && (i == 0 || i == GRID_N-1));
            if (fix && e != ecs::NullEntity) {
                auto& reg = ctx_->world->getRegistry();
                if (auto* hc = reg.get<ecs::Hull>(e)) {
                    hc->inverseMass = 0.0f; hc->inverseInertia = math::Mat3::scale({0,0,0});
                    hc->velocity = {}; hc->omega = {};
                }
                if (!reg.has<ecs::Fixed>(e)) reg.add<ecs::Fixed>(e);
            }
        }
    }

    for (int j = 0; j < GRID_N; j++)
        for (int i = 0; i < GRID_N - 1; i++)
            ctx_->world->addSpring(grid[i][j], grid[i+1][j], SPRING_K, GRID_STEP);

    for (int i = 0; i < GRID_N; i++)
        for (int j = 0; j < GRID_N - 1; j++)
            ctx_->world->addSpring(grid[i][j], grid[i][j+1], SPRING_K, GRID_STEP);

    float dist = (GRID_N - 1) * GRID_STEP;
    if (horizontal) {
        ctx_->camera->setPosition({0.0f, topY + dist, dist * 0.7f});
        ctx_->camera->setRotation({-0.8f, 0.0f, 0.0f});
    } else {
        ctx_->camera->setPosition({0.0f, topY * 0.5f, dist + 5.0f});
        ctx_->camera->setRotation({0.0f, 0.0f, 0.0f});
    }
}

// ---- loadStressTest ----

void SandboxScript::loadStressTest() {
    playerEnt_ = ctx_->world->addSphere(1.0f, 0.5f, ctx_->camera->getPosition());
    fixPlayerSphere(playerEnt_, ctx_->world);

    ctx_->world->addStaticAABB({0.0f, -0.5f, 0.0f}, {STRESS_HALF, 0.5f, STRESS_HALF});
    addFloorMesh(STRESS_HALF, STRESS_HALF);

    float wH = STRESS_CEILING * 0.5f;
    auto addWall = [&](math::Vec3 pos, math::Vec3 ext) {
        ctx_->world->addStaticAABB(pos, ext);
        ecs::Entity e = ctx_->world->addRenderObject(Primitives::rect(ext));
        if (auto* m = ctx_->world->getMesh(e)) {
            m->color[0] = 0.22f; m->color[1] = 0.22f; m->color[2] = 0.28f; m->state = 0;
            m->modelMatrix = math::Mat4::translate(pos);
        }
    };
    addWall({-STRESS_HALF - 0.4f, wH, 0},   {0.4f, wH, STRESS_HALF});
    addWall({ STRESS_HALF + 0.4f, wH, 0},   {0.4f, wH, STRESS_HALF});
    addWall({0, wH, -STRESS_HALF - 0.4f},   {STRESS_HALF, wH, 0.4f});
    addWall({0, wH,  STRESS_HALF + 0.4f},   {STRESS_HALF, wH, 0.4f});

    ecs::Entity mirrorEnt = ctx_->world->addRenderObject(Primitives::plane(STRESS_HALF - 1.0f));
    if (auto* m = ctx_->world->getMesh(mirrorEnt)) {
        m->modelMatrix  = math::Mat4::translate({0.f, 1.01f, 0.f});
        m->reflectivity = 0.8f;
        m->color[0] = 0.85f; m->color[1] = 0.90f; m->color[2] = 0.95f;
    }

    ctx_->camera->setPosition({0.0f, 3.5f, STRESS_HALF - 2.0f});
    ctx_->camera->setRotation({0.0f, 0.0f, 0.0f});
}

// ---- loadDopplerTest ----

void SandboxScript::loadDopplerTest() {
    if (ambientEmitter_) ambientEmitter_->stop();

    ctx_->world->addStaticAABB({0.0f, -100.5f, 0.0f}, {50.0f, 0.5f, 50.0f});

    ecs::Entity e = ctx_->world->addSphere(1.0f, 0.5f, {0.0f, 30.0f, 0.0f});
    if (auto* hc = ctx_->world->getRegistry().get<ecs::Hull>(e))
        hc->velocity = {0.0f, -40.0f, 0.0f};
    ctx_->world->attachMesh(e, Primitives::icosphere(0.5f));
    if (auto* m = ctx_->world->getMesh(e)) {
        m->color[0] = 1.0f; m->color[1] = 0.35f; m->color[2] = 0.1f; m->state = 0;
    }
    dopplerEnt_ = e;

    AudioSystem::SoundBuffer* sb = ctx_->audio->loadSound("audios/test.ogg");
    dopplerSource_ = ctx_->audio->createSource(sb);
    {
        auto& reg = ctx_->world->getRegistry();
        if (auto* tf = reg.get<Transform>(e)) dopplerSource_->setPosition(tf->position);
        if (auto* hc = reg.get<ecs::Hull>(e)) dopplerSource_->setVelocity(hc->velocity);
    }
    dopplerSource_->enableLooping(true);
    dopplerSource_->play();

    ctx_->camera->setPosition({0.0f, 2.0f, 8.0f});
    ctx_->camera->setRotation({0.0f, 0.0f, 0.0f});
}

// ---- spawnObject ----

void SandboxScript::spawnObject() {
    math::Vec3 forward = ctx_->camera->getForward();
    math::Vec3 origin  = ctx_->camera->getPosition() + forward * 1.5f;
    math::Vec3 vel     = forward * SPAWN_SPEED;
    math::Quat rot     = math::Quat::fromAxisAngle(randomUnitVec(), randF(0, math::PI * 2.0f));
    float s = 0.65f;

    auto& reg = ctx_->world->getRegistry();
    switch (spawnType) {
    case 0: {
        ecs::Entity e = ctx_->world->addSphere(1.0f, s, origin);
        if (auto* hc = reg.get<ecs::Hull>(e)) hc->velocity = vel;
        ctx_->world->attachMesh(e, Primitives::icosphere(s));
        if (auto* m = ctx_->world->getMesh(e)) {
            m->color[0] = 0.2f; m->color[1] = 0.5f; m->color[2] = 1.0f; m->state = 0;
        }
        break;
    }
    case 1: {
        ecs::Entity e = ctx_->world->addAABB({s, s, s}, 1.0f, origin);
        if (auto* hc = reg.get<ecs::Hull>(e)) hc->velocity = vel;
        ctx_->world->attachMesh(e, Primitives::rect({s, s, s}));
        if (auto* m = ctx_->world->getMesh(e)) {
            m->color[0] = 0.3f; m->color[1] = 0.85f; m->color[2] = 0.4f; m->state = 0;
        }
        break;
    }
    case 2: {
        float sy = s * randF(0.5f, 1.8f);
        ecs::Entity e = ctx_->world->addOBB({s, sy, s}, 1.0f, origin);
        if (auto* hc = reg.get<ecs::Hull>(e)) hc->velocity = vel;
        if (auto* tf = reg.get<Transform>(e)) tf->rotation = rot;
        ctx_->world->attachMesh(e, Primitives::rect({s, sy, s}));
        if (auto* m = ctx_->world->getMesh(e)) {
            m->color[0] = 1.0f; m->color[1] = 0.5f; m->color[2] = 0.1f; m->state = 0;
        }
        break;
    }
    }
}
