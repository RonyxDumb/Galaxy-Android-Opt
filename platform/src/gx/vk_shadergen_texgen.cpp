#include "vk_shadergen_common.h"

namespace gpu {

void vkGenerateTexGens(VulkanShaderGenOut& o, const ShaderUid& u) {
    uint32_t f = u.vtxFlags;
    for (uint32_t i = 0; i < u.numTexGens && i < 8; i++) {
        uint32_t info = u.texMtxInfo[i];
        uint32_t src = (info >> 7) & 31, type = (info >> 4) & 3;
        o.w("  {\n");
        if (type == 1) {
            uint32_t source = (info >> 12) & 7, light = (info >> 15) & 7;
            if (source < i) {
                o.w("    vec3 d = fetch_st(lbase + %uu).xyz - pos;\n", light * 5 + 3);
                o.w("    vec3 lvec = dot(d, d) > 0.0 ? normalize(d) : nrm;\n");
                o.w("    v_tex%u = v_tex%u + vec3(dot(lvec, tan), dot(lvec, bin), 0.0);\n", i, source);
            } else {
                o.w("    v_tex%u = vec3(0.0, 0.0, 1.0);\n", i);
            }
        } else if (type >= 2) {
            o.w("    v_tex%u = vec3(v_clr%u.rg, 1.0);\n", i, type - 2);
        } else {
            const char* in = "vec4(0.0, 0.0, 1.0, 1.0)";
            char uvBuf[64];
            // XF texgen reads the vertex attributes before position/normal transforms.
            if (src == 0) in = "vec4(a_pos, 1.0)";
            else if (src == 1 && (f & VF_NRM)) in = "vec4(a_nrm, 1.0)";
            else if (src == 3 && (f & VF_NBT)) in = "vec4(a_bin, 1.0)";
            else if (src == 4 && (f & VF_NBT)) in = "vec4(a_tan, 1.0)";
            else if (src >= 5 && src <= 12 && (f & (VF_TEX0 << (src - 5)))) {
                snprintf(uvBuf, sizeof(uvBuf), "vec4(a_tex%u, 1.0, 1.0)", src - 5);
                in = uvBuf;
            }
            o.w("    vec4 tsrc = %s;\n", in);
            if (!(info & 4)) o.w("    tsrc.z = 1.0;\n");
            o.w("    tsrc.xyz = mix(tsrc.xyz, vec3(1.0), isnan(tsrc.xyz));\n");
            if (f & VF_TEXMTX) {
                o.w("    uint m = (a_texmtx.%s >> %uu) & 63u;\n", i < 4 ? "x" : "y", (i % 4) * 8);
            } else {
                o.w("    uint m = (fetch_recs(rb + %uu) >> %uu) & 63u;\n",
                    i < 4 ? (unsigned)kRecMtxIdxA : (unsigned)kRecMtxIdxB,
                    i < 4 ? (i + 1) * 6 : (i - 4) * 6);
            }
            // Index zero is a valid matrix. Only index 60 selects GX_IDENTITY.
            o.w("    uint r = slotRow(rb, %uu, m);\n", (unsigned)kRecPosSlots);
            if (info & 2) o.w("    v_tex%u = mulRows(r, tsrc);\n", i);
            else o.w("    v_tex%u = vec3(dot(fetch_rows(r), tsrc), dot(fetch_rows(r + 1u), tsrc), 1.0);\n", i);
            if (u.dualTex) {
                if (u.postMtxInfo[i] & 0x100) {
                    o.w("    if (dot(v_tex%u, v_tex%u) > 0.0) v_tex%u = normalize(v_tex%u);\n", i, i, i, i);
                }
                o.w("    uint pr = fetch_recs(rb + %uu);\n", (unsigned)(kRecPost + i));
                o.w("    v_tex%u = mulRows(pr, vec4(v_tex%u, 1.0));\n", i, i);
            }
            o.w("    if (v_tex%u.z == 0.0) v_tex%u.xy = clamp(v_tex%u.xy / 2.0, vec2(-1.0), vec2(1.0));\n", i, i, i);
        }
        if ((u.screenTexGens >> i) & 1) {
            o.w("    if (vrDraw) {\n");
            o.w("      v_tex%u = vec3((clipPos.xy / clipPos.w) * 0.5 + 0.5, 1.0);\n", i);
            o.w("    } else {\n      v_tex%u = vec3((c.xy / c.w) * 0.5 + 0.5, 1.0);\n    }\n", i);
        }
        o.w("  }\n");
    }
}

}  // namespace gpu
