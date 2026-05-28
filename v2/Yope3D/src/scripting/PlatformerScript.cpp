#include "Script.h"
#include "ScriptFactory.h"
#include "ScriptContext.h"
#include "rendering/Camera.h"
#include "rendering/Light.h"
#include "world/World.h"
#include "physics/Raycast.h"
#include "world/Transform.h"
#include "assets/Primitives.h"
#include "assets/ObjLoader.h"
#include "platform/Input.h"
#include "math/Math.h"
#include "math/Mat3.h"
#include "ui/UIManager.h"
#include "ui/Background.h"
#include "ui/TextBox.h"
#include "ui/TextAtlas.h"
#include "ecs/Components.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <cmath>
#include <string>
#include <random>
#include <ctime>
#include <limits>

static constexpr float PLAT_HALF_XZ = 1.0f;
static constexpr float PLAT_HALF_Y  = 0.5f;
static constexpr float PLAYER_R     = 0.5f;
static constexpr float CAMERA_LIFT  = 0.6f;     // camera above sphere center
static constexpr float SENSITIVITY  = 0.002f;
static constexpr float BASE_SPEED   = 1.4f;     // per-frame impulse — steady state ~9.3 m/s with 0.85 damping
static constexpr float HORIZ_DAMP   = 0.85f;    // script-side per-frame horizontal velocity decay
static constexpr float JUMP_VEL     = 10.0f;
static constexpr float DASH_MULT    = 4.0f;
static constexpr int   DASH_FRAMES  = 31;
static constexpr int   DASH_COOLDOWN_FRAMES = 240;
static constexpr float FOV_BOOST    = 0.25f;
static constexpr float WIN_TIME     = 90.0f;
static constexpr float PLAYER_FRICTION = 0.05f; // very low so contact friction doesn't fight WASD

static constexpr int   PLATFORM_COUNT = 51;
static constexpr float INITIAL_RADIUS = 15.0f;
static constexpr float ANGLE_STEP     = 0.3f;
static constexpr float HEIGHT_STEP    = 2.0f;
static constexpr float RADIUS_GROW    = 0.5f;

static constexpr float STAR_PLAT_X   = -50.0f;
static constexpr float STAR_PLAT_Y   = 65.0f;
static constexpr float STAR_PLAT_Z   =  0.0f;
static constexpr float STAR_Y_OFFSET =  4.0f;

static constexpr int   PARTICLE_COUNT = 50;
static constexpr float PARTICLE_SPEED = 5.0f;

static math::Vec3 platform49Pos() {
    float radius = INITIAL_RADIUS + 49 * RADIUS_GROW;
    float angle  = 49 * ANGLE_STEP;
    return {radius * std::cos(angle), PLAT_HALF_Y + 49 * HEIGHT_STEP, radius * std::sin(angle)};
}

static std::mt19937 platRng(static_cast<unsigned>(std::time(nullptr)));
static float platRandF(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(platRng);
}
static math::Vec3 randUnitVec3() {
    math::Vec3 v;
    do { v = {platRandF(-1,1), platRandF(-1,1), platRandF(-1,1)}; }
    while (v.dot(v) < 1e-4f);
    return v * (1.0f / std::sqrt(v.dot(v)));
}

class PlatformerScript : public Script {
public:
    void init(ScriptContext& ctx) override;
    void update(ScriptContext& ctx, float dt) override;

private:
    ecs::Entity playerEnt_ = ecs::NullEntity;

    RenderMesh* starMesh_ = nullptr;
    math::Vec3  starPos_  = {STAR_PLAT_X, STAR_PLAT_Y + STAR_Y_OFFSET, STAR_PLAT_Z};

    std::vector<RenderMesh*> particles_;
    std::vector<math::Vec3>  particleVels_;
    std::vector<math::Vec3>  particlePositions_;

    bool  won_           = false;
    bool  starCollected_ = false;

    float gameTime_  = 0.0f;
    float totalTime_ = 0.0f;

    int   dashFrames_   = 0;
    int   dashCooldown_ = 0;
    float baseFov_      = 0.0f;

