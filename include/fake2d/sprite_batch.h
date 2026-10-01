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

struct LineVertex {
    Vec2 position;
    Color color;
};

/// Alpha blending (normal transparency) or additive (glow/particles).
enum class BlendMode : std::uint8_t {
    Alpha = 0,
    Additive = 1,
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

    // --- batching strategy & draw state ------------------------------------
    /// Deferred mode: quads are collected and sorted at Flush by
    /// (layer, z, blend, texture), merging same-texture quads into one draw
    /// call regardless of submission order. The contract: within one
    /// (layer, z) bucket draws must not rely on submission order across
    /// different textures. Default false = immediate submission-order
    /// flushing (exact legacy behavior).
    void SetSorted(bool sorted);
    [[nodiscard]] bool IsSorted() const { return sorted_; }
    /// Tags subsequent draws; only takes part in sorting when sorted mode is
    /// on. Resets to (0, 0, Alpha) on every Begin.
    void SetLayer(int layer) { current_layer_ = layer; }
    void SetZ(float z) { current_z_ = z; }
    void SetBlendMode(BlendMode mode) { current_blend_ = mode; }

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

    // --- line primitives (GL_LINES, flushed after quads at End) ---
    void DrawLine(const Vec2 &a, const Vec2 &b, const Color &color);
    void DrawRectOutline(const Rect &rect, const Color &color);
    /// Polygonal circle outline with `segments` straight edges.
    void DrawCircleOutline(const Vec2 &center, float radius, int segments,
                           const Color &color);
    void FlushLines();

    [[nodiscard]] size_t DrawCallCount() const { return draw_call_count_; }
    [[nodiscard]] size_t QuadCount() const { return total_quad_count_; }
    void ResetStats() { draw_call_count_ = 0; total_quad_count_ = 0; }

private:
    /// One submitted quad with its sort key.
    struct Command {
        std::uint32_t texture_id = 0;
        std::uint32_t sequence = 0; // submission order (stable tiebreak)
        size_t vertex_offset = 0;   // first of 4 vertices in vertices_
        int layer = 0;
        float z = 0.0f;
        BlendMode blend = BlendMode::Alpha;
    };

    void EnsureCapacity(size_t quads_to_add, std::uint32_t texture_id);
    void RecordCommand(std::uint32_t texture_id);
    void FlushImmediate();
    void FlushSorted();
    static void ApplyBlend(BlendMode mode);

    std::uint32_t vao_ = 0;
    std::uint32_t vbo_ = 0;
    std::uint32_t ibo_ = 0;

    std::uint32_t line_vao_ = 0;
    std::uint32_t line_vbo_ = 0;
    size_t max_lines_ = 2048;
    size_t current_lines_ = 0;
    std::vector<LineVertex> line_vertices_;

    size_t max_quads_ = 4096;
    size_t current_quads_ = 0;
    std::uint32_t current_texture_id_ = 0;

    std::vector<Vertex2D> vertices_;
    std::vector<Vertex2D> sorted_vertices_;
    std::vector<Command> commands_;
    std::uint32_t next_sequence_ = 0;

    bool sorted_ = false;
    int current_layer_ = 0;
    float current_z_ = 0.0f;
    BlendMode current_blend_ = BlendMode::Alpha;
    BlendMode pending_blend_ = BlendMode::Alpha; // blend of the unflushed run

    Mat4 current_view_projection_;
    Shader *current_shader_ = nullptr;
    bool in_begin_ = false;

    size_t draw_call_count_ = 0;
    size_t total_quad_count_ = 0;
};

} // namespace fake2d
