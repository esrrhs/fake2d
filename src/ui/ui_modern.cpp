#include "fake2d/ui.h"
#include "fake2d/ease.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fake2d {

namespace {

constexpr Color kScrollThumbModern{0.22f, 0.30f, 0.48f, 1.0f};
constexpr float kPiOver2 = 1.57079632679f;
constexpr float kDragThresholdPx = 5.0f;

std::vector<Vec2> BuildRoundedRectContour(const Rect &r, float radius) {
    const float max_r = std::min(r.width, r.height) * 0.5f;
    const float rad = std::clamp(radius, 0.0f, std::max(0.0f, max_r));
    constexpr int kSegs = 6;
    std::vector<Vec2> pts;
    pts.reserve(4 * (kSegs + 1));
    const float cxs[4] = {r.x + r.width - rad, r.x + r.width - rad, r.x + rad, r.x + rad};
    const float cys[4] = {r.y + rad, r.y + r.height - rad, r.y + r.height - rad, r.y + rad};
    const float a0s[4] = {-kPiOver2, 0.0f, kPiOver2, 2.0f * kPiOver2};
    for (int corner = 0; corner < 4; ++corner) {
        for (int s = 0; s <= kSegs; ++s) {
            const float a = a0s[corner] + kPiOver2 * (static_cast<float>(s) / kSegs);
            pts.push_back({cxs[corner] + std::cos(a) * rad, cys[corner] + std::sin(a) * rad});
        }
    }
    return pts;
}

Color LerpColor(const Color &a, const Color &b, float t) {
    const float u = std::clamp(t, 0.0f, 1.0f);
    return {
        a.r + (b.r - a.r) * u,
        a.g + (b.g - a.g) * u,
        a.b + (b.b - a.b) * u,
        a.a + (b.a - a.a) * u,
    };
}

void AppendJsonEscaped(std::string &out, std::string_view sv) {
    out.push_back('"');
    for (const char c : sv) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

void SerializeSemanticJsonRec(std::string &out, const UISemanticNode &n) {
    out += "{\"key\":";
    AppendJsonEscaped(out, n.key);
    out += ",\"role\":";
    AppendJsonEscaped(out, n.role);
    out += ",\"label\":";
    AppendJsonEscaped(out, n.label);
    out += ",\"value\":";
    AppendJsonEscaped(out, n.value);
    out += ",\"hint\":";
    AppendJsonEscaped(out, n.hint);
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  ",\"rect\":[%.1f,%.1f,%.1f,%.1f],\"enabled\":%s,\"focused\":%s,\"checked\":%s",
                  n.rect.x, n.rect.y, n.rect.width, n.rect.height, n.enabled ? "true" : "false",
                  n.focused ? "true" : "false", n.checked ? "true" : "false");
    out += buf;
    out += ",\"actions\":[";
    for (std::size_t i = 0; i < n.actions.size(); ++i) {
        if (i > 0) out.push_back(',');
        AppendJsonEscaped(out, n.actions[i]);
    }
    out += "],\"children\":[";
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        if (i > 0) out.push_back(',');
        SerializeSemanticJsonRec(out, n.children[i]);
    }
    out += "]}";
}

