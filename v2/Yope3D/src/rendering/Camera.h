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

    math::Mat4 genViewMatrix()       const;
    math::Mat4 genProjectionMatrix() const;

    math::Vec3 getPosition() const { return position; }
    math::Vec3 getRotation() const { return rotation; }
    math::Vec3 getForward()  const;  // Returns the camera's forward direction in world space

    // Unproject a screen pixel (top-left origin, +Y down) into a world-space ray.
    // outOrigin is the camera eye; outDir is unit length. Feed into KinematicQuery::raycast
    // for click-to-pick / shoot-from-cursor. Uses the camera's current window dimensions.
    void screenToRay(float px, float py, math::Vec3& outOrigin, math::Vec3& outDir) const;

    float getFov()         const { return fov; }
    float getAspectRatio() const { return aspectRatio; }

    void setPosition(const math::Vec3& p) { position = p; }
    void setRotation(const math::Vec3& r) { rotation = r; }

    // Aim the camera at a world point (sets pitch/yaw so getForward() points at `target`).
    void lookAt(const math::Vec3& target);

    // ---- Clip planes -------------------------------------------------------
    // Depth precision is dominated by the NEAR plane, not the far one: resolvable
    // depth at distance z runs about z^2 * (far - near) / (near * far * 2^24), so
    // the defaults (0.1 / 2000) spend nearly the whole buffer before the content
    // starts. Scenes whose geometry sits far from the eye — a long-lens framing,
    // an orbit view, anything measured in hundreds of units — gain orders of
    // magnitude by pulling the near plane out to just short of their nearest
    // geometry. At z = 120 the defaults resolve ~0.0086; 100 / 200 resolves ~1e-5.
    //
    // Bad input (non-positive, or near >= far) is ignored rather than applied, so
    // a script cannot produce a degenerate projection matrix.
    void  setClipPlanes(float nearPlane, float farPlane);
    float getNearPlane() const { return nearPlane; }
    float getFarPlane()  const { return farPlane; }

private:
    static constexpr float DEFAULT_NEAR_PLANE = 0.1f;
    static constexpr float DEFAULT_FAR_PLANE  = 2000.0f;

    float nearPlane = DEFAULT_NEAR_PLANE;
    float farPlane  = DEFAULT_FAR_PLANE;

    float fov;
    int   windowWidth;
    int   windowHeight;
    float aspectRatio;

    math::Vec3 position;
    math::Vec3 rotation;  // pitch (x), yaw (y), roll (z) — all radians
};
