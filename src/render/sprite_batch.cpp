#include "fake2d/sprite_batch.h"
#include "gl.h"

#include <algorithm>
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
    sorted_vertices_.resize(max_quads_ * 4);
    commands_.reserve(max_quads_);

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

    // --- line stream (pos + color, same default shader) ---
    line_vertices_.resize(max_lines_ * 2);
    glGenVertexArrays(1, &line_vao_);
    glBindVertexArray(line_vao_);
    glGenBuffers(1, &line_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(max_lines_ * 2 * sizeof(LineVertex)),
                 nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          reinterpret_cast<const void *>(offsetof(LineVertex, position)));
    glDisableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          reinterpret_cast<const void *>(offsetof(LineVertex, color)));
    glBindVertexArray(0);

    return vao_ != 0;
}

void SpriteBatch::Shutdown() {
    if (line_vao_) {
        glDeleteVertexArrays(1, &line_vao_);
        line_vao_ = 0;
    }
    if (line_vbo_) {
        glDeleteBuffers(1, &line_vbo_);
        line_vbo_ = 0;
    }
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
    sorted_vertices_.clear();
    line_vertices_.clear();
    commands_.clear();
    current_quads_ = 0;
    current_lines_ = 0;
    current_texture_id_ = 0;
    in_begin_ = false;
}

void SpriteBatch::Begin(const Mat4 &view_projection, Shader *shader) {
    in_begin_ = true;
    current_view_projection_ = view_projection;
    current_shader_ = shader ? shader : Shader::GetDefault2D();
    current_quads_ = 0;
    current_lines_ = 0;
    current_texture_id_ = 0;
    commands_.clear();
    next_sequence_ = 0;
    current_layer_ = 0;
    current_z_ = 0.0f;
    current_blend_ = BlendMode::Alpha;
}

void SpriteBatch::End() {
    if (!in_begin_) return;
    Flush();
    FlushLines();
    in_begin_ = false;
}

void SpriteBatch::SetSorted(bool sorted) {
    // Switching strategy mid-frame would orphan buffered geometry; flush first.
    if (sorted != sorted_ && in_begin_) {
        Flush();
    }
    sorted_ = sorted;
}

void SpriteBatch::EnsureCapacity(size_t quads_to_add, std::uint32_t texture_id) {
    if (!sorted_ && current_quads_ > 0 &&
        (current_texture_id_ != texture_id || current_blend_ != pending_blend_)) {
        // Immediate mode: a texture or blend-mode switch closes the run.
        FlushImmediate();
    }
    if (current_quads_ + quads_to_add > max_quads_) {
        Flush();
    }
    current_texture_id_ = texture_id;
    pending_blend_ = current_blend_;
}

void SpriteBatch::RecordCommand(std::uint32_t texture_id) {
    if (sorted_) {
        Command cmd;
        cmd.texture_id = texture_id;
        cmd.sequence = next_sequence_++;
        cmd.vertex_offset = current_quads_ * 4;
        cmd.layer = current_layer_;
        cmd.z = current_z_;
        cmd.blend = current_blend_;
        commands_.push_back(cmd);
    }
}

