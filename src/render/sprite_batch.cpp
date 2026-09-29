#include "fake2d/sprite_batch.h"
#include "gl.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fake2d {

SpriteBatch::SpriteBatch() = default;

SpriteBatch::~SpriteBatch() {
    Shutdown();
}

bool SpriteBatch::Init(size_t max_quads) {
    Shutdown();
    max_quads_ = max_quads;
    vertices_.resize(max_quads_ * 4);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(max_quads_ * 4 * sizeof(Vertex2D)), nullptr, GL_DYNAMIC_DRAW);

    // Setup vertex attributes
    // location 0: vec2 a_pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<const void *>(offsetof(Vertex2D, position)));

    // location 1: vec2 a_uv
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<const void *>(offsetof(Vertex2D, uv)));

    // location 2: vec4 a_color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<const void *>(offsetof(Vertex2D, color)));

    // Generate indices: 6 indices per quad (two triangles: 0-1-2 and 2-3-0)
    std::vector<std::uint32_t> indices(max_quads_ * 6);
    for (size_t i = 0; i < max_quads_; ++i) {
        const auto base_v = static_cast<std::uint32_t>(i * 4);
        indices[i * 6 + 0] = base_v + 0;
        indices[i * 6 + 1] = base_v + 1;
        indices[i * 6 + 2] = base_v + 2;
        indices[i * 6 + 3] = base_v + 2;
        indices[i * 6 + 4] = base_v + 3;
        indices[i * 6 + 5] = base_v + 0;
    }

    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return vao_ != 0;
}

void SpriteBatch::Shutdown() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ibo_) {
        glDeleteBuffers(1, &ibo_);
        ibo_ = 0;
    }
    vertices_.clear();
    current_quads_ = 0;
    current_texture_id_ = 0;
    in_begin_ = false;
}

void SpriteBatch::Begin(const Mat4 &view_projection, Shader *shader) {
    in_begin_ = true;
    current_view_projection_ = view_projection;
    current_shader_ = shader ? shader : Shader::GetDefault2D();
    current_quads_ = 0;
    current_texture_id_ = 0;
}

void SpriteBatch::End() {
    if (!in_begin_) return;
    Flush();
    in_begin_ = false;
}

void SpriteBatch::EnsureCapacity(size_t quads_to_add, std::uint32_t texture_id) {
    if (current_texture_id_ != 0 && current_texture_id_ != texture_id) {
        Flush();
    }
    if (current_quads_ + quads_to_add > max_quads_) {
        Flush();
    }
    current_texture_id_ = texture_id;
}

void SpriteBatch::Flush() {
    if (current_quads_ == 0 || !vao_ || !current_shader_) {
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    current_shader_->Bind();
    current_shader_->SetMat4("u_view_projection", current_view_projection_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current_texture_id_);
    current_shader_->SetInt("u_texture", 0);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(current_quads_ * 4 * sizeof(Vertex2D)),
                    vertices_.data());

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(current_quads_ * 6), GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    ++draw_call_count_;
    total_quad_count_ += current_quads_;
    current_quads_ = 0;
}

void SpriteBatch::DrawQuad(const Rect &dst, const Color &color) {
    EnsureCapacity(1, Texture2D::White().Id());

    const size_t v_idx = current_quads_ * 4;
    const float l = dst.x;
    const float t = dst.y;
    const float r = dst.x + dst.width;
    const float b = dst.y + dst.height;

    // Top-left
    vertices_[v_idx + 0] = {{l, t}, {0.0f, 0.0f}, color};
    // Top-right
    vertices_[v_idx + 1] = {{r, t}, {1.0f, 0.0f}, color};
    // Bottom-right
    vertices_[v_idx + 2] = {{r, b}, {1.0f, 1.0f}, color};
    // Bottom-left
    vertices_[v_idx + 3] = {{l, b}, {0.0f, 1.0f}, color};

    ++current_quads_;
}

void SpriteBatch::DrawSprite(const Texture2D &texture, const Rect &dst, const Color &tint) {
    DrawSprite(texture, {0.0f, 0.0f, static_cast<float>(texture.Width()), static_cast<float>(texture.Height())}, dst, tint);
}

void SpriteBatch::DrawSprite(const Texture2D &texture, const Rect &src, const Rect &dst, const Color &tint) {
    if (!texture.IsValid()) return;
    EnsureCapacity(1, texture.Id());

    const size_t v_idx = current_quads_ * 4;
    const float l = dst.x;
    const float t = dst.y;
    const float r = dst.x + dst.width;
    const float b = dst.y + dst.height;

    const float tw = static_cast<float>(texture.Width());
    const float th = static_cast<float>(texture.Height());
    const float u0 = src.x / tw;
    const float v0 = src.y / th;
    const float u1 = (src.x + src.width) / tw;
    const float v1 = (src.y + src.height) / th;

    vertices_[v_idx + 0] = {{l, t}, {u0, v0}, tint};
    vertices_[v_idx + 1] = {{r, t}, {u1, v0}, tint};
    vertices_[v_idx + 2] = {{r, b}, {u1, v1}, tint};
    vertices_[v_idx + 3] = {{l, b}, {u0, v1}, tint};

    ++current_quads_;
}

void SpriteBatch::DrawSpriteRotated(const Texture2D &texture, const Rect &src, const Rect &dst,
                                   float angle_rad, const Vec2 &origin, const Color &tint) {
    if (!texture.IsValid()) return;
    EnsureCapacity(1, texture.Id());

    const size_t v_idx = current_quads_ * 4;
    const float cos_a = std::cos(angle_rad);
    const float sin_a = std::sin(angle_rad);

    auto transform = [&](float lx, float ly) -> Vec2 {
        const float ox = lx - origin.x;
        const float oy = ly - origin.y;
        return {
            origin.x + (ox * cos_a - oy * sin_a) + dst.x,
            origin.y + (ox * sin_a + oy * cos_a) + dst.y
        };
    };

    const float tw = static_cast<float>(texture.Width());
    const float th = static_cast<float>(texture.Height());
    const float u0 = src.x / tw;
    const float v0 = src.y / th;
    const float u1 = (src.x + src.width) / tw;
    const float v1 = (src.y + src.height) / th;

    vertices_[v_idx + 0] = {transform(0.0f, 0.0f), {u0, v0}, tint};
    vertices_[v_idx + 1] = {transform(dst.width, 0.0f), {u1, v0}, tint};
    vertices_[v_idx + 2] = {transform(dst.width, dst.height), {u1, v1}, tint};
    vertices_[v_idx + 3] = {transform(0.0f, dst.height), {u0, v1}, tint};

    ++current_quads_;
}

void SpriteBatch::DrawVertices(const Texture2D &texture, const Vec2 corners[4], const Vec2 uvs[4],
                               const Color &tint) {
    if (!texture.IsValid()) return;
    EnsureCapacity(1, texture.Id());

    const size_t v_idx = current_quads_ * 4;
    for (size_t i = 0; i < 4; ++i) {
        vertices_[v_idx + i] = {corners[i], uvs[i], tint};
    }

    ++current_quads_;
}

} // namespace fake2d
