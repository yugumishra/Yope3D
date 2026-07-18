#include "GpuDevice.h"
#include "platform/Window.h"
#include <stdexcept>

GpuDevice::GpuDevice(Window& window) {
    vkInstance = std::make_unique<VulkanInstance>();
    vkSurface  = std::make_unique<Surface>(vkInstance->get(), window);
    vkPhysical = std::make_unique<PhysicalDevice>(vkInstance->get(), vkSurface->get());
    vkLogical  = std::make_unique<LogicalDevice>(*vkPhysical);
}

uint32_t GpuDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    throw std::runtime_error("Failed to find suitable memory type");
}
