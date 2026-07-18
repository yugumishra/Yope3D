#include "Camera.h"
#include "math/Vec4.h"
#include <cmath>
#include <algorithm>

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

void Camera::lookAt(const Vec3& target) {
    Vec3 d = (target - position).normalize();
    // Invert getForward(): forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)).
    float pitch = std::asin(std::max(-1.0f, std::min(1.0f, d.y)));
    float yaw   = std::atan2(-d.x, -d.z);
    rotation = {pitch, yaw, 0.0f};
}

void Camera::screenToRay(float px, float py, Vec3& outOrigin, Vec3& outDir) const {
    // Pixel → NDC. Projection uses OpenGL-style depth (z in [-1,1]) with the
    // Vulkan Y-flip baked into the matrix, so px/py (top-left, +Y down) map
    // straight to ndc with no extra sign change; near plane is z=-1, far z=+1.
    float ndcX = 2.0f * px / static_cast<float>(windowWidth)  - 1.0f;
    float ndcY = 2.0f * py / static_cast<float>(windowHeight) - 1.0f;

    Mat4 invVP = (genProjectionMatrix() * genViewMatrix()).inverse();

    Vec4 nearH = invVP * Vec4{ndcX, ndcY, -1.0f, 1.0f};
    Vec4 farH  = invVP * Vec4{ndcX, ndcY,  1.0f, 1.0f};

    Vec3 nearW{nearH.x / nearH.w, nearH.y / nearH.w, nearH.z / nearH.w};
    Vec3 farW { farH.x  / farH.w,  farH.y  / farH.w,  farH.z  / farH.w};

    outOrigin = position;
    outDir    = (farW - nearW).normalize();
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
