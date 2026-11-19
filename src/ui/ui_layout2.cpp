#include "fake2d/ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

namespace fake2d {

namespace {

constexpr float kUnbounded = 1e8f;
constexpr Color kScrollThumb{0.22f, 0.30f, 0.48f, 1.0f};
constexpr Color kSliderThumb{0.22f, 0.30f, 0.48f, 1.0f};

Rect IntersectClip(const Rect &a, const Rect &b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

float AlignOffset(UIAxisAlign align, float container, float item) {
    const float slack = std::max(0.0f, container - item);
    switch (align) {
    case UIAxisAlign::Center:
        return slack * 0.5f;
    case UIAxisAlign::End:
        return slack;
    case UIAxisAlign::Start:
    case UIAxisAlign::Stretch:
    default:
        return 0.0f;
    }
}

void ComputeMainAxisSpacing(UIAxisAlign align, float slack, std::size_t count,
                            float &out_start, float &out_extra_gap) {
    out_start = 0.0f;
    out_extra_gap = 0.0f;
    if (slack <= 0.0f || count == 0) {
        return;
    }
    switch (align) {
    case UIAxisAlign::Center:
        out_start = slack * 0.5f;
        break;
    case UIAxisAlign::End:
        out_start = slack;
        break;
    case UIAxisAlign::SpaceBetween:
        if (count > 1) {
            out_extra_gap = slack / static_cast<float>(count - 1);
        } else {
            out_start = slack * 0.5f;
        }
        break;
    case UIAxisAlign::SpaceAround: {
        const float step = slack / static_cast<float>(count);
        out_start = step * 0.5f;
        out_extra_gap = step;
        break;
    }
    case UIAxisAlign::SpaceEvenly: {
        const float step = slack / static_cast<float>(count + 1);
        out_start = step;
        out_extra_gap = step;
        break;
    }
    case UIAxisAlign::Start:
    case UIAxisAlign::Stretch:
    case UIAxisAlign::Baseline:
    default:
        break;
    }
}

Color MixColor(const Color &a, const Color &b, float t) {
    const float u = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u,
            a.a + (b.a - a.a) * u};
}

} // namespace

void UISystem::ResetAutoLayoutFrame() {
    prev_solved_widgets_ = std::move(curr_solved_widgets_);
    curr_solved_widgets_.clear();
    auto_nodes_.clear();
    auto_stack_.clear();
    deferred_auto_nodes_.clear();
    deferred_rich_links_.clear();
    last_auto_node_ = -1;
    next_auto_mod_ = {};
    auto_row_seq_ = 0;
}

float UISystem::AutoWidgetTextScale(float scale) const {
    return scale > 0.0f ? scale : ThemeMetric("font_scale");
}

float UISystem::FontLineHeightPx(float scale) const {
    const float sc = AutoWidgetTextScale(scale);
    if (renderer_) {
        const float lh = renderer_->GetFont().LineHeight() * sc;
        if (lh > 0.0f) {
            return lh;
        }
    }
    return 32.0f * sc;
}

void UISystem::ApplyClipRectDirect(const Rect &clip_rect, float clip_radius, const Rect *clip_rrect) {
    if (!renderer_) {
        return;
    }
    if (clip_rect.width >= 1e7f && clip_rect.height >= 1e7f && clip_rect.x <= 0.0f && clip_rect.y <= 0.0f) {
        renderer_->ClearClipRect();
        return;
    }
    Rect r = clip_rect;
    if (r.width <= 0.0f || r.height <= 0.0f) {
        r = {-100.0f, -100.0f, 0.001f, 0.001f};
    }
    r.x -= world_origin_.x;
    r.y -= world_origin_.y;
    Rect rr_mapped{};
    if (clip_rrect && clip_radius > 0.5f && clip_rrect->width < 1e7f && clip_rrect->height < 1e7f) {
        Rect rr = *clip_rrect;
        rr.x -= world_origin_.x;
        rr.y -= world_origin_.y;
        rr_mapped = MapRect(rr);
        renderer_->SetClipRect(MapRect(r), clip_radius * scale_, &rr_mapped);
    } else {
        renderer_->SetClipRect(MapRect(r), clip_radius > 0.5f ? clip_radius * scale_ : 0.0f);
    }
}

int UISystem::AllocAutoNode(AutoNode::Kind kind) {
    const int idx = static_cast<int>(auto_nodes_.size());
    AutoNode node;
    node.kind = kind;
    node.box_id = box_sequence_++;
    node.disabled = IsDisabled();
    node.suppressed = suppress_;
    node.layer = manual_modal_ ? manual_modal_layer_ : ((!modal_.empty() || !popup_.empty()) ? 1 : 0);
    node.modal_id = in_modal_ ? modal_ : "";
    node.popup_id = in_popup_ ? popup_ : "";
    if (!active_focus_scope_keys_.empty()) {
        node.focus_scope_id = active_focus_scope_keys_.back();
    } else if (in_modal_ && !modal_.empty()) {
        node.focus_scope_id = "@modal:" + modal_;
    }
    node.align_group_ids = align_stack_;

    node.limits = next_limits_;
    next_limits_ = {};

    const AutoLayoutMod mod = next_auto_mod_;
    next_auto_mod_ = {};

    if (!auto_stack_.empty()) {
        const int p_idx = auto_stack_.back();
        const auto &parent = auto_nodes_[static_cast<std::size_t>(p_idx)];
        node.parent = p_idx;
        node.root_box_id = parent.root_box_id;
        node.box_depth = parent.box_depth + 1;
        node.in_scroll = parent.in_scroll;
        node.est_avail_w = parent.est_avail_w;
        node.alpha_mul = parent.alpha_mul;
        node.layer = std::max(node.layer, parent.layer);
    } else {
        node.root_box_id = node.box_id;
        node.box_depth = 1;
        node.est_avail_w = LogicalWidth();
    }

    node.layout_dir = LayoutDirection();
    node.radius = ThemeMetric("corner_radius");
    if (mod.has_w) {
        node.width_spec = mod.w;
        if (mod.w.kind == UISizeKind::Fixed) {
            node.est_avail_w = mod.w.value;
        }
    }
    if (mod.has_h) {
        node.height_spec = mod.h;
    }
    if (mod.has_cross) {
        node.cross_align = mod.cross;
        node.has_cross_override = true;
    }
    if (mod.has_main) {
        node.main_align = mod.main;
    }
    if (mod.has_dir) {
        node.layout_dir = mod.dir;
    }
    if (mod.has_pad) {
        node.pad_l = mod.pad_l;
        node.pad_t = mod.pad_t;
        node.pad_r = mod.pad_r;
        node.pad_b = mod.pad_b;
    }
    if (mod.has_margin) {
        node.margin_l = mod.margin_l;
        node.margin_t = mod.margin_t;
        node.margin_r = mod.margin_r;
        node.margin_b = mod.margin_b;
    }
    if (mod.has_shrink) {
        node.shrink = mod.shrink;
    }
    if (mod.has_aspect) {
        node.aspect_ratio = mod.aspect_ratio;
    }
    if (mod.has_span) {
        node.col_span = mod.col_span;
    }
    if (mod.has_row_span) {
        node.row_span = mod.row_span;
    }
    if (mod.has_bg) {
        node.has_bg = true;
        node.bg_col = mod.bg_col;
    }
    if (mod.has_grad) {
        node.has_grad = true;
        node.grad_start = mod.grad_start;
        node.grad_end = mod.grad_end;
        node.grad_horiz = mod.grad_horiz;
    }
    if (mod.has_border) {
        node.has_border = true;
        node.custom_border_col = mod.border_col;
        node.border_w = mod.border_w;
    }
    if (mod.has_radius) {
        node.radius = mod.radius;
    }
    if (mod.has_shadow) {
        node.has_shadow = true;
        node.shadow_off_y = mod.shadow_off_y;
        node.shadow_spread = mod.shadow_spread;
        node.shadow_alpha = mod.shadow_alpha;
    }
    if (mod.has_blur) {
        node.backdrop_blur = mod.backdrop_blur;
    }
    if (mod.has_alpha) {
        node.alpha_mul *= mod.alpha_mul;
    }
    if (mod.has_anim) {
        node.anim_key = mod.anim_key;
        node.anim_duration = mod.anim_duration;
    }
    if (mod.has_placeholder) {
        node.placeholder = mod.placeholder;
    }
    if (mod.has_tip) {
        node.tip_text = mod.tip;
    }
    if (mod.has_sem) {
        node.sem_role = mod.sem_role;
        node.sem_label = mod.sem_label;
        node.sem_value = mod.sem_value;
        node.sem_hint = mod.sem_hint;
    }
    if (mod.has_visible) {
        node.visible = mod.visible_val;
    }
    if (mod.has_enabled) {
        node.disabled = !mod.enabled_val;
    }
    node.est_avail_w = std::max(0.0f, node.est_avail_w - node.margin_l - node.margin_r);
    if (node.kind == AutoNode::Kind::Root || node.kind == AutoNode::Kind::AnchorRoot ||
        node.kind == AutoNode::Kind::VStack || node.kind == AutoNode::Kind::HStack ||
        node.kind == AutoNode::Kind::ZStack || node.kind == AutoNode::Kind::Grid ||
        node.kind == AutoNode::Kind::Card || node.kind == AutoNode::Kind::Table ||
        node.kind == AutoNode::Kind::Flow || node.kind == AutoNode::Kind::CustomBox ||
        node.kind == AutoNode::Kind::SafeArea) {
        node.clip_children = true;
    }
    if (mod.has_clip) {
        node.clip_children = mod.clip_val;
    }

    auto_nodes_.push_back(std::move(node));
    if (auto_nodes_.back().parent >= 0) {
        auto_nodes_[static_cast<std::size_t>(auto_nodes_.back().parent)].children.push_back(idx);
    }
    last_auto_node_ = idx;
    return idx;
}

void UISystem::CloseAutoNode(int expected_idx) {
    if (auto_stack_.empty()) {
        return;
    }
    if (expected_idx >= 0 && auto_stack_.back() != expected_idx) {
        // Unwind gracefully if mismatched
        while (!auto_stack_.empty() && auto_stack_.back() != expected_idx) {
            auto_stack_.pop_back();
        }
        if (auto_stack_.empty()) {
            return;
        }
    }
    const int closed_idx = auto_stack_.back();
    auto_stack_.pop_back();
    last_auto_node_ = closed_idx;

    if (!auto_stack_.empty()) {
        return;
    }

    // Closed a top-level auto-layout root: run Pass 1 (Measure) -> Pass 2 (Arrange) -> Pass 3 (Commit)!
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
    const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : 720.0f;
    const float ox = camera_ ? -scale_offset_.x / std::max(1e-6f, scale_) : 0.0f;
    const float oy = camera_ ? -scale_offset_.y / std::max(1e-6f, scale_) : 0.0f;
    const Rect vp_clip{ox, oy, vw, vh};

    MeasureAutoNode(closed_idx, vw, vh);

    auto &root = auto_nodes_[static_cast<std::size_t>(closed_idx)];
    if (root.defer_commit) {
        last_rect_ = {root.anchor_x, root.anchor_y, root.meas_w, root.meas_h};
        return;
    }

    auto compute_root_rect = [&](int ridx) -> Rect {
        const auto &rt = auto_nodes_[static_cast<std::size_t>(ridx)];
        float rw = rt.meas_w;
        if (rt.width_spec.kind == UISizeKind::Fixed) {
            rw = rt.width_spec.value;
        } else if (rt.width_spec.kind == UISizeKind::Percent) {
            rw = vw * rt.width_spec.value;
        } else if (rt.width_spec.kind == UISizeKind::Grow) {
            rw = vw;
        }
        rw = std::clamp(rw, rt.limits.min_w, rt.limits.max_w);

        float rh = rt.meas_h;
        if (rt.height_spec.kind == UISizeKind::Fixed) {
            rh = rt.height_spec.value;
        } else if (rt.height_spec.kind == UISizeKind::Percent) {
            rh = vh * rt.height_spec.value;
        } else if (rt.height_spec.kind == UISizeKind::Grow) {
            rh = vh;
        }
        rh = std::clamp(rh, rt.limits.min_h, rt.limits.max_h);

        float rx = rt.use_screen_origin ? rt.anchor_x : (ox + rt.anchor_x);
        float ry = rt.use_screen_origin ? rt.anchor_y : (oy + rt.anchor_y);
        if (rt.kind == AutoNode::Kind::AnchorRoot) {
            if (rt.h_align == "anchor_to") {
                Rect target{ox + vw * 0.5f, oy + vh * 0.5f, 0.0f, 0.0f};
                if (!rt.anchor_target_key.empty()) {
                    if (const auto it = curr_solved_widgets_.find(rt.anchor_target_key);
                        it != curr_solved_widgets_.end()) {
                        target = it->second.rect;
                    } else if (const auto it2 = prev_solved_widgets_.find(rt.anchor_target_key);
                               it2 != prev_solved_widgets_.end()) {
                        target = it2->second.rect;
                    } else if (const auto it3 = named_rects_.find(rt.anchor_target_key);
                               it3 != named_rects_.end()) {
                        target = it3->second;
                    } else if (const auto it4 = prev_named_rects_.find(rt.anchor_target_key);
                               it4 != prev_named_rects_.end()) {
                        target = it4->second;
                    }
                }
                const std::string &p = rt.v_align;
                const float g = rt.gap;
                if (p == "top_start" || p == "top_center" || p == "top_end" || p == "top") {
                    ry = target.y - g - rh;
                    if (ry < oy && target.y + target.height + g + rh <= oy + vh) {
                        ry = target.y + target.height + g;
                    }
                    rx = (p == "top_end") ? (target.x + target.width - rw)
                         : (p == "top_center" || p == "top") ? (target.x + (target.width - rw) * 0.5f)
                                                               : target.x;
                } else if (p == "right_start" || p == "right") {
                    rx = target.x + target.width + g;
                    if (rx + rw > ox + vw && target.x - g - rw >= ox) {
                        rx = target.x - g - rw;
                    }
                    ry = target.y;
                } else if (p == "left_start" || p == "left") {
                    rx = target.x - g - rw;
                    if (rx < ox && target.x + target.width + g + rw <= ox + vw) {
                        rx = target.x + target.width + g;
                    }
                    ry = target.y;
                } else {
                    ry = target.y + target.height + g;
                    if (ry + rh > oy + vh && target.y - g - rh >= oy) {
                        ry = target.y - g - rh;
                    }
                    rx = (p == "bottom_end") ? (target.x + target.width - rw)
                         : (p == "bottom_center" || p == "bottom")
                             ? (target.x + (target.width - rw) * 0.5f)
                             : target.x;
                }
                rx = std::clamp(rx, ox, std::max(ox, ox + vw - rw));
                ry = std::clamp(ry, oy, std::max(oy, oy + vh - rh));
            } else {
                if (rt.h_align == "stretch") {
                    rx = ox + rt.inset_x;
                    rw = std::max(0.0f, vw - 2.0f * rt.inset_x);
                } else if (rt.h_align == "right") {
                    rx = ox + vw - rw - rt.inset_x;
                } else if (rt.h_align == "center") {
                    rx = ox + (vw - rw) * 0.5f + rt.inset_x;
                } else {
                    rx = ox + rt.inset_x;
                }

                if (rt.v_align == "stretch") {
                    ry = oy + rt.inset_y;
                    rh = std::max(0.0f, vh - 2.0f * rt.inset_y);
                } else if (rt.v_align == "bottom") {
                    ry = oy + vh - rh - rt.inset_y;
                } else if (rt.v_align == "center") {
                    ry = oy + (vh - rh) * 0.5f + rt.inset_y;
                } else {
                    ry = oy + rt.inset_y;
                }
            }
        }
        return {rx, ry, rw, rh};
    };

    const Rect root_rect = compute_root_rect(closed_idx);
    const Rect effective_clip = root.has_clip_override ? IntersectClip(vp_clip, root.clip_override) : vp_clip;
    ArrangeAutoNode(closed_idx, root_rect, effective_clip, root.in_scroll, root.box_id, -1, -1,
                    0.0f, effective_clip);
    ResolveDeepestWheel(closed_idx);

    if (root.is_modal_root && renderer_) {
        ApplyClipRectDirect(vp_clip);
        renderer_->DrawQuad(MapRect(vp_clip), {0.0f, 0.0f, 0.0f, 0.55f});
    }

    if (root.block_input) {
        BlockInput(root.rect);
        if (!root.suppressed && tip_recorder_) {
            tip_recorder_(root.rect, {}, true);
        }
    }

    const bool prev_commit = in_auto_commit_;
    in_auto_commit_ = true;
    std::vector<int> overlay_queue;
    CommitAutoNode(closed_idx, root.in_scroll ? root.rect : vp_clip, false, &overlay_queue);
    if (!overlay_queue.empty()) {
        std::stable_sort(overlay_queue.begin(), overlay_queue.end(), [&](int a, int b) {
            return auto_nodes_[static_cast<std::size_t>(a)].layer <
                   auto_nodes_[static_cast<std::size_t>(b)].layer;
        });
        for (std::size_t oi = 0; oi < overlay_queue.size(); ++oi) {
            const int oidx = overlay_queue[oi];
            const bool is_anchored =
                auto_nodes_[static_cast<std::size_t>(oidx)].h_align == "anchor_to";
            Rect ov_rect{};
            bool ov_block = false;
            bool ov_supp = false;
            if (is_anchored) {
                MeasureAutoNode(oidx, vw, vh);
                ov_rect = compute_root_rect(oidx);
                const auto &on_m = auto_nodes_[static_cast<std::size_t>(oidx)];
                const Rect ov_clip =
                    on_m.has_clip_override ? IntersectClip(vp_clip, on_m.clip_override) : vp_clip;
                const bool ov_in_scroll = on_m.in_scroll;
                const std::uint32_t ov_box_id = on_m.box_id;
                ov_block = on_m.block_input;
                ov_supp = on_m.suppressed;
                ArrangeAutoNode(oidx, ov_rect, ov_clip, ov_in_scroll, ov_box_id, -1, -1, 0.0f,
                                ov_clip);
                ResolveDeepestWheel(oidx);
            }
            const auto &on = auto_nodes_[static_cast<std::size_t>(oidx)];
            const Rect bounds = (!on.has_clip_override && on.parent >= 0)
                                     ? auto_nodes_[static_cast<std::size_t>(on.parent)].content_rect
                                     : vp_clip;
            if (is_anchored && ov_block) {
                BlockInput(ov_rect);
                if (!ov_supp && tip_recorder_) {
                    tip_recorder_(ov_rect, {}, true);
                }
            }
            const std::size_t before_nested = overlay_queue.size();
            CommitAutoNode(oidx, bounds, true, &overlay_queue);
            if (overlay_queue.size() > before_nested) {
                std::stable_sort(overlay_queue.begin() + static_cast<std::ptrdiff_t>(oi + 1),
                                 overlay_queue.end(), [&](int a, int b) {
                                     return auto_nodes_[static_cast<std::size_t>(a)].layer <
                                            auto_nodes_[static_cast<std::size_t>(b)].layer;
                                 });
            }
        }
    }
    in_auto_commit_ = prev_commit;
    ApplyClipRectDirect({0.0f, 0.0f, 1e9f, 1e9f});
    last_rect_ = root.rect;
    if (!pending_scroll_key_.empty()) {
        ScrollToWidget(pending_scroll_key_, pending_scroll_smooth_);
    }
    if (inspector_enabled_ && !recording_) {
        DrawInspectorOverlay();
    }
}

void UISystem::RootBegin(float x, float y, UISize w, UISize h, float pad, bool block_input) {
    const int idx = AllocAutoNode(AutoNode::Kind::Root);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.anchor_x = x;
    node.anchor_y = y;
    node.width_spec = w;
    node.height_spec = h;
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = 0.0f;
    node.block_input = block_input;
    const float p = std::max(0.0f, pad);
    node.pad_l = node.pad_t = node.pad_r = node.pad_b = p;
    const float est_w = (w.kind == UISizeKind::Fixed) ? w.value
                        : (w.kind == UISizeKind::Percent ? LogicalWidth() * w.value : LogicalWidth());
    node.est_avail_w = std::max(0.0f, est_w - 2.0f * p);
    auto_stack_.push_back(idx);
}

void UISystem::RootAnchorBegin(std::string_view h_align, std::string_view v_align, UISize w, UISize h,
                               float inset_x, float inset_y, float pad, bool overlay,
                               bool block_input) {
    const int idx = AllocAutoNode(AutoNode::Kind::AnchorRoot);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.h_align = std::string(h_align);
    node.v_align = std::string(v_align);
    node.width_spec = w;
    node.height_spec = h;
    node.inset_x = inset_x;
    node.inset_y = inset_y;
    node.overlay = overlay;
    node.block_input = block_input;
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = 0.0f;
    const float p = std::max(0.0f, pad);
    node.pad_l = node.pad_t = node.pad_r = node.pad_b = p;
    const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
    const float est_w = (h_align == "stretch") ? std::max(0.0f, vw - 2.0f * inset_x)
                        : (w.kind == UISizeKind::Fixed ? w.value
                           : (w.kind == UISizeKind::Percent ? vw * w.value : vw));
    node.est_avail_w = std::max(0.0f, est_w - 2.0f * p);
    auto_stack_.push_back(idx);
}

void UISystem::RootEnd() {
    CloseAutoNode();
}

namespace {
template <typename Nodes>
UISize ComputeDefaultStackWidth(const std::vector<int> &stack, const Nodes &nodes) {
    if (stack.empty()) {
        return UISize::Grow(1.0f);
    }
    const auto &parent = nodes[static_cast<std::size_t>(stack.back())];
    const bool shrink_w = parent.kind == decltype(parent.kind)::HStack ||
                          ((parent.kind == decltype(parent.kind)::Root ||
                            parent.kind == decltype(parent.kind)::AnchorRoot) &&
                           parent.width_spec.kind == UISizeKind::Auto);
    return shrink_w ? UISize::Auto() : UISize::Grow(1.0f);
}
} // namespace

void UISystem::VStackBegin(float gap, float pad, UIAxisAlign cross_align) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const bool had_cross = next_auto_mod_.has_cross;
    const bool had_pad = next_auto_mod_.has_pad;
    const int idx = AllocAutoNode(AutoNode::Kind::VStack);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = ComputeDefaultStackWidth(auto_stack_, auto_nodes_);
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    if (!had_cross) {
        node.cross_align = cross_align;
    }
    node.gap = gap >= 0.0f ? gap : ThemeMetric("row_gap");
    if (!had_pad && pad > 0.0f) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = pad;
    }
    node.est_avail_w = std::max(0.0f, node.est_avail_w - node.pad_l - node.pad_r);
    auto_stack_.push_back(idx);
}

