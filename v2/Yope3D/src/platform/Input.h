#pragma once

#include <GLFW/glfw3.h>
#include <array>

// Per-frame mouse movement accumulated from the cursor callback.
struct MouseDelta {
    double x = 0.0;
    double y = 0.0;
};

// ---------------------------------------------------------------------------
// Input
//
// Polling-based.  Window's static GLFW callbacks write into this class;
// game code reads from it.
//
// Frame ordering note
// -------------------
// One-shot queries (isKeyPressed / isKeyReleased) work correctly only when
// pollEvents() fires BEFORE beginFrame() in the main loop:
//
//   engine.window->pollEvents();   // callbacks update keyState / justPressed
//   engine.input->beginFrame();    // snapshot prev, clear transients
//   engine.update();               // game reads isKeyPressed here — correct
//   engine.render();
//   engine.window->swapBuffers();
//
// If pollEvents() comes AFTER beginFrame() (as in the provided main.cpp),
// just-pressed flags are cleared before the game ever sees them.
// isKeyDown() is always correct regardless of ordering.
// ---------------------------------------------------------------------------

class Input {
public:
    // Call once at the start of each frame (ideally after pollEvents).
    // Clears per-frame transient state: just-pressed/released flags,
    // mouse delta, and scroll accumulator.
    void beginFrame();

    // ------------------------------------------------------------------
    // Keys
    // ------------------------------------------------------------------

    // True every frame the key is held down.
    bool isKeyDown(int key) const;

    // True only on the first frame a key transitions to pressed.
    // Requires pollEvents() before beginFrame() — see note above.
    bool isKeyPressed(int key) const;

    // True only on the first frame a key transitions to released.
    bool isKeyReleased(int key) const;

    // ------------------------------------------------------------------
    // Mouse buttons
    // ------------------------------------------------------------------

    bool isLMBDown()      const { return mouseButtons[GLFW_MOUSE_BUTTON_LEFT];   }
    bool isRMBDown()      const { return mouseButtons[GLFW_MOUSE_BUTTON_RIGHT];  }
    bool isMMBDown()      const { return mouseButtons[GLFW_MOUSE_BUTTON_MIDDLE]; }
    bool isForwardMBDown()  const { return mouseButtons[GLFW_MOUSE_BUTTON_5];   }
    bool isBackwardMBDown() const { return mouseButtons[GLFW_MOUSE_BUTTON_4];   }

    // ------------------------------------------------------------------
    // Mouse movement  (delta since last beginFrame)
    // ------------------------------------------------------------------

    MouseDelta getMouseDelta() const { return prevMouseDelta; }

    // ------------------------------------------------------------------
    // Scroll  (accumulated since last beginFrame)
    // ------------------------------------------------------------------

    double getScrollX() const { return scrollX; }
    double getScrollY() const { return scrollY; }

    // ------------------------------------------------------------------
    // Callback sinks — called by Window's static GLFW callbacks only.
    // ------------------------------------------------------------------

    void onKey        (int key,    int action);
    void onMouseButton(int button, int action);
    void onMouseMove  (double dx,  double dy);   // receives DELTA, not absolute pos
    void onScroll     (double xOffset, double yOffset);

private:
    // Persistent key state.
    std::array<bool, GLFW_KEY_LAST + 1> keyState{};

    // One-shot transition flags — set by onKey, cleared by beginFrame.
    std::array<bool, GLFW_KEY_LAST + 1> keyJustPressed{};
    std::array<bool, GLFW_KEY_LAST + 1> keyJustReleased{};

    // Mouse button state  (GLFW_MOUSE_BUTTON_LAST is typically 7).
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouseButtons{};

    // Per-frame delta — accumulated by onMouseMove, cleared by beginFrame.
    MouseDelta mouseDelta;
    MouseDelta prevMouseDelta;

    // Accumulated scroll since last beginFrame.
    double scrollX = 0.0;
    double scrollY = 0.0;
};