#include "Camera.h"
#include <cmath>

using namespace math;

Camera::Camera(int width, int height, float fov)
    : fov(fov),
      windowWidth(width), windowHeight(height),
      aspectRatio(static_cast<float>(width) / static_cast<float>(height)),
      position{0.0f, 5.0f, 8.0f}  // start 2 units back so the default quad is visible
{}

void Camera::WindowChanged(int width, int height) {
    windowWidth  = width;
    windowHeight = height;
    aspectRatio  = static_cast<float>(width) / static_cast<float>(height);
}

void Camera::setFOV(float f) {
    fov = f;
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

    // Forward = (-sin(yaw)*cos(pitch), sin(pitch), -cos(yaw)*cos(pitch))
    return Vec3{-sy * cp, sp, -cy * cp};
}