void UISystem::HStackBegin(float gap, float pad, UIAxisAlign cross_align) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const bool had_cross = next_auto_mod_.has_cross;
    const bool had_pad = next_auto_mod_.has_pad;
    const int idx = AllocAutoNode(AutoNode::Kind::HStack);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = ComputeDefaultStackWidth(auto_stack_, auto_nodes_);
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    if (!had_cross) {
        node.cross_align = cross_align;
    }
    node.gap = gap >= 0.0f ? gap : ThemeMetric("col_gap");
    if (!had_pad && pad > 0.0f) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = pad;
    }
    node.est_avail_w = std::max(0.0f, node.est_avail_w - node.pad_l - node.pad_r);
    auto_stack_.push_back(idx);
}

void UISystem::StackEnd() {
    CloseAutoNode();
}

void UISystem::CardBegin(float pad, float gap) {
    CardBegin(ThemeColor("panel"), ThemeColor("border"), pad, gap);
    auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].themed_panel = true;
}

void UISystem::CardBegin(const Color &fill, const Color &border, float pad, float gap) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const bool had_pad = next_auto_mod_.has_pad;
    const int idx = AllocAutoNode(AutoNode::Kind::Card);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = gap >= 0.0f ? gap : ThemeMetric("row_gap");
    const float p = pad >= 0.0f ? pad : ThemeMetric("card_pad");
    if (!had_pad) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = p;
    }
    node.draw_panel = true;
    node.themed_panel = false;
    node.fill_col = fill;
    node.border_col = border;
    node.est_avail_w = std::max(0.0f, node.est_avail_w - node.pad_l - node.pad_r);
    auto_stack_.push_back(idx);
}

void UISystem::CardEnd() {
    CloseAutoNode();
}

void UISystem::TableBegin(std::string_view key, const std::vector<UISize> &cols,
                          float col_gap, float row_gap) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Table);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = ScopedKey(key);
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    node.col_specs = cols;
    node.col_limits.resize(cols.size());
    node.col_widths.assign(cols.size(), 0.0f);
    node.gap = col_gap >= 0.0f ? col_gap : ThemeMetric("col_gap");
    node.line_gap = row_gap >= 0.0f ? row_gap : ThemeMetric("row_gap");
    auto_stack_.push_back(idx);
}

void UISystem::TableColLimits(std::size_t col, float min_w, float max_w) {
    if (auto_stack_.empty()) {
        return;
    }
    auto &node = auto_nodes_[static_cast<std::size_t>(auto_stack_.back())];
    if (node.kind != AutoNode::Kind::Table || col >= node.col_limits.size()) {
        return;
    }
    node.col_limits[col] = {std::max(0.0f, min_w), std::max(std::max(0.0f, min_w), max_w), 0.0f, 1e9f};
}

void UISystem::TableRowBegin(UIAxisAlign cross_align) {
    const bool had_cross = next_auto_mod_.has_cross;
    const int idx = AllocAutoNode(AutoNode::Kind::TableRow);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Auto();
    if (!had_cross) {
        node.cross_align = cross_align;
    }
    node.row_id = auto_row_seq_++;
    auto_stack_.push_back(idx);
}

void UISystem::TableCellBegin(float pad) {
    const int idx = AllocAutoNode(AutoNode::Kind::TableCell);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Auto();
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = 2.0f;
    if (pad > 0.0f) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = pad;
    }
    auto_stack_.push_back(idx);
}

void UISystem::TableCellEnd() {
    CloseAutoNode();
}

void UISystem::TableRowEnd() {
    CloseAutoNode();
}

void UISystem::TableEnd() {
    CloseAutoNode();
}

void UISystem::ScrollBoxBegin(std::string_view key, UISize view_h) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Scroll);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = ScopedKey(key);
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = view_h;
    }
    node.clip_children = true;
    node.in_scroll = true;
    node.gap = 0.0f;
    node.cross_align = UIAxisAlign::Stretch;
    const float sb = ThemeMetric("scrollbar_width");
    node.est_avail_w = std::max(0.0f, node.est_avail_w - sb);
    auto_stack_.push_back(idx);
}

void UISystem::ScrollBoxEnd() {
    CloseAutoNode();
}

void UISystem::FlowBoxBegin(float col_gap, float row_gap) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Flow);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = UISize::Auto();
    }
    node.gap = col_gap >= 0.0f ? col_gap : ThemeMetric("col_gap");
    node.line_gap = row_gap >= 0.0f ? row_gap : ThemeMetric("row_gap");
    node.cross_align = UIAxisAlign::Center;
    auto_stack_.push_back(idx);
}

void UISystem::FlowBoxEnd() {
    CloseAutoNode();
}

void UISystem::CanvasBegin(UISize w, UISize h, float pad) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const bool had_pad = next_auto_mod_.has_pad;
    const bool in_hbox = !auto_stack_.empty() &&
                         auto_nodes_[static_cast<std::size_t>(auto_stack_.back())].kind == AutoNode::Kind::HStack;
    const int idx = AllocAutoNode(AutoNode::Kind::CustomBox);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = (w.kind == UISizeKind::Auto && !in_hbox) ? ComputeDefaultStackWidth(auto_stack_, auto_nodes_) : w;
    }
    if (!had_h) {
        node.height_spec = h;
    }
    if (!had_pad && pad > 0.0f) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = pad;
    }
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = 0.0f;
    if (node.width_spec.kind == UISizeKind::Fixed) {
        node.est_avail_w = std::max(0.0f, node.width_spec.value - node.pad_l - node.pad_r);
    } else {
        node.est_avail_w = std::max(0.0f, node.est_avail_w - node.pad_l - node.pad_r);
    }
    auto_stack_.push_back(idx);
}

void UISystem::CanvasEnd() {
    CloseAutoNode();
}

void UISystem::Spacer(float px) {
    if (!InAutoLayout()) {
        return;
    }
    const int idx = AllocAutoNode(AutoNode::Kind::Spacer);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (px < 0.0f) {
        node.width_spec = UISize::Grow(1.0f);
        node.height_spec = UISize::Grow(1.0f);
    } else {
        node.width_spec = UISize::Fixed(px);
        node.height_spec = UISize::Fixed(px);
    }
}

void UISystem::Divider(const Color &color, float thickness) {
    if (!InAutoLayout()) {
        return;
    }
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Divider);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = UISize::Fixed(std::max(1.0f, thickness));
    }
    node.fill_col = color;
}

bool UISystem::ModalBegin(std::string_view key, UISize w, UISize h) {
    if (modal_ != key || !camera_) {
        return false;
    }
    if (input_ && (input_->KeyPressed("escape") || input_->GamepadButtonPressed(0, "b"))) {
        if (recording_) {
            dismissed_overlay_ = true;
        }
        CloseModal();
        return false;
    }
    in_modal_ = true;
    const float pad = ThemeMetric("padding");
    RootAnchorBegin("center", "center", w, h, 0.0f, 0.0f, pad, true, true);
    if (!auto_stack_.empty()) {
        auto &root = auto_nodes_[static_cast<std::size_t>(auto_stack_.back())];
        root.is_modal_root = true;
        root.layer = 100;
        root.gap = ThemeMetric("spacing");
    }
    ThemedPanel();
    return true;
}

bool UISystem::ListView(std::string_view key, UISize view_h) {
    if (!InAutoLayout()) {
        return false;
    }
    const std::string k(key);
    if (list_views_.count(k)) {
        return false;
    }
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::ListViewSlot);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = k;
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = view_h;
    }
    node.clip_children = true;
    ListViewport view;
    view.modal = in_modal_ ? modal_ : "";
    view.popup = in_popup_ ? popup_ : "";
    view.disabled = IsDisabled();
    list_views_.emplace(k, std::move(view));
    return true;
}

void UISystem::NextSize(UISize w, UISize h) {
    next_auto_mod_.has_w = true;
    next_auto_mod_.w = w;
    next_auto_mod_.has_h = true;
    next_auto_mod_.h = h;
}

void UISystem::NextWidth(UISize w) {
    next_auto_mod_.has_w = true;
    next_auto_mod_.w = w;
}

void UISystem::NextHeight(UISize h) {
    next_auto_mod_.has_h = true;
    next_auto_mod_.h = h;
}

void UISystem::NextGrow(float weight) {
    next_auto_mod_.has_w = true;
    next_auto_mod_.w = UISize::Grow(weight);
}

void UISystem::NextAlign(UIAxisAlign cross_align) {
    next_auto_mod_.has_cross = true;
    next_auto_mod_.cross = cross_align;
}

void UISystem::NextMainAlign(UIAxisAlign main_align) {
    next_auto_mod_.has_main = true;
    next_auto_mod_.main = main_align;
}

void UISystem::NextPad(float l, float t, float r, float b) {
    next_auto_mod_.has_pad = true;
    next_auto_mod_.pad_l = std::max(0.0f, l);
    next_auto_mod_.pad_t = std::max(0.0f, t);
    next_auto_mod_.pad_r = std::max(0.0f, r);
    next_auto_mod_.pad_b = std::max(0.0f, b);
}

// ---------------------------------------------------------------------------
// Immediate-mode hit-test against previous frame's Pass-2 solved geometry
// ---------------------------------------------------------------------------

bool UISystem::HitTestSolvedKey(std::string_view key, bool text, bool adjustable, bool focusable,
                              bool release_anywhere) {
    (void)adjustable;
    (void)focusable;
    if (recording_ || !camera_ || !renderer_ || !input_) {
        return false;
    }
    const auto it = prev_solved_widgets_.find(std::string(key));
    if (it == prev_solved_widgets_.end()) {
        return false;
    }
    const auto &info = it->second;
    if (info.disabled || (!modal_.empty() && modal_ != info.modal) ||
        (!popup_.empty() && popup_ != info.popup)) {
        return false;
    }
    const Rect vis = IntersectClip(info.rect, info.clip_rect);
    if (vis.width <= 0.0f || vis.height <= 0.0f) {
        return false;
    }
    const Vec2 mouse = Mouse();
    const bool hover = vis.Contains(mouse) && CanInteract() && MouseVisible();
    if (hover && input_->MousePressed(0)) {
        nav_focus_ = key;
        if (!text) {
            focused_.clear();
        }
    }
    const bool activate = CanInteract() && nav_focus_ == key && nav_activate_ && !text;
    WidgetState &state = widgets_[std::string(key)];
    bool clicked = false;
    if (release_anywhere) {
        const bool mouse_down = CanInteract() && input_->MouseDown(0);
        const bool mouse_released = input_->MouseReleased(0);
        if (hover) {
            pointer_over_ = true;
        }
        if (state.pressed && mouse_released) {
            clicked = true;
            ++click_count_;
            state.pressed = false;
        } else if (hover) {
            state.pressed = (mouse_down && (!resolving_ || input_->MousePressed(0))) ||
                            (state.pressed && !mouse_released);
        } else if (!mouse_down) {
            state.pressed = false;
        }
        state.hover = hover;
    } else {
        clicked = TrackPressWidget(state, hover);
    }
    const bool fired = clicked || activate;
    frame_results_[std::string(key)] = fired ? 1.0f : 0.0f;
    if (fired && !text && !adjustable) {
        activated_widget_ = key;
    }
    return fired;
}

// ---------------------------------------------------------------------------
// Auto-Layout Leaf Creators
// ---------------------------------------------------------------------------

float UISystem::AutoText(std::string_view text, float scale, UITextOverflow overflow, int align,
                         const Color &tint, float min_scale) {
    const float sc = AutoWidgetTextScale(scale);
    const int idx = AllocAutoNode(AutoNode::Kind::Text);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.text = std::string(text);
    node.scale = sc;
    node.min_scale = std::clamp(min_scale, 0.0f, sc);
    node.overflow = overflow;
    node.text_align = align;
    node.fill_col = tint;
    if (node.width_spec.kind == UISizeKind::Auto && overflow == UITextOverflow::Wrap) {
        // Wrap text stretches to parent width unless explicit fixed width given
        if (node.parent >= 0) {
            const auto pkind = auto_nodes_[static_cast<std::size_t>(node.parent)].kind;
            if (pkind == AutoNode::Kind::VStack || pkind == AutoNode::Kind::Root ||
                pkind == AutoNode::Kind::AnchorRoot || pkind == AutoNode::Kind::Scroll ||
                pkind == AutoNode::Kind::Card || pkind == AutoNode::Kind::CustomBox) {
                node.width_spec = UISize::Grow(1.0f);
            }
        }
    }
    // Return estimated height for callers checking Text() return
    const float w = std::max(0.0f, node.est_avail_w);
    if (overflow == UITextOverflow::Shrink && renderer_ && !text.empty()) {
        const float natural = renderer_->MeasureText(text, sc);
        const float used = natural > w && natural > 0.0f
                               ? std::max(node.min_scale, sc * w / natural) : sc;
        return FontLineHeightPx(used);
    }
    if (overflow == UITextOverflow::Wrap && renderer_ && w > 0.0f && !text.empty()) {
        return renderer_->GetFont().MeasureTextEx(text, sc, w);
    }
    if (overflow == UITextOverflow::Wrap && w <= 0.0f) {
        return 0.0f;
    }
    return text.empty() ? 0.0f : FontLineHeightPx(sc);
}

bool UISystem::AutoButton(std::string_view key, UISize h_spec, std::string_view text, float scale) {
    const float sc = AutoWidgetTextScale(scale);
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Button);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    node.text = std::string(text);
    node.scale = sc;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    node.has_btn_colors = true;
    node.btn_col = ThemeColor("button");
    node.btn_hover = ThemeColor("hover");
    node.btn_pressed = ThemeColor("pressed");
    node.btn_border = ThemeColor("border");
    node.btn_text = ThemeColor("text");

    return HitTestSolvedKey(skey, false, false, true);
}

float UISystem::AutoSlider(std::string_view key, UISize h_spec, float value, float min_v, float max_v) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Slider);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = h_spec;
    }
    node.min_val = min_v;
    node.max_val = max_v;

    float next = (max_v > min_v) ? std::clamp(value, min_v, max_v) : min_v;
    if (max_v > min_v) {
        if (!recording_ && camera_ && renderer_ && input_) {
            const auto it = prev_solved_widgets_.find(node.key);
            if (it != prev_solved_widgets_.end() && !it->second.disabled) {
                const Rect r = it->second.rect;
                const Rect vis = IntersectClip(r, it->second.clip_rect);
                FocusWidget(skey, r, false, true);
                if (CanInteract() && nav_focus_ == skey) {
                    if (input_->KeyPressed("left") || input_->GamepadButtonPressed(0, "dpad_left")) {
                        next -= (max_v - min_v) * 0.05f;
                    }
                    if (input_->KeyPressed("right") || input_->GamepadButtonPressed(0, "dpad_right")) {
                        next += (max_v - min_v) * 0.05f;
                    }
                    next += static_cast<float>(nav_adjust_step_) * (max_v - min_v) * 0.05f;
                    next = std::clamp(next, min_v, max_v);
                }
                WidgetState &st = widgets_[node.key];
                const float thumb_w = r.height * 0.6f;
                const float range_px = std::max(1.0f, r.width - thumb_w);
                const float t = (next - min_v) / (max_v - min_v);
                const Rect thumb{r.x + t * range_px, r.y, thumb_w, r.height};
                const Vec2 mouse = Mouse();
                const bool hover = vis.width > 0 && vis.Contains(mouse) && MouseVisible();
                const bool mouse_down = CanInteract() && input_->MouseDown(0);
                const bool mouse_pressed = CanInteract() && input_->MousePressed(0);
                const bool mouse_released = input_->MouseReleased(0);
                if (mouse_pressed && hover) {
                    ++click_count_;
                    st.dragging = true;
                    st.pressed = true;
                    if (thumb.Contains(mouse)) {
                        st.grab = mouse.x - thumb.x;
                    } else {
                        next = std::clamp(min_v + (mouse.x - r.x - thumb_w * 0.5f) / range_px * (max_v - min_v),
                                          min_v, max_v);
                        st.grab = thumb_w * 0.5f;
                    }
                }
                if (st.dragging && mouse_down) {
                    next = std::clamp(min_v + (mouse.x - r.x - st.grab) / range_px * (max_v - min_v),
                                      min_v, max_v);
                }
                if (mouse_released || !mouse_down) {
                    st.dragging = false;
                    st.pressed = false;
                }
                if (hover || st.dragging) {
                    pointer_over_ = true;
                }
                st.hover = hover;
            }
        }
    }
    node.val = next;
    frame_results_[node.key] = next;
    return next;
}

bool UISystem::AutoCheckbox(std::string_view key, UISize sz_spec, bool checked,
                            std::string_view label, float scale, bool is_radio) {
    const float sc = AutoWidgetTextScale(scale);
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(is_radio ? AutoNode::Kind::RadioButton : AutoNode::Kind::Checkbox);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    node.checked = checked;
    node.text = std::string(label);
    node.scale = sc;
    if (!had_h) {
        node.height_spec = sz_spec;
    }
    return HitTestSolvedKey(skey, false, false, true);
}

bool UISystem::AutoTextInput(std::string_view key, UISize h_spec, std::size_t max_chars) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::TextInput);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = h_spec;
    }
    node.max_chars = max_chars;
    return !recording_ && input_ && CanInteract() && focused_ == skey && input_->KeyPressed("enter");
}

void UISystem::AutoProgressBar(UISize h_spec, float value, const Color &fill, const Color &track) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::ProgressBar);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = h_spec;
    }
    node.val = std::clamp(value, 0.0f, 1.0f);
    node.fill_col = fill;
    node.track_col = track;
}

bool UISystem::AutoTab(std::string_view key, UISize h_spec, std::string_view label, bool active,
                       float scale) {
    const float sc = AutoWidgetTextScale(scale);
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Tab);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    node.text = std::string(label);
    node.checked = active;
    node.scale = sc;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(skey, false, false, true);
}

bool UISystem::AutoTree(std::string_view key, UISize h_spec, std::string_view label, bool open,
                        float scale) {
    const float sc = AutoWidgetTextScale(scale);
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Tree);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    node.text = std::string(label);
    node.checked = open;
    node.scale = sc;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(skey, false, false, true);
}

bool UISystem::AutoCombo(std::string_view key, UISize h_spec, std::string_view label,
                         bool open_or_sel, float scale, bool is_item) {
    const float sc = AutoWidgetTextScale(scale);
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(is_item ? AutoNode::Kind::ComboItem : AutoNode::Kind::ComboBegin);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    node.text = std::string(label);
    node.checked = open_or_sel;
    node.scale = sc;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(skey, false, false, true, !is_item);
}

void UISystem::AutoImage(const Texture2D *texture, const Rect &src, UISize h_spec, const Color &tint) {
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Image);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.texture = texture;
    node.tex_src = src;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    node.fill_col = tint;
}

bool UISystem::AutoImageButton(std::string_view key, const Texture2D *texture, const Rect &src,
                               UISize h_spec, std::string_view label) {
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::ImageButton);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    node.texture = texture;
    node.tex_src = src;
    node.text = std::string(label);
    node.scale = ThemeMetric("font_scale");
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(skey, false, false, true);
}

std::string UISystem::AutoRichText(std::string_view markup, float scale) {
    const float sc = AutoWidgetTextScale(scale);
    const int idx = AllocAutoNode(AutoNode::Kind::RichText);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = "@rich/" + std::to_string(idx);
    node.text = std::string(markup);
    node.scale = sc;
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Auto();
    if (!recording_) {
        const auto previous = prev_solved_widgets_.find(node.key);
        if (previous != prev_solved_widgets_.end()) {
            node.input_processed = true;
            DrawRichTextSolved(node, previous->second.rect, previous->second.clip_rect, false, true);
        }
    }
    return clicked_link_;
}

float UISystem::AutoSplitter(std::string_view key, UISize h_spec, float initial, float minimum) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Splitter);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    const std::string skey = ScopedKey(key);
    node.key = skey;
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = h_spec.kind == UISizeKind::Auto ? UISize::Fixed(ThemeMetric("slider_height")) : h_spec;
    }
    auto [it, inserted] = widgets_.try_emplace(node.key);
    auto &state = it->second;
    if (inserted) {
        state.grab = initial;
    }
    if (!recording_ && camera_ && renderer_ && input_) {
        const auto pit = prev_solved_widgets_.find(node.key);
        if (pit != prev_solved_widgets_.end() && !pit->second.disabled) {
            const Rect r = pit->second.rect;
            const float eff_min = std::clamp(minimum, 0.0f, r.width * 0.5f);
            state.grab = std::clamp(state.grab, eff_min, std::max(eff_min, r.width - eff_min));
            if (CanInteract() && nav_focus_ == skey) {
                state.grab = std::clamp(state.grab + nav_adjust_step_ * 8.0f, eff_min, r.width - eff_min);
            }
            const Rect divider{r.x + state.grab - 3.0f, r.y, 6.0f, r.height};
            const Rect vis = IntersectClip(divider, pit->second.clip_rect);
            if (CanInteract()) {
                if (MouseVisible() && vis.width > 0.0f && vis.Contains(Mouse()) && input_->MousePressed(0)) {
                    state.dragging = true;
                    nav_focus_ = node.key;
                }
                if (state.dragging && input_->MouseDown(0)) {
                    state.grab = std::clamp(Mouse().x - r.x, eff_min, std::max(eff_min, r.width - eff_min));
                }
                if (!input_->MouseDown(0)) {
                    state.dragging = false;
                }
                if (nav_focus_ == skey) {
                    if (input_->KeyPressed("left")) {
                        state.grab = std::max(eff_min, state.grab - 8.0f);
                    }
                    if (input_->KeyPressed("right")) {
                        state.grab = std::min(r.width - eff_min, state.grab + 8.0f);
                    }
                }
            } else {
                state.dragging = false;
            }
            if (state.dragging || (MouseVisible() && vis.Contains(Mouse()))) {
                pointer_over_ = true;
            }
        }
    }
    node.val = state.grab;
    node.min_val = minimum;
    frame_results_[node.key] = state.grab;
    return state.grab;
}

bool UISystem::AutoWindow(std::string_view key, std::string_view title, float scale) {
    const float sc = AutoWidgetTextScale(scale);
    const int idx = AllocAutoNode(AutoNode::Kind::Window);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = ScopedKey(key);
    node.text = std::string(title);
    node.scale = sc;
    node.width_spec = UISize::Grow(1.0f);
    const float bar_h = std::max(22.0f, FontLineHeightPx(sc) + 10.0f);
    node.height_spec = UISize::Fixed(bar_h);

    bool dragging = false;
    if (!recording_ && camera_ && renderer_ && input_) {
        const auto pit = prev_solved_widgets_.find(node.key);
        WidgetState &state = widgets_[node.key];
        if (pit != prev_solved_widgets_.end() && !pit->second.disabled) {
            const Rect &bar = pit->second.rect;
            const Rect vis = IntersectClip(bar, pit->second.clip_rect);
            const Vec2 mouse = Mouse();
            const bool hover_bar = vis.width > 0.0f && vis.Contains(mouse) && MouseVisible();
            const bool mouse_down = CanInteract() && input_->MouseDown(0);
            const bool mouse_pressed = CanInteract() && input_->MousePressed(0);
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
            if (hover_bar || state.dragging) {
                pointer_over_ = true;
            }
        }
    }
    frame_results_[node.key] = dragging ? 1.0f : 0.0f;
    return dragging;
}

