#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::growSamplerPool() {
    constexpr uint32_t sets = 512;
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets * 8},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets},
    };
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = sets;
    info.poolSizeCount = 2;
    info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(mDevice, &info, nullptr, &pool) != VK_SUCCESS) return false;
    mSamplerPools.push_back(pool);
    mDescriptorPool = pool;
    return true;
}

VkDescriptorSet VkRenderer::allocateImageSet(VkDescriptorSetLayout layout, VkDescriptorPool& pool) {
    auto allocate = [&](VkDescriptorPool candidate, VkDescriptorSet& set) {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = candidate;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        return vkAllocateDescriptorSets(mDevice, &info, &set);
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (auto candidate : mSamplerPools) {
        const VkResult result = allocate(candidate, set);
        if (result == VK_SUCCESS) { pool = candidate; return set; }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) return VK_NULL_HANDLE;
    }
    // A pool reset here would invalidate commands already recorded this frame.
    if (!growSamplerPool() || allocate(mDescriptorPool, set) != VK_SUCCESS) return VK_NULL_HANDLE;
    pool = mDescriptorPool;
    return set;
}

void VkRenderer::releaseRetiredImageSets() {
    // Caller has waited for both render and presentation submissions.
    for (const auto& entry : mRetiredImageSets) {
        if (entry.pool && entry.set) vkFreeDescriptorSets(mDevice, entry.pool, 1, &entry.set);
    }
    mRetiredImageSets.clear();
}

}  // namespace gpu
