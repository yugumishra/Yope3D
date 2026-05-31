#include "Window.h"
#include "Input.h"
#include "../assets/ImageLoader.h"

#include <filesystem>
#include <iostream>

#ifdef YOPE_EMBED_ASSETS
#include "generated/embedded_assets.h"
#endif

#include <stdexcept>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Window::Window(const std::string& title, int screenW, int screenH)
    : maxWidth(screenW), maxHeight(screenH)
{
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan — no OpenGL context
    glfwWindowHint(GLFW_VISIBLE,    GLFW_FALSE);   // show after fully set up
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    // Create at half the screen resolution, centred — mirrors Java Window.java.
    int initW = screenW / 2;
    int initH = screenH / 2;

    window = glfwCreateWindow(initW, initH, title.c_str(), nullptr, nullptr);
    if (!window)
        throw std::runtime_error("Failed to create GLFW window");

    width  = initW;
    height = initH;

    // Store 'this' so static callbacks can reach the instance.
    glfwSetWindowUserPointer(window, this);

    glfwSetWindowPos(window, screenW / 4, screenH / 4);

    // Make visible.
    glfwShowWindow(window);
}

Window::~Window() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void Window::init(Input* inp) {
    input = inp;

    // Register every callback after 'input' is set so the sinks are valid.
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetKeyCallback            (window, keyCallback);
    glfwSetMouseButtonCallback    (window, mouseButtonCallback);
    glfwSetCursorPosCallback      (window, cursorPosCallback);
    glfwSetScrollCallback         (window, scrollCallback);

    applyCursorMode();
}

void Window::pollEvents() {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    return static_cast<bool>(glfwWindowShouldClose(window));
}

void Window::swapBuffers() {
    // No-op for Milestone 2.
    // Milestone 3: Vulkan swapchain present (vkQueuePresentKHR) replaces this.
    // glfwSwapBuffers is intentionally NOT called — GLFW_NO_API makes it invalid.
}

void Window::setTitle(const std::string& newTitle) {
    glfwSetWindowTitle(window, newTitle.c_str());
}

void Window::pause() {
    paused = true;
    // Centre the visible cursor before revealing it so it always appears in a
    // predictable position regardless of where it was when DISABLED was active.
    glfwSetCursorPos(window, width / 2.0, height / 2.0);
    applyCursorMode();
}

void Window::unpause() {
    paused = false;
    // Mark firstMouse true so the first movement after resuming doesn't spike.
    firstMouse = true;
    applyCursorMode();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Window::applyCursorMode() const {
    // GLFW_CURSOR_DISABLED: cursor hidden + locked; position becomes unbounded,
    // giving us clean per-frame delta without any OS acceleration.
    // GLFW_CURSOR_NORMAL:   restore visible cursor during pause / menus.
    glfwSetInputMode(window, GLFW_CURSOR,
                     paused ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

// ---------------------------------------------------------------------------
// Static GLFW callbacks
// ---------------------------------------------------------------------------

void Window::framebufferResizeCallback(GLFWwindow* w, int newWidth, int newHeight) {
    auto* self   = static_cast<Window*>(glfwGetWindowUserPointer(w));
    self->width   = newWidth;
    self->height  = newHeight;
    self->resized = true;
}

void Window::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));

    // Engine-level hotkeys are consumed here and are NOT forwarded to Input,
    // so scripts never see them as raw keys.

    if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE && self->escapeCloses)
        glfwSetWindowShouldClose(w, GLFW_TRUE);

    if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
        if (!self->fullscreen) {
            // Go fullscreen on the primary monitor at its native resolution.
            glfwMaximizeWindow(w);
            self->width  = self->maxWidth;
            self->height = self->maxHeight;
            glfwSetWindowMonitor(w, glfwGetPrimaryMonitor(),
                                 0, 0, self->width, self->height, GLFW_DONT_CARE);
            self->fullscreen = true;
        } else {
            // Return to windowed mode at the last known windowed size.
            glfwSetWindowMonitor(w, nullptr, 0, 0,
                                 self->width, self->height, GLFW_DONT_CARE);
            self->firstMouse = true; // prevent delta spike on mode change
            self->fullscreen = false;
        }
    }

    if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        if (self->paused) self->unpause();
        else              self->pause();
    }

    // Forward everything else to Input.
    if (self->input && key >= 0 && key <= GLFW_KEY_LAST)
        self->input->onKey(key, action);
}

void Window::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    if (self->input)
        self->input->onMouseButton(button, action);
}

void Window::cursorPosCallback(GLFWwindow* w, double xPos, double yPos) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));

    // Initialise on the very first callback to avoid a huge spike.
    if (self->firstMouse) {
        self->lastCursorX = xPos;
        self->lastCursorY = yPos;
        self->firstMouse  = false;
    }

    // Accumulate delta only while unpaused and CURSOR_DISABLED is active.
    // GLFW_CURSOR_DISABLED reports an unbounded virtual position, so
    // (current − last) gives the raw per-frame mouse travel.
    if (!self->paused && self->input) {
        double dx = xPos - self->lastCursorX;
        double dy = yPos - self->lastCursorY;
        self->input->onMouseMove(dx, dy);
    }

    self->lastCursorX = xPos;
    self->lastCursorY = yPos;
}

void Window::scrollCallback(GLFWwindow* w, double xOffset, double yOffset) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    if (self->input)
        self->input->onScroll(xOffset, yOffset);
}

void Window::setIcon(const std::string& assetPath) {
    LoadedImage image;

    try {
#ifdef YOPE_EMBED_ASSETS
        EmbeddedAsset asset = getEmbeddedAsset(assetPath.c_str());
        if (asset.data) {
            image = ImageLoader::loadFromMemory(asset.data, static_cast<int>(asset.size));
        } else {
            return;  // Icon is cosmetic — missing file is not fatal.
        }
#else
        // This handles "/" vs "\" automatically for Mac and Windows builds
        std::string fullPath = (std::filesystem::path(YOPE_ASSETS_DIR) / assetPath).string();
        image = ImageLoader::load(fullPath);
#endif
    } catch (...) {
        // Icon is cosmetic — missing file is not fatal.
        return;
    }

    GLFWimage icon{ image.width, image.height, image.pixels.data() };
    glfwSetWindowIcon(window, 1, &icon);
}