bool UISystem::AutoTextArea(std::string_view key, UISize h_spec, std::size_t max_chars) {
    const float sc = ThemeMetric("font_scale");
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::TextArea);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = ScopedKey(key);
    node.max_chars = max_chars;
    node.scale = sc;
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        const float def_h = FontLineHeightPx(sc) * 4.0f + 2.0f * ThemeMetric("input_pad_y");
        node.height_spec = (h_spec.kind == UISizeKind::Auto) ? UISize::Fixed(def_h) : h_spec;
    }
    // Immediate TextArea reports edits in this call, not one frame later.
    // Use the same previous-frame geometry policy as immediate sliders, then
    // paint at commit without consuming the keyboard event a second time.
    if (!recording_ && !node.disabled && input_ && CanInteract()) {
        const auto previous = prev_solved_widgets_.find(node.key);
        if (previous != prev_solved_widgets_.end() && !previous->second.disabled) {
            node.input_processed = true;
            return EditText(node.key, previous->second.rect, node.max_chars,
                            true, false, false, true, node.placeholder,
                            node.radius, node.alpha_mul);
        }
    }
    return false;
}

bool UISystem::AutoTextPassword(std::string_view key, UISize h_spec, std::size_t max_chars) {
    const float sc = ThemeMetric("font_scale");
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::TextPassword);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = ScopedKey(key);
    node.max_chars = max_chars;
    node.scale = sc;
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        const float def_h = FontLineHeightPx(sc) + 2.0f * ThemeMetric("input_pad_y");
        node.height_spec = (h_spec.kind == UISizeKind::Auto) ? UISize::Fixed(def_h) : h_spec;
    }
    return false;
}

namespace {
std::size_t StepUtf8(std::string_view text, std::size_t i) {
    if (i >= text.size()) {
        return text.size();
    }
    ++i;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) {
        ++i;
    }
    return i;
}
} // namespace

float UISystem::MeasureRichTextHeight(std::string_view markup, float scale, float avail_w) {
    if (!renderer_ || markup.empty() || avail_w <= 0.0f) {
        return 0.0f;
    }
    const float lh = FontLineHeightPx(scale);
    const auto font_revision = renderer_->GetFont().Revision();
    if (rich_advance_scale_ != scale || rich_advance_revision_ != font_revision ||
        rich_advances_.size() > 4096) {
        rich_advances_.clear();
        rich_advance_scale_ = scale;
        rich_advance_revision_ = font_revision;
    }
    float x = 0.0f, y = 0.0f;
    for (std::size_t i = 0; i < markup.size();) {
        bool image = false;
        float w = 0.0f;
        std::string_view ch;
        if (markup[i] == '[') {
            const auto end = markup.find(']', i);
            if (end != std::string_view::npos) {
                const auto tag = markup.substr(i + 1, end - i - 1);
                bool consumed = true;
                if (tag == "b" || tag == "/b" || tag == "/color" || tag == "/link" ||
                    tag.substr(0, 7) == "color=#" || tag.substr(0, 5) == "link=") {
                } else if (tag.substr(0, 4) == "img=") {
                    if (rich_icons_.count(std::string(tag.substr(4)))) {
                        image = true;
                        w = lh;
                    }
                } else {
                    consumed = false;
                }
                if (consumed) {
                    i = end + 1;
                    if (!image) {
                        continue;
                    }
                }
            }
        }
        if (!image) {
            const auto next = StepUtf8(markup, i);
            ch = markup.substr(i, next - i);
            i = next;
            if (ch == "\n") {
                x = 0.0f;
                y += lh;
                continue;
            }
            auto entry = rich_advances_.emplace(std::string(ch), 0.0f);
            if (entry.second) {
                entry.first->second = renderer_->MeasureText(ch, scale);
            }
            w = entry.first->second;
            ++frame_stats_.rich_glyphs;
        }
        if (x > 0.0f && x + w > avail_w) {
            x = 0.0f;
            y += lh;
        }
        x += w;
    }
    return y + lh;
}

void UISystem::DrawRichTextSolved(const AutoNode &node, const Rect &r, const Rect &clip,
                                  bool allow_draw, bool allow_input) {
    if (!renderer_ || r.width <= 0.0f || node.text.empty()) {
        return;
    }
    const float sc = node.scale;
    const float lh = FontLineHeightPx(sc);
    const float width = r.width;
    const std::string &markup = node.text;
    float x = 0.0f, y = 0.0f;
    bool bold = false;
    std::string link;
    Color color = ThemeColor("text");
    std::vector<Color> colors;
    std::string run;
    float run_x = 0.0f, run_y = 0.0f, run_width = 0.0f;
    std::size_t run_end = 0;

    const Rect text_clip = IntersectClip(clip, r);
    if (allow_draw) ApplyClipRectDirect(text_clip);

    auto flush = [&](bool image_run = false) {
        if (run.empty()) {
            return;
        }
        const Rect rr{r.x + run_x, r.y + run_y, std::min(run_width, width), lh};
        if (!link.empty()) {
            const std::string key = "rich/" + link;
            const std::string state_key = key + "/" + std::to_string(run_end);
            const Rect vis = IntersectClip(rr, text_clip);
            if (recording_) {
                deferred_rich_links_.push_back({link, key, state_key, rr, vis, -1});
            } else if (allow_input) {
                auto &st = widgets_[state_key];
                const bool hover = !node.disabled && vis.width > 0.0f && vis.Contains(Mouse()) && CanInteract() && MouseVisible();
                const bool activate = !node.disabled && CanInteract() && nav_focus_ == key && nav_activate_;
                if (activate || (hover && st.pressed && input_ && input_->MouseReleased(0))) {
                    clicked_link_ = link;
                }
                st.pressed = hover && input_ && CanInteract() && input_->MouseDown(0);
                if (!node.disabled && (modal_.empty() || modal_ == node.modal_id) &&
                    (popup_.empty() || popup_ == node.popup_id) && vis.width > 0.0f && vis.height > 0.0f) {
                    if (std::none_of(focus_ring_.begin(), focus_ring_.end(),
                                     [&](const FocusEntry &fe) { return fe.key == key; })) {
                        focus_ring_.push_back({key, rr, false, false, node.focus_scope_id});
                    }
                }
                if (hover && input_ && input_->MousePressed(0)) {
                    nav_focus_ = key;
                }
            }
            if (allow_draw) FocusIndicator(key, rr);
            Color uc = color;
            if (node.disabled) uc.a *= 0.4f;
            if (allow_draw) renderer_->DrawQuad(MapRect({rr.x, rr.y + lh - 2.0f, rr.width, 1.0f}), uc);
        }
        if (!image_run && allow_draw) {
            Color tc = color;
            if (node.disabled) tc.a *= 0.4f;
            renderer_->DrawText(run, rr.x * scale_ + scale_offset_.x, rr.y * scale_ + scale_offset_.y,
                                sc * scale_, tc, false);
            if (bold) {
                renderer_->DrawText(run, (rr.x + 0.7f) * scale_ + scale_offset_.x,
                                    rr.y * scale_ + scale_offset_.y, sc * scale_, tc, false);
            }
            ++frame_stats_.rich_batches;
        }
        run.clear();
        run_width = 0.0f;
    };

    for (std::size_t i = 0; i < markup.size();) {
        bool image = false;
        Slice icon{};
        float w = 0.0f;
        std::string_view ch;
        if (markup[i] == '[') {
            flush();
            const auto end = markup.find(']', i);
            if (end != std::string_view::npos) {
                const auto tag = markup.substr(i + 1, end - i - 1);
                bool consumed = true;
                if (tag == "b") {
                    bold = true;
                } else if (tag == "/b") {
                    bold = false;
                } else if (tag == "/color") {
                    if (!colors.empty()) {
                        color = colors.back();
                        colors.pop_back();
                    }
                } else if (tag.substr(0, 7) == "color=#" && (tag.size() == 13 || tag.size() == 15)) {
                    const auto hex = std::string(tag.substr(7));
                    char *stop = nullptr;
                    const auto value = std::strtoul(hex.c_str(), &stop, 16);
                    if (stop && *stop == 0) {
                        colors.push_back(color);
                        if (hex.size() == 6) {
                            color = {((value >> 16) & 255) / 255.0f, ((value >> 8) & 255) / 255.0f,
                                     (value & 255) / 255.0f, 1.0f};
                        } else {
                            color = {((value >> 24) & 255) / 255.0f, ((value >> 16) & 255) / 255.0f,
                                     ((value >> 8) & 255) / 255.0f, (value & 255) / 255.0f};
                        }
                    }
                } else if (tag.substr(0, 5) == "link=") {
                    link = std::string(tag.substr(5));
                } else if (tag == "/link") {
                    link.clear();
                } else if (tag.substr(0, 4) == "img=") {
                    auto it = rich_icons_.find(std::string(tag.substr(4)));
                    if (it != rich_icons_.end()) {
                        image = true;
                        icon = it->second;
                        w = lh;
                    }
                } else {
                    consumed = false;
                }
                if (consumed) {
                    i = end + 1;
                    if (!image) {
                        continue;
                    }
                }
            }
        }
        if (!image) {
            const auto next = StepUtf8(markup, i);
            ch = std::string_view(markup).substr(i, next - i);
            i = next;
            if (ch == "\n") {
                flush();
                x = 0.0f;
                y += lh;
                continue;
            }
            auto entry = rich_advances_.find(std::string(ch));
            w = entry != rich_advances_.end() ? entry->second : renderer_->MeasureText(ch, sc);
        }
        if (x > 0.0f && x + w > width) {
            flush();
            x = 0.0f;
            y += lh;
        }
        const Rect rr{r.x + x, r.y + y, std::min(w, width), lh};
        if (image) {
            flush();
            if (!link.empty()) {
                run_x = x;
                run_y = y;
                run_width = w;
                run_end = i;
                run = " ";
                flush(true);
            }
            if (allow_draw && icon.texture && icon.texture->IsValid()) {
                Color col = color;
                if (node.disabled) col.a *= 0.4f;
                renderer_->DrawSprite(*icon.texture, icon.source, MapRect(rr), col);
            }
        } else {
            if (run.empty()) {
                run_x = x;
                run_y = y;
            }
            run.append(ch);
            run_width += w;
            run_end = i;
        }
        x += w;
    }
    flush();
}

// ---------------------------------------------------------------------------
// Public Heightless Two-Pass Widget Entry Points
// ---------------------------------------------------------------------------

bool UISystem::Button(std::string_view key, std::string_view text, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoButton(key, UISize::Auto(), text, scale);
}

float UISystem::Slider(std::string_view key, float value, float min_value, float max_value) {
    if (!InAutoLayout()) {
        return value;
    }
    return AutoSlider(key, UISize::Auto(), value, min_value, max_value);
}

bool UISystem::Checkbox(std::string_view key, bool checked, std::string_view label, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoCheckbox(key, UISize::Auto(), checked, label, scale, false);
}

bool UISystem::RadioButton(std::string_view key, bool selected, std::string_view label, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoCheckbox(key, UISize::Auto(), selected, label, scale, true);
}

bool UISystem::TextInput(std::string_view key, std::size_t max_chars) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoTextInput(key, UISize::Auto(), max_chars);
}

bool UISystem::TextArea(std::string_view key, std::size_t max_chars) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoTextArea(key, UISize::Auto(), max_chars);
}

bool UISystem::TextPassword(std::string_view key, std::size_t max_chars) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoTextPassword(key, UISize::Auto(), max_chars);
}

float UISystem::Splitter(std::string_view key, float initial, float minimum) {
    if (!InAutoLayout()) {
        auto [it, inserted] = widgets_.try_emplace(std::string(key));
        if (inserted) {
            it->second.grab = initial;
        }
        return it->second.grab;
    }
    return AutoSplitter(key, UISize::Auto(), initial, minimum);
}

void UISystem::ProgressBar(float value, const Color &fill, const Color &track) {
    if (!InAutoLayout()) {
        return;
    }
    AutoProgressBar(UISize::Auto(), value, fill, track);
}

bool UISystem::Tab(std::string_view key, std::string_view label, bool active, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoTab(key, UISize::Auto(), label, active, scale);
}

bool UISystem::Tree(std::string_view key, std::string_view label, bool open, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoTree(key, UISize::Auto(), label, open, scale);
}

bool UISystem::ComboBegin(std::string_view key, std::string_view label, bool open, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoCombo(key, UISize::Auto(), label, open, scale, false);
}

bool UISystem::ComboItem(std::string_view key, std::string_view label, bool selected, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoCombo(key, UISize::Auto(), label, selected, scale, true);
}

void UISystem::Image(const Texture2D *texture, const Rect &source, const Color &tint) {
    if (!InAutoLayout()) {
        return;
    }
    AutoImage(texture, source, UISize::Auto(), tint);
}

bool UISystem::ImageButton(std::string_view key, const Texture2D *texture, const Rect &source,
                           std::string_view label) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoImageButton(key, texture, source, UISize::Auto(), label);
}

bool UISystem::Window(std::string_view key, std::string_view title, float scale) {
    if (!InAutoLayout()) {
        return false;
    }
    return AutoWindow(key, title, scale);
}

float UISystem::Text(std::string_view text, float scale, UITextOverflow overflow, int align,
                     const Color &tint, float min_scale) {
    if (!InAutoLayout()) {
        return 0.0f;
    }
    return AutoText(text, scale, overflow, align, tint, min_scale);
}

std::string UISystem::RichText(std::string_view markup, float scale) {
    if (!InAutoLayout()) {
        return {};
    }
    return AutoRichText(markup, scale);
}

// ---------------------------------------------------------------------------
// Pass 1: Bottom-Up Measure
// ---------------------------------------------------------------------------

