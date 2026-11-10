#include "fake2d/ui.h"
#include "fake2d/ease.h"
#include <cstdlib>

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
    prior_focus_ = std::move(focus_ring_);
    focus_ring_.clear();
    nav_activate_ = false;
    nav_adjust_step_ = 0;
    if (input_) {
        const bool shift = input_->KeyDown("left_shift") || input_->KeyDown("right_shift");
        auto it = std::find_if(prior_focus_.begin(), prior_focus_.end(),
                               [this](const FocusEntry &e) { return e.key == nav_focus_; });
        const bool editing = it != prior_focus_.end() && it->text && focused_ == nav_focus_;
        const float axis = input_->GamepadAxis(0, "left_y");
        const float axis_x = input_->GamepadAxis(0, "left_x");
        const bool stick = std::abs(axis) > .55f || std::abs(axis_x) > .55f;
        int step = input_->KeyPressed("tab") ? (shift ? -1 : 1) : 0;
        if (!editing) {
            const bool adjustable = it != prior_focus_.end() && it->adjustable;
            if (input_->KeyPressed("down") || (!adjustable && input_->KeyPressed("right")) ||
                input_->GamepadButtonPressed(0, "dpad_down") ||
                (!adjustable && input_->GamepadButtonPressed(0, "dpad_right")) ||
                (stick && !stick_held_ && (axis > .55f || (!adjustable && axis_x > .55f))))
                step = 1;
            if (input_->KeyPressed("up") || (!adjustable && input_->KeyPressed("left")) ||
                input_->GamepadButtonPressed(0, "dpad_up") ||
                (!adjustable && input_->GamepadButtonPressed(0, "dpad_left")) ||
                (stick && !stick_held_ && (axis < -.55f || (!adjustable && axis_x < -.55f))))
                step = -1;
            if (adjustable && stick && !stick_held_)
                nav_adjust_step_ = axis_x > .55f ? 1 : axis_x < -.55f ? -1 : 0;
            nav_activate_ = input_->KeyPressed("enter") || input_->KeyPressed("space") ||
                            input_->GamepadButtonPressed(0, "a");
        }
        stick_held_ = stick;
        if (step && !prior_focus_.empty()) {
            int index = it == prior_focus_.end() ? (step > 0 ? -1 : 0)
                                                 : static_cast<int>(it - prior_focus_.begin());
            index = (index + step + static_cast<int>(prior_focus_.size())) %
                    static_cast<int>(prior_focus_.size());
            nav_focus_ = prior_focus_[index].key;
            focused_ = prior_focus_[index].text ? nav_focus_ : "";
        }
    }
    next_limits_ = {};
    row_sequence_ = 0;
    deferred_ = 0;
    flows_.clear();
    disabled_.clear();
    lists_.clear();
    named_rects_.clear();
    in_modal_ = in_popup_ = false;
    if (!boxes_.empty() && renderer_)
        renderer_->ClearClipRect();
    boxes_.clear();
    if (reference_w_ > 0)
        SetScale(reference_w_, reference_h_, scale_mode_);
    overlay_scrolls_.clear();
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
    if (!CanInteract())
        return false;
    if (!input_ || boxes_.empty())
        return true;
    const Vec2 mouse = RawMouse();
    for (const auto &clip : boxes_.back().clips) {
        Rect visible = clip.measured ? *clip.measured : clip.rect;
        visible.y -= ScrollTranslation(clip.scrolls);
        if (!visible.Contains(mouse))
            return false;
    }
    return true;
}

Vec2 UISystem::Mouse() const {
    if (!CanInteract())
        return {-1e20f, -1e20f};
    Vec2 p = RawMouse();
    p.y += ScrollTranslation(active_scrolls_);
    return p;
}

void UISystem::PaintPanel(float offset_x, float offset_y, float w, float h, const Color &fill,
                          const Color &border) {
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

void UISystem::PaintTip(float offset_x, float offset_y, float w, float h, std::string_view text) {
    if (camera_ == nullptr || input_ == nullptr || text.empty()) {
        return;
    }
    const Vec2 p{offset_x, offset_y};
    const Vec2 mouse = Mouse();
    const bool inside =
        mouse.x >= p.x && mouse.x <= p.x + w && mouse.y >= p.y && mouse.y <= p.y + h;
    if (!inside) {
        // Leaving restarts the hover clock (and never keeps a stale tooltip).
        tip_hover_key_.clear();
        return;
    }
    // Hover key: same rect + text keeps the clock running across frames.
    const std::string key = std::to_string(static_cast<int>(p.x)) + "," +
                            std::to_string(static_cast<int>(p.y)) + "," + std::string(text);
    const double now =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
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
    const float vw = (camera_->ViewportWidth() - scale_offset_.x) / scale_;
    const float vh = (camera_->ViewportHeight() - scale_offset_.y) / scale_;
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
    renderer_->SetClipRect({0, 0, camera_->ViewportWidth(), camera_->ViewportHeight()});
    Record("tooltip", {x, y, tw, th});
    renderer_->DrawQuad(MapRect({x - 7.0f, y - 5.0f, tw + 14.0f, th + 10.0f}),
                        Color{0.55f, 0.46f, 0.28f, 0.95f});
    renderer_->DrawQuad(MapRect({x - 5.0f, y - 3.0f, tw + 10.0f, th + 6.0f}),
                        Color{0.07f, 0.08f, 0.11f, 0.97f});
    renderer_->DrawTextEx(tooltip_, x * scale_ + scale_offset_.x, y * scale_ + scale_offset_.y,
                          scale * scale_, 0, width * scale_, Color{0.93f, 0.90f, 0.82f, 1.0f});
    ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
}

bool UISystem::PaintButton(std::string_view key, float offset_x, float offset_y, float w, float h,
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
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
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
    Color label_color = ThemeColor("text");
    if (state.pressed) {
        fill = ThemeColor("pressed");
    } else if (hover) {
        fill = ThemeColor("hover");
    } else {
        fill = ThemeColor("button");
    }

    const float border = ThemeMetric("border");
    Quad({p.x - border, p.y - border, w + border * 2, h + border * 2}, ThemeColor("border"));
    Quad(bounds, fill);

    if (!text.empty()) {
        const float available = std::max(0.0f, w - 8.0f);
        const float natural = renderer_->MeasureText(text, text_scale);
        const float used_scale =
            natural > available && natural > 0
                ? std::min(text_scale, std::max(ThemeMetric("min_font_scale"),
                                                text_scale * available / natural))
                : text_scale;
        const std::string fitted = Ellipsize(text, available, used_scale);
        const float text_w = renderer_->MeasureText(fitted, used_scale);
        const float text_h = renderer_->GetFont().LineHeight() * used_scale;
        // A tiny button must clip vertically as well as fit horizontally.

        String(fitted, p.x + (w - text_w) * 0.5f, p.y + (h - text_h) * 0.5f, used_scale,
               label_color);
    }
    return clicked;
}

float UISystem::PaintSlider(std::string_view key, float offset_x, float offset_y, float w, float h,
                            float value, float min_value, float max_value) {
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
    const bool hover = (thumb.Contains(mouse) || track.Contains(mouse)) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
    const bool mouse_pressed = CanInteract() && input_->MousePressed(0);
    const bool mouse_released = input_->MouseReleased(0);

    if (mouse_pressed && hover) {
        state.dragging = true;
        state.pressed = true;
        if (thumb.Contains(mouse)) {
            // Grab the thumb where it was clicked, so it does not jump.
            state.grab = mouse.x - thumb.x;
        } else {
            // Clicking the bare track jumps there, centred on the thumb.
            next =
                min_value + (mouse.x - p.x - thumb_w * 0.5f) / range_px * (max_value - min_value);
            next = clamp_value(next);
            state.grab = thumb_w * 0.5f;
        }
    }
    if (state.dragging && mouse_down) {
        next = min_value + (mouse.x - p.x - state.grab) / range_px * (max_value - min_value);
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
    const Color thumb_color = state.dragging ? ThemeColor("accent")
                              : hover        ? ThemeColor("hover")
                                             : kThumb;
    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, ThemeColor("track"));
    Quad(track, ThemeColor("track"));
    Quad({p.x + drawn_t * range_px, p.y, thumb_w, h}, thumb_color);
    return next;
}

bool UISystem::PaintCheckbox(std::string_view key, float offset_x, float offset_y, float size,
                             bool checked, std::string_view label, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Vec2 mouse = Mouse();
    const Rect bounds{p.x, p.y, size, size};
    const bool hover = bounds.Contains(mouse) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
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
         hover ? ThemeColor("hover") : ThemeColor("border"));
    Quad(bounds, ThemeColor("field"));
    if (checked) {
        // Inset tick block: cheap, and reads at any size.
        const float inset = size * 0.26f;
        Quad({p.x + inset, p.y + inset, size - inset * 2.0f, size - inset * 2.0f},
             ThemeColor("check"));
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        String(label, p.x + size + 8.0f, p.y + size * 0.5f - text_h * 0.5f, text_scale,
               Color::White());
    }
    return clicked;
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
    state.caret = state.anchor = state.text.size();
}

