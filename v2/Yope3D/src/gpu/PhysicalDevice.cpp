#include "PhysicalDevice.h"
#include <stdexcept>
#include <iostream>
#include <vector>
#include <set>
#include <string>
#include <cstring>

const std::vector<const char*> PhysicalDevice::requiredExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

PhysicalDevice::PhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> candidates(count);
    vkEnumeratePhysicalDevices(instance, &count, candidates.data());

    int bestScore = -1;
    for (auto candidate : candidates) {
        int score = scoreDevice(candidate, surface);
        if (score > bestScore) {
            bestScore = score;
            device    = candidate;
        }
    }

    if (bestScore < 0)
        throw std::runtime_error("No suitable GPU found — needs VK_KHR_swapchain, graphics/present/compute queues");

    vkGetPhysicalDeviceProperties(device, &props);
    vkGetPhysicalDeviceFeatures(device, &feats);
    families = findQueueFamilies(device, surface);
    collectExtensionsToEnable(device);

    printInfo();
}

int PhysicalDevice::scoreDevice(VkPhysicalDevice d, VkSurfaceKHR surface) const {
    if (!hasRequiredExtensions(d)) return -1;

    auto fam = findQueueFamilies(d, surface);
    if (!fam.isComplete()) return -1;

    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(d, &p);

    int score = 0;
    switch (p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score += 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score += 500;  break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score += 200;  break;
        default:                                                     break;
    }
    // Tiebreaker: higher max texture dimension = more capable device.
    score += static_cast<int>(p.limits.maxImageDimension2D / 1024);
    return score;
}

QueueFamilyIndices PhysicalDevice::findQueueFamilies(VkPhysicalDevice d, VkSurfaceKHR surface) const {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &count, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(count);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &count, qfProps.data());

    QueueFamilyIndices idx;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& f = qfProps[i];
        if (f.queueFlags & VK_QUEUE_GRAPHICS_BIT) idx.graphics = i;
        if (f.queueFlags & VK_QUEUE_COMPUTE_BIT)  idx.compute  = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &presentSupport);
        if (presentSupport) idx.present = i;

        if (idx.isComplete()) break;
    }
    return idx;
}

bool PhysicalDevice::hasRequiredExtensions(VkPhysicalDevice d) const {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(d, nullptr, &count, available.data());

    std::set<std::string> remaining(requiredExtensions.begin(), requiredExtensions.end());
    for (const auto& ext : available)
        remaining.erase(ext.extensionName);
    return remaining.empty();
}

void PhysicalDevice::collectExtensionsToEnable(VkPhysicalDevice d) {
    extensions = requiredExtensions;

    // VK_KHR_portability_subset MUST be enabled if the device exposes it.
    // On MoltenVK (Mac) this is always present.  If listed by the driver
    // but not requested in vkCreateDevice, device creation is rejected.
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(d, nullptr, &count, available.data());

    for (const auto& ext : available) {
        if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
            extensions.push_back("VK_KHR_portability_subset");
            break;
        }
    }
}

static const char* deviceTypeStr(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                      return "Unknown";
    }
}

void PhysicalDevice::printInfo() const {
    std::cout << "=== Vulkan Physical Device ===\n"
              << "  Name:    " << props.deviceName << "\n"
              << "  Type:    " << deviceTypeStr(props.deviceType) << "\n"
              << "  API:     "
              << VK_API_VERSION_MAJOR(props.apiVersion) << "."
              << VK_API_VERSION_MINOR(props.apiVersion) << "."
              << VK_API_VERSION_PATCH(props.apiVersion) << "\n"
              << "  Queues:  graphics=" << families.graphics.value()
              << "  present="           << families.present.value()
              << "  compute="           << families.compute.value() << "\n"
              << "  Device extensions enabled:\n";
    for (const char* ext : extensions)
        std::cout << "    " << ext << "\n";
    std::cout << "==============================\n";
}
