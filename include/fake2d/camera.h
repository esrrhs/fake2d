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

    // --- follow / bounds ---
    /// World rectangle the camera view must stay inside. The camera center is
    /// clamped so the visible rect never leaves `bounds`; when the level is
    /// smaller than the view on an axis, that axis is centred instead.
    /// Pass a zero-size rect (or ClearBounds) to disable.
    void SetBounds(const Rect &world_bounds);
    void SetBounds(float x, float y, float width, float height);
    void ClearBounds();
    [[nodiscard]] const Rect &Bounds() const { return bounds_; }
    [[nodiscard]] bool HasBounds() const { return has_bounds_; }

    /// Half-size of a box around the follow target: inside it the camera does
    /// not move at all (0 = lock tightly to the target).
    void SetDeadzone(float half_width, float half_height) {
        deadzone_ = {half_width < 0.0f ? 0.0f : half_width,
                     half_height < 0.0f ? 0.0f : half_height};
    }
    [[nodiscard]] Vec2 Deadzone() const { return deadzone_; }

    /// Follow smoothing rate (higher = snappier). Frame-rate independent
    /// exponential approach: `1 - exp(-rate * dt)` per frame.
    void SetFollowSmoothing(float rate) { follow_rate_ = rate > 0.0f ? rate : 0.0f; }
    [[nodiscard]] float FollowSmoothing() const { return follow_rate_; }

    /// SMB-style horizontal scroll lock: the camera never moves left while
    /// following (desired.x below the current position is ignored). Direct
    /// SetPosition / SnapTo still move freely, so level resets work.
    void SetForwardOnlyX(bool enabled) { forward_only_x_ = enabled; }
    [[nodiscard]] bool ForwardOnlyX() const { return forward_only_x_; }

    /// Move toward `target` with deadzone + smoothing, then clamp to bounds.
    /// Call once per frame with the frame delta.
    void Follow(const Vec2 &target, float dt);
    void Follow(float x, float y, float dt);
    /// Jump straight to `target` (no smoothing), still clamped to bounds.
    void SnapTo(const Vec2 &target);

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
    /// Clamp `position_` so the visible rect stays inside bounds_.
    void ClampToBounds();

    float viewport_width_ = 1280.0f;
    float viewport_height_ = 720.0f;
    float content_scale_ = 1.0f;
    Vec2 position_ = {0.0f, 0.0f};
    float zoom_ = 1.0f;
    float rotation_ = 0.0f;

    Rect bounds_{};
    bool has_bounds_ = false;
    Vec2 deadzone_{0.0f, 0.0f};
    float follow_rate_ = 8.0f;
    bool forward_only_x_ = false;

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
