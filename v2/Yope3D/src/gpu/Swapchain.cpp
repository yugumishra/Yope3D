#include "Swapchain.h"
#include "GpuDevice.h"
#include "platform/Window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <algorithm>
#include <limits>

Swapchain::Swapchain(GpuDevice& gpu, Window& window) {
    device = gpu.device();
    create(gpu, window);
}

Swapchain::~Swapchain() { cleanup(); }

void Swapchain::recreate(GpuDevice& gpu, Window& window) {
    cleanup();
    create(gpu, window);
}

void Swapchain::create(GpuDevice& gpu, Window& window) {
    VkPhysicalDevice pd  = gpu.physicalDevice();
    VkSurfaceKHR     sur = gpu.surface();

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, sur, &caps);

    uint32_t count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, sur, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, sur, &count, formats.data());

    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, sur, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, sur, &count, modes.data());

    auto sf     = chooseSurfaceFormat(formats);
    auto pm     = choosePresentMode(modes);
    auto extent = chooseExtent(caps, window);

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = sur;
    ci.minImageCount    = imgCount;
    ci.imageFormat      = sf.format;
    ci.imageColorSpace  = sf.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const auto& fam = gpu.queueFamilies();
    uint32_t queueIndices[] = { fam.graphics.value(), fam.present.value() };
    if (queueIndices[0] != queueIndices[1]) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = queueIndices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = pm;
    ci.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain");

    format = sf.format;
    ext    = extent;

    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
    images.resize(imgCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, images.data());

    views.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image      = images[i];
        vi.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        vi.format     = format;
        vi.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device, &vi, nullptr, &views[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create swapchain image view");
    }
}

void Swapchain::cleanup() {
    for (auto v : views) vkDestroyImageView(device, v, nullptr);
    views.clear();
    images.clear();
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats[0];
}

VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR; // guaranteed available
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps, Window& window) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

    int w, h;
    glfwGetFramebufferSize(window.getHandle(), &w, &h);
    return {
        std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}
