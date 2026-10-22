#pragma once
#include "gx/vk_uber.h"
#include "gx/vk/vk_pipeline_key.h"

namespace gpu {
inline PipelineKey uberPipelineKey(PipelineKey key) {
    ShaderUid material = key.uid;
    material.vtxFlags = key.flags;
    key.uid = uberShaderUid(material);
    return key;
}
}  // namespace gpu
