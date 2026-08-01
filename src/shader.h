#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <string>

class Shader {
public:
    GLuint id = 0;

    Shader() = default;
    Shader(const char* vsSrc, const char* fsSrc) {
        GLuint vs = compile(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);
        id = glCreateProgram();
        glAttachShader(id, vs);
        glAttachShader(id, fs);
        glLinkProgram(id);
        GLint ok = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetProgramInfoLog(id, sizeof log, nullptr, log);
            throw std::runtime_error(std::string("shader link failed:\n") + log);
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void use() const { glUseProgram(id); }
    GLint loc(const char* name) const { return glGetUniformLocation(id, name); }
    void set(const char* n, int v) const { glUniform1i(loc(n), v); }
    void set(const char* n, float v) const { glUniform1f(loc(n), v); }
    void set(const char* n, const glm::vec3& v) const { glUniform3fv(loc(n), 1, glm::value_ptr(v)); }
    void set(const char* n, const glm::vec4& v) const { glUniform4fv(loc(n), 1, glm::value_ptr(v)); }
    void set(const char* n, const glm::mat4& m) const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, glm::value_ptr(m)); }

private:
    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            throw std::runtime_error(std::string("shader compile failed:\n") + log);
        }
        return s;
    }
};
