#include "AssetManager.h"
#include "../gpu/Texture.h"
#include "../gpu/GpuDevice.h"
#include "../world/RenderMesh.h"
#include "../debug/TaskProgress.h"
#include "AssetResolve.h"
#include "ObjLoader.h"
#include "ImageLoader.h"
#include <filesystem>
#include <stdexcept>
#include <chrono>
#include <vector>

namespace {
constexpr const char* kTextureStreamTaskLabel = "Streaming textures";
}

AssetManager::~AssetManager() {
    // Cleanup is handled by the caller (Engine) explicitly.
}

void AssetManager::init(GpuDevice& gpu, VkCommandPool commandPool,
                       VkDescriptorSetLayout textureSetLayout,
                       VkDescriptorSetLayout materialSetLayout)
{
    gpu_ = &gpu;
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

    // ---------------------------------------------------------------------------
    // Default 1×1 PBR maps for unfilled material slots. Data maps are UNORM
    // (linear, no gamma decode); only the color maps are sRGB.
    // ---------------------------------------------------------------------------
    auto makeDefault = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb) {
        uint8_t px[4] = {r, g, b, a};
        return std::make_unique<Texture>(
            Texture::load(gpu, commandPool, textureSetLayout, descriptorPool,
                          px, 1, 1, /*generateMipmaps*/ false, srgb));
    };
    defaultNormal     = makeDefault(128, 128, 255, 255, false);  // tangent-space (0,0,1)
    defaultMetalRough = makeDefault(255, 255, 255, 255, false);  // neutral multiply
    defaultOcclusion  = makeDefault(255, 255, 255, 255, false);  // unoccluded
    defaultEmissive   = makeDefault(0,   0,   0,   255, false);  // no emission

    // MaterialCache shares the 5-sampler material set layout (owned by Renderer).
    materialCache_.init(device, materialSetLayout, this);

    streamer_.start();
}

void AssetManager::cleanup(VkDevice dev)
{
    // Join the decode worker first — it holds no GPU resources, but must not be
    // pushing to the ready queue while we tear down textures below it.
    streamer_.stop();

    // Explicitly destroy all loaded meshes first.
    for (auto& pair : meshes) {
        if (pair.second) {
            pair.second->destroy(dev);
        }
    }
    meshes.clear();

    // Explicitly destroy all loaded textures.
    for (auto& pair : textures) {
        if (pair.second) {
            pair.second->destroy(dev);
        }
    }
    textures.clear();

    // Destroy default textures.
    for (Texture* t : { defaultTexture.get(), defaultNormal.get(), defaultMetalRough.get(),
                        defaultOcclusion.get(), defaultEmissive.get() }) {
        if (t) t->destroy(dev);
    }
    defaultTexture.reset();
    defaultNormal.reset();
    defaultMetalRough.reset();
    defaultOcclusion.reset();
    defaultEmissive.reset();

    // Destroy the material descriptor pool before its referenced textures are gone.
    materialCache_.cleanup(dev);

    // Destroy descriptor pool.
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
}

Texture* AssetManager::loadTextureSrgb(const std::string& path, bool srgb)
{
    // Cache key distinguishes the color space so the same file can serve as both
    // an sRGB color map and a linear data map without aliasing.
    std::string key = srgb ? path : (path + "#lin");
    auto it = textures.find(key);
    if (it != textures.end())
        return it->second.get();

    // ---------------------------------------------------------------------------
    // Load image data (supports embedded + filesystem modes).
    // ---------------------------------------------------------------------------

    std::vector<uint8_t> bytes = assets::readBytes(path);
    if (bytes.empty())
        throw std::runtime_error("Failed to load texture: " + path);
    LoadedImage image = ImageLoader::loadFromMemory(bytes.data(), static_cast<int>(bytes.size()));

    // ---------------------------------------------------------------------------
    // Create Vulkan texture from pixel data.
    // ---------------------------------------------------------------------------

    auto texture = std::make_unique<Texture>(
        Texture::load(*gpu_, commandPool, descriptorSetLayout, descriptorPool,
                     image.pixels.data(), image.width, image.height,
                     /*generateMipmaps*/ true, srgb)
    );

    Texture* texturePtr = texture.get();
    textures[key] = std::move(texture);
    return texturePtr;
}

Texture* AssetManager::loadTexture(GpuDevice& gpu, const std::string& path)
{
    return loadTextureSrgb(path, true);
}

Texture* AssetManager::loadTexture(const std::string& path)
{
    return loadTextureSrgb(path, true);
}

