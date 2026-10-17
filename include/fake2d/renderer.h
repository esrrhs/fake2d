#pragma once

#include "fake2d/camera.h"
#include "fake2d/font.h"
#include "fake2d/math.h"
#include "fake2d/render_target.h"
#include "fake2d/shader.h"
#include "fake2d/sprite_batch.h"
#include "fake2d/texture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32) && defined(DrawText)
// windows.h may have been included before this header (e.g. via GL or
// fakelua); drop the wingdi.h DrawText macro that masks the renderer API.
#undef DrawText
#endif
#if defined(_WIN32) && defined(DrawTextEx)
// ... and DrawTextEx -> DrawTextExA/W, which masks the aligned-text API.
#undef DrawTextEx
#endif

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

    /// The frame clock, in seconds. Fed by the engine every frame and stamped
    /// onto any custom shader that declares a `u_time` uniform, so an animated
    /// pass needs no per-script bookkeeping (and no extra binding).
    void SetTime(float seconds) { time_ = seconds; }
    [[nodiscard]] float Time() const { return time_; }

    void Shutdown();

    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }
    [[nodiscard]] bool IsReady() const { return ready_; }

    /// The window camera. Note this is the *window* camera even while a
    /// render target is bound: subsystems that draw into the active surface
    /// (e.g. Tilemap) must use ActiveCamera instead.
    Camera2D &GetCamera() { return camera_; }
    [[nodiscard]] const Camera2D &GetCamera() const { return camera_; }

    /// Camera of the surface being drawn into: the target's canvas camera
    /// while a render target is bound, otherwise the window camera.
    Camera2D &ActiveCamera() {
        return active_target_ != 0 ? target_camera_ : camera_;
    }
    [[nodiscard]] const Camera2D &ActiveCamera() const {
        return active_target_ != 0 ? target_camera_ : camera_;
    }

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
    /// Mirrored variant: flip_x/flip_y mirror the source region via UV swap.
    void DrawSpriteFlipped(const Texture2D &texture, const Rect &src, const Rect &dst,
                           bool flip_x, bool flip_y, const Color &tint = Color::White());
    void DrawSpriteRotated(const Texture2D &texture, const Rect &src, const Rect &dst,
                           float angle_rad, const Vec2 &origin, const Color &tint = Color::White());

    /// Nine-slice panel (see SpriteBatch::DrawNineSlice): corners keep their
    /// source pixel size, edges/center stretch.
    void DrawNineSlice(const Texture2D &texture, const Rect &src, const Rect &dst,
                       float corner_x, float corner_y, const Color &tint = Color::White());

    // --- offscreen render targets -------------------------------------------
    RenderTargetLibrary &GetRenderTargets() { return targets_; }
    [[nodiscard]] const RenderTargetLibrary &GetRenderTargets() const { return targets_; }

    /// Redirect every subsequent draw into render target `id` (1-based, as
    /// returned by RenderTargetLibrary::Create). The pending batch is flushed
    /// first so nothing already submitted leaks into the target, and the
    /// camera is replaced by a top-left-origin canvas of the target's own
    /// pixel size. Returns false for id 0, unknown ids, invalid targets, or
    /// while another target is already active (no nesting).
    bool BeginRenderTarget(int id);
    /// Flush and return to the window framebuffer. Safe to call when no
    /// target is active; also done automatically by EndFrame.
    void EndRenderTarget();
    /// Active target id, or 0 when drawing to the window.
    [[nodiscard]] int ActiveRenderTarget() const { return active_target_; }

    /// Move the bound target's canvas camera. A fresh target starts at (0,0)
    /// with 1 unit = 1 pixel; pointing it at a world offset is what lets
    /// whole-world draws (`Tilemap::Draw`) render the *camera's* view into
    /// the target for a post-processing pass. Returns false when no target
    /// is bound (the window camera is owned by the game, not by this call).
    /// Flushes the pending batch so later draws use the new transform.
    bool SetTargetCamera(float x, float y);

    /// Clear the active target — or the window when no target is bound — to a
    /// solid colour. Pending draws are flushed first.
    void ClearActiveTarget(const Color &clear_color);

    /// Draw a target's colour texture into a world rect. V is flipped for
    /// you: FBOs render bottom-up while sprite UVs sample top-down, so the
    /// result matches what was drawn into the target. Ignored when the id is
    /// unknown, invalid, or is the target currently being rendered into
    /// (drawing a surface into itself is undefined).
    void DrawRenderTarget(int id, float x, float y, float w, float h,
                          const Color &tint = Color::White());
    void DrawRenderTarget(int id, const Rect &dst, const Color &tint = Color::White());
    void DrawRenderTarget(int id, const Rect &src, const Rect &dst,
                          const Color &tint = Color::White());

    // --- scissor clipping ----------------------------------------------------
    /// Restrict all drawing to a rectangle in logical points (top-left
    /// origin, like every game coordinate). Cleared at the start of each
    /// frame; nest manually if you need stacked regions.
    void SetClipRect(const Rect &points_rect);
    void ClearClipRect();

    /// Blit a target across the whole of the surface currently being drawn
    /// into (the window, or another target) — the post-processing present.
    /// V is flipped like DrawRenderTarget. `shader` overrides the program for
    /// this blit only (nullptr = the current override, or the default); it
    /// does not touch Renderer's active override, so the rest of the frame
    /// keeps its shader. Ignored for unknown/invalid ids and for a target
    /// drawn into itself.
    void PresentRenderTarget(int id, Shader *shader = nullptr,
                             const Color &tint = Color::White());

    /// One ping-pong pass: blit `src` across the whole canvas of target `dst`,
    /// optionally through `shader`. Equivalent to
    /// `BeginRenderTarget(dst); PresentRenderTarget(src, shader); EndRenderTarget();`
    /// but it cannot be got wrong — it refuses `src == dst` (sampling the
    /// surface being drawn into is undefined), the window as either side, and
    /// any nesting. `dst` is *not* cleared first: the blit covers the whole
    /// canvas, so clear manually when the source is translucent.
    bool CopyRenderTarget(int src, int dst, Shader *shader = nullptr,
                          const Color &tint = Color::White());

    /// Reallocate a target at a new pixel size; the old contents are lost.
    /// Refused for unknown ids and for the target currently bound (its FBO is
    /// in use). Half- and quarter-resolution passes are the reason this
    /// exists: a blur over a downsampled surface costs a fraction of the taps.
    bool ResizeRenderTarget(int id, int width, int height);

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
    /// Aligned, word-wrapped text (see Font::DrawTextEx). Returns the y just
    /// below the last line.
    float DrawTextEx(std::string_view text, float x, float y, float scale,
                     int align, float wrap_width, const Color &tint = Color::White());
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
    /// View-projection of the surface being drawn into: the window camera, or
    /// the target's own canvas while a render target is bound.
    [[nodiscard]] const Mat4 &ActiveViewProjection() const;

    /// Push `u_time` and `u_resolution` onto every loaded custom shader that
    /// declares them. Shaders that do not are untouched — a uniform the
    /// program never declared has no location, and setting one is a no-op.
    /// The resolution is the surface about to be drawn into, so a pass run at
    /// half size is told it is running at half size, not at window size.
    void StampAutoUniforms(int width, int height);

    int width_ = 0;
    int height_ = 0;
    float time_ = 0.0f;
    bool ready_ = false;

    Camera2D camera_;
    SpriteBatch batch_;
    Font font_;
    RenderTargetLibrary targets_;
    /// 1-based id of the bound target; 0 = the window.
    int active_target_ = 0;
    /// Canvas camera for the active target: top-left origin, own pixel size.
    Camera2D target_camera_;
    /// Framebuffer the window presents to, captured at Init. Usually 0, but a
    /// headless context renders through its own offscreen FBO, so EndRenderTarget
    /// must restore this exact id rather than assume 0.
    std::uint32_t window_fbo_ = 0;

    static constexpr std::size_t kMaxCustomShaders = 8;
    std::array<Shader, kMaxCustomShaders> shaders_{};
    Shader *active_shader_ = nullptr;
};

} // namespace fake2d
