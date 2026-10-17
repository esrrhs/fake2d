#include "fake2d/shader.h"
#include "gl.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
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

bool IsIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// True when `line` starts with `word` followed by whitespace.
bool StartsWithWord(std::string_view line, std::string_view word) {
    return line.size() > word.size() && line.substr(0, word.size()) == word &&
           (line[word.size()] == ' ' || line[word.size()] == '\t');
}

// GLSL 1.30+ sources (the "#version 330 core" every shipped asset uses) do not
// compile on a legacy 2.1 context, which only understands GLSL 1.20 — macOS
// falls back to exactly that when no 3.3 core pixel format is available. This
// rewrites the subset the engine's shaders use into 1.20: the version line,
// layout() qualifiers (attribute locations are bound before linking instead),
// in/out declarations to attribute/varying, the fragment output to
// gl_FragColor, and texture() to texture2D(). Sources that already target
// 1.20 (or carry no #version) are returned unchanged.
std::string DowngradeToGlsl120(std::string_view src, bool vertex) {
    // Find the #version directive; only 1.30+ sources are rewritten.
    {
        const auto pos = src.find("#version");
        if (pos == std::string_view::npos) return std::string(src);
        const int version = std::atoi(std::string(src.substr(pos + 8, 8)).c_str());
        if (version < 130) return std::string(src);
    }

    std::string out;
    out.reserve(src.size() + 64);
    std::size_t start = 0;
    while (start <= src.size()) {
        std::size_t end = src.find('\n', start);
        const bool last = end == std::string_view::npos;
        if (last) end = src.size();
        std::string_view line = src.substr(start, end - start);

        std::size_t indent = 0;
        while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) ++indent;
        std::string_view body = line.substr(indent);
        const std::string lead(line.substr(0, indent));

        if (body.substr(0, 8) == "#version") {
            out += "#version 120";
        } else {
            // layout (location = N) in vec2 a_pos;  ->  in vec2 a_pos;
            std::string_view decl = body;
            if (decl.substr(0, 6) == "layout") {
                const auto close = decl.find(')');
                if (close != std::string_view::npos) {
                    decl = decl.substr(close + 1);
                    while (!decl.empty() && (decl[0] == ' ' || decl[0] == '\t')) {
                        decl.remove_prefix(1);
                    }
                }
            }
            if (StartsWithWord(decl, "smooth")) {
                decl.remove_prefix(6);
                while (!decl.empty() && (decl[0] == ' ' || decl[0] == '\t')) {
                    decl.remove_prefix(1);
                }
            }
            if (StartsWithWord(decl, "in")) {
                out += lead + (vertex ? "attribute" : "varying") + std::string(decl.substr(2));
            } else if (StartsWithWord(decl, "out")) {
                if (vertex) {
                    out += lead + "varying" + std::string(decl.substr(3));
                } else {
                    // out vec4 frag_color;  ->  #define frag_color gl_FragColor
                    std::string_view rest = decl.substr(3);
                    const auto semi = rest.find(';');
                    if (semi != std::string_view::npos) rest = rest.substr(0, semi);
                    std::size_t e = rest.size();
                    while (e > 0 && !IsIdentChar(rest[e - 1])) --e;
                    std::size_t b = e;
                    while (b > 0 && IsIdentChar(rest[b - 1])) --b;
                    out += "#define " + std::string(rest.substr(b, e - b)) + " gl_FragColor";
                }
            } else if (decl.size() != body.size()) {
                // A layout() qualifier on something else (e.g. a uniform):
                // keep the declaration without it.
                out += lead + std::string(decl);
            } else {
                out += std::string(line);
            }
        }
        if (last) break;
        out += '\n';
        start = end + 1;
    }

    // texture(...) -> texture2D(...): only the bare builtin name, not
    // identifiers that merely contain it (u_texture, texture2D, ...).
    std::string result;
    result.reserve(out.size() + 32);
    constexpr std::string_view kTex = "texture";
    for (std::size_t i = 0; i < out.size();) {
        if (out.compare(i, kTex.size(), kTex) == 0 && (i == 0 || !IsIdentChar(out[i - 1]))) {
            std::size_t j = i + kTex.size();
            while (j < out.size() && (out[j] == ' ' || out[j] == '\t')) ++j;
            if (j < out.size() && out[j] == '(') {
                result += "texture2D";
                i += kTex.size();
                continue;
            }
        }
        result += out[i++];
    }
    return result;
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

// GL 2.1 (legacy) variants: attribute/varying syntax, no layout qualifiers
// (locations are pinned with glBindAttribLocation before linking), and
// texture2D + gl_FragColor instead of texture() and out variables.
const char *kDefaultVertexShader120 = R"(#version 120
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;

uniform mat4 u_view_projection;

varying vec2 v_uv;
varying vec4 v_color;

void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_view_projection * vec4(a_pos, 0.0, 1.0);
}
)";

const char *kDefaultFragmentShader120 = R"(#version 120
varying vec2 v_uv;
varying vec4 v_color;

uniform sampler2D u_texture;

void main() {
    gl_FragColor = texture2D(u_texture, v_uv) * v_color;
}
)";

void BindDefaultAttribLocations(GLuint program) {
    glBindAttribLocation(program, 0, "a_pos");
    glBindAttribLocation(program, 1, "a_uv");
    glBindAttribLocation(program, 2, "a_color");
}

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

    // Legacy 2.1 contexts only take GLSL 1.20: rewrite 1.30+ sources down.
    std::string legacy_vert;
    std::string legacy_frag;
    if (glcaps::legacy_mode) {
        legacy_vert = DowngradeToGlsl120(vert_src, true);
        legacy_frag = DowngradeToGlsl120(frag_src, false);
        vert_src = legacy_vert;
        frag_src = legacy_frag;
    }

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
    // Legacy GLSL 120 shaders carry no layout() qualifiers; pin the attribute
    // locations explicitly so they match the VAO/attrib-pointer setup.
    if (glcaps::legacy_mode) {
        BindDefaultAttribLocations(prog);
    }
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
        if (glcaps::legacy_mode) {
            initialized = default_shader.LoadFromSource(kDefaultVertexShader120,
                                                        kDefaultFragmentShader120);
        } else {
            initialized = default_shader.LoadFromSource(kDefaultVertexShader,
                                                        kDefaultFragmentShader);
        }
        if (initialized) {
            default_shader.SetInt("u_texture", 0);
        }
    }
    return initialized ? &default_shader : nullptr;
}

} // namespace fake2d
