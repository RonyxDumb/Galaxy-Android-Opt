#include "vk_shadergen_common.h"

namespace gpu {

void vkGenerateLighting(VulkanShaderGenOut& o, const ShaderUid& u) {
    o.w("  vec4 vclr0 = %s;\n", (u.vtxFlags & VF_CLR0) ? "a_clr0" : "vec4(1.0)");
    o.w("  vec4 vclr1 = %s;\n", (u.vtxFlags & VF_CLR1) ? "a_clr1" : "vec4(1.0)");
    for (int c = 0; c < 2; c++) {
        if ((uint32_t)c >= u.numChans) {
            o.w("  v_clr%d = vec4(0.0);\n", c);
            continue;
        }
        for (int alpha = 0; alpha < 2; alpha++) {
            uint32_t ctrl = alpha ? u.alphaCtrl[c] : u.colorCtrl[c];
            const char* sw = alpha ? "a" : "rgb";
            const char* type = alpha ? "int" : "ivec3";
            const char* floatType = alpha ? "float" : "vec3";
            char mat[64], amb[64];
            snprintf(mat, sizeof(mat), (ctrl & 1) ? "vclr%d.%s" : "matColor[%d].%s", c, sw);
            snprintf(amb, sizeof(amb), ((ctrl >> 6) & 1) ? "vclr%d.%s" : "ambColor[%d].%s", c, sw);
            if (!(ctrl & 2)) {
                o.w("  v_clr%d.%s = %s;\n", c, sw, mat);
                continue;
            }
            uint32_t mask = ((ctrl >> 2) & 15) | (((ctrl >> 11) & 15) << 4);
            uint32_t diffuse = (ctrl >> 7) & 3;
            bool atten = (ctrl >> 9) & 1, spot = (ctrl >> 10) & 1;
            o.w("  {\n    %s mat = %s(round(%s * 255.0));\n", type, type, mat);
            o.w("    %s lit = %s(round(%s * 255.0));\n", type, type, amb);
            for (int l = 0; l < 8; l++) {
                if (!(mask & (1u << l))) continue;
                o.w("    {\n      uint b = lbase + %uu;\n", (unsigned)(l * 5));
                o.w("      vec3 d = fetch_st(b + 3u).xyz - pos;\n");
                o.w("      float d2 = dot(d, d);\n      float dist = sqrt(d2);\n");
                o.w("      vec3 lvec = dist > 0.0 ? d / dist : nrm;\n      float a = 1.0;\n");
                if (atten) {
                    o.w("      vec3 k = fetch_st(b + 1u).xyz;\n      vec3 da = fetch_st(b + 2u).xyz;\n");
                    if (spot) {
                        // GXInitLightDir has already reversed the supplied direction.
                        o.w("      float angle = max(0.0, dot(lvec, fetch_st(b + 4u).xyz));\n");
                        o.w("      a = max(0.0, dot(k, vec3(1.0, angle, angle * angle))) / dot(da, vec3(1.0, dist, d2));\n");
                    } else {
                        // Specular attenuation uses the normal/half-vector angle,
                        // not distance to the synthetic directional-light position.
                        o.w("      float angle = dot(nrm, lvec) >= 0.0 ? max(0.0, dot(nrm, fetch_st(b + 4u).xyz)) : 0.0;\n");
                        if (diffuse != 0) o.w("      da = normalize(da);\n");
                        o.w("      vec3 angles = vec3(1.0, angle, angle * angle);\n");
                        o.w("      a = max(0.0, dot(k, angles)) / dot(da, angles);\n");
                    }
                }
                if (diffuse == 0) o.w("      float illum = 1.0;\n");
                else if (diffuse == 1) o.w("      float illum = dot(nrm, lvec);\n");
                else o.w("      float illum = max(0.0, dot(nrm, lvec));\n");
                o.w("      lit += %s(round(fetch_st(b).%s * 255.0 * (illum * a)));\n    }\n", type, sw);
            }
            // XF clamps the accumulated light before modulating the material.
            // Its 8-bit multiply also expands light 255 to 256, preserving white.
            o.w("    lit = clamp(lit, %s(0), %s(255));\n", type, type);
            o.w("    v_clr%d.%s = %s((mat * (lit + (lit >> 7))) >> 8) / 255.0;\n  }\n", c, sw, floatType);
        }
    }
}

}  // namespace gpu
