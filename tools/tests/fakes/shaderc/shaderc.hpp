#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum shaderc_optimization_level { shaderc_optimization_level_zero, shaderc_optimization_level_performance };
enum shaderc_shader_kind { shaderc_glsl_vertex_shader, shaderc_glsl_fragment_shader };
constexpr int shaderc_target_env_vulkan = 0, shaderc_env_version_vulkan_1_0 = 0;
constexpr int shaderc_compilation_status_success = 0;
namespace shaderc {
inline int compileCalls = 0;
class CompileOptions {
public:
    void SetTargetEnvironment(int, int) {}
    void SetOptimizationLevel(shaderc_optimization_level) {}
};
class SpvCompilationResult {
    std::vector<uint32_t> words = std::vector<uint32_t>(262144, 0x07230203);
public:
    int GetCompilationStatus() const { return shaderc_compilation_status_success; }
    std::string GetErrorMessage() const { return {}; }
    auto cbegin() const { return words.cbegin(); }
    auto cend() const { return words.cend(); }
};
class Compiler {
public:
    SpvCompilationResult CompileGlslToSpv(const std::string&, shaderc_shader_kind, const char*, const CompileOptions&) {
        compileCalls++; return {};
    }
};
}
