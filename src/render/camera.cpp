#include "fake2d/camera.h"

namespace fake2d {

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

void Camera2D::UpdateMatrices() const {
    if (!is_dirty_) {
        return;
    }

    // Top-left origin: 0 at top, viewport_height at bottom.
    projection_ = Mat4::Orthographic(0.0f, viewport_width_, viewport_height_, 0.0f, -1.0f, 1.0f);

    // View matrix:
    // 1. Translate origin to center of viewport
    // 2. Rotate around Z
    // 3. Scale by zoom
    // 4. Translate by -position - (center)
    const float cx = viewport_width_ * 0.5f;
    const float cy = viewport_height_ * 0.5f;

    const Mat4 t_to_center = Mat4::Translation(cx, cy);
    const Mat4 r = Mat4::RotationZ(-rotation_);
    const Mat4 s = Mat4::Scale(zoom_, zoom_);
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
