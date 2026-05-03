#pragma once
#include <vulkan/vulkan.h>
#include <string>

class ShaderModule {
public:
    ShaderModule(VkDevice device, const std::string& spvPath);
    ~ShaderModule();

    VkShaderModule get() const { return module; }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

private:
    VkDevice       device = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;
};
