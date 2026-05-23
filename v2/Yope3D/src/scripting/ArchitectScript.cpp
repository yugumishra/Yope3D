#include "Script.h"
#include "ScriptFactory.h"
#include "ScriptContext.h"
#include "rendering/Camera.h"
#include "rendering/Light.h"
#include "world/World.h"
#include "world/SceneObject.h"
#include "physics/CSphere.h"
#include "assets/Primitives.h"
#include "assets/AssetManager.h"
#include "audio/AudioSystem.h"
#include "audio/Source.h"
#include "platform/Input.h"
#include "math/Math.h"
#include "ui/UIManager.h"
#include "ui/Background.h"
#include "ui/TextBox.h"
#include "ui/TextAtlas.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <string>

static constexpr float ARCH_PLAYER_R    = 0.5f;
static constexpr float ARCH_CAMERA_LIFT = 0.6f;
static constexpr float ARCH_SENSITIVITY = 0.002f;
static constexpr float ARCH_BASE_SPEED  = 1.0f;    // steady state ~6.7 m/s
static constexpr float ARCH_HORIZ_DAMP  = 0.85f;
static constexpr float ARCH_JUMP_VEL    = 6.0f;
static constexpr float ARCH_PLAYER_FRICTION = 0.05f;
static constexpr int   FLASH_COOLDOWN   = 40;
static constexpr float ARENA_HALF       = 50.0f;

static constexpr float TRACKER_ACTIVATE_DIST = 15.0f;
static constexpr float TRACKER_GROWL_DIST    = 35.0f;
static constexpr float TRACKER_SCARE_DIST    =  3.5f;
static constexpr float TRACKER_SPEED         =  1.0f;

class ArchitectScript : public Script {
public:
    void init(ScriptContext& ctx) override;
    void update(ScriptContext& ctx, float dt) override;

private:
    SceneObject* playerSphere_  = nullptr;
    SceneObject* trackerSphere_ = nullptr;

    Source* jumpSrc_     = nullptr;
    Source* growlSrc_    = nullptr;
    Source* staticSrc_   = nullptr;
    Source* clickOnSrc_  = nullptr;
    Source* clickOffSrc_ = nullptr;
    float   jumpPitch_   = 1.0f;
    float   jumpGain_    = 1.0f;

    bool  flashlightOn_  = false;
    bool  lmbWasDown_    = false;
    int   flashCooldown_ = 0;
    int   flashlightIdx_ = -1;

    int frameCounter_ = 0;

    TextAtlas* atlas_     = nullptr;
    TextBox*   hintLabel_ = nullptr;
};

