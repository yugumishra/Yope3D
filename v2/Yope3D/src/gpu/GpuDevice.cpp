#include "GpuDevice.h"
#include "platform/Window.h"

GpuDevice::GpuDevice(Window& window) {
    vkInstance = std::make_unique<VulkanInstance>();
    vkSurface  = std::make_unique<Surface>(vkInstance->get(), window);
    vkPhysical = std::make_unique<PhysicalDevice>(vkInstance->get(), vkSurface->get());
    vkLogical  = std::make_unique<LogicalDevice>(*vkPhysical);
}
