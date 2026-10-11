#include "gfx/GL.h"

#include <string>

#include "core/Log.h"

// Definitions for the pointers declared in GL.h. The aliases there would rename a
// plain glName, so these are spelled out through the same mc_gl##name concatenation.
#define MC_GL_DEFINE(type, name) type mc_gl##name = nullptr;
MC_GL_FUNCTIONS(MC_GL_DEFINE)
#undef MC_GL_DEFINE

namespace mc::gl {
namespace {

bool g_loaded = false;

const char* sourceName(GLenum source) {
    switch (source) {
    case GL_DEBUG_SOURCE_API: return "api";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "window";
    case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader";
    case GL_DEBUG_SOURCE_THIRD_PARTY: return "third-party";
    case GL_DEBUG_SOURCE_APPLICATION: return "app";
    default: return "other";
    }
}

void APIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei,
                            const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    // 131185: NVIDIA's "buffer will use VIDEO memory" chatter.
    if (id == 131185 || id == 131218 || id == 131204) return;
    const bool error = type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH;
    if (error)
        log::error("GL [{} #{}] {}", sourceName(source), id, message);
    else
        log::warn("GL [{} #{}] {}", sourceName(source), id, message);
}

} // namespace

bool load(LoaderFunc loader) {
    if (!loader) return false;

    int missing = 0;
    std::string names;
#define MC_GL_LOAD(type, name)                                              \
    mc_gl##name = reinterpret_cast<type>(loader("gl" #name));              \
    if (!mc_gl##name) {                                                     \
        if (missing < 8) names += " gl" #name;                              \
        ++missing;                                                          \
    }
    MC_GL_FUNCTIONS(MC_GL_LOAD)
#undef MC_GL_LOAD

    if (missing > 0) {
        log::error("OpenGL loader: {} entry point(s) missing, e.g.{} -- is this a 4.5 core context?",
                   missing, names);
        g_loaded = false;
        return false;
    }
    g_loaded = true;
    return true;
}

bool isLoaded() { return g_loaded; }

bool hasExtension(const char* name) {
    if (!g_loaded || !name) return false;
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (ext && std::string_view(ext) == name) return true;
    }
    return false;
}

void enableDebugOutput() {
    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (!(flags & GL_CONTEXT_FLAG_DEBUG_BIT)) return;
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugCallback, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    log::info("OpenGL debug output enabled");
}

} // namespace mc::gl
