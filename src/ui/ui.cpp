#include "fake2d/ui.h"
#include "fake2d/ease.h"
#include <cstdlib>
#include <cctype>
#include <filesystem>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace fake2d {

namespace {

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

Rect RectIntersectHelper(const Rect &a, const Rect &b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

bool TryLoadFontPathHelper(Renderer &r, const std::string &path, float px) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
        return false;
    }
    return r.LoadFont(path, px);
}

template <typename Entry>
void AdvanceRing(const std::vector<Entry> &ring, int step, std::string &nav_focus,
                 std::string &focused) {
    if (!step || ring.empty()) {
        return;
    }
    std::vector<std::size_t> scoped;
    std::string trap_scope;
    for (const auto &e : ring) {
        if (!e.scope_id.empty()) {
            trap_scope = e.scope_id;
        }
    }
    if (!trap_scope.empty()) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            if (ring[i].scope_id == trap_scope) {
                scoped.push_back(i);
            }
        }
    }
    if (scoped.empty()) {
        int index = step > 0 ? -1 : 0;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            if (ring[i].key == nav_focus) {
                index = static_cast<int>(i);
                break;
            }
        }
        index = (index + step + static_cast<int>(ring.size())) % static_cast<int>(ring.size());
        nav_focus = ring[static_cast<std::size_t>(index)].key;
        focused = ring[static_cast<std::size_t>(index)].text ? nav_focus : "";
        return;
    }
    int pos = step > 0 ? -1 : 0;
    for (std::size_t i = 0; i < scoped.size(); ++i) {
        if (ring[scoped[i]].key == nav_focus) {
            pos = static_cast<int>(i);
            break;
        }
    }
    pos = (pos + step + static_cast<int>(scoped.size())) % static_cast<int>(scoped.size());
    const auto &picked = ring[scoped[static_cast<std::size_t>(pos)]];
    nav_focus = picked.key;
    focused = picked.text ? nav_focus : "";
}

} // namespace

void UISystem::Bind(const Input *input, const Camera2D *camera, Renderer *renderer) {
    input_ = input;
    camera_ = camera;
    if (renderer_ != renderer)
        rich_advances_.clear();
    renderer_ = renderer;
}

void UISystem::NewFrame() {
    const float raw_dt = clock_seconds_ - last_frame_clock_;
    frame_dt_ = (raw_dt > 0.0001f && raw_dt < 0.25f) ? raw_dt : (1.0f / 60.0f);
    last_frame_clock_ = clock_seconds_;
    ResetAutoLayoutFrame();
    recording_ = resolving_ = frame_submitted_ = false;
    dismissed_overlay_ = false;
    resolving_allowed_ = true;
    pointer_over_prev_ = pointer_over_;
    pointer_over_ = false;
    manual_modal_ = false;
    manual_modal_layer_ = 0;
    modal_layer_seq_ = 0;
    suppress_ = false;
    box_sequence_ = 1;
    align_stack_.clear();
    align_groups_.clear();
    id_stack_.clear();
    dir_stack_.clear();
    focus_scopes_.clear();
    active_focus_scope_keys_.clear();
    virtual_stack_.clear();
    pending_drag_sources_.clear();
    pending_drop_targets_.clear();
    drop_hovered_key_.clear();
    drop_results_.clear();
    menu_stack_.clear();
    menu_bar_active_ = false;
    desired_cursor_shape_ = 0;
    ime_cursor_rect_ = {};
    for (auto &kv : scroll_states_) {
        auto &sc = kv.second;
        if (sc.has_target) {
            const float alpha = std::min(1.0f, frame_dt_ * 14.0f);
            sc.offset += (sc.target_offset - sc.offset) * alpha;
            if (std::abs(sc.target_offset - sc.offset) < 0.5f) {
                sc.offset = sc.target_offset;
                sc.has_target = false;
            }
        }
        if (sc.has_target_x) {
            const float alpha = std::min(1.0f, frame_dt_ * 14.0f);
            sc.offset_x += (sc.target_offset_x - sc.offset_x) * alpha;
            if (std::abs(sc.target_offset_x - sc.offset_x) < 0.5f) {
                sc.offset_x = sc.target_offset_x;
                sc.has_target_x = false;
            }
        }
    }
    text_activated_.clear();
    list_views_.clear();
    active_list_view_.clear();
    layout_resolved_ = false;
    variable_scopes_.clear();
    frame_results_.clear();
    widget_rects_.clear();
    clicked_link_.clear();
    activated_widget_.clear();
    frame_stats_ = {};
    // Widget state persists (hover/pressed); tooltip records are per frame.
    tooltip_.clear();
    saved_nav_focus_ = nav_focus_;
    saved_text_focus_ = focused_;
    saved_stick_ = stick_held_;
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
            AdvanceRing(prior_focus_, step, nav_focus_, focused_);
        }
    }
    next_limits_ = {};
    deferred_ = 0;
    disabled_.clear();
    lists_.clear();
    prev_named_rects_ = std::move(named_rects_);
    named_rects_.clear();
    in_modal_ = in_popup_ = false;
    if (renderer_)
        renderer_->ClearClipRect();
    if (reference_w_ > 0)
        SetScale(reference_w_, reference_h_, scale_mode_);
    audit_.clear();
    last_rect_ = {};
}

UISystem::WidgetState *UISystem::Find(std::string_view key) {
    const auto it = widgets_.find(std::string(key));
    return it != widgets_.end() ? &it->second : nullptr;
}

bool UISystem::MouseVisible() const {
    return CanInteract() &&
           (!resolving_ || resolving_key_ == pointer_owner_ || resolving_key_ == pointer_active_);
}

Vec2 UISystem::Mouse() const {
    if (!MouseVisible())
        return {-1e20f, -1e20f};
    return RawMouse();
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
    renderer_->ClearClipRect();
}

bool UISystem::TrackPressWidget(WidgetState &state, bool hover) {
    const bool mouse_down = CanInteract() && input_->MouseDown(0);
    const bool mouse_released = input_->MouseReleased(0);
    bool clicked = false;
    if (hover) {
        pointer_over_ = true;
        if (state.pressed && mouse_released) {
            clicked = true;
            ++click_count_;
        }
        state.pressed = (mouse_down && (!resolving_ || input_->MousePressed(0))) ||
                        (state.pressed && !mouse_released && hover);
    } else {
        state.pressed = false;
    }
    state.hover = hover;
    return clicked;
}

std::string_view UISystem::TextValue(std::string_view key) const {
    if (const WidgetState *state = const_cast<UISystem *>(this)->Find(ResolveKeyLookup(key))) {
        return state->text;
    }
    return {};
}

void UISystem::SetTextValue(std::string_view key, std::string_view text) {
    WidgetState &state = widgets_[ScopedKey(key)];
    state.text = std::string(text);
    state.caret = state.anchor = state.text.size();
    state.undo.clear();
    state.redo.clear();
    state.last_click = -1;
    state.selecting = false;
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
    if (max_scroll > 0.0f && !scroll_input_only_) {
        renderer_->DrawQuad(MapRect(bar), ThemeColor("track"));
        const float drawn = p.y + (next / max_scroll) * thumb_track;
        const Color thumb_col = state.dragging ? ThemeColor("accent")
                              : hover          ? ThemeColor("hover")
                                               : Color{0.22f, 0.30f, 0.48f, 1.0f};
        renderer_->DrawQuad(MapRect({bar.x + 1.0f, drawn, bar_w - 2.0f, thumb_h}), thumb_col);
    }
    return next;
}

float UISystem::DrawFittedText(std::string_view text, float x, float y, float w, float scale,
                               const Color &tint, bool tail) {
    if (!renderer_ || w <= 0.0f)
        return x;
    std::string shown;
    if (!tail)
        shown = Ellipsize(text, w, scale);
    else {
        std::size_t first = 0;
        while (first < text.size() && renderer_->MeasureText(text.substr(first), scale) > w) {
            ++first;
            while (first < text.size() && (static_cast<unsigned char>(text[first]) & 0xC0u) == 0x80u)
                ++first;
        }
        shown.assign(text.substr(first));
    }
    if (shown.empty())
        return x;
    Color col = tint;
    if (IsDisabled())
        col.a *= 0.4f;
    renderer_->DrawText(shown, x * scale_ + scale_offset_.x, y * scale_ + scale_offset_.y,
                        scale * scale_, col, true);
    return x + renderer_->MeasureText(shown, scale, true);
}

bool UISystem::IsHovered(std::string_view key) const {
    if (const WidgetState *state = const_cast<UISystem *>(this)->Find(ResolveKeyLookup(key))) {
        return state->hover;
    }
    return false;
}