bool AcceptListMatches(std::string_view accepted_types, std::string_view drag_type) {
    if (accepted_types.empty() || accepted_types == "*") {
        return true;
    }
    if (drag_type.empty()) {
        return false;
    }
    std::size_t pos = 0;
    while (pos <= accepted_types.size()) {
        const std::size_t sep = accepted_types.find_first_of("|,", pos);
        const std::size_t end = sep == std::string_view::npos ? accepted_types.size() : sep;
        std::string_view tok = accepted_types.substr(pos, end - pos);
        while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
        while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);
        if (tok == drag_type || tok == "*") {
            return true;
        }
        if (sep == std::string_view::npos) break;
        pos = sep + 1;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Hierarchical ID Scoping
// ---------------------------------------------------------------------------

void UISystem::PushID(std::string_view id) {
    id_stack_.emplace_back(id);
}

void UISystem::PushID(int id) {
    id_stack_.push_back(std::to_string(id));
}

void UISystem::PopID() {
    if (!id_stack_.empty()) {
        id_stack_.pop_back();
    }
}

std::string UISystem::ScopedKey(std::string_view key) const {
    if (key.empty()) {
        return {};
    }
    if (key.front() == '/') {
        return std::string(key.substr(1));
    }
    if (id_stack_.empty()) {
        return std::string(key);
    }
    std::string out;
    for (const auto &part : id_stack_) {
        out += part;
        out.push_back('/');
    }
    out.append(key);
    return out;
}

std::string UISystem::ResolveKeyLookup(std::string_view key) const {
    if (key.empty()) {
        return {};
    }
    if (!id_stack_.empty() && key.front() != '/') {
        const std::string scoped = ScopedKey(key);
        if (frame_results_.count(scoped) || widget_rects_.count(scoped) || widgets_.count(scoped) ||
            scroll_states_.count(scoped) || prev_solved_widgets_.count(scoped) ||
            curr_solved_widgets_.count(scoped) || drop_results_.count(scoped)) {
            return scoped;
        }
        return scoped;
    }
    if (key.front() == '/') {
        return std::string(key.substr(1));
    }
    return std::string(key);
}

// ---------------------------------------------------------------------------
// Focus Scope, Programmatic Focus, Direction, SafeArea, Scroll-Into-View
// ---------------------------------------------------------------------------

void UISystem::FocusScopeBegin(std::string_view key, bool trap) {
    const std::string scoped = ScopedKey(key);
    focus_scopes_.push_back({scoped, nav_focus_, trap});
    active_focus_scope_keys_.push_back(scoped);
}

void UISystem::FocusScopeEnd() {
    if (!active_focus_scope_keys_.empty()) {
        active_focus_scope_keys_.pop_back();
    }
    if (!focus_scopes_.empty()) {
        focus_scopes_.pop_back();
    }
}

void UISystem::RequestFocus(std::string_view key) {
    const std::string k = ResolveKeyLookup(key);
    nav_focus_ = k;
    saved_nav_focus_ = k;
    auto it = std::find_if(focus_ring_.begin(), focus_ring_.end(),
                           [&](const FocusEntry &e) { return e.key == k; });
    if (it == focus_ring_.end()) {
        it = std::find_if(prior_focus_.begin(), prior_focus_.end(),
                          [&](const FocusEntry &e) { return e.key == k; });
        if (it != prior_focus_.end()) {
            focused_ = it->text ? k : std::string{};
            saved_text_focus_ = focused_;
            EnsureFocusedVisible();
            return;
        }
    } else {
        focused_ = it->text ? k : std::string{};
        saved_text_focus_ = focused_;
        EnsureFocusedVisible();
        return;
    }
    // Also activate text focus eagerly if widget record looks like text or exists
    for (const auto &n : auto_nodes_) {
        if (n.key == k) {
            const bool is_txt = (n.kind == AutoNode::Kind::TextInput ||
                                 n.kind == AutoNode::Kind::TextArea ||
                                 n.kind == AutoNode::Kind::TextPassword);
            focused_ = is_txt ? k : std::string{};
            saved_text_focus_ = focused_;
            break;
        }
    }
    EnsureFocusedVisible();
}

void UISystem::ClearFocus() {
    nav_focus_.clear();
    focused_.clear();
    saved_nav_focus_.clear();
    saved_text_focus_.clear();
}

void UISystem::ScrollToWidget(std::string_view key, bool smooth) {
    pending_scroll_key_ = ResolveKeyLookup(key);
    pending_scroll_smooth_ = smooth;
    if (pending_scroll_key_.empty()) {
        return;
    }
    for (std::size_t idx = 0; idx < auto_nodes_.size(); ++idx) {
        if (auto_nodes_[idx].key != pending_scroll_key_) {
            continue;
        }
        const Rect target_r = auto_nodes_[idx].rect;
        int p = auto_nodes_[idx].parent;
        while (p >= 0 && static_cast<std::size_t>(p) < auto_nodes_.size()) {
            const auto &anc = auto_nodes_[static_cast<std::size_t>(p)];
            if (anc.kind == AutoNode::Kind::Scroll && !anc.key.empty()) {
                auto &sc = scroll_states_[anc.key];
                const Rect vp = anc.content_rect;
                const float sb = ThemeMetric("scrollbar_width");
                const float view_h = std::max(1.0f, vp.height);
                const float max_y = std::max(0.0f, anc.content_height - view_h);
                float next_y = sc.offset;
                if (target_r.y < vp.y) {
                    next_y -= (vp.y - target_r.y);
                } else if (target_r.y + target_r.height > vp.y + view_h) {
                    next_y += (target_r.y + target_r.height) - (vp.y + view_h);
                }
                next_y = std::clamp(next_y, 0.0f, max_y);
                if (smooth) {
                    sc.target_offset = next_y;
                    sc.has_target = std::abs(next_y - sc.offset) > 0.5f;
                } else {
                    sc.offset = next_y;
                    sc.has_target = false;
                }
                if (anc.scroll_dir == UIScrollDir::Horizontal || anc.scroll_dir == UIScrollDir::Both) {
                    const float view_w = std::max(1.0f, vp.width - (anc.scroll_dir == UIScrollDir::Both ? sb : 0.0f));
                    const float max_x = std::max(0.0f, anc.content_width - view_w);
                    float next_x = sc.offset_x;
                    if (target_r.x < vp.x) {
                        next_x -= (vp.x - target_r.x);
                    } else if (target_r.x + target_r.width > vp.x + view_w) {
                        next_x += (target_r.x + target_r.width) - (vp.x + view_w);
                    }
                    next_x = std::clamp(next_x, 0.0f, max_x);
                    if (smooth) {
                        sc.target_offset_x = next_x;
                        sc.has_target_x = std::abs(next_x - sc.offset_x) > 0.5f;
                    } else {
                        sc.offset_x = next_x;
                        sc.has_target_x = false;
                    }
                }
                pending_scroll_key_.clear();
                break;
            }
            p = anc.parent;
        }
        break;
    }
}

void UISystem::EnsureFocusedVisible() {
    if (!nav_focus_.empty()) {
        ScrollToWidget(nav_focus_, false);
    }
}

float UISystem::PinchDelta() const {
    return input_ ? static_cast<float>(input_->PinchDelta()) : 0.0f;
}

UILayoutDirection UISystem::LayoutDirection() const {
    return dir_stack_.empty() ? default_layout_dir_ : dir_stack_.back();
}

void UISystem::LayoutDirectionBegin(UILayoutDirection dir) {
    dir_stack_.push_back(dir);
}

void UISystem::LayoutDirectionEnd() {
    if (!dir_stack_.empty()) {
        dir_stack_.pop_back();
    }
}

void UISystem::SetSafeAreaInsets(float left, float top, float right, float bottom) {
    safe_area_ = {std::max(0.0f, left), std::max(0.0f, top),
                  std::max(0.0f, right), std::max(0.0f, bottom)};
}

void UISystem::SafeAreaBegin(float extra_pad) {
    const int idx = AllocAutoNode(AutoNode::Kind::SafeArea);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.pad_l += safe_area_.left + std::max(0.0f, extra_pad);
    node.pad_t += safe_area_.top + std::max(0.0f, extra_pad);
    node.pad_r += safe_area_.right + std::max(0.0f, extra_pad);
    node.pad_b += safe_area_.bottom + std::max(0.0f, extra_pad);
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Grow(1.0f);
    node.cross_align = UIAxisAlign::Stretch;
    node.est_avail_w = std::max(0.0f, node.est_avail_w - node.pad_l - node.pad_r);
    auto_stack_.push_back(idx);
}

void UISystem::SafeAreaEnd() {
    CloseAutoNode();
}

// ---------------------------------------------------------------------------
// IME Cursor Rect Tracking
// ---------------------------------------------------------------------------

void UISystem::UpdateImeCaret(const Rect &caret_logical_rect) {
    ime_cursor_rect_ = caret_logical_rect;
    if (ime_rect_cb_) {
        const bool diff = std::abs(ime_cursor_rect_.x - last_emitted_ime_rect_.x) > 0.5f ||
                          std::abs(ime_cursor_rect_.y - last_emitted_ime_rect_.y) > 0.5f ||
                          std::abs(ime_cursor_rect_.height - last_emitted_ime_rect_.height) > 0.5f;
        if (diff) {
            last_emitted_ime_rect_ = ime_cursor_rect_;
            ime_rect_cb_(ime_cursor_rect_);
        }
    }
}

// ---------------------------------------------------------------------------
// Modern Visual Primitives (Rounded Rect, Gradient, Border, Shadow, Blur)
// ---------------------------------------------------------------------------

void UISystem::DrawRoundedQuad(const Rect &r, float radius, const Color &color) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f || color.a <= 0.0f) {
        return;
    }
    if (radius <= 0.5f) {
        renderer_->DrawQuad(MapRect(r), color);
        return;
    }
    const Rect mr = MapRect(r);
    const auto contour = BuildRoundedRectContour(mr, radius * scale_);
    const std::size_t n = contour.size();
    std::vector<Vertex2D> verts(n + 1);
    verts[0] = {{mr.x + mr.width * 0.5f, mr.y + mr.height * 0.5f}, {0.5f, 0.5f}, color};
    for (std::size_t i = 0; i < n; ++i) {
        verts[i + 1] = {contour[i], {0.5f, 0.5f}, color};
    }
    std::vector<std::uint16_t> indices;
    indices.reserve(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<std::uint16_t>(i + 1));
        indices.push_back(static_cast<std::uint16_t>((i + 1) % n + 1));
    }
    renderer_->GetSpriteBatch().DrawTriangles(
        Texture2D::White(), verts.data(), verts.size(), indices.data(), indices.size());
}

void UISystem::DrawRoundedGradientQuad(const Rect &r, float radius, const Color &start_col,
                                       const Color &end_col, bool horizontal) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    if (start_col.a <= 0.0f && end_col.a <= 0.0f) {
        return;
    }
    if (radius <= 0.5f) {
        if (horizontal) {
            renderer_->DrawGradientQuad(MapRect(r), start_col, end_col, end_col, start_col);
        } else {
            renderer_->DrawGradientQuad(MapRect(r), start_col, start_col, end_col, end_col);
        }
        return;
    }
    const Rect mr = MapRect(r);
    const auto contour = BuildRoundedRectContour(mr, radius * scale_);
    const std::size_t n = contour.size();
    std::vector<Vertex2D> verts(n + 1);
    const Color mid = LerpColor(start_col, end_col, 0.5f);
    verts[0] = {{mr.x + mr.width * 0.5f, mr.y + mr.height * 0.5f}, {0.5f, 0.5f}, mid};
    for (std::size_t i = 0; i < n; ++i) {
        const float t = horizontal
                            ? (mr.width > 0.0f ? (contour[i].x - mr.x) / mr.width : 0.0f)
                            : (mr.height > 0.0f ? (contour[i].y - mr.y) / mr.height : 0.0f);
        verts[i + 1] = {contour[i], {0.5f, 0.5f}, LerpColor(start_col, end_col, t)};
    }
    std::vector<std::uint16_t> indices;
    indices.reserve(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<std::uint16_t>(i + 1));
        indices.push_back(static_cast<std::uint16_t>((i + 1) % n + 1));
    }
    renderer_->GetSpriteBatch().DrawTriangles(
        Texture2D::White(), verts.data(), verts.size(), indices.data(), indices.size());
}

void UISystem::DrawRoundedFrame(const Rect &r, float radius, float thickness, const Color &color) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f || thickness <= 0.0f || color.a <= 0.0f) {
        return;
    }
    if (radius <= 0.5f) {
        const Rect mr = MapRect(r);
        const float t = std::max(1.0f, thickness * scale_);
        renderer_->GetSpriteBatch().DrawFrame(mr, color, t);
        return;
    }
    const Rect outer_r = MapRect(r);
    const float t = std::min({thickness * scale_, outer_r.width * 0.45f, outer_r.height * 0.45f});
    const Rect inner_r{outer_r.x + t, outer_r.y + t, std::max(0.0f, outer_r.width - 2.0f * t),
                       std::max(0.0f, outer_r.height - 2.0f * t)};
    const auto outer = BuildRoundedRectContour(outer_r, radius * scale_);
    const auto inner = BuildRoundedRectContour(inner_r, std::max(0.0f, radius * scale_ - t));
    const std::size_t n = std::min(outer.size(), inner.size());
    std::vector<Vertex2D> verts(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        verts[i * 2] = {outer[i], {0.5f, 0.5f}, color};
        verts[i * 2 + 1] = {inner[i], {0.5f, 0.5f}, color};
    }
    std::vector<std::uint16_t> indices;
    indices.reserve(n * 6);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint16_t o0 = static_cast<std::uint16_t>(i * 2);
        const std::uint16_t i0 = static_cast<std::uint16_t>(i * 2 + 1);
        const std::uint16_t o1 = static_cast<std::uint16_t>(((i + 1) % n) * 2);
        const std::uint16_t i1 = static_cast<std::uint16_t>(((i + 1) % n) * 2 + 1);
        indices.push_back(o0);
        indices.push_back(o1);
        indices.push_back(i0);
        indices.push_back(o1);
        indices.push_back(i1);
        indices.push_back(i0);
    }
    renderer_->GetSpriteBatch().DrawTriangles(
        Texture2D::White(), verts.data(), verts.size(), indices.data(), indices.size());
}

