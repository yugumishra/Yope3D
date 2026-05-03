#pragma once
#include <vulkan/vulkan.h>
#include <optional>
#include <vector>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    std::optional<uint32_t> compute;

    bool isComplete() const {
        return graphics.has_value() && present.has_value() && compute.has_value();
    }
};

// ---------------------------------------------------------------------------
// PhysicalDevice
//
// Enumerates all VkPhysicalDevice handles and selects the best one via a
// simple scoring function.  Disqualifies any device missing required queue
// families or required extensions.
//
// Also determines which device extensions the LogicalDevice must enable
// (required extensions + optional ones like VK_KHR_portability_subset that
// MUST be listed if exposed by the device).
// ---------------------------------------------------------------------------

class PhysicalDevice {
public:
    PhysicalDevice(VkInstance instance, VkSurfaceKHR surface);

    VkPhysicalDevice                   get()                const { return device; }
    const QueueFamilyIndices&          queueFamilies()      const { return families; }
    const VkPhysicalDeviceProperties&  properties()         const { return props; }
    const VkPhysicalDeviceFeatures&    features()           const { return feats; }
    // Full list of device extensions LogicalDevice should enable.
    const std::vector<const char*>&    extensionsToEnable() const { return extensions; }

    void printInfo() const;

    PhysicalDevice(const PhysicalDevice&) = delete;
    PhysicalDevice& operator=(const PhysicalDevice&) = delete;

private:
    VkPhysicalDevice           device     = VK_NULL_HANDLE;
    QueueFamilyIndices         families;
    VkPhysicalDeviceProperties props      = {};
    VkPhysicalDeviceFeatures   feats      = {};
    std::vector<const char*>   extensions;

    int                scoreDevice(VkPhysicalDevice d, VkSurfaceKHR surface) const;
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice d, VkSurfaceKHR surface) const;
    bool               hasRequiredExtensions(VkPhysicalDevice d) const;
    void               collectExtensionsToEnable(VkPhysicalDevice d);

    // Extensions every device must support.
    static const std::vector<const char*> requiredExtensions;
};
