#include "CameraController.h"
#include "platform/Input.h"
#include <GLFW/glfw3.h>
#include <cmath>

void FpsCameraController::update(Camera& camera, const Input& input, float dt) {
    using namespace math;

    MouseDelta delta = input.getMouseDelta();
    Vec3 rot = camera.getRotation();
    rot.y += static_cast<float>(delta.x) * -sensitivity;
    rot.x += static_cast<float>(delta.y) * -sensitivity;

    static constexpr float PITCH_LIMIT = 1.5607963f;
    if (rot.x >  PITCH_LIMIT) rot.x =  PITCH_LIMIT;
    if (rot.x < -PITCH_LIMIT) rot.x = -PITCH_LIMIT;
    camera.setRotation(rot);

    float sy = std::sin(rot.y);
    float cy = std::cos(rot.y);
    Vec3 forward = {-sy, 0.0f, -cy};
    Vec3 right   = { cy, 0.0f, -sy};

    Vec3 move{};
    if (input.isKeyDown(GLFW_KEY_W))          move += forward;
    if (input.isKeyDown(GLFW_KEY_S))          move -= forward;
    if (input.isKeyDown(GLFW_KEY_D))          move += right;
    if (input.isKeyDown(GLFW_KEY_A))          move -= right;
    if (input.isKeyDown(GLFW_KEY_SPACE))      move.y += 1.0f;
    if (input.isKeyDown(GLFW_KEY_LEFT_SHIFT)) move.y -= 1.0f;

    if (move.length() > 0.0f)
        camera.setPosition(camera.getPosition() + move.normalize() * (speed * dt));
}
