#pragma once
#include <vulkan/vulkan.h>

class PhysicalDevice;

// Owns VkDevice and the three queue handles.
// Queue handles are non-owning — they're implicitly destroyed with the device.
class LogicalDevice {
public:
    explicit LogicalDevice(const PhysicalDevice& physical);
    ~LogicalDevice();

    VkDevice get()           const { return device; }
    VkQueue  graphicsQueue() const { return graphics; }
    VkQueue  presentQueue()  const { return present;  }
    VkQueue  computeQueue()  const { return compute;  }

    LogicalDevice(const LogicalDevice&) = delete;
    LogicalDevice& operator=(const LogicalDevice&) = delete;

private:
    VkDevice device   = VK_NULL_HANDLE;
    VkQueue  graphics = VK_NULL_HANDLE;
    VkQueue  present  = VK_NULL_HANDLE;
    VkQueue  compute  = VK_NULL_HANDLE;
};
