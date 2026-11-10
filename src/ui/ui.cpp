#include "fake2d/ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
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
    if (!boxes_.empty() && renderer_) renderer_->ClearClipRect();
    boxes_.clear();
    rows_.clear();
    scrolls_.clear();
    active_scrolls_.clear();
    scroll_offsets_.clear();
    pending_draws_.clear();
    scroll_resolvers_.clear();
    audit_.clear();
    last_rect_ = {};
    paint_bounds_ = {};
    wheel_scroll_ = static_cast<std::size_t>(-1);
    wheel_depth_ = 0;
}

UISystem::WidgetState *UISystem::Find(std::string_view key) {
    const auto it = widgets_.find(std::string(key));
    return it != widgets_.end() ? &it->second : nullptr;
}

bool UISystem::MouseVisible() const {
    if (!input_ || boxes_.empty()) return true;
    const Vec2 mouse{input_->MousePosition().x + world_origin_.x,
                     input_->MousePosition().y + world_origin_.y};
    for (const auto &clip : boxes_.back().clips) {
        Rect visible = clip.rect;
        visible.y -= ScrollTranslation(clip.scrolls);
        if (!visible.Contains(mouse)) return false;
    }
    return true;
}

Vec2 UISystem::Mouse() const {
    return {input_->MousePosition().x + world_origin_.x,
            input_->MousePosition().y + world_origin_.y + ScrollTranslation(active_scrolls_)};
}

void UISystem::PaintPanel(float offset_x, float offset_y,
                     float w, float h, const Color &fill, const Color &border) {
    if (camera_ == nullptr) {
        return;
    }
    const Vec2 p{offset_x, offset_y};
    // Draw the border as a slightly larger backdrop quad first.
    if (border.a > 0.0f && renderer_ != nullptr) {
        Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, border);
    }
    if (renderer_ != nullptr) {
        Quad({p.x, p.y, w, h}, fill);
    }
}

void UISystem::PaintTip(float offset_x, float offset_y, float w,
                   float h, std::string_view text) {
    if (camera_ == nullptr || input_ == nullptr || text.empty()) {
        return;
    }
    const Vec2 p{offset_x, offset_y};
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
    const float vw = camera_->ViewportWidth();
    const float vh = camera_->ViewportHeight();
    const float width = std::max(1.0f, std::min(LayoutToken("panel_width"), vw - 24.0f));
    const float tw = std::min(renderer_->MeasureText(tooltip_, scale), width);
    const float th = renderer_->GetFont().MeasureTextEx(tooltip_, scale, width);
    float x = tooltip_mouse_.x + 14.0f;
    float y = tooltip_mouse_.y + 18.0f;
    if (x + tw + 14.0f > vw) {
        x = std::max(6.0f, vw - tw - 14.0f);
    }
    if (y + th + 12.0f > vh) {
        y = std::max(6.0f, tooltip_mouse_.y - th - 18.0f);
    }
    renderer_->SetClipRect({0, 0, vw, vh});
    Record("tooltip", {x, y, tw, th});
    renderer_->DrawQuad({x - 7.0f, y - 5.0f, tw + 14.0f, th + 10.0f},
                        Color{0.55f, 0.46f, 0.28f, 0.95f});
    renderer_->DrawQuad({x - 5.0f, y - 3.0f, tw + 10.0f, th + 6.0f},
                        Color{0.07f, 0.08f, 0.11f, 0.97f});
    renderer_->DrawTextEx(tooltip_, x, y, scale, 0, width, Color{0.93f, 0.90f, 0.82f, 1.0f});
    ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
}

bool UISystem::PaintButton(std::string_view key,
                      float offset_x, float offset_y, float w, float h,
                      std::string_view text, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }

    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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

    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f},
                        Color{0.35f, 0.45f, 0.65f, 1.0f});
    Quad(bounds, fill);

    if (!text.empty()) {
        const float available = std::max(0.0f, w - 8.0f);
        const float natural = renderer_->MeasureText(text, text_scale);
        const float used_scale = natural > available && natural > 0
            ? std::min(text_scale, std::max(0.35f, text_scale * available / natural))
            : text_scale;
        const std::string fitted = Ellipsize(text, available, used_scale);
        const float text_w = renderer_->MeasureText(fitted, used_scale);
        const float text_h = renderer_->GetFont().LineHeight() * used_scale;
        // A tiny button must clip vertically as well as fit horizontally.

        String(fitted, p.x + (w - text_w) * 0.5f,
                            p.y + (h - text_h) * 0.5f, used_scale, label_color);

    }
    return clicked;
}

