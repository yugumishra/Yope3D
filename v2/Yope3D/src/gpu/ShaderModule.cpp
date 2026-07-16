#include "ShaderModule.h"
#include <fstream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <cstring>

#ifdef YOPE_EMBED_ASSETS
#include "generated/embedded_assets.h"
#endif

ShaderModule::ShaderModule(VkDevice device, const std::string& spvPath) : device(device) {
    // spvPath is YOPE_SHADER_DIR-rooted (compiled_shaders/, a separate tree from
    // assets/), so this doesn't go through assets::readBytes — that resolver's
    // filesystem fallback is assets/-rooted. Shaders are always embedded
    // (both PARTIAL and FULL scope), keyed by filename since compiled_shaders/ is flat.
    std::vector<char> code;
    size_t size = 0;

#ifdef YOPE_EMBED_ASSETS
    std::string shaderName = std::filesystem::path(spvPath).filename().string();
    EmbeddedAsset asset = getEmbeddedAsset(shaderName.c_str());
    if (asset.data) {
        code.resize(asset.size);
        std::memcpy(code.data(), asset.data, asset.size);
        size = asset.size;
    }
#endif

    if (size == 0) {
        std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("Failed to open shader: " + spvPath);

        size = static_cast<size_t>(file.tellg());
        code.resize(size);
        file.seekg(0);
        file.read(code.data(), static_cast<std::streamsize>(size));
    }

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module: " + spvPath);
}

ShaderModule::~ShaderModule() {
    if (module != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, module, nullptr);
}
