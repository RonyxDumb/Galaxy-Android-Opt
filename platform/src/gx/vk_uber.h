#pragma once
#include <string>
#include "gx_shader_uid.h"

namespace gpu {
// screenTexGens only uses bits 0..7 in recorded game UIDs.
constexpr uint32_t kUberShader = 0x80000000u;
inline bool isUberShader(const ShaderUid& uid) { return uid.screenTexGens == kUberShader; }
inline bool alphaAlwaysPasses(uint32_t test) {
    const uint32_t a = test & 7, b = (test >> 3) & 7, op = (test >> 6) & 3;
    return (op == 0 && a == 7 && b == 7) || (op == 1 && (a == 7 || b == 7)) ||
           (op == 2 && ((a == 0 && b == 7) || (a == 7 && b == 0))) ||
           (op == 3 && ((a == 0 && b == 0) || (a == 7 && b == 7)));
}
inline ShaderUid uberShaderUid(const ShaderUid& material) {
    ShaderUid uid{};
    uid.vtxFlags = material.vtxFlags;
    // Keep the program small for simple materials. TEV operations and matrices
    // still come from the original draw record, so these are complete shaders.
    uid.numTexGens = material.numTexGens > 8 ? 8 : material.numTexGens;
    uid.numChans = material.numChans > 2 ? 2 : material.numChans;
    uid.numTevStages = material.numTevStages > 16 ? 16 : material.numTevStages;
    uid.numIndStages = material.numIndStages > 4 ? 4 : material.numIndStages;
    for (uint32_t s = 0; s < uid.numTevStages; s++) {
        const uint32_t order = material.tevOrder[s / 2] >> ((s & 1) * 12);
        if (order & 64) uid.iref |= 1u << (order & 7);
    }
    for (uint32_t s = 0; s < uid.numIndStages; s++) uid.iref |= 1u << ((material.iref >> (s * 6)) & 7);
    for (uint32_t c = 0; c < uid.numChans; c++) {
        uid.colorCtrl[c] = material.colorCtrl[c] & 2;
        uid.alphaCtrl[c] = material.alphaCtrl[c] & 2;
    }
    uid.zTex = ((material.zTex >> 2) & 3) != 0;
    uid.alphaFunc = !alphaAlwaysPasses(material.alphaFunc);
    uid.screenTexGens = kUberShader;
    return uid;
}
std::string genUberVertexShader(const ShaderUid& uid);
std::string genUberFragmentShader(const ShaderUid& uid);
extern const char* kUberStateRead;
}  // namespace gpu
