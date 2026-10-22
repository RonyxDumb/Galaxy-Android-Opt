#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::createPresentationPipeline() {
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mPresentDescLayout) != VK_SUCCESS) {
        return false;
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &mPresentDescLayout;

    if (vkCreatePipelineLayout(mDevice, &plci, nullptr, &mPresentPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(mDevice, &sci, nullptr, &mPresentSampler) != VK_SUCCESS) {
        return false;
    }

    const char* vsSrc = R"(#version 450 core
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
    const char* fsSrcFast = R"(#version 450 core
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D s_target;
void main() {
    outColor = vec4(texture(s_target, v_uv).rgb, 1.0);
}
)";

    // Preserve the Wii image; no extra FXAA, saturation or bloom pass.
    std::vector<uint32_t> vsSpirv, fsFastSpirv;
    std::string err;
    if (!compileShaderToSPIRV(vsSrc, true, vsSpirv, err) ||
        !compileShaderToSPIRV(fsSrcFast, false, fsFastSpirv, err)) {
        LOGE("Failed to compile presentation fast shaders: %s", err.c_str());
        return false;
    }

    VkShaderModule vsMod = createShaderModule(vsSpirv.data(), vsSpirv.size() * 4);
    VkShaderModule fsFastMod = createShaderModule(fsFastSpirv.data(), fsFastSpirv.size() * 4);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsFastMod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vici{};
    vici.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo iaci{};
    iaci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpsi{};
    vpsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpsi.viewportCount = 1;
    vpsi.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsci{};
    rsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsci.polygonMode = VK_POLYGON_MODE_FILL;
    rsci.cullMode = VK_CULL_MODE_NONE;
    rsci.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsci.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAttachment{};
    cbAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cbci{};
    cbci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbci.attachmentCount = 1;
    cbci.pAttachments = &cbAttachment;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsci{};
    dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsci.dynamicStateCount = 2;
    dsci.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vici;
    pci.pInputAssemblyState = &iaci;
    pci.pViewportState = &vpsi;
    pci.pRasterizationState = &rsci;
    pci.pMultisampleState = &msci;
    pci.pColorBlendState = &cbci;
    pci.pDynamicState = &dsci;
    pci.layout = mPresentPipelineLayout;
    pci.renderPass = mPresentRenderPass;

    VkResult res = vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &pci, nullptr, &mPresentPipeline);
    if (res != VK_SUCCESS || mPresentPipeline == VK_NULL_HANDLE) {
        if (mPipelineCache != VK_NULL_HANDLE) {
            res = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &mPresentPipeline);
        }
    }

    vkDestroyShaderModule(mDevice, vsMod, nullptr);
    vkDestroyShaderModule(mDevice, fsFastMod, nullptr);

    return res == VK_SUCCESS;
}

void VkRenderer::destroyPresentationPipeline() {
    if (mDevice != VK_NULL_HANDLE) {
        if (mPresentPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(mDevice, mPresentPipeline, nullptr);
            mPresentPipeline = VK_NULL_HANDLE;
        }
        if (mPresentPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(mDevice, mPresentPipelineLayout, nullptr);
            mPresentPipelineLayout = VK_NULL_HANDLE;
        }
        if (mPresentDescLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(mDevice, mPresentDescLayout, nullptr);
            mPresentDescLayout = VK_NULL_HANDLE;
        }
        if (mPresentSampler != VK_NULL_HANDLE) {
            vkDestroySampler(mDevice, mPresentSampler, nullptr);
            mPresentSampler = VK_NULL_HANDLE;
        }
        mPresentDescSets.clear();
    }
}

}  // namespace gpu
