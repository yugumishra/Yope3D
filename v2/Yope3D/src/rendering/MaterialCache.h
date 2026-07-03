#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <memory>
#include <string>

class AssetManager;
class Texture;
namespace ecs { struct Material; }

// Runtime GPU handle for a resolved material: a set-1 descriptor set binding the
// five PBR maps (albedo / normal / metalRough / occlusion / emissive). Held
// (non-owning) by ecs::Material::resolved. The PBR scalar factors are NOT stored
// here — they travel via push constants straight from the component.
struct ResolvedMaterial {
    VkDescriptorSet set = VK_NULL_HANDLE;
};

// ---------------------------------------------------------------------------
// MaterialCache
//
// Owns the descriptor pool for material sets (set 1, 5 combined-image-samplers)
// and deduplicates them. Two entry points:
//   - defaultSetFor(albedo): a set with the given albedo (or default white) and
//     engine defaults for every other slot. Used for meshes with no Material.
//   - resolve(material): loads the material's five maps (substituting defaults
//     for empty paths) and builds/fetches a cached set keyed by the map paths.
//
// Owned by AssetManager; shares the materialSetLayout handle created by Renderer.
// ---------------------------------------------------------------------------

class MaterialCache {
public:
    void init(VkDevice device, VkDescriptorSetLayout materialSetLayout, AssetManager* assets);
    void cleanup(VkDevice device);

    VkDescriptorSet   defaultSetFor(Texture* albedo);
    ResolvedMaterial* resolve(const ecs::Material& m);

    MaterialCache() = default;
    MaterialCache(const MaterialCache&) = delete;
    MaterialCache& operator=(const MaterialCache&) = delete;

private:
    VkDescriptorSet allocateSet();
    void writeSet(VkDescriptorSet set, Texture* albedo, Texture* normal,
                  Texture* metalRough, Texture* occlusion, Texture* emissive);

    VkDevice              device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool      pool_   = VK_NULL_HANDLE;
    AssetManager*         assets_ = nullptr;

    std::unordered_map<Texture*, VkDescriptorSet>                     defaultSets_;
    std::unordered_map<std::string, std::unique_ptr<ResolvedMaterial>> materialSets_;
};
