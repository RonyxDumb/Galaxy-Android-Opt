#include "vk_shadergen_common.h"
#include "vk_uber.h"

namespace gpu {

const char* kUberStateRead = R"(
uint U(uint base, uint word) {
    return floatBitsToUint(fetch_st(base + word / 4u))[word & 3u];
}
)";

std::string genUberVertexShader(const ShaderUid& uid) {
    ShaderUid base{};
    base.vtxFlags = uid.vtxFlags;
    base.numTexGens = uid.numTexGens;
    std::string src = genVulkanVertexShader(base);
    src.resize(src.find("  vec4 vclr0 ="));
    std::string helpers = kUberStateRead;
    helpers += R"(
layout(location = 13) flat out uint v_uid;
vec4 lightChannel(uint ctrl, vec4 vertex, vec4 material, vec4 ambient,
                  vec3 pos, vec3 nrm, uint lights) {
    vec4 mat = (ctrl & 1u) != 0u ? vertex : material;
    if ((ctrl & 2u) == 0u) return mat;
    ivec4 m = ivec4(round(mat * 255.0));
    ivec4 acc = ivec4(round(((ctrl & 64u) != 0u ? vertex : ambient) * 255.0));
    uint mask = ((ctrl >> 2u) & 15u) | (((ctrl >> 11u) & 15u) << 4u);
    uint diffuse = (ctrl >> 7u) & 3u;
    for (uint l = 0u; l < 8u; l++) {
        if ((mask & (1u << l)) == 0u) continue;
        uint b = lights + l * 5u;
        vec3 d = fetch_st(b + 3u).xyz - pos;
        float d2 = dot(d, d), dist = sqrt(d2);
        vec3 lv = dist > 0.0 ? d / dist : nrm;
        float att = 1.0;
        if ((ctrl & 512u) != 0u) {
            vec3 k = fetch_st(b + 1u).xyz, da = fetch_st(b + 2u).xyz;
            float angle;
            if ((ctrl & 1024u) != 0u) {
                angle = max(0.0, dot(lv, fetch_st(b + 4u).xyz));
                att = max(0.0, dot(k, vec3(1.0, angle, angle * angle))) / dot(da, vec3(1.0, dist, d2));
            } else {
                angle = dot(nrm, lv) >= 0.0 ? max(0.0, dot(nrm, fetch_st(b + 4u).xyz)) : 0.0;
                if (diffuse != 0u) da = normalize(da);
                vec3 angles = vec3(1.0, angle, angle * angle);
                att = max(0.0, dot(k, angles)) / dot(da, angles);
            }
        }
        float df = diffuse == 0u ? 1.0 : diffuse == 1u ? dot(nrm, lv) : max(0.0, dot(nrm, lv));
        acc += ivec4(round(fetch_st(b) * 255.0 * (df * att)));
    }
    acc = clamp(acc, ivec4(0), ivec4(255));
    return vec4((m * (acc + (acc >> 7))) >> 8) / 255.0;
}
)";
    src.insert(src.find("void main()"), helpers);
    VulkanShaderGenOut o;
    o.s = src;
    o.w("  uint ub = fetch_recs(rb + %uu);\n  v_uid = ub;\n", (unsigned)kRecShaderUid);
    o.w("  vec4 vertex0 = %s;\n", (uid.vtxFlags & VF_CLR0) ? "a_clr0" : "vec4(1.0)");
    o.w("  vec4 vertex1 = %s;\n", (uid.vtxFlags & VF_CLR1) ? "a_clr1" : "vec4(1.0)");
    for (uint32_t c = 0; c < 2; c++) {
        o.w("  v_clr%u = vec4(0.0);\n", c);
        if (c >= uid.numChans) continue;
        o.w("  {\n");
        if (uid.colorCtrl[c]) {
            o.w("    v_clr%u.rgb = lightChannel(U(ub, %uu), vertex%u, matColor[%u], ambColor[%u], pos, nrm, lbase).rgb;\n", c, 4 + c, c, c, c);
        } else {
            o.w("    v_clr%u.rgb = (U(ub, %uu) & 1u) != 0u ? vertex%u.rgb : matColor[%u].rgb;\n", c, 4 + c, c, c);
        }
        if (uid.alphaCtrl[c]) {
            o.w("    v_clr%u.a = lightChannel(U(ub, %uu), vertex%u, matColor[%u], ambColor[%u], pos, nrm, lbase).a;\n", c, 6 + c, c, c, c);
        } else {
            o.w("    v_clr%u.a = (U(ub, %uu) & 1u) != 0u ? vertex%u.a : matColor[%u].a;\n", c, 6 + c, c, c);
        }
        o.w("  }\n");
    }
    o.w("  vec4 raw[13];\n  for (int i = 0; i < 13; i++) raw[i] = vec4(0.0, 0.0, 1.0, 1.0);\n");
    o.w("  raw[0] = vec4(a_pos, 1.0);\n");
    if (uid.vtxFlags & VF_NRM) o.w("  raw[1] = vec4(a_nrm, 1.0);\n");
    if (uid.vtxFlags & VF_NBT) o.w("  raw[3] = vec4(a_bin, 1.0);\n  raw[4] = vec4(a_tan, 1.0);\n");
    for (uint32_t t = 0; t < 8; t++) {
        if (uid.vtxFlags & (VF_TEX0 << t)) o.w("  raw[%u] = vec4(a_tex%u, 1.0, 1.0);\n", t + 5, t);
    }
    o.w("  vec3 tc[8];\n  for (uint i = 0u; i < %uu; i++) {\n", uid.numTexGens);
    o.s += R"(
    tc[i] = vec3(0.0, 0.0, 1.0);
    uint info = U(ub, 8u + i), type = (info >> 4u) & 3u;
    if (type == 1u) {
        uint from = (info >> 12u) & 7u, l = (info >> 15u) & 7u;
        if (from < i) {
            vec3 d = fetch_st(lbase + l * 5u + 3u).xyz - pos;
            vec3 lv = dot(d, d) > 0.0 ? normalize(d) : nrm;
            tc[i] = tc[from] + vec3(dot(lv, tan), dot(lv, bin), 0.0);
        }
        continue;
    }
    if (type >= 2u) { tc[i] = vec3(type == 2u ? v_clr0.rg : v_clr1.rg, 1.0); continue; }
    uint row = min((info >> 7u) & 31u, 12u);
    vec4 tsrc = raw[row];
    if ((info & 4u) == 0u) tsrc.z = 1.0;
    tsrc.xyz = mix(tsrc.xyz, vec3(1.0), isnan(tsrc.xyz));
)";
    if (uid.vtxFlags & VF_TEXMTX) {
        o.w("    uint m = (i < 4u ? a_texmtx.x >> (i * 8u) : a_texmtx.y >> ((i - 4u) * 8u)) & 63u;\n");
    } else {
        o.w("    uint m = (i < 4u ? mA >> ((i + 1u) * 6u) : mB >> ((i - 4u) * 6u)) & 63u;\n");
    }
    o.w("    uint r = slotRow(rb, %uu, int(m));\n", (unsigned)kRecPosSlots);
    o.s += R"(
    tc[i] = vec3(dot(fetch_rows(r), tsrc), dot(fetch_rows(r + 1u), tsrc), 1.0);
    if ((info & 2u) != 0u) tc[i].z = dot(fetch_rows(r + 2u), tsrc);
    if (U(ub, 3u) != 0u) {
        if ((U(ub, 16u + i) & 256u) != 0u && dot(tc[i], tc[i]) > 0.0) tc[i] = normalize(tc[i]);
)";
    o.w("        uint pr = fetch_recs(rb + %uu + i);\n", (unsigned)kRecPost);
    o.s += R"(
        tc[i] = mulRows(pr, vec4(tc[i], 1.0));
    }
    if (tc[i].z == 0.0) tc[i].xy = clamp(tc[i].xy / 2.0, vec2(-1.0), vec2(1.0));
    if ((U(ub, 98u) & (1u << i)) != 0u) tc[i] = vec3((c.xy / c.w) * 0.5 + 0.5, 1.0);
  }
)";
    for (uint32_t i = 0; i < uid.numTexGens; i++) o.w("  v_tex%u = tc[%u];\n", i, i);
    o.w("}\n");
    return o.s;
}

}  // namespace gpu
