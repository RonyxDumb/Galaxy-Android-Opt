#pragma once

#include <vulkan/vulkan.h>

namespace gpu {

// A skipped present can leave the previous render signal unconsumed. A fence
// or vkQueueWaitIdle only completes that signal; neither unsignals a binary
// semaphore. Consume it before signaling the next frame, on the same queue.
inline VkResult submitRenderCommands(VkDevice device, VkQueue queue,
                                     VkCommandBuffer commands, VkFence fence,
                                     VkSemaphore renderReady, bool pendingSignal) {
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (pendingSignal) {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &renderReady;
        submitInfo.pWaitDstStageMask = &waitStage;
    }
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commands;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderReady;

    VkResult reset = vkResetFences(device, 1, &fence);
    if (reset != VK_SUCCESS) return reset;
    return vkQueueSubmit(queue, 1, &submitInfo, fence);
}

}  // namespace gpu