void UISystem::MeasureAutoNode(int idx, float avail_w, float avail_h) {
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!node.visible) {
        node.meas_w = 0.0f;
        node.meas_h = 0.0f;
        node.baseline = 0.0f;
        return;
    }
    if (node.kind == AutoNode::Kind::CachedBox && node.cached_hit) {
        return;
    }
    const float pad_x = node.pad_l + node.pad_r;
    const float pad_y = node.pad_t + node.pad_b;
    const float mar_x = node.margin_l + node.margin_r;
    const float mar_y = node.margin_t + node.margin_b;
    const float vis_avail_w = (avail_w < 1e7f) ? std::max(0.0f, avail_w - mar_x) : avail_w;
    const float vis_avail_h = (avail_h < 1e7f) ? std::max(0.0f, avail_h - mar_y) : avail_h;

    auto apply_explicit_size = [&](float w, float h) {
        const bool fixed_w = node.width_spec.kind == UISizeKind::Fixed ||
                             (node.width_spec.kind == UISizeKind::Percent && vis_avail_w < 1e7f);
        const bool fixed_h = node.height_spec.kind == UISizeKind::Fixed ||
                             (node.height_spec.kind == UISizeKind::Percent && vis_avail_h < 1e7f);
        if (node.width_spec.kind == UISizeKind::Fixed) {
            w = node.width_spec.value;
        } else if (node.width_spec.kind == UISizeKind::Percent && vis_avail_w < 1e7f) {
            w = vis_avail_w * node.width_spec.value;
        }
        if (node.height_spec.kind == UISizeKind::Fixed) {
            h = node.height_spec.value;
        } else if (node.height_spec.kind == UISizeKind::Percent && vis_avail_h < 1e7f) {
            h = vis_avail_h * node.height_spec.value;
        }
        if (node.aspect_ratio > 0.0f) {
            if (fixed_h && !fixed_w) {
                w = h * node.aspect_ratio;
            } else if (w > 0.0f) {
                h = w / node.aspect_ratio;
            }
        }
        const float cw = std::clamp(w, node.limits.min_w, node.limits.max_w);
        const float ch = std::clamp(h, node.limits.min_h, node.limits.max_h);
        node.meas_w = cw + mar_x;
        node.meas_h = ch + mar_y;
    };

    const float target_w =
        node.width_spec.kind == UISizeKind::Fixed
            ? node.width_spec.value
            : (node.width_spec.kind == UISizeKind::Percent && vis_avail_w < 1e7f
                   ? vis_avail_w * node.width_spec.value
                   : vis_avail_w);
    const float target_h =
        node.height_spec.kind == UISizeKind::Fixed
            ? node.height_spec.value
            : (node.height_spec.kind == UISizeKind::Percent && vis_avail_h < 1e7f
                   ? vis_avail_h * node.height_spec.value
                   : vis_avail_h);
    const float inner_avail_w = std::max(0.0f, std::min(target_w, node.limits.max_w) - pad_x);
    const float inner_avail_h = std::max(0.0f, std::min(target_h, node.limits.max_h) - pad_y);

    switch (node.kind) {
    case AutoNode::Kind::Text: {
        const float sc = AutoWidgetTextScale(node.scale);
        const float line_h = FontLineHeightPx(sc);
        const float nat_w = renderer_ ? renderer_->MeasureText(node.text, sc) : 0.0f;
        node.baseline = node.margin_t + node.pad_t + line_h * 0.8f;
        if (node.overflow == UITextOverflow::Wrap) {
            const float wrap_w = (inner_avail_w < 1e7f) ? inner_avail_w : nat_w;
            const float w = (wrap_w > 0.0f) ? std::min(nat_w, wrap_w) : 0.0f;
            const float h = (wrap_w > 0.0f && !node.text.empty() && renderer_)
                                ? renderer_->GetFont().MeasureTextEx(node.text, sc, wrap_w)
                                : 0.0f;
            apply_explicit_size(w + pad_x, h + pad_y);
        } else {
            const float used = node.overflow == UITextOverflow::Shrink && nat_w > inner_avail_w && nat_w > 0.0f
                                   ? std::max(node.min_scale, sc * inner_avail_w / nat_w) : sc;
            const float h = node.text.empty() ? 0.0f : FontLineHeightPx(used);
            node.baseline = node.margin_t + node.pad_t + h * 0.8f;
            apply_explicit_size(nat_w + pad_x, h + pad_y);
        }
        return;
    }
    case AutoNode::Kind::Button:
    case AutoNode::Kind::Tab: {
        const float sc = AutoWidgetTextScale(node.scale);
        const float px = ThemeMetric(node.kind == AutoNode::Kind::Tab ? "tab_pad_x" : "button_pad_x");
        const float py = ThemeMetric(node.kind == AutoNode::Kind::Tab ? "tab_pad_y" : "button_pad_y");
        const float tw = renderer_ ? renderer_->MeasureText(node.text, sc) : 0.0f;
        const float lh = FontLineHeightPx(sc);
        node.baseline = node.margin_t + py + lh * 0.8f;
        apply_explicit_size(tw + 2.0f * px, lh + 2.0f * py);
        return;
    }
    case AutoNode::Kind::Tree:
    case AutoNode::Kind::ComboBegin:
    case AutoNode::Kind::ComboItem: {
        const float sc = AutoWidgetTextScale(node.scale);
        const float px = ThemeMetric("button_pad_x");
        const float py = ThemeMetric("button_pad_y");
        const float tw = renderer_ ? renderer_->MeasureText(node.text, sc) : 0.0f;
        const float lh = FontLineHeightPx(sc);
        node.baseline = node.margin_t + py + lh * 0.8f;
        apply_explicit_size(tw + 2.0f * px + 18.0f, lh + 2.0f * py);
        return;
    }
    case AutoNode::Kind::Checkbox:
    case AutoNode::Kind::RadioButton: {
        const float sc = AutoWidgetTextScale(node.scale);
        const float lh = FontLineHeightPx(sc);
        const float sz = node.height_spec.kind == UISizeKind::Fixed ? node.height_spec.value : std::max(14.0f, lh);
        const float tw = (renderer_ && !node.text.empty()) ? (8.0f + renderer_->MeasureText(node.text, sc)) : 0.0f;
        node.baseline = node.margin_t + (sz - lh) * 0.5f + lh * 0.8f;
        apply_explicit_size(sz + tw, sz);
        return;
    }
    case AutoNode::Kind::ProgressBar: {
        apply_explicit_size(48.0f, ThemeMetric("bar_height"));
        node.baseline = node.meas_h * 0.8f;
        return;
    }
    case AutoNode::Kind::Slider: {
        apply_explicit_size(80.0f, ThemeMetric("slider_height"));
        node.baseline = node.meas_h * 0.8f;
        return;
    }
    case AutoNode::Kind::TextInput: {
        const float sc = ThemeMetric("font_scale");
        const float lh = FontLineHeightPx(sc);
        const float py = ThemeMetric("input_pad_y");
        node.baseline = node.margin_t + py + lh * 0.8f;
        apply_explicit_size(120.0f, lh + 2.0f * py);
        return;
    }
    case AutoNode::Kind::TextArea: {
        const float sc = ThemeMetric("font_scale");
        const float lh = FontLineHeightPx(sc);
        const float py = ThemeMetric("input_pad_y");
        node.baseline = node.margin_t + py + lh * 0.8f;
        apply_explicit_size(120.0f, lh * 4.0f + 2.0f * py);
        return;
    }
    case AutoNode::Kind::TextPassword: {
        const float sc = ThemeMetric("font_scale");
        const float lh = FontLineHeightPx(sc);
        const float py = ThemeMetric("input_pad_y");
        node.baseline = node.margin_t + py + lh * 0.8f;
        apply_explicit_size(120.0f, lh + 2.0f * py);
        return;
    }
    case AutoNode::Kind::Splitter: {
        apply_explicit_size(120.0f, ThemeMetric("slider_height"));
        node.baseline = node.meas_h * 0.8f;
        return;
    }
    case AutoNode::Kind::Window: {
        const float lh = FontLineHeightPx(node.scale);
        const float bar_h = std::max(22.0f, lh + 10.0f);
        const float tw = renderer_ ? renderer_->MeasureText(node.text, node.scale) + 16.0f : 120.0f;
        node.baseline = node.margin_t + (bar_h - lh) * 0.5f + lh * 0.8f;
        apply_explicit_size(tw, bar_h);
        return;
    }
    case AutoNode::Kind::RichText: {
        const float wrap_w = inner_avail_w < 1e7f ? inner_avail_w : 400.0f;
        const float h = MeasureRichTextHeight(node.text, node.scale, wrap_w);
        node.baseline = node.margin_t + FontLineHeightPx(node.scale) * 0.8f;
        apply_explicit_size(wrap_w, h);
        return;
    }
    case AutoNode::Kind::ListViewSlot: {
        apply_explicit_size(80.0f, 120.0f);
        return;
    }
    case AutoNode::Kind::Divider: {
        apply_explicit_size(0.0f, 1.0f);
        return;
    }
    case AutoNode::Kind::Image:
    case AutoNode::Kind::ImageButton: {
        const float aspect =
            (node.tex_src.width > 0.0f && node.tex_src.height > 0.0f)
                ? (node.tex_src.width / node.tex_src.height)
                : 1.0f;
        float w = node.tex_src.width > 0.0f ? node.tex_src.width : 24.0f;
        float h = node.tex_src.height > 0.0f ? node.tex_src.height : 24.0f;
        if (node.height_spec.kind == UISizeKind::Fixed) {
            h = node.height_spec.value;
            w = h * aspect;
        } else if (node.width_spec.kind == UISizeKind::Fixed) {
            w = node.width_spec.value;
            h = w / std::max(1e-4f, aspect);
        } else if (inner_avail_w < 1e7f && w > inner_avail_w) {
            w = inner_avail_w;
            h = w / std::max(1e-4f, aspect);
        }
        apply_explicit_size(w, h);
        return;
    }
    case AutoNode::Kind::Spacer: {
        apply_explicit_size(0.0f, 0.0f);
        return;
    }
    case AutoNode::Kind::MenuSeparator: {
        apply_explicit_size(0.0f, 7.0f);
        return;
    }
    case AutoNode::Kind::Menu:
    case AutoNode::Kind::MenuItem: {
        const float sc = AutoWidgetTextScale(node.scale);
        const float px = ThemeMetric("button_pad_x");
        const float py = ThemeMetric("button_pad_y");
        const float tw = renderer_ ? renderer_->MeasureText(node.text, sc) : 0.0f;
        const float sw = (renderer_ && !node.shortcut.empty())
                             ? (18.0f + renderer_->MeasureText(node.shortcut, sc * 0.9f))
                             : (node.kind == AutoNode::Kind::Menu && node.col_span > 0 ? 16.0f : 0.0f);
        const float check_pad = (node.kind == AutoNode::Kind::MenuItem) ? 16.0f : 0.0f;
        const float lh = FontLineHeightPx(sc);
        node.baseline = node.margin_t + py + lh * 0.8f;
        apply_explicit_size(tw + sw + check_pad + 2.0f * px, lh + 2.0f * py);
        return;
    }
    case AutoNode::Kind::MenuBar: {
        const auto children = node.children;
        const std::size_t n = children.size();
        const float gaps = n > 1 ? node.gap * static_cast<float>(n - 1) : 0.0f;
        float sum_w = 0.0f, max_h = 0.0f;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_avail_w, inner_avail_h);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) continue;
            sum_w += c.meas_w;
            max_h = std::max(max_h, c.meas_h);
        }
        apply_explicit_size(sum_w + gaps + pad_x, max_h + pad_y);
        return;
    }
    case AutoNode::Kind::Root:
    case AutoNode::Kind::AnchorRoot:
    case AutoNode::Kind::SafeArea:
    case AutoNode::Kind::DragSource:
    case AutoNode::Kind::DropTarget:
    case AutoNode::Kind::VStack:
    case AutoNode::Kind::TableCell:
    case AutoNode::Kind::Card:
    case AutoNode::Kind::CustomBox:
    case AutoNode::Kind::CachedBox: {
        float max_w = 0.0f;
        float sum_h = 0.0f;
        float cmd_w = 0.0f;
        float cmd_h = 0.0f;
        float first_base = 0.0f;
        for (const auto &cmd : node.custom_cmds) {
            if (cmd.op != AutoCustomCmd::Op::NineSlice) {
                cmd_w = std::max(cmd_w, cmd.rect.x + cmd.rect.width);
                cmd_h = std::max(cmd_h, cmd.rect.y + cmd.rect.height);
            }
        }
        std::size_t count = 0;
        const auto children = node.children;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_avail_w, kUnbounded);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) {
                continue;
            }
            if (count == 0) {
                first_base = c.baseline;
            }
            max_w = std::max(max_w, c.meas_w);
            sum_h += c.meas_h;
            ++count;
        }
        const float gaps = count > 1 ? node.gap * static_cast<float>(count - 1) : 0.0f;
        auto_nodes_[static_cast<std::size_t>(idx)].baseline = node.margin_t + node.pad_t + first_base;
        apply_explicit_size(std::max(max_w, cmd_w) + pad_x, std::max(sum_h + gaps, cmd_h) + pad_y);
        return;
    }
    case AutoNode::Kind::ZStack: {
        float max_w = 0.0f, max_h = 0.0f, any_w = 0.0f, any_h = 0.0f, base = 0.0f;
        bool has_flow = false;
        const auto children = node.children;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_avail_w, inner_avail_h);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            any_w = std::max(any_w, c.meas_w);
            any_h = std::max(any_h, c.meas_h);
            if (!(c.kind == AutoNode::Kind::AnchorRoot && c.overlay)) {
                if (!has_flow) {
                    base = c.baseline;
                }
                has_flow = true;
                max_w = std::max(max_w, c.meas_w);
                max_h = std::max(max_h, c.meas_h);
            }
        }
        auto_nodes_[static_cast<std::size_t>(idx)].baseline =
            node.margin_t + node.pad_t + (has_flow ? base : 0.0f);
        apply_explicit_size((has_flow ? max_w : any_w) + pad_x, (has_flow ? max_h : any_h) + pad_y);
        return;
    }
    case AutoNode::Kind::Grid: {
        const auto children = node.children;
        const std::size_t n = children.size();
        const float min_cw = std::max(16.0f, node.min_col_w);
        const int ncols = node.grid_fixed_cols > 0
                              ? node.grid_fixed_cols
                              : ((inner_avail_w < 1e7f)
                                     ? std::max(1, static_cast<int>(std::floor((inner_avail_w + node.gap) / (min_cw + node.gap))))
                                     : std::max(1, static_cast<int>(n)));
        const float col_gaps = ncols > 1 ? node.gap * static_cast<float>(ncols - 1) : 0.0f;
        const float col_w = (inner_avail_w < 1e7f)
                                ? std::max(1.0f, (inner_avail_w - col_gaps) / static_cast<float>(ncols))
                                : min_cw;
        std::vector<std::vector<bool>> occ;
        struct GridMeasItem { int r, cspan, rspan; float h; };
        std::vector<GridMeasItem> placed;
        placed.reserve(n);
        int cur_r = 0, cur_c = 0;
        float max_base = 0.0f;
        auto ensure_rows = [&](int needed) {
            while (static_cast<int>(occ.size()) < needed) {
                occ.emplace_back(static_cast<std::size_t>(ncols), false);
            }
        };
        for (const int cid : children) {
            const int cspan = std::clamp(auto_nodes_[static_cast<std::size_t>(cid)].col_span, 1, ncols);
            const int rspan = std::max(1, auto_nodes_[static_cast<std::size_t>(cid)].row_span);
            while (true) {
                if (cur_c + cspan > ncols) {
                    cur_c = 0;
                    ++cur_r;
                    continue;
                }
                ensure_rows(cur_r + rspan);
                bool fits = true;
                for (int rr = 0; rr < rspan && fits; ++rr) {
                    for (int cc = 0; cc < cspan; ++cc) {
                        if (occ[static_cast<std::size_t>(cur_r + rr)][static_cast<std::size_t>(cur_c + cc)]) {
                            fits = false;
                            break;
                        }
                    }
                }
                if (fits) break;
                ++cur_c;
            }
            for (int rr = 0; rr < rspan; ++rr) {
                for (int cc = 0; cc < cspan; ++cc) {
                    occ[static_cast<std::size_t>(cur_r + rr)][static_cast<std::size_t>(cur_c + cc)] = true;
                }
            }
            const float cell_w = col_w * static_cast<float>(cspan) + node.gap * static_cast<float>(cspan - 1);
            MeasureAutoNode(cid, cell_w, kUnbounded);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (cur_r == 0) {
                max_base = std::max(max_base, c.baseline);
            }
            placed.push_back({cur_r, cspan, rspan, c.meas_h});
            cur_c += cspan;
        }
        std::vector<float> row_hs(occ.size(), 0.0f);
        for (const auto &it : placed) {
            if (it.rspan == 1) {
                row_hs[static_cast<std::size_t>(it.r)] = std::max(row_hs[static_cast<std::size_t>(it.r)], it.h);
            }
        }
        for (const auto &it : placed) {
            if (it.rspan > 1) {
                float cur_h = node.line_gap * static_cast<float>(it.rspan - 1);
                for (int rr = 0; rr < it.rspan; ++rr) cur_h += row_hs[static_cast<std::size_t>(it.r + rr)];
                if (it.h > cur_h) {
                    const float add = (it.h - cur_h) / static_cast<float>(it.rspan);
                    for (int rr = 0; rr < it.rspan; ++rr) row_hs[static_cast<std::size_t>(it.r + rr)] += add;
                }
            }
        }
        float sum_h = row_hs.size() > 1 ? node.line_gap * static_cast<float>(row_hs.size() - 1) : 0.0f;
        for (float rh : row_hs) sum_h += rh;
        auto_nodes_[static_cast<std::size_t>(idx)].baseline = node.margin_t + node.pad_t + max_base;
        const float total_w = col_w * static_cast<float>(ncols) + col_gaps;
        apply_explicit_size(total_w + pad_x, sum_h + pad_y);
        return;
    }
    case AutoNode::Kind::HStack: {
        const auto children = node.children;
        const std::size_t n = children.size();
        const float gaps = n > 1 ? node.gap * static_cast<float>(n - 1) : 0.0f;
        float fixed_w = 0.0f;
        float grow_sum = 0.0f;
        float shrink_sum = 0.0f;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_avail_w, inner_avail_h);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.width_spec.kind == UISizeKind::Grow) {
                grow_sum += c.width_spec.value;
            } else if (c.width_spec.kind == UISizeKind::Percent && inner_avail_w < 1e7f) {
                fixed_w += std::clamp(inner_avail_w * c.width_spec.value, c.limits.min_w, c.limits.max_w) +
                           c.margin_l + c.margin_r;
            } else {
                fixed_w += c.meas_w;
                if (c.shrink > 0.0f) {
                    shrink_sum += c.shrink * c.meas_w;
                }
            }
        }
        // Re-measure Grow / Percent / Shrink children with their allocated column widths!
        const float budget_w = (inner_avail_w < 1e7f ? inner_avail_w : fixed_w);
        const float diff_w = budget_w - fixed_w - gaps;
        const float rem_w = std::max(0.0f, diff_w);
        const float deficit_w = std::max(0.0f, -diff_w);
        float max_h = 0.0f;
        float total_w = 0.0f;
        float max_base = 0.0f;
        for (const int cid : children) {
            const auto &csnap = auto_nodes_[static_cast<std::size_t>(cid)];
            const auto spec = csnap.width_spec;
            if (spec.kind == UISizeKind::Grow && grow_sum > 0.0f) {
                const float cw = rem_w * (spec.value / grow_sum);
                MeasureAutoNode(cid, cw, inner_avail_h);
                const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
                total_w += std::max(cw, c.meas_w);
                max_h = std::max(max_h, c.meas_h);
                max_base = std::max(max_base, c.baseline);
            } else if (deficit_w > 0.0f && csnap.shrink > 0.0f && shrink_sum > 0.0f) {
                const float cut = deficit_w * (csnap.shrink * csnap.meas_w / shrink_sum);
                const float min_outer = csnap.limits.min_w + csnap.margin_l + csnap.margin_r;
                const float shrunk_w = std::max(min_outer, csnap.meas_w - cut);
                MeasureAutoNode(cid, shrunk_w, inner_avail_h);
                auto &c_mut = auto_nodes_[static_cast<std::size_t>(cid)];
                c_mut.meas_w = shrunk_w;
                total_w += shrunk_w;
                max_h = std::max(max_h, c_mut.meas_h);
                max_base = std::max(max_base, c_mut.baseline);
            } else {
                const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
                total_w += c.meas_w;
                max_h = std::max(max_h, c.meas_h);
                max_base = std::max(max_base, c.baseline);
            }
        }
        auto_nodes_[static_cast<std::size_t>(idx)].baseline = node.margin_t + node.pad_t + max_base;
        apply_explicit_size(total_w + gaps + pad_x, max_h + pad_y);
        return;
    }
    case AutoNode::Kind::Table: {
        const auto rows = node.children;
        const std::size_t ncols = node.col_specs.size();
        const float col_gaps = ncols > 1 ? node.gap * static_cast<float>(ncols - 1) : 0.0f;
        std::vector<float> col_w(ncols, 0.0f);

        // Pre-compute cell occupancy respecting col_span and row_span
        std::vector<std::vector<bool>> occ(rows.size(), std::vector<bool>(ncols, false));
        struct TCellSpan { int cid, r_idx, c_idx, cspan, rspan; float h; };
        std::vector<TCellSpan> spans;
        for (std::size_t r_idx = 0; r_idx < rows.size(); ++r_idx) {
            const auto &r_children = auto_nodes_[static_cast<std::size_t>(rows[r_idx])].children;
            std::size_t c = 0;
            for (const int cid : r_children) {
                while (c < ncols && occ[r_idx][c]) ++c;
                if (c >= ncols) break;
                const int cspan = std::clamp(auto_nodes_[static_cast<std::size_t>(cid)].col_span, 1,
                                             static_cast<int>(ncols - c));
                const int rspan = std::clamp(auto_nodes_[static_cast<std::size_t>(cid)].row_span, 1,
                                             static_cast<int>(rows.size() - r_idx));
                for (int rr = 0; rr < rspan; ++rr) {
                    for (int cc = 0; cc < cspan; ++cc) {
                        occ[r_idx + static_cast<std::size_t>(rr)][c + static_cast<std::size_t>(cc)] = true;
                    }
                }
                spans.push_back({cid, static_cast<int>(r_idx), static_cast<int>(c), cspan, rspan, 0.0f});
                c += static_cast<std::size_t>(cspan);
            }
        }

        // Pass 1a: Measure Auto / Fixed / Percent columns across ALL rows (by mapped column index)
        for (std::size_t c = 0; c < ncols; ++c) {
            const auto &cs = node.col_specs[c];
            if (cs.kind == UISizeKind::Fixed) {
                col_w[c] = cs.value;
            } else if (cs.kind == UISizeKind::Percent && inner_avail_w < 1e7f) {
                col_w[c] = inner_avail_w * cs.value;
            } else if (cs.kind == UISizeKind::Auto) {
                for (const auto &sp : spans) {
                    if (sp.c_idx == static_cast<int>(c) && sp.cspan == 1) {
                        MeasureAutoNode(sp.cid, inner_avail_w, kUnbounded);
                        col_w[c] = std::max(col_w[c], auto_nodes_[static_cast<std::size_t>(sp.cid)].meas_w);
                    }
                }
            }
            if (c < node.col_limits.size()) {
                col_w[c] = std::clamp(col_w[c], node.col_limits[c].min_w, node.col_limits[c].max_w);
            }
        }

        // Pass 1b: Distribute remaining width to Grow columns
        float used_w = col_gaps;
        float grow_sum = 0.0f;
        for (std::size_t c = 0; c < ncols; ++c) {
            if (node.col_specs[c].kind == UISizeKind::Grow) {
                grow_sum += node.col_specs[c].value;
            } else {
                used_w += col_w[c];
            }
        }
        const float rem_w = std::max(0.0f, (inner_avail_w < 1e7f ? inner_avail_w : used_w) - used_w);
        for (std::size_t c = 0; c < ncols; ++c) {
            if (node.col_specs[c].kind == UISizeKind::Grow && grow_sum > 0.0f) {
                col_w[c] = rem_w * (node.col_specs[c].value / grow_sum);
                if (c < node.col_limits.size()) {
                    col_w[c] = std::clamp(col_w[c], node.col_limits[c].min_w, node.col_limits[c].max_w);
                }
            }
        }
        auto_nodes_[static_cast<std::size_t>(idx)].col_widths = col_w;

        // Pass 1c: Measure cells respecting col_span and row_span widths
        for (auto &sp : spans) {
            float span_w = node.gap * static_cast<float>(sp.cspan - 1);
            for (int cc = 0; cc < sp.cspan; ++cc) {
                span_w += col_w[static_cast<std::size_t>(sp.c_idx + cc)];
            }
            MeasureAutoNode(sp.cid, span_w, kUnbounded);
            sp.h = auto_nodes_[static_cast<std::size_t>(sp.cid)].meas_h;
        }
        std::vector<float> row_hs(rows.size(), 0.0f);
        for (const auto &sp : spans) {
            if (sp.rspan == 1) {
                row_hs[static_cast<std::size_t>(sp.r_idx)] =
                    std::max(row_hs[static_cast<std::size_t>(sp.r_idx)], sp.h);
            }
        }
        for (const auto &sp : spans) {
            if (sp.rspan > 1) {
                float cur_h = node.line_gap * static_cast<float>(sp.rspan - 1);
                for (int rr = 0; rr < sp.rspan; ++rr)
                    cur_h += row_hs[static_cast<std::size_t>(sp.r_idx + rr)];
                if (sp.h > cur_h) {
                    const float add = (sp.h - cur_h) / static_cast<float>(sp.rspan);
                    for (int rr = 0; rr < sp.rspan; ++rr)
                        row_hs[static_cast<std::size_t>(sp.r_idx + rr)] += add;
                }
            }
        }
        float sum_row_h = 0.0f;
        for (std::size_t r_idx = 0; r_idx < rows.size(); ++r_idx) {
            auto &rn = auto_nodes_[static_cast<std::size_t>(rows[r_idx])];
            rn.meas_w = inner_avail_w;
            rn.meas_h = std::clamp(row_hs[r_idx] + rn.pad_t + rn.pad_b, rn.limits.min_h, rn.limits.max_h);
            sum_row_h += rn.meas_h;
        }
        const float row_gaps = rows.size() > 1 ? node.line_gap * static_cast<float>(rows.size() - 1) : 0.0f;
        float total_col_w = col_gaps;
        for (float w : col_w) {
            total_col_w += w;
        }
        apply_explicit_size(total_col_w + pad_x, sum_row_h + row_gaps + pad_y);
        return;
    }
    case AutoNode::Kind::TableRow: {
        // Measured directly by Table parent above, or fallback as HStack if standalone
        float sum_w = 0.0f, max_h = 0.0f;
        const auto children = node.children;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_avail_w, kUnbounded);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            sum_w += c.meas_w;
            max_h = std::max(max_h, c.meas_h);
        }
        apply_explicit_size(sum_w + pad_x, max_h + pad_y);
        return;
    }
    case AutoNode::Kind::Flow: {
        const auto children = node.children;
        float cx = 0.0f, line_h = 0.0f, total_h = 0.0f, max_line_w = 0.0f;
        const float wrap_w = inner_avail_w < 1e7f ? inner_avail_w : kUnbounded;
        bool any_line = false;
        for (const int cid : children) {
            MeasureAutoNode(cid, wrap_w, kUnbounded);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            const float cw = std::min(c.meas_w, wrap_w);
            if (cx > 0.0f && cx + cw > wrap_w + 0.01f) {
                total_h += line_h + node.line_gap;
                max_line_w = std::max(max_line_w, cx - node.gap);
                cx = 0.0f;
                line_h = 0.0f;
            }
            cx += cw + node.gap;
            line_h = std::max(line_h, c.meas_h);
            any_line = true;
        }
        if (any_line) {
            total_h += line_h;
            max_line_w = std::max(max_line_w, std::max(0.0f, cx - node.gap));
        }
        apply_explicit_size(max_line_w + pad_x, total_h + pad_y);
        return;
    }
    case AutoNode::Kind::Scroll: {
        const float sb = ThemeMetric("scrollbar_width");
        const bool want_v = (node.scroll_dir == UIScrollDir::Vertical || node.scroll_dir == UIScrollDir::Both);
        const bool want_h = (node.scroll_dir == UIScrollDir::Horizontal || node.scroll_dir == UIScrollDir::Both);
        const float inner_w = want_h ? kUnbounded : std::max(0.0f, inner_avail_w - (want_v ? sb : 0.0f));
        float sum_h = 0.0f;
        float max_w = 0.0f;
        const auto children = node.children;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_w, kUnbounded);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            sum_h += c.meas_h;
            max_w = std::max(max_w, c.meas_w);
        }
        const float gaps = children.size() > 1 ? node.gap * static_cast<float>(children.size() - 1) : 0.0f;
        const float content_h = sum_h + gaps + pad_y + (want_h ? sb : 0.0f);
        const float content_w = max_w + pad_x + (want_v ? sb : 0.0f);
        auto_nodes_[static_cast<std::size_t>(idx)].content_height = content_h;
        auto_nodes_[static_cast<std::size_t>(idx)].content_width = content_w;
        const float pref_h = (node.height_spec.kind == UISizeKind::Grow) ? 0.0f : content_h;
        apply_explicit_size(max_w + (want_v ? sb : 0.0f) + pad_x, pref_h);
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// Pass 2: Top-Down Arrange
// ---------------------------------------------------------------------------