void UISystem::Record(std::string_view kind, const Rect &r, std::string_view name) {
    if (resolving_ && kind != "tooltip") {
        return;
    }
    const Rect bounds = {-scale_offset_.x / scale_, -scale_offset_.y / scale_,
                         camera_ ? camera_->ViewportWidth() / scale_ : 0.0f,
                         camera_ ? camera_->ViewportHeight() / scale_ : 0.0f};
    constexpr float epsilon = 0.01f;
    const bool overflow = r.x < bounds.x - epsilon || r.y < bounds.y - epsilon ||
                          r.x + r.width > bounds.x + bounds.width + epsilon ||
                          r.y + r.height > bounds.y + bounds.height + epsilon;
    AuditRect entry;
    entry.kind = std::string(kind);
    entry.rect = r;
    entry.bounds = bounds;
    entry.overflow = overflow;
    entry.name = std::string(name);
    entry.clip_rect = bounds;
    entry.layer = manual_modal_ ? manual_modal_layer_ : ((!modal_.empty() || !popup_.empty()) ? 1 : 0);
    entry.suppressed = suppress_;
    entry.disabled = IsDisabled();
    audit_.push_back(std::move(entry));
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

void UISystem::Panel(const Color &fill, const Color &border) {
    if (!InAutoLayout())
        return;
    auto &n = auto_nodes_[static_cast<std::size_t>(auto_stack_.back())];
    n.draw_panel = true;
    n.themed_panel = false;
    n.fill_col = fill;
    n.border_col = border;
}

void UISystem::DrawNineSliceAt(const Texture2D *texture, float corner, const Color &tint) {
    if (!texture || !texture->IsValid() || !InAutoLayout())
        return;
    AutoCustomCmd cmd;
    cmd.op = AutoCustomCmd::Op::NineSlice;
    cmd.texture = texture;
    cmd.src = {0.0f, 0.0f, static_cast<float>(texture->Width()),
               static_cast<float>(texture->Height())};
    cmd.fill = tint;
    cmd.corner_x = corner;
    cmd.corner_y = corner;
    auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
}

void UISystem::DrawSpriteAt(const Texture2D *texture, float x, float y, float w, float h,
                            const Color &tint) {
    if (!texture || !texture->IsValid() || w <= 0 || h <= 0)
        return;
    const Rect src{0.0f, 0.0f, static_cast<float>(texture->Width()),
                   static_cast<float>(texture->Height())};
    if (!InAutoLayout()) {
        DrawSpriteRect(texture, src, {x, y, w, h}, tint);
        return;
    }
    Color col = tint;
    if (IsDisabled())
        col.a *= .4f;
    AutoCustomCmd cmd;
    cmd.op = AutoCustomCmd::Op::Sprite;
    cmd.rect = {x, y, w, h};
    cmd.src = src;
    cmd.texture = texture;
    cmd.fill = col;
    auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
}

void UISystem::FillRect(float x, float y, float w, float h, const Color &fill, const Color &border) {
    if (w <= 0 || h <= 0)
        return;
    if (!InAutoLayout()) {
        DrawQuadAt({x, y, w, h}, fill);
        if (border.a > 0.0f)
            DrawFrameAt({x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f}, border, 1.0f);
        return;
    }
    AutoCustomCmd cmd;
    cmd.op = AutoCustomCmd::Op::Quad;
    cmd.rect = {x, y, w, h};
    cmd.fill = fill;
    cmd.border = border;
    auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
}
void UISystem::Tip(std::string_view text) {
    if (!InAutoLayout())
        return;
    if (last_auto_node_ >= 0 && static_cast<std::size_t>(last_auto_node_) < auto_nodes_.size()) {
        auto_nodes_[static_cast<std::size_t>(last_auto_node_)].tip_text = std::string(text);
    }
}

// Layout constraints (applicable cleanly in Two-Pass before containers/widgets).
void UISystem::Constrain(float min_w, float max_w, float min_h, float max_h) {
    if (!std::isfinite(min_w) || !std::isfinite(max_w) || !std::isfinite(min_h) ||
        !std::isfinite(max_h))
        return;
    min_w = std::max(0.0f, min_w);
    min_h = std::max(0.0f, min_h);
    next_limits_ = {min_w, std::max(min_w, max_w), min_h, std::max(min_h, max_h)};
}
bool UISystem::SetScale(float w, float h, std::string_view mode) {
    if (!auto_stack_.empty() || !camera_ || !std::isfinite(w) || !std::isfinite(h))
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
void UISystem::Remember(std::string_view name) {
    if (InAutoLayout() && last_auto_node_ >= 0 &&
        static_cast<std::size_t>(last_auto_node_) < auto_nodes_.size()) {
        auto_nodes_[static_cast<std::size_t>(last_auto_node_)].remember_name = std::string(name);
    }
}
void UISystem::RememberBox(std::string_view name) {
    if (InAutoLayout() && !auto_stack_.empty()) {
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].remember_name = std::string(name);
        return;
    }
    named_rects_[std::string(name)] = {};
}
Rect UISystem::NamedRect(std::string_view name) const {
    auto it = named_rects_.find(std::string(name));
    if (it != named_rects_.end()) {
        return it->second;
    }
    auto prev_it = prev_named_rects_.find(std::string(name));
    return prev_it == prev_named_rects_.end() ? Rect{} : prev_it->second;
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
void UISystem::ThemedPanel() {
    if (!InAutoLayout())
        return;
    auto &n = auto_nodes_[static_cast<std::size_t>(auto_stack_.back())];
    n.draw_panel = true;
    n.themed_panel = true;
}
void UISystem::DisabledBegin(bool disabled) { disabled_.push_back(IsDisabled() || disabled); }
void UISystem::DisabledEnd() {
    if (!disabled_.empty())
        disabled_.pop_back();
}
bool UISystem::CanInteract() const {
    return !suppress_ && !(recording_ && dismissed_overlay_) &&
           (!resolving_ || resolving_allowed_) && !IsDisabled() &&
           (modal_.empty() || in_modal_) && (popup_.empty() || in_popup_);
}
bool UISystem::FocusWidget(std::string_view key, const Rect &r, bool text, bool adjustable) {
    (void)adjustable;
    if (!CanInteract() || !input_) {
        if (nav_focus_ == key)
            nav_focus_.clear();
        if (focused_ == key)
            focused_.clear();
        return false;
    }
    if (MouseVisible() && r.Contains(Mouse()) && input_->MousePressed(0)) {
        nav_focus_ = key;
        if (!text)
            focused_.clear();
    }
    return nav_focus_ == key && nav_activate_ && !text;
}
void UISystem::FocusIndicator(std::string_view key, const Rect &r) {
    if (!renderer_ || !CanInteract() || nav_focus_ != key)
        return;
    const Color accent = ThemeColor("accent");
    renderer_->DrawQuad(MapRect({r.x, r.y, r.width, 2.0f}), accent);
    renderer_->DrawQuad(MapRect({r.x, r.y + r.height - 2.0f, r.width, 2.0f}), accent);
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
void UISystem::ModalEnd() {
    if (!in_modal_)
        return;
    RootEnd();
    in_modal_ = false;
}
void UISystem::SetScrollOffset(std::string_view key, float offset) {
    if (std::isfinite(offset)) {
        scroll_states_[std::string(key)].offset = std::max(0.0f, offset);
        auto it = variable_lists_.find(std::string(key));
        if (it != variable_lists_.end())
            it->second.anchor_pending = false;
    }
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
    if (InAutoLayout()) {
        if (last_auto_node_ >= 0 && static_cast<std::size_t>(last_auto_node_) < auto_nodes_.size()) {
            auto_nodes_[static_cast<std::size_t>(last_auto_node_)].context_menu_key = std::string(key);
        }
    }
    return popup_ == key;
}
int UISystem::Popup(std::string_view key, const std::vector<std::string> &items, float width,
                    float item_h) {
    if (popup_ != key || !camera_ || !input_)
        return -1;
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : (camera_->ViewportWidth() / std::max(1e-6f, scale_));
    const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : (camera_->ViewportHeight() / std::max(1e-6f, scale_));
    const float eff_item_h =
        item_h > 0.0f ? item_h : (FontLineHeightPx(0.0f) + 2.0f * ThemeMetric("button_pad_y") + 4.0f);
    const std::string popup_mem_key = std::string("@popup/") + std::string(key);
    float est_h = std::max(1.0f, static_cast<float>(items.size()) * eff_item_h);
    Rect r{popup_pos_.x, popup_pos_.y, std::max(1.0f, width), est_h};
    if (auto mem = named_rects_.find(popup_mem_key); mem != named_rects_.end()) {
        r.width = mem->second.width;
        r.height = mem->second.height;
    }
    r.x = std::clamp(r.x, 0.0f, std::max(0.0f, vw - r.width));
    r.y = std::clamp(r.y, 0.0f, std::max(0.0f, vh - r.height));
    const float ox = -scale_offset_.x / std::max(1e-6f, scale_);
    const float oy = -scale_offset_.y / std::max(1e-6f, scale_);
    const Rect screen_r{ox + r.x, oy + r.y, r.width, r.height};
    if (input_->KeyPressed("escape") || input_->GamepadButtonPressed(0, "b") ||
        (input_->MousePressed(0) && !screen_r.Contains(RawMouse()))) {
        if (recording_)
            dismissed_overlay_ = true;
        popup_.clear();
        return -1;
    }
    if (!items.empty()) {
        const std::string prefix = std::string(key) + "/";
        if (recording_) {
            if (nav_focus_.find(prefix) != 0)
                nav_focus_ = prefix + "0";
            frame_results_[std::string(key)] = -1;
        } else {
            if (nav_focus_.find(prefix) == 0) {
                popup_index_ = std::clamp(std::atoi(nav_focus_.substr(prefix.size()).c_str()), 0,
                                          static_cast<int>(items.size()) - 1);
            } else {
                if (input_->KeyPressed("down") || input_->GamepadButtonPressed(0, "dpad_down"))
                    ++popup_index_;
                if (input_->KeyPressed("up") || input_->GamepadButtonPressed(0, "dpad_up"))
                    --popup_index_;
                popup_index_ =
                    (popup_index_ + static_cast<int>(items.size())) % static_cast<int>(items.size());
                nav_focus_ = prefix + std::to_string(popup_index_);
            }
            if (screen_r.Contains(RawMouse()) && input_->MousePressed(0)) {
                const float step_h = std::max(1.0f, r.height / static_cast<float>(items.size()));
                popup_index_ =
                    std::clamp(static_cast<int>((RawMouse().y - screen_r.y) / step_h), 0,
                               static_cast<int>(items.size()) - 1);
                nav_focus_ = prefix + std::to_string(popup_index_);
            }
        }
    }
    const bool saved_activation = nav_activate_;
    if (popup_just_opened_)
        nav_activate_ = false;
    const bool saved_in_popup = in_popup_;
    in_popup_ = true;
    RootBegin(r.x, r.y, UISize::Fixed(std::max(1.0f, width)), UISize::Auto(), 0.0f, true);
    Remember(popup_mem_key);
    ThemedPanel();
    int chosen = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string item_key = std::string(key) + "/" + std::to_string(i);
        if (AutoButton(item_key, item_h > 0.0f ? UISize::Fixed(item_h) : UISize::Auto(), items[i],
                       0.0f)) {
            chosen = static_cast<int>(i);
        }
    }
    RootEnd();
    nav_activate_ = saved_activation;
    if (!recording_)
        popup_just_opened_ = false;
    in_popup_ = saved_in_popup;
    if (chosen >= 0) {
        frame_results_[std::string(key)] = static_cast<float>(chosen);
        popup_.clear();
        nav_focus_.clear();
    }
    return chosen;
}
UISystem::ListRange UISystem::ListBegin(std::string_view key, float view_h, int count,
                                        float item_h) {
    count = std::max(0, count);
    item_h = std::max(1.0f, item_h);
    auto lv_it = active_list_view_.empty() ? list_views_.end() : list_views_.find(active_list_view_);
    if (lv_it == list_views_.end()) {
        return {};
    }
    auto &lv = lv_it->second;
    const float eff_h = view_h > 0.0f ? view_h : std::max(0.0f, lv.rect.height);
    auto &state = scroll_states_[std::string(key)];
    state.height = count * item_h;
    const Rect view{lv.rect.x, lv.rect.y, lv.rect.width, eff_h};
    const Rect vp_clip{
        std::max(view.x, lv.clip_rect.x),
        std::max(view.y, lv.clip_rect.y),
        std::max(0.0f, std::min(view.x + view.width, lv.clip_rect.x + lv.clip_rect.width) -
                           std::max(view.x, lv.clip_rect.x)),
        std::max(0.0f, std::min(view.y + view.height, lv.clip_rect.y + lv.clip_rect.height) -
                           std::max(view.y, lv.clip_rect.y)),
    };
    const bool old_input_only = scroll_input_only_;
    scroll_input_only_ = true;
    if (input_ && renderer_ && camera_) {
        state.offset = PaintScrollArea(key, view.x, view.y, view.width, view.height, state.height,
                                       state.offset,
                                       CanInteract() && vp_clip.Contains(Mouse()) && MouseVisible());
    }
    scroll_input_only_ = old_input_only;
    state.offset = std::clamp(state.offset, 0.0f, std::max(0.0f, state.height - view.height));
    const int first = std::min(count, static_cast<int>(state.offset / item_h));
    const int end =
        std::min(count, static_cast<int>(std::ceil((state.offset + view.height) / item_h)));
    const float sb = ThemeMetric("scrollbar_width");
    const float content_w = std::max(0.0f, view.width - sb);
    const int idx = AllocAutoNode(AutoNode::Kind::Root);
    auto &root = auto_nodes_[static_cast<std::size_t>(idx)];
    root.anchor_x = view.x;
    root.anchor_y = view.y + first * item_h - state.offset;
    root.width_spec = UISize::Fixed(content_w);
    root.height_spec = UISize::Auto();
    root.cross_align = UIAxisAlign::Stretch;
    root.gap = 0.0f;
    root.use_screen_origin = true;
    root.has_clip_override = true;
    root.clip_override = vp_clip;
    root.in_scroll = true;
    root.est_avail_w = content_w;
    auto_stack_.push_back(idx);
    ListState ls;
    ls.count = count;
    ls.item_h = item_h;
    ls.key = std::string(key);
    ls.view = view;
    ls.vp_clip = vp_clip;
    lists_.push_back(std::move(ls));
    list_count_ = std::max(0, end - first);
    return {first, list_count_};
}
void UISystem::ListEnd() {
    if (lists_.empty())
        return;
    const ListState ls = std::move(lists_.back());
    lists_.pop_back();
    RootEnd();
    const auto &st = widgets_[ls.key];
    const auto &sc = scroll_states_[ls.key];
    ApplyClipRectDirect(ls.vp_clip);
    DrawScrollbarVisual(ls.view, sc.height, sc.offset, st.hover, st.dragging);
    ApplyClipRectDirect({0.0f, 0.0f, 1e9f, 1e9f});
    last_rect_ = ls.view;
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
int CharClass(std::string_view text, std::size_t p) {
    if (p >= text.size())
        return -1;
    const unsigned char c = static_cast<unsigned char>(text[p]);
    if (c < 128) {
        if (std::isalnum(c) || c == '_') return 1;
        if (std::isspace(c)) return 0;
        return 2;
    }
    return 3;
}
std::size_t PrevWord(std::string_view text, std::size_t i) {
    while (i > 0 && CharClass(text, PrevChar(text, i)) == 0)
        i = PrevChar(text, i);
    const int k = i > 0 ? CharClass(text, PrevChar(text, i)) : -1;
    while (i > 0 && CharClass(text, PrevChar(text, i)) == k)
        i = PrevChar(text, i);
    return i;
}
std::size_t NextWord(std::string_view text, std::size_t i) {
    const int k = i < text.size() ? CharClass(text, i) : -1;
    while (i < text.size() && CharClass(text, i) == k)
        i = NextChar(text, i);
    while (i < text.size() && CharClass(text, i) == 0)
        i = NextChar(text, i);
    return i;
}
} // namespace
bool UISystem::EditText(std::string_view key, const Rect &r, std::size_t max_chars, bool multiline,
                        bool password, bool allow_draw, bool allow_input,
                        std::string_view placeholder, float radius, float alpha) {
    if (!renderer_ || !input_)
        return false;
    auto &s = widgets_[std::string(key)];
    s.caret = std::min(s.caret, s.text.size());
    s.anchor = std::min(s.anchor, s.text.size());
    bool history_action = false;
    const float scale =
        multiline ? ThemeMetric("font_scale")
                  : std::min(ThemeMetric("font_scale"),
                             std::max(.01f, (r.height - 12) / renderer_->GetFont().LineHeight()));
    const float lh = renderer_->GetFont().LineHeight() * scale;
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
            const float w =
                ch == "\n" ? 0
                           : renderer_->MeasureText(password ? std::string_view("*") : ch, scale);
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
    const WidgetState::EditSnapshot before{s.text, s.caret, s.anchor};
    bool changed = false;
    if (allow_input) {
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
        if (hover && desired_cursor_shape_ == 0) {
            desired_cursor_shape_ = 1; // I-beam
        }
        const bool focused = CanInteract() && focused_ == key;
        const bool shift = input_->KeyDown("left_shift") || input_->KeyDown("right_shift");
        const bool ctrl = input_->KeyDown("left_control") || input_->KeyDown("right_control") ||
                          input_->KeyDown("left_super") || input_->KeyDown("right_super");
        const bool word_mod = ctrl || input_->KeyDown("left_alt") || input_->KeyDown("right_alt");
        const Vec2 old_caret = caret_pos(s.caret);
        const float mouse_scroll_x = multiline ? 0 : std::max(0.0f, old_caret.x - width + 2);
        const float mouse_scroll_y = multiline ? std::max(0.0f, old_caret.y - (r.height - 12 - lh)) : 0;
        if (hover && input_->MousePressed(0))
            s.selecting = true;
        if (s.selecting && CanInteract() &&
            (input_->MouseDown(0) || (input_->MouseReleased(0) &&
                                      (input_->MouseDelta().x != 0 || input_->MouseDelta().y != 0)))) {
            const Vec2 m = Mouse();
            float distance = 1e20f;
            std::size_t index = s.text.size();
            for (const auto &g : glyphs) {
                const float d = std::abs(g.y - (m.y - r.y - 6 + mouse_scroll_y)) * 1000 +
                                std::abs(g.x - (m.x - r.x - 6 + mouse_scroll_x));
                if (d < distance) {
                    distance = d;
                    index = g.start;
                }
            }
            s.caret = index;
            if (input_->MousePressed(0)) {
                const double now = interaction_clock_
                                       ? interaction_clock_()
                                       : std::chrono::duration<double>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count();
                const float dx = m.x - s.last_click_pos.x, dy = m.y - s.last_click_pos.y;
                const bool multi_click =
                    s.last_click >= 0 && now - s.last_click < .35 && dx * dx + dy * dy < 25;
                s.click_streak = multi_click ? (s.click_streak + 1) : 1;
                if (!shift)
                    s.anchor = index;
                if (s.click_streak >= 3 && !s.text.empty()) {
                    s.anchor = 0;
                    s.caret = s.text.size();
                    s.selecting = false;
                    s.last_click = -1;
                    s.click_streak = 0;
                } else if (s.click_streak == 2 && !s.text.empty()) {
                    const auto cls = [](std::string_view text, std::size_t p) {
                        if (p >= text.size())
                            return -1;
                        unsigned char c = static_cast<unsigned char>(text[p]);
                        if (c < 128)
                            return std::isalnum(c) || c == '_' ? 1 : 0;
                        unsigned int cp = c & ((c & 0xf0) == 0xf0 ? 7 : (c & 0xe0) == 0xe0 ? 15 : 31);
                        for (auto q = p + 1; q < NextChar(text, p); ++q)
                            cp = (cp << 6) | (static_cast<unsigned char>(text[q]) & 63);
                        if ((cp >= 0x3000 && cp <= 0x303f) || (cp >= 0xff00 && cp <= 0xff65))
                            return 0;
                        return cp >= 0x3400 && cp <= 0x9fff ? 2 : 1;
                    };
                    std::size_t a = index == s.text.size() ? PrevChar(s.text, index) : index,
                                b = NextChar(s.text, a);
                    const int kind = cls(s.text, a);
                    while (a > 0 && cls(s.text, PrevChar(s.text, a)) == kind)
                        a = PrevChar(s.text, a);
                    while (b < s.text.size() && cls(s.text, b) == kind)
                        b = NextChar(s.text, b);
                    s.anchor = a;
                    s.caret = b;
                    s.selecting = false;
                    s.last_click = now;
                    s.last_click_pos = m;
                } else {
                    s.last_click = now;
                    s.last_click_pos = m;
                }
            }
        }
        if (!CanInteract() || !input_->MouseDown(0))
            s.selecting = false;
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
            if (ctrl && input_->KeyPressed("z")) {
                changed = shift ? Redo(key) : Undo(key);
                history_action = true;
            } else if (ctrl && input_->KeyPressed("y")) {
                changed = Redo(key);
                history_action = true;
            }
            if (ctrl && input_->KeyPressed("a")) {
                s.anchor = 0;
                s.caret = s.text.size();
            }
            if (ctrl && (input_->KeyPressed("c") || input_->KeyPressed("x"))) {
                const auto a = std::min(s.anchor, s.caret), b = std::max(s.anchor, s.caret);
                if (a != b && !password)
                    input_->SetClipboard(std::string_view(s.text).substr(a, b - a));
                if (input_->KeyPressed("x"))
                    erase_selection();
            }
            if (ctrl && input_->KeyPressed("v"))
                insert(input_->Clipboard());
            if (!ctrl && text_activated_ != key)
                for (unsigned int cp : input_->TextCodepoints())
                    if (cp >= 32 && cp != 127) {
                        std::string utf;
                        AppendUtf8(utf, cp);
                        insert(utf);
                    }
            if (input_->KeyPressed("backspace") && !erase_selection() && s.caret > 0) {
                auto prev = word_mod ? PrevWord(s.text, s.caret) : PrevChar(s.text, s.caret);
                s.text.erase(prev, s.caret - prev);
                s.caret = s.anchor = prev;
                changed = true;
            }
            if (input_->KeyPressed("delete") && !erase_selection() && s.caret < s.text.size()) {
                auto nxt = word_mod ? NextWord(s.text, s.caret) : NextChar(s.text, s.caret);
                s.text.erase(s.caret, nxt - s.caret);
                changed = true;
            }
            glyphs = layout();
            if (input_->KeyPressed("left")) {
                const std::size_t tgt = word_mod ? PrevWord(s.text, s.caret) : PrevChar(s.text, s.caret);
                move(!shift && s.anchor != s.caret ? std::min(s.anchor, s.caret) : tgt);
            }
            if (input_->KeyPressed("right")) {
                const std::size_t tgt = word_mod ? NextWord(s.text, s.caret) : NextChar(s.text, s.caret);
                move(!shift && s.anchor != s.caret ? std::max(s.anchor, s.caret) : tgt);
            }
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
            if (input_->KeyPressed("enter") && text_activated_ != key) {
                if (multiline)
                    insert("\n");
                else
                    focused_.clear();
            }
            if (input_->KeyPressed("escape") || input_->GamepadButtonPressed(0, "b"))
                focused_.clear();
        }
    }
    if (allow_draw) {
        auto dim = [&](Color c) {
            c.a *= alpha;
            if (IsDisabled())
                c.a *= 0.4f;
            return c;
        };
        const bool focused_draw = CanInteract() && focused_ == key;
        glyphs = layout();
        const Vec2 caret = caret_pos(s.caret);
        const float scroll_x = multiline ? 0 : std::max(0.0f, caret.x - width + 2);
        const float scroll_y =
            multiline ? std::max(0.0f, caret.y + lh - std::max(lh, r.height - 12)) : 0;
        const Color brd_c = dim(focused_draw ? ThemeColor("accent") : ThemeColor("border"));
        const Color fld_c = dim(ThemeColor("field"));
        if (radius > 0.5f) {
            DrawRoundedQuad(r, radius, brd_c);
            DrawRoundedQuad({r.x + 2, r.y + 2, std::max(0.0f, r.width - 4), std::max(0.0f, r.height - 4)},
                            std::max(0.0f, radius - 1.5f), fld_c);
        } else {
            renderer_->DrawQuad(MapRect(r), brd_c);
            renderer_->DrawQuad(
                MapRect({r.x + 2, r.y + 2, std::max(0.0f, r.width - 4), std::max(0.0f, r.height - 4)}),
                fld_c);
        }
        const Rect inner_clip{r.x + 3.0f, r.y + 3.0f, std::max(0.0f, r.width - 6.0f),
                              std::max(0.0f, r.height - 6.0f)};
        ApplyClipRectDirect(inner_clip, std::max(0.0f, radius - 2.0f));
        // Per-glyph selection follows exactly the same wrapped geometry as text.
        const auto lo = std::min(s.caret, s.anchor), hi = std::max(s.caret, s.anchor);
        for (const auto &g : glyphs) {
            if (g.start == g.end)
                break;
            const float x = r.x + 6 + g.x - scroll_x, y = r.y + 6 + g.y - scroll_y;
            if (g.start >= lo && g.start < hi)
                renderer_->DrawQuad(MapRect({x, y, std::max(3.0f, g.w), lh}), dim(ThemeColor("accent")));
            if (s.text[g.start] != '\n') {
                const std::string_view ch_sv =
                    password ? std::string_view("*")
                             : std::string_view(s.text).substr(g.start, g.end - g.start);
                renderer_->DrawText(ch_sv, x * scale_ + scale_offset_.x, y * scale_ + scale_offset_.y,
                                    scale * scale_, dim(ThemeColor("text")), true);
            }
        }
        if (s.text.empty() && !focused_draw) {
            const std::string_view ph = placeholder.empty() ? std::string_view("click to type") : placeholder;
            DrawFittedText(ph, r.x + 6, r.y + 6, width, scale, dim({.45f, .48f, .55f, 1}));
        }
        if (focused_draw) {
            const Vec2 c = caret_pos(s.caret);
            const Rect caret_r{r.x + 6 + c.x - scroll_x, r.y + 6 + c.y - scroll_y, 1.5f, lh};
            const bool caret_visible = clock_seconds_ <= 0.0f || std::fmod(clock_seconds_, 1.0f) < 0.62f;
            if (caret_visible) {
                renderer_->DrawQuad(MapRect(caret_r), dim(ThemeColor("text")));
            }
            UpdateImeCaret(caret_r);
        }
    }
    if (allow_input && changed && !history_action) {
        if (s.undo.size() >= 128)
            s.undo.erase(s.undo.begin());
        s.undo.push_back(before);
        s.redo.clear();
    }
    return allow_input && changed;
}
void UISystem::SetRichIcon(std::string_view name, const Texture2D *texture, const Rect &src) {
    rich_icons_[std::string(name)] = {texture, src, 0, 0};
}
bool UISystem::BeginFrameUI() {
    if (recording_ || frame_submitted_ || !auto_stack_.empty() || deferred_ != 0)
        return false;
    nav_focus_ = saved_nav_focus_;
    focused_ = saved_text_focus_;
    stick_held_ = saved_stick_;
    recording_ = true;
    ++deferred_;
    frame_results_.clear();
    widget_rects_.clear();
    clicked_link_.clear();
    activated_widget_.clear();
    nav_activate_ = false;
    frame_started_ = std::chrono::steady_clock::now();
    return true;
}
void UISystem::NavigateCurrent() {
    text_activated_.clear();
    nav_activate_ = false;
    nav_adjust_step_ = 0;
    auto it = std::find_if(focus_ring_.begin(), focus_ring_.end(),
                           [this](const FocusEntry &e) { return e.key == nav_focus_; });
    if (it == focus_ring_.end()) {
        nav_focus_.clear();
        focused_.clear();
    }
    if (!input_ || focus_ring_.empty())
        return;
    const bool shift = input_->KeyDown("left_shift") || input_->KeyDown("right_shift");
    const bool editing = it != focus_ring_.end() && it->text && focused_ == nav_focus_;
    const bool adjustable = it != focus_ring_.end() && it->adjustable;
    int step = input_->KeyPressed("tab") ? (shift ? -1 : 1) : 0;
    const float x = input_->GamepadAxis(0, "left_x"), y = input_->GamepadAxis(0, "left_y");
    const bool stick = std::abs(x) > .55f || std::abs(y) > .55f;
    if (!editing) {
        if (input_->KeyPressed("down") || input_->GamepadButtonPressed(0, "dpad_down") ||
            (!adjustable &&
             (input_->KeyPressed("right") || input_->GamepadButtonPressed(0, "dpad_right"))) ||
            (stick && !stick_held_ && (y > .55f || (!adjustable && x > .55f))))
            step = 1;
        if (input_->KeyPressed("up") || input_->GamepadButtonPressed(0, "dpad_up") ||
            (!adjustable &&
             (input_->KeyPressed("left") || input_->GamepadButtonPressed(0, "dpad_left"))) ||
            (stick && !stick_held_ && (y < -.55f || (!adjustable && x < -.55f))))
            step = -1;
        if (adjustable && stick && !stick_held_)
            nav_adjust_step_ = x > .55f ? 1 : x < -.55f ? -1 : 0;
        nav_activate_ = input_->KeyPressed("enter") || input_->KeyPressed("space") ||
                        input_->GamepadButtonPressed(0, "a");
    }
    stick_held_ = stick;
    if (step && !focus_ring_.empty()) {
        AdvanceRing(focus_ring_, step, nav_focus_, focused_);
        EnsureFocusedVisible();
    }
    if (!editing && nav_activate_) {
        auto target =
            std::find_if(focus_ring_.begin(), focus_ring_.end(),
                         [this](const FocusEntry &entry) { return entry.key == nav_focus_; });
        if (target != focus_ring_.end() && target->text) {
            focused_ = nav_focus_;
            text_activated_ = nav_focus_;
            nav_activate_ = false;
        }
    }
    if (input_->MousePressed(0)) {
        auto mouse_it =
            std::find_if(focus_ring_.begin(), focus_ring_.end(),
                         [this](const FocusEntry &e) { return e.key == pointer_owner_; });
        if (mouse_it != focus_ring_.end()) {
            nav_focus_ = mouse_it->key;
            focused_ = mouse_it->text ? nav_focus_ : "";
        } else
            focused_.clear();
    }
}
bool UISystem::EndLayout() {
    if (!recording_ || resolving_ || layout_resolved_ || !auto_stack_.empty() || deferred_ != 1)
        return false;
    const auto now = std::chrono::steady_clock::now();
    frame_stats_.layout_ms +=
        std::chrono::duration<double, std::milli>(now - frame_started_).count();
    frame_started_ = std::chrono::steady_clock::now();
    frame_stats_.interaction_ms +=
        std::chrono::duration<double, std::milli>(frame_started_ - now).count();
    layout_resolved_ = true;
    return true;
}
bool UISystem::ListViewBegin(std::string_view key) {
    auto it = list_views_.find(std::string(key));
    if (resolving_ || (recording_ && !layout_resolved_) || !auto_stack_.empty() ||
        !active_list_view_.empty() || it == list_views_.end() || it->second.submitted)
        return false;
    auto &view = it->second;
    view.submitted = true;
    active_list_view_ = key;
    DisabledBegin(view.disabled);
    in_modal_ = !view.modal.empty() && view.modal == modal_;
    in_popup_ = !view.popup.empty() && view.popup == popup_;
    return true;
}
bool UISystem::ListViewEnd() {
    if (active_list_view_.empty())
        return false;
    if (!auto_stack_.empty() || !lists_.empty() || !variable_scopes_.empty())
        return false;
    DisabledEnd();
    in_modal_ = in_popup_ = false;
    active_list_view_.clear();
    return true;
}
bool UISystem::EndFrameUI() {
    if (!recording_ || resolving_ || !auto_stack_.empty() || deferred_ != 1)
        return false;
    const auto layout_done = std::chrono::steady_clock::now();
    frame_stats_.layout_ms +=
        std::chrono::duration<double, std::milli>(layout_done - frame_started_).count();
    if (!deferred_auto_nodes_.empty() || !deferred_rich_links_.empty()) {
        ResolveDeferredAutoWidgets();
    }
    ProcessDragAndDrop();
    recording_ = false;
    --deferred_;
    frame_stats_.interaction_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - layout_done)
            .count();
    popup_just_opened_ = false;
    frame_submitted_ = true;
    ApplyClipRectDirect({0.0f, 0.0f, 1e9f, 1e9f});
    if (inspector_enabled_) {
        DrawInspectorOverlay();
    }
    return true;
}
bool UISystem::Clicked(std::string_view key) const {
    auto it = frame_results_.find(ResolveKeyLookup(key));
    return it != frame_results_.end() && it->second != 0;
}
float UISystem::ResultNumber(std::string_view key, float fallback) const {
    auto it = frame_results_.find(ResolveKeyLookup(key));
    return it == frame_results_.end() ? fallback : it->second;
}
Rect UISystem::WidgetRect(std::string_view key) const {
    auto it = widget_rects_.find(ResolveKeyLookup(key));
    return it == widget_rects_.end() ? Rect{} : it->second;
}

