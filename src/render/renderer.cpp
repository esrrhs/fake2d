#include "fake2d/renderer.h"
#include "gl.h"

namespace fake2d {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Init(int width, int height) {
    width_ = width;
    height_ = height;

    camera_.SetViewport(static_cast<float>(width), static_cast<float>(height));

    if (!batch_.Init()) {
        return false;
    }

    ready_ = true;
    Resize(width, height);
    return true;
}

void Renderer::Resize(int width, int height) {
    width_ = width;
    height_ = height;
    camera_.SetViewport(static_cast<float>(width), static_cast<float>(height));
    if (ready_) {
        glViewport(0, 0, width_, height_);
    }
}

void Renderer::BeginFrame(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    batch_.ResetStats();
    batch_.Begin(camera_.ViewProjectionMatrix());
}

void Renderer::BeginFrame(const Color &clear_color) {
    BeginFrame(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
}

void Renderer::EndFrame() {
    batch_.End();
}

void Renderer::Shutdown() {
    if (ready_) {
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

} // namespace fake2d
