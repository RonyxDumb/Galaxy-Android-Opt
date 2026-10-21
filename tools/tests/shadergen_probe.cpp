// Host build of the GX shader generator for the deterministic source tests in
// test_shadergen.py.  The generator is plain GLSL string building, so the part
// that decides what the vertex shader computes can be checked without a GPU.
//
// usage: shadergen_probe <color-ctrl hex> [alpha-ctrl hex]
//   prints the generated vertex shader for a one-channel material that is lit
//   by GX_LIGHT0 with the given channel control words (XF 0x100E / 0x1010).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>

#include "vk_shadergen.h"
#include "gx_shader_uid.h"
#include "gx_record.h"
#include "gx_builder.h"
#include "vk_uber.h"

static uint32_t parseHex(const char* s) {
    return (uint32_t)strtoul(s, nullptr, 0);
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--state") == 0) {
        gpu::Builder builder{};
        builder.xfMem[0x600 + 10] = 1e18f;
        gpu::LightState lights{};
        builder.fillLights(lights);
        if (lights.lights[0][3][0] != 1e18f) return 6;
        for (auto& slot : builder.posSlot) slot = ~0u;
        for (auto& slot : builder.nrmSlot) slot = ~0u;
        builder.drawRecord(false, false, false, false, 0);
        if (builder.recs[gpu::kRecMtxIdxA] != 0 || builder.recs[gpu::kRecMtxIdxB] != 0) return 7;
        builder.bp[0] = 2 << 16;
        builder.bp[0x25] = 0x4321;
        builder.bp[0x26] = 0x8765;
        auto uid = gpu::makeShaderUid(builder.bp, builder.xfRegs, 0, false);
        if (uid.indScale[0] != 0x4321 || uid.indScale[1] != 0x8765) return 8;
        auto i = builder.uidFor(uid);
        auto base = builder.uidStateBases[i];
        if (memcmp(builder.states.data() + base * 4, &uid, sizeof(uid)) != 0) return 9;
        auto record = builder.drawRecord(false, false, false, false, 0, base);
        if (builder.recs[record * gpu::kRecWords + gpu::kRecShaderUid] != base) return 10;
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--uid") == 0) {
        gpu::ShaderUid uid{};
        FILE* in = fopen(argv[3], "rb");
        if (!in) return 3;
        size_t n = fread(&uid, 1, sizeof(uid), in);
        fclose(in);
        if (n != sizeof(uid)) return 4;
        bool universal = argv[2][0] == 'u';
        bool vertex = strcmp(argv[2], "vs") == 0 || strcmp(argv[2], "uvs") == 0;
        if (universal) uid = gpu::uberShaderUid(uid.vtxFlags);
        std::string out = vertex ? gpu::genVulkanVertexShader(uid) : gpu::genVulkanFragmentShader(uid);
        fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--threads") == 0) {
        std::atomic<bool> bad{false};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; t++) threads.emplace_back([t, &bad] {
            gpu::ShaderUid uid{};
            uid.numTevStages = 1;
            uid.alphaFunc = (t % 6 + 1) | (((t + 2) % 6 + 1) << 3);
            std::string expected = gpu::genVulkanFragmentShader(uid);
            for (int i = 0; i < 4000; i++) {
                if (gpu::genVulkanFragmentShader(uid) != expected) bad = true;
            }
        });
        for (auto& thread : threads) thread.join();
        return bad ? 5 : 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: %s [--fs] <color-ctrl> [alpha-ctrl] [alpha-func]\n", argv[0]);
        return 2;
    }

    bool wantFs = false;
    int argi = 1;
    if (strcmp(argv[argi], "--fs") == 0) {
        wantFs = true;
        argi++;
    }

    gpu::ShaderUid uid;
    memset(&uid, 0, sizeof(uid));
    uid.vtxFlags = gpu::VF_NRM;  // a lit material needs normals
    uid.numTexGens = 0;
    uid.numChans = 1;
    uid.colorCtrl[0] = (argi < argc) ? parseHex(argv[argi++]) : 0;
    uid.alphaCtrl[0] = (argi < argc) ? parseHex(argv[argi++]) : uid.colorCtrl[0];
    uid.alphaFunc = (argi < argc) ? parseHex(argv[argi++]) : 0;
    uid.numTevStages = 1;

    if (uid.numChans > 0) {
        // GX_CC_RASC as the only TEV input: forces the fragment shader to read
        // the rasterized colour the vertex lighting produced.
        uid.tevOrder[0] = 0x0000000F;  // ras for both colour and alpha
    }

    std::string out = wantFs ? gpu::genVulkanFragmentShader(uid) : gpu::genVertexShader(uid);
    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
