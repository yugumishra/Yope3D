#pragma once

#include <memory>
#include "platform/Window.h"
#include "platform/Input.h"
#include "gpu/GpuDevice.h"
#include "rendering/Renderer.h"
#include "rendering/Camera.h"
#include "world/World.h"
#include "assets/AssetManager.h"
#include "audio/AudioSystem.h"
#include "audio/Listener.h"

// ---------------------------------------------------------------------------
// Engine — top-level context.
//
// Scenes (LEFT / RIGHT arrow to cycle):
//   0  — Pyramid small    (base 4)
//   1  — Pyramid medium   (base 7)
//   2  — Pyramid large    (base 10)
//   3  — Spring [Sphere]  — Top Row Fixed
//   4  — Spring [AABB]    — Top Row Fixed
//   5  — Spring [OBB]     — Top Row Fixed
//   6  — Spring [Sphere]  — 4 Corners (Catenary)
//   7  — Spring [AABB]    — 4 Corners (Catenary)
//   8  — Spring [OBB]     — 4 Corners (Catenary)
//   9  — Spring [Sphere]  — 2 Top Corners
//   10 — Spring [AABB]    — 2 Top Corners
//   11 — Spring [OBB]     — 2 Top Corners
//   12 — Stress test (wide arena)
//   13 — Doppler test (sphere drops past camera)
//
// Spawn type (UP / DOWN arrow): Sphere → AABB → OBB
// Spawn object: hold LMB
// ---------------------------------------------------------------------------

struct Engine {
    std::unique_ptr<Window>        window;
    std::unique_ptr<Input>         input;
    std::unique_ptr<GpuDevice>     gpu;
    std::unique_ptr<Renderer>      renderer;
    std::unique_ptr<Camera>        camera;
    std::unique_ptr<World>         world;
    std::unique_ptr<AssetManager>  assets;
    std::unique_ptr<AudioSystem>   audio;

    double lastTime          = 0.0;
    float  physicsAccumulator_ = 0.0f;

    physics::CSphere* playerSphere    = nullptr;
    Source*           ambientEmitter  = nullptr;  // non-owning; audio owns it
    Source*           dopplerSource_  = nullptr;  // non-owning; tracks dopplerHull_
    physics::Hull*    dopplerHull_    = nullptr;  // non-owning; owned by World

    int  sceneIndex  = 12;  // which scene is loaded (default: stress test)
    int  spawnType   = 0;   // 0=Sphere 1=AABB 2=OBB
    static constexpr int SCENE_COUNT = 14;

    bool hasRendered   = false;  // guards waitIdle on first loadScene call
    bool rightWasDown  = false;
    bool leftWasDown   = false;
    bool upWasDown     = false;
    bool downWasDown   = false;
    bool pWasDown      = false;
    bool mWasDown      = false;
    bool audioPaused   = false;
    float spawnCooldown = 0.0f;

    float fpsAccum  = 0.0f;
    int   fpsFrames = 0;
    int   displayFps = 0;

    bool init();
    void update();
    void render();
    void cleanup();

private:
    void loadScene(int index);
    void loadPyramid(int baseN);
    void loadSpringCloth(int variant, int shapeType);  // variant: 0=top-row 1=4-corners 2=2-top-corners  shapeType: 0=Sphere 1=AABB 2=OBB
    void loadStressTest();
    void loadDopplerTest();
    void spawnObject();
    void addFloorMesh(float halfW, float halfD);
};
