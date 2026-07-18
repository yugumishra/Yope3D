#include "ComputePipeline.h"
#include <stdexcept>

ComputePipeline::ComputePipeline(VkDevice device, VkPipelineLayout layout, VkShaderModule shaderModule)
    : device_(device) {
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shaderModule;
    stage.pName  = "main";

    VkComputePipelineCreateInfo ci{};
    ci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage  = stage;
    ci.layout = layout;

    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute pipeline");
}

ComputePipeline::~ComputePipeline() {
    if (pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_, pipeline_, nullptr);
}

ComputePipeline::ComputePipeline(ComputePipeline&& o) noexcept
    : device_(o.device_), pipeline_(o.pipeline_) {
    o.device_   = VK_NULL_HANDLE;
    o.pipeline_ = VK_NULL_HANDLE;
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& o) noexcept {
    if (this != &o) {
        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
        device_   = o.device_;
        pipeline_ = o.pipeline_;
        o.device_   = VK_NULL_HANDLE;
        o.pipeline_ = VK_NULL_HANDLE;
    }
    return *this;
}
