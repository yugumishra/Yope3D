#include "Input.h"

// ---------------------------------------------------------------------------
// Frame boundary
// ---------------------------------------------------------------------------

void Input::beginFrame() {
    //place current into previous (current will get wiped)
    prevMouseDelta = mouseDelta;

    // Clear one-shot transition flags from the previous frame's callbacks.
    keyJustPressed.fill(false);
    keyJustReleased.fill(false);

    // Reset per-frame accumulations.
    mouseDelta = {};
    scrollX    = 0.0;
    scrollY    = 0.0;
}

// ---------------------------------------------------------------------------
// Key queries
// ---------------------------------------------------------------------------

bool Input::isKeyDown(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return keyState[static_cast<std::size_t>(key)];
}

bool Input::isKeyPressed(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return keyJustPressed[static_cast<std::size_t>(key)];
}

bool Input::isKeyReleased(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return keyJustReleased[static_cast<std::size_t>(key)];
}

// ---------------------------------------------------------------------------
// Callback sinks (called by Window's static GLFW callbacks)
// ---------------------------------------------------------------------------

void Input::onKey(int key, int action) {
    if (key < 0 || key > GLFW_KEY_LAST) return;
    auto k = static_cast<std::size_t>(key);

    if (action == GLFW_PRESS) {
        // GLFW fires PRESS once, then REPEAT for held keys.
        // Treat first press only as 'just pressed'.
        if (!keyState[k]) {
            keyJustPressed[k] = true;
        }
        keyState[k] = true;
    } else if (action == GLFW_RELEASE) {
        keyState[k]       = false;
        keyJustReleased[k] = true;
    }
    // GLFW_REPEAT: keyState is already true; no flag changes needed.
}

void Input::onMouseButton(int button, int action) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    mouseButtons[static_cast<std::size_t>(button)] = (action == GLFW_PRESS);
}

void Input::onMouseMove(double dx, double dy) {
    // Window computes (current - last) before calling this, so we just
    // accumulate.  Multiple callbacks in one pollEvents() call are additive.
    mouseDelta.x += dx;
    mouseDelta.y += dy;
}

void Input::onScroll(double xOffset, double yOffset) {
    scrollX += xOffset;
    scrollY += yOffset;
}