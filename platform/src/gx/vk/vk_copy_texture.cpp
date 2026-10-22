#include "gx/vk/vk_common.h"

namespace gpu {

VkCopyTex& VkRenderer::getCopyTex(uint64_t key, int w, int h) {
    VkCopyTex& current = mCopies[key];
    if (current.image && current.view && current.w == w && current.h == h) return current;
    VkCopyTex next{};
    next.w = w; next.h = h;
    auto fail = [&](VkResult result) -> VkCopyTex& {
        if (next.view) vkDestroyImageView(mDevice, next.view, nullptr);
        if (next.image) vkDestroyImage(mDevice, next.image, nullptr);
        if (next.memory) vkFreeMemory(mDevice, next.memory, nullptr);
        markFatal("copy texture", "allocation failed", int(result));
        static VkCopyTex unavailable;
        return unavailable;
    };
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {uint32_t(w), uint32_t(h), 1};
    info.mipLevels = info.arrayLayers = 1;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    VkResult result = vkCreateImage(mDevice, &info, nullptr, &next.image);
    if (result != VK_SUCCESS) return fail(result);
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(mDevice, next.image, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation.memoryTypeIndex == ~0u) allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, 0);
    if (allocation.memoryTypeIndex == ~0u) return fail(VK_ERROR_FEATURE_NOT_PRESENT);
    result = vkAllocateMemory(mDevice, &allocation, nullptr, &next.memory);
    if (result != VK_SUCCESS) return fail(result);
    result = vkBindImageMemory(mDevice, next.image, next.memory, 0);
    if (result != VK_SUCCESS) return fail(result);
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = next.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    result = vkCreateImageView(mDevice, &view, nullptr, &next.view);
    if (result != VK_SUCCESS) return fail(result);
    // Replacements are published only when complete. Earlier commands keep
    // their images and descriptor sets until the next completed submission.
    invalidateSamplerDescSetsForView(current.view);
    queueVkTextureDelete(uint64_t(current.image), uint64_t(current.memory), uint64_t(current.view));
    current = next;
    return current;
}

}  // namespace gpu