void UISystem::DrawDropShadow(const Rect &r, float radius, float off_y, float spread, float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f || alpha <= 0.0f) {
        return;
    }
    const int layers = 3;
    for (int l = layers; l >= 1; --l) {
        const float frac = static_cast<float>(l) / static_cast<float>(layers);
        const float ex = spread * frac;
        const Rect sr{r.x - ex, r.y + off_y - ex * 0.5f, r.width + 2.0f * ex, r.height + 2.0f * ex};
        const Color c{0.0f, 0.0f, 0.0f, alpha * (1.0f - frac * 0.65f) / static_cast<float>(layers)};
        DrawRoundedQuad(sr, radius + ex * 0.5f, c);
    }
}

void UISystem::DrawNodeBackdrop(const Rect &r, const AutoNode &node) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    if (node.has_shadow) {
        DrawDropShadow(r, node.radius, node.shadow_off_y, node.shadow_spread,
                       node.shadow_alpha * node.alpha_mul);
    }
    if (node.backdrop_blur > 0.0f) {
        const Rect &pc = node.clip_rect;
        const float cx0 = std::max(pc.x, r.x), cy0 = std::max(pc.y, r.y);
        const float cx1 = std::min(pc.x + pc.width, r.x + r.width);
        const float cy1 = std::min(pc.y + pc.height, r.y + r.height);
        const Rect blur_clip{cx0, cy0, std::max(0.0f, cx1 - cx0), std::max(0.0f, cy1 - cy0)};
        ApplyClipRectDirect(blur_clip, node.radius);
        Color tint = node.has_bg ? node.bg_col : Color{0.16f, 0.20f, 0.28f, 0.28f};
        tint.a *= node.alpha_mul;
        renderer_->DrawBlurredBackdrop(MapRect(r), node.backdrop_blur * scale_, tint);
        ApplyClipRectDirect(node.clip_rect, node.clip_radius);
    }
    if (node.has_grad) {
        Color gs = node.grad_start;
        Color ge = node.grad_end;
        gs.a *= node.alpha_mul;
        ge.a *= node.alpha_mul;
        DrawRoundedGradientQuad(r, node.radius, gs, ge, node.grad_horiz);
    } else if (node.has_bg && node.backdrop_blur <= 0.0f) {
        Color bg = node.bg_col;
        bg.a *= node.alpha_mul;
        DrawRoundedQuad(r, node.radius, bg);
    }
    if (node.has_border && node.border_w > 0.0f) {
        Color br = node.custom_border_col;
        br.a *= node.alpha_mul;
        DrawRoundedFrame(r, node.radius, node.border_w, br);
    }
}

void UISystem::StepWidgetTransition(WidgetState &st, bool hover, bool pressed, bool focused) {
    const float ms = ThemeMetric("state_transition_ms");
    if (ms <= 0.5f) {
        st.hover_t = hover ? 1.0f : 0.0f;
        st.press_t = pressed ? 1.0f : 0.0f;
        st.focus_t = focused ? 1.0f : 0.0f;
        return;
    }
    const float step = std::clamp(frame_dt_ / (ms * 0.001f), 0.0f, 1.0f);
    auto approach = [step](float &cur, bool target) {
        const float goal = target ? 1.0f : 0.0f;
        if (cur < goal) cur = std::min(goal, cur + step);
        else if (cur > goal) cur = std::max(goal, cur - step);
    };
    approach(st.hover_t, hover);
    approach(st.press_t, pressed);
    approach(st.focus_t, focused);
}

void UISystem::DrawHScrollbarVisual(const Rect &vp, float content_w, float offset_x, bool hover,
                                    bool dragging, float radius, float alpha) {
    if (!renderer_ || content_w <= vp.width || vp.width <= 0.0f) {
        return;
    }
    const float bar_h = 10.0f;
    const float max_scroll = std::max(1e-4f, content_w - vp.width);
    const Rect bar{vp.x, vp.y + vp.height - bar_h, vp.width, bar_h};
    const float thumb_w = std::min(vp.width, std::max(24.0f, vp.width * (vp.width / content_w)));
    const float track = vp.width - thumb_w;
    const float drawn = vp.x + std::clamp(offset_x / max_scroll, 0.0f, 1.0f) * track;
    Color tc = ThemeColor("track");
    tc.a *= alpha;
    Color th = dragging ? ThemeColor("accent") : (hover ? ThemeColor("hover") : kScrollThumbModern);
    th.a *= alpha;
    const float r = radius > 0.5f ? std::min(radius, bar_h * 0.5f) : 0.0f;
    DrawRoundedQuad(bar, r, tc);
    DrawRoundedQuad({drawn, bar.y + 1.0f, thumb_w, bar_h - 2.0f}, r, th);
}

// ---------------------------------------------------------------------------
// Implicit Layout Transitions
// ---------------------------------------------------------------------------

Rect UISystem::ApplyTransitionBox(const AutoNode &node, const Rect &target_rect) {
    if (node.anim_duration <= 0.0f || node.anim_key.empty()) {
        return target_rect;
    }
    auto &st = anim_boxes_[node.anim_key];
    if (!st.init) {
        st.current = target_rect;
        st.target = target_rect;
        st.elapsed = node.anim_duration;
        st.duration = node.anim_duration;
        st.init = true;
        return target_rect;
    }
    const bool changed = std::abs(target_rect.x - st.target.x) > 0.5f ||
                         std::abs(target_rect.y - st.target.y) > 0.5f ||
                         std::abs(target_rect.width - st.target.width) > 0.5f ||
                         std::abs(target_rect.height - st.target.height) > 0.5f;
    if (changed) {
        st.target = target_rect;
        st.elapsed = 0.0f;
        st.duration = node.anim_duration;
    }
    if (st.elapsed >= st.duration) {
        st.current = target_rect;
        return target_rect;
    }
    st.elapsed = std::min(st.duration, st.elapsed + std::max(0.001f, frame_dt_));
    const float t = st.duration > 0.0f ? ease::OutCubic(std::clamp(st.elapsed / st.duration, 0.0f, 1.0f)) : 1.0f;
    st.current = {
        st.current.x + (target_rect.x - st.current.x) * t,
        st.current.y + (target_rect.y - st.current.y) * t,
        st.current.width + (target_rect.width - st.current.width) * t,
        st.current.height + (target_rect.height - st.current.height) * t,
    };
    return st.current;
}

// ---------------------------------------------------------------------------
// Subtree Layout Cache (Capture & Replay)
// ---------------------------------------------------------------------------

void UISystem::CaptureSubtreeCache(int cached_box_idx) {
    if (cached_box_idx < 0 || static_cast<std::size_t>(cached_box_idx) >= auto_nodes_.size()) {
        return;
    }
    const auto &root_node = auto_nodes_[static_cast<std::size_t>(cached_box_idx)];
    if (root_node.key.empty() || root_node.cached_hit) {
        return;
    }
    CachedLayoutSnapshot snap;
    snap.hash = root_node.cache_hash;
    snap.est_avail_w = root_node.est_avail_w;
    snap.font_scale = ThemeMetric("font_scale");
    snap.origin_x = root_node.rect.x;
    snap.origin_y = root_node.rect.y;
    snap.meas_w = root_node.meas_w;
    snap.meas_h = root_node.meas_h;
    snap.baseline = root_node.baseline;

    std::function<int(int)> clone_rec = [&](int src_idx) -> int {
        const int dst_idx = static_cast<int>(snap.nodes.size());
        snap.nodes.push_back(auto_nodes_[static_cast<std::size_t>(src_idx)]);
        auto orig_children = snap.nodes[static_cast<std::size_t>(dst_idx)].children;
        snap.nodes[static_cast<std::size_t>(dst_idx)].children.clear();
        for (const int cid : orig_children) {
            const int child_dst = clone_rec(cid);
            snap.nodes[static_cast<std::size_t>(dst_idx)].children.push_back(child_dst);
            snap.nodes[static_cast<std::size_t>(child_dst)].parent = dst_idx;
        }
        return dst_idx;
    };
    clone_rec(cached_box_idx);
    layout_cache_[root_node.key] = std::move(snap);
}