void ArchitectScript::init(ScriptContext& ctx) {
    // Floor (StaticAABB, top at y=0)
    ctx.world->addStaticAABB({0.0f, -0.5f, 0.0f}, {ARENA_HALF, 0.5f, ARENA_HALF});
    

    // Textured brick floor (visual)
    auto* floorObj = ctx.world->addRenderObject(Primitives::plane(ARENA_HALF));
    if (auto* m = floorObj->getMesh()) {
        m->texture     = ctx.assets->loadTexture("textures/brick.jpg");
        m->state       = 1;
        m->modelMatrix = math::Mat4{};
        m->transformReady = true;
    }

    // No ambient lights — flashlight is the only illumination

    // Player sphere — physics-driven, sleeping disabled, low friction
    playerSphere_ = ctx.world->addSphere(1.0f, ARCH_PLAYER_R, {0.0f, 2.0f, 25.0f});
    {
        auto* h = playerSphere_->getHull();
        h->disableSleeping();
        h->friction      = ARCH_PLAYER_FRICTION;
        h->linearDamping = 0.0f;
    }

    // Tracker sphere (horror enemy)
    trackerSphere_ = ctx.world->addSphere(2.0f, 0.8f, {0.0f, 2.0f, -40.0f});
    {
        auto* h = trackerSphere_->getHull();
        h->disableSleeping();   // tracker also driven by script — same problem applies
    }
    auto* tm = ctx.world->attachMesh(trackerSphere_, Primitives::icosphere(0.8f));
    if (tm) { tm->color[0] = 0.6f; tm->color[1] = 0.05f; tm->color[2] = 0.05f; }

    // Camera
    ctx.camera->setPosition({0.0f, 2.0f + ARCH_CAMERA_LIFT, 25.0f});
    ctx.camera->setRotation({0.0f, 0.0f, 0.0f});

    // Audio
    {
        auto* buf = ctx.audio->loadSound("audios/fnaf4-foxy-closet-jumpscare.ogg");
        jumpSrc_ = ctx.audio->createSource(buf);
        jumpSrc_->setPosition({0.0f, 1.0f, -45.0f});
        jumpSrc_->setReferenceDistance(20.0f);
        jumpSrc_->setGain(jumpGain_);
    }
    {
        auto* buf = ctx.audio->loadSound("audios/glitchy-static.ogg");
        staticSrc_ = ctx.audio->createSource(buf);
        staticSrc_->setGain(0.01f);
        staticSrc_->enableLooping(true);
        staticSrc_->play();
    }
    {
        auto* buf = ctx.audio->loadSound("audios/growling-ambience.ogg");
        growlSrc_ = ctx.audio->createSource(buf);
        growlSrc_->setGain(1.0f);
        growlSrc_->setReferenceDistance(15.0f);
    }
    {
        auto* buf = ctx.audio->loadSound("audios/flashlight-click-on.ogg");
        clickOnSrc_ = ctx.audio->createSource(buf);
    }
    {
        auto* buf = ctx.audio->loadSound("audios/flashlight-click-off.ogg");
        clickOffSrc_ = ctx.audio->createSource(buf);
    }

    if (ctx.ui) {
        
        atlas_ = ctx.ui->loadAtlas("fonts/monaco.ttf", 128);
        auto* bg = ctx.ui->addBackground({0.01f,0.95f},{0.17f,0.995f},{0,0,0,0.5f},0);
        hintLabel_ = ctx.ui->addTextBox(bg, atlas_,
            "[LMB] Flashlight", 1, 22);
    }
}