void SpriteBatch::ApplyBlend(BlendMode mode) {
    glEnable(GL_BLEND);
    switch (mode) {
        case BlendMode::Additive:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BlendMode::Alpha:
        default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

void SpriteBatch::Flush() {
    if (current_quads_ == 0) {
        return;
    }
    if (sorted_) {
        FlushSorted();
    } else {
        FlushImmediate();
    }
}

void SpriteBatch::FlushImmediate() {
    if (current_quads_ == 0 || !vao_ || !current_shader_) {
        return;
    }

    ApplyBlend(pending_blend_);

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

void SpriteBatch::FlushSorted() {
    if (current_quads_ == 0 || commands_.empty() || !vao_ || !current_shader_) {
        current_quads_ = 0;
        return;
    }

    // Multi-key order: layer -> z -> blend mode -> texture. Stable sort keeps
    // submission sequence inside every fully-equal bucket, which preserves
    // overlap order for quads sharing one texture.
    std::stable_sort(commands_.begin(), commands_.end(), [](const Command &a, const Command &b) {
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.z != b.z) return a.z < b.z;
        if (a.blend != b.blend) return static_cast<std::uint8_t>(a.blend) < static_cast<std::uint8_t>(b.blend);
        if (a.texture_id != b.texture_id) return a.texture_id < b.texture_id;
        return a.sequence < b.sequence;
    });

    // Compact the referenced quads into submission order; each contiguous
    // (blend, texture) run becomes one instanced range of the static IBO.
    for (size_t i = 0; i < commands_.size(); ++i) {
        const Vertex2D *src = &vertices_[commands_[i].vertex_offset];
        for (int k = 0; k < 4; ++k) {
            sorted_vertices_[i * 4 + k] = src[k];
        }
    }

    current_shader_->Bind();
    current_shader_->SetMat4("u_view_projection", current_view_projection_);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(commands_.size() * 4 * sizeof(Vertex2D)),
                    sorted_vertices_.data());

    size_t run_start = 0;
    std::uint32_t run_texture = commands_[0].texture_id;
    BlendMode run_blend = commands_[0].blend;

    const auto draw_run = [&](size_t run_end) {
        if (run_end == run_start) {
            return;
        }
        ApplyBlend(run_blend);
        glBindTexture(GL_TEXTURE_2D, run_texture);
        current_shader_->SetInt("u_texture", 0);
        const auto index_offset = reinterpret_cast<const void *>(
            static_cast<std::uintptr_t>(run_start * 6 * sizeof(std::uint32_t)));
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>((run_end - run_start) * 6),
                       GL_UNSIGNED_INT, index_offset);
        ++draw_call_count_;
    };

    for (size_t i = 1; i <= commands_.size(); ++i) {
        const bool boundary = i == commands_.size() ||
                              commands_[i].texture_id != run_texture ||
                              commands_[i].blend != run_blend;
        if (boundary) {
            draw_run(i);
            run_start = i;
            if (i < commands_.size()) {
                run_texture = commands_[i].texture_id;
                run_blend = commands_[i].blend;
            }
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    total_quad_count_ += commands_.size();
    commands_.clear();
    current_quads_ = 0;
    next_sequence_ = 0;
}

void SpriteBatch::DrawQuad(const Rect &dst, const Color &color) {
    const std::uint32_t tex_id = Texture2D::White().Id();
    EnsureCapacity(1, tex_id);

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

    RecordCommand(tex_id);
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

    RecordCommand(texture.Id());
    ++current_quads_;
}

void SpriteBatch::DrawSpriteFlipped(const Texture2D &texture, const Rect &src, const Rect &dst,
                                    bool flip_x, bool flip_y, const Color &tint) {
    if (!texture.IsValid()) return;
    EnsureCapacity(1, texture.Id());

    const size_t v_idx = current_quads_ * 4;
    const float l = dst.x;
    const float t = dst.y;
    const float r = dst.x + dst.width;
    const float b = dst.y + dst.height;

    const float tw = static_cast<float>(texture.Width());
    const float th = static_cast<float>(texture.Height());
    float u0 = src.x / tw;
    float v0 = src.y / th;
    float u1 = (src.x + src.width) / tw;
    float v1 = (src.y + src.height) / th;
    // Mirroring only rewrites UVs — geometry stays put, so flipped sprites
    // keep the exact same footprint and batching key.
    if (flip_x) std::swap(u0, u1);
    if (flip_y) std::swap(v0, v1);

    vertices_[v_idx + 0] = {{l, t}, {u0, v0}, tint};
    vertices_[v_idx + 1] = {{r, t}, {u1, v0}, tint};
    vertices_[v_idx + 2] = {{r, b}, {u1, v1}, tint};
    vertices_[v_idx + 3] = {{l, b}, {u0, v1}, tint};

    RecordCommand(texture.Id());
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

    RecordCommand(texture.Id());
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

    RecordCommand(texture.Id());
    ++current_quads_;
}

void SpriteBatch::DrawLine(const Vec2 &a, const Vec2 &b, const Color &color) {
    if (current_lines_ >= max_lines_) {
        FlushLines();
    }
    const size_t idx = current_lines_ * 2;
    line_vertices_[idx + 0] = {a, color};
    line_vertices_[idx + 1] = {b, color};
    ++current_lines_;
}

void SpriteBatch::DrawRectOutline(const Rect &rect, const Color &color) {
    const Vec2 tl{rect.x, rect.y};
    const Vec2 tr{rect.x + rect.width, rect.y};
    const Vec2 br{rect.x + rect.width, rect.y + rect.height};
    const Vec2 bl{rect.x, rect.y + rect.height};
    DrawLine(tl, tr, color);
    DrawLine(tr, br, color);
    DrawLine(br, bl, color);
    DrawLine(bl, tl, color);
}

void SpriteBatch::DrawCircleOutline(const Vec2 &center, float radius, int segments,
                                    const Color &color) {
    if (segments < 3) {
        segments = 3;
    }
    Vec2 prev{center.x + radius, center.y};
    for (int i = 1; i <= segments; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        const Vec2 cur{center.x + std::cos(angle) * radius,
                       center.y + std::sin(angle) * radius};
        DrawLine(prev, cur, color);
        prev = cur;
    }
}

void SpriteBatch::FlushLines() {
    if (current_lines_ == 0 || !line_vao_ || !current_shader_) {
        current_lines_ = 0;
        return;
    }

    ApplyBlend(current_blend_);
    current_shader_->Bind();
    current_shader_->SetMat4("u_view_projection", current_view_projection_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Texture2D::White().Id());
    current_shader_->SetInt("u_texture", 0);

    glBindVertexArray(line_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(current_lines_ * 2 * sizeof(LineVertex)),
                    line_vertices_.data());
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(current_lines_ * 2));
    glBindVertexArray(0);

    ++draw_call_count_;
    current_lines_ = 0;
}

} // namespace fake2d
