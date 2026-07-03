#include "MaterialCache.h"
#include "../assets/AssetManager.h"
#include "../gpu/Texture.h"
#include "../ecs/Components.h"
#include <stdexcept>

void MaterialCache::init(VkDevice device, VkDescriptorSetLayout materialSetLayout, AssetManager* assets) {
    device_ = device;
    layout_ = materialSetLayout;
    assets_ = assets;

    // 5 combined-image-samplers per set; reserve room for 256 distinct materials.
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 256 * 5;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &poolSize;
    ci.maxSets       = 256;

    if (vkCreateDescriptorPool(device_, &ci, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create material descriptor pool");
}

void MaterialCache::cleanup(VkDevice device) {
    materialSets_.clear();
    defaultSets_.clear();
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

VkDescriptorSet MaterialCache::allocateSet() {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate material descriptor set");
    return set;
}

void MaterialCache::writeSet(VkDescriptorSet set, Texture* albedo, Texture* normal,
                             Texture* metalRough, Texture* occlusion, Texture* emissive) {
    Texture* texs[5] = { albedo, normal, metalRough, occlusion, emissive };
    VkDescriptorImageInfo infos[5]{};
    VkWriteDescriptorSet   writes[5]{};
    for (int i = 0; i < 5; ++i) {
        infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infos[i].imageView   = texs[i]->getImageView();
        infos[i].sampler     = texs[i]->getSampler();

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = set;
        writes[i].dstBinding      = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &infos[i];
    }
    vkUpdateDescriptorSets(device_, 5, writes, 0, nullptr);
}

VkDescriptorSet MaterialCache::defaultSetFor(Texture* albedo) {
    Texture* key = albedo ? albedo : assets_->getDefaultAlbedo();
    auto it = defaultSets_.find(key);
    if (it != defaultSets_.end()) return it->second;

    VkDescriptorSet set = allocateSet();
    writeSet(set, key,
             assets_->getDefaultNormal(),
             assets_->getDefaultMetalRough(),
             assets_->getDefaultOcclusion(),
             assets_->getDefaultEmissive());
    defaultSets_[key] = set;
    return set;
}

namespace {
Texture* loadOrDefault(AssetManager* a, const char* path, bool srgb, Texture* def) {
    if (!path || path[0] == '\0') return def;
    try { return a->loadTextureSrgb(path, srgb); }
    catch (...) { return def; }
}
} // namespace

ResolvedMaterial* MaterialCache::resolve(const ecs::Material& m) {
    // The descriptor set depends only on the five map paths (scalar factors ride
    // push constants), so the cache key is the concatenation of those paths.
    std::string key = std::string(m.albedoPath) + "|" + m.normalPath + "|" +
                      m.metalRoughPath + "|" + m.occlusionPath + "|" + m.emissivePath;
    auto it = materialSets_.find(key);
    if (it != materialSets_.end()) return it->second.get();

    Texture* albedo     = loadOrDefault(assets_, m.albedoPath,     true,  assets_->getDefaultAlbedo());
    Texture* normal     = loadOrDefault(assets_, m.normalPath,     false, assets_->getDefaultNormal());
    Texture* metalRough = loadOrDefault(assets_, m.metalRoughPath, false, assets_->getDefaultMetalRough());
    Texture* occlusion  = loadOrDefault(assets_, m.occlusionPath,  false, assets_->getDefaultOcclusion());
    Texture* emissive   = loadOrDefault(assets_, m.emissivePath,   true,  assets_->getDefaultEmissive());

    VkDescriptorSet set = allocateSet();
    writeSet(set, albedo, normal, metalRough, occlusion, emissive);

    auto rm = std::make_unique<ResolvedMaterial>();
    rm->set = set;
    ResolvedMaterial* ptr = rm.get();
    materialSets_[key] = std::move(rm);
    return ptr;
}
