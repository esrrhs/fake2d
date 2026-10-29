#include "fake2d/ui.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace fake2d {

namespace {

// Widget palette; hover/press/focus states brighten the same base colours.
constexpr Color kTrack{0.13f, 0.16f, 0.24f, 1.0f};
constexpr Color kThumb{0.22f, 0.30f, 0.48f, 1.0f};
constexpr Color kThumbHot{0.30f, 0.45f, 0.75f, 1.0f};
constexpr Color kThumbActive{0.34f, 0.55f, 0.92f, 1.0f};
constexpr Color kFrameLine{0.35f, 0.45f, 0.65f, 1.0f};
// The quiet border of an inactive tab: visible, but it must not compete with
// the active one for attention.
constexpr Color kDimLine{0.18f, 0.22f, 0.34f, 1.0f};
constexpr Color kField{0.10f, 0.12f, 0.18f, 1.0f};
constexpr Color kCheck{0.35f, 0.72f, 0.45f, 1.0f};
// Window body: darker than a field, because text and controls sit on top of
// it, and lighter than the canvas it covers.
constexpr Color kPanel{0.07f, 0.09f, 0.14f, 1.0f};

constexpr float kWheelStep = 40.0f; // scroll px per wheel notch

/// Append a codepoint as UTF-8. The engine takes text from GLFW's char
/// callback, which reports raw codepoints (not UTF-8 bytes).
void AppendUtf8(std::string &out, unsigned int cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

/// Drop the last UTF-8 sequence (backspace deletes one codepoint, not one
/// byte, so multi-byte characters erase cleanly).
void PopUtf8(std::string &s) {
    if (s.empty()) {
        return;
    }
    std::size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0u) == 0x80u) {
        --i; // walk back over continuation bytes
    }
    s.erase(i);
}

/// Number of codepoints (not bytes) — this is what a max_chars limit means.
std::size_t Utf8Length(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) {
            ++n;
        }
    }
    return n;
}

} // namespace

void UISystem::Bind(const Input *input, const Camera2D *camera, Renderer *renderer) {
    input_ = input;
    camera_ = camera;
    renderer_ = renderer;
}

void UISystem::NewFrame() {
    // Widget state persists (hover/pressed); tooltip records are per frame.
    tooltip_.clear();
}

UISystem::WidgetState *UISystem::Find(std::string_view key) {
    const auto it = widgets_.find(std::string(key));
    return it != widgets_.end() ? &it->second : nullptr;
}