void UISystem::RebuildVariable(VariableListState &s) {
    s.positions.clear();
    s.heights.resize(s.ids.size());
    s.fenwick.assign(s.ids.size() + 1, 0);
    for (std::size_t i = 0; i < s.ids.size(); ++i) {
        s.positions[s.ids[i]] = i;
        auto it = s.measured.find(s.ids[i]);
        s.heights[i] = it == s.measured.end() ? s.estimate : it->second;
        s.fenwick[i + 1] += s.heights[i];
        const std::size_t parent = (i + 1) + ((i + 1) & (~i));
        if (parent < s.fenwick.size())
            s.fenwick[parent] += s.fenwick[i + 1];
    }
}
float UISystem::VariablePrefix(const VariableListState &s, std::size_t count) const {
    float sum = 0;
    for (std::size_t i = std::min(count, s.ids.size()); i > 0; i -= i & (~i + 1))
        sum += s.fenwick[i];
    return sum;
}
std::size_t UISystem::VariableFind(const VariableListState &s, float offset) {
    std::size_t index = 0, bit = 1;
    while (bit < s.fenwick.size())
        bit <<= 1;
    float sum = 0;
    for (; bit; bit >>= 1) {
        ++frame_stats_.tree_steps;
        const auto next = index + bit;
        if (next < s.fenwick.size() && sum + s.fenwick[next] <= offset) {
            index = next;
            sum += s.fenwick[next];
        }
    }
    return std::min(index, s.ids.size());
}
void UISystem::SaveVariableAnchor(const std::string &key, VariableListState &s) {
    if (s.anchor_pending && s.positions.count(s.anchor))
        return;
    const float offset = scroll_states_[key].offset;
    const auto index = VariableFind(s, offset);
    if (index < s.ids.size()) {
        s.anchor = s.ids[index];
        s.anchor_inset = offset - VariablePrefix(s, index);
    } else {
        s.anchor = s.ids.empty() ? "" : s.ids.back();
        s.anchor_inset = 0;
    }
}
void UISystem::RestoreVariableAnchor(const std::string &key, VariableListState &s, float view_h) {
    auto &scroll = scroll_states_[key];
    scroll.height = VariablePrefix(s, s.ids.size());
    auto it = s.positions.find(s.anchor);
    if (it != s.positions.end()) {
        float inset = s.anchor_inset;
        if (s.measured.count(s.anchor))
            inset = std::min(inset, std::max(0.0f, s.heights[it->second] - .001f));
        scroll.offset = VariablePrefix(s, it->second) + inset;
    }
    if (s.bottom)
        scroll.offset = std::max(0.0f, scroll.height - view_h);
    scroll.offset = std::clamp(scroll.offset, 0.0f, std::max(0.0f, scroll.height - view_h));
}
bool UISystem::ConfigureVariableList(std::string_view name, const std::vector<std::string> &ids,
                                     float estimate, std::string_view revision) {
    if (std::any_of(variable_scopes_.begin(), variable_scopes_.end(),
                    [&](const VariableListScope &scope) { return scope.key == name; }))
        return false;
    if (!std::isfinite(estimate) || estimate < 1)
        return false;
    std::unordered_map<std::string, bool> seen;
    for (const auto &id : ids)
        if (id.empty() || !seen.emplace(id, true).second)
            return false;
    const std::string key(name);
    auto &s = variable_lists_[key];
    if (s.ids == ids && s.estimate == estimate && s.revision == revision)
        return true;
    SaveVariableAnchor(key, s);
    std::size_t old_index = 0;
    auto old = s.positions.find(s.anchor);
    if (old != s.positions.end())
        old_index = old->second;
    // If the anchor is deleted choose its nearest surviving successor, then predecessor.
    if (seen.find(s.anchor) == seen.end()) {
        std::string replacement;
        for (std::size_t i = old_index; i < s.ids.size(); ++i)
            if (seen.count(s.ids[i])) {
                replacement = s.ids[i];
                break;
            }
        if (replacement.empty())
            for (std::size_t i = std::min(old_index, s.ids.size()); i > 0; --i)
                if (seen.count(s.ids[i - 1])) {
                    replacement = s.ids[i - 1];
                    break;
                }
        s.anchor = replacement;
        s.anchor_inset = 0;
    }
    if (s.revision != revision) {
        s.measured.clear();
        s.measure_cursor = 0;
    }
    s.ids = ids;
    s.estimate = estimate;
    s.revision = revision;
    // Remove deleted cache entries so repeated replacements do not leak memory.
    for (auto it = s.measured.begin(); it != s.measured.end();)
        if (!seen.count(it->first))
            it = s.measured.erase(it);
        else
            ++it;
    for (auto it = s.texts.begin(); it != s.texts.end();)
        if (!seen.count(it->first))
            it = s.texts.erase(it);
        else
            ++it;
    RebuildVariable(s);
    s.anchor_pending = !s.anchor.empty() && !s.measured.count(s.anchor);
    RestoreVariableAnchor(key, s, 0);
    return true;
}
bool UISystem::SetVariableRowText(std::string_view name, std::string_view id,
                                  std::string_view text, float scale, float extra_h) {
    auto it = variable_lists_.find(std::string(name));
    if (it == variable_lists_.end() || !it->second.positions.count(std::string(id)) ||
        !std::isfinite(scale) || scale < 0 || !std::isfinite(extra_h) || extra_h < 0 ||
        !variable_scopes_.empty())
        return false;
    auto &s = it->second;
    auto old = s.texts.find(std::string(id));
    if (old != s.texts.end() && old->second.text == text && old->second.scale == scale &&
        old->second.extra == extra_h)
        return true;
    if (s.measured.count(std::string(id)))
        InvalidateVariableList(name, id);
    s.texts[std::string(id)] = {std::string(text), scale, extra_h};
    return true;
}
void UISystem::InvalidateVariableList(std::string_view name, std::string_view row_id) {
    if (std::any_of(variable_scopes_.begin(), variable_scopes_.end(),
                    [&](const VariableListScope &scope) { return scope.key == name; }))
        return;
    auto it = variable_lists_.find(std::string(name));
    if (it == variable_lists_.end())
        return;
    auto &s = it->second;
    SaveVariableAnchor(it->first, s);
    if (row_id.empty())
        s.measured.clear();
    else
        s.measured.erase(std::string(row_id));
    RebuildVariable(s);
    s.anchor_pending = !s.anchor.empty() && !s.measured.count(s.anchor);
    RestoreVariableAnchor(it->first, s, 0);
}
bool UISystem::VariableListBegin(std::string_view name, float view_h, bool pin_bottom) {
    auto lv_it = active_list_view_.empty() ? list_views_.end() : list_views_.find(active_list_view_);
    if (lv_it == list_views_.end())
        return false;
    const float eff_view_h = view_h > 0.0f ? view_h : lv_it->second.rect.height;
    if (!std::isfinite(eff_view_h) || eff_view_h <= 0)
        return false;
    const std::string key(name);
    auto it = variable_lists_.find(key);
    if (it == variable_lists_.end())
        return false;
    auto &s = it->second;
    const float sb = ThemeMetric("scrollbar_width");
    const float avail_w = lv_it->second.rect.width;
    const float width = std::max(0.0f, avail_w - sb), fs = ThemeMetric("font_scale");
    const auto font_revision = renderer_ ? renderer_->GetFont().Revision() : 0;
    const bool changed = s.width != width || s.font_scale != fs || s.font_revision != font_revision;
    if (changed) {
        SaveVariableAnchor(key, s);
        s.measured.clear();
        s.width = width;
        s.font_scale = fs;
        s.font_revision = font_revision;
        RebuildVariable(s);
        s.anchor_pending = !s.anchor.empty() && !s.measured.count(s.anchor);
        RestoreVariableAnchor(key, s, eff_view_h);
    }
    // Bounded exact work, no callback replay or interaction side effects. Visible rows
    // remain authoritative; text metadata must describe the same wrapped Text call.
    if (renderer_ && !s.ids.empty() && !s.texts.empty()) {
        SaveVariableAnchor(key, s);
        for (std::size_t scanned = 0; scanned < std::min(s.ids.size(), variable_measure_budget_);
             ++scanned) {
            const auto index = s.measure_cursor++ % s.ids.size();
            const auto &id = s.ids[index];
            auto text = s.texts.find(id);
            if (s.measured.count(id) || text == s.texts.end())
                continue;
            const auto &m = text->second;
            const float scale = m.scale > 0 ? m.scale : fs;
            const float height = std::max(1.0f, m.extra +
                (width <= 0 || m.text.empty() ? 0 :
                 renderer_->GetFont().MeasureTextEx(m.text, scale, width)));
            const float delta = height - s.heights[index];
            s.heights[index] = height;
            s.measured[id] = height;
            for (std::size_t n = index + 1; n < s.fenwick.size(); n += n & (~n + 1)) {
                s.fenwick[n] += delta;
                ++frame_stats_.tree_steps;
            }
            ++frame_stats_.premeasured_rows;
        }
        RestoreVariableAnchor(key, s, eff_view_h);
    }
    auto &saved = scroll_states_[key];
    saved.height = VariablePrefix(s, s.ids.size());
    s.bottom = pin_bottom;
    if (pin_bottom)
        s.anchor_pending = false;
    const float input_offset = saved.offset;
    if (pin_bottom)
        saved.offset = std::max(0.0f, saved.height - eff_view_h);
    const auto &lv = lv_it->second;
    const Rect view{lv.rect.x, lv.rect.y, lv.rect.width, eff_view_h};
    const Rect vp_clip{
        std::max(view.x, lv.clip_rect.x),
        std::max(view.y, lv.clip_rect.y),
        std::max(0.0f, std::min(view.x + view.width, lv.clip_rect.x + lv.clip_rect.width) -
                           std::max(view.x, lv.clip_rect.x)),
        std::max(0.0f, std::min(view.y + view.height, lv.clip_rect.y + lv.clip_rect.height) -
                           std::max(view.y, lv.clip_rect.y)),
    };
    const bool old_input_only = scroll_input_only_;
    scroll_input_only_ = true;
    if (input_ && renderer_ && camera_) {
        saved.offset = PaintScrollArea(key, view.x, view.y, view.width, view.height, saved.height,
                                       saved.offset,
                                       CanInteract() && MouseVisible() && vp_clip.Contains(Mouse()));
    }
    scroll_input_only_ = old_input_only;
    saved.offset = std::clamp(saved.offset, 0.0f, std::max(0.0f, saved.height - eff_view_h));
    if (saved.offset != input_offset && input_ &&
        (input_->MouseWheel() != 0 || input_->MouseDown(0)))
        s.anchor_pending = false;
    SaveVariableAnchor(key, s);
    const auto first = s.anchor_pending && s.positions.count(s.anchor)
                           ? s.positions.at(s.anchor)
                           : VariableFind(s, saved.offset);
    VariableListScope scope;
    scope.key = key;
    scope.view_h = eff_view_h;
    scope.next = static_cast<int>(first);
    scope.row_open = false;
    scope.pin_bottom = pin_bottom;
    scope.view = view;
    scope.vp_clip = vp_clip;
    variable_scopes_.push_back(std::move(scope));
    return true;
}
int UISystem::VariableListNext() {
    if (variable_scopes_.empty())
        return -1;
    auto &scope = variable_scopes_.back();
    auto &s = variable_lists_[scope.key];
    if (scope.row_open || !auto_stack_.empty())
        return -1;
    int index = scope.submitted.empty() && s.anchor_pending && s.positions.count(s.anchor)
                    ? static_cast<int>(s.positions.at(s.anchor))
                    : static_cast<int>(VariableFind(s, scroll_states_[scope.key].offset));
    while (index < static_cast<int>(s.ids.size()) && scope.visited.count(index))
        ++index;
    if (index >= static_cast<int>(s.ids.size()))
        return -1;
    const float start = VariablePrefix(s, index);
    if (start >= scroll_states_[scope.key].offset + scope.view_h)
        return -1;
    scope.next = index;
    scope.visited[index] = true;
    const float sb = ThemeMetric("scrollbar_width");
    const float content_w = std::max(0.0f, scope.view.width - sb);
    const int idx = AllocAutoNode(AutoNode::Kind::Root);
    auto &root = auto_nodes_[static_cast<std::size_t>(idx)];
    root.anchor_x = scope.view.x;
    root.anchor_y = scope.view.y + start - scroll_states_[scope.key].offset;
    root.width_spec = UISize::Fixed(content_w);
    root.height_spec = UISize::Auto();
    root.cross_align = UIAxisAlign::Stretch;
    root.gap = 0.0f;
    root.use_screen_origin = true;
    root.defer_commit = true;
    root.in_scroll = true;
    root.est_avail_w = content_w;
    auto_stack_.push_back(idx);
    scope.submitted.push_back({index, idx});
    scope.row_open = true;
    return index;
}
float UISystem::VariableListRowEnd() {
    if (variable_scopes_.empty() || !variable_scopes_.back().row_open)
        return 0;
    auto &scope = variable_scopes_.back();
    auto &s = variable_lists_[scope.key];
    const auto index = static_cast<std::size_t>(scope.next);
    RootEnd();
    const float height = std::max(1.0f, last_rect_.height);
    const float delta = height - s.heights[index];
    s.heights[index] = height;
    s.measured[s.ids[index]] = height;
    if (s.ids[index] == s.anchor) {
        s.anchor_pending = false;
        s.anchor_inset = std::min(s.anchor_inset, std::max(0.0f, height - .001f));
    }
    for (std::size_t i = index + 1; i < s.fenwick.size(); i += i & (~i + 1)) {
        s.fenwick[i] += delta;
        ++frame_stats_.tree_steps;
    }
    ++frame_stats_.measured_rows;
    scope.row_open = false;
    RestoreVariableAnchor(scope.key, s, scope.view_h);
    return height;
}
void UISystem::VariableListEnd() {
    if (variable_scopes_.empty())
        return;
    auto scope = variable_scopes_.back();
    if (scope.row_open)
        VariableListRowEnd();
    auto &s = variable_lists_[scope.key];
    RestoreVariableAnchor(scope.key, s, scope.view_h);
    const float offset = scroll_states_[scope.key].offset;
    const float sb = ThemeMetric("scrollbar_width");
    const float content_w = std::max(0.0f, scope.view.width - sb);
    const float total_h = VariablePrefix(s, s.ids.size());
    const bool prev_commit = in_auto_commit_;
    in_auto_commit_ = true;
    for (const auto &row : scope.submitted) {
        if (row.root_idx < 0 || static_cast<std::size_t>(row.root_idx) >= auto_nodes_.size())
            continue;
        const float y = scope.view.y + VariablePrefix(s, static_cast<std::size_t>(row.index)) - offset;
        const float h = s.heights[static_cast<std::size_t>(row.index)];
        const Rect row_rect{scope.view.x, y, content_w, h};
        ArrangeAutoNode(row.root_idx, row_rect, scope.vp_clip, true,
                        auto_nodes_[static_cast<std::size_t>(row.root_idx)].box_id, -1, -1);
        CommitAutoNode(row.root_idx, row_rect);
    }
    in_auto_commit_ = prev_commit;
    const auto &st = widgets_[scope.key];
    ApplyClipRectDirect(scope.vp_clip);
    DrawScrollbarVisual(scope.view, total_h, offset, st.hover, st.dragging);
    ApplyClipRectDirect({0.0f, 0.0f, 1e9f, 1e9f});
    last_rect_ = scope.view;
    SaveVariableAnchor(scope.key, s);
    variable_scopes_.pop_back();
}
float UISystem::VariableListTotal(std::string_view key) const {
    auto it = variable_lists_.find(std::string(key));
    return it == variable_lists_.end() ? 0 : VariablePrefix(it->second, it->second.ids.size());
}
std::string UISystem::VariableListAnchor(std::string_view key) const {
    auto it = variable_lists_.find(std::string(key));
    return it == variable_lists_.end() ? "" : it->second.anchor;
}

