#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <memory>
#include <string>
#include "../gpu/Texture.h"

class GpuDevice;
class RenderMesh;

// ---------------------------------------------------------------------------
// AssetManager
//
// Owns and deduplicates texture resources.
// Manages a separate VkDescriptorPool for per-texture descriptor sets (set 1).
// Also provides a default 1×1 white texture for untextured meshes.
// ---------------------------------------------------------------------------

class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager();

    // Initialize the asset manager with GPU context and command pool.
    void init(GpuDevice& gpu, VkCommandPool commandPool,
              VkDescriptorSetLayout textureSetLayout);

    // Cleanup all resources. Must be called before GPU is destroyed.
    void cleanup(VkDevice device);

    // Load a texture from disk (deduplicates by path).
    // Supports both embedded assets (YOPE_EMBED_ASSETS) and filesystem mode.
    Texture* loadTexture(GpuDevice& gpu, const std::string& path);

    // Get the default 1×1 white texture (used for untextured meshes).
    Texture* getDefaultTexture() const;

    // Load a mesh from an OBJ file (deduplicates by path).
    // The path is relative to YOPE_ASSETS_DIR (e.g., "models/myobj.obj").
    RenderMesh* loadMesh(GpuDevice& gpu, const std::string& path);

    // Add a mesh from CPU-side data (for Primitives or dynamic loading).
    // cacheKey: optional key to store in cache (if empty, mesh is not cached).
    RenderMesh* addMesh(GpuDevice& gpu, const std::string& cacheKey = "");

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

private:
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    std::unique_ptr<Texture> defaultTexture;
    std::unordered_map<std::string, std::unique_ptr<RenderMesh>> meshes;

    VkDescriptorPool      descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkCommandPool         commandPool = VK_NULL_HANDLE;
    VkDevice              device = VK_NULL_HANDLE;
};
