#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// ---------------------------------------------------------------------------
// StorageImage — RAII wrapper for a device-local VkImage intended for use as
// both a STORAGE_IMAGE (written by a compute shader) and a SAMPLED_IMAGE
// (read by a fragment shader).
//
// The image format should be verified against VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
// before construction. R8G8B8A8_UNORM is used by default.
//
// Lifecycle of layout per frame:
//   Frame start  : UNDEFINED (first frame only) or SHADER_READ_ONLY_OPTIMAL
//   Before dispatch : transition → GENERAL
//   After dispatch  : transition → SHADER_READ_ONLY_OPTIMAL
//   Fragment sample : SHADER_READ_ONLY_OPTIMAL
// ---------------------------------------------------------------------------

class StorageImage {
public:
    StorageImage() = default;
    StorageImage(GpuDevice& gpu, uint32_t width, uint32_t height, VkFormat format,
                 VkCommandPool cmdPool);
    ~StorageImage();

    StorageImage(StorageImage&&) noexcept;
    StorageImage& operator=(StorageImage&&) noexcept;
    StorageImage(const StorageImage&) = delete;
    StorageImage& operator=(const StorageImage&) = delete;

    void destroy(VkDevice device);
    void recreate(GpuDevice& gpu, uint32_t newWidth, uint32_t newHeight, VkCommandPool cmdPool);

    // Record a pipeline barrier + image layout transition into cmd.
    void transition(VkCommandBuffer cmd,
                    VkImageLayout oldLayout, VkImageLayout newLayout,
                    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                    VkAccessFlags srcAccess, VkAccessFlags dstAccess) const;

    VkImage     image()   const { return image_;   }
    VkImageView view()    const { return view_;    }
    VkSampler   sampler() const { return sampler_; }
    VkFormat    format()  const { return format_;  }
    uint32_t    width()   const { return width_;   }
    uint32_t    height()  const { return height_;  }

private:
    void create(GpuDevice& gpu, uint32_t w, uint32_t h, VkFormat fmt, VkCommandPool cmdPool);

    VkImage        image_   = VK_NULL_HANDLE;
    VkDeviceMemory memory_  = VK_NULL_HANDLE;
    VkImageView    view_    = VK_NULL_HANDLE;
    VkSampler      sampler_ = VK_NULL_HANDLE;
    VkFormat       format_  = VK_FORMAT_UNDEFINED;
    uint32_t       width_   = 0;
    uint32_t       height_  = 0;
    VkDevice       device_  = VK_NULL_HANDLE;
};