float UISystem::PaintScrollArea(std::string_view key, float offset_x, float offset_y, float w,
                                float h, float content_height, float scroll_y, bool wheel_enabled) {
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
    const float thumb_h =
        content_height > 0.0f ? std::min(h, std::max(24.0f, h * (h / content_height))) : h;
    const float thumb_track = h - thumb_h;

    const bool mouse_down = CanInteract() && input_->MouseDown(0);
    const bool mouse_pressed = CanInteract() && input_->MousePressed(0);
    const bool mouse_released = input_->MouseReleased(0);

    float next = scroll_y;
    if (wheel_enabled && hover && input_->MouseWheel() != 0.0) {
        // Wheel away (positive) scrolls the content up, i.e. the offset down.
        next -= static_cast<float>(input_->MouseWheel()) * kWheelStep;
    }

    const float thumb_y = p.y + (max_scroll > 0.0f ? next / max_scroll : 0.0f) * thumb_track;
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
        Quad(bar, ThemeColor("track"));
        const float drawn = p.y + (next / max_scroll) * thumb_track;
        Quad({bar.x + 1.0f, drawn, bar_w - 2.0f, thumb_h}, state.dragging ? ThemeColor("accent")
                                                           : hover        ? ThemeColor("hover")
                                                                          : kThumb);
    }
    return next;
}

float UISystem::DrawFittedText(std::string_view text, float x, float y, float w, float scale,
                               const Color &tint, bool tail) {
    if (w <= 0.0f)
        return x;
    if (!tail)
        return String(Ellipsize(text, w, scale), x, y, scale, tint);
    // Keep the editable tail, but never split a UTF-8 codepoint.
    std::size_t first = 0;
    while (first < text.size() && renderer_->MeasureText(text.substr(first), scale) > w) {
        ++first;
        while (first < text.size() && (static_cast<unsigned char>(text[first]) & 0xC0u) == 0x80u)
            ++first;
    }
    return String(text.substr(first), x, y, scale, tint);
}

void UISystem::PaintProgressBar(float offset_x, float offset_y, float w, float h, float value,
                                const Color &fill, const Color &track) {
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
    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, ThemeColor("border"));
    Quad({p.x, p.y, w, h}, track);
    if (t > 0.0f) {
        Quad({p.x, p.y, w * t, h}, fill);
    }
}

bool UISystem::PaintRadioButton(std::string_view key, float offset_x, float offset_y, float size,
                                bool selected, std::string_view label, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, size, size};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
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
         hover ? ThemeColor("hover") : ThemeColor("border"));
    Quad(bounds, ThemeColor("field"));
    if (selected) {
        // Inset block, like the checkbox tick: a ring would need a line
        // primitive, and this reads at any size.
        const float inset = size * 0.28f;
        Quad({p.x + inset, p.y + inset, size - inset * 2.0f, size - inset * 2.0f},
             ThemeColor("check"));
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        String(label, p.x + size + 8.0f, p.y + size * 0.5f - text_h * 0.5f, text_scale,
               Color::White());
    }
    return clicked;
}

bool UISystem::PaintComboBegin(std::string_view key, float offset_x, float offset_y, float w,
                               float h, std::string_view label, bool open, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
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

    const Color fill = open ? ThemeColor("accent")
                            : (state.pressed ? ThemeColor("accent")
                               : hover       ? ThemeColor("hover")
                                             : kThumb);
    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, ThemeColor("border"));
    Quad(bounds, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        const float left = p.x + 8.0f;
        DrawFittedText(label, left, p.y + h * 0.5f - text_h * 0.5f, w - 28.0f, text_scale,
                       Color::White());
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

bool UISystem::PaintComboItem(std::string_view key, float offset_x, float offset_y, float w,
                              float h, std::string_view label, bool selected, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
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

    const Color fill = hover ? ThemeColor("hover") : (selected ? kThumb : ThemeColor("field"));
    Quad(bounds, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 8.0f, p.y + h * 0.5f - text_h * 0.5f, w - 16.0f, text_scale,
                       Color::White());
    }
    return clicked;
}

bool UISystem::PaintWindow(std::string_view key, float offset_x, float offset_y, float w, float h,
                           std::string_view title, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    // The bar is sized off the text so a bigger scale gets a bigger grab
    // target, with a floor so an empty title still leaves something to hold.
    const float bar_h = std::max(22.0f, renderer_->GetFont().LineHeight() * text_scale + 10.0f);
    const Rect bar{p.x, p.y, w, bar_h};
    const Vec2 mouse = Mouse();
    const bool hover_bar = bar.Contains(mouse) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
    const bool mouse_pressed = CanInteract() && input_->MousePressed(0);

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

    Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, ThemeColor("border"));
    Quad({p.x, p.y, w, h}, ThemeColor("panel"));
    Quad(bar, state.dragging ? ThemeColor("accent") : kThumb);
    // A one-pixel rule under the bar: the cheapest way to say "this part
    // moves the panel, the rest does not".
    Quad({p.x, p.y + bar_h, w, 1.0f}, ThemeColor("border"));
    if (!title.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(title, p.x + 8.0f, p.y + bar_h * 0.5f - text_h * 0.5f, w - 16.0f, text_scale,
                       Color::White());
    }
    return dragging;
}

bool UISystem::PaintTab(std::string_view key, float offset_x, float offset_y, float w, float h,
                        std::string_view label, bool active, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
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
    const Color fill = active ? kThumb
                              : (state.pressed ? ThemeColor("hover")
                                 : hover       ? ThemeColor("hover")
                                               : ThemeColor("field"));
    if (!active) {
        Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, h + 4.0f}, kDimLine);
    } else {
        Quad({p.x - 2.0f, p.y - 2.0f, 2.0f, h + 2.0f + grow}, ThemeColor("border"));
        Quad({p.x + w, p.y - 2.0f, 2.0f, h + 2.0f + grow}, ThemeColor("border"));
        Quad({p.x - 2.0f, p.y - 2.0f, w + 4.0f, 2.0f}, ThemeColor("border"));
    }
    Quad({p.x, p.y, w, h + grow}, fill);
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 8.0f, p.y + h * 0.5f - text_h * 0.5f, w - 16.0f, text_scale,
                       active ? Color::White() : Color{0.72f, 0.78f, 0.86f, 1.0f});
    }
    return clicked;
}

