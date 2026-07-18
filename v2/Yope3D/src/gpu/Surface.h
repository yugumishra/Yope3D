#pragma once
#include <vulkan/vulkan.h>

class Window;

// Thin RAII wrapper around VkSurfaceKHR.
// Created from a GLFW window handle; destroyed before the VkInstance.
class Surface {
public:
    Surface(VkInstance instance, Window& window);
    ~Surface();

    VkSurfaceKHR get() const { return surface; }

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

private:
    VkInstance   instance = VK_NULL_HANDLE; // non-owning reference for cleanup
    VkSurfaceKHR surface  = VK_NULL_HANDLE;
};