    TextAtlas*  atlas_      = nullptr;
    TextBox*    timerLabel_ = nullptr;
    Background* winBg_      = nullptr;
    TextBox*    winLabel_   = nullptr;
};

void PlatformerScript::init(ScriptContext& ctx) {
    DirectionalLight dir{};
    dir.direction[0] = -0.4f; dir.direction[1] = -1.0f; dir.direction[2] = -0.6f;
    dir.color[0] = 0.95f; dir.color[1] = 0.95f; dir.color[2] = 1.0f;
    dir.intensity = 0.9f;
    ctx.world->addLight(dir);

    // Floor (StaticAABB, top at y=0)
    ctx.world->addStaticAABB({0.0f, -0.5f, 0.0f}, {50.0f, 0.5f, 50.0f});

    ecs::Entity floorEnt = ctx.world->addRenderObject(Primitives::plane(50.0f));
    if (auto* m = ctx.world->getMesh(floorEnt)) {
        m->color[0] = 0.2f; m->color[1] = 0.8f; m->color[2] = 0.9f;
        m->modelMatrix = math::Mat4{};
        m->transformReady = true;
    }

    // Spiral platforms
    float radius = INITIAL_RADIUS;
    for (int i = 0; i < PLATFORM_COUNT; ++i) {
        float angle = ANGLE_STEP * i;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        float y = PLAT_HALF_Y + HEIGHT_STEP * i;
        radius += RADIUS_GROW;

        ecs::Entity obj = ctx.world->addStaticAABB({x, y, z}, {PLAT_HALF_XZ, PLAT_HALF_Y, PLAT_HALF_XZ});
        auto* m = ctx.world->attachMesh(obj, Primitives::rect({PLAT_HALF_XZ, PLAT_HALF_Y, PLAT_HALF_XZ}));
        if (m) {
            m->color[0] = platRandF(0.3f, 1.0f);
            m->color[1] = platRandF(0.3f, 1.0f);
            m->color[2] = platRandF(0.3f, 1.0f);
        }
    }

    // Star side-platform (requires a dash to reach)
    {
        ecs::Entity obj = ctx.world->addStaticAABB(
            {STAR_PLAT_X, STAR_PLAT_Y, STAR_PLAT_Z},
            {PLAT_HALF_XZ * 2.0f, PLAT_HALF_Y, PLAT_HALF_XZ * 2.0f});
        auto* m = ctx.world->attachMesh(
            obj, Primitives::rect({PLAT_HALF_XZ * 2.0f, PLAT_HALF_Y, PLAT_HALF_XZ * 2.0f}));
        if (m) { m->color[0] = 1.0f; m->color[1] = 0.85f; m->color[2] = 0.1f; }
    }

    // Star OBJ
    auto starLoaded = ObjLoader::load(
        (std::filesystem::path(YOPE_ASSETS_DIR) / "models/star.obj").string());
    ecs::Entity starEnt = ctx.world->addRenderObject(starLoaded);
    starMesh_ = ctx.world->getMesh(starEnt);
    if (starMesh_) {
        starMesh_->color[0] = 1.0f; starMesh_->color[1] = 0.813f; starMesh_->color[2] = 0.0f;
        starMesh_->modelMatrix = math::Mat4::translate(starPos_);
        starMesh_->transformReady = true;
    }

    // Player sphere — physics-driven character.
    // Spawn above platform 0 so it lands on it.
    playerEnt_ = ctx.world->addSphere(1.0f, PLAYER_R, {INITIAL_RADIUS, 125.0f, 0.0f});
    {
        auto* h = ctx.world->getRegistry().get<ecs::Hull>(playerEnt_);
        if (h) {
            h->sleepingEnabled = false;
            h->sleepFrames     = 0;
            h->friction        = PLAYER_FRICTION;
            h->linearDamping   = 0.0f;
        }
    }

    // Camera at the spawn sphere
    baseFov_ = ctx.camera->getFov();
    ctx.camera->setPosition({INITIAL_RADIUS, 5.0f + CAMERA_LIFT, 0.0f});
    ctx.camera->setRotation({0.0f, 0.0f, 0.0f});

    if (ctx.ui) {
        atlas_ = ctx.ui->loadAtlas("fonts/monaco.ttf", 128);
        auto* timerBg = ctx.ui->addBackground({0.82f,0.01f},{0.99f,0.07f},{0,0,0,0.55f},0);
        timerLabel_ = ctx.ui->addTextBox(timerBg, atlas_, "Time: 0.0", 1, 32);
        winBg_   = ctx.ui->addBackground({0.35f,0.42f},{0.65f,0.62f},{0.08f,0.08f,0.08f,0.8f},0);
        winLabel_ = ctx.ui->addTextBox(winBg_, atlas_, "", 1, 72);
        winBg_->setVisible(false);
        winLabel_->setVisible(false);
    }
}

