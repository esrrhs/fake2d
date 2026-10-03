#pragma once

#include "fake2d/camera.h"
#include "fake2d/font.h"
#include "fake2d/math.h"
#include "fake2d/shader.h"
#include "fake2d/sprite_batch.h"
#include "fake2d/texture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace fake2d {

/// 2D Renderer managing camera, sprite batching, texture rendering, and OpenGL state.
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    /// logical_width/height are game coordinates (points).
    bool Init(int logical_width, int logical_height);
    /// Apply framebuffer size in physical pixels plus the logical point size
    /// and HiDPI content scale (1.0 on standard displays).
    void Resize(int fb_width, int fb_height, int logical_width, int logical_height,
                float content_scale = 1.0f);

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

    // --- batch draw-state pass-through (see SpriteBatch) ---
    void SetSortedBatch(bool sorted) { batch_.SetSorted(sorted); }
    void SetDrawLayer(int layer) { batch_.SetLayer(layer); }
    void SetDrawZ(float z) { batch_.SetZ(z); }
    void SetBlendMode(BlendMode mode) { batch_.SetBlendMode(mode); }

    // Immediate draw convenience helpers:
    void DrawQuad(float x, float y, float w, float h, const Color &color = Color::White());
    void DrawQuad(const Rect &dst, const Color &color);

    // --- line/debug primitives (flushed after the quads each frame) ---
    void DrawLine(float x1, float y1, float x2, float y2, const Color &color);
    void DrawRectOutline(const Rect &rect, const Color &color);
    void DrawCircleOutline(float cx, float cy, float radius, int segments,
                           const Color &color);

    void DrawSprite(const Texture2D &texture, float x, float y, float w = 0.0f, float h = 0.0f, const Color &tint = Color::White());
    void DrawSprite(const Texture2D &texture, const Rect &src, const Rect &dst, const Color &tint = Color::White());
    void DrawSpriteRotated(const Texture2D &texture, const Rect &src, const Rect &dst,
                           float angle_rad, const Vec2 &origin, const Color &tint = Color::White());

    // --- text (glyph quads are submitted into the same SpriteBatch) ---
    /// Rasterize the default font (system TTF, embedded 8x8 as fallback) at
    /// pixel_height. Called automatically at Init; safe to call again later.
    bool LoadDefaultFont(float pixel_height = 28.0f);
    Font &GetFont() { return font_; }
    [[nodiscard]] const Font &GetFont() const { return font_; }
    /// Draw text with its top-left at (x, y); scale multiplies the rasterized
    /// pixel height. Returns the pen x after the last glyph.
    float DrawText(std::string_view text, float x, float y, float scale = 1.0f,
                   const Color &tint = Color::White());
    /// Width of text at the given scale.
    [[nodiscard]] float MeasureText(std::string_view text, float scale = 1.0f) const;

    /// Read the current framebuffer and write it as a top-left-origin PNG.
    /// Call after a batch Flush (e.g. inside a frame callback) and before
    /// SwapBuffers. Intended for headless visual regression tests.
    bool SaveScreenshot(const std::string &path) const;

    // --- custom shaders (per-draw scope) ---
    /// Load a GLSL pair from files (attribute/uniform contract: a_pos,
    /// a_uv, a_color, u_view_projection, u_texture). Returns a 1-based slot
    /// id, or 0 on compile failure.
    int LoadShaderFromFile(const std::string &vert_path, const std::string &frag_path);
    void DestroyShader(int id);
    [[nodiscard]] Shader *GetShader(int id);
    void SetShaderFloat(int id, std::string_view name, float v);
    void SetShaderInt(int id, std::string_view name, int v);
    void SetShaderVec2(int id, std::string_view name, float x, float y);
    void SetShaderVec4(int id, std::string_view name, const Color &v);
    /// Route subsequent draws through shader `id`; 0 restores the default.
    /// Flushes the current batch first, so no draw is lost. The override
    /// resets to the default shader at the start of every frame.
    void UseShaderById(int id);
    /// Currently active override (nullptr = default shader). Lets subsystems
    /// that run their own batching cycle (Tilemap::Draw) stay in scope.
    [[nodiscard]] Shader *ActiveShader() const { return active_shader_; }

    [[nodiscard]] size_t DrawCallCount() const { return batch_.DrawCallCount(); }
    [[nodiscard]] size_t QuadCount() const { return batch_.QuadCount(); }

private:
    int width_ = 0;
    int height_ = 0;
    bool ready_ = false;

    Camera2D camera_;
    SpriteBatch batch_;
    Font font_;

    static constexpr std::size_t kMaxCustomShaders = 8;
    std::array<Shader, kMaxCustomShaders> shaders_{};
    Shader *active_shader_ = nullptr;
};

} // namespace fake2d
