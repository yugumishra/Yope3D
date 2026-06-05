#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class GpuDevice;

// ---------------------------------------------------------------------------
// Texture
//
// RAII wrapper around VkImage + VkImageView + VkSampler + VkDescriptorSet.
// Owns the combined image sampler descriptor for binding in set 1.
// Use load() to create from raw RGBA8 pixel data.
// Call destroy() before the GpuDevice is destroyed.
// ---------------------------------------------------------------------------

class Texture {
public:
    // Load from RGBA8 pixels (width × height).
    // generateMipmaps=true (default): generates full mip chain with LINEAR filtering.
    // generateMipmaps=false: single mip level, NEAREST mipmap mode, CLAMP_TO_EDGE —
    //   appropriate for font atlases where glyph bleeding across mip levels is undesirable.
    // srgb=true (default): R8G8B8A8_SRGB (gamma-decoded on sample) — for color textures.
    // srgb=false: R8G8B8A8_UNORM (linear, raw values) — required for data textures like
    //   MSDF distance fields, whose channels must NOT be gamma-decoded.
    static Texture load(GpuDevice& gpu,
                       VkCommandPool commandPool,
                       VkDescriptorSetLayout set1Layout,
                       VkDescriptorPool texturePool,
                       const uint8_t* pixels, int width, int height,
                       bool generateMipmaps = true,
                       bool srgb = true);

    Texture() = default;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture() = default;

    void destroy(VkDevice device);

    VkImage         getImage()         const { return image; }
    VkImageView     getImageView()     const { return imageView; }
    VkSampler       getSampler()       const { return sampler; }
    VkDescriptorSet getDescriptorSet() const { return descriptorSet; }
    uint32_t        getMipLevels()     const { return mipLevels; }

private:
    VkImage         image = VK_NULL_HANDLE;
    VkDeviceMemory  memory = VK_NULL_HANDLE;
    VkImageView     imageView = VK_NULL_HANDLE;
    VkSampler       sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    uint32_t        mipLevels = 1;

    Texture(VkImage img, VkDeviceMemory mem, VkImageView view, VkSampler samp,
            VkDescriptorSet descSet, uint32_t mips)
        : image(img), memory(mem), imageView(view), sampler(samp),
          descriptorSet(descSet), mipLevels(mips) {}

    static uint32_t calculateMipLevels(int width, int height);
    static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t filter,
                                   VkMemoryPropertyFlags props);
};
