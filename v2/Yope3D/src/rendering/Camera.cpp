#include "Camera.h"
#include "platform/Input.h"
#include <GLFW/glfw3.h>
#include <cmath>

using namespace math;

Camera::Camera(int width, int height, float fov)
    : fov(fov),
      windowWidth(width), windowHeight(height),
      aspectRatio(static_cast<float>(width) / static_cast<float>(height)),
      position{0.0f, 0.0f, 2.0f}  // start 2 units back so the default quad is visible
{}

void Camera::WindowChanged(int width, int height) {
    windowWidth  = width;
    windowHeight = height;
    aspectRatio  = static_cast<float>(width) / static_cast<float>(height);
}

void Camera::setFOV(float f) {
    fov = f;
}

void Camera::update(const Input& input, float dt) {
    MouseDelta delta = input.getMouseDelta();
    rotation.y += static_cast<float>(delta.x) * -SENSITIVITY; //typical control scheme is backwards for x
    rotation.x += static_cast<float>(delta.y) * -SENSITIVITY;

    // Clamp pitch to ~89° to prevent the camera from flipping at the poles.
    static constexpr float PITCH_LIMIT = 1.5607963f;
    if (rotation.x >  PITCH_LIMIT) rotation.x =  PITCH_LIMIT;
    if (rotation.x < -PITCH_LIMIT) rotation.x = -PITCH_LIMIT;

    // Derive movement axes from yaw only — pitch does not affect lateral movement.
    float sy = std::sin(rotation.y);
    float cy = std::cos(rotation.y);
    Vec3 forward = {-sy, 0.0f, -cy};
    Vec3 right   = { cy, 0.0f, -sy};

    Vec3 move{};
    if (input.isKeyDown(GLFW_KEY_W))           move += forward;
    if (input.isKeyDown(GLFW_KEY_S))           move -= forward;
    if (input.isKeyDown(GLFW_KEY_D))           move += right;
    if (input.isKeyDown(GLFW_KEY_A))           move -= right;
    if (input.isKeyDown(GLFW_KEY_SPACE))       move.y += 1.0f;
    if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT))  move.y -= 1.0f;

    if (move.length() > 0.0f)
        position += move.normalize() * (SPEED * dt);
}

Mat4 Camera::genViewMatrix() const {
    return Mat4::view(position, rotation);
}

Mat4 Camera::genProjectionMatrix() const {
    return Mat4::perspective(fov, aspectRatio, NEAR_PLANE, FAR_PLANE);
}

Vec3 Camera::getForward() const {
    // Compute forward direction from yaw (rotation.y) and pitch (rotation.x).
    // This is the 3D direction the camera is looking along (camera -Z axis in world space).
    float sy = std::sin(rotation.y);
    float cy = std::cos(rotation.y);
    float sp = std::sin(rotation.x);
    float cp = std::cos(rotation.x);

    // Forward = (-sin(yaw)*cos(pitch), -sin(pitch), -cos(yaw)*cos(pitch))
    return Vec3{-sy * cp, -sp, -cy * cp};
}