Vec2 UISystem::Mouse() const {
    return {input_->MousePosition().x + world_origin_.x,
            input_->MousePosition().y + world_origin_.y};
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

float UISystem::LabelFit(UIAnchor anchor, float offset_x, float offset_y,
                         float max_width, std::string_view text, float scale,
                         float min_scale, const Color &tint) {
    if (camera_ == nullptr || renderer_ == nullptr) {
        return 0.0f;
    }
    float used_scale = scale;
    const float natural = renderer_->MeasureText(text, scale);
    if (max_width > 0.0f && natural > max_width) {
        used_scale = std::max(min_scale, scale * (max_width / natural));
    }
    const float text_w = renderer_->MeasureText(text, used_scale);
    const float text_h = renderer_->GetFont().LineHeight() * used_scale;
    const Vec2 p = Resolve(anchor, offset_x, offset_y, text_w, text_h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    renderer_->DrawText(text, p.x, p.y, used_scale, tint);
    return text_w;
}

void UISystem::Tip(UIAnchor anchor, float offset_x, float offset_y, float w,
                   float h, std::string_view text) {
    if (camera_ == nullptr || input_ == nullptr || text.empty()) {
        return;
    }
    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Vec2 mouse = Mouse();
    const bool inside = mouse.x >= p.x && mouse.x <= p.x + w && mouse.y >= p.y &&
                        mouse.y <= p.y + h;
    if (!inside) {
        // Leaving restarts the hover clock (and never keeps a stale tooltip).
        tip_hover_key_.clear();
        return;
    }
    // Hover key: same rect + text keeps the clock running across frames.
    const std::string key = std::to_string(static_cast<int>(p.x)) + "," +
                            std::to_string(static_cast<int>(p.y)) + "," +
                            std::string(text);
    const double now = std::chrono::duration<double>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    if (key != tip_hover_key_) {
        tip_hover_key_ = key;
        tip_hover_since_ = now;
    }
    if (tip_delay_ <= 0.0f || now - tip_hover_since_ >= tip_delay_) {
        tooltip_.assign(text.data(), text.size());
        tooltip_mouse_ = mouse;
    }
}

void UISystem::DrawTooltip() {
    if (tooltip_.empty() || camera_ == nullptr || renderer_ == nullptr) {
        return;
    }
    constexpr float scale = 0.55f;
    const float tw = renderer_->MeasureText(tooltip_, scale);
    const float th = renderer_->GetFont().LineHeight() * scale;
    const float vw = camera_->ViewportWidth();
    const float vh = camera_->ViewportHeight();
    float x = tooltip_mouse_.x + 14.0f;
    float y = tooltip_mouse_.y + 18.0f;
    if (x + tw + 14.0f > vw) {
        x = std::max(6.0f, vw - tw - 14.0f);
    }
    if (y + th + 12.0f > vh) {
        y = std::max(6.0f, tooltip_mouse_.y - th - 18.0f);
    }
    renderer_->DrawQuad({x - 7.0f, y - 5.0f, tw + 14.0f, th + 10.0f},
                        Color{0.55f, 0.46f, 0.28f, 0.95f});
    renderer_->DrawQuad({x - 5.0f, y - 3.0f, tw + 10.0f, th + 6.0f},
                        Color{0.07f, 0.08f, 0.11f, 0.97f});
    renderer_->DrawText(tooltip_, x, y, scale, Color{0.93f, 0.90f, 0.82f, 1.0f});
}

bool UISystem::Button(std::string_view key, UIAnchor anchor,
                      float offset_x, float offset_y, float w, float h,
                      std::string_view text, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }

    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
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

float UISystem::Slider(std::string_view key, UIAnchor anchor, float offset_x,
                       float offset_y, float w, float h, float value,
                       float min_value, float max_value) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return value;
    }
    // A degenerate or inverted range would divide by zero; treat it as empty.
    if (!(max_value > min_value)) {
        return min_value;
    }

    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Vec2 mouse = Mouse();

    const float track_h = h * 0.34f;
    const Rect track{p.x, p.y + (h - track_h) * 0.5f, w, track_h};
    const float thumb_w = h * 0.6f;
    const float range_px = w - thumb_w;

    const auto clamp_value = [min_value, max_value](float v) {
        return v < min_value ? min_value : (v > max_value ? max_value : v);
    };
    float next = clamp_value(value);

    const float hover_t = (next - min_value) / (max_value - min_value);
    const Rect thumb{p.x + hover_t * range_px, p.y, thumb_w, h};
    const bool hover = thumb.Contains(mouse) || track.Contains(mouse);
    const bool mouse_down = input_->MouseDown(0);
    const bool mouse_pressed = input_->MousePressed(0);
    const bool mouse_released = input_->MouseReleased(0);

    if (mouse_pressed && hover) {
        state.dragging = true;
        state.pressed = true;
        if (thumb.Contains(mouse)) {
            // Grab the thumb where it was clicked, so it does not jump.
            state.grab = mouse.x - thumb.x;
        } else {
            // Clicking the bare track jumps there, centred on the thumb.
            next = min_value +
                   (mouse.x - p.x - thumb_w * 0.5f) / range_px * (max_value - min_value);
            next = clamp_value(next);
            state.grab = thumb_w * 0.5f;
        }
    }
    if (state.dragging && mouse_down) {
        next = min_value +
               (mouse.x - p.x - state.grab) / range_px * (max_value - min_value);
        next = clamp_value(next);
    }
    if (mouse_released || !mouse_down) {
        state.dragging = false;
        state.pressed = false;
    }
    state.hover = hover;

    // Draw the thumb from the value resolved for this frame, not from the
    // incoming one, so a click-jump shows up on the very same frame.
    const float drawn_t = (next - min_value) / (max_value - min_value);
    const Color thumb_color = state.dragging ? kThumbActive
                              : hover       ? kThumbHot
                                            : kThumb;
    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kTrack);
    renderer_->DrawQuad(track, kTrack);
    renderer_->DrawQuad({p.x + drawn_t * range_px, p.y, thumb_w, h}, thumb_color);
    return next;
}

