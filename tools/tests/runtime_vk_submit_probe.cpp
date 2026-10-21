#include <cassert>
#include <cstring>
#include "gx/vk/vk_render_submit.h"

static bool signaled = false;
static bool failReset = false, failSubmit = false;
static int submissions = 0, waits = 0;
extern "C" VkResult vkResetFences(VkDevice, uint32_t count, const VkFence*) {
    assert(count == 1);
    return failReset ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}
extern "C" VkResult vkQueueSubmit(VkQueue, uint32_t count, const VkSubmitInfo* info, VkFence) {
    submissions++;
    if (failSubmit) return VK_ERROR_DEVICE_LOST;
    assert(count == 1 && info->sType == VK_STRUCTURE_TYPE_SUBMIT_INFO);
    assert(info->commandBufferCount == 1 && info->pCommandBuffers);
    if (info->waitSemaphoreCount) {
        assert(info->waitSemaphoreCount == 1 && signaled);
        assert(*info->pWaitSemaphores == *info->pSignalSemaphores);
        assert(*info->pWaitDstStageMask == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        signaled = false;
        waits++;
    }
    assert(info->signalSemaphoreCount == 1 && !signaled);
    signaled = true;
    return VK_SUCCESS;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    auto submit = [](bool pending) {
        return gpu::submitRenderCommands(VK_NULL_HANDLE, VK_NULL_HANDLE,
            VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, pending);
    };
    if (!strcmp(argv[1], "skipped-present")) {
        assert(submit(false) == VK_SUCCESS);
        for (int i = 0; i < 100; i++) assert(submit(true) == VK_SUCCESS);
        assert(submissions == 101 && waits == 100);
        signaled = false; // Normal present consumed the last signal.
        assert(submit(false) == VK_SUCCESS);
        assert(waits == 100);
    } else if (!strcmp(argv[1], "failure")) {
        failReset = true;
        assert(submit(false) == VK_ERROR_DEVICE_LOST && submissions == 0);
        failReset = false; failSubmit = true;
        assert(submit(false) == VK_ERROR_DEVICE_LOST && submissions == 1);
    } else { return 2; }
}