void UISystem::ArrangeAutoNode(int idx, const Rect &final_rect, const Rect &parent_clip,
                               bool in_scroll, std::uint32_t root_id, int row_id, int col_idx,
                               float parent_clip_radius, const Rect &parent_clip_rrect) {
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!node.visible) {
        node.rect = {final_rect.x, final_rect.y, 0.0f, 0.0f};
        node.content_rect = node.rect;
        node.clip_rect = parent_clip;
        node.clip_rrect = parent_clip_rrect;
        node.clip_radius = parent_clip_radius;
        return;
    }
    Rect visual_rect{
        final_rect.x + node.margin_l,
        final_rect.y + node.margin_t,
        std::max(0.0f, final_rect.width - node.margin_l - node.margin_r),
        std::max(0.0f, final_rect.height - node.margin_t - node.margin_b),
    };
    if (node.aspect_ratio > 0.0f && visual_rect.width > 0.0f &&
        node.height_spec.kind == UISizeKind::Auto) {
        visual_rect.height = std::clamp(visual_rect.width / node.aspect_ratio,
                                        node.limits.min_h, node.limits.max_h);
    }
    node.rect = visual_rect;
    node.content_rect = {
        visual_rect.x + node.pad_l,
        visual_rect.y + node.pad_t,
        std::max(0.0f, visual_rect.width - node.pad_l - node.pad_r),
        std::max(0.0f, visual_rect.height - node.pad_t - node.pad_b),
    };
    const Rect base_clip = node.has_clip_override ? node.clip_override : parent_clip;
    const Rect base_rrect = node.has_clip_override ? node.clip_override : parent_clip_rrect;
    const float base_rad = node.has_clip_override ? 0.0f : parent_clip_radius;
    node.clip_rect = node.clip_children ? IntersectClip(base_clip, node.rect) : base_clip;
    if (node.clip_children && node.radius > 0.5f) {
        node.clip_rrect = node.rect;
        node.clip_radius = node.radius;
    } else {
        node.clip_rrect = base_rrect;
        node.clip_radius = base_rad;
    }
    node.root_box_id = root_id;
    node.in_scroll = in_scroll || node.kind == AutoNode::Kind::Scroll;
    if (node.row_id < 0 && row_id >= 0) {
        node.row_id = row_id;
    }
    if (node.col_index < 0 && col_idx >= 0) {
        node.col_index = col_idx;
    }

    if (node.kind == AutoNode::Kind::CachedBox && node.cached_hit) {
        ReplaySubtreeCache(idx, visual_rect.x, visual_rect.y, node.clip_rect, node.clip_radius,
                           node.in_scroll, root_id);
        return;
    }

    const Rect cr = node.content_rect;
    const Rect clip = node.clip_rect;
    const Rect c_rrect = node.clip_rrect;
    const float c_rad = node.clip_radius;
    const int eff_row_id = node.row_id;
    const int eff_col_idx = node.col_index;
    const auto children = node.children;
    const std::size_t n = children.size();
    const bool rtl = (node.layout_dir == UILayoutDirection::RTL);

    auto resolve_cross = [](UIAxisAlign align, const UISize &spec, float meas, float avail, float min_v,
                            float max_v, float margin = 0.0f) -> float {
        float v = meas;
        if (spec.kind == UISizeKind::Fixed) {
            v = spec.value + margin;
        } else if (spec.kind == UISizeKind::Percent) {
            v = avail * spec.value + margin;
        } else if (spec.kind == UISizeKind::Grow || align == UIAxisAlign::Stretch) {
            v = avail;
        }
        return std::clamp(v, std::min(avail, min_v), std::min(avail, max_v));
    };

    auto arrange_overlay_child = [&](int cid) {
        const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
        float cw = resolve_cross(UIAxisAlign::Start, c.width_spec, c.meas_w, cr.width,
                                 c.limits.min_w, c.limits.max_w);
        float ch = resolve_cross(UIAxisAlign::Start, c.height_spec, c.meas_h, cr.height,
                                 c.limits.min_h, c.limits.max_h);
        float cx = cr.x + c.inset_x;
        float cy = cr.y + c.inset_y;
        const float vw = LogicalWidth() > 1.0f ? LogicalWidth() : 1280.0f;
        const float vh = LogicalHeight() > 1.0f ? LogicalHeight() : 720.0f;
        if (c.h_align == "anchor_to") {
            cw = std::clamp(c.meas_w, c.limits.min_w, c.limits.max_w);
            ch = std::clamp(c.meas_h, c.limits.min_h, c.limits.max_h);
            Rect target{cr.x, cr.y, 0.0f, 0.0f};
            if (!c.anchor_target_key.empty()) {
                if (const auto it = curr_solved_widgets_.find(c.anchor_target_key);
                    it != curr_solved_widgets_.end()) {
                    target = it->second.rect;
                } else if (const auto it2 = prev_solved_widgets_.find(c.anchor_target_key);
                           it2 != prev_solved_widgets_.end()) {
                    target = it2->second.rect;
                } else if (const auto it3 = named_rects_.find(c.anchor_target_key);
                           it3 != named_rects_.end()) {
                    target = it3->second;
                }
            }
            const std::string &p = c.v_align;
            const float g = c.gap;
            if (p == "right_start" || p == "right") {
                cx = target.x + target.width + g;
                if (cx + cw > vw && target.x - g - cw >= 0.0f) cx = target.x - g - cw;
                cy = target.y;
            } else if (p == "left_start" || p == "left") {
                cx = target.x - g - cw;
                if (cx < 0.0f && target.x + target.width + g + cw <= vw) cx = target.x + target.width + g;
                cy = target.y;
            } else if (p == "top_start" || p == "top_center" || p == "top_end" || p == "top") {
                cy = target.y - g - ch;
                if (cy < 0.0f && target.y + target.height + g + ch <= vh) cy = target.y + target.height + g;
                cx = (p == "top_end") ? (target.x + target.width - cw) : target.x;
            } else {
                cy = target.y + target.height + g;
                if (cy + ch > vh && target.y - g - ch >= 0.0f) cy = target.y - g - ch;
                cx = (p == "bottom_end") ? (target.x + target.width - cw) : target.x;
            }
            cx = std::clamp(cx, 0.0f, std::max(0.0f, vw - cw));
            cy = std::clamp(cy, 0.0f, std::max(0.0f, vh - ch));
            const Rect oclip = c.has_clip_override ? c.clip_override : Rect{0.0f, 0.0f, vw, vh};
            ArrangeAutoNode(cid, {cx, cy, cw, ch}, oclip, false, root_id, eff_row_id, eff_col_idx,
                            0.0f, oclip);
            return;
        }
        if (c.h_align == "stretch") {
            cx = cr.x + c.inset_x;
            cw = std::clamp(std::max(0.0f, cr.width - 2.0f * c.inset_x), c.limits.min_w, c.limits.max_w);
        } else if (c.h_align == "right" || c.h_align == "end") {
            cx = cr.x + cr.width - cw - c.inset_x;
        } else if (c.h_align == "center") {
            cx = cr.x + (cr.width - cw) * 0.5f + c.inset_x;
        }
        if (c.v_align == "stretch") {
            cy = cr.y + c.inset_y;
            ch = std::clamp(std::max(0.0f, cr.height - 2.0f * c.inset_y), c.limits.min_h, c.limits.max_h);
        } else if (c.v_align == "bottom" || c.v_align == "end") {
            cy = cr.y + cr.height - ch - c.inset_y;
        } else if (c.v_align == "center") {
            cy = cr.y + (cr.height - ch) * 0.5f + c.inset_y;
        }
        ArrangeAutoNode(cid, {cx, cy, cw, ch}, clip, node.in_scroll, root_id,
                        eff_row_id, eff_col_idx, c_rad, c_rrect);
    };

    switch (node.kind) {
    case AutoNode::Kind::Root:
    case AutoNode::Kind::AnchorRoot:
    case AutoNode::Kind::SafeArea:
    case AutoNode::Kind::DragSource:
    case AutoNode::Kind::DropTarget:
    case AutoNode::Kind::VStack:
    case AutoNode::Kind::TableCell:
    case AutoNode::Kind::Card:
    case AutoNode::Kind::CustomBox:
    case AutoNode::Kind::CachedBox: {
        if (n == 0) {
            return;
        }
        std::size_t flow_n = 0;
        float fixed_h = 0.0f;
        float grow_sum = 0.0f;
        float shrink_sum = 0.0f;
        std::vector<float> child_h(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (!c.visible || (c.kind == AutoNode::Kind::AnchorRoot && c.overlay)) {
                continue;
            }
            ++flow_n;
            if (c.height_spec.kind == UISizeKind::Grow) {
                grow_sum += c.height_spec.value;
            } else {
                float h = c.meas_h;
                if (c.height_spec.kind == UISizeKind::Percent) {
                    h = std::clamp(cr.height * c.height_spec.value, c.limits.min_h, c.limits.max_h) +
                        c.margin_t + c.margin_b;
                }
                child_h[i] = h;
                fixed_h += h;
                if (c.shrink > 0.0f) {
                    shrink_sum += c.shrink * h;
                }
            }
        }
        const float gaps = flow_n > 1 ? node.gap * static_cast<float>(flow_n - 1) : 0.0f;
        const float diff_h = cr.height - fixed_h - gaps;
        const float rem_h = std::max(0.0f, diff_h);
        const float deficit_h = std::max(0.0f, -diff_h);
        float total_used_h = gaps;
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (!c.visible || (c.kind == AutoNode::Kind::AnchorRoot && c.overlay)) {
                continue;
            }
            if (c.height_spec.kind == UISizeKind::Grow && grow_sum > 0.0f) {
                child_h[i] = std::clamp(rem_h * (c.height_spec.value / grow_sum),
                                        c.limits.min_h + c.margin_t + c.margin_b,
                                        c.limits.max_h + c.margin_t + c.margin_b);
            } else if (deficit_h > 0.0f && c.shrink > 0.0f && shrink_sum > 0.0f) {
                const float min_h = c.limits.min_h + c.margin_t + c.margin_b;
                const float cut = deficit_h * (c.shrink * child_h[i] / shrink_sum);
                child_h[i] = std::max(min_h, child_h[i] - cut);
            }
            total_used_h += child_h[i];
        }

        float y_start = 0.0f, extra_gap = 0.0f;
        ComputeMainAxisSpacing(node.main_align, std::max(0.0f, cr.height - total_used_h), flow_n,
                               y_start, extra_gap);
        float y = cr.y + y_start;
        for (std::size_t i = 0; i < n; ++i) {
            const int cid = children[i];
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) {
                arrange_overlay_child(cid);
                continue;
            }
            if (!c.visible) {
                ArrangeAutoNode(cid, {cr.x, y, 0.0f, 0.0f}, clip, node.in_scroll, root_id,
                                eff_row_id, eff_col_idx, c_rad, c_rrect);
                continue;
            }
            UIAxisAlign x_align = c.has_cross_override ? c.cross_align : node.cross_align;
            if (rtl) {
                if (x_align == UIAxisAlign::Start) x_align = UIAxisAlign::End;
                else if (x_align == UIAxisAlign::End) x_align = UIAxisAlign::Start;
            }
            const float cw = resolve_cross(x_align, c.width_spec, c.meas_w, cr.width,
                                           c.limits.min_w + c.margin_l + c.margin_r,
                                           c.limits.max_w + c.margin_l + c.margin_r,
                                           c.margin_l + c.margin_r);
            const float cx = cr.x + AlignOffset(x_align, cr.width, cw) + c.anchor_x;
            const float cy = y + c.anchor_y;
            ArrangeAutoNode(cid, {cx, cy, cw, child_h[i]}, clip, node.in_scroll, root_id,
                            eff_row_id, eff_col_idx, c_rad, c_rrect);
            y += child_h[i] + node.gap + extra_gap;
        }
        return;
    }
    case AutoNode::Kind::ZStack: {
        for (const int cid : children) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) {
                arrange_overlay_child(cid);
                continue;
            }
            const UIAxisAlign x_align = c.main_align != UIAxisAlign::Start ? c.main_align : node.cross_align;
            const UIAxisAlign y_align = c.has_cross_override ? c.cross_align : node.cross_align;
            const float cw = resolve_cross(x_align,
                                           c.width_spec, c.meas_w, cr.width,
                                           c.limits.min_w + c.margin_l + c.margin_r,
                                           c.limits.max_w + c.margin_l + c.margin_r,
                                           c.margin_l + c.margin_r);
            const float ch = resolve_cross(y_align,
                                           c.height_spec, c.meas_h, cr.height,
                                           c.limits.min_h + c.margin_t + c.margin_b,
                                           c.limits.max_h + c.margin_t + c.margin_b,
                                           c.margin_t + c.margin_b);
            const float cx = cr.x + AlignOffset(rtl && x_align == UIAxisAlign::Start ? UIAxisAlign::End : x_align,
                                                cr.width, cw);
            const float cy = cr.y + AlignOffset(y_align, cr.height, ch);
            ArrangeAutoNode(cid, {cx, cy, cw, ch}, clip, node.in_scroll, root_id,
                            eff_row_id, eff_col_idx, c_rad, c_rrect);
        }
        return;
    }
    case AutoNode::Kind::Grid: {
        if (n == 0) {
            return;
        }
        const float min_cw = std::max(16.0f, node.min_col_w);
        const int ncols = node.grid_fixed_cols > 0
                              ? node.grid_fixed_cols
                              : std::max(1, static_cast<int>(std::floor((cr.width + node.gap) / (min_cw + node.gap))));
        const float col_gaps = ncols > 1 ? node.gap * static_cast<float>(ncols - 1) : 0.0f;
        const float col_w = std::max(1.0f, (cr.width - col_gaps) / static_cast<float>(ncols));
        struct GridPlacement {
            int cid;
            int row;
            int col;
            int cspan;
            int rspan;
            float h;
        };
        std::vector<std::vector<bool>> occ;
        std::vector<GridPlacement> placed;
        placed.reserve(n);
        int cur_r = 0, cur_c = 0;
        auto ensure_rows = [&](int needed) {
            while (static_cast<int>(occ.size()) < needed) {
                occ.emplace_back(static_cast<std::size_t>(ncols), false);
            }
        };
        for (const int cid : children) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (!c.visible) {
                ArrangeAutoNode(cid, {cr.x, cr.y, 0.0f, 0.0f}, clip, node.in_scroll, root_id,
                                eff_row_id, eff_col_idx, c_rad, c_rrect);
                continue;
            }
            const int cspan = std::clamp(c.col_span, 1, ncols);
            const int rspan = std::max(1, c.row_span);
            while (true) {
                if (cur_c + cspan > ncols) {
                    cur_c = 0;
                    ++cur_r;
                    continue;
                }
                ensure_rows(cur_r + rspan);
                bool fits = true;
                for (int rr = 0; rr < rspan && fits; ++rr) {
                    for (int cc = 0; cc < cspan; ++cc) {
                        if (occ[static_cast<std::size_t>(cur_r + rr)][static_cast<std::size_t>(cur_c + cc)]) {
                            fits = false;
                            break;
                        }
                    }
                }
                if (fits) break;
                ++cur_c;
            }
            for (int rr = 0; rr < rspan; ++rr) {
                for (int cc = 0; cc < cspan; ++cc) {
                    occ[static_cast<std::size_t>(cur_r + rr)][static_cast<std::size_t>(cur_c + cc)] = true;
                }
            }
            placed.push_back({cid, cur_r, cur_c, cspan, rspan, c.meas_h});
            cur_c += cspan;
        }
        std::vector<float> row_heights(occ.size(), 0.0f);
        for (const auto &gp : placed) {
            if (gp.rspan == 1) {
                row_heights[static_cast<std::size_t>(gp.row)] =
                    std::max(row_heights[static_cast<std::size_t>(gp.row)], gp.h);
            }
        }
        for (const auto &gp : placed) {
            if (gp.rspan > 1) {
                float cur_h = node.line_gap * static_cast<float>(gp.rspan - 1);
                for (int rr = 0; rr < gp.rspan; ++rr)
                    cur_h += row_heights[static_cast<std::size_t>(gp.row + rr)];
                if (gp.h > cur_h) {
                    const float add = (gp.h - cur_h) / static_cast<float>(gp.rspan);
                    for (int rr = 0; rr < gp.rspan; ++rr)
                        row_heights[static_cast<std::size_t>(gp.row + rr)] += add;
                }
            }
        }
        std::vector<float> row_y(occ.size(), cr.y);
        for (std::size_t r_idx = 1; r_idx < occ.size(); ++r_idx) {
            row_y[r_idx] = row_y[r_idx - 1] + row_heights[r_idx - 1] + node.line_gap;
        }
        for (const auto &gp : placed) {
            const float gw = col_w * static_cast<float>(gp.cspan) + node.gap * static_cast<float>(gp.cspan - 1);
            const float lx = static_cast<float>(gp.col) * (col_w + node.gap);
            const float gx = rtl ? (cr.x + cr.width - lx - gw) : (cr.x + lx);
            float gh = node.line_gap * static_cast<float>(gp.rspan - 1);
            for (int rr = 0; rr < gp.rspan; ++rr) gh += row_heights[static_cast<std::size_t>(gp.row + rr)];
            ArrangeAutoNode(gp.cid, {gx, row_y[static_cast<std::size_t>(gp.row)], gw, gh},
                            clip, node.in_scroll, root_id, gp.row, gp.col, c_rad, c_rrect);
        }
        return;
    }
    case AutoNode::Kind::MenuBar:
    case AutoNode::Kind::HStack: {
        if (n == 0) {
            return;
        }
        std::size_t flow_n = 0;
        float fixed_w = 0.0f;
        float grow_sum = 0.0f;
        float shrink_sum = 0.0f;
        std::vector<float> child_w(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (!c.visible || (c.kind == AutoNode::Kind::AnchorRoot && c.overlay)) {
                continue;
            }
            ++flow_n;
            if (c.width_spec.kind == UISizeKind::Grow) {
                grow_sum += c.width_spec.value;
            } else {
                float w = c.meas_w;
                if (c.width_spec.kind == UISizeKind::Percent) {
                    w = std::clamp(cr.width * c.width_spec.value, c.limits.min_w, c.limits.max_w) +
                        c.margin_l + c.margin_r;
                }
                child_w[i] = w;
                fixed_w += w;
                if (c.shrink > 0.0f) {
                    shrink_sum += c.shrink * w;
                }
            }
        }
        const float gaps = flow_n > 1 ? node.gap * static_cast<float>(flow_n - 1) : 0.0f;
        const float diff_w = cr.width - fixed_w - gaps;
        const float rem_w = std::max(0.0f, diff_w);
        const float deficit_w = std::max(0.0f, -diff_w);
        float total_used_w = gaps;
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (!c.visible || (c.kind == AutoNode::Kind::AnchorRoot && c.overlay)) {
                continue;
            }
            if (c.width_spec.kind == UISizeKind::Grow && grow_sum > 0.0f) {
                child_w[i] = std::clamp(rem_w * (c.width_spec.value / grow_sum),
                                        c.limits.min_w + c.margin_l + c.margin_r,
                                        c.limits.max_w + c.margin_l + c.margin_r);
            } else if (deficit_w > 0.0f && c.shrink > 0.0f && shrink_sum > 0.0f) {
                const float min_w = c.limits.min_w + c.margin_l + c.margin_r;
                const float cut = deficit_w * (c.shrink * child_w[i] / shrink_sum);
                child_w[i] = std::max(min_w, child_w[i] - cut);
            }
            total_used_w += child_w[i];
        }

        float max_base = 0.0f;
        if (node.cross_align == UIAxisAlign::Baseline) {
            for (const int cid : children) {
                const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
                if (!c.visible) continue;
                const float b = c.baseline > 0.0f ? c.baseline : c.meas_h * 0.8f;
                max_base = std::max(max_base, b);
            }
        }

        UIAxisAlign eff_main = node.main_align;
        if (rtl) {
            if (eff_main == UIAxisAlign::Start) eff_main = UIAxisAlign::End;
            else if (eff_main == UIAxisAlign::End) eff_main = UIAxisAlign::Start;
        }
        float x_start = 0.0f, extra_gap = 0.0f;
        ComputeMainAxisSpacing(eff_main, std::max(0.0f, cr.width - total_used_w), flow_n, x_start, extra_gap);
        float x = rtl ? (cr.x + cr.width - x_start) : (cr.x + x_start);
        for (std::size_t i = 0; i < n; ++i) {
            const int cid = children[i];
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) {
                arrange_overlay_child(cid);
                continue;
            }
            if (!c.visible) {
                ArrangeAutoNode(cid, {x, cr.y, 0.0f, 0.0f}, clip, node.in_scroll, root_id,
                                eff_row_id, eff_col_idx, c_rad, c_rrect);
                continue;
            }
            const UIAxisAlign y_align = c.has_cross_override ? c.cross_align : node.cross_align;
            const float ch = resolve_cross(y_align, c.height_spec, c.meas_h, cr.height,
                                           c.limits.min_h + c.margin_t + c.margin_b,
                                           c.limits.max_h + c.margin_t + c.margin_b,
                                           c.margin_t + c.margin_b);
            float cy = cr.y + AlignOffset(y_align, cr.height, ch);
            if (y_align == UIAxisAlign::Baseline) {
                const float b = c.baseline > 0.0f ? c.baseline : ch * 0.8f;
                cy = cr.y + std::max(0.0f, max_base - b);
            }
            const float cx = rtl ? (x - child_w[i]) : x;
            const int cell_col = (eff_row_id >= 0) ? static_cast<int>(i) : eff_col_idx;
            ArrangeAutoNode(cid, {cx, cy, child_w[i], ch}, clip, node.in_scroll, root_id,
                            eff_row_id, cell_col, c_rad, c_rrect);
            if (rtl) x -= (child_w[i] + node.gap + extra_gap);
            else x += (child_w[i] + node.gap + extra_gap);
        }
        return;
    }
    case AutoNode::Kind::Table: {
        const std::size_t ncols = node.col_specs.size();
        if (ncols == 0 || n == 0) {
            return;
        }
        std::vector<float> col_w = node.col_widths;
        const float col_gaps = ncols > 1 ? node.gap * static_cast<float>(ncols - 1) : 0.0f;
        float used_w = col_gaps;
        float grow_sum = 0.0f;
        for (std::size_t c = 0; c < ncols; ++c) {
            if (node.col_specs[c].kind == UISizeKind::Grow) {
                grow_sum += node.col_specs[c].value;
            } else {
                used_w += col_w[c];
            }
        }
        const float rem_w = std::max(0.0f, cr.width - used_w);
        for (std::size_t c = 0; c < ncols; ++c) {
            if (node.col_specs[c].kind == UISizeKind::Grow && grow_sum > 0.0f) {
                col_w[c] = rem_w * (node.col_specs[c].value / grow_sum);
                if (c < node.col_limits.size()) {
                    col_w[c] = std::clamp(col_w[c], node.col_limits[c].min_w, node.col_limits[c].max_w);
                }
            }
        }

        std::vector<float> col_x(ncols, cr.x);
        if (rtl) {
            float rx = cr.x + cr.width;
            for (std::size_t c = 0; c < ncols; ++c) {
                rx -= col_w[c];
                col_x[c] = rx;
                rx -= node.gap;
            }
        } else {
            float lx = cr.x;
            for (std::size_t c = 0; c < ncols; ++c) {
                col_x[c] = lx;
                lx += col_w[c] + node.gap;
            }
        }

        std::vector<float> row_y(n, cr.y);
        for (std::size_t r_idx = 0; r_idx < n; ++r_idx) {
            const int rid = children[r_idx];
            const float rh = auto_nodes_[static_cast<std::size_t>(rid)].meas_h;
            auto &rn = auto_nodes_[static_cast<std::size_t>(rid)];
            rn.rect = {cr.x, row_y[r_idx], cr.width, rh};
            rn.content_rect = {cr.x + rn.pad_l, row_y[r_idx] + rn.pad_t,
                               std::max(0.0f, cr.width - rn.pad_l - rn.pad_r),
                               std::max(0.0f, rh - rn.pad_t - rn.pad_b)};
            rn.clip_rect = clip;
            rn.clip_rrect = c_rrect;
            rn.clip_radius = c_rad;
            rn.root_box_id = root_id;
            rn.in_scroll = node.in_scroll;
            if (r_idx + 1 < n) {
                row_y[r_idx + 1] = row_y[r_idx] + rh + node.line_gap;
            }
        }

        std::vector<std::vector<bool>> occ(n, std::vector<bool>(ncols, false));
        for (std::size_t r_idx = 0; r_idx < n; ++r_idx) {
            const int rid = children[r_idx];
            const auto &rn = auto_nodes_[static_cast<std::size_t>(rid)];
            const auto r_cells = rn.children;
            std::size_t c = 0;
            for (const int cid : r_cells) {
                while (c < ncols && occ[r_idx][c]) ++c;
                if (c >= ncols) break;
                const auto &cell = auto_nodes_[static_cast<std::size_t>(cid)];
                const int cspan = std::clamp(cell.col_span, 1, static_cast<int>(ncols - c));
                const int rspan = std::clamp(cell.row_span, 1, static_cast<int>(n - r_idx));
                for (int rr = 0; rr < rspan; ++rr) {
                    for (int cc = 0; cc < cspan; ++cc) {
                        occ[r_idx + static_cast<std::size_t>(rr)][c + static_cast<std::size_t>(cc)] = true;
                    }
                }
                float span_w = node.gap * static_cast<float>(cspan - 1);
                for (int cc = 0; cc < cspan; ++cc) span_w += col_w[c + static_cast<std::size_t>(cc)];
                float span_h = node.line_gap * static_cast<float>(rspan - 1);
                for (int rr = 0; rr < rspan; ++rr) {
                    span_h += auto_nodes_[static_cast<std::size_t>(children[r_idx + static_cast<std::size_t>(rr)])].content_rect.height;
                }
                const float ch = rspan > 1 ? span_h : resolve_cross(rn.cross_align, cell.height_spec, cell.meas_h,
                                                                    rn.content_rect.height, cell.limits.min_h, cell.limits.max_h);
                const float cy = rn.content_rect.y +
                                 (rspan > 1 ? 0.0f : AlignOffset(rn.cross_align, rn.content_rect.height, ch));
                const float cx = rtl ? col_x[c + static_cast<std::size_t>(cspan - 1)] : col_x[c];
                ArrangeAutoNode(cid, {cx, cy, span_w, ch}, clip, node.in_scroll, root_id,
                                rn.row_id, static_cast<int>(c), c_rad, c_rrect);
                c += static_cast<std::size_t>(cspan);
            }
        }
        return;
    }
    case AutoNode::Kind::TableRow: {
        return;
    }
    case AutoNode::Kind::Flow: {
        if (n == 0) {
            return;
        }
        struct FlowLine {
            std::vector<int> ids;
            std::vector<float> ws;
            std::vector<float> hs;
            std::vector<float> bases;
            float used_w = 0.0f;
            float height = 0.0f;
            float max_base = 0.0f;
        };
        std::vector<FlowLine> lines(1);
        float cx = 0.0f;
        for (const int cid : children) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (!c.visible) {
                ArrangeAutoNode(cid, {cr.x, cr.y, 0.0f, 0.0f}, clip, node.in_scroll, root_id,
                                eff_row_id, eff_col_idx, c_rad, c_rrect);
                continue;
            }
            const float cw = std::min(c.meas_w, cr.width);
            const float ch = c.meas_h;
            if (cx > 0.0f && cx + cw > cr.width + 0.01f) {
                lines.emplace_back();
                cx = 0.0f;
            }
            auto &ln = lines.back();
            ln.ids.push_back(cid);
            ln.ws.push_back(cw);
            ln.hs.push_back(ch);
            const float b = c.baseline > 0.0f ? c.baseline : ch * 0.8f;
            ln.bases.push_back(b);
            ln.height = std::max(ln.height, ch);
            ln.max_base = std::max(ln.max_base, b);
            ln.used_w += (ln.ids.size() > 1 ? node.gap : 0.0f) + cw;
            cx += cw + node.gap;
        }
        UIAxisAlign eff_main = node.main_align;
        if (rtl) {
            if (eff_main == UIAxisAlign::Start) eff_main = UIAxisAlign::End;
            else if (eff_main == UIAxisAlign::End) eff_main = UIAxisAlign::Start;
        }
        float y = cr.y;
        for (auto &ln : lines) {
            float grow_sum = 0.0f;
            for (const int cid : ln.ids) {
                const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
                if (c.width_spec.kind == UISizeKind::Grow) grow_sum += c.width_spec.value;
            }
            if (grow_sum > 0.0f && cr.width > ln.used_w) {
                const float rem = cr.width - ln.used_w;
                for (std::size_t i = 0; i < ln.ids.size(); ++i) {
                    const auto &c = auto_nodes_[static_cast<std::size_t>(ln.ids[i])];
                    if (c.width_spec.kind == UISizeKind::Grow) {
                        const float add = rem * (c.width_spec.value / grow_sum);
                        ln.ws[i] += add;
                        ln.used_w += add;
                    }
                }
            }
            float x_start = 0.0f, extra_gap = 0.0f;
            ComputeMainAxisSpacing(eff_main, std::max(0.0f, cr.width - ln.used_w), ln.ids.size(),
                                   x_start, extra_gap);
            float x = rtl ? (cr.x + cr.width - x_start) : (cr.x + x_start);
            for (std::size_t i = 0; i < ln.ids.size(); ++i) {
                const auto &c = auto_nodes_[static_cast<std::size_t>(ln.ids[i])];
                const UIAxisAlign y_align = c.has_cross_override ? c.cross_align : node.cross_align;
                float cy = y + AlignOffset(y_align, ln.height, ln.hs[i]);
                if (y_align == UIAxisAlign::Baseline) {
                    cy = y + std::max(0.0f, ln.max_base - ln.bases[i]);
                }
                const float item_x = rtl ? (x - ln.ws[i]) : x;
                ArrangeAutoNode(ln.ids[i], {item_x, cy, ln.ws[i], ln.hs[i]}, clip, node.in_scroll,
                                root_id, eff_row_id, eff_col_idx, c_rad, c_rrect);
                if (rtl) x -= (ln.ws[i] + node.gap + extra_gap);
                else x += (ln.ws[i] + node.gap + extra_gap);
            }
            y += ln.height + node.line_gap;
        }
        return;
    }
    case AutoNode::Kind::Scroll: {
        const float sb = ThemeMetric("scrollbar_width");
        const bool want_v = (node.scroll_dir == UIScrollDir::Vertical || node.scroll_dir == UIScrollDir::Both);
        const bool want_h = (node.scroll_dir == UIScrollDir::Horizontal || node.scroll_dir == UIScrollDir::Both);
        const float view_w = std::max(0.0f, cr.width - (want_v ? sb : 0.0f));
        const float view_h = std::max(0.0f, cr.height - (want_h && node.content_width > view_w ? sb : 0.0f));
        auto &saved = scroll_states_[node.key];
        saved.height = node.content_height;
        saved.width = node.content_width;

        const bool old_input_only = scroll_input_only_;
        const bool saved_modal = in_modal_, saved_popup = in_popup_;
        in_modal_ = !node.modal_id.empty() && node.modal_id == modal_;
        in_popup_ = !node.popup_id.empty() && node.popup_id == popup_;
        scroll_input_only_ = true;
        if (input_ && camera_ && renderer_) {
            saved.offset = PaintScrollArea(node.key, cr.x, cr.y, cr.width, view_h,
                                           node.content_height, saved.offset, false);
        } else {
            saved.offset = std::clamp(saved.offset, 0.0f, std::max(0.0f, node.content_height - view_h));
        }
        saved.offset_x = want_h
                             ? std::clamp(saved.offset_x, 0.0f, std::max(0.0f, node.content_width - view_w))
                             : 0.0f;
        in_modal_ = saved_modal;
        in_popup_ = saved_popup;
        scroll_input_only_ = old_input_only;
        node.scroll_offset = saved.offset;
        node.scroll_offset_x = saved.offset_x;

        float y = cr.y - saved.offset;
        for (const int cid : children) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            const float cw = want_h
                                 ? c.meas_w
                                 : resolve_cross(node.cross_align, c.width_spec, c.meas_w, view_w,
                                                 c.limits.min_w, c.limits.max_w);
            const float cx = (cr.x - saved.offset_x) + (want_h ? 0.0f : AlignOffset(node.cross_align, view_w, cw));
            ArrangeAutoNode(cid, {cx, y, cw, c.meas_h}, clip, true, root_id, eff_row_id,
                            eff_col_idx, c_rad, c_rrect);
            y += c.meas_h + node.gap;
        }
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Pass 3: Commit (Solved Rects + Audit + Tooltips + Paint)
// ---------------------------------------------------------------------------

