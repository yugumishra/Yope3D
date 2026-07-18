#include "LogicalDevice.h"
#include "PhysicalDevice.h"
#include <stdexcept>
#include <vector>
#include <set>

LogicalDevice::LogicalDevice(const PhysicalDevice& physical) {
    const auto& fam = physical.queueFamilies();

    // Collect unique family indices — Vulkan rejects duplicate entries in
    // pQueueCreateInfos.  On MoltenVK (Mac) all three indices are often 0.
    std::set<uint32_t> uniqueFamilies = {
        fam.graphics.value(),
        fam.present.value(),
        fam.compute.value()
    };

    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    // Enable no extra features for now; individual milestones opt in as needed.
    VkPhysicalDeviceFeatures deviceFeatures{};

    const auto& exts = physical.extensionsToEnable();

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos       = queueInfos.data();
    createInfo.pEnabledFeatures        = &deviceFeatures;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    createInfo.ppEnabledExtensionNames = exts.data();
    // enabledLayerCount / ppEnabledLayerNames: deprecated since Vulkan 1.1.2,
    // ignored by drivers — we intentionally leave them at 0 / nullptr.

    if (vkCreateDevice(physical.get(), &createInfo, nullptr, &device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create VkDevice");

    // Queue index 0 — we request exactly one queue per family.
    vkGetDeviceQueue(device, fam.graphics.value(), 0, &graphics);
    vkGetDeviceQueue(device, fam.present.value(),  0, &present);
    vkGetDeviceQueue(device, fam.compute.value(),  0, &compute);
}

LogicalDevice::~LogicalDevice() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device); // flush pending work before teardown
        vkDestroyDevice(device, nullptr);
    }
}