std::string UISystem::VariableListID(std::string_view key, int index) const {
    auto it = variable_lists_.find(std::string(key));
    return it == variable_lists_.end() || index < 0 ||
                   static_cast<std::size_t>(index) >= it->second.ids.size()
               ? ""
               : it->second.ids[index];
}
float UISystem::VariableListHeight(std::string_view key, std::string_view id) const {
    auto it = variable_lists_.find(std::string(key));
    if (it == variable_lists_.end())
        return 0;
    auto pos = it->second.positions.find(std::string(id));
    return pos == it->second.positions.end() ? 0 : it->second.heights[pos->second];
}
bool UISystem::VariableListMeasured(std::string_view key, std::string_view id) const {
    auto it = variable_lists_.find(std::string(key));
    return it != variable_lists_.end() && it->second.measured.count(std::string(id));
}
bool UISystem::Undo(std::string_view key) {
    auto *s = Find(key);
    if (!s || s->undo.empty())
        return false;
    s->redo.push_back({s->text, s->caret, s->anchor});
    auto snapshot = std::move(s->undo.back());
    s->undo.pop_back();
    s->text = std::move(snapshot.text);
    s->caret = snapshot.caret;
    s->anchor = snapshot.anchor;
    return true;
}
bool UISystem::Redo(std::string_view key) {
    auto *s = Find(key);
    if (!s || s->redo.empty())
        return false;
    s->undo.push_back({s->text, s->caret, s->anchor});
    auto snapshot = std::move(s->redo.back());
    s->redo.pop_back();
    s->text = std::move(snapshot.text);
    s->caret = snapshot.caret;
    s->anchor = snapshot.anchor;
    return true;
}
std::size_t UISystem::Selection(std::string_view key, bool end) const {
    auto it = widgets_.find(std::string(key));
    if (it == widgets_.end())
        return 0;
    return end ? std::max(it->second.caret, it->second.anchor)
               : std::min(it->second.caret, it->second.anchor);
}

