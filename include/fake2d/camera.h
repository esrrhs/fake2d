#pragma once

#include "fake2d/math.h"

namespace fake2d {

class Camera2D {
public:
    Camera2D() = default;
    Camera2D(float viewport_width, float viewport_height);

    void SetViewport(float width, float height);
    [[nodiscard]] float ViewportWidth() const { return viewport_width_; }
    [[nodiscard]] float ViewportHeight() const { return viewport_height_; }

    /// Framebuffer / logical-point scale (1.0 standard, 2.0 Retina). The
    /// orthographic projection covers physical pixels while game coordinates
    /// stay in logical points; screen<->world conversions stay in points.
    void SetContentScale(float scale);
    [[nodiscard]] float ContentScale() const { return content_scale_; }

    void SetPosition(const Vec2 &pos) { position_ = pos; is_dirty_ = true; }
    void SetPosition(float x, float y) { position_ = {x, y}; is_dirty_ = true; }
    [[nodiscard]] const Vec2 &Position() const { return position_; }

    void SetZoom(float zoom) { zoom_ = (zoom > 1e-4f ? zoom : 1e-4f); is_dirty_ = true; }
    [[nodiscard]] float Zoom() const { return zoom_; }

    void SetRotation(float radians) { rotation_ = radians; is_dirty_ = true; }
    [[nodiscard]] float Rotation() const { return rotation_; }

    // --- trauma shake ---
    /// Add impact in [0,1]; shake magnitude scales with trauma squared and
    /// decays automatically. Multiple impacts stack up to 1.
    void AddTrauma(float amount);
    [[nodiscard]] float Trauma() const { return trauma_; }
    /// Called by the engine once per frame to decay trauma and advance noise.
    void UpdateEffects(float dt);

    void Move(const Vec2 &delta) { position_ += delta; is_dirty_ = true; }
    void Move(float dx, float dy) { position_.x += dx; position_.y += dy; is_dirty_ = true; }

    [[nodiscard]] const Mat4 &ProjectionMatrix() const;
    [[nodiscard]] const Mat4 &ViewMatrix() const;
    [[nodiscard]] const Mat4 &ViewProjectionMatrix() const;

    [[nodiscard]] Vec2 ScreenToWorld(const Vec2 &screen_pos) const;
    [[nodiscard]] Vec2 WorldToScreen(const Vec2 &world_pos) const;

private:
    void UpdateMatrices() const;

    float viewport_width_ = 1280.0f;
    float viewport_height_ = 720.0f;
    float content_scale_ = 1.0f;
    Vec2 position_ = {0.0f, 0.0f};
    float zoom_ = 1.0f;
    float rotation_ = 0.0f;

    float trauma_ = 0.0f;
    float shake_time_ = 0.0f;
    Vec2 shake_offset_{0.0f, 0.0f}; // physical pixels
    float shake_angle_ = 0.0f;

    mutable bool is_dirty_ = true;
    mutable Mat4 projection_;
    mutable Mat4 view_;
    mutable Mat4 view_projection_;
};

} // namespace fake2d