float UISystem::PaintSlider(std::string_view key, float offset_x,
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

    const Vec2 p{offset_x, offset_y};
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
    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kTrack);
    Quad(track, kTrack);
    Quad({p.x + drawn_t * range_px, p.y, thumb_w, h}, thumb_color);
    return next;
}

bool UISystem::PaintCheckbox(std::string_view key, float offset_x,
                        float offset_y, float size, bool checked,
                        std::string_view label, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Vec2 mouse = Mouse();
    const Rect bounds{p.x, p.y, size, size};
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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

    Quad({p.x - 2.0f, p.y - 2.0f, size + 4.0f, size + 4.0f},
                        hover ? kThumbHot : kFrameLine);
    Quad(bounds, kField);
    if (checked) {
        // Inset tick block: cheap, and reads at any size.
        const float inset = size * 0.26f;
        Quad({p.x + inset, p.y + inset, size - inset * 2.0f,
                             size - inset * 2.0f},
                            kCheck);
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        String(label, p.x + size + 8.0f, p.y + size * 0.5f - text_h * 0.5f,
               text_scale, Color::White());
    }
    return clicked;
}

bool UISystem::PaintTextInput(std::string_view key, float offset_x,
                         float offset_y, float w, float h,
                         std::size_t max_chars) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Vec2 mouse = Mouse();
    const Rect bounds{p.x, p.y, w, h};
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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

    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f},
                        focused ? kThumbHot : kFrameLine);
    Quad(bounds, kField);

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
        Quad({caret_x, text_y, 2.0f, text_h}, Color::White());
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

float UISystem::PaintScrollArea(std::string_view key, float offset_x,
                           float offset_y, float w, float h, float content_height,
                           float scroll_y, bool wheel_enabled) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return scroll_y;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Vec2 mouse = Mouse();

    const float max_scroll = content_height > h ? content_height - h : 0.0f;
    const Rect area{p.x, p.y, w, h};
    const bool hover = area.Contains(mouse) && MouseVisible();

    const float bar_w = 10.0f;
    const Rect bar{p.x + w - bar_w, p.y, bar_w, h};
    // A short content list would make the thumb taller than the track; keep
    // it a grabbable minimum instead.
    const float thumb_h = content_height > 0.0f
                              ? std::min(h, std::max(24.0f, h * (h / content_height)))
                              : h;
    const float thumb_track = h - thumb_h;

    const bool mouse_down = input_->MouseDown(0);
    const bool mouse_pressed = input_->MousePressed(0);
    const bool mouse_released = input_->MouseReleased(0);

    float next = scroll_y;
    if (wheel_enabled && hover && input_->MouseWheel() != 0.0) {
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
        Quad(bar, kTrack);
        const float drawn = p.y + (next / max_scroll) * thumb_track;
        Quad({bar.x + 1.0f, drawn, bar_w - 2.0f, thumb_h},
                            state.dragging ? kThumbActive
                            : hover        ? kThumbHot
                                           : kThumb);
    }
    return next;
}

float UISystem::DrawFittedText(std::string_view text, float x, float y, float w,
                               float scale, const Color &tint, bool tail) {
    if (w <= 0.0f) return x;
    if (!tail) return String(Ellipsize(text, w, scale), x, y, scale, tint);
    // Keep the editable tail, but never split a UTF-8 codepoint.
    std::size_t first = 0;
    while (first < text.size() && renderer_->MeasureText(text.substr(first), scale) > w) {
        ++first;
        while (first < text.size() &&
               (static_cast<unsigned char>(text[first]) & 0xC0u) == 0x80u) ++first;
    }
    return String(text.substr(first), x, y, scale, tint);
}

