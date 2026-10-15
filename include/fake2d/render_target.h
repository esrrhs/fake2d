#pragma once

#include "fake2d/math.h"
#include "fake2d/texture.h"

#include <array>
#include <cstdint>
#include <memory>

namespace fake2d {

/// Offscreen render target: an FBO with a single RGBA colour texture
/// attached. Render into it with Renderer::BeginRenderTarget, then draw the
/// result like any other sprite with Renderer::DrawRenderTarget.
///
/// Typical uses: bake static art once (backgrounds, prerendered text) and
/// blit it every frame, accumulate feedback effects (motion trails, fog of
/// war, minimaps), or post-process a scene before it reaches the screen.
///
/// Coordinates inside a target are top-left origin with (0,0) at the target's
/// own top-left and 1 unit = 1 pixel, independent of the window camera.
class RenderTarget {
public:
    /// Guard against absurd allocations; also the practical GL limit on
    /// every driver the engine targets.
    static constexpr int kMaxSize = 8192;

    RenderTarget();
    ~RenderTarget();

    RenderTarget(const RenderTarget &) = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;

    /// Allocate an RGBA target of `width` x `height` pixels. Returns false
    /// (and logs once) when the size is out of range, or when the context
    /// exposes no framebuffer objects at all (the legacy GL2 path without
    /// EXT_framebuffer_object); gameplay can probe with IsValid().
    bool Create(int width, int height);
    void Destroy();

    [[nodiscard]] bool IsValid() const { return fbo_ != 0; }
    [[nodiscard]] int Width() const { return texture_.Width(); }
    [[nodiscard]] int Height() const { return texture_.Height(); }
    /// Colour attachment; draw it with Renderer::DrawRenderTarget (which
    /// flips V for you — FBOs render bottom-up, sprites sample top-down).
    [[nodiscard]] const Texture2D &Texture() const { return texture_; }
    [[nodiscard]] std::uint32_t FramebufferId() const { return fbo_; }

    /// Make every subsequent draw land in this target.
    void Bind();
    /// Go back to `window_fbo`, the framebuffer the window presented to
    /// (0 for a normal windowed context, but a headless context may render
    /// into its own offscreen FBO — never assume it is 0).
    static void Unbind(std::uint32_t window_fbo);

    /// Read one pixel back; `x`/`y` use the same top-left origin draws use.
    /// Flush pending draws first (see Renderer::EndRenderTarget). The current
    /// framebuffer binding is preserved. Returns false when out of bounds or
    /// the target is invalid.
    bool ReadPixel(int x, int y, Color &out) const;

private:
    Texture2D texture_;
    std::uint32_t fbo_ = 0;
};

/// Fixed pool of render targets addressed by a 1-based id (0 = the window
/// itself), the same handle scheme as TilemapLibrary.
class RenderTargetLibrary {
public:
    static constexpr std::size_t kMaxTargets = 8;

    /// Returns a 1-based id, or 0 when the pool is full or creation failed.
    int Create(int width, int height);
    [[nodiscard]] RenderTarget *Get(int id);
    [[nodiscard]] const RenderTarget *Get(int id) const;
    void Destroy(int id);
    void Clear();
    [[nodiscard]] int Count() const;

private:
    struct Slot {
        std::unique_ptr<RenderTarget> target;
        bool used = false;
    };
    std::array<Slot, kMaxTargets> slots_;
};

} // namespace fake2d
