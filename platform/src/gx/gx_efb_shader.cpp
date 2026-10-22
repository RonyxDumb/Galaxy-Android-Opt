#include "gx_efb_shader.h"

namespace gpu {

std::string efbCopyComputeSource() {
    return R"(#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D colorEfb;
layout(set = 0, binding = 1) uniform sampler2D depthEfb;
layout(rgba8, set = 0, binding = 2) uniform writeonly image2D copyOut;
layout(push_constant) uniform CopyState { ivec4 src; ivec4 dst; } pc;

uint expand4(uint v) { return (v & 240u) | (v >> 4u); }
uint expand5(uint v) { return (v & 248u) | (v >> 5u); }
uint expand6(uint v) { return (v & 252u) | (v >> 6u); }
uint expand3(uint v) { v >>= 5u; return (v << 5u) | (v << 2u) | (v >> 1u); }
uvec4 readEfb(ivec2 pixel) {
    pixel = clamp(pixel, pc.src.xy, pc.src.xy + pc.src.zw - 1);
    uint pf = uint(pc.dst.w) & 7u;
    if (pf == 3u) {
        uint z = min(uint(clamp(texelFetch(depthEfb, pixel, 0).r, 0.0, 1.0) * 16777215.0 + 0.5), 16777215u);
        return uvec4((z >> 16u) & 255u, (z >> 8u) & 255u, z & 255u, 255u);
    }
    uvec4 c = uvec4(roundEven(clamp(texelFetch(colorEfb, pixel, 0), 0.0, 1.0) * 255.0));
    if (pf == 1u) c = (c & 252u) | (c >> 6u);
    else {
        c.a = 255u;
        if (pf == 2u) c.rgb = uvec3(expand5(c.r), expand6(c.g), expand5(c.b));
    }
    return c;
}
uvec4 convertCopy(uvec4 c, uint ctrl) {
    if ((ctrl & 32768u) != 0u) {
        uint y = (66u * c.r + 129u * c.g + 25u * c.b + 4096u + 128u) >> 8u;
        c.r = min(y, 255u);
    }
    uint fmt = ((ctrl >> 4u) & 7u) | (ctrl & 8u);
    if (fmt == 0u) return uvec4(expand4(c.r));
    if (fmt == 1u || fmt == 8u) return uvec4(c.r);
    if (fmt == 2u) return uvec4(uvec3(expand4(c.r)), expand4(c.a));
    if (fmt == 3u) return c.rrra;
    if (fmt == 4u) return uvec4(expand5(c.r), expand6(c.g), expand5(c.b), 255u);
    if (fmt == 5u) {
        if (c.a >= 224u) return uvec4(expand5(c.r), expand5(c.g), expand5(c.b), 255u);
        return uvec4(expand4(c.r), expand4(c.g), expand4(c.b), expand3(c.a));
    }
    if (fmt == 7u) return c.aaaa;
    if (fmt == 9u) return c.gggg;
    if (fmt == 10u) return c.bbbb;
    if (fmt == 11u) return c.rrrg;
    if (fmt == 12u) return c.gggb;
    return c;
}
void main() {
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(dst, pc.dst.xy))) return;
    bool halfSize = (uint(pc.dst.z) & 512u) != 0u;
    ivec2 pos = pc.src.xy + dst * (halfSize ? 2 : 1);
    uvec4 c = readEfb(pos);
    if (halfSize) {
        c += readEfb(pos + ivec2(1, 0)) + readEfb(pos + ivec2(0, 1)) + readEfb(pos + ivec2(1, 1));
        c = (c + 2u) / 4u;
    }
    imageStore(copyOut, dst, vec4(convertCopy(c, uint(pc.dst.z))) / 255.0);
}
)";
}

}  // namespace gpu