bool UISystem::Checkbox(std::string_view key, UIAnchor anchor, float offset_x,
                        float offset_y, float size, bool checked,
                        std::string_view label, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, size, size,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Vec2 mouse = Mouse();
    const Rect bounds{p.x, p.y, size, size};
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
        state.pressed = false;
    }
    state.hover = hover;

    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, size + 4.0f, size + 4.0f},
                        hover ? kThumbHot : kFrameLine);
    renderer_->DrawQuad(bounds, kField);
    if (checked) {
        // Inset tick block: cheap, and reads at any size.
        const float inset = size * 0.26f;
        renderer_->DrawQuad({p.x + inset, p.y + inset, size - inset * 2.0f,
                             size - inset * 2.0f},
                            kCheck);
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + size + 8.0f, p.y + size * 0.5f - text_h * 0.5f,
                       0.0f, text_scale, Color::White());
    }
    return clicked;
}

bool UISystem::TextInput(std::string_view key, UIAnchor anchor, float offset_x,
                         float offset_y, float w, float h,
                         std::size_t max_chars) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Vec2 mouse = Mouse();
    const Rect bounds{p.x, p.y, w, h};
    const bool hover = bounds.Contains(mouse);
    const bool mouse_down = input_->MouseDown(0);
    const bool mouse_released = input_->MouseReleased(0);

    // A click focuses this field and unfocuses every other one: only one
    // widget may swallow typed characters.
    // Uses last frame's pressed state, exactly like Button: a press that
    // started inside this field and is released inside it, takes focus.
    if (hover && mouse_released && state.pressed) {
        focused_ = key_str;
    }
    if (hover) {
        state.pressed = mouse_down || (state.pressed && !mouse_released && hover);
    } else {
        state.pressed = false;
    }
    state.hover = hover;

    const bool focused = focused_ == key_str;
    bool changed = false;

    if (focused) {
        for (const unsigned int cp : input_->TextCodepoints()) {
            // Printable only; control codepoints have no glyph.
            if (cp < 0x20u || cp == 0x7Fu) {
                continue;
            }
            // max_chars == 0 means unlimited.
            if (max_chars > 0 && Utf8Length(state.text) >= max_chars) {
                break;
            }
            AppendUtf8(state.text, cp);
            changed = true;
        }
        if (input_->KeyPressed("backspace") && !state.text.empty()) {
            PopUtf8(state.text);
            changed = true;
        }
    }

    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f},
                        focused ? kThumbHot : kFrameLine);
    renderer_->DrawQuad(bounds, kField);

    const float text_scale = 0.7f;
    const float text_h = renderer_->GetFont().LineHeight() * text_scale;
    const float pad = 6.0f;
    const float text_y = p.y + h * 0.5f - text_h * 0.5f;
    const float avail = w - pad * 2.0f;

    float caret_x = p.x + pad;
    if (!state.text.empty()) {
        caret_x = DrawFittedText(state.text, p.x + pad, text_y, avail, text_scale,
                                 Color::White()) + 1.0f;
    } else if (!focused) {
        DrawFittedText("click to type", p.x + pad, text_y, avail, text_scale,
                       Color{0.45f, 0.45f, 0.5f, 1.0f});
    }
    if (focused) {
        renderer_->DrawQuad({caret_x, text_y, 2.0f, text_h}, Color::White());
    }
    return changed;
}