void UISystem::SetModal(bool modal) {
    if (modal && !manual_modal_) {
        manual_modal_layer_ = ++modal_layer_seq_;
    } else if (!modal) {
        manual_modal_layer_ = 0;
    }
    manual_modal_ = modal;
}

void UISystem::BeginAlignGroup(std::string_view key, std::string_view rules) {
    AlignGroupRecord rec;
    rec.key = std::string(key);
    rec.rules = std::string(rules);
    if (InAutoLayout()) {
        rec.parent_auto_idx = auto_stack_.back();
        rec.depth = auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].box_depth + 1;
        rec.parent_box_id = auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].box_id;
        rec.parent_rect = {world_origin_.x, world_origin_.y, LogicalWidth(), LogicalHeight()};
        rec.audit_start = std::numeric_limits<std::size_t>::max();
        rec.audit_end = 0;
    } else {
        rec.depth = 0;
        rec.parent_box_id = 0;
        rec.parent_rect = {world_origin_.x, world_origin_.y, LogicalWidth(), LogicalHeight()};
        rec.audit_start = audit_.size();
        rec.audit_end = audit_.size();
    }
    rec.layer = manual_modal_ ? manual_modal_layer_ : ((!modal_.empty() || !popup_.empty()) ? 1 : 0);
    rec.suppressed = suppress_;
    const int gi = static_cast<int>(align_groups_.size());
    align_groups_.push_back(std::move(rec));
    align_stack_.push_back(gi);
}

