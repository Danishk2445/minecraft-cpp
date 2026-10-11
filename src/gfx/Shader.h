// Shader.h -- GLSL program loaded from shaders/ with #include and #define injection.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace mc {

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(Shader&& o) noexcept;
    Shader& operator=(Shader&& o) noexcept;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Directory that file names and #include "..." paths resolve against.
    static void setDirectory(const std::filesystem::path& dir);
    static const std::filesystem::path& directory();

    // Compiles <dir>/<vertex> and <dir>/<fragment>. `defines` become "#define NAME VALUE"
    // lines after #version (entries are "NAME" or "NAME VALUE"). Logs and returns false on error.
    bool load(std::string_view vertex, std::string_view fragment, const std::vector<std::string>& defines = {});
    // Compute shader variant.
    bool loadCompute(std::string_view compute, const std::vector<std::string>& defines = {});
    void destroy();

    unsigned id() const { return program_; }
    bool valid() const { return program_ != 0; }
    void use() const;

    int uniform(const char* name) const; // cached; -1 when absent
    void set(const char* name, int v) const;
    void set(const char* name, float v) const;
    void set(const char* name, const glm::vec2& v) const;
    void set(const char* name, const glm::vec3& v) const;
    void set(const char* name, const glm::vec4& v) const;
    void set(const char* name, const glm::mat4& m) const;
    void bindBlock(const char* blockName, unsigned binding) const;

private:
    unsigned program_ = 0;
    std::string name_;
    mutable std::unordered_map<std::string, int> uniforms_;
};

} // namespace mc
