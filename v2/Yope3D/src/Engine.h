#pragma once

#include <memory>
#include "platform/Window.h"
#include "platform/Input.h"
#include "gpu/GpuDevice.h"
#include "rendering/Renderer.h"
#include "rendering/Camera.h"
#include "world/World.h"
#include "assets/AssetManager.h"

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

    double lastTime = 0.0;

    physics::CSphere* playerSphere = nullptr;

    int  sceneIndex  = 12;  // which scene is loaded (default: stress test)
    int  spawnType   = 0;   // 0=Sphere 1=AABB 2=OBB
    static constexpr int SCENE_COUNT = 13;

    bool hasRendered   = false;  // guards waitIdle on first loadScene call
    bool rightWasDown  = false;
    bool leftWasDown   = false;
    bool upWasDown     = false;
    bool downWasDown   = false;
    float spawnCooldown = 0.0f;

    bool init();
    void update();
    void render();
    void cleanup();

private:
    void loadScene(int index);
    void loadPyramid(int baseN);
    void loadSpringCloth(int variant, int shapeType);  // variant: 0=top-row 1=4-corners 2=2-top-corners  shapeType: 0=Sphere 1=AABB 2=OBB
    void loadStressTest();
    void spawnObject();
    void addFloorMesh(float halfW, float halfD);
};