bool UISystem::ReplaySubtreeCache(int cached_box_idx, float origin_x, float origin_y,
                                  const Rect &parent_clip, float parent_clip_radius, bool in_scroll,
                                  std::uint32_t root_id) {
    auto it = layout_cache_.find(auto_nodes_[static_cast<std::size_t>(cached_box_idx)].key);
    if (it == layout_cache_.end() || it->second.nodes.empty()) {
        return false;
    }
    const auto &snap = it->second;
    const float dx = origin_x - snap.origin_x;
    const float dy = origin_y - snap.origin_y;
    const std::size_t base_idx = auto_nodes_.size();

    auto &target = auto_nodes_[static_cast<std::size_t>(cached_box_idx)];
    const int saved_parent = target.parent;
    const std::uint32_t saved_box_depth = target.box_depth;

    auto shift_rect = [dx, dy](Rect r) {
        r.x += dx;
        r.y += dy;
        return r;
    };

    for (std::size_t i = 1; i < snap.nodes.size(); ++i) {
        AutoNode copy = snap.nodes[i];
        copy.box_id = box_sequence_++;
        copy.root_box_id = root_id;
        copy.in_scroll = in_scroll || copy.in_scroll;
        copy.rect = shift_rect(copy.rect);
        copy.content_rect = shift_rect(copy.content_rect);
        copy.clip_rect = parent_clip;
        copy.clip_radius = parent_clip_radius;
        for (auto &cmd : copy.custom_cmds) {
            if (cmd.op == AutoCustomCmd::Op::Quad || cmd.op == AutoCustomCmd::Op::Sprite) {
                cmd.rect = shift_rect(cmd.rect);
            }
        }
        if (copy.parent == 0) {
            copy.parent = cached_box_idx;
        } else if (copy.parent > 0) {
            copy.parent = static_cast<int>(base_idx) + (copy.parent - 1);
        }
        for (int &cid : copy.children) {
            cid = static_cast<int>(base_idx) + (cid - 1);
        }
        copy.box_depth = saved_box_depth + (copy.box_depth - snap.nodes[0].box_depth);
        auto_nodes_.push_back(std::move(copy));
    }

    auto &root_after = auto_nodes_[static_cast<std::size_t>(cached_box_idx)];
    root_after.parent = saved_parent;
    root_after.children.clear();
    for (const int cid : snap.nodes[0].children) {
        root_after.children.push_back(static_cast<int>(base_idx) + (cid - 1));
    }
    ++frame_stats_.cached_subtrees_hit;
    return true;
}

// ---------------------------------------------------------------------------
// Drag & Drop Across Containers
// ---------------------------------------------------------------------------

bool UISystem::DragSourceBegin(std::string_view key, std::string_view type, std::string_view payload) {
    const std::string skey = ScopedKey(key);
    const int idx = AllocAutoNode(AutoNode::Kind::DragSource);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = skey;
    node.drag_type = std::string(type);
    node.drag_payload = std::string(payload);
    node.width_spec = UISize::Auto();
    node.height_spec = UISize::Auto();
    node.cross_align = UIAxisAlign::Stretch;
    auto_stack_.push_back(idx);
    return drag_active_ && drag_source_key_ == skey;
}

void UISystem::DragSourceEnd() {
    CloseAutoNode();
}

bool UISystem::DragPreviewBegin() {
    if (!drag_active_) {
        return false;
    }
    const Vec2 m = RawMouse();
    NextClip(false);
    const int idx = AllocAutoNode(AutoNode::Kind::AnchorRoot);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.anchor_x = m.x + 12.0f;
    node.anchor_y = m.y + 12.0f;
    node.use_screen_origin = true;
    node.width_spec = UISize::Auto();
    node.height_spec = UISize::Auto();
    node.overlay = true;
    node.block_input = false;
    node.layer = 1000;
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
    const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : 720.0f;
    node.has_clip_override = true;
    node.clip_override = {0.0f, 0.0f, vw, vh};
    auto_stack_.push_back(idx);
    return true;
}

void UISystem::DragPreviewEnd() {
    if (!auto_stack_.empty()) {
        CloseAutoNode();
    }
}

bool UISystem::DropTargetBegin(std::string_view key, std::string_view accepted_types) {
    const std::string skey = ScopedKey(key);
    const int idx = AllocAutoNode(AutoNode::Kind::DropTarget);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = skey;
    node.drop_accept = std::string(accepted_types);
    node.width_spec = UISize::Auto();
    node.height_spec = UISize::Auto();
    node.cross_align = UIAxisAlign::Stretch;
    auto_stack_.push_back(idx);
    return drop_results_.count(skey) > 0;
}

void UISystem::DropTargetEnd() {
    CloseAutoNode();
}

bool UISystem::IsDropHovered(std::string_view key) const {
    return drag_active_ && drop_hovered_key_ == ResolveKeyLookup(key);
}

std::string UISystem::DroppedPayload(std::string_view target_key) const {
    const auto it = drop_results_.find(ResolveKeyLookup(target_key));
    return it != drop_results_.end() ? it->second.payload : std::string{};
}

std::string UISystem::DroppedType(std::string_view target_key) const {
    const auto it = drop_results_.find(ResolveKeyLookup(target_key));
    return it != drop_results_.end() ? it->second.type : std::string{};
}

