#pragma once

#include <GLFW/glfw3.h>
#include <string>

class Input; // forward-declare to avoid circular include

class Window {
public:
    // title       — window title bar string
    // screenW/H   — full monitor resolution; window is created at half that,
    //               matching the Java Launch/Window pattern. maxWidth/maxHeight
    //               stores the full value for fullscreen toggle.
    Window(const std::string& title, int screenW, int screenH);
    ~Window();

    // Must be called once after Input is constructed.
    // Registers all GLFW callbacks and applies the initial cursor mode.
    void init(Input* input);

    void pollEvents();
    bool shouldClose() const;

    // No-op until Milestone 3 (Vulkan swapchain present replaces this).
    // With GLFW_NO_API set, glfwSwapBuffers is invalid; the declaration is kept
    // so main.cpp compiles unchanged.
    void swapBuffers();

    void setTitle(const std::string& newTitle);
    void setEscapeCloses(bool v) { escapeCloses = v; }

    int   getWidth()       const { return width;  }
    int   getHeight()      const { return height; }
    float getAspectRatio() const { return static_cast<float>(width) / static_cast<float>(height); }
    GLFWwindow* getHandle() const { return window; }

    // GLFW reports cursor position in screen coordinates (points), while
    // width/height above track framebuffer pixels — on a HiDPI/Retina display
    // these differ by this scale factor. Multiply cursor coords by this
    // before dividing by getWidth()/getHeight() to get a correct [0,1]
    // fraction; see Engine::update's UI input routing.
    void getContentScale(float& sx, float& sy) const { glfwGetWindowContentScale(window, &sx, &sy); }

    bool isPaused() const { return paused; }
    void pause();
    void unpause();

    // Set by the framebuffer resize callback; cleared by Renderer after recreation.
    bool wasResized()      const { return resized; }
    void clearResizedFlag()      { resized = false; }

    //asset path is always relative to assets folder
    void setIcon(const std::string& assetPath);

private:
    GLFWwindow* window    = nullptr;
    Input*      input     = nullptr;

    int  width     = 0;
    int  height    = 0;
    int  maxWidth  = 0;
    int  maxHeight = 0;

    bool paused        = false;
    bool fullscreen    = false;
    bool resized       = false;
    bool escapeCloses  = true;

    // Previous cursor position — used to compute per-frame delta.
    // 'firstMouse' prevents a large spike on the very first callback.
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    bool   firstMouse  = true;

    void applyCursorMode() const;

    // Static GLFW callbacks — each retrieves Window* via glfwGetWindowUserPointer.
    static void framebufferResizeCallback(GLFWwindow* w, int width, int height);
    static void keyCallback              (GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback      (GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback        (GLFWwindow* w, double xPos, double yPos);
    static void scrollCallback           (GLFWwindow* w, double xOffset, double yOffset);
    static void charCallback             (GLFWwindow* w, unsigned int codepoint);
};