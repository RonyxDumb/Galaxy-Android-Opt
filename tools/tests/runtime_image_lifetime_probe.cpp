#include "gx/vk/vk_common.h"
#include <cassert>
#include <map>

static uintptr_t nextHandle = 10;
static std::map<VkDescriptorPool, int> pools;
static std::map<VkDescriptorSet, VkDescriptorPool> sets;
static std::unordered_set<VkImageView> destroyedViews;
static bool fenceComplete = false, failAllocation = false, failBinding = false;
static int freed = 0;
template <class T> T handle() { return (T)++nextHandle; }

extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }
extern "C" VkResult vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo* i,
    const VkAllocationCallbacks*, VkDescriptorPool* out) {
    assert(i->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    assert(i->maxSets == 512 && i->poolSizeCount == 2);
    *out = handle<VkDescriptorPool>(); pools[*out] = 0; return VK_SUCCESS;
}
extern "C" VkResult vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo* i, VkDescriptorSet* out) {
    if (failAllocation) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (pools.at(i->descriptorPool) == 2) return VK_ERROR_OUT_OF_POOL_MEMORY;
    *out = handle<VkDescriptorSet>(); sets[*out] = i->descriptorPool; pools[i->descriptorPool]++; return VK_SUCCESS;
}
extern "C" VkResult vkResetDescriptorPool(VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags) {
    assert(false && "pool reset invalidates earlier recorded commands"); return VK_ERROR_UNKNOWN;
}
extern "C" VkResult vkFreeDescriptorSets(VkDevice, VkDescriptorPool pool, uint32_t count, const VkDescriptorSet* s) {
    assert(fenceComplete && count == 1 && sets.at(*s) == pool);
    sets.erase(*s); pools[pool]--; freed++; return VK_SUCCESS;
}
extern "C" void vkUpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const VkCopyDescriptorSet*) {}
extern "C" void vkDestroyImageView(VkDevice, VkImageView view, const VkAllocationCallbacks*) {
    // Newly allocated, unused images may be rolled back before the fence.
    destroyedViews.insert(view);
}
extern "C" void vkDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) {}
extern "C" void vkFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) {}
extern "C" VkResult vkCreateImage(VkDevice, const VkImageCreateInfo* i, const VkAllocationCallbacks*, VkImage* out) {
    assert(i->usage & VK_IMAGE_USAGE_STORAGE_BIT); *out = handle<VkImage>(); return VK_SUCCESS;
}
extern "C" void vkGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* out) {
    out->size = 4096; out->memoryTypeBits = 1;
}
extern "C" VkResult vkAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* out) {
    *out = handle<VkDeviceMemory>(); return VK_SUCCESS;
}
extern "C" VkResult vkBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) {
    return failBinding ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
extern "C" VkResult vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* out) {
    *out = handle<VkImageView>(); return VK_SUCCESS;
}
namespace gpu {
void VkRenderer::shutdown() {}
void VkRenderer::markFatal(const char*, const char*, int) { mFatal = true; }
uint32_t VkRenderer::findMemoryType(uint32_t, VkMemoryPropertyFlags) { return 0; }
}

static VkDescriptorSet sample(gpu::VkRenderer& r, VkImageView view) {
    std::array<VkImageView, 8> views{}; views.fill(view);
    std::array<VkSampler, 8> samplers{};
    return r.getSamplerDescriptorSet(views, samplers);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    gpu::VkRenderer r; r.mDevice = handle<VkDevice>(); r.mTextureDescLayout = handle<VkDescriptorSetLayout>();
    const VkImageView a = handle<VkImageView>(), b = handle<VkImageView>(), c = handle<VkImageView>();
    if (!strcmp(argv[1], "saturation")) {
        auto one = sample(r, a), two = sample(r, b), three = sample(r, c);
        assert(one && two && three && pools.size() == 2 && sets.size() == 3 && freed == 0);
        assert(sample(r, a) == one && sets.count(one));
        auto before = pools.size(); failAllocation = true;
        assert(!sample(r, handle<VkImageView>()) && pools.size() == before && sets.size() == 3);
    } else if (!strcmp(argv[1], "retire")) {
        auto old = sample(r, a); auto epoch = r.mSamplerDescEpoch;
        r.invalidateSamplerDescSetsForView(a);
        assert(r.mSamplerDescSets.empty() && r.mSamplerDescEpoch > epoch && sets.count(old) && freed == 0);
        auto fresh = sample(r, a); assert(fresh != old);
        fenceComplete = true; r.releaseRetiredImageSets();
        assert(!sets.count(old) && sets.count(fresh) && freed == 1);
    } else if (!strcmp(argv[1], "deleted-view")) {
        auto old = sample(r, a); sample(r, b);
        gpu::queueVkTextureDelete(1, 2, uint64_t(a));
        assert(sets.count(old) && destroyedViews.empty());
        fenceComplete = true; r.flushDeferredDeletes();
        assert(!sets.count(old) && destroyedViews.count(a) && r.mSamplerDescSets.size() == 1);
        assert(sample(r, a) != old); // A reused view handle must never reuse the stale set.
    } else if (!strcmp(argv[1], "copy-retention")) {
        auto frame = std::make_shared<gpu::Frame>(); r.mCurrentFrame = frame;
        const auto key = gpu::copyKey(0, 0x80001000);
        r.mCopies[key].view = a; r.mCopies[key].frame = 1;
        r.mCopies[gpu::kXfbCopyKey].view = b;
        frame->liveEfbCopies.push_back(0x80001000);
        r.evictCopies(100000); assert(r.mCopies.size() == 2);
        frame->liveEfbCopies.clear(); r.evictCopies(100001);
        assert(r.mCopies.size() == 1 && r.mCopies.count(gpu::kXfbCopyKey) && destroyedViews.empty());
        fenceComplete = true; r.flushDeferredDeletes(); assert(destroyedViews.count(a));
    } else if (!strcmp(argv[1], "resize")) {
        const auto key = gpu::copyKey(0, 0x1000);
        auto first = r.getCopyTex(key, 64, 64); auto oldSet = sample(r, first.view);
        auto second = r.getCopyTex(key, 32, 32);
        assert(second.view != first.view && sets.count(oldSet) && destroyedViews.empty());
        failBinding = true;
        assert(!r.getCopyTex(key, 16, 16).view && r.mFatal);
        assert(r.mCopies.at(key).view == second.view && r.mCopies.at(key).w == 32);
        assert(!destroyedViews.count(first.view) && !destroyedViews.count(second.view));
        fenceComplete = true; r.flushDeferredDeletes();
        assert(!sets.count(oldSet) && destroyedViews.count(first.view) && !destroyedViews.count(second.view));
    } else assert(false);
}
