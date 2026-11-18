#include "fake2d/ui.h"
#include "fake2d/ease.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fake2d {

namespace {

constexpr Color kScrollThumbModern{0.22f, 0.30f, 0.48f, 1.0f};
constexpr float kPiOver2 = 1.57079632679f;

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
            curr_solved_widgets_.count(scoped)) {
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
// Focus Scope Stack
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
// Modern Visual Primitives (Rounded Rect, Rounded Border, Drop Shadow, HScroll)
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
    if (node.has_bg) {
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

void UISystem::DrawHScrollbarVisual(const Rect &vp, float content_w, float offset_x, bool hover,
                                    bool dragging) {
    if (!renderer_ || content_w <= vp.width || vp.width <= 0.0f) {
        return;
    }
    const float bar_h = 10.0f;
    const float max_scroll = std::max(1e-4f, content_w - vp.width);
    const Rect bar{vp.x, vp.y + vp.height - bar_h, vp.width, bar_h};
    const float thumb_w = std::min(vp.width, std::max(24.0f, vp.width * (vp.width / content_w)));
    const float track = vp.width - thumb_w;
    const float drawn = vp.x + std::clamp(offset_x / max_scroll, 0.0f, 1.0f) * track;
    renderer_->DrawQuad(MapRect(bar), ThemeColor("track"));
    renderer_->DrawQuad(MapRect({drawn, bar.y + 1.0f, thumb_w, bar_h - 2.0f}),
                        dragging ? ThemeColor("accent") : (hover ? ThemeColor("hover") : kScrollThumbModern));
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
                                  const Rect &parent_clip, bool in_scroll, std::uint32_t root_id) {
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

    // Clone descendant nodes (indices 1..N-1 in snap.nodes) into auto_nodes_
    for (std::size_t i = 1; i < snap.nodes.size(); ++i) {
        AutoNode copy = snap.nodes[i];
        copy.box_id = box_sequence_++;
        copy.root_box_id = root_id;
        copy.in_scroll = in_scroll || copy.in_scroll;
        copy.rect = shift_rect(copy.rect);
        copy.content_rect = shift_rect(copy.content_rect);
        copy.clip_rect = parent_clip;
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
// Live DevTools Inspector Overlay
// ---------------------------------------------------------------------------

void UISystem::DrawInspectorOverlay() {
    if (!inspector_enabled_ || !renderer_ || audit_.empty()) {
        return;
    }
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
    const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : 720.0f;
    const Vec2 mouse = RawMouse();

    const AuditRect *hovered = nullptr;
    for (auto it = audit_.rbegin(); it != audit_.rend(); ++it) {
        if (it->suppressed || it->rect.width <= 0.0f || it->rect.height <= 0.0f) {
            continue;
        }
        if (it->overflow) {
            DrawFrameAt(it->rect, Color{1.0f, 0.25f, 0.25f, 0.85f}, 1.5f);
        }
        if (!hovered && it->rect.Contains(mouse)) {
            hovered = &(*it);
        }
    }

    if (hovered) {
        const Rect &r = hovered->rect;
        if (hovered->margin_l > 0 || hovered->margin_t > 0 || hovered->margin_r > 0 || hovered->margin_b > 0) {
            const Rect mr{r.x - hovered->margin_l, r.y - hovered->margin_t,
                          r.width + hovered->margin_l + hovered->margin_r,
                          r.height + hovered->margin_t + hovered->margin_b};
            DrawQuadAt(mr, Color{1.0f, 0.65f, 0.2f, 0.16f});
            DrawFrameAt(mr, Color{1.0f, 0.65f, 0.2f, 0.9f}, 1.0f);
        }
        if (hovered->pad_l > 0 || hovered->pad_t > 0 || hovered->pad_r > 0 || hovered->pad_b > 0) {
            DrawQuadAt(r, Color{0.25f, 0.85f, 0.4f, 0.14f});
            const Rect cr{r.x + hovered->pad_l, r.y + hovered->pad_t,
                          std::max(0.0f, r.width - hovered->pad_l - hovered->pad_r),
                          std::max(0.0f, r.height - hovered->pad_t - hovered->pad_b)};
            DrawFrameAt(cr, Color{0.25f, 0.85f, 0.4f, 0.85f}, 1.0f);
        }
        DrawFrameAt(r, Color{0.2f, 0.85f, 1.0f, 0.95f}, 1.5f);

        char info[320];
        std::snprintf(info, sizeof(info),
                      "[Inspector] %s '%s' rect=(%.0f,%.0f %.0fx%.0f) meas=(%.0fx%.0f) L=%d cache=%zu",
                      hovered->kind.c_str(), hovered->name.empty() ? "-" : hovered->name.c_str(),
                      r.x, r.y, r.width, r.height, hovered->meas_w, hovered->meas_h,
                      hovered->layer, frame_stats_.cached_subtrees_hit);
        const float px = 12.0f;
        const float tw = MeasureTextPx(info, px);
        const float bh = 22.0f;
        const float bw = std::min(vw - 16.0f, tw + 16.0f);
        const float bx = std::clamp(mouse.x + 12.0f, 8.0f, std::max(8.0f, vw - bw - 8.0f));
        const float by = (mouse.y + 16.0f + bh < vh) ? (mouse.y + 16.0f) : std::max(8.0f, mouse.y - bh - 10.0f);
        DrawQuadAt({bx, by, bw, bh}, Color{0.05f, 0.07f, 0.12f, 0.92f});
        DrawFrameAt({bx, by, bw, bh}, Color{0.2f, 0.85f, 1.0f, 0.95f}, 1.0f);
        DrawTextPx(bx + 8.0f, by + 4.0f, info, px, Color::White(), 0, 0);
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

void UISystem::NextAlpha(float alpha) {
    next_auto_mod_.has_alpha = true;
    next_auto_mod_.alpha_mul = std::clamp(alpha, 0.0f, 1.0f);
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
    node.layer = std::max(1, ++modal_layer_seq_);
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
    node.min_col_w = std::max(16.0f, min_col_w);
    node.gap = col_gap >= 0.0f ? col_gap : ThemeMetric("col_gap");
    node.line_gap = row_gap >= 0.0f ? row_gap : ThemeMetric("row_gap");
    node.cross_align = UIAxisAlign::Stretch;
    auto_stack_.push_back(idx);
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
        return false; // Caller skips declaring subtree; layout + commit replay cache!
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
