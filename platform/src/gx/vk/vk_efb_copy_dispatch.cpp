#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::copyEfbTexture(VkCommandBuffer cmd, VkTarget& target, VkCopyTex& dst,
                               int x, int y, int w, int h, uint32_t ctrl, uint32_t pixelFormat) {
    if (!mCopyPipeline || !dst.view || !target.depthSampleView) return false;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = allocateImageSet(mCopyDescLayout, pool);
    if (!set) return false;
    mRetiredImageSets.push_back({set, mFrameNumber, pool});
    VkDescriptorImageInfo images[] = {
        {mCopySampler, target.colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {mCopySampler, target.depthSampleView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, dst.view, VK_IMAGE_LAYOUT_GENERAL},
    };
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(mDevice, 3, writes, 0, nullptr);
    VkImageMemoryBarrier barriers[3]{};
    for (auto& b : barriers) {
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    barriers[0].image = target.colorImage;
    barriers[0].oldLayout = target.colorLayout;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].image = target.depthImage;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (mDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
        mDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT) barriers[1].subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[2].image = dst.image;
    barriers[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[2].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[2].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 3, barriers);
    int32_t state[8] = {x, y, w, h, dst.w, dst.h, int32_t(ctrl), int32_t(pixelFormat)};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mCopyPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mCopyPipelineLayout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, mCopyPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(state), state);
    vkCmdDispatch(cmd, (dst.w + 7) / 8, (dst.h + 7) / 8, 1);
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[2].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[2].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, 3, barriers);
    target.colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return true;
}

}  // namespace gpu
