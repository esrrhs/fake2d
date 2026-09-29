#include "fake2d/shader.h"
#include "gl.h"

#include <cstdio>
#include <vector>

namespace fake2d {

namespace {

GLuint CompileShader(GLenum type, std::string_view source) {
    GLuint shader = glCreateShader(type);
    const char *src_ptr = source.data();
    const auto src_len = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src_ptr, &src_len);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        std::vector<char> info_log(log_len > 0 ? log_len : 512);
        glGetShaderInfoLog(shader, static_cast<GLsizei>(info_log.size()), nullptr, info_log.data());
        std::fprintf(stderr, "fake2d: Shader compilation failed (%s):\n%s\n",
                     type == GL_VERTEX_SHADER ? "vertex" : "fragment",
                     info_log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

const char *kDefaultVertexShader = R"(#version 330 core
layout (location = 0) in vec2 a_pos;
layout (location = 1) in vec2 a_uv;
layout (location = 2) in vec4 a_color;

uniform mat4 u_view_projection;

out vec2 v_uv;
out vec4 v_color;

void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_view_projection * vec4(a_pos, 0.0, 1.0);
}
)";

const char *kDefaultFragmentShader = R"(#version 330 core
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture;

out vec4 frag_color;

void main() {
    frag_color = texture(u_texture, v_uv) * v_color;
}
)";

} // namespace

Shader::Shader() = default;

Shader::~Shader() {
    Destroy();
}

Shader::Shader(Shader &&other) noexcept
    : program_id_(other.program_id_),
      uniform_cache_(std::move(other.uniform_cache_)) {
    other.program_id_ = 0;
}

Shader &Shader::operator=(Shader &&other) noexcept {
    if (this != &other) {
        Destroy();
        program_id_ = other.program_id_;
        uniform_cache_ = std::move(other.uniform_cache_);
        other.program_id_ = 0;
    }
    return *this;
}

bool Shader::LoadFromSource(std::string_view vert_src, std::string_view frag_src) {
    Destroy();

    GLuint vert = CompileShader(GL_VERTEX_SHADER, vert_src);
    if (!vert) return false;

    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, frag_src);
    if (!frag) {
        glDeleteShader(vert);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint success = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        GLint log_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
        std::vector<char> info_log(log_len > 0 ? log_len : 512);
        glGetProgramInfoLog(prog, static_cast<GLsizei>(info_log.size()), nullptr, info_log.data());
        std::fprintf(stderr, "fake2d: Shader program link failed:\n%s\n", info_log.data());
        glDeleteProgram(prog);
        return false;
    }

    program_id_ = prog;
    uniform_cache_.clear();
    return true;
}

void Shader::Destroy() {
    if (program_id_) {
        glDeleteProgram(program_id_);
        program_id_ = 0;
        uniform_cache_.clear();
    }
}

void Shader::Bind() const {
    if (program_id_) {
        glUseProgram(program_id_);
    }
}

void Shader::Unbind() const {
    glUseProgram(0);
}

int Shader::GetUniformLocation(std::string_view name) {
    const std::string key(name);
    auto it = uniform_cache_.find(key);
    if (it != uniform_cache_.end()) {
        return it->second;
    }
    const int loc = glGetUniformLocation(program_id_, key.c_str());
    uniform_cache_[key] = loc;
    return loc;
}

void Shader::SetInt(std::string_view name, int val) {
    Bind();
    const int loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform1i(loc, val);
    }
}

void Shader::SetFloat(std::string_view name, float val) {
    Bind();
    const int loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform1f(loc, val);
    }
}

void Shader::SetVec2(std::string_view name, const Vec2 &val) {
    Bind();
    const int loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform2f(loc, val.x, val.y);
    }
}

void Shader::SetVec4(std::string_view name, const Color &val) {
    Bind();
    const int loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform4f(loc, val.r, val.g, val.b, val.a);
    }
}

void Shader::SetMat4(std::string_view name, const Mat4 &val) {
    Bind();
    const int loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, val.m);
    }
}

Shader *Shader::GetDefault2D() {
    static Shader default_shader;
    static bool initialized = false;
    if (!initialized) {
        initialized = default_shader.LoadFromSource(kDefaultVertexShader, kDefaultFragmentShader);
        if (initialized) {
            default_shader.SetInt("u_texture", 0);
        }
    }
    return initialized ? &default_shader : nullptr;
}

} // namespace fake2d
