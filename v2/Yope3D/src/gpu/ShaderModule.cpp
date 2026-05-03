#include "ShaderModule.h"
#include <fstream>
#include <stdexcept>
#include <vector>

ShaderModule::ShaderModule(VkDevice device, const std::string& spvPath) : device(device) {
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Failed to open shader: " + spvPath);

    auto size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

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