void UISystem::EndAlignGroup() {
    if (align_stack_.empty()) {
        return;
    }
    const int gi = align_stack_.back();
    align_stack_.pop_back();
    if (!InAutoLayout() && gi >= 0 && static_cast<std::size_t>(gi) < align_groups_.size()) {
        align_groups_[static_cast<std::size_t>(gi)].audit_end = audit_.size();
    }
}

bool UISystem::IsInteractiveKind(std::string_view kind) {
    return kind == "button" || kind == "tab" || kind == "checkbox" || kind == "slider" ||
           kind == "image button" || kind == "combo" || kind == "combo_item" ||
           kind == "menu" || kind == "menu_item" ||
           kind == "text_input" || kind == "text_area" || kind == "text input" ||
           kind == "text area" || kind == "password" || kind == "radio" ||
           kind == "splitter" || kind == "tree";
}

namespace {

Rect IntersectRects(const Rect &a, const Rect &b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

std::string AuditDisplayName(const UISystem::AuditRect &a, std::size_t idx) {
    if (!a.name.empty()) {
        return a.name;
    }
    return a.kind + "#" + std::to_string(idx);
}

bool HasAlignRule(std::string_view rules, std::string_view token) {
    std::size_t pos = 0;
    while (pos < rules.size()) {
        const std::size_t sep = rules.find_first_of("|, ", pos);
        const std::string_view part =
            sep == std::string_view::npos ? rules.substr(pos) : rules.substr(pos, sep - pos);
        if (part == token) {
            return true;
        }
        if (sep == std::string_view::npos) {
            break;
        }
        pos = sep + 1;
    }
    return false;
}

} // namespace

std::vector<UISystem::LayoutIssue> UISystem::AuditLayout() const {
    std::vector<LayoutIssue> issues;

    const float vw = LogicalWidth();
    const float vh = LogicalHeight();
    const bool has_vp = vw > 1.0f && vh > 1.0f;

    // 1. Box overflow + Viewport containment
    for (std::size_t i = 0; i < audit_.size(); ++i) {
        const auto &a = audit_[i];
        const std::string label = AuditDisplayName(a, i);
        if (a.overflow) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "overflow: %s (%s) rect=(%.1f,%.1f,%.1f,%.1f) bounds=(%.1f,%.1f,%.1f,%.1f)",
                          label.c_str(), a.kind.c_str(), a.rect.x, a.rect.y, a.rect.width, a.rect.height,
                          a.bounds.x, a.bounds.y, a.bounds.width, a.bounds.height);
            issues.push_back({"overflow", "bounds", label, "", a.rect, a.bounds, 1.0f, buf});
        }
        if (!a.suppressed && !a.disabled && !a.in_scroll && IsInteractiveKind(a.kind) &&
            (a.rect.width <= 0.5f || a.rect.height <= 0.5f)) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "zero_size: enabled %s '%s' collapsed to (%.1fx%.1f)",
                          a.kind.c_str(), label.c_str(), a.rect.width, a.rect.height);
            issues.push_back({"zero_size", "non_empty", label, "", a.rect, a.bounds, 1.0f, buf});
        }
        if (has_vp && !a.suppressed && IsInteractiveKind(a.kind) &&
            a.rect.width > 0.0f && a.rect.height > 0.0f) {
            const Rect &check_r = a.in_scroll ? a.clip_rect : a.rect;
            if (!a.in_scroll || (check_r.width > 0.5f && check_r.height > 0.5f)) {
                const float dx0 = -check_r.x;
                const float dy0 = -check_r.y;
                const float dx1 = (check_r.x + check_r.width) - vw;
                const float dy1 = (check_r.y + check_r.height) - vh;
                const float max_out = std::max({dx0, dy0, dx1, dy1});
                if (max_out > 0.5f) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "viewport: %s (%s) rect=(%.1f,%.1f,%.1f,%.1f) outside (%.0fx%.0f)",
                                  label.c_str(), a.kind.c_str(), check_r.x, check_r.y, check_r.width,
                                  check_r.height, vw, vh);
                    issues.push_back({"viewport", "screen", label, "", check_r, {0, 0, vw, vh}, max_out, buf});
                }
            }
        }
    }

    // 2. Explicit AlignGroup hard constraints
    for (const auto &g : align_groups_) {
        std::vector<const AuditRect *> members;
        const std::size_t end = std::min(g.audit_end, audit_.size());
        const bool want_text = HasAlignRule(g.rules, "include_text");
        const bool force_widgets = HasAlignRule(g.rules, "widgets") || want_text;
        if (!force_widgets) {
            for (std::size_t i = g.audit_start; i < end; ++i) {
                const auto &a = audit_[i];
                if (!a.suppressed && a.box_depth == g.depth &&
                    (a.kind == "box" || a.kind == "auto box" || a.kind == "panel") &&
                    a.rect.width > 0.0f && a.rect.height > 0.0f) {
                    members.push_back(&a);
                }
            }
        }
        if (members.empty()) {
            auto qualifies = [want_text](const AuditRect &a) {
                if (a.suppressed || a.rect.width <= 0.0f || a.rect.height <= 0.0f) {
                    return false;
                }
                if (IsInteractiveKind(a.kind)) {
                    return true;
                }
                return want_text && (a.kind == "text" || a.kind == "rich text" ||
                                     a.kind == "progress" || a.kind == "panel");
            };
            std::uint32_t min_depth = std::numeric_limits<std::uint32_t>::max();
            for (std::size_t i = g.audit_start; i < end; ++i) {
                const auto &a = audit_[i];
                if (a.box_depth >= g.depth && qualifies(a)) {
                    min_depth = std::min(min_depth, a.box_depth);
                }
            }
            if (min_depth != std::numeric_limits<std::uint32_t>::max()) {
                for (std::size_t i = g.audit_start; i < end; ++i) {
                    const auto &a = audit_[i];
                    if (a.box_depth == min_depth && qualifies(a)) {
                        members.push_back(&a);
                    }
                }
            }
        }
        if (members.empty()) {
            continue;
        }

        // 2a. Parent-container centering, symmetry, and containment checks (>= 1 member)
        if (g.parent_rect.width > 0.0f && g.parent_rect.height > 0.0f) {
            constexpr float kParentTol = 0.5f;
            const Rect &pr = g.parent_rect;
            const bool chk_cx = HasAlignRule(g.rules, "center_x_in_parent") ||
                                HasAlignRule(g.rules, "center_in_parent");
            const bool chk_cy = HasAlignRule(g.rules, "center_y_in_parent") ||
                                HasAlignRule(g.rules, "center_in_parent");
            const bool chk_sx = HasAlignRule(g.rules, "symmetric_x");
            const bool chk_sy = HasAlignRule(g.rules, "symmetric_y");
            const bool chk_fit = HasAlignRule(g.rules, "fit_parent");
            for (std::size_t m = 0; m < members.size(); ++m) {
                const Rect &mr = members[m]->rect;
                const std::string m_name =
                    !members[m]->name.empty() ? members[m]->name : g.key + "[" + std::to_string(m) + "]";
                if (chk_cx) {
                    const float mc = mr.x + mr.width * 0.5f;
                    const float pc = pr.x + pr.width * 0.5f;
                    const float d = std::abs(mc - pc);
                    if (d > kParentTol) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                                      "align_group '%s' rule 'center_x_in_parent': '%s' cx=%.2f != parent cx=%.2f (delta=%.2f)",
                                      g.key.c_str(), m_name.c_str(), mc, pc, d);
                        issues.push_back({"align_group", "center_x_in_parent", m_name, g.key, mr, pr, d, buf});
                    }
                }
                if (chk_cy) {
                    const float mc = mr.y + mr.height * 0.5f;
                    const float pc = pr.y + pr.height * 0.5f;
                    const float d = std::abs(mc - pc);
                    if (d > kParentTol) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                                      "align_group '%s' rule 'center_y_in_parent': '%s' cy=%.2f != parent cy=%.2f (delta=%.2f)",
                                      g.key.c_str(), m_name.c_str(), mc, pc, d);
                        issues.push_back({"align_group", "center_y_in_parent", m_name, g.key, mr, pr, d, buf});
                    }
                }
                if (chk_sx) {
                    const float ml = mr.x - pr.x;
                    const float mrr = (pr.x + pr.width) - (mr.x + mr.width);
                    const float d = std::abs(ml - mrr);
                    if (d > kParentTol) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                                      "align_group '%s' rule 'symmetric_x': '%s' margins L=%.2f R=%.2f (delta=%.2f)",
                                      g.key.c_str(), m_name.c_str(), ml, mrr, d);
                        issues.push_back({"align_group", "symmetric_x", m_name, g.key, mr, pr, d, buf});
                    }
                }
                if (chk_sy) {
                    const float mt = mr.y - pr.y;
                    const float mb = (pr.y + pr.height) - (mr.y + mr.height);
                    const float d = std::abs(mt - mb);
                    if (d > kParentTol) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                                      "align_group '%s' rule 'symmetric_y': '%s' margins T=%.2f B=%.2f (delta=%.2f)",
                                      g.key.c_str(), m_name.c_str(), mt, mb, d);
                        issues.push_back({"align_group", "symmetric_y", m_name, g.key, mr, pr, d, buf});
                    }
                }
                if (chk_fit) {
                    const float out = std::max({pr.x - mr.x, pr.y - mr.y,
                                                (mr.x + mr.width) - (pr.x + pr.width),
                                                (mr.y + mr.height) - (pr.y + pr.height)});
                    if (out > kParentTol) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                                      "align_group '%s' rule 'fit_parent': '%s' exceeds parent by %.2f px",
                                      g.key.c_str(), m_name.c_str(), out);
                        issues.push_back({"align_group", "fit_parent", m_name, g.key, mr, pr, out, buf});
                    }
                }
            }
        }

        if (members.size() < 2) {
            continue;
        }
        const Rect &ref = members.front()->rect;
        const std::string ref_name = !members.front()->name.empty() ? members.front()->name : g.key + "[0]";
        constexpr float kTol = 0.05f;
        const struct RuleCheck {
            const char *name;
            float (*metric)(const Rect &);
        } checks[] = {
            {"left", [](const Rect &r) { return r.x; }},
            {"right", [](const Rect &r) { return r.x + r.width; }},
            {"center_x", [](const Rect &r) { return r.x + r.width * 0.5f; }},
            {"top", [](const Rect &r) { return r.y; }},
            {"bottom", [](const Rect &r) { return r.y + r.height; }},
            {"center_y", [](const Rect &r) { return r.y + r.height * 0.5f; }},
            {"width", [](const Rect &r) { return r.width; }},
            {"height", [](const Rect &r) { return r.height; }},
        };
        for (const auto &rc : checks) {
            if (!HasAlignRule(g.rules, rc.name)) {
                continue;
            }
            const float expected = rc.metric(ref);
            for (std::size_t m = 1; m < members.size(); ++m) {
                const float actual = rc.metric(members[m]->rect);
                const float delta = std::abs(actual - expected);
                if (delta > kTol) {
                    const std::string m_name =
                        !members[m]->name.empty() ? members[m]->name : g.key + "[" + std::to_string(m) + "]";
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "align_group '%s' rule '%s': '%s' (%.2f) != '%s' (%.2f), delta=%.2f",
                                  g.key.c_str(), rc.name, m_name.c_str(), actual, ref_name.c_str(),
                                  expected, delta);
                    issues.push_back({"align_group", rc.name, ref_name, m_name,
                                      ref, members[m]->rect, delta, buf});
                }
            }
        }
        if (HasAlignRule(g.rules, "no_overlap")) {
            for (std::size_t i = 0; i < members.size(); ++i) {
                for (std::size_t j = i + 1; j < members.size(); ++j) {
                    const Rect inter = IntersectRects(members[i]->rect, members[j]->rect);
                    if (inter.width > 0.5f && inter.height > 0.5f) {
                        const std::string na = !members[i]->name.empty() ? members[i]->name : g.key;
                        const std::string nb = !members[j]->name.empty() ? members[j]->name : g.key;
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                                      "align_group '%s' overlap: '%s' & '%s' inter=(%.1fx%.1f)",
                                      g.key.c_str(), na.c_str(), nb.c_str(), inter.width, inter.height);
                        issues.push_back({"align_group", "no_overlap", na, nb,
                                          members[i]->rect, members[j]->rect,
                                          std::min(inter.width, inter.height), buf});
                    }
                }
            }
        }

        // 2b. Group bounding-box centering & uniform spacing checks
        const bool grp_cx = HasAlignRule(g.rules, "group_center_x_in_parent") ||
                            HasAlignRule(g.rules, "group_center_in_parent");
        const bool grp_cy = HasAlignRule(g.rules, "group_center_y_in_parent") ||
                            HasAlignRule(g.rules, "group_center_in_parent");
        if ((grp_cx || grp_cy) && g.parent_rect.width > 0.0f && g.parent_rect.height > 0.0f) {
            float ux0 = members[0]->rect.x, uy0 = members[0]->rect.y;
            float ux1 = ux0 + members[0]->rect.width, uy1 = uy0 + members[0]->rect.height;
            for (std::size_t m = 1; m < members.size(); ++m) {
                const Rect &mr = members[m]->rect;
                ux0 = std::min(ux0, mr.x);
                uy0 = std::min(uy0, mr.y);
                ux1 = std::max(ux1, mr.x + mr.width);
                uy1 = std::max(uy1, mr.y + mr.height);
            }
            const Rect u{ux0, uy0, ux1 - ux0, uy1 - uy0};
            if (grp_cx) {
                const float uc = u.x + u.width * 0.5f;
                const float pc = g.parent_rect.x + g.parent_rect.width * 0.5f;
                const float d = std::abs(uc - pc);
                if (d > 0.5f) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "align_group '%s' rule 'group_center_x_in_parent': group cx=%.2f != parent cx=%.2f (delta=%.2f)",
                                  g.key.c_str(), uc, pc, d);
                    issues.push_back({"align_group", "group_center_x_in_parent", g.key, g.key, u,
                                      g.parent_rect, d, buf});
                }
            }
            if (grp_cy) {
                const float uc = u.y + u.height * 0.5f;
                const float pc = g.parent_rect.y + g.parent_rect.height * 0.5f;
                const float d = std::abs(uc - pc);
                if (d > 0.5f) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "align_group '%s' rule 'group_center_y_in_parent': group cy=%.2f != parent cy=%.2f (delta=%.2f)",
                                  g.key.c_str(), uc, pc, d);
                    issues.push_back({"align_group", "group_center_y_in_parent", g.key, g.key, u,
                                      g.parent_rect, d, buf});
                }
            }
        }
        if (HasAlignRule(g.rules, "equal_spacing_x") && members.size() >= 3) {
            std::vector<const AuditRect *> sorted = members;
            std::sort(sorted.begin(), sorted.end(),
                      [](const AuditRect *a, const AuditRect *b) { return a->rect.x < b->rect.x; });
            const float gap0 = sorted[1]->rect.x - (sorted[0]->rect.x + sorted[0]->rect.width);
            for (std::size_t m = 1; m + 1 < sorted.size(); ++m) {
                const float gap = sorted[m + 1]->rect.x - (sorted[m]->rect.x + sorted[m]->rect.width);
                const float d = std::abs(gap - gap0);
                if (d > 1.0f) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "align_group '%s' rule 'equal_spacing_x': gap[%zu]=%.2f != gap[0]=%.2f (delta=%.2f)",
                                  g.key.c_str(), m, gap, gap0, d);
                    issues.push_back({"align_group", "equal_spacing_x", sorted[m]->name,
                                      sorted[m + 1]->name, sorted[m]->rect, sorted[m + 1]->rect, d, buf});
                }
            }
        }
        if (HasAlignRule(g.rules, "equal_spacing_y") && members.size() >= 3) {
            std::vector<const AuditRect *> sorted = members;
            std::sort(sorted.begin(), sorted.end(),
                      [](const AuditRect *a, const AuditRect *b) { return a->rect.y < b->rect.y; });
            const float gap0 = sorted[1]->rect.y - (sorted[0]->rect.y + sorted[0]->rect.height);
            for (std::size_t m = 1; m + 1 < sorted.size(); ++m) {
                const float gap = sorted[m + 1]->rect.y - (sorted[m]->rect.y + sorted[m]->rect.height);
                const float d = std::abs(gap - gap0);
                if (d > 1.0f) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "align_group '%s' rule 'equal_spacing_y': gap[%zu]=%.2f != gap[0]=%.2f (delta=%.2f)",
                                  g.key.c_str(), m, gap, gap0, d);
                    issues.push_back({"align_group", "equal_spacing_y", sorted[m]->name,
                                      sorted[m + 1]->name, sorted[m]->rect, sorted[m + 1]->rect, d, buf});
                }
            }
        }
    }

    // 3. Pairwise same-layer interactive overlap & near-miss H/V alignment audit
    struct VisItem {
        std::size_t idx;
        Rect vis;
        std::string label;
    };
    std::vector<VisItem> visible;
    visible.reserve(audit_.size());
    for (std::size_t i = 0; i < audit_.size(); ++i) {
        const auto &a = audit_[i];
        if (a.suppressed || !IsInteractiveKind(a.kind) ||
            a.rect.width <= 0.5f || a.rect.height <= 0.5f) {
            continue;
        }
        const Rect vis = IntersectRects(a.rect, a.clip_rect);
        if (vis.width > 1.0f && vis.height > 1.0f) {
            visible.push_back({i, vis, AuditDisplayName(a, i)});
        }
    }

    for (std::size_t i = 0; i < visible.size(); ++i) {
        const auto &va = visible[i];
        const auto &a = audit_[va.idx];
        for (std::size_t j = i + 1; j < visible.size(); ++j) {
            const auto &vb = visible[j];
            const auto &b = audit_[vb.idx];
            if (a.layer != b.layer) {
                continue;
            }
            const Rect inter = IntersectRects(va.vis, vb.vis);
            if (inter.width > 1.5f && inter.height > 1.5f) {
                const float area_a = va.vis.width * va.vis.height;
                const float area_b = vb.vis.width * vb.vis.height;
                const float area_inter = inter.width * inter.height;
                const float min_area = std::min(area_a, area_b);
                const float max_area = std::max(area_a, area_b);
                const bool card_wrapper = (min_area > 0.0f && area_inter / min_area >= 0.85f &&
                                           max_area >= 2.5f * min_area);
                if (!card_wrapper) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "overlap: '%s' (%.1f,%.1f,%.1fx%.1f) vs '%s' (%.1f,%.1f,%.1fx%.1f) inter=(%.1fx%.1f)",
                                  va.label.c_str(), a.rect.x, a.rect.y, a.rect.width, a.rect.height,
                                  vb.label.c_str(), b.rect.x, b.rect.y, b.rect.width, b.rect.height,
                                  inter.width, inter.height);
                    issues.push_back({"overlap", "no_overlap", va.label, vb.label,
                                      a.rect, b.rect, std::min(inter.width, inter.height), buf});
                }
                continue;
            }

            // Horizontal peer strip (side-by-side neighbors) -> top / bottom / center_y alignment
            const bool same_root = (a.root_box_id == b.root_box_id);
            const bool cross_scroll = (!same_root && (a.in_scroll || b.in_scroll));
            const float gap_x = std::max(va.vis.x - (vb.vis.x + vb.vis.width),
                                         vb.vis.x - (va.vis.x + va.vis.width));
            const float ov_y = std::min(va.vis.y + va.vis.height, vb.vis.y + vb.vis.height) -
                               std::max(va.vis.y, vb.vis.y);
            const float max_gap_x = same_root ? 80.0f : 12.0f;
            if (!cross_scroll && gap_x >= -1.0f && gap_x <= max_gap_x &&
                ov_y >= 0.5f * std::min(va.vis.height, vb.vis.height)) {
                const float dt = std::abs(a.rect.y - b.rect.y);
                const float db = std::abs((a.rect.y + a.rect.height) - (b.rect.y + b.rect.height));
                const float dcy = std::abs((a.rect.y + a.rect.height * 0.5f) -
                                           (b.rect.y + b.rect.height * 0.5f));
                const bool same_h = std::abs(a.rect.height - b.rect.height) <= 0.5f;
                const float err = same_h ? dt : std::min({dt, dcy, db});
                const float limit = same_h ? 8.0f : 6.0f;
                if (err > 0.5f && err <= limit) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "align_h: '%s' y=%.1f h=%.1f vs '%s' y=%.1f h=%.1f (off by %.1f px)",
                                  va.label.c_str(), a.rect.y, a.rect.height,
                                  vb.label.c_str(), b.rect.y, b.rect.height, err);
                    issues.push_back({"align_h", same_h ? "top" : "top|center_y|bottom",
                                      va.label, vb.label, a.rect, b.rect, err, buf});
                }
            }

            // Vertical column stack (adjacent above/below neighbors) -> left / right / center_x alignment
            const float gap_y = std::max(va.vis.y - (vb.vis.y + vb.vis.height),
                                         vb.vis.y - (va.vis.y + va.vis.height));
            const float ov_x = std::min(va.vis.x + va.vis.width, vb.vis.x + vb.vis.width) -
                               std::max(va.vis.x, vb.vis.x);
            const bool same_box = (a.box_id == b.box_id && a.box_depth == b.box_depth);
            const bool similar_w = std::min(a.rect.width, b.rect.width) >=
                                   0.65f * std::max(a.rect.width, b.rect.width);
            const bool check_v = !cross_scroll &&
                                 (same_box ||
                                  (same_root && a.box_depth == b.box_depth && similar_w && gap_y <= 24.0f) ||
                                  (!same_root && similar_w && gap_y <= 12.0f));
            if (check_v && gap_y >= -1.0f && gap_y <= 28.0f &&
                ov_x >= 0.5f * std::min(va.vis.width, vb.vis.width)) {
                const float dl = std::abs(a.rect.x - b.rect.x);
                const float dr = std::abs((a.rect.x + a.rect.width) - (b.rect.x + b.rect.width));
                const float dcx = std::abs((a.rect.x + a.rect.width * 0.5f) -
                                           (b.rect.x + b.rect.width * 0.5f));
                const bool same_w = std::abs(a.rect.width - b.rect.width) <= 0.5f;
                const float err = same_w ? dl : std::min({dl, dcx, dr});
                const float limit = same_w ? 8.0f : 6.0f;
                if (err > 0.5f && err <= limit) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "align_v: '%s' x=%.1f w=%.1f vs '%s' x=%.1f w=%.1f (off by %.1f px)",
                                  va.label.c_str(), a.rect.x, a.rect.width,
                                  vb.label.c_str(), b.rect.x, b.rect.width, err);
                    issues.push_back({"align_v", same_w ? "left" : "left|center_x|right",
                                      va.label, vb.label, a.rect, b.rect, err, buf});
                }
            }
        }
    }

    return issues;
}

