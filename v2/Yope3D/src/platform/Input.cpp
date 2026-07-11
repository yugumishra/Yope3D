#include "Input.h"

// ---------------------------------------------------------------------------
// Frame boundary
// ---------------------------------------------------------------------------

void Input::beginFrame() {
    //place current into previous (current will get wiped)
    prevMouseDelta = mouseDelta;

    // Snapshot the one-shot transitions accumulated during the just-completed
    // pollEvents() into the frame-stable buffers the game reads this frame, then
    // reset the live accumulators. (Clearing the read buffers directly here would
    // wipe a press that just fired during this poll — see Input.h.)
    keyJustPressed    = keyPressedLive;    keyPressedLive.fill(false);
    keyJustReleased   = keyReleasedLive;   keyReleasedLive.fill(false);
    mouseJustPressed  = mousePressedLive;  mousePressedLive.fill(false);
    mouseJustReleased = mouseReleasedLive; mouseReleasedLive.fill(false);

    // Reset per-frame accumulations (snapshotting scroll like mouseDelta).
    mouseDelta  = {};
    prevScrollX = scrollX;  scrollX = 0.0;
    prevScrollY = scrollY;  scrollY = 0.0;

    // Same live/snapshot scheme as scroll: hand off what accumulated during the
    // poll that just finished, then clear the accumulator for the next one.
    prevTypedChars = std::move(typedCharsLive);
    typedCharsLive.clear();
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
            keyPressedLive[k] = true;
        }
        keyState[k] = true;
    } else if (action == GLFW_RELEASE) {
        keyState[k]        = false;
        keyReleasedLive[k] = true;
    }
    // GLFW_REPEAT: keyState is already true; no flag changes needed.
}

bool Input::isMousePressed(int button) const {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return mouseJustPressed[static_cast<std::size_t>(button)];
}

bool Input::isMouseReleased(int button) const {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return mouseJustReleased[static_cast<std::size_t>(button)];
}

void Input::onMouseButton(int button, int action) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    auto b = static_cast<std::size_t>(button);
    if (action == GLFW_PRESS) {
        if (!mouseButtons[b]) mousePressedLive[b] = true;
        mouseButtons[b] = true;
    } else if (action == GLFW_RELEASE) {
        mouseButtons[b]        = false;
        mouseReleasedLive[b]   = true;
    }
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

void Input::onCursorPos(double x, double y) {
    cursorX = x;
    cursorY = y;
}

void Input::onChar(unsigned int codepoint) {
    typedCharsLive.push_back(codepoint);
}