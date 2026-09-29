#pragma once

#include "fake2d/math.h"
#include "fake2d/shader.h"
#include "fake2d/texture.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fake2d {

struct Vertex2D {
    Vec2 position;
    Vec2 uv;
    Color color;
};

class SpriteBatch {
public:
    SpriteBatch();
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch &) = delete;
    SpriteBatch &operator=(const SpriteBatch &) = delete;

    bool Init(size_t max_quads = 4096);
    void Shutdown();

    void Begin(const Mat4 &view_projection, Shader *shader = nullptr);
    void End();
    void Flush();

    /// Draw solid color quad.
    void DrawQuad(const Rect &dst, const Color &color);

    /// Draw entire texture mapped to dst rect.
    void DrawSprite(const Texture2D &texture, const Rect &dst, const Color &tint = Color::White());

    /// Draw sub-region (src) of texture mapped to dst rect.
    void DrawSprite(const Texture2D &texture, const Rect &src, const Rect &dst, const Color &tint = Color::White());

    /// Draw rotated sprite with rotation origin.
    void DrawSpriteRotated(const Texture2D &texture, const Rect &src, const Rect &dst,
                           float angle_rad, const Vec2 &origin, const Color &tint = Color::White());

    /// Low-level: submit a pre-transformed quad. Corners are world-space
    /// positions ordered TL, TR, BR, BL; uvs map the same corners.
    void DrawVertices(const Texture2D &texture, const Vec2 corners[4], const Vec2 uvs[4],
                      const Color &tint = Color::White());

    [[nodiscard]] size_t DrawCallCount() const { return draw_call_count_; }
    [[nodiscard]] size_t QuadCount() const { return total_quad_count_; }
    void ResetStats() { draw_call_count_ = 0; total_quad_count_ = 0; }

private:
    void EnsureCapacity(size_t quads_to_add, std::uint32_t texture_id);

    std::uint32_t vao_ = 0;
    std::uint32_t vbo_ = 0;
    std::uint32_t ibo_ = 0;

    size_t max_quads_ = 4096;
    size_t current_quads_ = 0;
    std::uint32_t current_texture_id_ = 0;

    std::vector<Vertex2D> vertices_;
    Mat4 current_view_projection_;
    Shader *current_shader_ = nullptr;
    bool in_begin_ = false;

    size_t draw_call_count_ = 0;
    size_t total_quad_count_ = 0;
};

} // namespace fake2d
