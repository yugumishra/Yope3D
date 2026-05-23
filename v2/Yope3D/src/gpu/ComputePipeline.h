#pragma once
#include <vulkan/vulkan.h>

// ---------------------------------------------------------------------------
// ComputePipeline — thin RAII wrapper around VkPipeline for compute shaders.
// The caller owns and provides the VkPipelineLayout.
// ---------------------------------------------------------------------------

class ComputePipeline {
public:
    ComputePipeline() = default;
    ComputePipeline(VkDevice device, VkPipelineLayout layout, VkShaderModule shaderModule);
    ~ComputePipeline();

    ComputePipeline(ComputePipeline&&) noexcept;
    ComputePipeline& operator=(ComputePipeline&&) noexcept;
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    VkPipeline get() const { return pipeline_; }

private:
    VkDevice   device_   = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