std::string_view UISystem::TextValue(std::string_view key) const {
    if (const WidgetState *state = const_cast<UISystem *>(this)->Find(key)) {
        return state->text;
    }
    return {};
}

void UISystem::SetTextValue(std::string_view key, std::string_view text) {
    WidgetState &state = widgets_[std::string(key)];
    state.text = std::string(text);
}

float UISystem::ScrollArea(std::string_view key, UIAnchor anchor, float offset_x,
                           float offset_y, float w, float h, float content_height,
                           float scroll_y) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return scroll_y;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Vec2 mouse = Mouse();

    const float max_scroll = content_height > h ? content_height - h : 0.0f;
    const Rect area{p.x, p.y, w, h};
    const bool hover = area.Contains(mouse);

    const float bar_w = 10.0f;
    const Rect bar{p.x + w - bar_w, p.y, bar_w, h};
    // A short content list would make the thumb taller than the track; keep
    // it a grabbable minimum instead.
    const float thumb_h = content_height > 0.0f
                              ? std::max(24.0f, h * (h / content_height))
                              : h;
    const float thumb_track = h - thumb_h;

    const bool mouse_down = input_->MouseDown(0);
    const bool mouse_pressed = input_->MousePressed(0);
    const bool mouse_released = input_->MouseReleased(0);

    float next = scroll_y;
    if (hover && input_->MouseWheel() != 0.0) {
        // Wheel away (positive) scrolls the content up, i.e. the offset down.
        next -= static_cast<float>(input_->MouseWheel()) * kWheelStep;
    }

    const float thumb_y = p.y + (max_scroll > 0.0f ? next / max_scroll : 0.0f) *
                                    thumb_track;
    if (mouse_pressed && bar.Contains(mouse)) {
        state.dragging = true;
        state.pressed = true;
        state.grab = mouse.y - thumb_y;
    }
    if (state.dragging && mouse_down && thumb_track > 0.0f) {
        next = (mouse.y - p.y - state.grab) / thumb_track * max_scroll;
    }
    if (mouse_released || !mouse_down) {
        state.dragging = false;
        state.pressed = false;
    }
    state.hover = hover;

    next = next < 0.0f ? 0.0f : (next > max_scroll ? max_scroll : next);

    // The container itself is invisible: content is drawn by the caller,
    // clipped, before this call. Only the scrollbar is drawn here.
    if (max_scroll > 0.0f) {
        renderer_->DrawQuad(bar, kTrack);
        const float drawn = p.y + (next / max_scroll) * thumb_track;
        renderer_->DrawQuad({bar.x + 1.0f, drawn, bar_w - 2.0f, thumb_h},
                            state.dragging ? kThumbActive
                            : hover        ? kThumbHot
                                           : kThumb);
    }
    return next;
}

float UISystem::DrawFittedText(std::string_view text, float x, float y, float w,
                               float scale, const Color &tint) {
    if (w <= 0.0f) {
        return renderer_->DrawText(text, x, y, scale, tint);
    }
    // Walk the tail backwards while it still fits and keep that slice.
    std::size_t first = 0;
    for (std::size_t i = text.size(); i > 0; --i) {
        if (renderer_->MeasureText(text.substr(i - 1), scale) > w) {
            first = i;
            break;
        }
    }
    return renderer_->DrawText(text.substr(first), x, y, scale, tint);
}

void UISystem::ProgressBar(UIAnchor anchor, float offset_x, float offset_y, float w,
                           float h, float value, const Color &fill, const Color &track) {
    if (camera_ == nullptr || renderer_ == nullptr) {
        return;
    }
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    // Clamp rather than trust: a health bar fed 1.4 or -0.2 should still look
    // like a bar, and a fill wider than the track would read as a glitch.
    const float t = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kFrameLine);
    renderer_->DrawQuad({p.x, p.y, w, h}, track);
    if (t > 0.0f) {
        renderer_->DrawQuad({p.x, p.y, w * t, h}, fill);
    }
}

