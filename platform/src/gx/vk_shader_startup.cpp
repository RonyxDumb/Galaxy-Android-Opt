#include "gx/vk_shader_inventory.h"
#include "gx/vk_renderer.h"
#include "gx/vk/vk_uber_pipeline.h"
#include "port/port.h"
#include <chrono>

namespace gpu {

std::vector<PipelineKey> ShaderInventory::startupPipelineKeys() const {
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<PipelineKey> keys;
    std::unordered_set<PipelineKey, PipelineKeyHash> seen;
    auto add = [&](const InventoryEntry& entry) {
        PipelineKey key = uberPipelineKey(entry.key);
        if (keys.size() < kStartupPipelineBudget && seen.insert(key).second) keys.push_back(key);
    };
    // These few families cover the title/UI. Do not compile every material in
    // the extracted manifest, nor the old fixed-colour placeholder catalogue.
    for (const auto& entry : mEntries) {
        if (entry.sourceAsset == "title_screen_and_ui" || entry.sourceAsset == "title_screen_3d_logo") add(entry);
    }
    // Reuse a small sample of previously observed states without making the
    // next launch proportional to the number of levels visited.
    for (const auto& entry : mEntries) {
        if (keys.size() == kStartupPipelineBudget) break;
        if (entry.isRuntimeDiscovered) add(entry);
    }
    return keys;
}

void ShaderInventory::prewarmStartupPipelines(VkRenderer& renderer) {
    const auto keys = startupPipelineKeys();
    const auto start = std::chrono::steady_clock::now();
    size_t ready = 0;
    for (const auto& key : keys) {
        const auto result = renderer.prewarmExactPipeline(key, key.uid);
        if (result == PrewarmResult::Created || result == PrewarmResult::Reused) ready++;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStartupReady = !keys.empty() && ready == keys.size();
        mStats.timePipelinePhaseMs = ms;
    }
    port_log("vk_shader_inventory: bounded startup %zu/%zu pipelines in %.1f ms (budget %zu)",
             ready, keys.size(), ms, kStartupPipelineBudget);
}

}  // namespace gpu