void UISystem::ProcessDragAndDrop() {
    if (!input_) {
        return;
    }
    if (drag_active_ && input_->KeyPressed("escape")) {
        drag_active_ = false;
        drag_pending_ = false;
        drag_source_key_.clear();
        drag_type_.clear();
        drag_payload_.clear();
        drop_hovered_key_.clear();
        return;
    }

    const Vec2 m = RawMouse();
    if (!drag_active_ && !drag_pending_ && input_->MousePressed(0)) {
        for (auto it = auto_nodes_.rbegin(); it != auto_nodes_.rend(); ++it) {
            if (it->kind != AutoNode::Kind::DragSource || it->disabled || it->key.empty()) {
                continue;
            }
            const Rect vis{std::max(it->rect.x, it->clip_rect.x), std::max(it->rect.y, it->clip_rect.y),
                           std::max(0.0f, std::min(it->rect.x + it->rect.width, it->clip_rect.x + it->clip_rect.width) -
                                              std::max(it->rect.x, it->clip_rect.x)),
                           std::max(0.0f, std::min(it->rect.y + it->rect.height, it->clip_rect.y + it->clip_rect.height) -
                                              std::max(it->rect.y, it->clip_rect.y))};
            if (vis.Contains(m)) {
                drag_pending_ = true;
                drag_press_pos_ = m;
                drag_pending_key_ = it->key;
                drag_pending_type_ = it->drag_type;
                drag_pending_payload_ = it->drag_payload;
                break;
            }
        }
    }
    if (drag_pending_) {
        if (!input_->MouseDown(0)) {
            drag_pending_ = false;
            drag_pending_key_.clear();
        } else {
            const float dx = m.x - drag_press_pos_.x;
            const float dy = m.y - drag_press_pos_.y;
            if (dx * dx + dy * dy >= kDragThresholdPx * kDragThresholdPx) {
                drag_active_ = true;
                drag_pending_ = false;
                drag_source_key_ = drag_pending_key_;
                drag_type_ = drag_pending_type_;
                drag_payload_ = drag_pending_payload_;
            }
        }
    }
    if (drag_active_) {
        desired_cursor_shape_ = 3; // hand while dragging
        drop_hovered_key_.clear();
        for (auto it = auto_nodes_.rbegin(); it != auto_nodes_.rend(); ++it) {
            if (it->kind != AutoNode::Kind::DropTarget || it->disabled || it->key.empty()) {
                continue;
            }
            if (!AcceptListMatches(it->drop_accept, drag_type_)) {
                continue;
            }
            const Rect vis{std::max(it->rect.x, it->clip_rect.x), std::max(it->rect.y, it->clip_rect.y),
                           std::max(0.0f, std::min(it->rect.x + it->rect.width, it->clip_rect.x + it->clip_rect.width) -
                                              std::max(it->rect.x, it->clip_rect.x)),
                           std::max(0.0f, std::min(it->rect.y + it->rect.height, it->clip_rect.y + it->clip_rect.height) -
                                              std::max(it->rect.y, it->clip_rect.y))};
            if (vis.Contains(m)) {
                drop_hovered_key_ = it->key;
                break;
            }
        }
        if (input_->MouseReleased(0) || !input_->MouseDown(0)) {
            if (!drop_hovered_key_.empty()) {
                drop_results_[drop_hovered_key_] = {drag_type_, drag_payload_};
                frame_results_[drop_hovered_key_] = 1.0f;
            }
            drag_active_ = false;
            drag_source_key_.clear();
            drag_type_.clear();
            drag_payload_.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Hierarchical MenuBar & Cascade Submenus
// ---------------------------------------------------------------------------

void UISystem::MenuBarBegin() {
    menu_bar_active_ = true;
    const int idx = AllocAutoNode(AutoNode::Kind::MenuBar);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Auto();
    node.gap = 4.0f;
    node.pad_l = node.pad_r = 6.0f;
    node.pad_t = node.pad_b = 3.0f;
    node.cross_align = UIAxisAlign::Center;
    node.has_bg = true;
    node.bg_col = ThemeColor("panel");
    node.has_border = true;
    node.custom_border_col = ThemeColor("border");
    node.border_w = 1.0f;
    auto_stack_.push_back(idx);
}

void UISystem::MenuBarEnd() {
    CloseAutoNode();
    menu_bar_active_ = false;
}

bool UISystem::MenuBegin(std::string_view key, std::string_view label) {
    if (!InAutoLayout()) {
        return false;
    }
    const std::string skey = ScopedKey(key);
    const std::size_t depth = menu_stack_.size();
    const bool is_open =
        depth < open_menu_path_.size() && open_menu_path_[depth] == skey;

    // Emit the trigger item inside parent (MenuBar if depth==0, else parent Menu popup)
    const int trig_idx = AllocAutoNode(AutoNode::Kind::Menu);
    auto &trig = auto_nodes_[static_cast<std::size_t>(trig_idx)];
    trig.key = skey;
    trig.text = std::string(label);
    trig.checked = is_open;
    trig.col_span = static_cast<int>(depth); // store menu depth cleanly
    trig.scale = ThemeMetric("font_scale");
    trig.width_spec = depth == 0 ? UISize::Auto() : UISize::Grow(1.0f);
    trig.height_spec = UISize::Auto();
    HitTestSolvedKey(skey);

    if (!recording_) {
        if (Clicked(skey) || (depth == 0 && !open_menu_path_.empty() && IsHovered(skey))) {
            if (Clicked(skey) && is_open && depth == 0) {
                open_menu_path_.clear();
                return false;
            }
            open_menu_path_.resize(depth + 1);
            open_menu_path_[depth] = skey;
        } else if (depth > 0 && IsHovered(skey)) {
            open_menu_path_.resize(depth + 1);
            open_menu_path_[depth] = skey;
        }
    }

    const bool open_now = depth < open_menu_path_.size() && open_menu_path_[depth] == skey;
    if (!open_now) {
        return false;
    }

    // Open floating dropdown anchored to trigger!
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
    const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : 720.0f;
    const int pop_idx = AllocAutoNode(AutoNode::Kind::AnchorRoot);
    auto &pop = auto_nodes_[static_cast<std::size_t>(pop_idx)];
    pop.anchor_target_key = skey;
    pop.h_align = "anchor_to";
    pop.v_align = depth == 0 ? "bottom_start" : "right_start";
    pop.gap = 2.0f;
    pop.width_spec = UISize::Auto();
    pop.height_spec = UISize::Auto();
    pop.limits.min_w = 150.0f;
    pop.overlay = true;
    pop.block_input = true;
    pop.layer = 50 + static_cast<int>(depth) * 10;
    pop.has_clip_override = true;
    pop.clip_override = {0.0f, 0.0f, vw, vh};
    pop.has_bg = true;
    pop.bg_col = ThemeColor("panel");
    pop.has_border = true;
    pop.custom_border_col = ThemeColor("border");
    pop.border_w = 1.0f;
    pop.has_shadow = true;
    pop.shadow_off_y = 4.0f;
    pop.shadow_spread = 6.0f;
    pop.shadow_alpha = 0.35f;
    pop.radius = ThemeMetric("corner_radius");
    pop.pad_l = pop.pad_t = pop.pad_r = pop.pad_b = 4.0f;
    pop.gap = 2.0f;
    pop.cross_align = UIAxisAlign::Stretch;
    auto_stack_.push_back(pop_idx);
    menu_stack_.push_back(skey);
    return true;
}

void UISystem::MenuEnd() {
    if (!menu_stack_.empty()) {
        menu_stack_.pop_back();
    }
    if (!auto_stack_.empty()) {
        CloseAutoNode();
    }
}

bool UISystem::MenuItem(std::string_view key, std::string_view label, std::string_view shortcut,
                        bool checked) {
    if (!InAutoLayout()) {
        return false;
    }
    const std::string skey = ScopedKey(key);
    const int idx = AllocAutoNode(AutoNode::Kind::MenuItem);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = skey;
    node.text = std::string(label);
    node.shortcut = std::string(shortcut);
    node.checked = checked;
    node.scale = ThemeMetric("font_scale");
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Auto();
    const bool fired = HitTestSolvedKey(skey);
    if (fired) {
        open_menu_path_.clear();
    }
    return fired;
}

void UISystem::MenuSeparator() {
    if (!InAutoLayout()) {
        return;
    }
    const int idx = AllocAutoNode(AutoNode::Kind::MenuSeparator);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Fixed(7.0f);
}

// ---------------------------------------------------------------------------
// Accessibility / Semantic Tree Export
// ---------------------------------------------------------------------------

UISemanticNode UISystem::BuildSemanticNode(int idx) const {
    UISemanticNode out;
    if (idx < 0 || static_cast<std::size_t>(idx) >= auto_nodes_.size()) {
        return out;
    }
    const auto &n = auto_nodes_[static_cast<std::size_t>(idx)];
    out.key = n.key;
    out.rect = n.rect;
    out.clip_rect = n.clip_rect;
    out.enabled = !n.disabled;
    out.focused = (!n.key.empty() && (nav_focus_ == n.key || focused_ == n.key));
    out.checked = n.checked;
    out.selected = n.checked;
    out.label = !n.sem_label.empty() ? n.sem_label : n.text;
    out.value = n.sem_value;
    out.hint = !n.sem_hint.empty() ? n.sem_hint : n.tip_text;

    if (!n.sem_role.empty()) {
        out.role = n.sem_role;
    } else {
        switch (n.kind) {
        case AutoNode::Kind::Button:
        case AutoNode::Kind::ImageButton: out.role = "button"; out.actions.push_back("click"); break;
        case AutoNode::Kind::Checkbox: out.role = "checkbox"; out.actions.push_back("toggle"); break;
        case AutoNode::Kind::RadioButton: out.role = "radio"; out.actions.push_back("select"); break;
        case AutoNode::Kind::Slider:
            out.role = "slider";
            out.value = std::to_string(n.val);
            out.actions.push_back("adjust");
            break;
        case AutoNode::Kind::TextInput:
        case AutoNode::Kind::TextArea:
        case AutoNode::Kind::TextPassword:
            out.role = "text_field";
            if (const auto it = widgets_.find(n.key); it != widgets_.end()) {
                out.value = (n.kind == AutoNode::Kind::TextPassword) ? "***" : it->second.text;
            }
            out.actions.push_back("edit");
            break;
        case AutoNode::Kind::Tab: out.role = "tab"; out.actions.push_back("select"); break;
        case AutoNode::Kind::Tree: out.role = "tree_item"; out.actions.push_back("toggle"); break;
        case AutoNode::Kind::ComboBegin: out.role = "combobox"; out.actions.push_back("expand"); break;
        case AutoNode::Kind::ComboItem: out.role = "option"; out.actions.push_back("select"); break;
        case AutoNode::Kind::MenuBar: out.role = "menubar"; break;
        case AutoNode::Kind::Menu: out.role = "menu"; out.actions.push_back("open"); break;
        case AutoNode::Kind::MenuItem: out.role = "menuitem"; out.actions.push_back("click"); break;
        case AutoNode::Kind::Scroll: out.role = "scroll_view"; out.actions.push_back("scroll"); break;
        case AutoNode::Kind::ProgressBar:
            out.role = "progressbar";
            out.value = std::to_string(n.val);
            break;
        case AutoNode::Kind::Text:
        case AutoNode::Kind::RichText: out.role = "text"; break;
        case AutoNode::Kind::Image: out.role = "image"; break;
        case AutoNode::Kind::Splitter: out.role = "splitter"; out.actions.push_back("adjust"); break;
        case AutoNode::Kind::DragSource: out.role = "draggable"; out.actions.push_back("drag"); break;
        case AutoNode::Kind::DropTarget: out.role = "drop_zone"; out.actions.push_back("drop"); break;
        case AutoNode::Kind::Table: out.role = "table"; break;
        case AutoNode::Kind::TableRow: out.role = "row"; break;
        case AutoNode::Kind::TableCell: out.role = "cell"; break;
        case AutoNode::Kind::AnchorRoot:
        case AutoNode::Kind::Root:
            out.role = n.is_modal_root ? "dialog" : "window";
            break;
        default: out.role = "group"; break;
        }
    }
    for (const int cid : n.children) {
        out.children.push_back(BuildSemanticNode(cid));
    }
    return out;
}

std::vector<UISemanticNode> UISystem::SemanticNodes() const {
    std::vector<UISemanticNode> roots;
    for (std::size_t i = 0; i < auto_nodes_.size(); ++i) {
        if (auto_nodes_[i].parent < 0) {
            roots.push_back(BuildSemanticNode(static_cast<int>(i)));
        }
    }
    return roots;
}

UISemanticNode UISystem::SemanticTree() const {
    UISemanticNode root;
    root.key = "@root";
    root.role = "application";
    root.children = SemanticNodes();
    return root;
}

std::string UISystem::SemanticJson() const {
    std::string out;
    out.reserve(1024);
    SerializeSemanticJsonRec(out, SemanticTree());
    return out;
}

// ---------------------------------------------------------------------------
// Interactive Live DevTools Inspector Overlay
// ---------------------------------------------------------------------------

void UISystem::DrawInspectorOverlay() {
    if (!inspector_enabled_ || !renderer_ || audit_.empty()) {
        return;
    }
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
    const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : 720.0f;
    const Vec2 mouse = RawMouse();

    const float pw = std::min(340.0f, vw - 24.0f);
    const float ph = 248.0f + (inspector_show_issues_ ? 88.0f : 0.0f) + (inspector_show_semantics_ ? 74.0f : 0.0f);
    inspector_panel_pos_.x = std::clamp(inspector_panel_pos_.x, 8.0f, std::max(8.0f, vw - pw - 8.0f));
    inspector_panel_pos_.y = std::clamp(inspector_panel_pos_.y, 8.0f, std::max(8.0f, vh - ph - 8.0f));
    const Rect panel{inspector_panel_pos_.x, inspector_panel_pos_.y, pw, ph};
    const Rect header{panel.x, panel.y, panel.width, 24.0f};

    if (input_) {
        if (header.Contains(mouse) && input_->MousePressed(0)) {
            inspector_dragging_ = true;
            inspector_drag_grab_ = {mouse.x - panel.x, mouse.y - panel.y};
        }
        if (inspector_dragging_ && input_->MouseDown(0)) {
            inspector_panel_pos_ = {mouse.x - inspector_drag_grab_.x, mouse.y - inspector_drag_grab_.y};
        } else if (!input_->MouseDown(0)) {
            inspector_dragging_ = false;
        }
    }

    const AuditRect *hovered = nullptr;
    const AuditRect *selected = nullptr;
    for (auto it = audit_.rbegin(); it != audit_.rend(); ++it) {
        if (it->suppressed || it->rect.width <= 0.0f || it->rect.height <= 0.0f) {
            continue;
        }
        if (it->overflow) {
            DrawFrameAt(it->rect, Color{1.0f, 0.25f, 0.25f, 0.85f}, 1.5f);
        }
        if (!selected && !inspector_selected_key_.empty() && it->name == inspector_selected_key_) {
            selected = &(*it);
        }
        if (!hovered && !panel.Contains(mouse) && it->rect.Contains(mouse)) {
            hovered = &(*it);
        }
    }
    if (input_ && input_->MousePressed(0) && hovered && !panel.Contains(mouse)) {
        inspector_selected_key_ = hovered->name;
        selected = hovered;
    }

    const AuditRect *focus_box = hovered ? hovered : selected;
    if (focus_box) {
        const Rect &r = focus_box->rect;
        if (focus_box->margin_l > 0 || focus_box->margin_t > 0 ||
            focus_box->margin_r > 0 || focus_box->margin_b > 0) {
            const Rect mr{r.x - focus_box->margin_l, r.y - focus_box->margin_t,
                          r.width + focus_box->margin_l + focus_box->margin_r,
                          r.height + focus_box->margin_t + focus_box->margin_b};
            DrawQuadAt(mr, Color{1.0f, 0.65f, 0.2f, 0.16f});
            DrawFrameAt(mr, Color{1.0f, 0.65f, 0.2f, 0.9f}, 1.0f);
        }
        if (focus_box->pad_l > 0 || focus_box->pad_t > 0 ||
            focus_box->pad_r > 0 || focus_box->pad_b > 0) {
            DrawQuadAt(r, Color{0.25f, 0.85f, 0.4f, 0.14f});
            const Rect cr{r.x + focus_box->pad_l, r.y + focus_box->pad_t,
                          std::max(0.0f, r.width - focus_box->pad_l - focus_box->pad_r),
                          std::max(0.0f, r.height - focus_box->pad_t - focus_box->pad_b)};
            DrawFrameAt(cr, Color{0.25f, 0.85f, 0.4f, 0.85f}, 1.0f);
        }
        DrawFrameAt(r, Color{0.2f, 0.85f, 1.0f, 0.95f}, 1.8f);
    }

    // Draw Interactive DevTools Panel
    DrawDropShadow(panel, 6.0f, 4.0f, 8.0f, 0.45f);
    DrawRoundedQuad(panel, 6.0f, Color{0.05f, 0.07f, 0.12f, 0.94f});
    DrawRoundedFrame(panel, 6.0f, 1.2f, Color{0.22f, 0.72f, 0.98f, 0.92f});
    DrawQuadAt(header, Color{0.11f, 0.16f, 0.26f, 0.96f});
    DrawTextPx(panel.x + 8.0f, panel.y + 5.0f, "UI DevTools Inspector (Drag Header)", 12.0f,
               Color{0.62f, 0.88f, 1.0f, 1.0f});

    float y = panel.y + 30.0f;
    const auto issues = AuditLayout();
    char line[256];
    std::snprintf(line, sizeof(line), "Nodes:%zu  Layout:%.2fms  Cache:%zu  Issues:%zu",
                  frame_stats_.nodes, frame_stats_.layout_ms,
                  frame_stats_.cached_subtrees_hit, issues.size());
    DrawTextPx(panel.x + 8.0f, y, line, 11.0f, Color{0.88f, 0.92f, 0.98f, 1.0f});
    y += 18.0f;

    if (focus_box) {
        std::snprintf(line, sizeof(line), "%s [%s] L=%d", focus_box->kind.c_str(),
                      focus_box->name.empty() ? "-" : focus_box->name.c_str(), focus_box->layer);
        DrawTextPx(panel.x + 8.0f, y, line, 11.0f, Color{0.4f, 0.92f, 0.7f, 1.0f});
        y += 15.0f;
        std::snprintf(line, sizeof(line), "Rect:(%.0f,%.0f %.0fx%.0f) Meas:(%.0fx%.0f)",
                      focus_box->rect.x, focus_box->rect.y, focus_box->rect.width,
                      focus_box->rect.height, focus_box->meas_w, focus_box->meas_h);
        DrawTextPx(panel.x + 8.0f, y, line, 11.0f, Color::White());
        y += 15.0f;
        std::snprintf(line, sizeof(line), "Pad(%.0f,%.0f,%.0f,%.0f) Mar(%.0f,%.0f,%.0f,%.0f)",
                      focus_box->pad_l, focus_box->pad_t, focus_box->pad_r, focus_box->pad_b,
                      focus_box->margin_l, focus_box->margin_t, focus_box->margin_r,
                      focus_box->margin_b);
        DrawTextPx(panel.x + 8.0f, y, line, 11.0f, Color{0.85f, 0.85f, 0.9f, 1.0f});
        y += 18.0f;
    } else {
        DrawTextPx(panel.x + 8.0f, y, "Hover or click any widget to inspect Box Model", 11.0f,
                   Color{0.65f, 0.7f, 0.8f, 1.0f});
        y += 48.0f;
    }

    // Live interactive Theme Metric steppers (+/- buttons handled cleanly via mouse press!)
    auto draw_metric_stepper = [&](const char *label, const char *metric, float step, float min_v, float max_v) {
        float val = ThemeMetric(metric);
        char lbl[96];
        std::snprintf(lbl, sizeof(lbl), "%s: %.2f", label, val);
        DrawTextPx(panel.x + 8.0f, y + 2.0f, lbl, 11.0f, Color::White());
        const Rect minus_r{panel.x + pw - 58.0f, y, 22.0f, 18.0f};
        const Rect plus_r{panel.x + pw - 30.0f, y, 22.0f, 18.0f};
        const bool m_hov = minus_r.Contains(mouse);
        const bool p_hov = plus_r.Contains(mouse);
        DrawRoundedQuad(minus_r, 3.0f, m_hov ? ThemeColor("hover") : Color{0.18f, 0.24f, 0.36f, 1.0f});
        DrawRoundedQuad(plus_r, 3.0f, p_hov ? ThemeColor("hover") : Color{0.18f, 0.24f, 0.36f, 1.0f});
        DrawTextPx(minus_r.x + 7.0f, minus_r.y + 2.0f, "-", 12.0f, Color::White());
        DrawTextPx(plus_r.x + 6.0f, plus_r.y + 2.0f, "+", 12.0f, Color::White());
        if (input_ && input_->MousePressed(0)) {
            if (m_hov) SetThemeMetric(metric, std::max(min_v, val - step));
            if (p_hov) SetThemeMetric(metric, std::min(max_v, val + step));
        }
        y += 22.0f;
    };
    draw_metric_stepper("CornerRadius", "corner_radius", 2.0f, 0.0f, 24.0f);
    draw_metric_stepper("Spacing", "spacing", 2.0f, 0.0f, 32.0f);
    draw_metric_stepper("FontScale", "font_scale", 0.05f, 0.25f, 1.5f);
    draw_metric_stepper("TransitionMs", "state_transition_ms", 40.0f, 0.0f, 400.0f);

    const Rect btn_iss{panel.x + 8.0f, y + 4.0f, (pw - 24.0f) * 0.5f, 20.0f};
    const Rect btn_sem{btn_iss.x + btn_iss.width + 8.0f, y + 4.0f, (pw - 24.0f) * 0.5f, 20.0f};
    DrawRoundedQuad(btn_iss, 3.0f, inspector_show_issues_ ? ThemeColor("accent") : Color{0.18f, 0.22f, 0.32f, 1.0f});
    DrawRoundedQuad(btn_sem, 3.0f, inspector_show_semantics_ ? ThemeColor("accent") : Color{0.18f, 0.22f, 0.32f, 1.0f});
    DrawTextPx(btn_iss.x + 6.0f, btn_iss.y + 3.0f, inspector_show_issues_ ? "Issues [ON]" : "Issues", 11.0f, Color::White());
    DrawTextPx(btn_sem.x + 6.0f, btn_sem.y + 3.0f, inspector_show_semantics_ ? "Semantics [ON]" : "Semantics", 11.0f, Color::White());
    if (input_ && input_->MousePressed(0)) {
        if (btn_iss.Contains(mouse)) inspector_show_issues_ = !inspector_show_issues_;
        if (btn_sem.Contains(mouse)) inspector_show_semantics_ = !inspector_show_semantics_;
    }
    y += 30.0f;

    if (inspector_show_issues_) {
        DrawTextPx(panel.x + 8.0f, y, issues.empty() ? "No layout issues!" : "Top Layout Issues:",
                   11.0f, issues.empty() ? Color{0.4f, 0.9f, 0.5f, 1.0f} : Color{1.0f, 0.45f, 0.45f, 1.0f});
        y += 15.0f;
        for (std::size_t i = 0; i < std::min<std::size_t>(4, issues.size()); ++i) {
            std::snprintf(line, sizeof(line), "- %s: %s", issues[i].category.c_str(),
                          issues[i].a_name.c_str());
            DrawTextPx(panel.x + 12.0f, y, line, 10.5f, Color{1.0f, 0.78f, 0.78f, 1.0f});
            y += 15.0f;
        }
    }
    if (inspector_show_semantics_) {
        const auto sems = SemanticNodes();
        std::snprintf(line, sizeof(line), "Semantic Roots: %zu  Focus: %s", sems.size(),
                      nav_focus_.empty() ? "(none)" : nav_focus_.c_str());
        DrawTextPx(panel.x + 8.0f, y, line, 11.0f, Color{0.72f, 0.88f, 1.0f, 1.0f});
    }
}

// ---------------------------------------------------------------------------
// Modern One-Shot Modifiers
// ---------------------------------------------------------------------------

void UISystem::NextShrink(float weight) {
    next_auto_mod_.has_shrink = true;
    next_auto_mod_.shrink = weight > 0.0f ? weight : 0.0f;
}

void UISystem::NextAspect(float ratio) {
    next_auto_mod_.has_aspect = true;
    next_auto_mod_.aspect_ratio = ratio > 0.0f ? ratio : 0.0f;
}

void UISystem::NextSpan(int cols) {
    next_auto_mod_.has_span = true;
    next_auto_mod_.col_span = std::max(1, cols);
}

void UISystem::NextRowSpan(int rows) {
    next_auto_mod_.has_row_span = true;
    next_auto_mod_.row_span = std::max(1, rows);
}

void UISystem::NextMargin(float l, float t, float r, float b) {
    next_auto_mod_.has_margin = true;
    next_auto_mod_.margin_l = std::max(0.0f, l);
    next_auto_mod_.margin_t = std::max(0.0f, t);
    next_auto_mod_.margin_r = std::max(0.0f, r);
    next_auto_mod_.margin_b = std::max(0.0f, b);
}

void UISystem::NextBg(const Color &col, float radius) {
    next_auto_mod_.has_bg = true;
    next_auto_mod_.bg_col = col;
    if (radius > 0.0f) {
        next_auto_mod_.has_radius = true;
        next_auto_mod_.radius = radius;
    }
}

void UISystem::NextGradient(const Color &start_col, const Color &end_col, bool horizontal) {
    next_auto_mod_.has_grad = true;
    next_auto_mod_.grad_start = start_col;
    next_auto_mod_.grad_end = end_col;
    next_auto_mod_.grad_horiz = horizontal;
}

void UISystem::NextBorder(const Color &col, float width) {
    next_auto_mod_.has_border = true;
    next_auto_mod_.border_col = col;
    next_auto_mod_.border_w = std::max(0.0f, width);
}

void UISystem::NextRadius(float radius) {
    next_auto_mod_.has_radius = true;
    next_auto_mod_.radius = std::max(0.0f, radius);
}

void UISystem::NextShadow(float offset_y, float spread, float alpha) {
    next_auto_mod_.has_shadow = true;
    next_auto_mod_.shadow_off_y = offset_y;
    next_auto_mod_.shadow_spread = std::max(0.0f, spread);
    next_auto_mod_.shadow_alpha = std::clamp(alpha, 0.0f, 1.0f);
}

void UISystem::NextBackdropBlur(float radius) {
    next_auto_mod_.has_blur = true;
    next_auto_mod_.backdrop_blur = std::max(0.0f, radius);
}

void UISystem::NextAlpha(float alpha) {
    next_auto_mod_.has_alpha = true;
    next_auto_mod_.alpha_mul = std::clamp(alpha, 0.0f, 1.0f);
}

void UISystem::NextClip(bool clip) {
    next_auto_mod_.has_clip = true;
    next_auto_mod_.clip_val = clip;
}

void UISystem::NextDirection(UILayoutDirection dir) {
    next_auto_mod_.has_dir = true;
    next_auto_mod_.dir = dir;
}

void UISystem::NextPlaceholder(std::string_view text) {
    next_auto_mod_.has_placeholder = true;
    next_auto_mod_.placeholder = std::string(text);
}

void UISystem::NextTooltip(std::string_view text) {
    next_auto_mod_.has_tip = true;
    next_auto_mod_.tip = std::string(text);
}

void UISystem::NextSemantics(std::string_view role, std::string_view label,
                             std::string_view value, std::string_view hint) {
    next_auto_mod_.has_sem = true;
    next_auto_mod_.sem_role = std::string(role);
    next_auto_mod_.sem_label = std::string(label);
    next_auto_mod_.sem_value = std::string(value);
    next_auto_mod_.sem_hint = std::string(hint);
}

void UISystem::NextVisible(bool visible) {
    next_auto_mod_.has_visible = true;
    next_auto_mod_.visible_val = visible;
}

void UISystem::NextEnabled(bool enabled) {
    next_auto_mod_.has_enabled = true;
    next_auto_mod_.enabled_val = enabled;
}

void UISystem::NextTransition(std::string_view anim_key, float duration_sec) {
    next_auto_mod_.has_anim = true;
    next_auto_mod_.anim_key = ScopedKey(anim_key);
    next_auto_mod_.anim_duration = std::max(0.0f, duration_sec);
}

// ---------------------------------------------------------------------------
// Modern Two-Pass Containers (ZStack, Overlay, AnchorTo, Grid, ScrollHV, Virtual, Cache)
// ---------------------------------------------------------------------------

void UISystem::ZStackBegin(float pad) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const bool had_pad = next_auto_mod_.has_pad;
    const int idx = AllocAutoNode(AutoNode::Kind::ZStack);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = UISize::Auto();
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    if (!had_pad && pad > 0.0f) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = pad;
    }
    node.cross_align = UIAxisAlign::Stretch;
    auto_stack_.push_back(idx);
}

void UISystem::ZStackEnd() {
    CloseAutoNode();
}

void UISystem::OverlayBegin(std::string_view h_align, std::string_view v_align,
                            float off_x, float off_y) {
    RootAnchorBegin(h_align, v_align, UISize::Auto(), UISize::Auto(), off_x, off_y, 0.0f, true, false);
}

void UISystem::OverlayEnd() {
    CloseAutoNode();
}

void UISystem::AnchorToBegin(std::string_view target_key, std::string_view placement,
                             float gap, UISize w, UISize h) {
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
    const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : 720.0f;
    const int idx = AllocAutoNode(AutoNode::Kind::AnchorRoot);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.anchor_target_key = ResolveKeyLookup(target_key);
    node.h_align = "anchor_to";
    node.v_align = std::string(placement);
    node.gap = gap;
    node.width_spec = w;
    node.height_spec = h;
    node.overlay = true;
    node.block_input = true;
    node.has_clip_override = true;
    node.clip_override = {0.0f, 0.0f, vw, vh};
    node.layer = std::max(10, ++modal_layer_seq_ + 10);
    auto_stack_.push_back(idx);
}

void UISystem::AnchorToEnd() {
    CloseAutoNode();
}

void UISystem::GridBegin(float min_col_w, float col_gap, float row_gap, float pad) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const bool had_pad = next_auto_mod_.has_pad;
    const int idx = AllocAutoNode(AutoNode::Kind::Grid);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    if (!had_pad && pad > 0.0f) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = pad;
    }
    node.grid_fixed_cols = 0;
    node.min_col_w = std::max(16.0f, min_col_w);
    node.gap = col_gap >= 0.0f ? col_gap : ThemeMetric("col_gap");
    node.line_gap = row_gap >= 0.0f ? row_gap : ThemeMetric("row_gap");
    node.cross_align = UIAxisAlign::Stretch;
    auto_stack_.push_back(idx);
}