bool UISystem::RadioButton(std::string_view key, UIAnchor anchor, float offset_x,
                           float offset_y, float size, bool selected,
                           std::string_view label, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, size, size,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Rect bounds{p.x, p.y, size, size};
    const Vec2 mouse = Mouse();
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
        state.pressed = false;
    }
    state.hover = hover;

    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, size + 4.0f, size + 4.0f},
                        hover ? kThumbHot : kFrameLine);
    renderer_->DrawQuad(bounds, kField);
    if (selected) {
        // Inset block, like the checkbox tick: a ring would need a line
        // primitive, and this reads at any size.
        const float inset = size * 0.28f;
        renderer_->DrawQuad({p.x + inset, p.y + inset, size - inset * 2.0f,
                             size - inset * 2.0f},
                            kCheck);
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + size + 8.0f, p.y + size * 0.5f - text_h * 0.5f,
                       0.0f, text_scale, Color::White());
    }
    return clicked;
}

bool UISystem::ComboBegin(std::string_view key, UIAnchor anchor, float offset_x,
                          float offset_y, float w, float h, std::string_view label,
                          bool open, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
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
        // Unlike a button, releasing outside still counts as a click: the
        // list is a popup that swallows the gesture, and users flick it.
        if (state.pressed && mouse_released) {
            clicked = true;
        }
        state.pressed = mouse_down;
    }
    state.hover = hover;

    const Color fill = open      ? kThumbActive
                       : (state.pressed ? kThumbActive
                          : hover       ? kThumbHot
                                        : kThumb);
    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kFrameLine);
    renderer_->DrawQuad(bounds, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        const float left = p.x + 8.0f;
        DrawFittedText(label, left, p.y + h * 0.5f - text_h * 0.5f, w - 28.0f,
                       text_scale, Color::White());
    }
    // Chevron: a two-step triangle — wide bar on top reads as a down arrow
    // when closed, and flipping the halves reads as an up arrow when open.
    const float cx = p.x + w - 20.0f;
    const float cy = p.y + h * 0.5f - 2.0f;
    if (open) {
        renderer_->DrawQuad({cx + 2.0f, cy, 4.0f, 2.0f}, Color::White());
        renderer_->DrawQuad({cx, cy + 2.0f, 8.0f, 2.0f}, Color::White());
    } else {
        renderer_->DrawQuad({cx, cy, 8.0f, 2.0f}, Color::White());
        renderer_->DrawQuad({cx + 2.0f, cy + 2.0f, 4.0f, 2.0f}, Color::White());
    }
    return clicked;
}

bool UISystem::ComboItem(std::string_view key, UIAnchor anchor, float offset_x,
                         float offset_y, float w, float h, std::string_view label,
                         bool selected, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
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
        state.pressed = false;
    }
    state.hover = hover;

    const Color fill = hover ? kThumbHot : (selected ? kThumb : kField);
    renderer_->DrawQuad(bounds, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 8.0f, p.y + h * 0.5f - text_h * 0.5f, w - 16.0f,
                       text_scale, Color::White());
    }
    return clicked;
}

bool UISystem::Window(std::string_view key, UIAnchor anchor, float offset_x,
                      float offset_y, float w, float h, std::string_view title,
                      float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    // The bar is sized off the text so a bigger scale gets a bigger grab
    // target, with a floor so an empty title still leaves something to hold.
    const float bar_h = std::max(22.0f,
                                 renderer_->GetFont().LineHeight() * text_scale + 10.0f);
    const Rect bar{p.x, p.y, w, bar_h};
    const Vec2 mouse = Mouse();
    const bool hover_bar = bar.Contains(mouse);
    const bool mouse_down = input_->MouseDown(0);
    const bool mouse_pressed = input_->MousePressed(0);

    // Grab on the press frame, but report the drag only from the next one:
    // the click that grabs a window also moves the cursor onto it, and
    // applying that delta would snap the panel under the pointer.
    bool dragging = false;
    if (mouse_down) {
        if (mouse_pressed && hover_bar) {
            state.dragging = true;
        }
        dragging = state.dragging && state.pressed;
        state.pressed = state.dragging;
    } else {
        state.dragging = false;
        state.pressed = false;
    }
    state.hover = hover_bar;

    renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kFrameLine);
    renderer_->DrawQuad({p.x, p.y, w, h}, kPanel);
    renderer_->DrawQuad(bar, state.dragging ? kThumbActive : kThumb);
    // A one-pixel rule under the bar: the cheapest way to say "this part
    // moves the panel, the rest does not".
    renderer_->DrawQuad({p.x, p.y + bar_h, w, 1.0f}, kFrameLine);
    if (!title.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(title, p.x + 8.0f, p.y + bar_h * 0.5f - text_h * 0.5f,
                       w - 16.0f, text_scale, Color::White());
    }
    return dragging;
}

