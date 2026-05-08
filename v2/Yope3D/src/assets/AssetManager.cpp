#include "AssetManager.h"
#include "../gpu/Texture.h"
#include "../gpu/GpuDevice.h"
#include <stb_image.h>
#include <filesystem>
#include <stdexcept>

#ifdef YOPE_EMBED_ASSETS
#include "generated/embedded_assets.h"
#endif

AssetManager::~AssetManager() {
    // Cleanup is handled by the caller (Engine) explicitly.
}

void AssetManager::init(GpuDevice& gpu, VkCommandPool commandPool,
                       VkDescriptorSetLayout textureSetLayout)
{
    device = gpu.device();
    this->commandPool = commandPool;
    descriptorSetLayout = textureSetLayout;

    // ---------------------------------------------------------------------------
    // Create descriptor pool for texture samplers (set 1).
    // Reserve capacity for up to 64 textures.
    // ---------------------------------------------------------------------------

    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 64;

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    poolCI.maxSets       = 64;

    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture descriptor pool");

    // ---------------------------------------------------------------------------
    // Create default 1×1 white texture.
    // ---------------------------------------------------------------------------

    uint8_t whitePixel[4] = {255, 255, 255, 255};  // RGBA white
    defaultTexture = std::make_unique<Texture>(
        Texture::load(gpu, commandPool, textureSetLayout, descriptorPool,
                     whitePixel, 1, 1)
    );
}

void AssetManager::cleanup(VkDevice dev)
{
    // Explicitly destroy all loaded textures first.
    for (auto& pair : textures) {
        if (pair.second) {
            pair.second->destroy(dev);
        }
    }
    textures.clear();

    // Destroy default texture.
    if (defaultTexture) {
        defaultTexture->destroy(dev);
    }
    defaultTexture.reset();

    // Destroy descriptor pool.
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
}

Texture* AssetManager::loadTexture(GpuDevice& gpu, const std::string& path)
{
    // Check if already loaded.
    auto it = textures.find(path);
    if (it != textures.end())
        return it->second.get();

    // ---------------------------------------------------------------------------
    // Load image data (supports embedded + filesystem modes).
    // ---------------------------------------------------------------------------

    int w, h, channels;
    stbi_uc* pixels = nullptr;

#ifdef YOPE_EMBED_ASSETS
    EmbeddedAsset asset = getEmbeddedAsset(path.c_str());
    if (asset.data) {
        pixels = stbi_load_from_memory(
            asset.data, static_cast<int>(asset.size),
            &w, &h, &channels, 4);  // Force RGBA
    }
#else
    // Filesystem mode: handle "/" vs "\" automatically for cross-platform paths.
    std::string fullPath = (std::filesystem::path(YOPE_ASSETS_DIR) / path).string();
    pixels = stbi_load(fullPath.c_str(), &w, &h, &channels, 4);  // Force RGBA
#endif

    if (!pixels)
        throw std::runtime_error("Failed to load texture: " + path);

    // ---------------------------------------------------------------------------
    // Create Vulkan texture from pixel data.
    // ---------------------------------------------------------------------------

    auto texture = std::make_unique<Texture>(
        Texture::load(gpu, commandPool, descriptorSetLayout, descriptorPool,
                     pixels, w, h)
    );

    stbi_image_free(pixels);

    Texture* texturePtr = texture.get();
    textures[path] = std::move(texture);
    return texturePtr;
}

Texture* AssetManager::getDefaultTexture() const
{
    return defaultTexture.get();
}
