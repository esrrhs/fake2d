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
