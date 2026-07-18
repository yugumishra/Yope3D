#pragma once
#include <vulkan/vulkan.h>
#include <vector>

class GpuDevice;
class Window;

// ---------------------------------------------------------------------------
// Swapchain
//
// Owns the VkSwapchainKHR, its VkImages (non-owning handles given by the
// driver), and the VkImageViews we create for them.
//
// recreate() is called on window resize or VK_ERROR_OUT_OF_DATE_KHR.
// It destroys the old swapchain and builds a new one in place.
// ---------------------------------------------------------------------------

class Swapchain {
public:
    Swapchain(GpuDevice& gpu, Window& window);
    ~Swapchain();

    void recreate(GpuDevice& gpu, Window& window);

    VkSwapchainKHR                  handle()      const { return swapchain; }
    VkFormat                        imageFormat() const { return format;    }
    VkExtent2D                      extent()      const { return ext;       }
    uint32_t                        imageCount()  const { return static_cast<uint32_t>(views.size()); }
    const std::vector<VkImageView>& imageViews()  const { return views;     }

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

private:
    VkDevice               device    = VK_NULL_HANDLE;
    VkSwapchainKHR         swapchain = VK_NULL_HANDLE;
    VkFormat               format    = VK_FORMAT_UNDEFINED;
    VkExtent2D             ext       = {};
    std::vector<VkImage>   images;
    std::vector<VkImageView> views;

    void create(GpuDevice& gpu, Window& window);
    void cleanup();

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR   choosePresentMode  (const std::vector<VkPresentModeKHR>&   modes);
    VkExtent2D         chooseExtent       (const VkSurfaceCapabilitiesKHR& caps, Window& window);
};
