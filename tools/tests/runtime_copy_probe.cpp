#include "gx/vk/vk_common.h"
#include <cassert>

static VkImageBlit copied{};
static VkClearRect cleared{};
static uint32_t clearCalls = 0, copyCalls = 0, aspects = 0;
static gpu::VkCopyTex texture{};

extern "C" void vkCmdEndRenderPass(VkCommandBuffer) {}
extern "C" void vkCmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents) {}
extern "C" void vkCmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
    uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*) {}
extern "C" void vkCmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType) {}
extern "C" void vkCmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
    VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
    uint32_t, const VkImageMemoryBarrier*) {}
extern "C" void vkCmdBlitImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
    uint32_t n, const VkImageBlit* regions, VkFilter) {
    assert(n == 1); copied = regions[0]; copyCalls++;
}
extern "C" void vkCmdClearAttachments(VkCommandBuffer, uint32_t n, const VkClearAttachment* a,
    uint32_t count, const VkClearRect* rect) {
    assert(count == 1); cleared = *rect; clearCalls++;
    for (uint32_t i = 0; i < n; i++) aspects |= a[i].aspectMask;
}

namespace gpu {
void VkRenderer::shutdown() {}
VkCopyTex& VkRenderer::getCopyTex(uint64_t, int w, int h) {
    texture.w = w; texture.h = h; texture.image = (VkImage)(uintptr_t)1;
    return texture;
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    gpu::VkRenderer renderer;
    gpu::VkTarget target{}; target.width = 640; target.height = 528;
    gpu::Item item{};
    item.copy[1] = 12 | (18 << 10);
    item.copy[2] = 100 | (50 << 10); // 101 x 51 source, odd dimensions.
    item.copy[9] = 1 << 11; // Clear after texture copy.
    item.copyCmode = (1 << 3) | (1 << 4);
    item.copyZmode = 1 << 4;
    bool half = !strcmp(argv[1], "half"), scaled = !strcmp(argv[1], "clipped");
    if (half || scaled) item.copy[9] |= 1 << 9;
    if (scaled) {
        item.copy[1] = 300 | (250 << 10);
        item.copy[2] = 99 | (49 << 10);
    }
    if (!strcmp(argv[1], "no-clear")) item.copy[9] &= ~(1 << 11);
    bool inPass = true;
    gpu::Prepared frame{}; frame.number = 42;
    gpu::FrameResource resource{};
    VkRenderPassBeginInfo rp{};
    renderer.executeCopyItem(VK_NULL_HANDLE, item, frame, target,
                             scaled ? 2.0f : 1.0f, scaled ? 2.0f : 1.0f, inPass, rp, resource);
    assert(copyCalls == 1 && texture.frame == 42);
    const int w = scaled ? 40 : 101, h = scaled ? 28 : 51;
    assert(copied.srcOffsets[1].x - copied.srcOffsets[0].x == w);
    assert(copied.srcOffsets[1].y - copied.srcOffsets[0].y == h);
    assert(copied.dstOffsets[1].x == ((half || scaled) ? (w + 1) / 2 : w));
    assert(copied.dstOffsets[1].y == ((half || scaled) ? (h + 1) / 2 : h));
    if (!strcmp(argv[1], "no-clear")) { assert(clearCalls == 0); return 0; }
    assert(clearCalls == 1 && inPass);
    assert(cleared.rect.offset.x == (scaled ? 600 : 12));
    assert(cleared.rect.offset.y == (scaled ? 500 : 18));
    assert(cleared.rect.extent.width == (uint32_t)w && cleared.rect.extent.height == (uint32_t)h);
    assert(aspects == (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT));
}
