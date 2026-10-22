#include "vk_shadergen_common.h"
#include "vk_uber.h"

namespace gpu {

std::string genUberFragmentShader(const ShaderUid& uid) {
    VulkanShaderGenOut o;
    o.s = "#version 450\n";
    o.s += kVulkanUniformBlocksFs;
    o.s += kUberStateRead;
    for (int t = 0; t < 8; t++) {
        if (uid.iref & (1u << t)) o.w("layout(set = 1, binding = %d) uniform sampler2D s_tex%d;\n", t, t);
    }
    o.w("layout(location = 0) flat in uint v_pix;\nlayout(location = 1) in vec4 v_clr0;\nlayout(location = 2) in vec4 v_clr1;\n");
    for (uint32_t t = 0; t < uid.numTexGens; t++) o.w("layout(location = %u) in vec3 v_tex%u;\n", t + 3, t);
    o.w("layout(location = %u) in float v_viewz;\nlayout(location = %u) in float v_gxz;\n", uid.numTexGens + 3, uid.numTexGens + 4);
    o.s += R"(
layout(location = 13) flat in uint v_uid;
layout(location = 0) out vec4 o_color;
vec3 tc(uint i) {
)";
    for (uint32_t t = 0; t < uid.numTexGens; t++) o.w("  if (i == %uu) return v_tex%u;\n", t, t);
    o.s += "  return vec3(0.0, 0.0, 1.0);\n}\nvec4 sampleUnit(uint i, vec2 uv) {\n";
    for (int t = 0; t < 8; t++) {
        if (uid.iref & (1u << t)) o.w("  if (i == %du) return texture(s_tex%d, uv);\n", t, t);
    }
    o.s += R"(
  return vec4(0.0);
}
vec2 projectUv(vec3 v) {
    if (isnan(v.z) || isinf(v.z) || abs(v.z) < 1e-6) return (any(isnan(v.xy)) || any(isinf(v.xy))) ? vec2(0.0) : v.xy;
    vec2 uv = v.xy / v.z;
    return (any(isnan(uv)) || any(isinf(uv))) ? vec2(0.0) : uv;
}
ivec4 swizzle(ivec4 v, uint table) {
    uint lo = U(v_uid, 66u + table * 2u), hi = U(v_uid, 67u + table * 2u);
    return ivec4(v[lo & 3u], v[(lo >> 2u) & 3u], v[hi & 3u], v[(hi >> 2u) & 3u]);
}
ivec3 colorIn(uint arg, ivec4 r[4], ivec4 tex, ivec4 ras, ivec3 k) {
    if (arg < 8u) return (arg & 1u) != 0u ? r[arg / 2u].aaa : r[arg / 2u].rgb;
    if (arg == 8u) return tex.rgb;
    if (arg == 9u) return tex.aaa;
    if (arg == 10u) return ras.rgb;
    if (arg == 11u) return ras.aaa;
    if (arg == 12u) return ivec3(255);
    if (arg == 13u) return ivec3(128);
    if (arg == 14u) return k;
    return ivec3(0);
}
int alphaIn(uint arg, ivec4 r[4], ivec4 tex, ivec4 ras, int k) {
    if (arg < 4u) return r[arg].a;
    if (arg == 4u) return tex.a;
    if (arg == 5u) return ras.a;
    if (arg == 6u) return k;
    return 0;
}
ivec4 regular(ivec4 a, ivec4 b, ivec4 c, ivec4 d, uint ctrl) {
    uint bias = (ctrl >> 16u) & 3u, scale = (ctrl >> 20u) & 3u;
    bool sub = (ctrl & 262144u) != 0u;
    int shift = scale == 3u ? 0 : int(scale);
    ivec4 lerp = ((a * 256 + (b - a) * (c + (c >> 7))) * (1 << shift) + (scale == 3u ? 0 : sub ? 127 : 128)) >> 8;
    ivec4 value = (d + (bias == 1u ? 128 : bias == 2u ? -128 : 0)) * (1 << shift) + (sub ? -lerp : lerp);
    return scale == 3u ? value >> 1 : value;
}
ivec4 compareTev(ivec4 a, ivec4 b, ivec4 c, ivec4 d, uint ctrl, bool alpha) {
    uint mode = (ctrl >> 20u) & 3u;
    bool eq = (ctrl & 262144u) != 0u;
    if (mode == 3u) {
        bvec4 test = eq ? equal(a, b) : greaterThan(a, b);
        return d + ivec4(test) * c;
    }
    ivec3 weights = mode == 0u ? ivec3(1, 0, 0) : mode == 1u ? ivec3(1, 256, 0) : ivec3(1, 256, 65536);
    ivec3 av = a.rgb * weights, bv = b.rgb * weights;
    int x = av.r + av.g + av.b, y = bv.r + bv.g + bv.b;
    return d + ((eq ? x == y : x > y) ? c : ivec4(0));
}
bool alphaTest(uint fn, int a, int ref) {
    if (fn == 0u) return false;
    if (fn == 1u) return a < ref;
    if (fn == 2u) return a == ref;
    if (fn == 3u) return a <= ref;
    if (fn == 4u) return a > ref;
    if (fn == 5u) return a != ref;
    if (fn == 6u) return a >= ref;
    return true;
}
void main() {
    ivec4 r[4], k[4];
    for (uint i = 0u; i < 4u; i++) {
        r[i] = ivec4(round(fetch_st(v_pix + i) * 255.0));
        k[i] = ivec4(round(clamp(fetch_st(v_pix + 4u + i), 0.0, 1.0) * 255.0));
    }
    vec4 texSize[8];
    for (uint i = 0u; i < 8u; i++) texSize[i] = fetch_st(v_pix + 12u + i);
    vec3 ind[4];
)";
    o.w("    const uint ni = %uu, ng = %uu;\n    for (uint i = 0u; i < ni; i++) {\n", uid.numIndStages, uid.numTexGens);
    o.s += R"(
        ind[i] = vec3(0.0);
        uint ref = U(v_uid, 92u) >> (i * 6u), coord = (ref >> 3u) & 7u;
        uint sc = U(v_uid, 93u + i / 2u) >> ((i & 1u) * 8u);
        if (coord < ng) ind[i] = floor(sampleUnit(ref & 7u, projectUv(tc(coord)) / exp2(vec2(sc & 15u, (sc >> 4u) & 15u))).abg * 255.0 + 0.5);
    }
    const int frac[8] = int[8](255, 223, 191, 159, 128, 96, 64, 32);
    uint lastC = 0u, lastA = 0u;
    ivec4 ztex = ivec4(0);
    int bump = 0;
)";
    o.w("    for (uint s = 0u; s < %uu; s++) {\n", uid.numTevStages);
    o.s += R"(
        uint order = U(v_uid, 26u + s / 2u) >> ((s & 1u) * 12u);
        uint map = order & 7u, coord = (order >> 3u) & 7u, chan = (order >> 7u) & 7u;
        uint cc = U(v_uid, 34u + s), ac = U(v_uid, 50u + s), cmd = U(v_uid, 76u + s);
        vec2 uv = coord < ng ? projectUv(tc(coord)) : vec2(0.0);
        if (ni > 0u && cmd != 0u) {
            uint bt = cmd & 3u, mtx = (cmd >> 9u) & 15u, bias = (cmd >> 4u) & 7u;
            if (bt < ni) {
                uint bs = (cmd >> 7u) & 3u, fmt = (cmd >> 2u) & 3u;
                if (bs > 0u) bump = (int(ind[bt][bs - 1u]) << (fmt == 0u ? 0u : fmt == 1u ? 5u : fmt == 2u ? 4u : 3u)) & 248;
                uint index = (mtx - 1u) & 3u;
                if (mtx != 0u && index < 3u) {
                    vec3 v = ind[bt] - vec3((bias & 1u) != 0u ? 128.0 : 0.0, (bias & 2u) != 0u ? 128.0 : 0.0, (bias & 4u) != 0u ? 128.0 : 0.0);
                    vec2 offset = vec2(dot(fetch_st(v_pix + 20u + index * 2u).xyz, v), dot(fetch_st(v_pix + 21u + index * 2u).xyz, v));
                    if (mtx >= 5u && mtx <= 7u) offset.y = 0.0;
                    if (mtx >= 9u && mtx <= 11u) offset.x = 0.0;
                    uv += offset * texSize[map].zw;
                }
            }
        }
        ivec4 tex = ivec4(0);
        if ((order & 64u) != 0u) {
            vec4 v = sampleUnit(map, uv);
            if (any(isnan(v)) || any(isinf(v))) v = vec4(0.0);
            tex = swizzle(ivec4(round(clamp(v, 0.0, 1.0) * 255.0)), (ac >> 2u) & 3u);
            ztex = tex;
        }
        vec4 raster = chan == 0u ? v_clr0 : chan == 1u ? v_clr1 : vec4(0.0);
        if (any(isnan(raster)) || any(isinf(raster))) raster = vec4(0.0);
        ivec4 ras = swizzle(ivec4(round(clamp(raster, 0.0, 1.0) * 255.0)), ac & 3u);
        if (chan == 5u || chan == 6u) ras = ivec4(chan == 5u ? bump : bump | (bump >> 5));
        uint sel = U(v_uid, 66u + s / 2u) >> ((s & 1u) * 10u), cs = (sel >> 4u) & 31u, asel = (sel >> 9u) & 31u;
        ivec3 kc = cs < 8u ? ivec3(frac[cs]) : cs >= 12u && cs < 16u ? k[cs - 12u].rgb : cs >= 16u ? ivec3(k[(cs - 16u) & 3u][(cs - 16u) >> 2u]) : ivec3(0);
        int ka = asel < 8u ? frac[asel] : asel >= 16u ? k[(asel - 16u) & 3u][(asel - 16u) >> 2u] : 0;
        ivec4 a = ivec4(colorIn((cc >> 12u) & 15u, r, tex, ras, kc), alphaIn((ac >> 13u) & 7u, r, tex, ras, ka)) & 255;
        ivec4 b = ivec4(colorIn((cc >> 8u) & 15u, r, tex, ras, kc), alphaIn((ac >> 10u) & 7u, r, tex, ras, ka)) & 255;
        ivec4 c = ivec4(colorIn((cc >> 4u) & 15u, r, tex, ras, kc), alphaIn((ac >> 7u) & 7u, r, tex, ras, ka)) & 255;
        ivec4 d = clamp(ivec4(colorIn(cc & 15u, r, tex, ras, kc), alphaIn((ac >> 4u) & 7u, r, tex, ras, ka)), ivec4(-1024), ivec4(1023));
        ivec4 cv = ((cc >> 16u) & 3u) == 3u ? compareTev(a, b, c, d, cc, false) : regular(a, b, c, d, cc);
        ivec4 av = ((ac >> 16u) & 3u) == 3u ? compareTev(a, b, c, d, ac, true) : regular(a, b, c, d, ac);
        lastC = (cc >> 22u) & 3u; lastA = (ac >> 22u) & 3u;
        r[lastC].rgb = clamp(cv.rgb, ivec3((cc & 524288u) != 0u ? 0 : -1024), ivec3((cc & 524288u) != 0u ? 255 : 1023));
        r[lastA].a = clamp(av.a, (ac & 524288u) != 0u ? 0 : -1024, (ac & 524288u) != 0u ? 255 : 1023);
    }
    ivec4 result = ivec4(r[lastC].rgb, r[lastA].a) & 255;
    vec4 refs = fetch_st(v_pix + 8u);
)";
    if (uid.alphaFunc) o.s += R"(
    uint test = U(v_uid, 74u), logic = (test >> 6u) & 3u;
    bool a0 = alphaTest(test & 7u, result.a, int(refs.x)), a1 = alphaTest((test >> 3u) & 7u, result.a, int(refs.y));
    bool pass = logic == 0u ? a0 && a1 : logic == 1u ? a0 || a1 : logic == 2u ? a0 != a1 : a0 == a1;
    if (!pass) discard;
)";
    o.s += "    vec4 outc = vec4(result) / 255.0;\n";
    // Merely declaring a depth write can disable early depth rejection on tile
    // GPUs. Ordinary GX materials must not statically write gl_FragDepth.
    if (uid.zTex) o.s += R"(
    uint z = U(v_uid, 96u), ztype = z & 3u, zop = (z >> 2u) & 3u;
    if (zop != 0u) {
        int value = ztype == 0u ? ztex.a : ztype == 1u ? ztex.r + ztex.a * 256 : ztex.r * 65536 + ztex.g * 256 + ztex.b;
        value += int(refs.z);
        if (zop == 1u) value += int(gl_FragCoord.z * 16777215.0 + 0.5);
        gl_FragDepth = float(value & 0xffffff) / 16777215.0;
    }
)";
    o.s += R"(
    uint fog = U(v_uid, 75u), ftype = (fog >> 1u) & 7u;
    if (ftype >= 2u) {
        vec4 fp = fetch_st(v_pix + 10u);
        float zc = floor(clamp(v_gxz, 0.0, 1.0) * 16777215.0), ze;
        if ((fog & 1u) != 0u) ze = fp.x * zc / 16777216.0;
        else { float denom = fp.z - floor(zc / exp2(clamp(fp.w, 0.0, 31.0))); ze = denom > 1.0 ? fp.x * 16777216.0 / denom : 0.0; }
        float f = clamp(ze - fp.y, 0.0, 1.0);
        if (ftype == 4u) f = 1.0 - exp2(-8.0 * f);
        if (ftype == 5u) f = 1.0 - exp2(-8.0 * f * f);
        if (ftype == 6u) f = exp2(-8.0 * (1.0 - f));
        if (ftype == 7u) f = exp2(-8.0 * (1.0 - f) * (1.0 - f));
        outc.rgb = mix(outc.rgb, fetch_st(v_pix + 9u).rgb, f);
    }
    o_color = clamp(outc, 0.0, 1.0);
}
)";
    return o.s;
}

}  // namespace gpu
