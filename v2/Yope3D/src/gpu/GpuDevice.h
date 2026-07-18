#pragma once
#include <memory>
#include "VulkanInstance.h"
#include "Surface.h"
#include "PhysicalDevice.h"
#include "LogicalDevice.h"

class Window;

// ---------------------------------------------------------------------------
// GpuDevice
//
// Top-level Vulkan context owned by Engine.  Wraps the four foundational
// objects in strict construction / destruction order:
//
//   1. VulkanInstance  — VkInstance + optional debug messenger
//   2. Surface         — VkSurfaceKHR (needs instance + GLFW window)
//   3. PhysicalDevice  — device selection + queue family indices
//   4. LogicalDevice   — VkDevice + queue handles
//
// unique_ptr members are destroyed in reverse declaration order, so the
// destruction sequence (logical → physical → surface → instance) is correct
// without any explicit cleanup code.
//
// Milestone 3b+ will add Swapchain, RenderPass, CommandPool, etc. as members.
// ---------------------------------------------------------------------------

class GpuDevice {
public:
    explicit GpuDevice(Window& window);
    ~GpuDevice() = default;

    // Raw handle accessors for downstream subsystems (Swapchain, pipelines…).
    VkInstance       instance()       const { return vkInstance->get(); }
    VkSurfaceKHR     surface()        const { return vkSurface->get(); }
    VkPhysicalDevice physicalDevice() const { return vkPhysical->get(); }
    VkDevice         device()         const { return vkLogical->get(); }
    VkQueue          graphicsQueue()  const { return vkLogical->graphicsQueue(); }
    VkQueue          presentQueue()   const { return vkLogical->presentQueue(); }
    VkQueue          computeQueue()   const { return vkLogical->computeQueue(); }

    const QueueFamilyIndices&         queueFamilies() const { return vkPhysical->queueFamilies(); }
    const VkPhysicalDeviceProperties& deviceProps()   const { return vkPhysical->properties(); }
    const VkPhysicalDeviceFeatures&   deviceFeatures() const { return vkPhysical->features(); }

    // Find a memory type index matching requirements and desired properties.
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    // Block the CPU until all GPU work is finished. Safe to call at any time.
    // Use this for mid-frame resource swaps (e.g. scene reloads).
    // Do NOT use Renderer::waitIdle() for this — that is a full teardown.
    void syncDevice() const { vkDeviceWaitIdle(vkLogical->get()); }

    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;

private:
    std::unique_ptr<VulkanInstance> vkInstance;
    std::unique_ptr<Surface>        vkSurface;
    std::unique_ptr<PhysicalDevice> vkPhysical;
    std::unique_ptr<LogicalDevice>  vkLogical;
};
