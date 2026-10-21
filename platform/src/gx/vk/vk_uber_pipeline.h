#pragma once
#include "gx/vk_uber.h"
#include "gx/vk/vk_pipeline_key.h"

namespace gpu {
inline PipelineKey uberPipelineKey(PipelineKey key) {
    key.uid = uberShaderUid(key.flags);
    return key;
}
}  // namespace gpu