void UISystem::CommitAutoNode(int idx, const Rect &parent_bounds, bool overlay_pass,
                              std::vector<int> *overlay_queue) {
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    if (!node.visible) {
        return;
    }
    if (!overlay_pass && overlay_queue && node.kind == AutoNode::Kind::AnchorRoot &&
        node.overlay && node.parent >= 0) {
        overlay_queue->push_back(idx);
        return;
    }
    if (node.anim_duration > 0.0f && !node.anim_key.empty()) {
        const Rect animated = ApplyTransitionBox(node, node.rect);
        node.rect = animated;
        node.content_rect = {
            animated.x + node.pad_l,
            animated.y + node.pad_t,
            std::max(0.0f, animated.width - node.pad_l - node.pad_r),
            std::max(0.0f, animated.height - node.pad_t - node.pad_b),
        };
    }
    const Rect &r = node.rect;
    const Rect &clip = node.clip_rect;
    const Rect vis = IntersectClip(r, clip);

    if (!node.key.empty()) {
        widget_rects_[node.key] = r;
        curr_solved_widgets_[node.key] = {r, clip, node.layer, node.disabled, node.modal_id, node.popup_id};
    }
    if (!node.remember_name.empty()) {
        named_rects_[node.remember_name] = r;
    }

    for (auto &ag : align_groups_) {
        if (ag.parent_auto_idx == idx) {
            ag.parent_rect = node.content_rect;
        }
    }

    auto emit_audit = [&](std::string_view kind, std::string_view name, bool extra_overflow = false) {
        constexpr float eps = 0.05f;
        const bool ov = extra_overflow || r.x < parent_bounds.x - eps || r.y < parent_bounds.y - eps ||
                        r.x + r.width > parent_bounds.x + parent_bounds.width + eps ||
                        r.y + r.height > parent_bounds.y + parent_bounds.height + eps;
        AuditRect e;
        e.kind = std::string(kind);
        e.rect = r;
        e.bounds = parent_bounds;
        e.overflow = ov;
        e.name = std::string(name);
        if (!node.text.empty()) {
            e.text = node.text;
        } else if (node.kind == AutoNode::Kind::TextInput ||
                   node.kind == AutoNode::Kind::TextArea ||
                   node.kind == AutoNode::Kind::TextPassword) {
            if (const auto it = widgets_.find(node.key); it != widgets_.end()) {
                e.text = it->second.text;
            }
        }
        e.clip_rect = clip;
        e.box_id = node.box_id;
        e.parent_box_id =
            (node.parent >= 0 && static_cast<std::size_t>(node.parent) < auto_nodes_.size())
                ? auto_nodes_[static_cast<std::size_t>(node.parent)].box_id
                : 0;
        e.root_box_id = node.root_box_id;
        e.box_depth = node.box_depth;
        e.row_id = node.row_id;
        e.col_index = node.col_index;
        e.layer = node.layer;
        e.suppressed = node.suppressed;
        e.in_scroll = node.in_scroll;
        e.disabled = node.disabled;
        e.margin_l = node.margin_l;
        e.margin_t = node.margin_t;
        e.margin_r = node.margin_r;
        e.margin_b = node.margin_b;
        e.pad_l = node.pad_l;
        e.pad_t = node.pad_t;
        e.pad_r = node.pad_r;
        e.pad_b = node.pad_b;
        e.meas_w = node.meas_w;
        e.meas_h = node.meas_h;
        const std::size_t audit_idx = audit_.size();
        audit_.push_back(std::move(e));
        for (const int ag_id : node.align_group_ids) {
            if (ag_id >= 0 && static_cast<std::size_t>(ag_id) < align_groups_.size()) {
                auto &ag = align_groups_[static_cast<std::size_t>(ag_id)];
                if (ag.audit_start > audit_idx) {
                    ag.audit_start = audit_idx;
                }
                ag.audit_end = std::max(ag.audit_end, audit_idx + 1);
            }
        }
        if (audit_enabled_ && ov) {
            std::fprintf(stderr,
                         "[UI_OVERFLOW] %.*s rect=(%.1f,%.1f,%.1f,%.1f) box=(%.1f,%.1f,%.1f,%.1f)\n",
                         static_cast<int>(kind.size()), kind.data(), r.x, r.y, r.width, r.height,
                         parent_bounds.x, parent_bounds.y, parent_bounds.width, parent_bounds.height);
        }
    };

    if (!node.tip_text.empty() && !node.suppressed && vis.width > 0.0f && vis.height > 0.0f) {
        if (tip_recorder_) {
            tip_recorder_(vis, node.tip_text, false);
        }
        // Keep the built-in tooltip path usable without an external tracker.
        if (input_ && camera_ && MouseVisible() && vis.Contains(Mouse())) {
            const std::string key = std::to_string(static_cast<int>(vis.x)) + "," +
                                    std::to_string(static_cast<int>(vis.y)) + "," + node.tip_text;
            const double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (key != tip_hover_key_) {
                tip_hover_key_ = key;
                tip_hover_since_ = now;
            }
            if (tip_delay_ <= 0.0f || now - tip_hover_since_ >= tip_delay_) {
                tooltip_ = node.tip_text;
                tooltip_mouse_ = Mouse();
            }
        } else {
            tip_hover_key_.clear();
        }
    }
    if (!node.context_menu_key.empty() && !node.disabled && input_ && CanInteract() &&
        MouseVisible() && vis.width > 0.0f && vis.Contains(Mouse()) && input_->MousePressed(1)) {
        const Vec2 rm = RawMouse();
        OpenPopup(node.context_menu_key, rm.x, rm.y);
    }

    ++frame_stats_.nodes;
    if (vis.width > 0.0f && vis.height > 0.0f) {
        ++frame_stats_.visible;
    }
    ++frame_stats_.draw_commands;

    if (recording_) {
        switch (node.kind) {
        case AutoNode::Kind::Button:
        case AutoNode::Kind::Slider:
        case AutoNode::Kind::Checkbox:
        case AutoNode::Kind::RadioButton:
        case AutoNode::Kind::Tab:
        case AutoNode::Kind::Tree:
        case AutoNode::Kind::ComboBegin:
        case AutoNode::Kind::ComboItem:
        case AutoNode::Kind::Menu:
        case AutoNode::Kind::MenuItem:
        case AutoNode::Kind::ImageButton:
        case AutoNode::Kind::Splitter:
        case AutoNode::Kind::Window:
        case AutoNode::Kind::TextInput:
        case AutoNode::Kind::TextArea:
        case AutoNode::Kind::TextPassword:
            deferred_auto_nodes_.push_back(idx);
            break;
        default:
            break;
        }
    }

    // Focus ring registration for interactive widgets
    auto register_focus = [&](bool is_text, bool adjustable) {
        if (!recording_ && !node.disabled && (modal_.empty() || modal_ == node.modal_id) &&
            (popup_.empty() || popup_ == node.popup_id) && vis.width > 0.0f && vis.height > 0.0f) {
            if (std::none_of(focus_ring_.begin(), focus_ring_.end(),
                             [&](const FocusEntry &fe) { return fe.key == node.key; })) {
                focus_ring_.push_back({node.key, r, is_text, adjustable, node.focus_scope_id});
            }
        }
    };

    ApplyClipRectDirect(clip, node.clip_radius, &node.clip_rrect);
    DrawNodeBackdrop(r, node);

    const float eff_alpha = node.alpha_mul * (node.kind == AutoNode::Kind::DragSource && drag_active_ &&
                                              drag_source_key_ == node.key ? 0.45f : 1.0f);
    (void)eff_alpha;
    if (node.kind == AutoNode::Kind::DragSource && !node.disabled && vis.width > 0.0f &&
        vis.Contains(Mouse()) && MouseVisible() && desired_cursor_shape_ == 0) {
        desired_cursor_shape_ = 3; // hand
    }
    if (node.kind == AutoNode::Kind::DropTarget && drag_active_ &&
        drop_hovered_key_ == node.key && renderer_) {
        DrawRoundedFrame(r, node.radius, 2.0f, ThemeColor("accent"));
    }

    if (node.draw_panel) {
        const Color fill = node.themed_panel ? ThemeColor("panel") : node.fill_col;
        const Color brd = node.themed_panel ? ThemeColor("border") : node.border_col;
        if (renderer_) {
            if (panel_slice_.texture && panel_slice_.texture->IsValid() && node.themed_panel) {
                const auto &s = panel_slice_;
                const float cx = std::min(s.x, r.width * 0.5f), cy = std::min(s.y, r.height * 0.5f);
                const float sx0 = s.source.x, sy0 = s.source.y, sw = s.source.width, sh = s.source.height;
                const float sx[4] = {sx0, sx0 + s.x, sx0 + sw - s.x, sx0 + sw};
                const float sy[4] = {sy0, sy0 + s.y, sy0 + sh - s.y, sy0 + sh};
                const float dx[4] = {r.x, r.x + cx, r.x + r.width - cx, r.x + r.width};
                const float dy[4] = {r.y, r.y + cy, r.y + r.height - cy, r.y + r.height};
                for (int py_i = 0; py_i < 3; ++py_i) {
                    for (int px_i = 0; px_i < 3; ++px_i) {
                        const float dw = dx[px_i + 1] - dx[px_i], dh = dy[py_i + 1] - dy[py_i];
                        const float swp = sx[px_i + 1] - sx[px_i], shp = sy[py_i + 1] - sy[py_i];
                        if (dw > 0 && dh > 0 && swp > 0 && shp > 0) {
                            renderer_->DrawSprite(*s.texture, {sx[px_i], sy[py_i], swp, shp},
                                                  MapRect({dx[px_i], dy[py_i], dw, dh}), Color::White());
                        }
                    }
                }
            } else {
                const float border = ThemeMetric("border");
                if (node.radius > 0.5f) {
                    if (brd.a > 0.0f) {
                        DrawRoundedFrame({r.x - border, r.y - border, r.width + 2 * border,
                                          r.height + 2 * border},
                                         node.radius + border, border, brd);
                    }
                    if (fill.a > 0.0f) {
                        DrawRoundedQuad(r, node.radius, fill);
                    }
                } else {
                    if (brd.a > 0.0f) {
                        renderer_->DrawQuad(MapRect({r.x - border, r.y - border, r.width + 2 * border,
                                                     r.height + 2 * border}),
                                            brd);
                    }
                    if (fill.a > 0.0f) {
                        renderer_->DrawQuad(MapRect(r), fill);
                    }
                }
            }
        }
        emit_audit("panel", node.key);
    } else if (!node.align_group_ids.empty() &&
               (node.kind == AutoNode::Kind::Root || node.kind == AutoNode::Kind::AnchorRoot)) {
        // Explicit alignment constraints describe geometry, even for a
        // transparent container with no panel paint to emit an audit record.
        emit_audit("auto box", node.key);
    }
    if (renderer_) {
        for (const auto &cmd : node.custom_cmds) {
            if (cmd.op == AutoCustomCmd::Op::NineSlice && cmd.texture && cmd.texture->IsValid()) {
                renderer_->DrawNineSlice(*cmd.texture, cmd.src, MapRect(r),
                                         cmd.corner_x * scale_, cmd.corner_y * scale_, cmd.fill);
            }
        }
    }

    switch (node.kind) {
    case AutoNode::Kind::Root:
    case AutoNode::Kind::AnchorRoot:
    case AutoNode::Kind::SafeArea:
    case AutoNode::Kind::DragSource:
    case AutoNode::Kind::DropTarget:
    case AutoNode::Kind::MenuBar:
    case AutoNode::Kind::VStack:
    case AutoNode::Kind::HStack:
    case AutoNode::Kind::Table:
    case AutoNode::Kind::TableRow:
    case AutoNode::Kind::TableCell:
    case AutoNode::Kind::Flow:
    case AutoNode::Kind::ZStack:
    case AutoNode::Kind::Grid:
    case AutoNode::Kind::Card: {
        for (const int cid : node.children) {
            CommitAutoNode(cid, node.content_rect, false, overlay_queue);
        }
        break;
    }
    case AutoNode::Kind::CachedBox: {
        for (const int cid : node.children) {
            CommitAutoNode(cid, node.content_rect, false, overlay_queue);
        }
        if (!node.cached_hit) {
            CaptureSubtreeCache(idx);
        }
        break;
    }
    case AutoNode::Kind::CustomBox: {
        if (renderer_) {
            const float ox = node.content_rect.x;
            const float oy = node.content_rect.y;
            for (const auto &cmd : node.custom_cmds) {
                if (cmd.op == AutoCustomCmd::Op::Quad) {
                    Rect qr{ox + cmd.rect.x, oy + cmd.rect.y, cmd.rect.width, cmd.rect.height};
                    const float border = ThemeMetric("border");
                    if (cmd.border.a > 0.0f) {
                        renderer_->DrawQuad(MapRect({qr.x - border, qr.y - border,
                                                     qr.width + 2 * border, qr.height + 2 * border}),
                                            cmd.border);
                    }
                    renderer_->DrawQuad(MapRect(qr), cmd.fill);
                } else if (cmd.op == AutoCustomCmd::Op::Sprite && cmd.texture && cmd.texture->IsValid()) {
                    Rect dr{ox + cmd.rect.x, oy + cmd.rect.y, cmd.rect.width, cmd.rect.height};
                    renderer_->DrawSprite(*cmd.texture, cmd.src, MapRect(dr), cmd.fill);
                } else if (cmd.op == AutoCustomCmd::Op::Text) {
                    const float xx = (ox + cmd.rect.x) * scale_ + scale_offset_.x;
                    const float yy = (oy + cmd.rect.y) * scale_ + scale_offset_.y;
                    if (text_hooks_.draw) {
                        text_hooks_.draw(renderer_->GetSpriteBatch(), cmd.text, xx, yy,
                                         cmd.px * scale_, cmd.fill, cmd.align, cmd.face);
                    } else {
                        const float sc = (cmd.px / 32.0f) * scale_;
                        const float mw = renderer_->MeasureText(cmd.text, sc);
                        const float ax = cmd.align == 1 ? (xx - mw * 0.5f) : (cmd.align == 2 ? xx - mw : xx);
                        renderer_->DrawTextEx(cmd.text, ax, yy, sc, 0, 0, cmd.fill);
                    }
                } else if (cmd.op == AutoCustomCmd::Op::WrappedText) {
                    const float xx = (ox + cmd.rect.x) * scale_ + scale_offset_.x;
                    const float yy = (oy + cmd.rect.y) * scale_ + scale_offset_.y;
                    if (text_hooks_.draw_wrapped) {
                        text_hooks_.draw_wrapped(renderer_->GetSpriteBatch(), cmd.text, xx, yy,
                                                 cmd.px * scale_, cmd.rect.width * scale_, cmd.fill,
                                                 cmd.face);
                    } else {
                        renderer_->DrawTextEx(cmd.text, xx, yy, (cmd.px / 32.0f) * scale_, 0,
                                              cmd.rect.width * scale_, cmd.fill);
                    }
                }
            }
        }
        for (const int cid : node.children) {
            CommitAutoNode(cid, node.content_rect, false, overlay_queue);
        }
        break;
    }
    case AutoNode::Kind::Scroll: {
        emit_audit("scroll viewport", node.key);
        const float sb = ThemeMetric("scrollbar_width");
        const bool want_v = (node.scroll_dir == UIScrollDir::Vertical || node.scroll_dir == UIScrollDir::Both);
        const bool want_h = (node.scroll_dir == UIScrollDir::Horizontal || node.scroll_dir == UIScrollDir::Both);
        const Rect scroll_bounds{
            node.content_rect.x - node.scroll_offset_x,
            node.content_rect.y - node.scroll_offset,
            std::max(std::max(0.0f, node.content_rect.width - (want_v ? sb : 0.0f)), node.content_width),
            std::max(node.content_rect.height, node.content_height),
        };
        for (const int cid : node.children) {
            CommitAutoNode(cid, scroll_bounds, false, overlay_queue);
        }
        ApplyClipRectDirect(clip, node.clip_radius, &node.clip_rrect);
        const auto &st = widgets_[node.key];
        if (want_v) {
            DrawScrollbarVisual(node.content_rect, node.content_height, node.scroll_offset, st.hover,
                                st.dragging, node.radius, node.alpha_mul);
        }
        if (want_h && node.content_width > node.content_rect.width) {
            DrawHScrollbarVisual(node.content_rect, node.content_width, node.scroll_offset_x,
                                 st.hover, st.dragging, node.radius, node.alpha_mul);
        }
        break;
    }
    case AutoNode::Kind::MenuSeparator: {
        if (renderer_ && r.width > 0.0f) {
            Color c = ThemeColor("border");
            c.a *= 0.6f * node.alpha_mul;
            renderer_->DrawQuad(MapRect({r.x + 4.0f, r.y + r.height * 0.5f,
                                         std::max(0.0f, r.width - 8.0f), 1.0f}), c);
        }
        break;
    }
    case AutoNode::Kind::Menu:
    case AutoNode::Kind::MenuItem: {
        const bool is_menu = (node.kind == AutoNode::Kind::Menu);
        emit_audit(is_menu ? "menu" : "menu_item", node.key);
        register_focus(false, false);
        auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        if (hov && !node.disabled && desired_cursor_shape_ == 0) desired_cursor_shape_ = 3;
        StepWidgetTransition(st, hov, st.pressed, nav_focus_ == node.key);
        if (renderer_) {
            Color bg = Color::Clear();
            if (st.pressed || node.checked) bg = ThemeColor("accent");
            else if (hov || st.hover_t > 0.01f) bg = MixColor(Color{0, 0, 0, 0}, ThemeColor("hover"), st.hover_t);
            bg.a *= node.alpha_mul;
            if (bg.a > 0.01f) {
                DrawRoundedQuad(r, node.radius > 0.5f ? node.radius : 3.0f, bg);
            }
            const float px = ThemeMetric("button_pad_x");
            const float sc = AutoWidgetTextScale(node.scale);
            const float th = FontLineHeightPx(sc);
            const float ty = r.y + (r.height - th) * 0.5f;
            Color tc = ThemeColor("text");
            tc.a *= node.alpha_mul * (node.disabled ? 0.45f : 1.0f);
            float tx = r.x + px;
            if (!is_menu) {
                if (node.checked) {
                    renderer_->DrawTextEx("*", tx * scale_ + scale_offset_.x,
                                          ty * scale_ + scale_offset_.y, sc * scale_, 0, 0,
                                          ThemeColor("check"));
                }
                tx += 14.0f;
            }
            renderer_->DrawTextEx(node.text, tx * scale_ + scale_offset_.x,
                                  ty * scale_ + scale_offset_.y, sc * scale_, 0, 0, tc);
            if (!node.shortcut.empty()) {
                const float sw = renderer_->MeasureText(node.shortcut, sc * 0.9f);
                Color sc_col = tc;
                sc_col.a *= 0.65f;
                renderer_->DrawTextEx(node.shortcut,
                                      (r.x + r.width - px - sw) * scale_ + scale_offset_.x,
                                      ty * scale_ + scale_offset_.y, sc * 0.9f * scale_, 0, 0, sc_col);
            } else if (is_menu && node.col_span > 0) {
                renderer_->DrawTextEx(">", (r.x + r.width - px - 8.0f) * scale_ + scale_offset_.x,
                                      ty * scale_ + scale_offset_.y, sc * scale_, 0, 0, tc);
            }
        }
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::Text: {
        const bool clip_ov =
            (node.overflow == UITextOverflow::Clip && renderer_ && !node.text.empty() &&
             node.scale > 0.0f &&
             renderer_->MeasureText(node.text, node.scale) > r.width + 0.05f);
        emit_audit("text", node.key, clip_ov);
        if (!renderer_ || r.width <= 0.0f || node.text.empty() || node.scale <= 0.0f) {
            break;
        }
        Color text_color = (node.fill_col.r == 1.0f && node.fill_col.g == 1.0f &&
                            node.fill_col.b == 1.0f && node.fill_col.a == 1.0f)
                               ? ThemeColor("text")
                               : node.fill_col;
        text_color.a *= node.alpha_mul;
        if (node.disabled) {
            text_color.a *= 0.4f;
        }
        float used = node.scale;
        std::string drawn = node.text;
        const bool wrap = node.overflow == UITextOverflow::Wrap;
        if (node.overflow == UITextOverflow::Shrink) {
            const float natural = renderer_->MeasureText(node.text, node.scale);
            if (natural > r.width && natural > 0.0f) {
                used = std::max(node.min_scale, node.scale * r.width / natural);
            }
        }
        if (node.overflow == UITextOverflow::Ellipsis || node.overflow == UITextOverflow::Shrink) {
            drawn = Ellipsize(node.text, r.width, used);
        }
        int eff_align = node.text_align;
        if (eff_align == 0 && node.layout_dir == UILayoutDirection::RTL) {
            eff_align = 2;
        }
        const float measured = wrap ? r.width : renderer_->MeasureText(drawn, used);
        const float dx = r.x + (wrap ? 0.0f
                                     : (eff_align == 1 ? (r.width - measured) * 0.5f
                                        : eff_align == 2 ? (r.width - measured)
                                                         : 0.0f));
        const Rect text_clip = IntersectClip(clip, r);
        ApplyClipRectDirect(text_clip, node.clip_radius, &node.clip_rrect);
        renderer_->DrawTextEx(drawn, dx * scale_ + scale_offset_.x,
                              r.y * scale_ + scale_offset_.y, used * scale_,
                              wrap ? eff_align : 0, wrap ? r.width * scale_ : 0.0f,
                              text_color);
        break;
    }
    case AutoNode::Kind::Button: {
        emit_audit("button", node.key);
        register_focus(false, false);
        auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        if (hov && !node.disabled && desired_cursor_shape_ == 0) desired_cursor_shape_ = 3;
        StepWidgetTransition(st, hov, st.pressed, nav_focus_ == node.key);
        const Color bc = node.has_btn_colors ? node.btn_col : ThemeColor("button");
        const Color hc = node.has_btn_colors ? node.btn_hover : ThemeColor("hover");
        const Color pc = node.has_btn_colors ? node.btn_pressed : ThemeColor("pressed");
        const Color brd = node.has_btn_colors ? node.btn_border : ThemeColor("border");
        const Color tc = node.has_btn_colors ? node.btn_text : ThemeColor("text");
        DrawButtonVisual(r, hov, st.pressed, node.disabled, node.text, node.scale, bc, hc, pc, brd, tc,
                         node.radius, st.hover_t, st.press_t, node.alpha_mul);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::ProgressBar: {
        emit_audit("progress", node.key);
        DrawProgressVisual(r, node.val, node.fill_col, node.track_col, node.radius, node.alpha_mul);
        break;
    }
    case AutoNode::Kind::Slider: {
        emit_audit("slider", node.key);
        register_focus(false, true);
        auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        // A newly submitted immediate slider has no previous-frame geometry.
        // Its solved rectangle is now available: retain the initial press so
        // next frame's normal drag path sees the original thumb grab offset.
        // Recording/deferred frames process input in ResolveDeferredAutoNodes.
        if (!recording_ && !prev_solved_widgets_.contains(node.key) &&
            !node.disabled && input_ && CanInteract() && hov &&
            (modal_.empty() || modal_ == node.modal_id) &&
            (popup_.empty() || popup_ == node.popup_id) &&
            node.max_val > node.min_val && input_->MousePressed(0)) {
            const float thumb_w = r.height * 0.6f;
            const float range_px = std::max(1.0f, r.width - thumb_w);
            const float t = (node.val - node.min_val) / (node.max_val - node.min_val);
            const Rect thumb{r.x + t * range_px, r.y, thumb_w, r.height};
            st.grab = thumb.Contains(Mouse()) ? Mouse().x - thumb.x : thumb_w * 0.5f;
            st.dragging = st.pressed = true;
            nav_focus_ = node.key;
            pointer_over_ = true;
            ++click_count_;
        }
        if (hov && !node.disabled && desired_cursor_shape_ == 0) desired_cursor_shape_ = 3;
        StepWidgetTransition(st, hov, st.dragging, nav_focus_ == node.key);
        DrawSliderVisual(r, node.val, node.min_val, node.max_val, hov, st.dragging,
                         node.radius, st.hover_t, node.alpha_mul);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::Checkbox:
    case AutoNode::Kind::RadioButton: {
        const bool radio = (node.kind == AutoNode::Kind::RadioButton);
        emit_audit(radio ? "radio" : "checkbox", node.key);
        register_focus(false, false);
        auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        if (hov && !node.disabled && desired_cursor_shape_ == 0) desired_cursor_shape_ = 3;
        StepWidgetTransition(st, hov, st.pressed, nav_focus_ == node.key);
        DrawCheckboxVisual(r, node.checked, node.text, node.scale, hov, radio,
                           node.radius, st.hover_t, node.alpha_mul);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::Tab: {
        emit_audit("tab", node.key);
        register_focus(false, false);
        auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        if (hov && !node.disabled && desired_cursor_shape_ == 0) desired_cursor_shape_ = 3;
        StepWidgetTransition(st, hov, st.pressed, nav_focus_ == node.key);
        DrawTabVisual(r, node.text, node.checked, hov, st.pressed, node.scale,
                      node.radius, st.hover_t, node.alpha_mul);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::Tree: {
        emit_audit("tree", node.key);
        register_focus(false, false);
        auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        if (hov && !node.disabled && desired_cursor_shape_ == 0) desired_cursor_shape_ = 3;
        DrawTreeVisual(r, node.text, node.checked, hov, node.scale, node.radius, node.alpha_mul);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::ComboBegin:
    case AutoNode::Kind::ComboItem: {
        const bool is_item = (node.kind == AutoNode::Kind::ComboItem);
        emit_audit(is_item ? "combo_item" : "combo", node.key);
        register_focus(false, false);
        auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        if (hov && !node.disabled && desired_cursor_shape_ == 0) desired_cursor_shape_ = 3;
        StepWidgetTransition(st, hov, st.pressed, nav_focus_ == node.key);
        DrawComboVisual(r, node.text, node.checked, node.checked, hov, node.scale, is_item,
                        node.radius, st.hover_t, node.alpha_mul);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::TextInput: {
        emit_audit("text_input", node.key);
        register_focus(true, false);
        if (!node.disabled && vis.width > 0 && vis.Contains(Mouse()) && MouseVisible()) desired_cursor_shape_ = 1;
        EditText(node.key, r, node.max_chars, false, false, true, !recording_,
                 node.placeholder, node.radius, node.alpha_mul);
        break;
    }
    case AutoNode::Kind::TextArea: {
        emit_audit("text_area", node.key);
        register_focus(true, false);
        if (!node.disabled && vis.width > 0 && vis.Contains(Mouse()) && MouseVisible()) desired_cursor_shape_ = 1;
        EditText(node.key, r, node.max_chars, true, false, true, !recording_ && !node.input_processed,
                 node.placeholder, node.radius, node.alpha_mul);
        break;
    }
    case AutoNode::Kind::TextPassword: {
        emit_audit("password", node.key);
        register_focus(true, false);
        if (!node.disabled && vis.width > 0 && vis.Contains(Mouse()) && MouseVisible()) desired_cursor_shape_ = 1;
        EditText(node.key, r, node.max_chars, false, true, true, !recording_,
                 node.placeholder, node.radius, node.alpha_mul);
        break;
    }
    case AutoNode::Kind::Splitter: {
        emit_audit("splitter", node.key);
        register_focus(false, true);
        const auto &st = widgets_[node.key];
        const float eff_min = std::clamp(node.min_val, 0.0f, r.width * 0.5f);
        const float gx = std::clamp(st.grab, eff_min, std::max(eff_min, r.width - eff_min));
        const Rect divider{r.x + gx - 3.0f, r.y, 6.0f, r.height};
        if (st.dragging || (vis.width > 0 && divider.Contains(Mouse()) && MouseVisible())) {
            desired_cursor_shape_ = 4; // hresize
        }
        if (renderer_) {
            Color c = ThemeColor(st.dragging ? "accent" : "border");
            c.a *= node.alpha_mul;
            DrawRoundedQuad(divider, std::min(node.radius, 3.0f), c);
        }
        FocusIndicator(node.key, divider);
        break;
    }
    case AutoNode::Kind::Window: {
        emit_audit("window", node.key);
        const auto &st = widgets_[node.key];
        if (renderer_) {
            const Color bar_col = st.dragging ? ThemeColor("accent") : kScrollThumb;
            renderer_->DrawQuad(MapRect(r), bar_col);
            renderer_->DrawQuad(MapRect({r.x, r.y + r.height, r.width, 1.0f}), ThemeColor("border"));
            if (!node.text.empty()) {
                const float th = FontLineHeightPx(node.scale);
                const std::string fitted = Ellipsize(node.text, std::max(0.0f, r.width - 16.0f), node.scale);
                renderer_->DrawTextEx(fitted, (r.x + 8.0f) * scale_ + scale_offset_.x,
                                      (r.y + r.height * 0.5f - th * 0.5f) * scale_ + scale_offset_.y,
                                      node.scale * scale_, 0, 0, Color::White());
            }
        }
        break;
    }
    case AutoNode::Kind::RichText: {
        emit_audit("rich text", node.key);
        DrawRichTextSolved(node, r, clip, true, !node.input_processed);
        break;
    }
    case AutoNode::Kind::ListViewSlot: {
        emit_audit("virtual viewport", node.key);
        auto it = list_views_.find(node.key);
        if (it != list_views_.end()) {
            it->second.rect = r;
            it->second.clip_rect = clip;
        }
        break;
    }
    case AutoNode::Kind::Divider: {
        if (renderer_ && r.width > 0.0f && r.height > 0.0f) {
            Color c = node.fill_col;
            if (node.disabled) c.a *= 0.4f;
            renderer_->DrawQuad(MapRect(r), c);
        }
        break;
    }
    case AutoNode::Kind::Image:
    case AutoNode::Kind::ImageButton: {
        const bool is_btn = (node.kind == AutoNode::Kind::ImageButton);
        emit_audit(is_btn ? "image button" : "image", node.key);
        if (is_btn) {
            register_focus(false, false);
            const auto &st = widgets_[node.key];
            if (vis.width > 0 && vis.Contains(Mouse()) && !node.disabled && MouseVisible() &&
                desired_cursor_shape_ == 0) {
                desired_cursor_shape_ = 3;
            }
            if (renderer_) {
                const float border = ThemeMetric("border");
                Color brd = st.hover ? ThemeColor("hover") : ThemeColor("border");
                Color fill = st.pressed ? ThemeColor("pressed")
                                        : st.hover ? ThemeColor("hover")
                                                   : ThemeColor("button");
                brd.a *= node.alpha_mul;
                fill.a *= node.alpha_mul;
                DrawRoundedQuad({r.x - border, r.y - border, r.width + 2 * border, r.height + 2 * border},
                                node.radius > 0.5f ? node.radius + border : 0.0f, brd);
                DrawRoundedQuad(r, node.radius, fill);
            }
        }
        if (renderer_ && node.texture && node.texture->IsValid()) {
            Color col = is_btn ? Color::White() : node.fill_col;
            if (node.disabled) {
                col.a *= 0.4f;
            }
            renderer_->DrawSprite(*node.texture, node.tex_src, MapRect(r), col);
        }
        if (is_btn) {
            FocusIndicator(node.key, r);
        }
        break;
    }
    case AutoNode::Kind::Spacer:
        break;
    }
}

// ---------------------------------------------------------------------------
// Pure Visual Painters (driven by exact Pass-2 solved Rects)
// ---------------------------------------------------------------------------

void UISystem::DrawButtonVisual(const Rect &r, bool hover, bool pressed, bool disabled,
                                std::string_view text, float scale, const Color &btn_c,
                                const Color &hov_c, const Color &prs_c, const Color &brd_c,
                                const Color &txt_c, float radius, float hover_t, float press_t,
                                float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float ht = std::max(hover_t, hover ? 1.0f : 0.0f);
    const float pt = std::max(press_t, pressed ? 1.0f : 0.0f);
    Color fill = MixColor(MixColor(btn_c, hov_c, ht), prs_c, pt);
    Color label_col = txt_c;
    Color brd = brd_c;
    fill.a *= alpha;
    label_col.a *= alpha;
    brd.a *= alpha;
    if (disabled) {
        fill.a *= 0.45f;
        label_col.a *= 0.45f;
        brd.a *= 0.45f;
    }
    const float border = ThemeMetric("border");
    if (radius > 0.5f) {
        DrawRoundedQuad({r.x - border, r.y - border, r.width + border * 2.0f, r.height + border * 2.0f},
                        radius + border, brd);
        DrawRoundedQuad(r, radius, fill);
    } else {
        renderer_->DrawQuad(
            MapRect({r.x - border, r.y - border, r.width + border * 2.0f, r.height + border * 2.0f}),
            brd);
        renderer_->DrawQuad(MapRect(r), fill);
    }

    if (!text.empty()) {
        const float lh = renderer_->GetFont().LineHeight();
        float sc = std::min(scale, std::max(0.01f, (r.height - 4.0f) / std::max(1.0f, lh)));
        const float available = std::max(0.0f, r.width - 8.0f);
        const float natural = renderer_->MeasureText(text, sc);
        const float used_scale =
            (natural > available && natural > 0.0f)
                ? std::min(sc, std::max(ThemeMetric("min_font_scale"), sc * available / natural))
                : sc;
        const std::string fitted = Ellipsize(text, available, used_scale);
        const float text_w = renderer_->MeasureText(fitted, used_scale);
        const float text_h = lh * used_scale;
        const float tx = (r.x + (r.width - text_w) * 0.5f) * scale_ + scale_offset_.x;
        const float ty = (r.y + (r.height - text_h) * 0.5f) * scale_ + scale_offset_.y;
        renderer_->DrawTextEx(fitted, tx, ty, used_scale * scale_, 0, 0, label_col);
    }
}

void UISystem::DrawProgressVisual(const Rect &r, float value, const Color &fill, const Color &track,
                                  float radius, float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float t = std::clamp(value, 0.0f, 1.0f);
    Color trk = track;
    Color fl = fill;
    trk.a *= alpha;
    fl.a *= alpha;
    if (radius > 0.5f) {
        DrawRoundedQuad(r, radius, trk);
        if (t > 0.0f) {
            DrawRoundedQuad({r.x, r.y, r.width * t, r.height}, radius, fl);
        }
    } else {
        renderer_->DrawQuad(MapRect(r), trk);
        if (t > 0.0f) {
            renderer_->DrawQuad(MapRect({r.x, r.y, r.width * t, r.height}), fl);
        }
    }
}

void UISystem::DrawSliderVisual(const Rect &r, float value, float min_v, float max_v, bool hover,
                                bool dragging, float radius, float hover_t, float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f || !(max_v > min_v)) {
        return;
    }
    const float t = std::clamp((value - min_v) / (max_v - min_v), 0.0f, 1.0f);
    const float track_h = r.height * 0.34f;
    const Rect track{r.x, r.y + (r.height - track_h) * 0.5f, r.width, track_h};
    const float thumb_w = r.height * 0.6f;
    const float range_px = std::max(0.0f, r.width - thumb_w);
    const float ht = std::max(hover_t, hover ? 1.0f : 0.0f);
    Color thumb_col = dragging ? ThemeColor("accent")
                               : MixColor(kSliderThumb, ThemeColor("hover"), ht);
    Color trk_col = ThemeColor("track");
    thumb_col.a *= alpha;
    trk_col.a *= alpha;
    if (radius > 0.5f) {
        DrawRoundedQuad({r.x - 2.0f, r.y - 2.0f, r.width + 4.0f, r.height + 4.0f}, radius + 1.0f, trk_col);
        DrawRoundedQuad(track, std::min(radius, track_h * 0.5f), trk_col);
        DrawRoundedQuad({r.x + t * range_px, r.y, thumb_w, r.height}, radius, thumb_col);
    } else {
        renderer_->DrawQuad(MapRect({r.x - 2.0f, r.y - 2.0f, r.width + 4.0f, r.height + 4.0f}),
                            trk_col);
        renderer_->DrawQuad(MapRect(track), trk_col);
        renderer_->DrawQuad(MapRect({r.x + t * range_px, r.y, thumb_w, r.height}), thumb_col);
    }
}

void UISystem::DrawCheckboxVisual(const Rect &r, bool checked, std::string_view label, float scale,
                                  bool hover, bool radio, float radius, float hover_t,
                                  float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float size = std::min(r.width, r.height);
    const Rect box{r.x, r.y + (r.height - size) * 0.5f, size, size};
    const float ht = std::max(hover_t, hover ? 1.0f : 0.0f);
    Color brd = MixColor(ThemeColor("border"), ThemeColor("hover"), ht);
    Color fld = ThemeColor("field");
    Color chk = ThemeColor("check");
    Color txt = ThemeColor("text");
    brd.a *= alpha;
    fld.a *= alpha;
    chk.a *= alpha;
    txt.a *= alpha;
    const float box_r = radio && radius > 0.5f ? (size * 0.5f) : radius;
    if (box_r > 0.5f) {
        DrawRoundedQuad({box.x - 2.0f, box.y - 2.0f, size + 4.0f, size + 4.0f}, box_r + 1.0f, brd);
        DrawRoundedQuad(box, box_r, fld);
        if (checked) {
            const float inset = size * (radio ? 0.28f : 0.26f);
            DrawRoundedQuad({box.x + inset, box.y + inset, size - inset * 2.0f, size - inset * 2.0f},
                            std::max(0.0f, box_r - inset), chk);
        }
    } else {
        renderer_->DrawQuad(MapRect({box.x - 2.0f, box.y - 2.0f, size + 4.0f, size + 4.0f}), brd);
        renderer_->DrawQuad(MapRect(box), fld);
        if (checked) {
            const float inset = size * (radio ? 0.28f : 0.26f);
            renderer_->DrawQuad(
                MapRect({box.x + inset, box.y + inset, size - inset * 2.0f, size - inset * 2.0f}),
                chk);
        }
    }
    if (!label.empty()) {
        const float avail_lbl = std::max(0.0f, r.width - size - 8.0f);
        const std::string fitted = Ellipsize(label, avail_lbl, scale);
        const float th = renderer_->GetFont().LineHeight() * scale;
        renderer_->DrawTextEx(fitted, (box.x + size + 8.0f) * scale_ + scale_offset_.x,
                              (box.y + size * 0.5f - th * 0.5f) * scale_ + scale_offset_.y,
                              scale * scale_, 0, 0, txt);
    }
}

void UISystem::DrawTabVisual(const Rect &r, std::string_view label, bool active, bool hover,
                             bool pressed, float scale, float radius, float hover_t,
                             float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float ht = std::max(hover_t, hover ? 1.0f : 0.0f);
    Color bg = pressed ? ThemeColor("pressed")
               : active ? ThemeColor("accent")
                        : MixColor(ThemeColor("panel"), ThemeColor("hover"), ht);
    Color brd = ThemeColor("border");
    Color txt = ThemeColor("text");
    bg.a *= alpha;
    brd.a *= alpha;
    txt.a *= alpha;
    const float border = ThemeMetric("border");
    if (radius > 0.5f) {
        DrawRoundedQuad({r.x - border, r.y - border, r.width + 2 * border, r.height + 2 * border},
                        radius + border, brd);
        DrawRoundedQuad(r, radius, bg);
    } else {
        renderer_->DrawQuad(
            MapRect({r.x - border, r.y - border, r.width + 2 * border, r.height + 2 * border}),
            brd);
        renderer_->DrawQuad(MapRect(r), bg);
    }
    if (!label.empty()) {
        const std::string fitted = Ellipsize(label, std::max(0.0f, r.width - 8.0f), scale);
        const float tw = renderer_->MeasureText(fitted, scale);
        const float th = renderer_->GetFont().LineHeight() * scale;
        renderer_->DrawTextEx(fitted, (r.x + (r.width - tw) * 0.5f) * scale_ + scale_offset_.x,
                              (r.y + (r.height - th) * 0.5f) * scale_ + scale_offset_.y,
                              scale * scale_, 0, 0, txt);
    }
}

void UISystem::DrawTreeVisual(const Rect &r, std::string_view label, bool open, bool hover,
                              float scale, float radius, float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    if (hover) {
        Color hc = ThemeColor("hover");
        hc.a *= alpha;
        DrawRoundedQuad(r, radius, hc);
    }
    const std::string txt = std::string(open ? "v " : "> ") + std::string(label);
    const std::string fitted = Ellipsize(txt, std::max(0.0f, r.width - 6.0f), scale);
    const float th = renderer_->GetFont().LineHeight() * scale;
    Color tc = ThemeColor("text");
    tc.a *= alpha;
    renderer_->DrawTextEx(fitted, (r.x + 4.0f) * scale_ + scale_offset_.x,
                          (r.y + (r.height - th) * 0.5f) * scale_ + scale_offset_.y,
                          scale * scale_, 0, 0, tc);
}

void UISystem::DrawComboVisual(const Rect &r, std::string_view label, bool open, bool selected,
                               bool hover, float scale, bool is_item, float radius,
                               float hover_t, float alpha) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float ht = std::max(hover_t, hover ? 1.0f : 0.0f);
    Color bg = is_item ? (selected ? ThemeColor("accent")
                                   : MixColor(ThemeColor("panel"), ThemeColor("hover"), ht))
                       : MixColor(ThemeColor("field"), ThemeColor("hover"), ht);
    Color brd = ThemeColor("border");
    Color tc = ThemeColor("text");
    bg.a *= alpha;
    brd.a *= alpha;
    tc.a *= alpha;
    const float border = ThemeMetric("border");
    if (radius > 0.5f) {
        DrawRoundedQuad({r.x - border, r.y - border, r.width + 2 * border, r.height + 2 * border},
                        radius + border, brd);
        DrawRoundedQuad(r, radius, bg);
    } else {
        renderer_->DrawQuad(
            MapRect({r.x - border, r.y - border, r.width + 2 * border, r.height + 2 * border}),
            brd);
        renderer_->DrawQuad(MapRect(r), bg);
    }
    const std::string txt =
        is_item ? std::string(label) : (std::string(label) + (open ? " ▲" : " ▼"));
    const std::string fitted = Ellipsize(txt, std::max(0.0f, r.width - 10.0f), scale);
    const float th = renderer_->GetFont().LineHeight() * scale;
    renderer_->DrawTextEx(fitted, (r.x + 6.0f) * scale_ + scale_offset_.x,
                          (r.y + (r.height - th) * 0.5f) * scale_ + scale_offset_.y,
                          scale * scale_, 0, 0, tc);
}

void UISystem::DrawScrollbarVisual(const Rect &vp, float content_h, float offset, bool hover,
                                   bool dragging, float radius, float alpha) {
    if (!renderer_ || content_h <= vp.height || vp.height <= 0.0f) {
        return;
    }
    const float bar_w = 10.0f;
    const float max_scroll = std::max(1e-4f, content_h - vp.height);
    const Rect bar{vp.x + vp.width - bar_w, vp.y, bar_w, vp.height};
    const float thumb_h = std::min(vp.height, std::max(24.0f, vp.height * (vp.height / content_h)));
    const float track = vp.height - thumb_h;
    const float drawn = vp.y + std::clamp(offset / max_scroll, 0.0f, 1.0f) * track;
    Color trk = ThemeColor("track");
    Color th = dragging ? ThemeColor("accent") : (hover ? ThemeColor("hover") : kScrollThumb);
    trk.a *= alpha;
    th.a *= alpha;
    const float r = radius > 0.5f ? std::min(radius, bar_w * 0.5f) : 0.0f;
    DrawRoundedQuad(bar, r, trk);
    DrawRoundedQuad({bar.x + 1.0f, drawn, bar_w - 2.0f, thumb_h}, r, th);
}

void UISystem::ResolveDeepestWheel(int root_idx) {
    if (!input_ || !camera_ || !renderer_ ||
        (input_->MouseWheel() == 0.0 && input_->MouseWheelX() == 0.0)) {
        return;
    }
    const Vec2 mouse = RawMouse();
    int best_idx = -1;
    bool best_is_listview = false;
    std::function<void(int)> walk = [&](int idx) {
        const auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
        if (node.kind == AutoNode::Kind::Scroll || node.kind == AutoNode::Kind::ListViewSlot) {
            const Rect vp = (node.kind == AutoNode::Kind::Scroll) ? node.content_rect : node.rect;
            const Rect vis = IntersectClip(vp, node.clip_rect);
            const bool eligible = !node.disabled &&
                                  (modal_.empty() || modal_ == node.modal_id) &&
                                  (popup_.empty() || popup_ == node.popup_id) &&
                                  vis.Contains(mouse);
            if (eligible) {
                if (node.kind == AutoNode::Kind::ListViewSlot) {
                    best_idx = idx;
                    best_is_listview = true;
                } else if (node.content_height > node.content_rect.height ||
                           ((node.scroll_dir == UIScrollDir::Horizontal || node.scroll_dir == UIScrollDir::Both) &&
                            node.content_width > node.content_rect.width)) {
                    best_idx = idx;
                    best_is_listview = false;
                }
            }
        }
        const auto children = node.children;
        for (const int cid : children) {
            walk(cid);
        }
    };
    walk(root_idx);
    if (best_idx < 0 || best_is_listview) {
        return;
    }
    auto &wn = auto_nodes_[static_cast<std::size_t>(best_idx)];
    auto &saved = scroll_states_[wn.key];
    const bool shift = input_->KeyDown("left_shift") || input_->KeyDown("right_shift");
    const bool horiz_only = wn.scroll_dir == UIScrollDir::Horizontal;
    const bool allow_h = (wn.scroll_dir == UIScrollDir::Horizontal || wn.scroll_dir == UIScrollDir::Both);
    const bool allow_v = (wn.scroll_dir == UIScrollDir::Vertical || wn.scroll_dir == UIScrollDir::Both);
    bool changed = false;

    const double wy = input_->MouseWheel();
    const double wx = input_->MouseWheelX();
    if (allow_h && (wx != 0.0 || ((horiz_only || shift) && wy != 0.0))) {
        const float max_x = std::max(0.0f, wn.content_width - wn.content_rect.width);
        const double d_x = (wx != 0.0) ? wx : wy;
        const float next_x = std::clamp(saved.offset_x - static_cast<float>(d_x) * 40.0f, 0.0f, max_x);
        if (std::abs(next_x - saved.offset_x) > 1e-6f) {
            saved.offset_x = next_x;
            saved.has_target_x = false;
            wn.scroll_offset_x = next_x;
            changed = true;
        }
    }
    if (allow_v && wy != 0.0 && !horiz_only && !shift) {
        const float max_scroll = std::max(0.0f, wn.content_height - wn.content_rect.height);
        const float next_off = std::clamp(saved.offset - static_cast<float>(wy) * 40.0f, 0.0f, max_scroll);
        if (std::abs(next_off - saved.offset) > 1e-6f) {
            saved.offset = next_off;
            saved.has_target = false;
            wn.scroll_offset = next_off;
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    const float sb = ThemeMetric("scrollbar_width");
    const bool want_h = (wn.scroll_dir == UIScrollDir::Horizontal || wn.scroll_dir == UIScrollDir::Both);
    const float content_w = std::max(0.0f, wn.content_rect.width - (wn.scroll_dir == UIScrollDir::Horizontal ? 0.0f : sb));
    float y = wn.content_rect.y - wn.scroll_offset;
    const auto children = wn.children;
    for (const int cid : children) {
        const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
        float cw = c.meas_w;
        if (!want_h) {
            if (c.width_spec.kind == UISizeKind::Percent) {
                cw = content_w * c.width_spec.value;
            } else if (c.width_spec.kind == UISizeKind::Grow || wn.cross_align == UIAxisAlign::Stretch) {
                cw = content_w;
            }
            cw = std::clamp(cw, std::min(content_w, c.limits.min_w), std::min(content_w, c.limits.max_w));
        }
        const float cx = (wn.content_rect.x - wn.scroll_offset_x) +
                         (want_h ? 0.0f : AlignOffset(wn.cross_align, content_w, cw));
        ArrangeAutoNode(cid, {cx, y, cw, c.meas_h}, wn.clip_rect, true, wn.root_box_id, wn.row_id,
                        wn.col_index, wn.clip_radius, wn.clip_rrect);
        y += c.meas_h + wn.gap;
    }
}

void UISystem::ResolveDeferredAutoWidgets() {
    focus_ring_.clear();
    pointer_owner_.clear();
    if (!camera_ || !renderer_ || !input_) {
        return;
    }
    const Vec2 mouse = RawMouse();
    std::vector<bool> eligible;
    eligible.reserve(deferred_auto_nodes_.size());
    for (const int idx : deferred_auto_nodes_) {
        const auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
        const Rect vis = IntersectClip(node.rect, node.clip_rect);
        const bool ok = !dismissed_overlay_ && !node.disabled &&
                        (modal_.empty() || modal_ == node.modal_id) &&
                        (popup_.empty() || popup_ == node.popup_id) &&
                        vis.width > 0.0f && vis.height > 0.0f;
        eligible.push_back(ok);
        if (!ok) {
            continue;
        }
        if (node.kind != AutoNode::Kind::Window) {
            const bool is_text = node.kind == AutoNode::Kind::TextInput ||
                                 node.kind == AutoNode::Kind::TextArea ||
                                 node.kind == AutoNode::Kind::TextPassword;
            const bool adjustable = node.kind == AutoNode::Kind::Slider ||
                                    node.kind == AutoNode::Kind::Splitter;
            if (std::none_of(focus_ring_.begin(), focus_ring_.end(),
                             [&](const FocusEntry &e) { return e.key == node.key; })) {
                focus_ring_.push_back({node.key, node.rect, is_text, adjustable, node.focus_scope_id});
            }
        }
        if (vis.Contains(mouse)) {
            pointer_owner_ = node.key;
        }
    }
    for (const auto &rl : deferred_rich_links_) {
        if (!dismissed_overlay_ && rl.vis.width > 0.0f && rl.vis.height > 0.0f) {
            if (std::none_of(focus_ring_.begin(), focus_ring_.end(),
                             [&](const FocusEntry &e) { return e.key == rl.key; })) {
                const std::string sc_id =
                    (rl.node_idx >= 0 && static_cast<std::size_t>(rl.node_idx) < auto_nodes_.size())
                        ? auto_nodes_[static_cast<std::size_t>(rl.node_idx)].focus_scope_id
                        : std::string{};
                focus_ring_.push_back({rl.key, rl.rr, false, false, sc_id});
            }
            if (rl.vis.Contains(mouse)) {
                pointer_owner_ = rl.key;
            }
        }
    }
    if (input_->MousePressed(0)) {
        pointer_active_ = pointer_owner_;
    }
    if (!pointer_active_.empty()) {
        bool still_active = false;
        for (std::size_t i = 0; i < deferred_auto_nodes_.size(); ++i) {
            if (eligible[i] &&
                auto_nodes_[static_cast<std::size_t>(deferred_auto_nodes_[i])].key == pointer_active_) {
                still_active = true;
                break;
            }
        }
        if (!still_active) {
            for (const auto &rl : deferred_rich_links_) {
                if (rl.key == pointer_active_) {
                    still_active = true;
                    break;
                }
            }
        }
        if (!still_active) {
            if (auto *st = Find(pointer_active_)) {
                st->pressed = false;
                st->dragging = false;
                st->selecting = false;
            }
            pointer_active_.clear();
        }
    }

    NavigateCurrent();
    if (popup_just_opened_) {
        nav_activate_ = false;
    }

    const bool saved_modal = in_modal_, saved_popup = in_popup_, saved_resolving = resolving_;
    resolving_ = true;
    for (std::size_t i = 0; i < deferred_auto_nodes_.size(); ++i) {
        const auto &node = auto_nodes_[static_cast<std::size_t>(deferred_auto_nodes_[i])];
        resolving_key_ = node.key;
        if (node.key != pointer_active_) {
            if (auto *st = Find(node.key)) {
                st->pressed = false;
                st->dragging = false;
                st->selecting = false;
            }
        }
        in_modal_ = !node.modal_id.empty();
        in_popup_ = !node.popup_id.empty();
        resolving_allowed_ = eligible[i];
        DisabledBegin(node.disabled);
        const Rect &r = node.rect;
        const Rect vis = IntersectClip(r, node.clip_rect);
        const bool allow = eligible[i];
        const bool mouse_hit = allow && vis.Contains(mouse) && CanInteract() && MouseVisible();

        switch (node.kind) {
        case AutoNode::Kind::Button:
        case AutoNode::Kind::Checkbox:
        case AutoNode::Kind::RadioButton:
        case AutoNode::Kind::Tab:
        case AutoNode::Kind::Tree:
        case AutoNode::Kind::ComboBegin:
        case AutoNode::Kind::ComboItem:
        case AutoNode::Kind::Menu:
        case AutoNode::Kind::MenuItem:
        case AutoNode::Kind::ImageButton: {
            if (!allow) {
                frame_results_[node.key] = 0.0f;
                if (auto *st = Find(node.key)) {
                    st->hover = false;
                    st->pressed = false;
                }
                break;
            }
            if (mouse_hit && input_->MousePressed(0)) {
                nav_focus_ = node.key;
                focused_.clear();
            }
            WidgetState &st = widgets_[node.key];
            bool clicked = false;
            if (node.kind == AutoNode::Kind::ComboBegin) {
                const bool down = CanInteract() && input_->MouseDown(0);
                const bool rel = input_->MouseReleased(0);
                if (mouse_hit && input_->MousePressed(0)) {
                    st.pressed = true;
                }
                if (rel && st.pressed) {
                    clicked = true;
                    ++click_count_;
                }
                if (!down) {
                    st.pressed = false;
                }
                st.hover = mouse_hit;
                if (mouse_hit || st.pressed) {
                    pointer_over_ = true;
                }
            } else {
                clicked = TrackPressWidget(st, mouse_hit);
            }
            const bool activate = CanInteract() && nav_focus_ == node.key && nav_activate_;
            const bool fired = clicked || activate;
            frame_results_[node.key] = fired ? 1.0f : 0.0f;
            if (node.kind == AutoNode::Kind::Menu) {
                const std::size_t depth = static_cast<std::size_t>(std::max(0, node.col_span));
                const bool is_open =
                    depth < open_menu_path_.size() && open_menu_path_[depth] == node.key;
                if (fired) {
                    if (is_open && depth == 0) {
                        open_menu_path_.clear();
                    } else {
                        open_menu_path_.resize(depth + 1);
                        open_menu_path_[depth] = node.key;
                    }
                } else if (mouse_hit && !is_open &&
                           ((depth == 0 && !open_menu_path_.empty()) || depth > 0)) {
                    open_menu_path_.resize(depth + 1);
                    open_menu_path_[depth] = node.key;
                }
            }
            if (fired) {
                activated_widget_ = node.key;
                if (node.kind == AutoNode::Kind::MenuItem) {
                    open_menu_path_.clear();
                }
                if (!node.popup_id.empty()) {
                    const std::string prefix = node.popup_id + "/";
                    if (node.key.rfind(prefix, 0) == 0) {
                        frame_results_[node.popup_id] =
                            static_cast<float>(std::atoi(node.key.substr(prefix.size()).c_str()));
                        popup_.clear();
                        nav_focus_.clear();
                    }
                }
            }
            break;
        }
        case AutoNode::Kind::Slider: {
            float next = node.val;
            if (allow && node.max_val > node.min_val) {
                if (CanInteract() && nav_focus_ == node.key) {
                    if (input_->KeyPressed("left") || input_->GamepadButtonPressed(0, "dpad_left")) {
                        next -= (node.max_val - node.min_val) * 0.05f;
                    }
                    if (input_->KeyPressed("right") || input_->GamepadButtonPressed(0, "dpad_right")) {
                        next += (node.max_val - node.min_val) * 0.05f;
                    }
                    next += static_cast<float>(nav_adjust_step_) * (node.max_val - node.min_val) * 0.05f;
                    next = std::clamp(next, node.min_val, node.max_val);
                }
                WidgetState &st = widgets_[node.key];
                const float thumb_w = r.height * 0.6f;
                const float range_px = std::max(1.0f, r.width - thumb_w);
                const float t = (next - node.min_val) / (node.max_val - node.min_val);
                const Rect thumb{r.x + t * range_px, r.y, thumb_w, r.height};
                if (input_->MousePressed(0) && mouse_hit) {
                    ++click_count_;
                    st.dragging = true;
                    st.pressed = true;
                    nav_focus_ = node.key;
                    focused_.clear();
                    if (thumb.Contains(mouse)) {
                        st.grab = mouse.x - thumb.x;
                    } else {
                        next = std::clamp(node.min_val + (mouse.x - r.x - thumb_w * 0.5f) / range_px *
                                                             (node.max_val - node.min_val),
                                          node.min_val, node.max_val);
                        st.grab = thumb_w * 0.5f;
                    }
                }
                if (st.dragging && CanInteract() && input_->MouseDown(0)) {
                    next = std::clamp(node.min_val + (mouse.x - r.x - st.grab) / range_px *
                                                         (node.max_val - node.min_val),
                                      node.min_val, node.max_val);
                }
                if (input_->MouseReleased(0) || !input_->MouseDown(0)) {
                    st.dragging = false;
                    st.pressed = false;
                }
                if (mouse_hit || st.dragging) {
                    pointer_over_ = true;
                }
                st.hover = mouse_hit;
            }
            frame_results_[node.key] = next;
            break;
        }
        case AutoNode::Kind::TextInput:
        case AutoNode::Kind::TextArea:
        case AutoNode::Kind::TextPassword: {
            const bool multi = (node.kind == AutoNode::Kind::TextArea);
            const bool pass = (node.kind == AutoNode::Kind::TextPassword);
            frame_results_[node.key] = EditText(node.key, r, node.max_chars, multi, pass, false, true,
                                                node.placeholder, node.radius, node.alpha_mul);
            break;
        }
        case AutoNode::Kind::Splitter: {
            WidgetState &st = widgets_[node.key];
            if (allow) {
                const float eff_min = std::clamp(node.min_val, 0.0f, r.width * 0.5f);
                st.grab = std::clamp(st.grab, eff_min, std::max(eff_min, r.width - eff_min));
                if (CanInteract() && nav_focus_ == node.key) {
                    st.grab = std::clamp(st.grab + nav_adjust_step_ * 8.0f, eff_min, r.width - eff_min);
                }
                const Rect divider{r.x + st.grab - 3.0f, r.y, 6.0f, r.height};
                const Rect dvis = IntersectClip(divider, node.clip_rect);
                if (CanInteract()) {
                    if (MouseVisible() && dvis.width > 0.0f && dvis.Contains(mouse) && input_->MousePressed(0)) {
                        st.dragging = true;
                        nav_focus_ = node.key;
                    }
                    if (st.dragging && input_->MouseDown(0)) {
                        st.grab = std::clamp(mouse.x - r.x, eff_min, std::max(eff_min, r.width - eff_min));
                    }
                    if (!input_->MouseDown(0)) {
                        st.dragging = false;
                    }
                } else {
                    st.dragging = false;
                }
            }
            frame_results_[node.key] = st.grab;
            break;
        }
        case AutoNode::Kind::Window: {
            WidgetState &st = widgets_[node.key];
            bool dragging = false;
            if (allow && CanInteract()) {
                if (input_->MouseDown(0)) {
                    if (input_->MousePressed(0) && mouse_hit) {
                        st.dragging = true;
                    }
                    dragging = st.dragging && st.pressed;
                    st.pressed = st.dragging;
                } else {
                    st.dragging = false;
                    st.pressed = false;
                }
                st.hover = mouse_hit;
            }
            frame_results_[node.key] = dragging ? 1.0f : 0.0f;
            break;
        }
        default:
            break;
        }
        DisabledEnd();
    }
    for (const auto &rl : deferred_rich_links_) {
        if (dismissed_overlay_) {
            continue;
        }
        resolving_key_ = rl.key;
        auto &st = widgets_[rl.state_key];
        const bool hover = rl.vis.width > 0.0f && rl.vis.Contains(mouse) && MouseVisible();
        if (hover && input_->MousePressed(0)) {
            nav_focus_ = rl.key;
        }
        if ((nav_focus_ == rl.key && nav_activate_) ||
            (hover && pointer_active_ == rl.key && st.pressed && input_->MouseReleased(0))) {
            clicked_link_ = rl.link;
        }
        st.pressed = hover && input_->MouseDown(0);
    }
    if (!open_menu_path_.empty()) {
        if (input_->KeyPressed("escape")) {
            open_menu_path_.clear();
        } else if (input_->MousePressed(0)) {
            bool over_menu = false;
            for (const auto &n : auto_nodes_) {
                if ((n.kind == AutoNode::Kind::Menu || n.kind == AutoNode::Kind::MenuItem) &&
                    n.rect.Contains(mouse)) {
                    over_menu = true;
                    break;
                }
            }
            if (!over_menu) {
                open_menu_path_.clear();
            }
        }
    }
    if (!input_->MouseDown(0)) {
        pointer_active_.clear();
    }
    resolving_key_.clear();
    in_modal_ = saved_modal;
    in_popup_ = saved_popup;
    resolving_allowed_ = true;
    resolving_ = saved_resolving;
}

} // namespace fake2d