void ArchitectScript::update(ScriptContext& ctx, float dt) {
    ++frameCounter_;

    auto* hull = playerSphere_->getHull();
    math::Vec3 spherePos = hull->getPosition();

    // ---- Mouselook ----
    auto delta = ctx.input->getMouseDelta();
    math::Vec3 rot = ctx.camera->getRotation();
    rot.y += static_cast<float>(delta.x) * -ARCH_SENSITIVITY;
    rot.x = math::clamp(rot.x + static_cast<float>(delta.y) * -ARCH_SENSITIVITY,
                        -1.5607963f, 1.5607963f);
    ctx.camera->setRotation(rot);

    // ---- WASD direction ----
    float yaw = rot.y;
    float sy = std::sin(yaw), cy = std::cos(yaw);
    math::Vec3 fwd   = {-sy, 0.0f, -cy};
    math::Vec3 right = { cy, 0.0f, -sy};
    math::Vec3 dir   = {};
    if (ctx.input->isKeyDown(GLFW_KEY_W)) dir += fwd;
    if (ctx.input->isKeyDown(GLFW_KEY_S)) dir -= fwd;
    if (ctx.input->isKeyDown(GLFW_KEY_D)) dir += right;
    if (ctx.input->isKeyDown(GLFW_KEY_A)) dir -= right;

    float speed = ARCH_BASE_SPEED * (ctx.input->isKeyDown(GLFW_KEY_LEFT_SHIFT) ? 1.8f : 1.0f);

    // ---- Ground check: sphere sitting on floor at y ≈ ARCH_PLAYER_R ----
    bool grounded = spherePos.y < (ARCH_PLAYER_R + 0.15f);

    // ---- Apply velocity ----
    math::Vec3 vel = hull->getVelocity();
    vel.x *= ARCH_HORIZ_DAMP;
    vel.z *= ARCH_HORIZ_DAMP;
    if (dir.length() > 0.01f)
        vel += dir.normalize() * speed;
    if (ctx.input->isKeyDown(GLFW_KEY_SPACE) && grounded && vel.y < 0.1f)
        vel.y = ARCH_JUMP_VEL;
    hull->setVelocity(vel);

    // ---- Camera follows sphere ----
    ctx.camera->setPosition(spherePos + math::Vec3{0.0f, ARCH_CAMERA_LIFT, 0.0f});

    // ---- MB4/MB5 gain control (every 4 frames) ----
    if (frameCounter_ % 4 == 0 && jumpSrc_) {
        bool changed = false;
        if (ctx.input->isForwardMBDown())  { jumpGain_ += 0.05f; changed = true; }
        if (ctx.input->isBackwardMBDown()) { jumpGain_ -= 0.05f; changed = true; }
        if (changed) {
            jumpGain_ = math::clamp(jumpGain_, 0.0f, 3.0f);
            jumpSrc_->setGain(jumpGain_);
        }
    }

    // ---- Flashlight toggle (LMB, cooldown) ----
    bool lmbNow = ctx.input->isLMBDown();
    if (lmbNow && !lmbWasDown_ && flashCooldown_ == 0) {
        flashlightOn_ = !flashlightOn_;
        flashCooldown_ = FLASH_COOLDOWN;
        if (flashlightOn_) {
            FlashLight fl{};
            fl.innerConeAngle = 0.219f;
            fl.outerConeAngle = 0.99f;
            fl.color[0] = 1.0f; fl.color[1] = 1.0f; fl.color[2] = 0.98f;
            fl.intensity = 2.0f;
            fl.constant  = 0.0f;
            fl.linear    = 0.05f;
            fl.quadratic = 0.005f;
            ctx.world->addLight(fl);
            flashlightIdx_ = static_cast<int>(ctx.world->getLights().size()) - 1;
            if (clickOnSrc_) clickOnSrc_->play();
        } else {
            if (flashlightIdx_ >= 0) {
                ctx.world->removeLight(flashlightIdx_);
                flashlightIdx_ = -1;
            }
            if (clickOffSrc_) clickOffSrc_->play();
        }
    }
    if (flashCooldown_ > 0) --flashCooldown_;
    lmbWasDown_ = lmbNow;

    // ---- Tracker behaviour ----
    if (trackerSphere_) {
        auto* th = trackerSphere_->getHull();
        math::Vec3 tPos = th->getPosition();
        math::Vec3 tVel = th->getVelocity();
        math::Vec3 diff = spherePos - tPos;
        float dist = diff.length();

        if (flashlightOn_ || dist < TRACKER_ACTIVATE_DIST) {
            if (dist < TRACKER_SCARE_DIST && jumpSrc_ && !jumpSrc_->isPlaying())
                jumpSrc_->play();

            if (dist > 0.01f)
                th->setVelocity(tVel + diff.normalize() * TRACKER_SPEED);

            if (jumpSrc_) {
                jumpSrc_->setPosition(tPos);
                jumpSrc_->setVelocity(tVel);
            }

            math::Vec3 tv = th->getVelocity();
            tv.x *= 0.9f; tv.z *= 0.9f;
            th->setVelocity(tv);
        }

        if (growlSrc_) {
            if (dist < TRACKER_GROWL_DIST && !growlSrc_->isPlaying())
                growlSrc_->play();
            else if (dist >= TRACKER_GROWL_DIST)
                growlSrc_->pause();
            growlSrc_->setPosition(tPos);
            growlSrc_->setVelocity(tVel);
        }
    }
}

YOPE_REGISTER_SCRIPT(ArchitectScript);
