#pragma once

#include "fake2d/camera.h"
#include "fake2d/math.h"
#include "fake2d/shader.h"
#include "fake2d/sprite_batch.h"
#include "fake2d/texture.h"

#include <cstdint>
#include <memory>

namespace fake2d {

/// 2D Renderer managing camera, sprite batching, texture rendering, and OpenGL state.
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool Init(int width, int height);
    void Resize(int width, int height);

    /// Begin frame with clear color.
    void BeginFrame(float r, float g, float b, float a = 1.0f);
    void BeginFrame(const Color &clear_color = Color{0.08f, 0.10f, 0.14f, 1.0f});

    /// End frame and flush all pending draw commands.
    void EndFrame();

    void Shutdown();

    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }
    [[nodiscard]] bool IsReady() const { return ready_; }

    Camera2D &GetCamera() { return camera_; }
    [[nodiscard]] const Camera2D &GetCamera() const { return camera_; }

    SpriteBatch &GetSpriteBatch() { return batch_; }
    [[nodiscard]] const SpriteBatch &GetSpriteBatch() const { return batch_; }

    // Immediate draw convenience helpers:
    void DrawQuad(float x, float y, float w, float h, const Color &color = Color::White());
    void DrawQuad(const Rect &dst, const Color &color);

    void DrawSprite(const Texture2D &texture, float x, float y, float w = 0.0f, float h = 0.0f, const Color &tint = Color::White());
    void DrawSprite(const Texture2D &texture, const Rect &src, const Rect &dst, const Color &tint = Color::White());
    void DrawSpriteRotated(const Texture2D &texture, const Rect &src, const Rect &dst,
                           float angle_rad, const Vec2 &origin, const Color &tint = Color::White());

    [[nodiscard]] size_t DrawCallCount() const { return batch_.DrawCallCount(); }
    [[nodiscard]] size_t QuadCount() const { return batch_.QuadCount(); }

private:
    int width_ = 0;
    int height_ = 0;
    bool ready_ = false;

    Camera2D camera_;
    SpriteBatch batch_;
};

} // namespace fake2d
