#pragma once
#include "math/Vec3.h"
#include "math/Mat4.h"

class Input;

// ---------------------------------------------------------------------------
// Camera
//
// FPS-style camera: Euler angles (pitch x, yaw y, no roll), WASD movement
// on the XZ plane, Space/LeftShift for vertical movement.
// Mouse delta from Input drives rotation every frame via update().
// genViewMatrix() / genProjectionMatrix() return column-major Mat4s ready
// to upload directly into the GlobalUBO.
// ---------------------------------------------------------------------------

class Camera {
public:
    // fov is in radians.  width/height set the initial aspect ratio.
    Camera(int width, int height, float fov);

    // Call when the window is resized to keep the projection correct.
    void WindowChanged(int width, int height);

    // fov in radians.
    void setFOV(float fov);

    // Consume mouse delta + WASD state from input, advance by dt seconds.
    void update(const Input& input, float dt);

    math::Mat4 genViewMatrix()       const;
    math::Mat4 genProjectionMatrix() const;

    math::Vec3 getPosition() const { return position; }

private:
    static constexpr float SENSITIVITY = 0.002f;   // radians per pixel
    static constexpr float SPEED       = 5.0f;     // world units per second
    static constexpr float NEAR_PLANE  = 0.1f;
    static constexpr float FAR_PLANE   = 2000.0f;

    float fov;
    int   windowWidth;
    int   windowHeight;
    float aspectRatio;

    math::Vec3 position;
    math::Vec3 rotation;  // pitch (x), yaw (y), roll (z) — all radians
    math::Vec3 velocity;
};
