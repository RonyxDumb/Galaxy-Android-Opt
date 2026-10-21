#include "gx/vk/vk_common.h"
#include "gx/vk_shader_inventory.h"

namespace gpu {

VkShaderModule VkRenderer::createShaderModule(const uint32_t* code, size_t sizeBytes) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = sizeBytes;
    info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(mDevice, &info, nullptr, &module);
    return module;
}

void VkRenderer::requestPipeline(const PipelineKey& key, const ShaderUid& uid) {
    // Keep the key pending until the render thread has published the result,
    // including the interval after the worker finishes. Every draw may ask.
    if (!mPipelinePending.insert(key).second) return;
    const bool knownUid = ShaderInventory::get().hasUid(uid);
    if (knownUid) ShaderInventory::get().recordRuntimePipelineKeyMiss(uid, key);
    else ShaderInventory::get().recordRuntimeShaderUidMiss(uid, key);
    {
        std::lock_guard<std::mutex> lock(mShaderMutex);
        mShaderQueue.push_back({key, uid, knownUid});
    }
    mShaderCv.notify_one();
}

void VkRenderer::startShaderWorker() {
    if (mShaderThread.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mShaderMutex);
        mShaderStop = false;
    }
    mShaderThread = std::thread([this]() {
        for (;;) {
            PipelineRequest request;
            {
                std::unique_lock<std::mutex> lock(mShaderMutex);
                mShaderCv.wait(lock, [this]() { return mShaderStop || !mShaderQueue.empty(); });
                if (mShaderStop) return;
                request = mShaderQueue.front();
                mShaderQueue.pop_front();
            }
            CompiledShader result{};
            result.uid = request.uid;
            result.key = request.key;
            result.knownUid = request.knownUid;
            auto start = std::chrono::steady_clock::now();
            std::vector<uint32_t> vs, fs;
            result.ok = lookupVulkanShader(request.uid, vs, fs) || compileVulkanShader(request.uid, vs, fs);
            if (result.ok) result.pipeline = buildPipeline(request.key, vs, fs);
            result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::lock_guard<std::mutex> lock(mShaderMutex);
            mShaderDone.push_back(std::move(result));
        }
    });
}

void VkRenderer::stopShaderWorker() {
    {
        std::lock_guard<std::mutex> lock(mShaderMutex);
        mShaderStop = true;
        mShaderQueue.clear();
    }
    mShaderCv.notify_all();
    if (mShaderThread.joinable()) mShaderThread.join();
    // A build already running may finish after stop was requested. Dispose of
    // its unpublished handle before render-pass/device destruction.
    for (const auto& result : mShaderDone) {
        if (result.pipeline) vkDestroyPipeline(mDevice, result.pipeline, nullptr);
    }
    mShaderDone.clear();
    mPipelinePending.clear();
    mPipelineFailed.clear();
    mShaderFailed.clear();
    mPipelinesLoading = false;
}

void VkRenderer::drainCompiledShaders() {
    std::vector<CompiledShader> done;
    {
        std::lock_guard<std::mutex> lock(mShaderMutex);
        done.swap(mShaderDone);
    }
    for (const auto& result : done) {
        mPipelinePending.erase(result.key);
        if (!result.pipeline) {
            mPipelineFailed.insert(result.key);
            if (!result.ok) mShaderFailed.insert(result.uid);
            LOGE("VkRenderer: exact pipeline failed (shader %016llx)",
                 (unsigned long long)ShaderUidHash()(result.uid));
            continue;
        }
        auto inserted = mPipelines.emplace(result.key, result.pipeline);
        if (!inserted.second) vkDestroyPipeline(mDevice, result.pipeline, nullptr);
        mShaderCompiles++;
        mRuntimePipelineCreates++;
        if (result.knownUid) mRuntimeCreatesKnownUid++;
        LOGI("VkRenderer: exact pipeline ready off-thread in %.1f ms", result.ms);
    }
}

}  // namespace gpu
