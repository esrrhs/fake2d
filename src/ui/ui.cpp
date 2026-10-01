#include "fake2d/ui.h"

#include <algorithm>
#include <utility>

namespace fake2d {

void UISystem::Bind(const Input *input, const Camera2D *camera, Renderer *renderer) {
    input_ = input;
    camera_ = camera;
    renderer_ = renderer;
}

void UISystem::NewFrame() {
    // State itself persists (hover/pressed); this hook exists so future
    // per-frame scratch data has a place. Kept intentionally minimal.
}

UISystem::ButtonState *UISystem::Find(std::string_view key) {
    const auto it = buttons_.find(std::string(key));
    return it != buttons_.end() ? &it->second : nullptr;
}

Vec2 UISystem::Resolve(UIAnchor anchor, float offset_x, float offset_y,
                       float w, float h, float viewport_w, float viewport_h) const {
    switch (anchor) {
        case UIAnchor::TopLeft:
            return {offset_x, offset_y};
        case UIAnchor::TopRight:
            return {viewport_w - w - offset_x, offset_y};
        case UIAnchor::BottomLeft:
            return {offset_x, viewport_h - h - offset_y};
        case UIAnchor::BottomRight:
            return {viewport_w - w - offset_x, viewport_h - h - offset_y};
        case UIAnchor::Center:
            return {viewport_w * 0.5f - w * 0.5f + offset_x,
                    viewport_h * 0.5f - h * 0.5f + offset_y};
    }
    return {offset_x, offset_y};
}

void UISystem::Panel(UIAnchor anchor, float offset_x, float offset_y,
                     float w, float h, const Color &fill, const Color &border) {
    if (camera_ == nullptr) {
        return;
    }
    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    // Draw the border as a slightly larger backdrop quad first.
    if (border.a > 0.0f && renderer_ != nullptr) {
        renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, border);
    }
    if (renderer_ != nullptr) {
        renderer_->DrawQuad({p.x, p.y, w, h}, fill);
    }
}

void UISystem::Label(UIAnchor anchor, float offset_x, float offset_y,
                     std::string_view text, float scale, const Color &tint) {
    if (camera_ == nullptr || renderer_ == nullptr) {
        return;
    }
    const float text_w = renderer_->MeasureText(text, scale);
    const float text_h = renderer_->GetFont().LineHeight() * scale;
    const Vec2 p = Resolve(anchor, offset_x, offset_y, text_w, text_h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    renderer_->DrawText(text, p.x, p.y, scale, tint);
}

bool UISystem::Button(std::string_view key, UIAnchor anchor,
                      float offset_x, float offset_y, float w, float h,
                      std::string_view text, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }

    const std::string key_str(key);
    ButtonState &state = buttons_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse{input_->MousePosition().x + world_origin_.x,
                     input_->MousePosition().y + world_origin_.y};
    const bool hover = bounds.Contains(mouse);
    const bool mouse_down = input_->MouseDown(0);
    const bool mouse_released = input_->MouseReleased(0);

    bool clicked = false;
    if (hover) {
        if (state.pressed && mouse_released) {
            clicked = true;
        }
        state.pressed = mouse_down || (state.pressed && !mouse_released && hover);
    } else {
        // Drag outside cancels the press.
        state.pressed = false;
    }
    state.hover = hover;

    Color fill;
    Color label_color = Color::White();
    if (state.pressed) {
        fill = Color{0.25f, 0.45f, 0.85f, 1.0f};
    } else if (hover) {
        fill = Color{0.20f, 0.35f, 0.65f, 1.0f};
    } else {
        fill = Color{0.16f, 0.22f, 0.34f, 1.0f};
    }

    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f},
                        Color{0.35f, 0.45f, 0.65f, 1.0f});
    renderer_->DrawQuad(bounds, fill);

    if (!text.empty()) {
        const float text_w = renderer_->MeasureText(text, text_scale);
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        renderer_->DrawText(text, p.x + (w - text_w) * 0.5f,
                            p.y + (h - text_h) * 0.5f, text_scale, label_color);
    }
    return clicked;
}

bool UISystem::IsHovered(std::string_view key) const {
    if (const ButtonState *state = const_cast<UISystem *>(this)->Find(key)) {
        return state->hover;
    }
    return false;
}

} // namespace fake2d
