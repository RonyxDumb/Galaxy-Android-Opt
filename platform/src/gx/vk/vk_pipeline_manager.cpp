#include "gx/vk/vk_common.h"
#include "gx/vk_shader_inventory.h"
#include "gx/vk/vk_pipeline_blend.h"
#include "gx/vk/vk_uber_pipeline.h"

namespace gpu {

PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid) {
    DecodedPipelineKey d{};
    d.uid = uid;
    d.vtxFlags = it.flags;
    d.depthTest = (it.depthTest != 0);
    d.depthWrite = (it.depthTest && it.depthWrite);
    d.depthCompareOp = toCanonicalDepthFunc(it.depthFunc);

    d.blendEnable = (it.blend != 0) || (it.dstAlpha != 0);
    d.srcColorBlendFactor = toCanonicalBlendFactor(it.blend ? it.srcRGB : 1);
    d.dstColorBlendFactor = toCanonicalBlendFactor(it.blend ? it.dstRGB : 0);
    d.colorBlendOp = toCanonicalBlendOp(it.blend ? it.eq : 0x8006);
    d.srcAlphaBlendFactor = toCanonicalBlendFactor(it.dstAlpha ? 0x8003 : (it.blend ? it.srcRGB : 1));
    d.dstAlphaBlendFactor = toCanonicalBlendFactor(it.dstAlpha ? 0 : (it.blend ? it.dstRGB : 0));
    d.alphaBlendOp = toCanonicalBlendOp(it.dstAlpha ? 0x8006 : (it.blend ? it.eq : 0x8006));
    d.colorWriteMask = it.colorMask & 0x0Fu;
    d.blendConstantAlpha = it.dstAlphaValue;

    d.cullMode = toCanonicalCullMode(it.cull != 0, it.cullFace);
    d.topology = toCanonicalTopology(it.prim);
    d.frontFace = CanonicalFrontFace::CounterClockwise;
    return encodePipelineKey(d);
}

VkPipeline VkRenderer::getPipeline(const Item& it, const ShaderUid& uid) {
    const PipelineKey key = pipelineKeyFor(it, uid);
    if (VkPipeline cached = mPipelineMru.lookup(key)) {
        mPipelineMruHits++;
        return cached;
    }
    mPipelineMruMisses++;
    auto found = mPipelines.find(key);
    if (found != mPipelines.end()) {
        mPipelineMru.store(key, found->second);
        return found->second;
    }
    if (!mPipelineFailed.count(key)) requestPipeline(key, uid);
    const PipelineKey general = uberPipelineKey(key);
    auto fallback = mPipelines.find(general);
    if (fallback != mPipelines.end()) return fallback->second;
    // Material/TEV changes normally reuse a family prepared at startup. A new
    // fixed-function raster state needs a driver pipeline, but no game pause.
    if (prewarmExactPipeline(general, general.uid) == PrewarmResult::Created) {
        return mPipelines.at(general);
    }
    return VK_NULL_HANDLE;
}

PrewarmResult VkRenderer::prewarmExactPipeline(const PipelineKey& key, const ShaderUid& uid) {
    if (mDevice == VK_NULL_HANDLE || mEfbRenderPass == VK_NULL_HANDLE) {
        return PrewarmResult::UnsupportedState;
    }

    auto pit = mPipelines.find(key);
    if (pit != mPipelines.end() && pit->second != VK_NULL_HANDLE) {
        return PrewarmResult::Reused;
    }

    std::vector<uint32_t> vsSpirv, fsSpirv;
    if (!lookupVulkanShader(uid, vsSpirv, fsSpirv)) {
        if (mShaderFailed.count(uid)) {
            return PrewarmResult::SpirvUnavailable;
        }
        if (!compileVulkanShader(uid, vsSpirv, fsSpirv)) {
            mShaderFailed.insert(uid);
            return PrewarmResult::SpirvUnavailable;
        }
    }

    VkPipeline pipeline = createPipeline(key, vsSpirv, fsSpirv);
    if (pipeline == VK_NULL_HANDLE) {
        return PrewarmResult::CreationFailed;
    }
    return PrewarmResult::Created;
}

VkPipeline VkRenderer::createPipeline(const PipelineKey& key,
                                      const std::vector<uint32_t>& vsSpirv, const std::vector<uint32_t>& fsSpirv) {
    auto found = mPipelines.find(key);
    if (found != mPipelines.end()) return found->second;
    VkPipeline pipeline = buildPipeline(key, vsSpirv, fsSpirv);
    if (pipeline != VK_NULL_HANDLE) mPipelines.emplace(key, pipeline);
    return pipeline;
}

bool VkRenderer::prepareFramePipelines(const Prepared& frame, HudMode hudMode) {
    drainCompiledShaders();
    bool ready = true, hud = false;
    mDrawPipelines.assign(frame.items.size(), VK_NULL_HANDLE);
    for (size_t index = 0; index < frame.items.size(); index++) {
        const Item& it = frame.items[index];
        if (it.type == IT_MARKER) { hud = it.marker == MARK_HUD_BEGIN; continue; }
        if ((hud && hudMode == HudMode::Skip) || it.type != IT_DRAW || !it.idxCount) continue;
        if (it.uid >= frame.uids.size()) {
            mPipelinesLoading = false;
            markFatal("prepare pipelines", "invalid shader UID index", 0);
            return false;
        }
        const ShaderUid& uid = frame.uids[it.uid];
        mDrawPipelines[index] = getPipeline(it, uid);
        if (mDrawPipelines[index] == VK_NULL_HANDLE) {
            ready = false;
            if (mPipelineFailed.count(pipelineKeyFor(it, uid))) {
                markFatal("prepare pipelines", "required shader or pipeline failed", 0);
            }
        }
    }
    mPipelinesLoading = false;
    if (!ready && !mFatal) markFatal("prepare pipelines", "universal pipeline unavailable", 0);
    return ready;
}

}  // namespace gpu
