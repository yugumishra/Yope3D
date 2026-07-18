#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <memory>
#include <string>
#include "../gpu/Texture.h"
#include "../rendering/MaterialCache.h"
#include "TextureStreamer.h"

class GpuDevice;
class RenderMesh;
namespace ecs { struct Material; }

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
    // textureSetLayout  — single-sampler set (UI + legacy per-texture sets).
    // materialSetLayout — 5-sampler PBR material set (shared with MaterialCache).
    void init(GpuDevice& gpu, VkCommandPool commandPool,
              VkDescriptorSetLayout textureSetLayout,
              VkDescriptorSetLayout materialSetLayout);

    // Cleanup all resources. Must be called before GPU is destroyed.
    void cleanup(VkDevice device);

    // Load a texture from disk (deduplicates by path). srgb=true (default) for
    // color maps (albedo/emissive); srgb=false for data maps (normal/metal-rough/
    // occlusion) whose values must not be gamma-decoded.
    // Supports both embedded assets (YOPE_EMBED_ASSETS) and filesystem mode.
    Texture* loadTexture(GpuDevice& gpu, const std::string& path);
    Texture* loadTexture(const std::string& path);  // script-friendly: uses cached gpu
    Texture* loadTextureSrgb(const std::string& path, bool srgb);

    // Register an already-decoded RGBA8 image under a synthetic cache key, stored
    // under the same srgb-aware key loadTextureSrgb(path, srgb) computes — so a
    // later material resolve finds it. Used by GltfLoader for embedded/base64 images.
    Texture* registerDecodedTexture(const std::string& path, bool srgb,
                                    const uint8_t* pixels, int width, int height);

    // Async streaming: queue an embedded/base64 image (still-encoded bytes, e.g.
    // PNG/JPEG) for background decode instead of decoding + uploading inline.
    // No-op if `key` (srgb-aware) is already resident. `encodedBytes` is copied.
    void enqueueTextureDecode(const std::string& key, bool srgb,
                              const uint8_t* encodedBytes, int len);

    // Call once per frame (main/render thread only, before drawFrame). Uploads
    // decoded-but-not-yet-GPU-resident textures within a time budget, and
    // rewrites any cached material descriptor sets that were waiting on them.
    void pumpTextureUploads(double budgetMs = 5.0);

    // True while async embedded-image decode/upload is still in flight. The
    // loading splash holds until this is false so the scene doesn't pop in with
    // placeholder (white) textures. completedCount always catches up to
    // enqueuedCount (decode failures count as completed), so this is guaranteed
    // to become false.
    bool isStreamingTextures() const {
        return streamer_.completedCount() < streamer_.enqueuedCount();
    }

    // Get the default 1×1 white texture (used for untextured meshes).
    Texture* getDefaultTexture() const;

    // Default PBR map textures (1×1) for unfilled material slots.
    Texture* getDefaultAlbedo()     const { return defaultTexture.get(); }      // white, sRGB
    Texture* getDefaultNormal()     const { return defaultNormal.get(); }       // flat (0,0,1)
    Texture* getDefaultMetalRough() const { return defaultMetalRough.get(); }   // white (neutral)
    Texture* getDefaultOcclusion()  const { return defaultOcclusion.get(); }    // white (unoccluded)
    Texture* getDefaultEmissive()   const { return defaultEmissive.get(); }     // black

    // Material descriptor sets (set 1, 5 PBR samplers).
    VkDescriptorSet   defaultMaterialSet(Texture* albedo) { return materialCache_.defaultSetFor(albedo); }
    ResolvedMaterial* resolveMaterial(const ecs::Material& m) { return materialCache_.resolve(m); }

    // Load a mesh from an OBJ file (deduplicates by path).
    // The path is relative to YOPE_ASSETS_DIR (e.g., "models/myobj.obj").
    RenderMesh* loadMesh(GpuDevice& gpu, const std::string& path);

    // Add a mesh from CPU-side data (for Primitives or dynamic loading).
    // cacheKey: optional key to store in cache (if empty, mesh is not cached).
    RenderMesh* addMesh(GpuDevice& gpu, const std::string& cacheKey = "");

#ifdef YOPE_EDITOR
    // Hot-reload: if path matches a cached texture or mesh, evict and reload it.
    void onFileChanged(const std::string& absPath);
#endif

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

private:
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    std::unique_ptr<Texture> defaultTexture;      // white albedo
    std::unique_ptr<Texture> defaultNormal;       // flat normal (128,128,255)
    std::unique_ptr<Texture> defaultMetalRough;   // white (neutral multiply)
    std::unique_ptr<Texture> defaultOcclusion;    // white
    std::unique_ptr<Texture> defaultEmissive;     // black
    std::unordered_map<std::string, std::unique_ptr<RenderMesh>> meshes;

    MaterialCache         materialCache_;
    TextureStreamer       streamer_;

    GpuDevice*            gpu_ = nullptr;
    VkDescriptorPool      descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkCommandPool         commandPool = VK_NULL_HANDLE;
    VkDevice              device = VK_NULL_HANDLE;
};
