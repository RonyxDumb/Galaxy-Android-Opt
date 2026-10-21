#pragma once
#include <string>
#include "gx_shader_uid.h"

namespace gpu {
// screenTexGens only uses bits 0..7 in recorded game UIDs.
constexpr uint32_t kUberShader = 0x80000000u;
inline bool isUberShader(const ShaderUid& uid) { return uid.screenTexGens == kUberShader; }
inline ShaderUid uberShaderUid(uint32_t flags) {
    ShaderUid uid{};
    uid.vtxFlags = flags;
    uid.screenTexGens = kUberShader;
    return uid;
}
std::string genUberVertexShader(const ShaderUid& uid);
std::string genUberFragmentShader();
extern const char* kUberStateRead;
}  // namespace gpu