void UISystem::PaintProgressBar(float offset_x, float offset_y, float w,
                           float h, float value, const Color &fill, const Color &track) {
    if (camera_ == nullptr || renderer_ == nullptr) {
        return;
    }
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    const Vec2 p{offset_x, offset_y};
    // Clamp rather than trust: a health bar fed 1.4 or -0.2 should still look
    // like a bar, and a fill wider than the track would read as a glitch.
    const float t = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kFrameLine);
    Quad({p.x, p.y, w, h}, track);
    if (t > 0.0f) {
        Quad({p.x, p.y, w * t, h}, fill);
    }
}

bool UISystem::PaintRadioButton(std::string_view key, float offset_x,
                           float offset_y, float size, bool selected,
                           std::string_view label, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, size, size};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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

    Quad({p.x - 2.0f, p.y - 2.0f, size + 4.0f, size + 4.0f},
                        hover ? kThumbHot : kFrameLine);
    Quad(bounds, kField);
    if (selected) {
        // Inset block, like the checkbox tick: a ring would need a line
        // primitive, and this reads at any size.
        const float inset = size * 0.28f;
        Quad({p.x + inset, p.y + inset, size - inset * 2.0f,
                             size - inset * 2.0f},
                            kCheck);
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        String(label, p.x + size + 8.0f, p.y + size * 0.5f - text_h * 0.5f,
               text_scale, Color::White());
    }
    return clicked;
}

bool UISystem::PaintComboBegin(std::string_view key, float offset_x,
                          float offset_y, float w, float h, std::string_view label,
                          bool open, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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
    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kFrameLine);
    Quad(bounds, fill);
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
        Quad({cx + 2.0f, cy, 4.0f, 2.0f}, Color::White());
        Quad({cx, cy + 2.0f, 8.0f, 2.0f}, Color::White());
    } else {
        Quad({cx, cy, 8.0f, 2.0f}, Color::White());
        Quad({cx + 2.0f, cy + 2.0f, 4.0f, 2.0f}, Color::White());
    }
    return clicked;
}

bool UISystem::PaintComboItem(std::string_view key, float offset_x,
                         float offset_y, float w, float h, std::string_view label,
                         bool selected, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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
    Quad(bounds, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 8.0f, p.y + h * 0.5f - text_h * 0.5f, w - 16.0f,
                       text_scale, Color::White());
    }
    return clicked;
}

bool UISystem::PaintWindow(std::string_view key, float offset_x,
                      float offset_y, float w, float h, std::string_view title,
                      float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    // The bar is sized off the text so a bigger scale gets a bigger grab
    // target, with a floor so an empty title still leaves something to hold.
    const float bar_h = std::max(22.0f,
                                 renderer_->GetFont().LineHeight() * text_scale + 10.0f);
    const Rect bar{p.x, p.y, w, bar_h};
    const Vec2 mouse = Mouse();
    const bool hover_bar = bar.Contains(mouse) && MouseVisible();
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

    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kFrameLine);
    Quad({p.x, p.y, w, h}, kPanel);
    Quad(bar, state.dragging ? kThumbActive : kThumb);
    // A one-pixel rule under the bar: the cheapest way to say "this part
    // moves the panel, the rest does not".
    Quad({p.x, p.y + bar_h, w, 1.0f}, kFrameLine);
    if (!title.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(title, p.x + 8.0f, p.y + bar_h * 0.5f - text_h * 0.5f,
                       w - 16.0f, text_scale, Color::White());
    }
    return dragging;
}

bool UISystem::PaintTab(std::string_view key, float offset_x,
                   float offset_y, float w, float h, std::string_view label,
                   bool active, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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

    // Active tabs are brighter; the box API never grows beyond declared h.
    const float grow = 0.0f;
    const Color fill = active  ? kThumb
                       : (state.pressed ? kThumbHot
                          : hover       ? kThumbHot
                                        : kField);
    if (!active) {
        Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kDimLine);
    } else {
        Quad({p.x - 2.0f, p.y - 2.0f, 2.0f, h + 2.0f + grow}, kFrameLine);
        Quad({p.x + w, p.y - 2.0f, 2.0f, h + 2.0f + grow}, kFrameLine);
        Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, 2.0f}, kFrameLine);
    }
    Quad({p.x, p.y, w, h + grow}, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 8.0f, p.y + h * 0.5f - text_h * 0.5f, w - 16.0f,
                       text_scale, active ? Color::White()
                                          : Color{0.72f, 0.78f, 0.86f, 1.0f});
    }
    return clicked;
}