std::size_t UISystem::LayoutIssueCount() const {
    return AuditLayout().size();
}

void UISystem::BlockInput(const Rect &r) {
    if (suppress_) {
        return;
    }
    if (r.Contains(RawMouse()) && MouseVisible()) {
        pointer_over_ = true;
    }
}

float UISystem::LogicalWidth() const {
    if (reference_w_ > 0.0f) {
        return reference_w_;
    }
    return camera_ ? camera_->ViewportWidth() / std::max(1e-6f, scale_) : 0.0f;
}

float UISystem::LogicalHeight() const {
    if (reference_h_ > 0.0f) {
        return reference_h_;
    }
    return camera_ ? camera_->ViewportHeight() / std::max(1e-6f, scale_) : 0.0f;
}

bool UISystem::HoveredRect(const Rect &r) const {
    if (!CanInteract() || !MouseVisible()) {
        return false;
    }
    return r.Contains(RawMouse());
}

bool UISystem::InvisibleButton(std::string_view key, const Rect &rect, const Color &hover_tint) {
    Record("button", rect, key);
    if (!camera_ || !renderer_ || !input_) {
        return false;
    }
    const std::string key_str(key);
    WidgetState &state = widgets_[key_str];
    const bool hover = rect.Contains(RawMouse()) && CanInteract() && MouseVisible();
    const bool clicked = TrackPressWidget(state, hover);
    if (hover && hover_tint.a > 0.0f) {
        DrawQuadAt(rect, hover_tint);
    }
    return clicked;
}