void UISystem::GridColumnsBegin(int cols, float col_gap, float row_gap, float pad) {
    GridBegin(16.0f, col_gap, row_gap, pad);
    if (!auto_stack_.empty()) {
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].grid_fixed_cols = std::max(1, cols);
    }
}

void UISystem::GridEnd() {
    CloseAutoNode();
}

void UISystem::ScrollBoxBegin(std::string_view key, UISize view_w, UISize view_h, UIScrollDir dir) {
    NextSize(view_w, view_h);
    ScrollBoxBegin(key, view_h);
    if (!auto_stack_.empty()) {
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].scroll_dir = dir;
    }
}

float UISystem::ScrollOffsetX(std::string_view key) const {
    auto it = scroll_states_.find(ResolveKeyLookup(key));
    return it == scroll_states_.end() ? 0.0f : it->second.offset_x;
}

float UISystem::ScrollContentWidth(std::string_view key) const {
    auto it = scroll_states_.find(ResolveKeyLookup(key));
    return it == scroll_states_.end() ? 0.0f : it->second.width;
}

void UISystem::SetScrollOffsetX(std::string_view key, float offset) {
    auto &s = scroll_states_[ScopedKey(key)];
    s.offset_x = std::max(0.0f, offset);
}

UISystem::VirtualRange UISystem::VirtualBegin(std::string_view key, int total_count,
                                              float item_h, int overscan) {
    VirtualRange out{};
    last_virtual_range_ = out;
    if (!InAutoLayout() || total_count <= 0 || item_h <= 0.0f) {
        virtual_stack_.push_back({ScopedKey(key), 0, 0, 0, item_h});
        return out;
    }
    const std::string skey = ResolveKeyLookup(key);
    float off = 0.0f;
    if (const auto it = scroll_states_.find(skey); it != scroll_states_.end()) {
        off = it->second.offset;
    }
    float view_h = 360.0f;
    for (auto it = auto_stack_.rbegin(); it != auto_stack_.rend(); ++it) {
        const auto &anc = auto_nodes_[static_cast<std::size_t>(*it)];
        if (anc.kind == AutoNode::Kind::Scroll) {
            if (!anc.key.empty()) {
                if (const auto sit = prev_solved_widgets_.find(anc.key); sit != prev_solved_widgets_.end()) {
                    view_h = std::max(item_h, sit->second.rect.height);
                } else if (anc.height_spec.kind == UISizeKind::Fixed) {
                    view_h = anc.height_spec.value;
                }
            }
            break;
        }
    }
    const int ov = std::max(0, overscan);
    const int first = std::clamp(static_cast<int>(std::floor(off / item_h)) - ov, 0, total_count);
    const int last = std::clamp(static_cast<int>(std::ceil((off + view_h) / item_h)) + ov, first, total_count);
    out.first = first;
    out.count = last - first;
    if (first > 0) {
        Spacer(static_cast<float>(first) * item_h);
    }
    last_virtual_range_ = out;
    virtual_stack_.push_back({skey, total_count, first, out.count, item_h});
    return out;
}