bool UISystem::PaintTree(std::string_view key, float offset_x,
                    float offset_y, float w, float h, std::string_view label,
                    bool open, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
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

    Quad(bounds, hover ? kThumbHot : Color::Clear());
    // Disclosure triangle in three bars: widening to the right reads as ">",
    // narrowing downwards reads as "v". Three quads, no line primitives (and
    // no scissor worries — those are not clipped).
    const float cx = p.x + 4.0f;
    const float cy = p.y + h * 0.5f;
    if (open) {
        Quad({cx, cy - 3.0f, 7.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 1.0f, cy - 1.0f, 5.0f, 2.0f},
                            Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 2.0f, cy + 1.0f, 3.0f, 2.0f},
                            Color{0.85f, 0.90f, 0.96f, 1.0f});
    } else {
        Quad({cx, cy - 3.0f, 2.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 2.0f, cy - 2.0f, 2.0f, 4.0f},
                            Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 4.0f, cy - 1.0f, 2.0f, 6.0f},
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

// Cursor layout is optional: legacy anchored widgets keep their coordinates.
float UISystem::AvailWidth() const {
    return boxes_.empty() ? 0.0f : boxes_.back().content.width;
}

float UISystem::ScrollTranslation(const std::vector<std::size_t> &ids) const {
    float dy = 0;
    for (const auto id : ids) dy += scroll_offsets_[id];
    return dy;
}

void UISystem::ApplyLayoutClip(const std::vector<LayoutClip> &clips) {
    if (!renderer_) return;
    if (clips.empty()) { renderer_->ClearClipRect(); return; }
    Rect intersection = clips.front().rect;
    intersection.y -= ScrollTranslation(clips.front().scrolls);
    for (std::size_t i = 1; i < clips.size(); ++i) {
        Rect r = clips[i].rect;
        r.y -= ScrollTranslation(clips[i].scrolls);
        const float right = std::min(intersection.x + intersection.width, r.x + r.width);
        const float bottom = std::min(intersection.y + intersection.height, r.y + r.height);
        intersection.x = std::max(intersection.x, r.x);
        intersection.y = std::max(intersection.y, r.y);
        intersection.width = std::max(0.0f, right - intersection.x);
        intersection.height = std::max(0.0f, bottom - intersection.y);
    }
    // Renderer::SetClipRect(empty) historically means clear. An offscreen
    // subpixel clip instead represents the empty intersection without changing
    // that public API (including on high-DPI framebuffers).
    if (intersection.width <= 0 || intersection.height <= 0)
        intersection = {-100.0f, -100.0f, 0.001f, 0.001f};
    intersection.x -= world_origin_.x;
    intersection.y -= world_origin_.y;
    renderer_->SetClipRect(intersection);
}

void UISystem::PushAbsolute(const Rect &r, float padding, float reserve) {
    const float pad = std::max(0.0f, std::min(padding, std::min(r.width, r.height) * 0.5f));
    LayoutBox box;
    box.content = {r.x + pad, r.y + pad, std::max(0.0f, r.width - 2 * pad),
                   std::max(0.0f, r.height - 2 * pad)};
    box.reserve = reserve;
    if (!boxes_.empty()) box.clips = boxes_.back().clips;
    box.clips.push_back({r, active_scrolls_});
    boxes_.push_back(std::move(box));
    ApplyLayoutClip(boxes_.back().clips);
}

void UISystem::Push(float x, float y, float w, float h, float padding) {
    w = std::max(0.0f, w); h = std::max(0.0f, h);
    const float reserve = std::max(0.0f, y + h);
    if (!boxes_.empty()) {
        x += boxes_.back().content.x;
        y += boxes_.back().content.y + boxes_.back().cursor;
    }
    Record("box", {x, y, w, h});
    PushAbsolute({x, y, w, h}, padding, reserve);
}

void UISystem::Pop() {
    if (boxes_.empty()) return;
    const float advance = boxes_.back().reserve;
    boxes_.pop_back();
    if (!boxes_.empty()) boxes_.back().cursor += advance;
    ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
}

void UISystem::Record(std::string_view kind, const Rect &r) {
    Rect bounds = boxes_.empty()
        ? Rect{0, 0, camera_ ? camera_->ViewportWidth() : 0,
                     camera_ ? camera_->ViewportHeight() : 0}
        : boxes_.back().content;
    if (kind == "widget text") bounds = paint_bounds_;
    if (kind == "tooltip") bounds = {0, 0, camera_->ViewportWidth(), camera_->ViewportHeight()};
    constexpr float epsilon = 0.01f;
    const bool overflow = r.x < bounds.x - epsilon || r.y < bounds.y - epsilon ||
        r.x + r.width > bounds.x + bounds.width + epsilon ||
        r.y + r.height > bounds.y + bounds.height + epsilon;
    audit_.push_back({std::string(kind), r, bounds, overflow});
    if (audit_enabled_ && overflow)
        std::fprintf(stderr, "[UI_OVERFLOW] %.*s rect=(%.1f,%.1f,%.1f,%.1f) box=(%.1f,%.1f,%.1f,%.1f)\n",
            static_cast<int>(kind.size()), kind.data(), r.x, r.y, r.width, r.height,
            bounds.x, bounds.y, bounds.width, bounds.height);
}

std::size_t UISystem::OverflowCount() const {
    return static_cast<std::size_t>(std::count_if(audit_.begin(), audit_.end(),
        [](const AuditRect &r) { return r.overflow; }));
}

std::string UISystem::Ellipsize(std::string_view text, float width, float scale) const {
    const auto newline = text.find('\n');
    const bool multiline = newline != std::string_view::npos;
    std::string result(text.substr(0, newline));
    if (!multiline && renderer_->MeasureText(result, scale) <= width) return result;
    const std::string suffix = renderer_->HasGlyph(0x2026) ? "…" : "...";
    if (renderer_->MeasureText(suffix, scale) > width) return {};
    while (!result.empty() && renderer_->MeasureText(result + suffix, scale) > width)
        PopUtf8(result);
    return result + suffix;
}

float UISystem::Text(std::string_view text, float scale, UITextOverflow overflow,
                     int align, const Color &tint, float min_scale) {
    if (boxes_.empty() || !renderer_) return 0;
    auto &box = boxes_.back();
    const float width = box.content.width;
    scale = std::max(0.0f, scale);
    min_scale = std::clamp(min_scale, 0.0f, scale);
    std::string drawn(text);
    float used = scale;
    if (overflow == UITextOverflow::Shrink) {
        const float natural = renderer_->MeasureText(text, scale);
        if (natural > width && natural > 0) used = std::max(min_scale, scale * width / natural);
    }
    if (overflow == UITextOverflow::Ellipsis || overflow == UITextOverflow::Shrink)
        drawn = Ellipsize(text, width, used);
    const bool wrap = overflow == UITextOverflow::Wrap;
    // The font treats wrap_width <= 0 as unlimited: zero-width boxes must
    // deliberately draw nothing, rather than switching wrapping off.
    const float height = width <= 0 || drawn.empty() || used <= 0 ? 0 :
        (wrap ? renderer_->GetFont().MeasureTextEx(drawn, used, width)
              : renderer_->GetFont().MeasureTextEx(drawn, used, 0));
    const float measured = wrap ? width : renderer_->MeasureText(drawn, used);
    const float x = box.content.x + (wrap ? 0 :
        (align == 1 ? (width - measured) * 0.5f : align == 2 ? width - measured : 0));
    const float y = box.content.y + box.cursor;
    last_rect_ = {x, y, width <= 0 ? 0 : measured, height};
    Record("text", last_rect_);
    box.cursor += height;
    auto clips = box.clips;
    // Prevent glyph bearings / a single oversized codepoint from escaping.
    clips.push_back({{box.content.x, box.content.y, width, box.content.height}, active_scrolls_});
    const auto ids = active_scrolls_;
    auto command = [this, drawn = std::move(drawn), x, y, used, width, wrap,
                    align, tint, height, clips = std::move(clips), ids]() {
        ApplyLayoutClip(clips);
        if (height > 0) renderer_->DrawTextEx(drawn, x,
            y - ScrollTranslation(ids), used, wrap ? align : 0, wrap ? width : 0, tint);
    };
    if (scrolls_.empty()) {
        command();
        ApplyLayoutClip(box.clips);
    } else pending_draws_.push_back(std::move(command));
    return height;
}

void UISystem::Row(float gap, const std::vector<float> &columns) {
    if (boxes_.empty()) return;
    LayoutRow row;
    row.depth = boxes_.size();
    row.gap = std::max(0.0f, gap);
    float fixed = 0, weights = 0;
    for (float w : columns) { if (w > 0) fixed += w; else weights -= w; }
    const float gaps = columns.empty() ? 0 : row.gap * (columns.size() - 1);
    const float flex = std::max(0.0f, AvailWidth() - fixed - gaps);
    for (float w : columns) row.widths.push_back(w < 0 && weights > 0 ? flex * -w / weights : std::max(0.0f, w));
    rows_.push_back(std::move(row));
}

void UISystem::Col(float fixed_width) {
    if (rows_.empty() || boxes_.size() != rows_.back().depth) return;
    auto &row = rows_.back();
    const auto &parent = boxes_.back();
    const float remaining = std::max(0.0f, parent.content.width - row.x);
    float width = fixed_width > 0 ? fixed_width : remaining;
    if (!row.widths.empty()) width = row.index < row.widths.size() ? row.widths[row.index] : 0;
    width = std::clamp(width, 0.0f, remaining);
    const Rect rect{parent.content.x + row.x, parent.content.y + parent.cursor,
                    width, std::max(0.0f, parent.content.height - parent.cursor)};
    row.x += width + row.gap;
    ++row.index;
    PushAbsolute(rect, 0, 0);
}

void UISystem::ColEnd() {
    if (rows_.empty() || boxes_.size() != rows_.back().depth + 1) return;
    rows_.back().height = std::max(rows_.back().height, boxes_.back().cursor);
    Pop();
}

void UISystem::RowEnd() {
    if (rows_.empty() || boxes_.size() != rows_.back().depth) return;
    const float height = rows_.back().height;
    rows_.pop_back();
    boxes_.back().cursor += height;
}

void UISystem::ScrollBegin(std::string_view key, float view_height) {
    if (boxes_.empty()) return;
    const auto &parent = boxes_.back();
    const Rect viewport{parent.content.x, parent.content.y + parent.cursor,
                        parent.content.width, std::max(0.0f, view_height)};
    Record("scroll viewport", viewport);
    const std::size_t index = scroll_offsets_.size();
    scroll_offsets_.push_back(scroll_states_[std::string(key)].offset);
    scroll_resolvers_.emplace_back();
    PushAbsolute(viewport, 0, viewport.height);
    // Keep the viewport clip, but give content an unbounded vertical cursor.
    // Scrollbar occupies the right 12 px, so text cannot paint under it.
    boxes_.back().content.width = std::max(0.0f, viewport.width - 12.0f);
    boxes_.back().content.height = std::numeric_limits<float>::max() * 0.25f;
    scrolls_.push_back({std::string(key), viewport, index, boxes_.size()});
    active_scrolls_.push_back(index);
}

float UISystem::ScrollEnd() {
    if (scrolls_.empty() || boxes_.size() != scrolls_.back().depth) return 0;
    const auto scroll = scrolls_.back();
    auto &state = scroll_states_[scroll.key];
    state.height = boxes_.back().cursor;
    // Resolve outer scrolls before their children once all heights are known.
    // This also moves nested viewport clips and scrollbars in the same frame.
    auto parent_ids = active_scrolls_;
    parent_ids.pop_back();
    auto clips = boxes_.back().clips;
    const float height = state.height;
    // Deepest hovered viewport receives the wheel once (not both nested
    // lists). Hit-testing uses the previous visible offsets; rendering and
    // clamping below use the newly measured height.
    if (input_ && height > scroll.viewport.height && scroll.depth >= wheel_depth_) {
        const Vec2 mouse{input_->MousePosition().x + world_origin_.x,
                         input_->MousePosition().y + world_origin_.y};
        bool hovered = true;
        for (const auto &clip : clips) {
            Rect visible = clip.rect;
            visible.y -= ScrollTranslation(clip.scrolls);
            hovered = hovered && visible.Contains(mouse);
        }
        if (hovered) { wheel_scroll_ = scroll.index; wheel_depth_ = scroll.depth; }
    }
    scroll_resolvers_[scroll.index] = [this, scroll, parent_ids, clips, height]() {
        ApplyLayoutClip(clips);
        auto &saved = scroll_states_[scroll.key];
        const float y = scroll.viewport.y - ScrollTranslation(parent_ids);
        paint_bounds_ = {scroll.viewport.x, y, scroll.viewport.width, scroll.viewport.height};
        float offset = std::clamp(saved.offset, 0.0f, std::max(0.0f, height - scroll.viewport.height));
        LayoutBox viewport_box;
        viewport_box.content = paint_bounds_;
        viewport_box.clips = clips;
        boxes_.push_back(viewport_box);
        if (input_ && camera_ && renderer_)
            offset = PaintScrollArea(scroll.key, scroll.viewport.x, y,
                                scroll.viewport.width, scroll.viewport.height, height, offset, wheel_scroll_ == scroll.index);
        boxes_.pop_back();
        saved.offset = offset;
        scroll_offsets_[scroll.index] = offset;
    };
    active_scrolls_.pop_back();
    scrolls_.pop_back();
    Pop();
    if (scrolls_.empty()) {
        for (auto &resolve : scroll_resolvers_) if (resolve) resolve();
        for (auto &resolve : scroll_resolvers_) resolve = {};
        for (auto &command : pending_draws_) command();
        pending_draws_.clear();
        ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
    }
    return state.height;
}

float UISystem::ScrollContentHeight(std::string_view key) const {
    const auto it = scroll_states_.find(std::string(key));
    return it == scroll_states_.end() ? 0 : it->second.height;
}
float UISystem::ScrollOffset(std::string_view key) const {
    const auto it = scroll_states_.find(std::string(key));
    return it == scroll_states_.end() ? 0 : it->second.offset;
}
float UISystem::LayoutToken(std::string_view name) const {
    const auto it = tokens_.find(std::string(name));
    return it == tokens_.end() ? 0 : it->second;
}
bool UISystem::SetLayoutToken(std::string_view name, float value) {
    const auto it = tokens_.find(std::string(name));
    if (it == tokens_.end() || !std::isfinite(value) || value < 0) return false;
    it->second = value;
    return true;
}

Rect UISystem::Allocate(float height, std::string_view kind) {
    if (boxes_.empty()) { last_rect_ = {}; return {}; }
    auto &box = boxes_.back();
    last_rect_ = {box.content.x, box.content.y + box.cursor,
                  box.content.width, std::max(0.0f, height)};
    paint_bounds_ = last_rect_;
    Record(kind, last_rect_);
    box.cursor += last_rect_.height;
    return last_rect_;
}

void UISystem::Quad(const Rect &rect, const Color &tint) {
    if (!renderer_) return;
    auto clips = boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips;
    clips.push_back({paint_bounds_, active_scrolls_});
    if (scrolls_.empty()) {
        ApplyLayoutClip(clips);
        renderer_->DrawQuad(rect, tint);
        ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
        return;
    }
    const auto ids = active_scrolls_;
    pending_draws_.push_back([this, rect, tint, clips, ids]() {
        ApplyLayoutClip(clips);
        Rect shifted = rect;
        shifted.y -= ScrollTranslation(ids);
        renderer_->DrawQuad(shifted, tint);
    });
}

float UISystem::String(std::string_view text, float x, float y, float scale, const Color &tint) {
    if (!renderer_) return x;
    const float width = renderer_->MeasureText(text, scale);
    Record("widget text", {x, y, width, renderer_->GetFont().MeasureTextEx(text, scale, 0)});
    auto clips = boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips;
    clips.push_back({paint_bounds_, active_scrolls_});
    if (scrolls_.empty()) {
        ApplyLayoutClip(clips);
        renderer_->DrawText(text, x, y, scale, tint);
        ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
        return x + width;
    }
    const auto ids = active_scrolls_;
    pending_draws_.push_back([this, text = std::string(text), x, y, scale, tint, clips, ids]() {
        ApplyLayoutClip(clips);
        renderer_->DrawText(text, x, y - ScrollTranslation(ids), scale, tint);
    });
    return x + width;
}

void UISystem::Panel(const Color &fill, const Color &border) {
    if (boxes_.empty()) return;
    const Rect r = boxes_.back().content;
    last_rect_ = r;
    paint_bounds_ = r;
    Record("panel", r);
    PaintPanel(r.x, r.y, r.width, r.height, fill, border);
}
bool UISystem::Button(std::string_view key, float h, std::string_view text, float scale) {
    if (boxes_.empty()) return false;
    const Rect r = Allocate(h, "button");
    return PaintButton(key, r.x, r.y, r.width, r.height, text, scale);
}
float UISystem::Slider(std::string_view key, float h, float value, float min_value, float max_value) {
    if (boxes_.empty()) return value;
    const Rect r = Allocate(h, "slider");
    return PaintSlider(key, r.x, r.y, r.width, r.height, value, min_value, max_value);
}
bool UISystem::TextInput(std::string_view key, float h, std::size_t max_chars) {
    if (boxes_.empty()) return false;
    const Rect r = Allocate(h, "text_input");
    return PaintTextInput(key, r.x, r.y, r.width, r.height, max_chars);
}
void UISystem::ProgressBar(float h, float value, const Color &fill, const Color &track) {
    if (boxes_.empty()) return;
    const Rect r = Allocate(h, "progress");
    PaintProgressBar(r.x, r.y, r.width, r.height, value, fill, track);
}
bool UISystem::Checkbox(std::string_view key, float size, bool checked, std::string_view label, float scale) {
    if (boxes_.empty()) return false;
    size = std::max(0.0f, std::min(size, AvailWidth()));
    const float line = renderer_ ? renderer_->GetFont().LineHeight() * scale : 0;
    const Rect r = Allocate(std::max(size, line), "checkbox");
    const std::string fitted = renderer_ ? Ellipsize(label, std::max(0.0f, r.width - size - 8), scale) : std::string{};
    return PaintCheckbox(key, r.x, r.y + (r.height - size) / 2, size, checked, fitted, scale);
}
bool UISystem::RadioButton(std::string_view key, float size, bool selected, std::string_view label, float scale) {
    if (boxes_.empty()) return false;
    size = std::max(0.0f, std::min(size, AvailWidth()));
    const float line = renderer_ ? renderer_->GetFont().LineHeight() * scale : 0;
    const Rect r = Allocate(std::max(size, line), "radio");
    const std::string fitted = renderer_ ? Ellipsize(label, std::max(0.0f, r.width - size - 8), scale) : std::string{};
    return PaintRadioButton(key, r.x, r.y + (r.height - size) / 2, size, selected, fitted, scale);
}
bool UISystem::ComboBegin(std::string_view key, float h, std::string_view label, bool open, float scale) {
    if (boxes_.empty()) return false;
    const Rect r = Allocate(h, "combo");
    return PaintComboBegin(key, r.x, r.y, r.width, r.height, label, open, scale);
}
bool UISystem::ComboItem(std::string_view key, float h, std::string_view label, bool selected, float scale) {
    if (boxes_.empty()) return false;
    const Rect r = Allocate(h, "combo_item");
    return PaintComboItem(key, r.x, r.y, r.width, r.height, label, selected, scale);
}
bool UISystem::Window(std::string_view key, std::string_view title, float scale) {
    if (boxes_.empty()) return false;
    const Rect r = boxes_.back().content;
    last_rect_ = r;
    paint_bounds_ = r;
    Record("window", r);
    return PaintWindow(key, r.x, r.y, r.width, r.height, title, scale);
}
bool UISystem::Tab(std::string_view key, float h, std::string_view label, bool active, float scale) {
    if (boxes_.empty()) return false;
    const Rect r = Allocate(h, "tab");
    return PaintTab(key, r.x, r.y, r.width, r.height, label, active, scale);
}
bool UISystem::Tree(std::string_view key, float h, std::string_view label, bool open, float scale) {
    if (boxes_.empty()) return false;
    const Rect r = Allocate(h, "tree");
    return PaintTree(key, r.x, r.y, r.width, r.height, label, open, scale);
}
void UISystem::Tip(std::string_view text) {
    const Rect r = last_rect_;
    PaintTip(r.x, r.y, r.width, r.height, text);
}

} // namespace fake2d