void UISystem::DrawQuadAt(const Rect &rect, const Color &color) {
    if (!renderer_ || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    renderer_->ClearClipRect();
    renderer_->DrawQuad(MapRect(rect), color);
}

void UISystem::DrawFrameAt(const Rect &rect, const Color &color, float thickness) {
    if (!renderer_ || thickness <= 0.0f || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    DrawQuadAt({rect.x, rect.y, rect.width, thickness}, color);
    DrawQuadAt({rect.x, rect.y + rect.height - thickness, rect.width, thickness}, color);
    DrawQuadAt({rect.x, rect.y, thickness, rect.height}, color);
    DrawQuadAt({rect.x + rect.width - thickness, rect.y, thickness, rect.height}, color);
}

void UISystem::DrawRoundedQuadAt(const Rect &rect, float radius, const Color &color,
                                 const Color &border, float border_w) {
    if (!renderer_ || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    if (InAutoLayout() && !auto_stack_.empty()) {
        AutoCustomCmd cmd;
        cmd.op = AutoCustomCmd::Op::RoundedQuad;
        cmd.rect = rect;
        cmd.radius = radius;
        cmd.fill = color;
        cmd.border = border;
        cmd.thickness = border_w;
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
        return;
    }
    renderer_->ClearClipRect();
    const Rect mr = MapRect(rect);
    const float rr = std::max(0.0f, radius * scale_);
    if (color.a > 0.001f) {
        renderer_->GetSpriteBatch().DrawRoundedQuad(mr, rr, color);
    }
    if (border.a > 0.001f && border_w > 0.0f) {
        renderer_->GetSpriteBatch().DrawRoundedFrame(mr, rr, border_w * scale_, border);
    }
}

void UISystem::DrawRoundedGradientAt(const Rect &rect, float radius, const Color &start_col,
                                     const Color &end_col, bool horizontal) {
    if (!renderer_ || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    if (InAutoLayout() && !auto_stack_.empty()) {
        AutoCustomCmd cmd;
        cmd.op = AutoCustomCmd::Op::RoundedGrad;
        cmd.rect = rect;
        cmd.radius = radius;
        cmd.fill = start_col;
        cmd.col2 = end_col;
        cmd.horizontal = horizontal;
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
        return;
    }
    renderer_->ClearClipRect();
    renderer_->GetSpriteBatch().DrawRoundedGradientQuad(MapRect(rect), std::max(0.0f, radius * scale_),
                                                        start_col, end_col, horizontal);
}

void UISystem::DrawRoundedFrameAt(const Rect &rect, float radius, float thickness, const Color &color) {
    if (!renderer_ || thickness <= 0.0f || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    if (InAutoLayout() && !auto_stack_.empty()) {
        AutoCustomCmd cmd;
        cmd.op = AutoCustomCmd::Op::RoundedFrame;
        cmd.rect = rect;
        cmd.radius = radius;
        cmd.thickness = thickness;
        cmd.fill = color;
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
        return;
    }
    renderer_->ClearClipRect();
    renderer_->GetSpriteBatch().DrawRoundedFrame(MapRect(rect), std::max(0.0f, radius * scale_),
                                                 thickness * scale_, color);
}

void UISystem::DrawDropShadowAt(const Rect &rect, float radius, float off_y, float spread, float alpha) {
    if (!renderer_ || rect.width <= 0.0f || rect.height <= 0.0f || alpha <= 0.001f) {
        return;
    }
    if (InAutoLayout() && !auto_stack_.empty()) {
        AutoCustomCmd cmd;
        cmd.op = AutoCustomCmd::Op::DropShadow;
        cmd.rect = rect;
        cmd.radius = radius;
        cmd.off_y = off_y;
        cmd.spread = spread;
        cmd.alpha = alpha;
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
        return;
    }
    renderer_->ClearClipRect();
    renderer_->GetSpriteBatch().DrawDropShadow(MapRect(rect), std::max(0.0f, radius * scale_),
                                               off_y * scale_, spread * scale_, alpha);
}

void UISystem::DrawSpriteRect(const Texture2D *texture, const Rect &src, const Rect &dst,
                              const Color &tint) {
    if (!renderer_ || !texture || !texture->IsValid() || dst.width <= 0.0f || dst.height <= 0.0f) {
        return;
    }
    Color col = tint;
    if (IsDisabled()) {
        col.a *= 0.4f;
    }
    renderer_->ClearClipRect();
    renderer_->DrawSprite(*texture, src, MapRect(dst), col);
}

float UISystem::MeasureTextPx(std::string_view text, float px, int face) const {
    if (text_hooks_.measure) {
        return text_hooks_.measure(text, px, face);
    }
    return renderer_ ? renderer_->MeasureText(text, px / 32.0f) : 0.0f;
}

float UISystem::MeasureWrappedTextPx(std::string_view text, float px, float wrap_w, int face) const {
    if (text_hooks_.measure_wrapped) {
        return text_hooks_.measure_wrapped(text, px, wrap_w, face);
    }
    return renderer_ ? renderer_->GetFont().MeasureTextEx(text, px / 32.0f, wrap_w) : 0.0f;
}

float UISystem::DrawTextPx(float x, float y, std::string_view text, float px,
                           const Color &color, int align, int face) {
    if (!renderer_ || text.empty()) {
        return 0.0f;
    }
    const float w = MeasureTextPx(text, px, face);
    if (InAutoLayout()) {
        AutoCustomCmd cmd;
        cmd.op = AutoCustomCmd::Op::Text;
        cmd.rect = {x, y, w, px};
        cmd.text = std::string(text);
        cmd.px = px;
        cmd.fill = color;
        cmd.align = align;
        cmd.face = face;
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
        return w;
    }
    renderer_->ClearClipRect();
    const float yy = y * scale_ + scale_offset_.y;
    const float xx = x * scale_ + scale_offset_.x;
    if (text_hooks_.draw) {
        text_hooks_.draw(renderer_->GetSpriteBatch(), text, xx, yy, px * scale_, color, align, face);
    } else {
        const float sc = (px / 32.0f) * scale_;
        const float mw = renderer_->MeasureText(text, sc);
        const float ax = align == 1 ? (xx - mw * 0.5f) : (align == 2 ? xx - mw : xx);
        renderer_->DrawTextEx(text, ax, yy, sc, 0, 0, color);
    }
    return w;
}

float UISystem::DrawWrappedTextPx(float x, float y, float wrap_w, std::string_view text, float px,
                                  const Color &color, int face) {
    if (!renderer_ || text.empty()) {
        return 0.0f;
    }
    const float h = MeasureWrappedTextPx(text, px, wrap_w, face);
    if (InAutoLayout()) {
        AutoCustomCmd cmd;
        cmd.op = AutoCustomCmd::Op::WrappedText;
        cmd.rect = {x, y, wrap_w, h};
        cmd.text = std::string(text);
        cmd.px = px;
        cmd.fill = color;
        cmd.face = face;
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].custom_cmds.push_back(std::move(cmd));
        return h;
    }
    renderer_->ClearClipRect();
    const float yy = y * scale_ + scale_offset_.y;
    const float xx = x * scale_ + scale_offset_.x;
    if (text_hooks_.draw_wrapped) {
        text_hooks_.draw_wrapped(renderer_->GetSpriteBatch(), text, xx, yy,
                                 px * scale_, wrap_w * scale_, color, face);
    } else {
        renderer_->DrawTextEx(text, xx, yy, (px / 32.0f) * scale_, 0, wrap_w * scale_, color);
    }
    return h;
}

const Texture2D *UITextureCache::GetOrLoad(const std::string &name,
                                           const std::string &root_dir,
                                           const std::string &default_subdir) {
    if (name.empty()) {
        return nullptr;
    }
    if (const auto it = cache_.find(name); it != cache_.end()) {
        return it->second.get();
    }
    if (failed_.count(name)) {
        return nullptr;
    }
    std::string rel = (name.find('/') != std::string::npos) ? name : (default_subdir + name);
    std::string path = root_dir.empty() ? rel : (root_dir + "/" + rel);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        failed_[name] = true;
        return nullptr;
    }
    auto tex = std::make_unique<Texture2D>();
    if (!tex->LoadFromFile(path, TextureFilter::Linear, TextureWrap::Clamp)) {
        failed_[name] = true;
        return nullptr;
    }
    const Texture2D *ptr = tex.get();
    cache_.emplace(name, std::move(tex));
    return ptr;
}

void UITextureCache::Clear() {
    cache_.clear();
    failed_.clear();
}

void UITextureCache::EvictPrefix(std::string_view prefix) {
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->first.rfind(prefix, 0) == 0) it = cache_.erase(it);
        else ++it;
    }
    for (auto it = failed_.begin(); it != failed_.end();) {
        if (it->first.rfind(prefix, 0) == 0) it = failed_.erase(it);
        else ++it;
    }
}

void UITipTracker::BeginFrame() {
    scroll_stack_.clear();
    entries_.clear();
    polled_rect_ = {};
    fallback_tip_.clear();
}

void UITipTracker::PushScroll(std::string key, const Rect &viewport, float offset) {
    Rect vp = viewport;
    for (const auto &sc : scroll_stack_) {
        vp = RectIntersectHelper(vp, sc.viewport);
    }
    scroll_stack_.push_back({std::move(key), vp, offset});
}

void UITipTracker::PopScroll() {
    if (!scroll_stack_.empty()) {
        scroll_stack_.pop_back();
    }
}

float UITipTracker::CurrentScrollOffset() const {
    float off = 0.0f;
    for (const auto &s : scroll_stack_) {
        off += s.offset;
    }
    return off;
}

Rect UITipTracker::ToScreenRect(Rect r) const {
    r.y -= CurrentScrollOffset();
    for (const auto &s : scroll_stack_) {
        r = RectIntersectHelper(r, s.viewport);
    }
    return r;
}

void UITipTracker::RecordEntry(const Rect &r, std::string text) {
    if (text.empty() || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    entries_.push_back({r, std::move(text)});
}

void UITipTracker::RecordClippedEntry(const Rect &r, std::string text) {
    if (text.empty()) {
        return;
    }
    RecordEntry(ToScreenRect(r), std::move(text));
}

void UITipTracker::RecordOccluder(const Rect &r) {
    if (r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    entries_.push_back({r, std::string{}});
}

std::string UITipTracker::Poll(Vec2 mouse, std::string_view fallback_override) {
    if (mouse.x < -1000.0f || mouse.y < -1000.0f) {
        polled_rect_ = {};
        return {};
    }
    for (int i = static_cast<int>(entries_.size()) - 1; i >= 0; --i) {
        const auto &e = entries_[static_cast<std::size_t>(i)];
        if (e.rect.width > 0.0f && e.rect.height > 0.0f && e.rect.Contains(mouse)) {
            if (e.text.empty()) {
                polled_rect_ = {};
                return {};
            }
            polled_rect_ = e.rect;
            return e.text;
        }
    }
    const std::string_view fb = fallback_override.empty() ? std::string_view(fallback_tip_) : fallback_override;
    if (!fb.empty()) {
        polled_rect_ = {mouse.x - 10.0f, mouse.y - 10.0f, 20.0f, 20.0f};
        return std::string(fb);
    }
    polled_rect_ = {};
    return {};
}

bool EnsureSystemCjkFonts(Renderer &renderer, const std::string &primary_font_path, float pixel_height) {
    static const char *const kSysFonts[] = {
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "C:/Windows/Fonts/msyh.ttc",
    };
    bool ok = TryLoadFontPathHelper(renderer, primary_font_path, pixel_height);
    if (!ok) {
        for (const char *p : kSysFonts) {
            if (TryLoadFontPathHelper(renderer, p, pixel_height)) {
                ok = true;
                break;
            }
        }
    }
    if (!ok) {
        return false;
    }
    for (const char *p : kSysFonts) {
        renderer.AddFallbackFont(p, pixel_height);
    }
    return true;
}

} // namespace fake2d
