#pragma once

#include <GLFW/glfw3.h>
#include <array>
#include <string>
#include <vector>

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

    // Localized name of the physical key `key` under the CURRENT OS keyboard
    // layout (thin glfwGetKeyName wrapper), e.g. "," for KEY_W on Dvorak, "w"
    // on QWERTY/Colemak — for rendering an accurate "Press ___" label in a
    // rebind UI. Contrast with `key` itself, which is a layout-independent
    // physical-position token (gameplay logic should keep using that
    // directly; only *display* needs this). Returns "" for non-printable
    // keys (arrows, F-keys, modifiers, ...) — GLFW has no name for those, so
    // callers need their own fallback label table.
    std::string getKeyName(int key) const {
        const char* name = glfwGetKeyName(key, 0);
        return name ? name : "";
    }

    // ------------------------------------------------------------------
    // Mouse buttons
    // ------------------------------------------------------------------

    bool isLMBDown()      const { return mouseButtons[GLFW_MOUSE_BUTTON_LEFT];   }
    bool isRMBDown()      const { return mouseButtons[GLFW_MOUSE_BUTTON_RIGHT];  }
    bool isMMBDown()      const { return mouseButtons[GLFW_MOUSE_BUTTON_MIDDLE]; }
    bool isForwardMBDown()  const { return mouseButtons[GLFW_MOUSE_BUTTON_5];   }
    bool isBackwardMBDown() const { return mouseButtons[GLFW_MOUSE_BUTTON_4];   }

    // Generic held-state query by raw button index — lets callers (e.g. a
    // script-side action-map layer) poll an arbitrary bound button without
    // dispatching through the five named is_*MBDown() helpers above.
    bool isMouseDown(int button) const {
        return button >= 0 && button < static_cast<int>(mouseButtons.size()) && mouseButtons[button];
    }

    // One-shot button-transition queries (true only on the frame the button
    // changes state). Requires pollEvents() before beginFrame() — see note above.
    bool isMousePressed (int button) const;
    bool isMouseReleased(int button) const;

    // ------------------------------------------------------------------
    // Mouse movement  (delta since last beginFrame)
    // ------------------------------------------------------------------

    MouseDelta getMouseDelta() const { return prevMouseDelta; }

    // ------------------------------------------------------------------
    // Scroll  (accumulated since last beginFrame)
    // ------------------------------------------------------------------

    // Return the snapshot taken in beginFrame (mirrors getMouseDelta): the live
    // accumulator is zeroed each frame, so reading it directly would always be 0.
    double getScrollX() const { return prevScrollX; }
    double getScrollY() const { return prevScrollY; }

    // ------------------------------------------------------------------
    // Cursor position (absolute, screen pixels)
    // ------------------------------------------------------------------

    double getCursorX() const { return cursorX; }
    double getCursorY() const { return cursorY; }

    // ------------------------------------------------------------------
    // Typed text (codepoints accumulated since last beginFrame)
    // ------------------------------------------------------------------

    // UTF-32 codepoints typed this frame, in order (GLFW char callback — key
    // events carry no shift/layout/IME info, this does). Cleared each beginFrame.
    const std::vector<unsigned int>& getTypedChars() const { return prevTypedChars; }

    // ------------------------------------------------------------------
    // Callback sinks — called by Window's static GLFW callbacks only.
    // ------------------------------------------------------------------

    void onKey        (int key,    int action);
    void onMouseButton(int button, int action);
    void onMouseMove  (double dx,  double dy);   // receives DELTA, not absolute pos
    void onCursorPos  (double x,   double y);    // receives ABSOLUTE pos
    void onScroll     (double xOffset, double yOffset);
    void onChar        (unsigned int codepoint);

private:
    // Persistent key state.
    std::array<bool, GLFW_KEY_LAST + 1> keyState{};

    // One-shot transition flags. The loop order is pollEvents()→beginFrame()→update(),
    // so callbacks fire (writing the *Live buffers) BEFORE beginFrame. beginFrame
    // snapshots *Live into the frame-stable buffers the game reads this frame, then
    // resets *Live — mirroring the mouseDelta/prevMouseDelta snapshot. Clearing the
    // read buffers directly in beginFrame would wipe the transition before update sees it.
    std::array<bool, GLFW_KEY_LAST + 1> keyJustPressed{};    // read by isKeyPressed
    std::array<bool, GLFW_KEY_LAST + 1> keyJustReleased{};   // read by isKeyReleased
    std::array<bool, GLFW_KEY_LAST + 1> keyPressedLive{};    // written by onKey during poll
    std::array<bool, GLFW_KEY_LAST + 1> keyReleasedLive{};

    // Mouse button state  (GLFW_MOUSE_BUTTON_LAST is typically 7).
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouseButtons{};

    // One-shot mouse-button transitions (same snapshot scheme as the key flags above).
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouseJustPressed{};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouseJustReleased{};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mousePressedLive{};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouseReleasedLive{};

    // Per-frame delta — accumulated by onMouseMove, cleared by beginFrame.
    MouseDelta mouseDelta;
    MouseDelta prevMouseDelta;

    // Scroll: live accumulator (written by onScroll during poll) + frame snapshot
    // (read by getScrollX/Y), same pattern as mouseDelta/prevMouseDelta.
    double scrollX = 0.0;
    double scrollY = 0.0;
    double prevScrollX = 0.0;
    double prevScrollY = 0.0;

    // Absolute cursor position (screen pixels) — written by onCursorPos,
    // tracked independent of paused/cursor-mode so UI hit-testing works
    // whenever the OS cursor is visible.
    double cursorX = 0.0;
    double cursorY = 0.0;

    // Typed codepoints: live accumulator (written by onChar during poll) +
    // frame snapshot (read by getTypedChars), same pattern as scroll.
    std::vector<unsigned int> typedCharsLive;
    std::vector<unsigned int> prevTypedChars;
};