#include "gfx/Shader.h"

#include <format>
#include <set>

#include <glm/gtc/type_ptr.hpp>

#include "core/Fs.h"
#include "core/Log.h"
#include "gfx/GL.h"

namespace mc {
namespace {

std::filesystem::path g_dir = MC_DEFAULT_SHADER_DIR;

// Expands #include "file" recursively (each file at most once) and emits #line
// markers so compiler errors point at the right file: file ids are listed in the log.
bool preprocess(const std::filesystem::path& file, std::string& out, std::vector<std::string>& files,
                std::set<std::filesystem::path>& seen, std::string& error) {
    const auto canonical = std::filesystem::weakly_canonical(file);
    if (seen.contains(canonical)) return true;
    seen.insert(canonical);
    auto text = fs::readText(file);
    if (!text) {
        error = std::format("cannot read {}", file.string());
        return false;
    }
    const int fileId = int(files.size());
    files.push_back(file.filename().string());
    out += std::format("#line 1 {}\n", fileId);

    size_t pos = 0;
    int lineNo = 1;
    while (pos <= text->size()) {
        size_t end = text->find('\n', pos);
        if (end == std::string::npos) end = text->size();
        std::string_view line(text->data() + pos, end - pos);
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos && line.substr(first).starts_with("#include")) {
            const size_t q1 = line.find('"');
            const size_t q2 = line.find('"', q1 + 1);
            if (q1 == std::string_view::npos || q2 == std::string_view::npos) {
                error = std::format("{}:{}: malformed #include", file.string(), lineNo);
                return false;
            }
            const auto inc = g_dir / std::string(line.substr(q1 + 1, q2 - q1 - 1));
            if (!preprocess(inc, out, files, seen, error)) return false;
            out += std::format("#line {} {}\n", lineNo + 1, fileId);
        } else if (first != std::string_view::npos && line.substr(first).starts_with("#version")) {
            // #version is emitted once by the loader; drop file-level copies.
            out += "\n";
        } else {
            out.append(line);
            out += '\n';
        }
        if (end == text->size()) break;
        pos = end + 1;
        ++lineNo;
    }
    return true;
}

GLuint compile(GLenum stage, const std::string& name, const std::vector<std::string>& defines) {
    std::string body;
    std::vector<std::string> files;
    std::set<std::filesystem::path> seen;
    std::string error;
    if (!preprocess(g_dir / name, body, files, seen, error)) {
        log::error("shader {}: {}", name, error);
        return 0;
    }
    std::string source = "#version 450 core\n";
    for (const auto& d : defines) {
        const size_t sp = d.find(' ');
        source += sp == std::string::npos ? std::format("#define {}\n", d)
                                          : std::format("#define {} {}\n", d.substr(0, sp), d.substr(sp + 1));
    }
    source += body;

    const GLuint shader = glCreateShader(stage);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string logText(size_t(std::max(len, 1)), '\0');
        glGetShaderInfoLog(shader, len, nullptr, logText.data());
        std::string fileList;
        for (size_t i = 0; i < files.size(); ++i) fileList += std::format(" {}={}", i, files[i]);
        log::error("shader {} failed to compile (file ids:{}):\n{}", name, fileList, logText);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link(const std::vector<GLuint>& stages, const std::string& name) {
    const GLuint program = glCreateProgram();
    for (GLuint s : stages) glAttachShader(program, s);
    glLinkProgram(program);
    for (GLuint s : stages) {
        glDetachShader(program, s);
        glDeleteShader(s);
    }
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::string logText(size_t(std::max(len, 1)), '\0');
        glGetProgramInfoLog(program, len, nullptr, logText.data());
        log::error("shader program {} failed to link:\n{}", name, logText);
        glDeleteProgram(program);
        return 0;
    }
    glObjectLabel(GL_PROGRAM, program, -1, name.c_str());
    return program;
}

} // namespace

void Shader::setDirectory(const std::filesystem::path& dir) { g_dir = dir; }
const std::filesystem::path& Shader::directory() { return g_dir; }

Shader::~Shader() { destroy(); }

Shader::Shader(Shader&& o) noexcept
    : program_(std::exchange(o.program_, 0)), name_(std::move(o.name_)), uniforms_(std::move(o.uniforms_)) {}

Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        destroy();
        program_ = std::exchange(o.program_, 0);
        name_ = std::move(o.name_);
        uniforms_ = std::move(o.uniforms_);
    }
    return *this;
}

bool Shader::load(std::string_view vertex, std::string_view fragment, const std::vector<std::string>& defines) {
    const std::string name = std::format("{}+{}", vertex, fragment);
    const GLuint vs = compile(GL_VERTEX_SHADER, std::string(vertex), defines);
    const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, std::string(fragment), defines) : 0;
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        return false;
    }
    const GLuint program = link({vs, fs}, name);
    if (!program) return false;
    destroy();
    program_ = program;
    name_ = name;
    return true;
}

bool Shader::loadCompute(std::string_view compute, const std::vector<std::string>& defines) {
    const GLuint cs = compile(GL_COMPUTE_SHADER, std::string(compute), defines);
    if (!cs) return false;
    const GLuint program = link({cs}, std::string(compute));
    if (!program) return false;
    destroy();
    program_ = program;
    name_ = std::string(compute);
    return true;
}

void Shader::destroy() {
    if (program_) glDeleteProgram(program_);
    program_ = 0;
    uniforms_.clear();
}

void Shader::use() const { glUseProgram(program_); }

int Shader::uniform(const char* name) const {
    auto it = uniforms_.find(name);
    if (it != uniforms_.end()) return it->second;
    const int loc = glGetUniformLocation(program_, name);
    uniforms_.emplace(name, loc);
    return loc;
}

void Shader::set(const char* name, int v) const { glProgramUniform1i(program_, uniform(name), v); }
void Shader::set(const char* name, float v) const { glProgramUniform1f(program_, uniform(name), v); }
void Shader::set(const char* name, const glm::vec2& v) const {
    glProgramUniform2f(program_, uniform(name), v.x, v.y);
}
void Shader::set(const char* name, const glm::vec3& v) const {
    glProgramUniform3f(program_, uniform(name), v.x, v.y, v.z);
}
void Shader::set(const char* name, const glm::vec4& v) const {
    glProgramUniform4f(program_, uniform(name), v.x, v.y, v.z, v.w);
}
void Shader::set(const char* name, const glm::mat4& m) const {
    glProgramUniformMatrix4fv(program_, uniform(name), 1, GL_FALSE, glm::value_ptr(m));
}

void Shader::bindBlock(const char* blockName, unsigned binding) const {
    const GLuint index = glGetUniformBlockIndex(program_, blockName);
    if (index != GL_INVALID_INDEX) glUniformBlockBinding(program_, index, binding);
}

} // namespace mc