bool UISystem::Tab(std::string_view key, UIAnchor anchor, float offset_x,
                   float offset_y, float w, float h, std::string_view label,
                   bool active, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
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
        state.pressed = false;
    }
    state.hover = hover;

    // An active tab sits brighter than its idle siblings and grows 2 px,
    // dropping its bottom edge so it reads as connected to the panel below.
    const float grow = active ? 2.0f : 0.0f;
    const Color fill = active  ? kThumb
                       : (state.pressed ? kThumbHot
                          : hover       ? kThumbHot
                                        : kField);
    if (!active) {
        renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kDimLine);
    } else {
        renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, 2.0f, h + 2.0f + grow}, kFrameLine);
        renderer_->DrawQuad({p.x + w, p.y - 2.0f, 2.0f, h + 2.0f + grow}, kFrameLine);
        renderer_->DrawQuad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, 2.0f}, kFrameLine);
    }
    renderer_->DrawQuad({p.x, p.y, w, h + grow}, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 8.0f, p.y + h * 0.5f - text_h * 0.5f, w - 16.0f,
                       text_scale, active ? Color::White()
                                          : Color{0.72f, 0.78f, 0.86f, 1.0f});
    }
    return clicked;
}

bool UISystem::Tree(std::string_view key, UIAnchor anchor, float offset_x,
                    float offset_y, float w, float h, std::string_view label,
                    bool open, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p = Resolve(anchor, offset_x, offset_y, w, h,
                           camera_->ViewportWidth(), camera_->ViewportHeight());
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
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
        state.pressed = false;
    }
    state.hover = hover;

    renderer_->DrawQuad(bounds, hover ? kThumbHot : Color::Clear());
    // Disclosure triangle in three bars: widening to the right reads as ">",
    // narrowing downwards reads as "v". Three quads, no line primitives (and
    // no scissor worries — those are not clipped).
    const float cx = p.x + 4.0f;
    const float cy = p.y + h * 0.5f;
    if (open) {
        renderer_->DrawQuad({cx, cy - 3.0f, 7.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        renderer_->DrawQuad({cx + 1.0f, cy - 1.0f, 5.0f, 2.0f},
                            Color{0.85f, 0.90f, 0.96f, 1.0f});
        renderer_->DrawQuad({cx + 2.0f, cy + 1.0f, 3.0f, 2.0f},
                            Color{0.85f, 0.90f, 0.96f, 1.0f});
    } else {
        renderer_->DrawQuad({cx, cy - 3.0f, 2.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        renderer_->DrawQuad({cx + 2.0f, cy - 2.0f, 2.0f, 4.0f},
                            Color{0.85f, 0.90f, 0.96f, 1.0f});
        renderer_->DrawQuad({cx + 4.0f, cy - 1.0f, 2.0f, 6.0f},
                            Color{0.85f, 0.90f, 0.96f, 1.0f});
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 18.0f, p.y + h * 0.5f - text_h * 0.5f,
                       w - 24.0f, text_scale, Color::White());
    }
    return clicked;
}

bool UISystem::IsHovered(std::string_view key) const {
    if (const WidgetState *state = const_cast<UISystem *>(this)->Find(key)) {
        return state->hover;
    }
    return false;
}

} // namespace fake2d
