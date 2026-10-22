#include "gx/vk/vk_common.h"
#include "gx/gx_efb_shader.h"
#include <shaderc/shaderc.hpp>

namespace gpu {

bool VkRenderer::createEfbCopyPipeline() {
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo descriptor{};
    descriptor.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor.bindingCount = 3;
    descriptor.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(mDevice, &descriptor, nullptr, &mCopyDescLayout) != VK_SUCCESS) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &mCopyDescLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(mDevice, &layout, nullptr, &mCopyPipelineLayout) != VK_SUCCESS) return false;
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    auto result = compiler.CompileGlslToSpv(efbCopyComputeSource(), shaderc_glsl_compute_shader, "efb-copy.comp", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        LOGE("EFB copy shader: %s", result.GetErrorMessage().c_str());
        return false;
    }
    std::vector<uint32_t> code(result.cbegin(), result.cend());
    VkShaderModule module = createShaderModule(code.data(), code.size() * 4);
    if (!module) return false;
    VkComputePipelineCreateInfo pipeline{};
    pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline.layout = mCopyPipelineLayout;
    pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = module;
    pipeline.stage.pName = "main";
    VkResult status = vkCreateComputePipelines(mDevice, mPipelineCache, 1, &pipeline, nullptr, &mCopyPipeline);
    vkDestroyShaderModule(mDevice, module, nullptr);
    if (status != VK_SUCCESS) return false;
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return vkCreateSampler(mDevice, &sampler, nullptr, &mCopySampler) == VK_SUCCESS;
}

void VkRenderer::destroyEfbCopyPipeline() {
    if (mCopyPipeline) vkDestroyPipeline(mDevice, mCopyPipeline, nullptr);
    if (mCopySampler) vkDestroySampler(mDevice, mCopySampler, nullptr);
    if (mCopyPipelineLayout) vkDestroyPipelineLayout(mDevice, mCopyPipelineLayout, nullptr);
    if (mCopyDescLayout) vkDestroyDescriptorSetLayout(mDevice, mCopyDescLayout, nullptr);
    mCopyPipeline = VK_NULL_HANDLE;
    mCopySampler = VK_NULL_HANDLE;
    mCopyPipelineLayout = VK_NULL_HANDLE;
    mCopyDescLayout = VK_NULL_HANDLE;
}

}  // namespace gpu
