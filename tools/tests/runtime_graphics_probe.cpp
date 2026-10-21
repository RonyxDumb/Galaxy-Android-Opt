#include "gx/vk/vk_common.h"
#include "gx/vk_shader_inventory.h"
#include "gx/vk_shader_manifest.h"
#include "port/pause_state.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <filesystem>

static std::atomic<int> built{0}, destroyed{0}, compiled{0};
static std::mutex buildLock;
static std::condition_variable buildCv;
static bool holdBuild = false, entered = false, failDriver = false;
static std::thread::id renderThread;
static std::unordered_set<gpu::ShaderUid, gpu::ShaderUidHash> spirv;

extern "C" void port_log(const char*, ...) {}
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }
extern "C" VkResult vkCreateShaderModule(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*) { return VK_ERROR_DEVICE_LOST; }
extern "C" void vkDestroyPipeline(VkDevice, VkPipeline pipeline, const VkAllocationCallbacks*) { assert(pipeline); destroyed++; }

namespace gpu {
bool lookupVulkanShader(const ShaderUid& uid, std::vector<uint32_t>& vs, std::vector<uint32_t>& fs) {
    if (!spirv.count(uid)) return false;
    vs = fs = {1}; return true;
}
bool compileVulkanShader(const ShaderUid& uid, std::vector<uint32_t>& vs, std::vector<uint32_t>& fs) {
    assert(std::this_thread::get_id() != renderThread);
    compiled++;
    if (uid.numTexGens == 99) return false;
    spirv.insert(uid); vs = fs = {1}; return true;
}
VkPipeline VkRenderer::buildPipeline(const PipelineKey&, const std::vector<uint32_t>&, const std::vector<uint32_t>&) {
    assert(std::this_thread::get_id() != renderThread);
    std::unique_lock<std::mutex> lock(buildLock);
    entered = true; buildCv.notify_all();
    buildCv.wait(lock, [] { return !holdBuild; });
    if (failDriver) return VK_NULL_HANDLE;
    return (VkPipeline)(uintptr_t)(++built);
}
void VkRenderer::savePipelineCache() {}
void VkRenderer::markFatal(const char*, const char*, int) { mFatal = true; }
void VkRenderer::shutdown() {
    stopShaderWorker();
    for (const auto& kv : mPipelines) vkDestroyPipeline(mDevice, kv.second, nullptr);
    mPipelines.clear();
}
} // namespace gpu

static void waitForResults(gpu::VkRenderer& r, size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        { std::lock_guard<std::mutex> lock(r.mShaderMutex); if (r.mShaderDone.size() >= count) return; }
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

static gpu::Prepared scene() {
    gpu::Prepared p{};
    gpu::ShaderUid uid{}; uid.vtxFlags = gpu::VF_CLR0; uid.alphaFunc = 0x3f;
    p.uids.push_back(uid);
    gpu::Item item{}; item.type = gpu::IT_DRAW; item.idxCount = 3; item.flags = uid.vtxFlags;
    p.items.push_back(item);
    item.depthTest = item.depthWrite = 1;
    p.items.push_back(item); // Same UID, distinct exact pipeline state.
    return p;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    renderThread = std::this_thread::get_id();
    if (!strcmp(argv[1], "pause")) {
        PortPauseState pause;
        assert(!pause.isPaused());
        pause.setRenderLoading(true); assert(pause.isPaused());
        pause.setLifecyclePaused(true);
        pause.setRenderLoading(false); assert(pause.isPaused());
        pause.setRenderLoading(true);
        pause.setLifecyclePaused(false); assert(pause.isPaused());
        pause.setRenderLoading(false); assert(!pause.isPaused());
        return 0;
    }
    gpu::VkRenderer r;
    gpu::Prepared frame = scene();
    if (!strcmp(argv[1], "deduplicate")) {
        r.startShaderWorker();
        assert(!r.prepareFramePipelines(frame, gpu::HudMode::Inline));
        assert(r.waitingForPipelines());
        assert(!r.mDrawPipelines[0] && !r.mDrawPipelines[1]);
        waitForResults(r, 2);
        // Completion alone does not remove the pending marker: repeated asks
        // before publishing must not queue another compile or driver build.
        for (int i = 0; i < 1000; i++) assert(!r.getPipeline(frame.items[0], frame.uids[0]));
        assert(r.mPipelinePending.size() == 2 && r.mPipelines.empty());
        assert(r.prepareFramePipelines(frame, gpu::HudMode::Inline));
        assert(!r.waitingForPipelines() && r.mPipelines.size() == 2);
        assert(r.mDrawPipelines[0] && r.mDrawPipelines[1] && r.mDrawPipelines[0] != r.mDrawPipelines[1]);
        assert(built == 2 && compiled == 1);
        auto stats = gpu::ShaderInventory::get().stats();
        assert(stats.runtimeDiscoveredEntries == 2);
        for (int i = 0; i < 100; i++) assert(r.prepareFramePipelines(frame, gpu::HudMode::Inline));
        assert(built == 2);
    } else if (!strcmp(argv[1], "shutdown")) {
        holdBuild = true;
        r.startShaderWorker();
        r.prepareFramePipelines(frame, gpu::HudMode::Inline);
        { std::unique_lock<std::mutex> lock(buildLock); buildCv.wait(lock, [] { return entered; }); }
        std::thread release([] { std::lock_guard<std::mutex> lock(buildLock); holdBuild = false; buildCv.notify_all(); });
        r.stopShaderWorker(); release.join();
        assert(destroyed == built && r.mPipelinePending.empty() && r.mShaderDone.empty());
    } else if (!strcmp(argv[1], "failure")) {
        failDriver = true;
        r.startShaderWorker();
        assert(!r.prepareFramePipelines(frame, gpu::HudMode::Inline));
        waitForResults(r, 2);
        assert(!r.prepareFramePipelines(frame, gpu::HudMode::Inline));
        assert(r.hasFailed() && !r.waitingForPipelines() && r.mPipelines.empty());
    } else if (!strcmp(argv[1], "inventory")) {
        const auto dir = std::filesystem::temp_directory_path() / ("galaxy-inventory-" + std::to_string((uintptr_t)&r));
        std::filesystem::create_directories(dir);
        gpu::ShaderInventory inv;
        inv.init(dir.string());
        const auto path = dir / "vk_shader_inventory.bin";
        const gpu::PipelineKey key = gpu::pipelineKeyFor(frame.items[0], frame.uids[0]);
        for (int i = 0; i < 10000; i++) inv.recordRuntimeShaderUidMiss(frame.uids[0], key);
        assert(inv.stats().runtimeDiscoveredEntries == 1 && !std::filesystem::exists(path));
        inv.saveInventory(); assert(std::filesystem::is_regular_file(path));
        std::vector<gpu::ManifestStagedEntry> loaded; uint32_t version = 0;
        assert(gpu::parseAssetManifest(path.string(), loaded, version) && loaded.size() == 1);
        std::filesystem::remove_all(dir);
    } else { return 2; }
}