void UISystem::VirtualEnd() {
    if (virtual_stack_.empty()) {
        return;
    }
    const VirtualScope vs = virtual_stack_.back();
    virtual_stack_.pop_back();
    const int after = std::max(0, vs.total - (vs.first + vs.count));
    if (after > 0 && vs.item_h > 0.0f && InAutoLayout()) {
        Spacer(static_cast<float>(after) * vs.item_h);
    }
}

bool UISystem::CachedSubtreeBegin(std::string_view key, std::uint64_t version_hash) {
    const std::string skey = ScopedKey(key);
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::CachedBox);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = skey;
    node.cache_hash = version_hash;
    if (!had_w) {
        node.width_spec = UISize::Auto();
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    const auto it = layout_cache_.find(skey);
    const float fscale = ThemeMetric("font_scale");
    if (it != layout_cache_.end() && it->second.hash == version_hash &&
        std::abs(it->second.est_avail_w - node.est_avail_w) < 0.25f &&
        std::abs(it->second.font_scale - fscale) < 1e-4f) {
        node.cached_hit = true;
        node.meas_w = it->second.meas_w;
        node.meas_h = it->second.meas_h;
        node.baseline = it->second.baseline;
        return false;
    }
    auto_stack_.push_back(idx);
    return true;
}

void UISystem::CachedSubtreeEnd() {
    if (!auto_stack_.empty() &&
        auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].kind == AutoNode::Kind::CachedBox) {
        CloseAutoNode();
    }
}

} // namespace fake2d