Texture* AssetManager::registerDecodedTexture(const std::string& path, bool srgb,
                                              const uint8_t* pixels, int width, int height)
{
    std::string key = srgb ? path : (path + "#lin");
    auto it = textures.find(key);
    if (it != textures.end())
        return it->second.get();

    auto texture = std::make_unique<Texture>(
        Texture::load(*gpu_, commandPool, descriptorSetLayout, descriptorPool,
                      pixels, width, height, /*generateMipmaps*/ true, srgb)
    );
    Texture* ptr = texture.get();
    textures[key] = std::move(texture);
    return ptr;
}

void AssetManager::enqueueTextureDecode(const std::string& key, bool srgb,
                                        const uint8_t* encodedBytes, int len)
{
    std::string cacheKey = srgb ? key : (key + "#lin");
    if (textures.find(cacheKey) != textures.end())
        return;   // already GPU-resident (or already queued and since arrived)
    streamer_.enqueueDecode(key, srgb, encodedBytes, len);
}

void AssetManager::pumpTextureUploads(double budgetMs)
{
    auto start = std::chrono::steady_clock::now();

    std::vector<std::string> arrivedKeys;
    TextureStreamer::DecodedTexture item;
    while (streamer_.popNext(item)) {
        registerDecodedTexture(item.key, item.srgb, item.pixels.data(), item.width, item.height);
        // `item.key` is the plain path as stored in ecs::Material's path fields
        // (registerDecodedTexture applies the "#lin" cache-key suffix internally).
        arrivedKeys.push_back(item.key);

        double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsedMs >= budgetMs) break;
    }

    // Report progress every pump call (not just when something arrived) so the
    // UI shows movement while textures are still decoding on the worker thread,
    // before the first one is ever popped here. completedCount() counts decode
    // failures too, so this always reaches 100% and clears via end() below.
    int enqueued  = streamer_.enqueuedCount();
    int completed = streamer_.completedCount();
    if (enqueued > 0) {
        if (completed >= enqueued) TaskProgress::end(kTextureStreamTaskLabel);
        else                       TaskProgress::report(kTextureStreamTaskLabel, completed, enqueued);
    }

    if (arrivedKeys.empty()) return;

    // Any material resolved before these textures arrived is bound to a default
    // map in an ALREADY-ALLOCATED descriptor set that may still be referenced by
    // a command buffer from a previous frame still executing on the GPU (the
    // renderer keeps multiple frames in flight — see Renderer::MAX_FRAMES —
    // and pumpTextureUploads only runs once per frame, before that frame's own
    // command buffer is recorded). Rewriting such a set's bindings in place while
    // it's in use is a Vulkan validation error (and undefined behavior without
    // validation). Drain all in-flight GPU work once before any rewrite so no
    // command buffer can still be referencing it.
    if (gpu_) gpu_->syncDevice();
    for (const std::string& key : arrivedKeys)
        materialCache_.refreshForTexture(key);
}

Texture* AssetManager::getDefaultTexture() const
{
    return defaultTexture.get();
}

RenderMesh* AssetManager::loadMesh(GpuDevice& gpu, const std::string& path)
{
    // Check if already loaded.
    auto it = meshes.find(path);
    if (it != meshes.end())
        return it->second.get();

    // ---------------------------------------------------------------------------
    // Load and parse OBJ file.
    // ---------------------------------------------------------------------------

    LoadedMesh loaded = ObjLoader::load(assets::resolveFilesystemPath(path));

    // ---------------------------------------------------------------------------
    // Create Vulkan mesh from loaded data.
    // ---------------------------------------------------------------------------

    auto mesh = std::make_unique<RenderMesh>(gpu, commandPool, loaded.vertices, loaded.indices);
    RenderMesh* meshPtr = mesh.get();
    meshes[path] = std::move(mesh);

    // TODO: Apply material data (loaded.material) to the mesh if needed.
    // For now, the caller can manually set texture via mesh->texture.

    return meshPtr;
}

RenderMesh* AssetManager::addMesh(GpuDevice& gpu, const std::string& cacheKey)
{
    // This is a placeholder for future use (e.g., dynamic mesh creation).
    // For Milestone 5, it's not used; Primitives are added directly to World.
    return nullptr;
}

#ifdef YOPE_EDITOR
#include <filesystem>
void AssetManager::onFileChanged(const std::string& absPath) {
    // Find cache key by checking if absPath ends with any cached key.
    // Cache keys are relative to YOPE_ASSETS_DIR.
    for (auto it = textures.begin(); it != textures.end(); ++it) {
        if (absPath.find(it->first) != std::string::npos) {
            // Evict and reload
            it->second.reset();
            if (gpu_) loadTexture(*gpu_, it->first);
            return;
        }
    }
    for (auto it = meshes.begin(); it != meshes.end(); ++it) {
        if (absPath.find(it->first) != std::string::npos) {
            it->second.reset();
            if (gpu_) loadMesh(*gpu_, it->first);
            return;
        }
    }
}
#endif
