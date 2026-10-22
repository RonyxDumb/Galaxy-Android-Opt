#include "gx/vk_shadergen.h"
#include "gx/vk_shader_inventory.h"
#include <shaderc/shaderc.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>

extern "C" void port_log(const char*, ...) {}
extern "C" int64_t port_host_time_ns() { return 0; }
extern "C" void port_perf_shader_compile(int64_t) {}
namespace gpu {
std::string genVulkanVertexShader(const ShaderUid& uid) { return std::to_string(uid.tevColor[0]); }
std::string genVulkanFragmentShader(const ShaderUid& uid) { return std::to_string(uid.tevColor[0]) + "fs"; }
ShaderInventory& ShaderInventory::get() { static ShaderInventory instance; return instance; }
ShaderInventory::ShaderInventory() = default;
void ShaderInventory::populateCanonicalGalaxyInventory() {}
void ShaderInventory::compileAllSpirv(int, std::function<void(int, int, const char*)>) {}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto path = std::filesystem::temp_directory_path() / ("galaxy-spirv-" + std::to_string((uintptr_t)argv));
    gpu::setVulkanShaderCachePath(path.c_str());
    std::vector<uint32_t> vs, fs;
    gpu::ShaderUid uid{};
    for (uint32_t i = 0; i < 20; i++) {
        uid.tevColor[0] = i;
        assert(gpu::compileVulkanShader(uid, vs, fs));
        assert(vs.size() * 4 == 1048576 && fs.size() == vs.size());
    }
    assert(shaderc::compileCalls == 40);
    // Only 16 two-MiB pairs fit; overflow remains usable by the caller.
    assert(std::filesystem::file_size(path) == 8 + 16 * (24 + 2097152));
    uid.tevColor[0] = 0;
    assert(gpu::lookupVulkanShader(uid, vs, fs));
    uid.tevColor[0] = 19;
    assert(!gpu::lookupVulkanShader(uid, vs, fs));
    gpu::setVulkanShaderCachePath(path.c_str());
    uid.tevColor[0] = 15;
    assert(gpu::lookupVulkanShader(uid, vs, fs));
    assert(gpu::compileVulkanShader(uid, vs, fs) && shaderc::compileCalls == 40);
    // A bad/legacy header must not make stale data resident on upgrade.
    FILE* f = fopen(path.c_str(), "rb+"); assert(f);
    uint32_t oldVersion = 2;
    fseek(f, 4, SEEK_SET); fwrite(&oldVersion, 4, 1, f); fclose(f);
    gpu::setVulkanShaderCachePath(path.c_str());
    assert(!gpu::lookupVulkanShader(uid, vs, fs));
    assert(gpu::compileVulkanShader(uid, vs, fs));
    assert(std::filesystem::file_size(path) == 8 + 24 + 2097152);
    std::filesystem::remove(path);
}