bool UISystem::PaintTree(std::string_view key, float offset_x, float offset_y, float w, float h,
                         std::string_view label, bool open, float text_scale) {
    if (camera_ == nullptr || renderer_ == nullptr || input_ == nullptr) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];

    const Vec2 p{offset_x, offset_y};
    const Rect bounds{p.x, p.y, w, h};
    const Vec2 mouse = Mouse();
    const bool hover = bounds.Contains(mouse) && MouseVisible();
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
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

    Quad(bounds, hover ? ThemeColor("hover") : Color::Clear());
    // Disclosure triangle in three bars: widening to the right reads as ">",
    // narrowing downwards reads as "v". Three quads, no line primitives (and
    // no scissor worries — those are not clipped).
    const float cx = p.x + 4.0f;
    const float cy = p.y + h * 0.5f;
    if (open) {
        Quad({cx, cy - 3.0f, 7.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 1.0f, cy - 1.0f, 5.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 2.0f, cy + 1.0f, 3.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
    } else {
        Quad({cx, cy - 3.0f, 2.0f, 2.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 2.0f, cy - 2.0f, 2.0f, 4.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
        Quad({cx + 4.0f, cy - 1.0f, 2.0f, 6.0f}, Color{0.85f, 0.90f, 0.96f, 1.0f});
    }
    if (!label.empty()) {
        const float text_h = renderer_->GetFont().LineHeight() * text_scale;
        DrawFittedText(label, p.x + 18.0f, p.y + h * 0.5f - text_h * 0.5f, w - 24.0f, text_scale,
                       Color::White());
    }
    return clicked;
}

bool UISystem::IsHovered(std::string_view key) const {
    if (const WidgetState *state = const_cast<UISystem *>(this)->Find(key)) {
        return state->hover;
    }
    return false;
}

// Cursor layout is immediate-mode; values and declaration order belong to the caller.
float UISystem::AvailWidth() const { return boxes_.empty() ? 0.0f : boxes_.back().content.width; }

float UISystem::ScrollTranslation(const std::vector<std::size_t> &ids) const {
    float dy = 0;
    for (const auto id : ids)
        dy += scroll_offsets_[id];
    return dy;
}

void UISystem::ApplyLayoutClip(const std::vector<LayoutClip> &clips) {
    if (!renderer_)
        return;
    if (clips.empty()) {
        renderer_->ClearClipRect();
        return;
    }
    Rect intersection = clips.front().measured ? *clips.front().measured : clips.front().rect;
    intersection.y -= ScrollTranslation(clips.front().scrolls);
    for (std::size_t i = 1; i < clips.size(); ++i) {
        Rect r = clips[i].measured ? *clips[i].measured : clips[i].rect;
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
    renderer_->SetClipRect(MapRect(intersection));
}

void UISystem::PushAbsolute(const Rect &r, float padding, float reserve) {
    const float pad = std::max(0.0f, std::min(padding, std::min(r.width, r.height) * 0.5f));
    LayoutBox box;
    box.audit_start = audit_.size();
    box.content = {r.x + pad, r.y + pad, std::max(0.0f, r.width - 2 * pad),
                   std::max(0.0f, r.height - 2 * pad)};
    box.reserve = reserve;
    if (!boxes_.empty())
        box.clips = boxes_.back().clips;
    box.clips.push_back({r, active_scrolls_});
    boxes_.push_back(std::move(box));
    ApplyLayoutClip(boxes_.back().clips);
}

void UISystem::Push(float x, float y, float w, float h, float padding) {
    const auto limits = next_limits_;
    next_limits_ = {};
    const bool automatic = h <= 0;
    w = std::clamp(w, limits.min_w, limits.max_w);
    h = automatic ? 1e9f : std::clamp(h, limits.min_h, limits.max_h);
    const float y_offset = y;
    const float reserve = std::max(0.0f, y + h);
    if (!boxes_.empty()) {
        x += boxes_.back().content.x;
        y += boxes_.back().content.y + boxes_.back().cursor;
    }
    if (!automatic)
        Record("box", {x, y, w, h});
    PushAbsolute({x, y, w, h}, padding, reserve);
    auto &box = boxes_.back();
    box.limits = limits;
    box.auto_height = automatic;
    box.y_offset = y_offset;
    box.padding = std::max(0.0f, std::min(padding, w * .5f));
    if (automatic) {
        box.measured = std::make_shared<Rect>(Rect{x, y, w, h});
        box.clips.back().measured = box.measured;
        ++deferred_;
    }
}

void UISystem::Pop() {
    if (boxes_.empty())
        return;
    auto box = boxes_.back();
    float advance = box.reserve;
    if (box.auto_height) {
        const float h =
            std::clamp(box.cursor + box.padding * 2, box.limits.min_h, box.limits.max_h);
        box.measured->height = h;
        advance = std::max(0.0f, box.y_offset + h);
        last_rect_ = *box.measured;
        for (std::size_t i = box.audit_start; i < audit_.size(); ++i) {
            auto &entry = audit_[i];
            if (entry.bounds.y == box.content.y && entry.bounds.height > 1e8f) {
                entry.bounds.height = std::max(0.0f, h - box.padding * 2);
                if ((entry.kind == "panel" || entry.kind == "window") && entry.rect.height > 1e8f)
                    entry.rect.height = entry.bounds.height;
                const bool previously_overflow = entry.overflow;
                entry.overflow =
                    entry.rect.x < entry.bounds.x - .01f || entry.rect.y < entry.bounds.y - .01f ||
                    entry.rect.x + entry.rect.width > entry.bounds.x + entry.bounds.width + .01f ||
                    entry.rect.y + entry.rect.height > entry.bounds.y + entry.bounds.height + .01f;
                if (audit_enabled_ && entry.overflow && !previously_overflow)
                    std::fprintf(stderr, "[UI_OVERFLOW] %s in resolved auto-height box\n",
                                 entry.kind.c_str());
            }
        }
        --deferred_;
    }
    if (box.overlay)
        advance = 0;
    boxes_.pop_back();
    if (!boxes_.empty())
        boxes_.back().cursor += advance;
    if (box.auto_height)
        Record("auto box", *box.measured);
    FlushDraws();
    ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
}

void UISystem::FlushDraws() {
    if (deferred_ || !scrolls_.empty())
        return;
    for (auto &resolve : scroll_resolvers_)
        if (resolve)
            resolve();
    for (auto &resolve : scroll_resolvers_)
        resolve = {};
    auto commands = std::move(pending_draws_);
    pending_draws_.clear();
    for (auto &command : commands)
        command();
}

void UISystem::Record(std::string_view kind, const Rect &r) {
    Rect bounds = boxes_.empty() ? Rect{-scale_offset_.x / scale_, -scale_offset_.y / scale_,
                                        camera_ ? camera_->ViewportWidth() / scale_ : 0,
                                        camera_ ? camera_->ViewportHeight() / scale_ : 0}
                                 : boxes_.back().content;
    if (kind == "widget text")
        bounds = paint_bounds_;
    if (kind == "tooltip")
        bounds = {-scale_offset_.x / scale_, -scale_offset_.y / scale_,
                  camera_->ViewportWidth() / scale_, camera_->ViewportHeight() / scale_};
    constexpr float epsilon = 0.01f;
    const bool overflow = r.x < bounds.x - epsilon || r.y < bounds.y - epsilon ||
                          r.x + r.width > bounds.x + bounds.width + epsilon ||
                          r.y + r.height > bounds.y + bounds.height + epsilon;
    audit_.push_back({std::string(kind), r, bounds, overflow});
    if (audit_enabled_ && overflow)
        std::fprintf(stderr,
                     "[UI_OVERFLOW] %.*s rect=(%.1f,%.1f,%.1f,%.1f) box=(%.1f,%.1f,%.1f,%.1f)\n",
                     static_cast<int>(kind.size()), kind.data(), r.x, r.y, r.width, r.height,
                     bounds.x, bounds.y, bounds.width, bounds.height);
}

std::size_t UISystem::OverflowCount() const {
    return static_cast<std::size_t>(
        std::count_if(audit_.begin(), audit_.end(), [](const AuditRect &r) { return r.overflow; }));
}

std::string UISystem::Ellipsize(std::string_view text, float width, float scale) const {
    const auto newline = text.find('\n');
    const bool multiline = newline != std::string_view::npos;
    std::string result(text.substr(0, newline));
    if (!multiline && renderer_->MeasureText(result, scale) <= width)
        return result;
    const std::string suffix = renderer_->HasGlyph(0x2026) ? "…" : "...";
    if (renderer_->MeasureText(suffix, scale) > width)
        return {};
    while (!result.empty() && renderer_->MeasureText(result + suffix, scale) > width)
        PopUtf8(result);
    return result + suffix;
}

float UISystem::Text(std::string_view text, float scale, UITextOverflow overflow, int align,
                     const Color &tint, float min_scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty() || !renderer_)
        return 0;
    auto &box = boxes_.back();
    const float width = box.content.width;
    scale = std::max(0.0f, scale);
    min_scale = std::clamp(min_scale, 0.0f, scale);
    Color text_color =
        (tint.r == 1 && tint.g == 1 && tint.b == 1 && tint.a == 1) ? ThemeColor("text") : tint;
    if (IsDisabled())
        text_color.a *= .4f;
    std::string drawn(text);
    float used = scale;
    if (overflow == UITextOverflow::Shrink) {
        const float natural = renderer_->MeasureText(text, scale);
        if (natural > width && natural > 0)
            used = std::max(min_scale, scale * width / natural);
    }
    if (overflow == UITextOverflow::Ellipsis || overflow == UITextOverflow::Shrink)
        drawn = Ellipsize(text, width, used);
    const bool wrap = overflow == UITextOverflow::Wrap;
    // The font treats wrap_width <= 0 as unlimited: zero-width boxes must
    // deliberately draw nothing, rather than switching wrapping off.
    const float height = width <= 0 || drawn.empty() || used <= 0
                             ? 0
                             : (wrap ? renderer_->GetFont().MeasureTextEx(drawn, used, width)
                                     : renderer_->GetFont().MeasureTextEx(drawn, used, 0));
    const float measured = wrap ? width : renderer_->MeasureText(drawn, used);
    const float x = box.content.x + (wrap ? 0
                                          : (align == 1   ? (width - measured) * 0.5f
                                             : align == 2 ? width - measured
                                                          : 0));
    const float y = box.content.y + box.cursor;
    last_rect_ = {x, y, width <= 0 ? 0 : measured, height};
    Record("text", last_rect_);
    box.cursor += height;
    auto clips = box.clips;
    // Prevent glyph bearings / a single oversized codepoint from escaping.
    clips.push_back({{box.content.x, box.content.y, width, box.content.height}, active_scrolls_});
    const auto ids = active_scrolls_;
    auto command = [this, drawn = std::move(drawn), x, y, used, width, wrap, align, text_color,
                    height, clips = std::move(clips), ids]() {
        ApplyLayoutClip(clips);
        if (height > 0)
            renderer_->DrawTextEx(drawn, x * scale_ + scale_offset_.x,
                                  (y - ScrollTranslation(ids)) * scale_ + scale_offset_.y,
                                  used * scale_, wrap ? align : 0, wrap ? width * scale_ : 0,
                                  text_color);
    };
    if (scrolls_.empty() && deferred_ == 0) {
        command();
        ApplyLayoutClip(box.clips);
    } else
        pending_draws_.push_back(std::move(command));
    return height;
}

void UISystem::Row(float gap, const std::vector<float> &columns) {
    if (boxes_.empty())
        return;
    LayoutRow row;
    row.depth = boxes_.size();
    row.cache_id = row_sequence_++;
    if (row_offset_cache_.size() <= row.cache_id)
        row_offset_cache_.resize(row.cache_id + 1);
    row.gap = std::max(0.0f, gap);
    float fixed = 0, weights = 0;
    for (float w : columns) {
        if (w > 0)
            fixed += w;
        else
            weights -= w;
    }
    const float gaps = columns.empty() ? 0 : row.gap * (columns.size() - 1);
    const float flex = std::max(0.0f, AvailWidth() - fixed - gaps);
    for (float w : columns)
        row.widths.push_back(w < 0 && weights > 0 ? flex * -w / weights : std::max(0.0f, w));
    rows_.push_back(std::move(row));
    ++deferred_;
}

void UISystem::Col(float fixed_width) {
    if (rows_.empty() || boxes_.size() != rows_.back().depth)
        return;
    auto &row = rows_.back();
    const auto &parent = boxes_.back();
    const float remaining = std::max(0.0f, parent.content.width - row.x);
    float width = fixed_width > 0 ? fixed_width : remaining;
    if (!row.widths.empty())
        width = row.index < row.widths.size() ? row.widths[row.index] : 0;
    width = std::clamp(width, 0.0f, remaining);
    width = std::clamp(width, std::min(remaining, next_limits_.min_w),
                       std::min(remaining, next_limits_.max_w));
    const Rect rect{
        parent.content.x + row.x, parent.content.y + parent.cursor, width,
        std::min(next_limits_.max_h, std::max(0.0f, parent.content.height - parent.cursor))};
    const std::size_t translation = scroll_offsets_.size();
    const auto &cache = row_offset_cache_[row.cache_id];
    const float previous_offset = row.index < cache.size() ? cache[row.index] : 0;
    scroll_offsets_.push_back(previous_offset);
    scroll_resolvers_.emplace_back();
    active_scrolls_.push_back(translation);
    row.offsets.push_back({translation, 0});
    row.x += width + row.gap;
    ++row.index;
    PushAbsolute(rect, 0, 0);
    boxes_.back().limits = next_limits_;
    next_limits_ = {};
}

void UISystem::ColEnd() {
    if (rows_.empty() || boxes_.size() != rows_.back().depth + 1)
        return;
    const auto &b = boxes_.back();
    const float h = std::clamp(b.cursor, b.limits.min_h, b.limits.max_h);
    rows_.back().height = std::max(rows_.back().height, h);
    rows_.back().offsets.back().second = h;
    active_scrolls_.pop_back();
    Pop();
}

void UISystem::RowEnd() {
    if (rows_.empty() || boxes_.size() != rows_.back().depth)
        return;
    const float height = rows_.back().height;
    auto &cache = row_offset_cache_[rows_.back().cache_id];
    cache.clear();
    for (const auto &offset : rows_.back().offsets) {
        const float dy = -(height - offset.second) * (rows_.back().align == 1   ? .5f
                                                      : rows_.back().align == 2 ? 1.0f
                                                                                : 0.0f);
        scroll_offsets_[offset.first] = dy;
        cache.push_back(dy);
    }
    rows_.pop_back();
    boxes_.back().cursor += height;
    --deferred_;
    FlushDraws();
}

void UISystem::ScrollBegin(std::string_view key, float view_height) {
    if (boxes_.empty())
        return;
    const auto &parent = boxes_.back();
    const Rect viewport{parent.content.x, parent.content.y + parent.cursor, parent.content.width,
                        std::max(0.0f, view_height)};
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
    if (scrolls_.empty() || boxes_.size() != scrolls_.back().depth)
        return 0;
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
        const Vec2 mouse = RawMouse();
        bool hovered = true;
        for (const auto &clip : clips) {
            Rect visible = clip.measured ? *clip.measured : clip.rect;
            visible.y -= ScrollTranslation(clip.scrolls);
            hovered = hovered && visible.Contains(mouse);
        }
        if (hovered) {
            wheel_scroll_ = scroll.index;
            wheel_depth_ = scroll.depth;
        }
    }
    const bool disabled_scope = IsDisabled(), modal_scope = in_modal_, popup_scope = in_popup_;
    scroll_resolvers_[scroll.index] = [this, scroll, parent_ids, clips, height, disabled_scope,
                                       modal_scope, popup_scope]() {
        const bool old_modal = in_modal_, old_popup = in_popup_;
        in_modal_ = modal_scope;
        in_popup_ = popup_scope;
        DisabledBegin(disabled_scope);
        ApplyLayoutClip(clips);
        auto &saved = scroll_states_[scroll.key];
        const float y = scroll.viewport.y - ScrollTranslation(parent_ids);
        paint_bounds_ = {scroll.viewport.x, y, scroll.viewport.width, scroll.viewport.height};
        float offset =
            std::clamp(saved.offset, 0.0f, std::max(0.0f, height - scroll.viewport.height));
        LayoutBox viewport_box;
        viewport_box.content = paint_bounds_;
        viewport_box.clips = clips;
        boxes_.push_back(viewport_box);
        if (input_ && camera_ && renderer_)
            offset = PaintScrollArea(scroll.key, scroll.viewport.x, y, scroll.viewport.width,
                                     scroll.viewport.height, height, offset,
                                     wheel_scroll_ == scroll.index);
        boxes_.pop_back();
        saved.offset = offset;
        scroll_offsets_[scroll.index] = offset;
        DisabledEnd();
        in_modal_ = old_modal;
        in_popup_ = old_popup;
    };
    active_scrolls_.pop_back();
    scrolls_.pop_back();
    Pop();
    FlushDraws();
    ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
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
    if (it == tokens_.end() || !std::isfinite(value) || value < 0)
        return false;
    it->second = value;
    return true;
}

Rect UISystem::Allocate(float height, std::string_view kind) {
    if (boxes_.empty()) {
        last_rect_ = {};
        return {};
    }
    auto &box = boxes_.back();
    last_rect_ = {box.content.x, box.content.y + box.cursor, box.content.width,
                  std::max(0.0f, height)};
    paint_bounds_ = last_rect_;
    Record(kind, last_rect_);
    box.cursor += last_rect_.height;
    return last_rect_;
}

void UISystem::Quad(const Rect &rect, const Color &color) {
    Color tint = color;
    if (IsDisabled())
        tint.a *= .4f;
    if (!renderer_)
        return;
    auto clips = boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips;
    clips.push_back({paint_bounds_, active_scrolls_});
    if (scrolls_.empty() && deferred_ == 0) {
        ApplyLayoutClip(clips);
        renderer_->DrawQuad(MapRect(rect), tint);
        ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
        return;
    }
    const auto ids = active_scrolls_;
    pending_draws_.push_back([this, rect, tint, clips, ids]() {
        ApplyLayoutClip(clips);
        Rect shifted = rect;
        shifted.y -= ScrollTranslation(ids);
        renderer_->DrawQuad(MapRect(shifted), tint);
    });
}

float UISystem::String(std::string_view text, float x, float y, float scale, const Color &color) {
    Color tint =
        (color.r == 1 && color.g == 1 && color.b == 1 && color.a == 1) ? ThemeColor("text") : color;
    if (IsDisabled())
        tint.a *= .4f;
    if (!renderer_)
        return x;
    const float width = renderer_->MeasureText(text, scale);
    Record("widget text", {x, y, width, renderer_->GetFont().MeasureTextEx(text, scale, 0)});
    auto clips = boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips;
    clips.push_back({paint_bounds_, active_scrolls_});
    if (scrolls_.empty() && deferred_ == 0) {
        ApplyLayoutClip(clips);
        renderer_->DrawText(text, x * scale_ + scale_offset_.x, y * scale_ + scale_offset_.y,
                            scale * scale_, tint);
        ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
        return x + width;
    }
    const auto ids = active_scrolls_;
    pending_draws_.push_back([this, text = std::string(text), x, y, scale, tint, clips, ids]() {
        ApplyLayoutClip(clips);
        renderer_->DrawText(text, x * scale_ + scale_offset_.x,
                            (y - ScrollTranslation(ids)) * scale_ + scale_offset_.y, scale * scale_,
                            tint);
    });
    return x + width;
}

void UISystem::Panel(const Color &fill, const Color &border) {
    if (boxes_.empty())
        return;
    const auto box = boxes_.back();
    last_rect_ = box.content;
    paint_bounds_ = last_rect_;
    Record("panel", last_rect_);
    const auto ids = active_scrolls_;
    const auto slice = panel_slice_;
    const float border_width = ThemeMetric("border");
    auto command = [this, box, ids, fill, border, slice, border_width]() {
        Rect r = box.content;
        if (box.measured)
            r.height = std::max(0.0f, box.measured->height - box.padding * 2);
        r.y -= ScrollTranslation(ids);
        ApplyLayoutClip(box.clips);
        if (!renderer_)
            return;
        const Rect mapped = MapRect(r);
        if (slice.texture && slice.texture->IsValid())
            renderer_->DrawNineSlice(*slice.texture, slice.source, mapped, slice.x * scale_,
                                     slice.y * scale_, fill);
        else {
            if (border.a > 0)
                renderer_->DrawQuad(
                    MapRect({r.x - border_width, r.y - border_width, r.width + 2 * border_width,
                             r.height + 2 * border_width}),
                    border);
            renderer_->DrawQuad(mapped, fill);
        }
    };
    if (scrolls_.empty() && deferred_ == 0)
        command();
    else
        pending_draws_.push_back(std::move(command));
}
bool UISystem::Button(std::string_view key, float h, std::string_view text, float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    const Rect r = Allocate(h, "button");
    const bool activate = FocusWidget(key, r);
    const bool clicked = PaintButton(key, r.x, r.y, r.width, r.height, text, scale);
    FocusIndicator(key, r);
    return clicked || activate;
}
float UISystem::Slider(std::string_view key, float h, float value, float min_value,
                       float max_value) {
    if (boxes_.empty())
        return value;
    const Rect r = Allocate(h, "slider");
    FocusWidget(key, r, false, true);
    if (CanInteract() && input_ && nav_focus_ == key) {
        if (input_->KeyPressed("left") || input_->GamepadButtonPressed(0, "dpad_left"))
            value -= (max_value - min_value) * .05f;
        if (input_->KeyPressed("right") || input_->GamepadButtonPressed(0, "dpad_right"))
            value += (max_value - min_value) * .05f;
    }
    if (CanInteract() && nav_focus_ == key)
        value += nav_adjust_step_ * (max_value - min_value) * .05f;
    const float next = PaintSlider(key, r.x, r.y, r.width, r.height, value, min_value, max_value);
    FocusIndicator(key, r);
    return next;
}
bool UISystem::TextInput(std::string_view key, float h, std::size_t max_chars) {
    if (boxes_.empty())
        return false;
    return EditText(key, Allocate(h, "text_input"), max_chars, false);
}
void UISystem::ProgressBar(float h, float value, const Color &fill, const Color &track) {
    if (boxes_.empty())
        return;
    const Rect r = Allocate(h, "progress");
    PaintProgressBar(r.x, r.y, r.width, r.height, value, fill, track);
}
bool UISystem::Checkbox(std::string_view key, float size, bool checked, std::string_view label,
                        float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    size = std::max(0.0f, std::min(size, AvailWidth()));
    const float line = renderer_ ? renderer_->GetFont().LineHeight() * scale : 0;
    const Rect r = Allocate(std::max(size, line), "checkbox");
    const std::string fitted =
        renderer_ ? Ellipsize(label, std::max(0.0f, r.width - size - 8), scale) : std::string{};
    const bool activate = FocusWidget(key, r);
    const bool clicked =
        PaintCheckbox(key, r.x, r.y + (r.height - size) / 2, size, checked, fitted, scale);
    FocusIndicator(key, r);
    return clicked || activate;
}
bool UISystem::RadioButton(std::string_view key, float size, bool selected, std::string_view label,
                           float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    size = std::max(0.0f, std::min(size, AvailWidth()));
    const float line = renderer_ ? renderer_->GetFont().LineHeight() * scale : 0;
    const Rect r = Allocate(std::max(size, line), "radio");
    const std::string fitted =
        renderer_ ? Ellipsize(label, std::max(0.0f, r.width - size - 8), scale) : std::string{};
    const bool activate = FocusWidget(key, r);
    const bool clicked =
        PaintRadioButton(key, r.x, r.y + (r.height - size) / 2, size, selected, fitted, scale);
    FocusIndicator(key, r);
    return clicked || activate;
}
bool UISystem::ComboBegin(std::string_view key, float h, std::string_view label, bool open,
                          float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    const Rect r = Allocate(h, "combo");
    const bool activate = FocusWidget(key, r);
    const bool clicked = PaintComboBegin(key, r.x, r.y, r.width, r.height, label, open, scale);
    FocusIndicator(key, r);
    return clicked || activate;
}
bool UISystem::ComboItem(std::string_view key, float h, std::string_view label, bool selected,
                         float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    const Rect r = Allocate(h, "combo_item");
    const bool activate = FocusWidget(key, r);
    const bool clicked = PaintComboItem(key, r.x, r.y, r.width, r.height, label, selected, scale);
    FocusIndicator(key, r);
    return clicked || activate;
}
bool UISystem::Window(std::string_view key, std::string_view title, float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    const Rect r = boxes_.back().content;
    last_rect_ = r;
    paint_bounds_ = r;
    Record("window", r);
    return PaintWindow(key, r.x, r.y, r.width, r.height, title, scale);
}
bool UISystem::Tab(std::string_view key, float h, std::string_view label, bool active,
                   float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    const Rect r = Allocate(h, "tab");
    const bool activate = FocusWidget(key, r);
    const bool clicked = PaintTab(key, r.x, r.y, r.width, r.height, label, active, scale);
    FocusIndicator(key, r);
    return clicked || activate;
}
bool UISystem::Tree(std::string_view key, float h, std::string_view label, bool open, float scale) {
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    if (boxes_.empty())
        return false;
    const Rect r = Allocate(h, "tree");
    const bool activate = FocusWidget(key, r);
    const bool clicked = PaintTree(key, r.x, r.y, r.width, r.height, label, open, scale);
    FocusIndicator(key, r);
    return clicked || activate;
}
void UISystem::Tip(std::string_view text) {
    const Rect r = last_rect_;
    PaintTip(r.x, r.y, r.width, r.height, text);
}

// Practical containers and interaction helpers (Phases 57–61).
void UISystem::Constrain(float min_w, float max_w, float min_h, float max_h) {
    if (!std::isfinite(min_w) || !std::isfinite(max_w) || !std::isfinite(min_h) ||
        !std::isfinite(max_h))
        return;
    min_w = std::max(0.0f, min_w);
    min_h = std::max(0.0f, min_h);
    next_limits_ = {min_w, std::max(min_w, max_w), min_h, std::max(min_h, max_h)};
}
void UISystem::RowAlign(int align) {
    if (!rows_.empty())
        rows_.back().align = std::clamp(align, 0, 2);
}
void UISystem::FlowBegin(float gap, float line_gap) {
    if (boxes_.empty())
        return;
    if (gap < 0)
        gap = ThemeMetric("spacing");
    if (line_gap < 0)
        line_gap = ThemeMetric("spacing");
    FlowState flow{};
    flow.depth = boxes_.size();
    flow.gap = std::max(0.0f, gap);
    flow.line_gap = std::max(0.0f, line_gap);
    flows_.push_back(std::move(flow));
}
void UISystem::FlowItem(float width) {
    if (flows_.empty() || boxes_.size() != flows_.back().depth)
        return;
    auto &flow = flows_.back();
    auto &parent = boxes_.back();
    width = std::clamp(width, 0.0f, parent.content.width);
    if (flow.x > 0 && flow.x + width > parent.content.width + .01f) {
        parent.cursor += flow.height + flow.line_gap;
        flow.x = flow.height = 0;
    }
    const float x = flow.x;
    flow.x += width + flow.gap;
    Push(x, 0, width, 0);
    boxes_.back().reserve = 0;
}
void UISystem::FlowItemEnd() {
    if (flows_.empty() || boxes_.size() != flows_.back().depth + 1)
        return;
    const float h = std::clamp(boxes_.back().cursor + 2 * boxes_.back().padding,
                               boxes_.back().limits.min_h, boxes_.back().limits.max_h);
    const float cursor = boxes_[boxes_.size() - 2].cursor;
    Pop();
    boxes_.back().cursor = cursor;
    flows_.back().height = std::max(flows_.back().height, h);
}
void UISystem::FlowEnd() {
    if (flows_.empty() || boxes_.size() != flows_.back().depth)
        return;
    boxes_.back().cursor += flows_.back().height;
    flows_.pop_back();
}
void UISystem::GridBegin(int columns, float gap, const std::vector<float> &widths) {
    if (gap < 0)
        gap = ThemeMetric("spacing");
    FlowBegin(gap, gap);
    if (flows_.empty())
        return;
    auto &f = flows_.back();
    f.columns = std::max(1, columns);
    std::vector<float> slots = widths;
    slots.resize(f.columns, -1);
    float fixed = 0, weights = 0;
    for (float w : slots) {
        if (w >= 0)
            fixed += w;
        else
            weights -= w;
    }
    const float available = std::max(0.0f, AvailWidth() - fixed - gap * (f.columns - 1));
    for (float w : slots)
        f.widths.push_back(w < 0 && weights > 0 ? available * -w / weights : w);
}
void UISystem::GridCell() {
    if (flows_.empty())
        return;
    auto &f = flows_.back();
    if (f.index > 0 && f.index % f.columns == 0) {
        boxes_.back().cursor += f.height + f.line_gap;
        f.x = f.height = 0;
    }
    float width = f.widths[f.index % f.columns];
    ++f.index;
    FlowItem(width);
}
void UISystem::GridCellEnd() { FlowItemEnd(); }
void UISystem::GridEnd() { FlowEnd(); }
float UISystem::Splitter(std::string_view key, float height, float initial, float minimum) {
    if (boxes_.empty())
        return initial;
    const Rect r = Allocate(height, "splitter");
    auto [it, inserted] = widgets_.try_emplace(std::string(key));
    auto &state = it->second;
    if (inserted)
        state.grab = initial;
    minimum = std::clamp(minimum, 0.0f, r.width * .5f);
    state.grab = std::clamp(state.grab, minimum, r.width - minimum);
    if (CanInteract() && nav_focus_ == key)
        state.grab = std::clamp(state.grab + nav_adjust_step_ * 8, minimum, r.width - minimum);
    Rect divider{r.x + state.grab - 3, r.y, 6, r.height};
    FocusWidget(key, divider, false, true);
    if (input_ && CanInteract()) {
        if (MouseVisible() && divider.Contains(Mouse()) && input_->MousePressed(0))
            state.dragging = true;
        if (state.dragging && input_->MouseDown(0))
            state.grab = std::clamp(Mouse().x - r.x, minimum, r.width - minimum);
        if (!input_->MouseDown(0))
            state.dragging = false;
        if (nav_focus_ == key) {
            if (input_->KeyPressed("left"))
                state.grab = std::max(minimum, state.grab - 8);
            if (input_->KeyPressed("right"))
                state.grab = std::min(r.width - minimum, state.grab + 8);
        }
    } else
        state.dragging = false;
    divider.x = r.x + state.grab - 3;
    Quad(divider, ThemeColor(state.dragging ? "accent" : "border"));
    FocusIndicator(key, divider);
    return state.grab;
}
bool UISystem::SetScale(float w, float h, std::string_view mode) {
    if (!boxes_.empty() || !camera_ || !std::isfinite(w) || !std::isfinite(h))
        return false;
    if (w <= 0 || h <= 0) {
        scale_ = 1;
        reference_w_ = reference_h_ = 0;
        scale_offset_ = {};
        return true;
    }
    const float sx = camera_->ViewportWidth() / w, sy = camera_->ViewportHeight() / h;
    if (mode == "width")
        scale_ = sx;
    else if (mode == "height")
        scale_ = sy;
    else if (mode == "contain")
        scale_ = std::min(sx, sy);
    else if (mode == "expand")
        scale_ = std::max(sx, sy);
    else
        return false;
    reference_w_ = w;
    reference_h_ = h;
    scale_mode_ = mode;
    scale_ = std::max(.0001f, scale_);
    scale_offset_ = {(camera_->ViewportWidth() - w * scale_) * .5f,
                     (camera_->ViewportHeight() - h * scale_) * .5f};
    return true;
}
Rect UISystem::MapRect(Rect r) const {
    return {r.x * scale_ + scale_offset_.x, r.y * scale_ + scale_offset_.y, r.width * scale_,
            r.height * scale_};
}
Vec2 UISystem::RawMouse() const {
    const Vec2 m = input_ ? input_->MousePosition() : Vec2{};
    return {(m.x - scale_offset_.x) / scale_ + world_origin_.x,
            (m.y - scale_offset_.y) / scale_ + world_origin_.y};
}
void UISystem::Remember(std::string_view name) { named_rects_[std::string(name)] = last_rect_; }
Rect UISystem::NamedRect(std::string_view name) const {
    auto it = named_rects_.find(std::string(name));
    return it == named_rects_.end() ? Rect{} : it->second;
}
bool UISystem::PushRelated(std::string_view target, std::string_view edge, float offset,
                           float width, float height, float padding, float right_inset) {
    if (boxes_.empty())
        return false;
    const auto parent = boxes_.back();
    if (!target.empty() && named_rects_.find(std::string(target)) == named_rects_.end())
        return false;
    const Rect r = target.empty() ? parent.content : NamedRect(target);
    float x = r.x, y = r.y;
    if (edge == "right")
        x += r.width + offset;
    else if (edge == "left")
        x += offset;
    else if (edge == "bottom")
        y += r.height + offset;
    else if (edge == "top")
        y += offset;
    else if (edge == "center_x")
        x += (r.width - width) * .5f + offset;
    else if (edge == "center_y")
        y += (r.height - height) * .5f + offset;
    else
        return false;
    if (right_inset >= 0)
        width = std::max(0.0f, parent.content.x + parent.content.width - right_inset - x);
    Push(x - parent.content.x, y - parent.content.y - parent.cursor, width, height, padding);
    // Relations are overlays, not additional flow reservations.
    boxes_.back().reserve = 0;
    boxes_.back().overlay = true;
    boxes_.back().y_offset = 0;
    return true;
}
float UISystem::ThemeMetric(std::string_view name) const {
    auto it = theme_metrics_.find(std::string(name));
    return it == theme_metrics_.end() ? 0 : it->second;
}
bool UISystem::SetThemeMetric(std::string_view name, float value) {
    auto it = theme_metrics_.find(std::string(name));
    if (it == theme_metrics_.end() || !std::isfinite(value) || value < 0)
        return false;
    it->second = value;
    return true;
}
Color UISystem::ThemeColor(std::string_view name) const {
    auto it = theme_colors_.find(std::string(name));
    return it == theme_colors_.end() ? Color::Clear() : it->second;
}
bool UISystem::SetThemeColor(std::string_view name, const Color &value) {
    auto it = theme_colors_.find(std::string(name));
    if (it == theme_colors_.end() || !std::isfinite(value.r) || !std::isfinite(value.g) ||
        !std::isfinite(value.b) || !std::isfinite(value.a))
        return false;
    it->second = value;
    return true;
}
void UISystem::SetPanelSlice(const Texture2D *texture, const Rect &source, float x, float y) {
    panel_slice_ = {texture, source, std::max(0.0f, x), std::max(0.0f, y)};
}
void UISystem::ThemedPanel() { Panel(ThemeColor("panel"), ThemeColor("border")); }
void UISystem::DisabledBegin(bool disabled) { disabled_.push_back(IsDisabled() || disabled); }
void UISystem::DisabledEnd() {
    if (!disabled_.empty())
        disabled_.pop_back();
}
bool UISystem::CanInteract() const {
    return !IsDisabled() && (modal_.empty() || in_modal_) && (popup_.empty() || in_popup_);
}
bool UISystem::FocusWidget(std::string_view key, const Rect &r, bool text, bool adjustable) {
    if (!CanInteract() || !input_) {
        if (nav_focus_ == key)
            nav_focus_.clear();
        if (focused_ == key)
            focused_.clear();
        return false;
    }
    // Clipped widgets are not tab stops. Geometry and mouse use reference points.
    Rect visible = r;
    visible.y -= ScrollTranslation(active_scrolls_);
    bool on_screen = true;
    if (!boxes_.empty())
        for (const auto &clip : boxes_.back().clips) {
            Rect c = clip.measured ? *clip.measured : clip.rect;
            c.y -= ScrollTranslation(clip.scrolls);
            if (visible.x + visible.width <= c.x || visible.x >= c.x + c.width ||
                visible.y + visible.height <= c.y || visible.y >= c.y + c.height)
                on_screen = false;
        }
    if (!on_screen)
        return false;
    focus_ring_.push_back({std::string(key), visible, text, adjustable});
    if (MouseVisible() && r.Contains(Mouse()) && input_->MousePressed(0)) {
        nav_focus_ = key;
        if (!text)
            focused_.clear();
    }
    const bool active = nav_focus_ == key;
    return active && nav_activate_ && !text;
}
void UISystem::FocusIndicator(std::string_view key, const Rect &r) {
    if (!CanInteract() || nav_focus_ != key)
        return;
    const Rect saved = paint_bounds_;
    paint_bounds_ = r;
    Quad({r.x, r.y, r.width, 2}, ThemeColor("accent"));
    Quad({r.x, r.y + r.height - 2, r.width, 2}, ThemeColor("accent"));
    paint_bounds_ = saved;
}
void UISystem::Sprite(const Texture2D *texture, const Rect &src, const Rect &dst,
                      const Color &color) {
    if (!renderer_ || !texture || !texture->IsValid())
        return;
    auto clips = boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips;
    clips.push_back({paint_bounds_, active_scrolls_});
    const auto ids = active_scrolls_;
    Color tint = color;
    if (IsDisabled())
        tint.a *= .4f;
    auto command = [this, texture, src, dst, tint, clips, ids]() {
        ApplyLayoutClip(clips);
        Rect r = dst;
        r.y -= ScrollTranslation(ids);
        renderer_->DrawSprite(*texture, src, MapRect(r), tint);
    };
    if (scrolls_.empty() && deferred_ == 0)
        command();
    else
        pending_draws_.push_back(std::move(command));
}
void UISystem::Image(const Texture2D *texture, const Rect &src, float height, const Color &tint) {
    if (boxes_.empty())
        return;
    const Rect r = Allocate(height, "image");
    Sprite(texture, src, r, tint);
}
bool UISystem::ImageButton(std::string_view key, const Texture2D *texture, const Rect &src,
                           float height, std::string_view label) {
    if (boxes_.empty())
        return false;
    const bool click = Button(key, height, label);
    const Rect r = last_rect_;
    const float size = std::max(0.0f, std::min(height - 8, r.width - 8));
    Sprite(texture, src, {r.x + 4, r.y + 4, size, size}, Color::White());
    return click;
}
void UISystem::OpenModal(std::string_view key) {
    if (modal_ != key) {
        modal_ = key;
        nav_focus_.clear();
        focused_.clear();
        focus_ring_.clear();
        prior_focus_.clear();
    }
}
void UISystem::CloseModal() {
    modal_.clear();
    nav_focus_.clear();
    focused_.clear();
}
bool UISystem::ModalBegin(std::string_view key, float width, float height) {
    if (modal_ != key || !camera_)
        return false;
    if (input_ && (input_->KeyPressed("escape") || input_->GamepadButtonPressed(0, "b"))) {
        CloseModal();
        return false;
    }
    in_modal_ = true;
    overlay_scrolls_.push_back(active_scrolls_);
    active_scrolls_.clear();
    const float vw = camera_->ViewportWidth() / scale_, vh = camera_->ViewportHeight() / scale_;
    // Overlay is intentionally outside the host clip, but its content has a fresh clip.
    LayoutBox box;
    box.content = {-scale_offset_.x / scale_, -scale_offset_.y / scale_, vw, vh};
    boxes_.push_back(box);
    paint_bounds_ = box.content;
    Quad(box.content, {0, 0, 0, .55f});
    Push((vw - width) * .5f, (vh - height) * .5f, width, height, ThemeMetric("padding"));
    ThemedPanel();
    return true;
}
void UISystem::ModalEnd() {
    if (!in_modal_)
        return;
    Pop();
    if (!boxes_.empty())
        boxes_.pop_back();
    in_modal_ = false;
    if (!overlay_scrolls_.empty()) {
        active_scrolls_ = std::move(overlay_scrolls_.back());
        overlay_scrolls_.pop_back();
    }
    ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
}
void UISystem::SetScrollOffset(std::string_view key, float offset) {
    if (std::isfinite(offset))
        scroll_states_[std::string(key)].offset = std::max(0.0f, offset);
}
void UISystem::OpenPopup(std::string_view key, float x, float y) {
    popup_ = key;
    popup_pos_ = {x, y};
    popup_index_ = 0;
    popup_just_opened_ = true;
    nav_focus_.clear();
    focused_.clear();
}
bool UISystem::ContextMenu(std::string_view key) {
    if (input_ && !IsDisabled() && (modal_.empty() || in_modal_) && last_rect_.Contains(Mouse()) &&
        MouseVisible() && input_->MousePressed(1)) {
        const Vec2 m = Mouse();
        OpenPopup(key, m.x, m.y);
    }
    return popup_ == key;
}
int UISystem::Popup(std::string_view key, const std::vector<std::string> &items, float width,
                    float item_h) {
    if (popup_ != key || !camera_ || !input_)
        return -1;
    in_popup_ = true;
    item_h = std::max(1.0f, item_h);
    const float height = items.size() * std::max(1.0f, item_h);
    Rect r{popup_pos_.x, popup_pos_.y, std::max(1.0f, width), height};
    r.x = std::clamp(r.x, 0.0f, std::max(0.0f, camera_->ViewportWidth() / scale_ - r.width));
    r.y = std::clamp(r.y, 0.0f, std::max(0.0f, camera_->ViewportHeight() / scale_ - r.height));
    if (input_->KeyPressed("escape") || input_->GamepadButtonPressed(0, "b") ||
        (input_->MousePressed(0) && !r.Contains(RawMouse()))) {
        popup_.clear();
        in_popup_ = false;
        return -1;
    }
    if (!items.empty()) {
        const std::string prefix = std::string(key) + "/";
        if (input_->KeyPressed("tab") && nav_focus_.find(prefix) == 0)
            popup_index_ = std::atoi(nav_focus_.substr(prefix.size()).c_str());
        if (input_->KeyPressed("down") || input_->GamepadButtonPressed(0, "dpad_down"))
            ++popup_index_;
        if (input_->KeyPressed("up") || input_->GamepadButtonPressed(0, "dpad_up"))
            --popup_index_;
        popup_index_ =
            (popup_index_ + static_cast<int>(items.size())) % static_cast<int>(items.size());
        if (r.Contains(RawMouse()) && input_->MousePressed(0))
            popup_index_ =
                std::clamp(static_cast<int>((RawMouse().y - r.y) / std::max(1.0f, item_h)), 0,
                           static_cast<int>(items.size()) - 1);
        nav_focus_ = prefix + std::to_string(popup_index_);
    }
    const bool saved_activation = nav_activate_;
    if (popup_just_opened_)
        nav_activate_ = false;
    overlay_scrolls_.push_back(active_scrolls_);
    active_scrolls_.clear();
    LayoutBox host;
    host.content = r;
    boxes_.push_back(host);
    PushAbsolute(r, 0, 0);
    ThemedPanel();
    int chosen = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (Button(std::string(key) + "/" + std::to_string(i), item_h, items[i]))
            chosen = static_cast<int>(i);
    }
    nav_activate_ = saved_activation;
    popup_just_opened_ = false;
    Pop();
    boxes_.pop_back();
    in_popup_ = false;
    active_scrolls_ = std::move(overlay_scrolls_.back());
    overlay_scrolls_.pop_back();
    if (chosen >= 0) {
        popup_.clear();
        nav_focus_.clear();
    }
    ApplyLayoutClip(boxes_.empty() ? std::vector<LayoutClip>{} : boxes_.back().clips);
    return chosen;
}
UISystem::ListRange UISystem::ListBegin(std::string_view key, float view_h, int count,
                                        float item_h) {
    if (boxes_.empty())
        return {};
    count = std::max(0, count);
    item_h = std::max(1.0f, item_h);
    // Fixed extent is known before drawing: resolve wheel/drag first, then
    // request exactly the newly visible window, never all rows.
    auto &state = scroll_states_[std::string(key)];
    state.height = count * item_h;
    const Rect view{boxes_.back().content.x, boxes_.back().content.y + boxes_.back().cursor,
                    AvailWidth(), std::max(0.0f, view_h)};
    paint_bounds_ = view;
    if (input_ && renderer_ && camera_)
        state.offset = PaintScrollArea(key, view.x, view.y, view.width, view.height, state.height,
                                       state.offset,
                                       CanInteract() && view.Contains(Mouse()) && MouseVisible());
    state.offset = std::clamp(state.offset, 0.0f, std::max(0.0f, state.height - view.height));
    const int first = std::min(count, static_cast<int>(state.offset / item_h));
    const int end =
        std::min(count, static_cast<int>(std::ceil((state.offset + view.height) / item_h)));
    ScrollBegin(key, view_h);
    boxes_.back().cursor = first * item_h;
    lists_.push_back({count, item_h});
    // ListBegin already consumed scrolling this frame.
    wheel_depth_ = boxes_.size() + 1;
    list_count_ = std::max(0, end - first);
    return {first, list_count_};
}
void UISystem::ListEnd() {
    if (lists_.empty() || boxes_.empty())
        return;
    boxes_.back().cursor = lists_.back().count * lists_.back().item_h;
    lists_.pop_back();
    ScrollEnd();
}
float UISystem::Tween(std::string_view key, float target, float seconds, float dt,
                      std::string_view easing) {
    auto [it, inserted] = tweens_.try_emplace(std::string(key));
    auto &s = it->second;
    if (inserted) {
        s.value = s.from = s.target = target;
        return target;
    }
    if (target != s.target) {
        s.from = s.value;
        s.target = target;
        s.elapsed = 0;
    }
    s.elapsed += std::max(0.0f, dt);
    float t = seconds <= 0 ? 1 : std::clamp(s.elapsed / seconds, 0.0f, 1.0f);
    if (easing == "smooth")
        t = t * t * (3 - 2 * t);
    else if (easing == "in")
        t *= t;
    else if (easing == "out")
        t = 1 - (1 - t) * (1 - t);
    else
        t = ease::ByName(easing, t);
    s.value = s.from + (s.target - s.from) * t;
    return s.value;
}
void UISystem::SetPage(std::string_view name, int page) { pages_[std::string(name)] = page; }
int UISystem::Page(std::string_view name) const {
    auto it = pages_.find(std::string(name));
    return it == pages_.end() ? 0 : it->second;
}
bool UISystem::PageBegin(std::string_view name, int page) const { return Page(name) == page; }

namespace {
std::size_t NextChar(std::string_view text, std::size_t i) {
    if (i >= text.size())
        return text.size();
    ++i;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
        ++i;
    return i;
}
std::size_t PrevChar(std::string_view text, std::size_t i) {
    if (i == 0)
        return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
        --i;
    return i;
}
} // namespace
bool UISystem::TextArea(std::string_view key, float height, std::size_t max_chars) {
    if (boxes_.empty())
        return false;
    return EditText(key, Allocate(height, "text_area"), max_chars, true);
}
bool UISystem::EditText(std::string_view key, const Rect &r, std::size_t max_chars,
                        bool multiline) {
    if (!renderer_ || !input_)
        return false;
    auto &s = widgets_[std::string(key)];
    s.caret = std::min(s.caret, s.text.size());
    s.anchor = std::min(s.anchor, s.text.size());
    FocusWidget(key, r, true);
    const bool hover = MouseVisible() && r.Contains(Mouse());
    if (hover && input_->MouseReleased(0) && s.pressed) {
        focused_ = key;
        nav_focus_ = key;
    }
    if (hover && input_->MousePressed(0)) {
        focused_ = key;
        nav_focus_ = key;
    }
    s.pressed = hover && CanInteract() && input_->MouseDown(0);
    s.hover = hover;
    const bool focused = CanInteract() && focused_ == key;
    const float scale = ThemeMetric("font_scale"), lh = renderer_->GetFont().LineHeight() * scale;
    const float width = std::max(1.0f, r.width - 12);
    struct Glyph {
        std::size_t start, end;
        float x, y, w;
    };
    auto layout = [&]() {
        std::vector<Glyph> glyphs;
        float x = 0, y = 0;
        for (std::size_t i = 0; i < s.text.size();) {
            const auto next = NextChar(s.text, i);
            const auto ch = std::string_view(s.text).substr(i, next - i);
            const float w = ch == "\n" ? 0 : renderer_->MeasureText(ch, scale);
            if (multiline && x > 0 && x + w > width) {
                x = 0;
                y += lh;
            }
            glyphs.push_back({i, next, x, y, w});
            if (ch == "\n" && multiline) {
                x = 0;
                y += lh;
            } else
                x += w;
            i = next;
        }
        glyphs.push_back({s.text.size(), s.text.size(), x, y, 0});
        return glyphs;
    };
    auto glyphs = layout();
    auto caret_pos = [&](std::size_t index) -> Vec2 {
        for (const auto &g : glyphs)
            if (g.start == index)
                return {g.x, g.y};
        return {};
    };
    const bool shift = input_->KeyDown("left_shift") || input_->KeyDown("right_shift");
    const bool ctrl = input_->KeyDown("left_control") || input_->KeyDown("right_control") ||
                      input_->KeyDown("left_super") || input_->KeyDown("right_super");
    bool changed = false;
    auto erase_selection = [&]() {
        const auto a = std::min(s.anchor, s.caret), b = std::max(s.anchor, s.caret);
        if (a != b) {
            s.text.erase(a, b - a);
            s.caret = s.anchor = a;
            changed = true;
            return true;
        }
        return false;
    };
    auto insert = [&](std::string text) {
        erase_selection();
        std::string accepted;
        std::size_t length = Utf8Length(s.text);
        for (std::size_t i = 0; i < text.size();) {
            auto next = NextChar(text, i);
            if (max_chars && length >= max_chars)
                break;
            if (static_cast<unsigned char>(text[i]) >= 32 || (multiline && text[i] == '\n')) {
                accepted.append(text, i, next - i);
                ++length;
            }
            i = next;
        }
        if (!accepted.empty()) {
            s.text.insert(s.caret, accepted);
            s.caret += accepted.size();
            s.anchor = s.caret;
            changed = true;
        }
    };
    auto move = [&](std::size_t index) {
        s.caret = index;
        if (!shift)
            s.anchor = index;
    };
    if (focused) {
        if (ctrl && input_->KeyPressed("a")) {
            s.anchor = 0;
            s.caret = s.text.size();
        }
        if (ctrl && (input_->KeyPressed("c") || input_->KeyPressed("x"))) {
            const auto a = std::min(s.anchor, s.caret), b = std::max(s.anchor, s.caret);
            if (a != b)
                input_->SetClipboard(std::string_view(s.text).substr(a, b - a));
            if (input_->KeyPressed("x"))
                erase_selection();
        }
        if (ctrl && input_->KeyPressed("v"))
            insert(input_->Clipboard());
        if (!ctrl)
            for (unsigned int cp : input_->TextCodepoints())
                if (cp >= 32 && cp != 127) {
                    std::string utf;
                    AppendUtf8(utf, cp);
                    insert(utf);
                }
        if (input_->KeyPressed("backspace") && !erase_selection() && s.caret > 0) {
            auto prev = PrevChar(s.text, s.caret);
            s.text.erase(prev, s.caret - prev);
            s.caret = s.anchor = prev;
            changed = true;
        }
        if (input_->KeyPressed("delete") && !erase_selection() && s.caret < s.text.size()) {
            s.text.erase(s.caret, NextChar(s.text, s.caret) - s.caret);
            changed = true;
        }
        glyphs = layout();
        if (input_->KeyPressed("left"))
            move(!shift && s.anchor != s.caret ? std::min(s.anchor, s.caret)
                                               : PrevChar(s.text, s.caret));
        if (input_->KeyPressed("right"))
            move(!shift && s.anchor != s.caret ? std::max(s.anchor, s.caret)
                                               : NextChar(s.text, s.caret));
        Vec2 caret = caret_pos(s.caret);
        if (input_->KeyPressed("home")) {
            std::size_t index = 0;
            if (!ctrl)
                for (const auto &g : glyphs)
                    if (g.y == caret.y) {
                        index = g.start;
                        break;
                    }
            move(index);
        }
        if (input_->KeyPressed("end")) {
            std::size_t index = s.text.size();
            if (!ctrl)
                for (const auto &g : glyphs)
                    if (g.y == caret.y)
                        index = g.start;
            move(index);
        }
        if (multiline && (input_->KeyPressed("up") || input_->KeyPressed("down"))) {
            const float target = caret.y + (input_->KeyPressed("up") ? -lh : lh);
            float distance = 1e20f;
            std::size_t index = s.caret;
            for (const auto &g : glyphs) {
                float d = std::abs(g.y - target) * 1000 + std::abs(g.x - caret.x);
                if (d < distance) {
                    distance = d;
                    index = g.start;
                }
            }
            move(index);
        }
        if (input_->KeyPressed("enter")) {
            if (multiline)
                insert("\n");
            else
                focused_.clear();
        }
        if (input_->KeyPressed("escape"))
            focused_.clear();
    }
    glyphs = layout();
    const Vec2 caret = caret_pos(s.caret);
    const float scroll_x = multiline ? 0 : std::max(0.0f, caret.x - width + 2);
    const float scroll_y =
        multiline ? std::max(0.0f, caret.y + lh - std::max(lh, r.height - 12)) : 0;
    if (hover && input_->MousePressed(0)) {
        const Vec2 m = Mouse();
        float distance = 1e20f;
        std::size_t index = s.text.size();
        for (const auto &g : glyphs) {
            float d = std::abs(g.y - (m.y - r.y - 6 + scroll_y)) * 1000 +
                      std::abs(g.x - (m.x - r.x - 6 + scroll_x));
            if (d < distance) {
                distance = d;
                index = g.start;
            }
        }
        s.caret = index;
        if (!shift)
            s.anchor = index;
    }
    Quad(r, ThemeColor("border"));
    Quad({r.x + 2, r.y + 2, std::max(0.0f, r.width - 4), std::max(0.0f, r.height - 4)},
         ThemeColor("field"));
    // Per-glyph selection follows exactly the same wrapped geometry as text.
    const auto lo = std::min(s.caret, s.anchor), hi = std::max(s.caret, s.anchor);
    for (const auto &g : glyphs) {
        if (g.start == g.end)
            break;
        const float x = r.x + 6 + g.x - scroll_x, y = r.y + 6 + g.y - scroll_y;
        if (g.start >= lo && g.start < hi)
            Quad({x, y, std::max(3.0f, g.w), lh}, ThemeColor("accent"));
        if (s.text[g.start] != '\n')
            String(std::string_view(s.text).substr(g.start, g.end - g.start), x, y, scale,
                   ThemeColor("text"));
    }
    if (s.text.empty() && !focused)
        DrawFittedText("click to type", r.x + 6, r.y + 6, width, scale, {.45f, .48f, .55f, 1});
    if (focused) {
        const Vec2 c = caret_pos(s.caret);
        Quad({r.x + 6 + c.x - scroll_x, r.y + 6 + c.y - scroll_y, 1.5f, lh}, ThemeColor("text"));
    }
    return changed;
}
void UISystem::SetRichIcon(std::string_view name, const Texture2D *texture, const Rect &src) {
    rich_icons_[std::string(name)] = {texture, src, 0, 0};
}
std::string UISystem::RichText(std::string_view markup, float scale) {
    if (boxes_.empty() || !renderer_)
        return {};
    if (scale <= 0)
        scale = ThemeMetric("font_scale");
    const float lh = renderer_->GetFont().LineHeight() * scale, width = AvailWidth();
    if (width <= 0) {
        last_rect_ = {boxes_.back().content.x, boxes_.back().content.y + boxes_.back().cursor, 0,
                      0};
        return {};
    }
    const float ox = boxes_.back().content.x, oy = boxes_.back().content.y + boxes_.back().cursor;
    float x = 0, y = 0;
    bool bold = false;
    std::string link, clicked;
    Color color = ThemeColor("text");
    std::vector<Color> colors;
    paint_bounds_ = {ox, oy, width, boxes_.back().content.height};
    for (std::size_t i = 0; i < markup.size();) {
        bool image = false;
        Slice icon;
        float w = 0;
        std::string_view ch;
        if (markup[i] == '[') {
            const auto end = markup.find(']', i);
            if (end != std::string_view::npos) {
                const auto tag = markup.substr(i + 1, end - i - 1);
                bool consumed = true;
                if (tag == "b")
                    bold = true;
                else if (tag == "/b")
                    bold = false;
                else if (tag == "/color") {
                    if (!colors.empty()) {
                        color = colors.back();
                        colors.pop_back();
                    }
                } else if (tag.substr(0, 7) == "color=#" &&
                           (tag.size() == 13 || tag.size() == 15)) {
                    const auto hex = std::string(tag.substr(7));
                    char *stop = nullptr;
                    const auto value = std::strtoul(hex.c_str(), &stop, 16);
                    if (stop && *stop == 0) {
                        colors.push_back(color);
                        if (hex.size() == 6)
                            color = {((value >> 16) & 255) / 255.0f, ((value >> 8) & 255) / 255.0f,
                                     (value & 255) / 255.0f, 1};
                        else
                            color = {((value >> 24) & 255) / 255.0f, ((value >> 16) & 255) / 255.0f,
                                     ((value >> 8) & 255) / 255.0f, (value & 255) / 255.0f};
                    }
                } else if (tag.substr(0, 5) == "link=")
                    link = tag.substr(5);
                else if (tag == "/link")
                    link.clear();
                else if (tag.substr(0, 4) == "img=") {
                    auto it = rich_icons_.find(std::string(tag.substr(4)));
                    if (it != rich_icons_.end()) {
                        image = true;
                        icon = it->second;
                        w = lh;
                    }
                } else
                    consumed = false;
                if (consumed) {
                    i = end + 1;
                    if (!image)
                        continue;
                }
            }
        }
        if (!image) {
            auto next = NextChar(markup, i);
            ch = markup.substr(i, next - i);
            i = next;
            if (ch == "\n") {
                x = 0;
                y += lh;
                continue;
            }
            w = renderer_->MeasureText(ch, scale);
        }
        if (x > 0 && x + w > width) {
            x = 0;
            y += lh;
        }
        const Rect r{ox + x, oy + y, std::min(w, width), lh};
        if (!link.empty()) {
            const std::string key = "rich/" + link + "/" + std::to_string(i);
            const bool activate = FocusWidget(key, r);
            auto &state = widgets_[key];
            const bool hover = MouseVisible() && r.Contains(Mouse());
            if (activate || (hover && state.pressed && input_ && input_->MouseReleased(0)))
                clicked = link;
            state.pressed = hover && input_ && CanInteract() && input_->MouseDown(0);
            Quad({r.x, r.y + lh - 2, r.width, 1}, color);
        }
        if (image)
            Sprite(icon.texture, icon.source, r, color);
        else {
            String(ch, r.x, r.y, scale, color);
            if (bold)
                String(ch, r.x + .7f, r.y, scale, color);
        }
        x += w;
    }
    const float h = markup.empty() ? 0 : y + lh;
    boxes_.back().cursor += h;
    last_rect_ = {ox, oy, width, h};
    Record("rich text", last_rect_);
    return clicked;
}

} // namespace fake2d
