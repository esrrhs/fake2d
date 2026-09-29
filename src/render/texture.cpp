#include "fake2d/texture.h"
#include "gl.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <cstdio>
#include <string>

namespace fake2d {

namespace {

GLenum ToGLFilter(TextureFilter f) {
    switch (f) {
        case TextureFilter::Nearest: return GL_NEAREST;
        case TextureFilter::Linear:  return GL_LINEAR;
    }
    return GL_LINEAR;
}

GLenum ToGLWrap(TextureWrap w) {
    switch (w) {
        case TextureWrap::Clamp:  return GL_CLAMP_TO_EDGE;
        case TextureWrap::Repeat: return GL_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

} // namespace

Texture2D::Texture2D() = default;

Texture2D::~Texture2D() {
    Destroy();
}

Texture2D::Texture2D(Texture2D &&other) noexcept
    : id_(other.id_),
      width_(other.width_),
      height_(other.height_),
      channels_(other.channels_) {
    other.id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    other.channels_ = 0;
}

Texture2D &Texture2D::operator=(Texture2D &&other) noexcept {
    if (this != &other) {
        Destroy();
        id_ = other.id_;
        width_ = other.width_;
        height_ = other.height_;
        channels_ = other.channels_;
        other.id_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.channels_ = 0;
    }
    return *this;
}

bool Texture2D::Create(int width, int height, const uint8_t *data, int channels,
                       TextureFilter filter, TextureWrap wrap) {
    Destroy();

    width_ = width;
    height_ = height;
    channels_ = channels;

    GLenum internal_format = GL_RGBA8;
    GLenum format = GL_RGBA;
    if (channels == 1) {
        internal_format = GL_R8;
        format = GL_RED;
    } else if (channels == 3) {
        internal_format = GL_RGB8;
        format = GL_RGB;
    } else if (channels == 4) {
        internal_format = GL_RGBA8;
        format = GL_RGBA;
    }

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);

    const GLenum gl_filter = ToGLFilter(filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);

    const GLenum gl_wrap = ToGLWrap(wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap);

    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    return id_ != 0;
}

bool Texture2D::LoadFromFile(std::string_view file_path, TextureFilter filter, TextureWrap wrap) {
    const std::string path(file_path);
    stbi_set_flip_vertically_on_load(0);
    int w = 0, h = 0, ch = 0;
    uint8_t *pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!pixels) {
        std::fprintf(stderr, "fake2d: Failed to load texture file '%s': %s\n", path.c_str(), stbi_failure_reason());
        return false;
    }

    const bool ok = Create(w, h, pixels, 4, filter, wrap);
    stbi_image_free(pixels);
    return ok;
}

void Texture2D::Destroy() {
    if (id_) {
        glDeleteTextures(1, &id_);
        id_ = 0;
        width_ = 0;
        height_ = 0;
        channels_ = 0;
    }
}

void Texture2D::Bind(uint32_t slot) const {
    if (id_) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, id_);
    }
}

void Texture2D::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

const Texture2D &Texture2D::White() {
    static Texture2D white_tex;
    static bool initialized = false;
    if (!initialized) {
        const uint8_t white_pixel[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        initialized = white_tex.Create(1, 1, white_pixel, 4, TextureFilter::Nearest, TextureWrap::Clamp);
    }
    return white_tex;
}

} // namespace fake2d
