#include "fake2d/renderer.h"
#include "gl.h"

#include "stb/stb_image_write.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace fake2d {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Init(int logical_width, int logical_height) {
    width_ = logical_width;
    height_ = logical_height;

    camera_.SetViewport(static_cast<float>(logical_width), static_cast<float>(logical_height));
    camera_.SetContentScale(1.0f);

    if (!batch_.Init()) {
        return false;
    }

    // The embedded 8x8 font guarantees this succeeds even on fontless hosts.
    font_.LoadDefault(28.0f);

    ready_ = true;
    return true;
}

void Renderer::Resize(int fb_width, int fb_height, int logical_width, int logical_height,
                      float content_scale) {
    width_ = fb_width;
    height_ = fb_height;
    camera_.SetViewport(static_cast<float>(logical_width), static_cast<float>(logical_height));
    camera_.SetContentScale(content_scale);
    if (ready_) {
        glViewport(0, 0, fb_width, fb_height);
    }
}

void Renderer::BeginFrame(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    active_shader_ = nullptr;
    batch_.ResetStats();
    batch_.Begin(camera_.ViewProjectionMatrix());
}

void Renderer::BeginFrame(const Color &clear_color) {
    BeginFrame(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
}

void Renderer::EndFrame() {
    batch_.End();
    active_shader_ = nullptr;
}

int Renderer::LoadShaderFromFile(const std::string &vert_path,
                                 const std::string &frag_path) {
    std::ifstream vf(vert_path, std::ios::binary);
    std::ifstream ff(frag_path, std::ios::binary);
    if (!vf || !ff) {
        return 0;
    }
    const std::string vs((std::istreambuf_iterator<char>(vf)),
                         std::istreambuf_iterator<char>());
    const std::string fs((std::istreambuf_iterator<char>(ff)),
                         std::istreambuf_iterator<char>());
    for (std::size_t i = 0; i < shaders_.size(); ++i) {
        if (!shaders_[i].IsValid()) {
            if (!shaders_[i].LoadFromSource(vs, fs)) {
                shaders_[i].Destroy();
                return 0;
            }
            shaders_[i].SetInt("u_texture", 0);
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

void Renderer::DestroyShader(int id) {
    if (id <= 0 || static_cast<std::size_t>(id) > shaders_.size()) {
        return;
    }
    auto &slot = shaders_[static_cast<std::size_t>(id) - 1];
    if (active_shader_ == &slot) {
        UseShaderById(0);
    }
    slot.Destroy();
}

Shader *Renderer::GetShader(int id) {
    if (id <= 0 || static_cast<std::size_t>(id) > shaders_.size()) {
        return nullptr;
    }
    Shader *s = &shaders_[static_cast<std::size_t>(id) - 1];
    return s->IsValid() ? s : nullptr;
}

void Renderer::SetShaderFloat(int id, std::string_view name, float v) {
    if (Shader *s = GetShader(id)) s->SetFloat(name, v);
}

void Renderer::SetShaderInt(int id, std::string_view name, int v) {
    if (Shader *s = GetShader(id)) s->SetInt(name, v);
}

void Renderer::SetShaderVec2(int id, std::string_view name, float x, float y) {
    if (Shader *s = GetShader(id)) s->SetVec2(name, {x, y});
}

void Renderer::SetShaderVec4(int id, std::string_view name, const Color &v) {
    if (Shader *s = GetShader(id)) s->SetVec4(name, v);
}

void Renderer::UseShaderById(int id) {
    Shader *target = id == 0 ? nullptr : GetShader(id);
    if (id != 0 && target == nullptr) {
        return;
    }
    if (target == active_shader_) {
        return;
    }
    // Restart the batch with the other program; switching resets the
    // per-batch layer/z/blend state (same contract as Tilemap::Draw).
    batch_.End();
    batch_.Begin(camera_.ViewProjectionMatrix(),
                 target ? target : Shader::GetDefault2D());
    active_shader_ = target;
}

void Renderer::Shutdown() {
    if (ready_) {
        // Release GPU resources while the GL context is still current.
        for (Shader &s : shaders_) {
            if (s.IsValid()) {
                s.Destroy();
            }
        }
        active_shader_ = nullptr;
        // Release the glyph atlas while the GL context is still current.
        font_.Reset();
        batch_.Shutdown();
        ready_ = false;
    }
}

void Renderer::DrawQuad(float x, float y, float w, float h, const Color &color) {
    batch_.DrawQuad({x, y, w, h}, color);
}

void Renderer::DrawQuad(const Rect &dst, const Color &color) {
    batch_.DrawQuad(dst, color);
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2, const Color &color) {
    batch_.DrawLine({x1, y1}, {x2, y2}, color);
}

void Renderer::DrawRectOutline(const Rect &rect, const Color &color) {
    batch_.DrawRectOutline(rect, color);
}

void Renderer::DrawCircleOutline(float cx, float cy, float radius, int segments,
                                 const Color &color) {
    batch_.DrawCircleOutline({cx, cy}, radius, segments, color);
}

void Renderer::DrawSprite(const Texture2D &texture, float x, float y, float w, float h, const Color &tint) {
    const float final_w = (w > 0.0f) ? w : static_cast<float>(texture.Width());
    const float final_h = (h > 0.0f) ? h : static_cast<float>(texture.Height());
    batch_.DrawSprite(texture, {x, y, final_w, final_h}, tint);
}

void Renderer::DrawSprite(const Texture2D &texture, const Rect &src, const Rect &dst, const Color &tint) {
    batch_.DrawSprite(texture, src, dst, tint);
}

void Renderer::DrawSpriteRotated(const Texture2D &texture, const Rect &src, const Rect &dst,
                                float angle_rad, const Vec2 &origin, const Color &tint) {
    batch_.DrawSpriteRotated(texture, src, dst, angle_rad, origin, tint);
}

bool Renderer::LoadDefaultFont(float pixel_height) {
    return font_.LoadDefault(pixel_height);
}

float Renderer::DrawText(std::string_view text, float x, float y, float scale, const Color &tint) {
    return font_.DrawText(batch_, text, x, y, scale, tint);
}

float Renderer::MeasureText(std::string_view text, float scale) const {
    return font_.MeasureText(text) * scale;
}

bool Renderer::SaveScreenshot(const std::string &path) const {
    if (!ready_ || width_ <= 0 || height_ <= 0) {
        return false;
    }
    const size_t row_bytes = static_cast<size_t>(width_) * 3;
    std::vector<unsigned char> pixels(row_bytes * static_cast<size_t>(height_));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // glReadPixels is bottom-left origin; flip rows for a top-left PNG.
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < height_; ++y) {
        std::memcpy(&flipped[static_cast<size_t>(height_ - 1 - y) * row_bytes],
                    &pixels[static_cast<size_t>(y) * row_bytes], row_bytes);
    }
    const int ok = stbi_write_png(path.c_str(), width_, height_, 3, flipped.data(),
                                  static_cast<int>(row_bytes));
    if (ok == 0) {
        std::fprintf(stderr, "fake2d: failed to write screenshot %s\n", path.c_str());
    }
    return ok != 0;
}

} // namespace fake2d