void PlatformerScript::update(ScriptContext& ctx, float dt) {
    totalTime_ += dt;
    if (!won_) gameTime_ += dt;

    auto& reg = ctx.world->getRegistry();
    auto* hull     = reg.get<ecs::Hull>(playerEnt_);
    auto* playerTf = reg.get<Transform>(playerEnt_);
    math::Vec3 spherePos = playerTf ? playerTf->position : math::Vec3{};

    if (!won_) {
        // ---- Mouselook (manual — matches FpsCameraController convention) ----
        auto delta = ctx.input->getMouseDelta();
        math::Vec3 rot = ctx.camera->getRotation();
        rot.y += static_cast<float>(delta.x) * -SENSITIVITY;
        rot.x = math::clamp(rot.x + static_cast<float>(delta.y) * -SENSITIVITY,
                            -1.5607963f, 1.5607963f);
        ctx.camera->setRotation(rot);

        // ---- WASD direction (camera-yaw relative) ----
        float yaw = rot.y;
        float sy  = std::sin(yaw), cy = std::cos(yaw);
        math::Vec3 fwd   = {-sy, 0.0f, -cy};
        math::Vec3 right = { cy, 0.0f, -sy};
        math::Vec3 dir   = {};
        if (ctx.input->isKeyDown(GLFW_KEY_W)) dir += fwd;
        if (ctx.input->isKeyDown(GLFW_KEY_S)) dir -= fwd;
        if (ctx.input->isKeyDown(GLFW_KEY_D)) dir += right;
        if (ctx.input->isKeyDown(GLFW_KEY_A)) dir -= right;

        // ---- Dash (F key) ----
        if (ctx.input->isKeyDown(GLFW_KEY_F) && dashCooldown_ == 0 && dashFrames_ == 0)
            dashFrames_ = DASH_FRAMES;
        float speed = BASE_SPEED;
        if (dashFrames_ > 0) {
            speed *= DASH_MULT;
            float t = 1.0f - static_cast<float>(dashFrames_) / DASH_FRAMES;
            ctx.camera->setFOV(baseFov_ + FOV_BOOST * std::sin(t * math::PI));
            --dashFrames_;
            if (dashFrames_ == 0) {
                dashCooldown_ = DASH_COOLDOWN_FRAMES;
                ctx.camera->setFOV(baseFov_);
            }
        }
        if (dashCooldown_ > 0) --dashCooldown_;

        // ---- Ground check (downward raycast against static AABBs) ----
        bool grounded = false;
        math::Vec3 downRay = {0.0f, -1.0f, 0.0f};
        constexpr float MISS = std::numeric_limits<float>::min();
        for (auto [e, af] : reg.view<ecs::AABBForm>()) {
            if (e == playerEnt_) continue;
            auto* atf = reg.get<Transform>(e);
            if (!atf) continue;
            float t = physics::Raycast::raycastAABB(
                downRay, spherePos, atf->position, af.extent);
            if (t != MISS && t > 0.0f && t < PLAYER_R + 0.25f) {
                grounded = true;
                break;
            }
        }

        // ---- Apply velocity ----
        math::Vec3 vel = hull ? hull->velocity : math::Vec3{};
        vel.x *= HORIZ_DAMP;   // script-side horizontal damping (snappy stop on release)
        vel.z *= HORIZ_DAMP;
        if (dir.length() > 0.01f)
            vel += dir.normalize() * speed;
        if (ctx.input->isKeyDown(GLFW_KEY_SPACE) && grounded && vel.y < 0.1f)
            vel.y = JUMP_VEL;
        if (hull) hull->velocity = vel;
    }

    // ---- Camera follows sphere ----
    ctx.camera->setPosition(spherePos + math::Vec3{0.0f, CAMERA_LIFT, 0.0f});

    // ---- Star animation ----
    if (starMesh_ && !starCollected_) {
        starPos_ = {STAR_PLAT_X,
                    STAR_PLAT_Y + STAR_Y_OFFSET + std::sin(3.0f * totalTime_) * 0.4f,
                    STAR_PLAT_Z};
        math::Mat4 rotMat;
        rotMat.setRotationScale(math::Mat3::rotation({0,1,0}, totalTime_ * 2.0f));
        starMesh_->modelMatrix = math::Mat4::translate(starPos_) * rotMat;
    }

    // ---- Particle update ----
    for (int i = 0; i < static_cast<int>(particles_.size()); ++i) {
        particlePositions_[i] += particleVels_[i] * dt;
        math::Mat4 pRot;
        pRot.setRotationScale(math::Mat3::rotation({0,1,0}, totalTime_ * 3.0f + i));
        particles_[i]->modelMatrix = math::Mat4::translate(particlePositions_[i]) *
                                     math::Mat4::scale({0.2f,0.2f,0.2f}) * pRot;
    }

    // ---- Star collection ----
    if (!starCollected_ && starMesh_) {
        if ((spherePos - starPos_).length() < 2.5f) {
            starCollected_ = true;
            starMesh_->modelMatrix = math::Mat4::scale({0,0,0});
            particles_.reserve(PARTICLE_COUNT);
            particleVels_.reserve(PARTICLE_COUNT);
            particlePositions_.reserve(PARTICLE_COUNT);
            for (int i = 0; i < PARTICLE_COUNT; ++i) {
                auto pLoaded = ObjLoader::load(
                    (std::filesystem::path(YOPE_ASSETS_DIR) / "models/star.obj").string());
                ecs::Entity pEnt = ctx.world->addRenderObject(pLoaded);
                if (auto* pm = ctx.world->getMesh(pEnt)) {
                    pm->color[0] = 1.0f; pm->color[1] = 0.813f; pm->color[2] = 0.0f;
                    pm->transformReady = true;
                    math::Vec3 sp = starPos_ + randUnitVec3() * 0.5f;
                    pm->modelMatrix = math::Mat4::translate(sp) * math::Mat4::scale({0.2f,0.2f,0.2f});
                    particles_.push_back(pm);
                    particleVels_.push_back(randUnitVec3() * (PARTICLE_SPEED * platRandF(0.5f,1.5f)));
                    particlePositions_.push_back(sp);
                }
            }
        }
    }

    // ---- Win check (reach platform 49) ----
    if (!won_) {
        math::Vec3 top49 = platform49Pos();
        math::Vec3 diff  = spherePos - top49;
        diff.y = 0.0f;
        if (diff.length() < 3.0f && std::abs(spherePos.y - (top49.y + PLAT_HALF_Y + PLAYER_R)) < 3.0f) {
            won_ = true;
            ctx.camera->setFOV(baseFov_);
            if (winBg_ && winLabel_) {
                winBg_->setVisible(true);
                std::string msg;
                if (starCollected_ && gameTime_ < WIN_TIME)
                    msg = "YOU WIN!\nTime: " + std::to_string(static_cast<int>(gameTime_)) + "." +
                          std::to_string(static_cast<int>(gameTime_ * 10) % 10) + "s";
                else if (!starCollected_)
                    msg = "Incomplete\nCollect the star first!";
                else
                    msg = "Too slow!\n" + std::to_string(static_cast<int>(gameTime_)) + "s (need <90s)";
                winLabel_->setText(msg);
                winLabel_->setVisible(true);
            }
        }
    }

    if (timerLabel_ && !won_) {
        int secs   = static_cast<int>(gameTime_);
        int tenths = static_cast<int>(gameTime_ * 10) % 10;
        timerLabel_->setText("Time: " + std::to_string(secs) + "." + std::to_string(tenths) + "s");
    }
}

YOPE_REGISTER_SCRIPT(PlatformerScript);
