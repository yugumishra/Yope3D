#include "Surface.h"
// Surface.h includes vulkan.h first.  Window.h brings in glfw3.h second.
// glfw3.h checks for VK_VERSION_1_0 (defined by vulkan.h) and only then
// declares glfwCreateWindowSurface — so this include order is load-bearing.
#include "platform/Window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>

Surface::Surface(VkInstance inst, Window& window)
    : instance(inst)
{
    if (glfwCreateWindowSurface(inst, window.getHandle(), nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan window surface (glfwCreateWindowSurface)");
}

Surface::~Surface() {
    if (surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surface, nullptr);
}
