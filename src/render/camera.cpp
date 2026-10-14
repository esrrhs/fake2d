#include "fake2d/camera.h"

#include <algorithm>
#include <cmath>

namespace fake2d {

namespace {
constexpr float kTraumaDecay = 1.1f;
constexpr float kMaxShakePoints = 14.0f;
constexpr float kMaxShakeAngle = 0.06f;
} // namespace

Camera2D::Camera2D(float viewport_width, float viewport_height)
    : viewport_width_(viewport_width), viewport_height_(viewport_height) {
    UpdateMatrices();
}

void Camera2D::SetViewport(float width, float height) {
    if (viewport_width_ != width || viewport_height_ != height) {
        viewport_width_ = width;
        viewport_height_ = height;
        is_dirty_ = true;
    }
}

void Camera2D::SetContentScale(float scale) {
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    if (content_scale_ != scale) {
        content_scale_ = scale;
        is_dirty_ = true;
    }
}

void Camera2D::SetBounds(const Rect &world_bounds) {
    bounds_ = world_bounds;
    has_bounds_ = world_bounds.width > 0.0f && world_bounds.height > 0.0f;
    if (has_bounds_) {
        is_dirty_ = true;
        ClampToBounds();
    }
}

void Camera2D::SetBounds(float x, float y, float width, float height) {
    SetBounds(Rect{x, y, width, height});
}

void Camera2D::ClearBounds() {
    has_bounds_ = false;
    bounds_ = Rect{};
}

void Camera2D::ClampToBounds() {
    if (!has_bounds_) {
        return;
    }
    // position_ is the TOP-LEFT of the visible world rect (see
    // UpdateMatrices); the visible size shrinks as zoom grows.
    const float view_w = viewport_width_ / zoom_;
    const float view_h = viewport_height_ / zoom_;
    if (bounds_.width <= view_w) {
        // Level narrower than the view: centre it (top-left may go negative).
        position_.x = bounds_.x + (bounds_.width - view_w) * 0.5f;
    } else {
        position_.x = std::clamp(position_.x, bounds_.x,
                                 bounds_.x + bounds_.width - view_w);
    }
    if (bounds_.height <= view_h) {
        position_.y = bounds_.y + (bounds_.height - view_h) * 0.5f;
    } else {
        position_.y = std::clamp(position_.y, bounds_.y,
                                 bounds_.y + bounds_.height - view_h);
    }
    is_dirty_ = true;
}

void Camera2D::Follow(const Vec2 &target, float dt) {
    const float view_w = viewport_width_ / zoom_;
    const float view_h = viewport_height_ / zoom_;
    const Vec2 view_center = position_ + Vec2{view_w * 0.5f, view_h * 0.5f};
    Vec2 desired = position_;

    if (deadzone_.x > 0.0f || deadzone_.y > 0.0f) {
        // Only push the camera once the target leaves the box around the
        // current view centre.
        const float dx = target.x - view_center.x;
        if (dx > deadzone_.x) {
            desired.x = position_.x + (dx - deadzone_.x);
        } else if (dx < -deadzone_.x) {
            desired.x = position_.x + (dx + deadzone_.x);
        }
        const float dy = target.y - view_center.y;
        if (dy > deadzone_.y) {
            desired.y = position_.y + (dy - deadzone_.y);
        } else if (dy < -deadzone_.y) {
            desired.y = position_.y + (dy + deadzone_.y);
        }
    } else {
        // Centre the view on the target (top-left = target - half view).
        desired = target - Vec2{view_w * 0.5f, view_h * 0.5f};
    }

    // SMB scroll lock: once the camera has advanced, a target further left
    // never pulls it back (SnapTo / SetPosition remain free for resets).
    if (forward_only_x_ && desired.x < position_.x) {
        desired.x = position_.x;
    }

    if (dt > 0.0f && follow_rate_ > 0.0f) {
        // Frame-rate independent exponential approach.
        const float k = 1.0f - std::exp(-follow_rate_ * dt);
        position_.x += (desired.x - position_.x) * k;
        position_.y += (desired.y - position_.y) * k;
    } else {
        position_ = desired;
    }

    is_dirty_ = true;
    ClampToBounds();
}

void Camera2D::Follow(float x, float y, float dt) {
    Follow(Vec2{x, y}, dt);
}

void Camera2D::SnapTo(const Vec2 &target) {
    position_ = target;
    is_dirty_ = true;
    ClampToBounds();
}

void Camera2D::AddTrauma(float amount) {
    trauma_ = std::clamp(trauma_ + amount, 0.0f, 1.0f);
    is_dirty_ = true;
}

void Camera2D::UpdateEffects(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    if (trauma_ > 0.0f) {
        trauma_ = std::max(0.0f, trauma_ - kTraumaDecay * dt);
        shake_time_ += dt;
        // Deterministic layered-sine pseudo-noise (no RNG state; headless
        // runs reproduce frame-for-frame).
        const float t = shake_time_;
        const float magnitude = trauma_ * trauma_;
        const float nx = std::sin(t * 47.0f) * 0.6f + std::sin(t * 91.3f) * 0.4f;
        const float ny = std::sin(t * 53.7f) * 0.6f + std::sin(t * 83.1f) * 0.4f;
        shake_offset_.x = kMaxShakePoints * content_scale_ * magnitude * nx;
        shake_offset_.y = kMaxShakePoints * content_scale_ * magnitude * ny;
        shake_angle_ = kMaxShakeAngle * magnitude * std::sin(t * 67.0f);
        is_dirty_ = true;
    } else if (shake_offset_.x != 0.0f || shake_offset_.y != 0.0f ||
               shake_angle_ != 0.0f) {
        shake_offset_ = {0.0f, 0.0f};
        shake_angle_ = 0.0f;
        is_dirty_ = true;
    }
}

void Camera2D::UpdateMatrices() const {
    if (!is_dirty_) {
        return;
    }

    // Projection covers physical framebuffer pixels; vertices are authored
    // in logical points and the view matrix's content-scale factor maps
    // points onto pixels (Retina sharpness without changing game coords).
    const float phys_w = viewport_width_ * content_scale_;
    const float phys_h = viewport_height_ * content_scale_;

    // Top-left origin: 0 at top, phys_h at bottom.
    projection_ = Mat4::Orthographic(0.0f, phys_w, phys_h, 0.0f, -1.0f, 1.0f);

    // View matrix (in physical space):
    // 1. Translate origin to -position minus the logical viewport center
    // 2. Scale by zoom and content scale (combined)
    // 3. Rotate around Z
    // 4. Translate to the physical viewport center
    const float cx = viewport_width_ * 0.5f;
    const float cy = viewport_height_ * 0.5f;
    const float phys_cx = phys_w * 0.5f;
    const float phys_cy = phys_h * 0.5f;

    const Mat4 t_to_center = Mat4::Translation(phys_cx + shake_offset_.x,
                                               phys_cy + shake_offset_.y);
    const Mat4 r = Mat4::RotationZ(-(rotation_ + shake_angle_));
    const Mat4 s = Mat4::Scale(zoom_ * content_scale_, zoom_ * content_scale_);
    const Mat4 t_from_pos = Mat4::Translation(-position_.x - cx, -position_.y - cy);

    view_ = t_to_center * r * s * t_from_pos;
    view_projection_ = projection_ * view_;

    is_dirty_ = false;
}

const Mat4 &Camera2D::ProjectionMatrix() const {
    UpdateMatrices();
    return projection_;
}

const Mat4 &Camera2D::ViewMatrix() const {
    UpdateMatrices();
    return view_;
}

const Mat4 &Camera2D::ViewProjectionMatrix() const {
    UpdateMatrices();
    return view_projection_;
}

Vec2 Camera2D::ScreenToWorld(const Vec2 &screen_pos) const {
    UpdateMatrices();
    const float cx = viewport_width_ * 0.5f;
    const float cy = viewport_height_ * 0.5f;

    // Offset from screen center
    float ox = screen_pos.x - cx;
    float oy = screen_pos.y - cy;

    // Inverse scale
    ox /= zoom_;
    oy /= zoom_;

    // Inverse rotation
    const float cos_r = std::cos(rotation_);
    const float sin_r = std::sin(rotation_);
    const float rx = ox * cos_r - oy * sin_r;
    const float ry = ox * sin_r + oy * cos_r;

    return {rx + position_.x + cx, ry + position_.y + cy};
}

Vec2 Camera2D::WorldToScreen(const Vec2 &world_pos) const {
    UpdateMatrices();
    const float cx = viewport_width_ * 0.5f;
    const float cy = viewport_height_ * 0.5f;

    // Offset from camera position
    const float ox = world_pos.x - (position_.x + cx);
    const float oy = world_pos.y - (position_.y + cy);

    // Rotation
    const float cos_r = std::cos(-rotation_);
    const float sin_r = std::sin(-rotation_);
    const float rx = ox * cos_r - oy * sin_r;
    const float ry = ox * sin_r + oy * cos_r;

    // Scale and center back
    return {rx * zoom_ + cx, ry * zoom_ + cy};
}

} // namespace fake2d
