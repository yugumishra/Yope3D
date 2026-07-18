#pragma once
#include "Camera.h"

class Input;

class CameraController {
public:
    virtual void update(Camera& camera, const Input& input, float dt) = 0;
    virtual ~CameraController() = default;
};

// Standard FPS-style controller: WASD + Space/Shift + mouse look.
// Sensitivity and speed are tunable per-instance.
class FpsCameraController : public CameraController {
public:
    float sensitivity = 0.002f;  // radians per pixel
    float speed       = 5.0f;   // world units per second

    void update(Camera& camera, const Input& input, float dt) override;
};

// Stub for future orbit/pan-style control (point-and-click games, editor orbit).
class PointClickController : public CameraController {
public:
    void update(Camera& camera, const Input& input, float dt) override {}
};
