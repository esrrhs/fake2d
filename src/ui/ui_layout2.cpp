#include "fake2d/ui.h"

#include <algorithm>
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

void UISystem::ApplyClipRectDirect(const Rect &clip_rect) {
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
    renderer_->SetClipRect(MapRect(r));
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
    node.align_group_ids = align_stack_;

    node.limits = next_limits_;
    next_limits_ = {};

    const AutoLayoutMod mod = next_auto_mod_;
    next_auto_mod_ = {};

    if (!auto_stack_.empty()) {
        const int p_idx = auto_stack_.back();
        node.parent = p_idx;
        node.root_box_id = auto_nodes_[static_cast<std::size_t>(p_idx)].root_box_id;
        node.box_depth = auto_nodes_[static_cast<std::size_t>(p_idx)].box_depth + 1;
        node.in_scroll = auto_nodes_[static_cast<std::size_t>(p_idx)].in_scroll;
        node.est_avail_w = auto_nodes_[static_cast<std::size_t>(p_idx)].est_avail_w;
    } else {
        node.root_box_id = node.box_id;
        node.box_depth = 1;
        node.est_avail_w = LogicalWidth();
    }

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
    }
    if (mod.has_main) {
        node.main_align = mod.main;
    }
    if (mod.has_pad) {
        node.pad_l = mod.pad_l;
        node.pad_t = mod.pad_t;
        node.pad_r = mod.pad_r;
        node.pad_b = mod.pad_b;
    }
    if (node.kind == AutoNode::Kind::Root || node.kind == AutoNode::Kind::AnchorRoot ||
        node.kind == AutoNode::Kind::VStack || node.kind == AutoNode::Kind::HStack ||
        node.kind == AutoNode::Kind::Card || node.kind == AutoNode::Kind::Table ||
        node.kind == AutoNode::Kind::Flow || node.kind == AutoNode::Kind::CustomBox) {
        node.clip_children = true;
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

    float rw = root.meas_w;
    if (root.width_spec.kind == UISizeKind::Fixed) {
        rw = root.width_spec.value;
    } else if (root.width_spec.kind == UISizeKind::Percent) {
        rw = vw * root.width_spec.value;
    } else if (root.width_spec.kind == UISizeKind::Grow) {
        rw = vw;
    }
    rw = std::clamp(rw, root.limits.min_w, root.limits.max_w);

    float rh = root.meas_h;
    if (root.height_spec.kind == UISizeKind::Fixed) {
        rh = root.height_spec.value;
    } else if (root.height_spec.kind == UISizeKind::Percent) {
        rh = vh * root.height_spec.value;
    } else if (root.height_spec.kind == UISizeKind::Grow) {
        rh = vh;
    }
    rh = std::clamp(rh, root.limits.min_h, root.limits.max_h);

    float rx = root.use_screen_origin ? root.anchor_x : (ox + root.anchor_x);
    float ry = root.use_screen_origin ? root.anchor_y : (oy + root.anchor_y);
    if (root.kind == AutoNode::Kind::AnchorRoot) {
        if (root.h_align == "stretch") {
            rx = ox + root.inset_x;
            rw = std::max(0.0f, vw - 2.0f * root.inset_x);
        } else if (root.h_align == "right") {
            rx = ox + vw - rw - root.inset_x;
        } else if (root.h_align == "center") {
            rx = ox + (vw - rw) * 0.5f + root.inset_x;
        } else {
            rx = ox + root.inset_x;
        }

        if (root.v_align == "stretch") {
            ry = oy + root.inset_y;
            rh = std::max(0.0f, vh - 2.0f * root.inset_y);
        } else if (root.v_align == "bottom") {
            ry = oy + vh - rh - root.inset_y;
        } else if (root.v_align == "center") {
            ry = oy + (vh - rh) * 0.5f + root.inset_y;
        } else {
            ry = oy + root.inset_y;
        }
    }

    const Rect root_rect{rx, ry, rw, rh};
    const Rect effective_clip = root.has_clip_override ? IntersectClip(vp_clip, root.clip_override) : vp_clip;
    ArrangeAutoNode(closed_idx, root_rect, effective_clip, root.in_scroll, root.box_id, -1, -1);
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
    CommitAutoNode(closed_idx, root.in_scroll ? root.rect : vp_clip);
    in_auto_commit_ = prev_commit;
    ApplyClipRectDirect({0.0f, 0.0f, 1e9f, 1e9f});
    last_rect_ = root.rect;
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
    node.key = std::string(key);
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
    node.key = std::string(key);
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
    node.key = std::string(key);
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

    return HitTestSolvedKey(key, false, false, true);
}

float UISystem::AutoSlider(std::string_view key, UISize h_spec, float value, float min_v, float max_v) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Slider);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = std::string(key);
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
                FocusWidget(key, r, false, true);
                if (CanInteract() && nav_focus_ == key) {
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
    node.key = std::string(key);
    node.checked = checked;
    node.text = std::string(label);
    node.scale = sc;
    if (!had_h) {
        node.height_spec = sz_spec;
    }
    return HitTestSolvedKey(key, false, false, true);
}

bool UISystem::AutoTextInput(std::string_view key, UISize h_spec, std::size_t max_chars) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::TextInput);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = std::string(key);
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = h_spec;
    }
    node.max_chars = max_chars;
    return !recording_ && input_ && CanInteract() && focused_ == key && input_->KeyPressed("enter");
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
    node.key = std::string(key);
    node.text = std::string(label);
    node.checked = active;
    node.scale = sc;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(key, false, false, true);
}

bool UISystem::AutoTree(std::string_view key, UISize h_spec, std::string_view label, bool open,
                        float scale) {
    const float sc = AutoWidgetTextScale(scale);
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Tree);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = std::string(key);
    node.text = std::string(label);
    node.checked = open;
    node.scale = sc;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(key, false, false, true);
}

bool UISystem::AutoCombo(std::string_view key, UISize h_spec, std::string_view label,
                         bool open_or_sel, float scale, bool is_item) {
    const float sc = AutoWidgetTextScale(scale);
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(is_item ? AutoNode::Kind::ComboItem : AutoNode::Kind::ComboBegin);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = std::string(key);
    node.text = std::string(label);
    node.checked = open_or_sel;
    node.scale = sc;
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(key, false, false, true, !is_item);
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
    node.key = std::string(key);
    node.texture = texture;
    node.tex_src = src;
    node.text = std::string(label);
    node.scale = ThemeMetric("font_scale");
    if (!had_h) {
        node.height_spec = h_spec;
    }
    return HitTestSolvedKey(key, false, false, true);
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
    return clicked_link_;
}

float UISystem::AutoSplitter(std::string_view key, UISize h_spec, float initial, float minimum) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::Splitter);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = std::string(key);
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
            if (CanInteract() && nav_focus_ == key) {
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
                if (nav_focus_ == key) {
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
    node.key = std::string(key);
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
    node.key = std::string(key);
    node.max_chars = max_chars;
    node.scale = sc;
    if (!had_w) {
        node.width_spec = UISize::Grow(1.0f);
    }
    if (!had_h) {
        const float def_h = FontLineHeightPx(sc) * 4.0f + 2.0f * ThemeMetric("input_pad_y");
        node.height_spec = (h_spec.kind == UISizeKind::Auto) ? UISize::Fixed(def_h) : h_spec;
    }
    return false;
}

bool UISystem::AutoTextPassword(std::string_view key, UISize h_spec, std::size_t max_chars) {
    const float sc = ThemeMetric("font_scale");
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const int idx = AllocAutoNode(AutoNode::Kind::TextPassword);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.key = std::string(key);
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

void UISystem::DrawRichTextSolved(const AutoNode &node, const Rect &r, const Rect &clip) {
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
    ApplyClipRectDirect(text_clip);

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
            } else {
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
                        focus_ring_.push_back({key, rr, false, false});
                    }
                }
                if (hover && input_ && input_->MousePressed(0)) {
                    nav_focus_ = key;
                }
            }
            FocusIndicator(key, rr);
            Color uc = color;
            if (node.disabled) uc.a *= 0.4f;
            renderer_->DrawQuad(MapRect({rr.x, rr.y + lh - 2.0f, rr.width, 1.0f}), uc);
        }
        if (!image_run) {
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
            if (icon.texture && icon.texture->IsValid()) {
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
    const float pad_x = node.pad_l + node.pad_r;
    const float pad_y = node.pad_t + node.pad_b;

    auto apply_explicit_size = [&](float w, float h) {
        if (node.width_spec.kind == UISizeKind::Fixed) {
            w = node.width_spec.value;
        } else if (node.width_spec.kind == UISizeKind::Percent && avail_w < 1e7f) {
            w = avail_w * node.width_spec.value;
        }
        if (node.height_spec.kind == UISizeKind::Fixed) {
            h = node.height_spec.value;
        } else if (node.height_spec.kind == UISizeKind::Percent && avail_h < 1e7f) {
            h = avail_h * node.height_spec.value;
        }
        node.meas_w = std::clamp(w, node.limits.min_w, node.limits.max_w);
        node.meas_h = std::clamp(h, node.limits.min_h, node.limits.max_h);
    };

    const float target_w =
        node.width_spec.kind == UISizeKind::Fixed
            ? node.width_spec.value
            : (node.width_spec.kind == UISizeKind::Percent && avail_w < 1e7f
                   ? avail_w * node.width_spec.value
                   : avail_w);
    const float target_h =
        node.height_spec.kind == UISizeKind::Fixed
            ? node.height_spec.value
            : (node.height_spec.kind == UISizeKind::Percent && avail_h < 1e7f
                   ? avail_h * node.height_spec.value
                   : avail_h);
    const float inner_avail_w = std::max(0.0f, std::min(target_w, node.limits.max_w) - pad_x);
    const float inner_avail_h = std::max(0.0f, std::min(target_h, node.limits.max_h) - pad_y);

    switch (node.kind) {
    case AutoNode::Kind::Text: {
        const float sc = AutoWidgetTextScale(node.scale);
        const float line_h = FontLineHeightPx(sc);
        const float nat_w = renderer_ ? renderer_->MeasureText(node.text, sc) : 0.0f;
        if (node.overflow == UITextOverflow::Wrap) {
            const float wrap_w = (inner_avail_w < 1e7f) ? inner_avail_w : nat_w;
            const float w = (wrap_w > 0.0f) ? std::min(nat_w, wrap_w) : 0.0f;
            const float h = (wrap_w > 0.0f && !node.text.empty() && renderer_)
                                ? renderer_->GetFont().MeasureTextEx(node.text, sc, wrap_w)
                                : 0.0f;
            apply_explicit_size(w + pad_x, h + pad_y);
        } else {
            const float h = node.text.empty() ? 0.0f : line_h;
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
        apply_explicit_size(tw + 2.0f * px + 18.0f, lh + 2.0f * py);
        return;
    }
    case AutoNode::Kind::Checkbox:
    case AutoNode::Kind::RadioButton: {
        const float sc = AutoWidgetTextScale(node.scale);
        const float lh = FontLineHeightPx(sc);
        const float sz = node.height_spec.kind == UISizeKind::Fixed ? node.height_spec.value : std::max(14.0f, lh);
        const float tw = (renderer_ && !node.text.empty()) ? (8.0f + renderer_->MeasureText(node.text, sc)) : 0.0f;
        apply_explicit_size(sz + tw, sz);
        return;
    }
    case AutoNode::Kind::ProgressBar: {
        apply_explicit_size(48.0f, ThemeMetric("bar_height"));
        return;
    }
    case AutoNode::Kind::Slider: {
        apply_explicit_size(80.0f, ThemeMetric("slider_height"));
        return;
    }
    case AutoNode::Kind::TextInput: {
        const float sc = ThemeMetric("font_scale");
        const float lh = FontLineHeightPx(sc);
        apply_explicit_size(120.0f, lh + 2.0f * ThemeMetric("input_pad_y"));
        return;
    }
    case AutoNode::Kind::TextArea: {
        const float sc = ThemeMetric("font_scale");
        const float lh = FontLineHeightPx(sc);
        apply_explicit_size(120.0f, lh * 4.0f + 2.0f * ThemeMetric("input_pad_y"));
        return;
    }
    case AutoNode::Kind::TextPassword: {
        const float sc = ThemeMetric("font_scale");
        const float lh = FontLineHeightPx(sc);
        apply_explicit_size(120.0f, lh + 2.0f * ThemeMetric("input_pad_y"));
        return;
    }
    case AutoNode::Kind::Splitter: {
        apply_explicit_size(120.0f, ThemeMetric("slider_height"));
        return;
    }
    case AutoNode::Kind::Window: {
        const float bar_h = std::max(22.0f, FontLineHeightPx(node.scale) + 10.0f);
        const float tw = renderer_ ? renderer_->MeasureText(node.text, node.scale) + 16.0f : 120.0f;
        apply_explicit_size(tw, bar_h);
        return;
    }
    case AutoNode::Kind::RichText: {
        const float wrap_w = inner_avail_w < 1e7f ? inner_avail_w : 400.0f;
        const float h = MeasureRichTextHeight(node.text, node.scale, wrap_w);
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
    case AutoNode::Kind::Root:
    case AutoNode::Kind::AnchorRoot:
    case AutoNode::Kind::VStack:
    case AutoNode::Kind::TableCell:
    case AutoNode::Kind::Card:
    case AutoNode::Kind::CustomBox: {
        float max_w = 0.0f;
        float sum_h = 0.0f;
        float cmd_w = 0.0f;
        float cmd_h = 0.0f;
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
            max_w = std::max(max_w, c.meas_w);
            sum_h += c.meas_h;
            ++count;
        }
        const float gaps = count > 1 ? node.gap * static_cast<float>(count - 1) : 0.0f;
        apply_explicit_size(std::max(max_w, cmd_w) + pad_x, std::max(sum_h + gaps, cmd_h) + pad_y);
        return;
    }
    case AutoNode::Kind::HStack: {
        const auto children = node.children;
        const std::size_t n = children.size();
        const float gaps = n > 1 ? node.gap * static_cast<float>(n - 1) : 0.0f;
        float fixed_w = 0.0f;
        float grow_sum = 0.0f;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_avail_w, inner_avail_h);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.width_spec.kind == UISizeKind::Grow) {
                grow_sum += c.width_spec.value;
            } else if (c.width_spec.kind == UISizeKind::Percent && inner_avail_w < 1e7f) {
                fixed_w += std::clamp(inner_avail_w * c.width_spec.value, c.limits.min_w, c.limits.max_w);
            } else {
                fixed_w += c.meas_w;
            }
        }
        // Re-measure Grow/Percent children with their actual allocated column widths so wrapped text heights match!
        const float rem_w = std::max(0.0f, (inner_avail_w < 1e7f ? inner_avail_w : fixed_w) - fixed_w - gaps);
        float max_h = 0.0f;
        float total_w = 0.0f;
        for (const int cid : children) {
            const auto spec = auto_nodes_[static_cast<std::size_t>(cid)].width_spec;
            if (spec.kind == UISizeKind::Grow && grow_sum > 0.0f) {
                const float cw = rem_w * (spec.value / grow_sum);
                MeasureAutoNode(cid, cw, inner_avail_h);
                const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
                total_w += std::max(cw, c.meas_w);
                max_h = std::max(max_h, c.meas_h);
            } else {
                const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
                total_w += c.meas_w;
                max_h = std::max(max_h, c.meas_h);
            }
        }
        apply_explicit_size(total_w + gaps + pad_x, max_h + pad_y);
        return;
    }
    case AutoNode::Kind::Table: {
        const auto rows = node.children;
        const std::size_t ncols = node.col_specs.size();
        const float col_gaps = ncols > 1 ? node.gap * static_cast<float>(ncols - 1) : 0.0f;
        std::vector<float> col_w(ncols, 0.0f);

        // Pass 1a: Measure Auto / Fixed / Percent columns across ALL rows
        for (std::size_t c = 0; c < ncols; ++c) {
            const auto &cs = node.col_specs[c];
            if (cs.kind == UISizeKind::Fixed) {
                col_w[c] = cs.value;
            } else if (cs.kind == UISizeKind::Percent && inner_avail_w < 1e7f) {
                col_w[c] = inner_avail_w * cs.value;
            } else if (cs.kind == UISizeKind::Auto) {
                for (const int rid : rows) {
                    const auto &r_children = auto_nodes_[static_cast<std::size_t>(rid)].children;
                    if (c < r_children.size()) {
                        MeasureAutoNode(r_children[c], inner_avail_w, kUnbounded);
                        col_w[c] = std::max(col_w[c], auto_nodes_[static_cast<std::size_t>(r_children[c])].meas_w);
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

        // Pass 1c: Re-measure each row's cells at exact column widths to get exact wrapped heights
        float sum_row_h = 0.0f;
        for (const int rid : rows) {
            const auto r_children = auto_nodes_[static_cast<std::size_t>(rid)].children;
            float row_h = 0.0f;
            for (std::size_t c = 0; c < ncols && c < r_children.size(); ++c) {
                MeasureAutoNode(r_children[c], col_w[c], kUnbounded);
                row_h = std::max(row_h, auto_nodes_[static_cast<std::size_t>(r_children[c])].meas_h);
            }
            auto &rn = auto_nodes_[static_cast<std::size_t>(rid)];
            rn.meas_w = inner_avail_w;
            rn.meas_h = std::clamp(row_h + rn.pad_t + rn.pad_b, rn.limits.min_h, rn.limits.max_h);
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
        const float inner_w = std::max(0.0f, inner_avail_w - sb);
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
        const float content_h = sum_h + gaps + pad_y;
        auto_nodes_[static_cast<std::size_t>(idx)].content_height = content_h;
        // For Auto-height scrolls (e.g., dropdown lists with Constrain max_h), report content_h;
        // for Grow scrolls, report min_h (0) so parent VStack shrinks-free and gives remaining space in Arrange.
        const float pref_h = (node.height_spec.kind == UISizeKind::Grow) ? 0.0f : content_h;
        apply_explicit_size(max_w + sb + pad_x, pref_h);
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// Pass 2: Top-Down Arrange
// ---------------------------------------------------------------------------

void UISystem::ArrangeAutoNode(int idx, const Rect &final_rect, const Rect &parent_clip,
                               bool in_scroll, std::uint32_t root_id, int row_id, int col_idx) {
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.rect = final_rect;
    node.content_rect = {
        final_rect.x + node.pad_l,
        final_rect.y + node.pad_t,
        std::max(0.0f, final_rect.width - node.pad_l - node.pad_r),
        std::max(0.0f, final_rect.height - node.pad_t - node.pad_b),
    };
    node.clip_rect = node.clip_children ? IntersectClip(parent_clip, node.rect) : parent_clip;
    node.root_box_id = root_id;
    node.in_scroll = in_scroll || node.kind == AutoNode::Kind::Scroll;
    if (node.row_id < 0 && row_id >= 0) {
        node.row_id = row_id;
    }
    if (node.col_index < 0 && col_idx >= 0) {
        node.col_index = col_idx;
    }

    const Rect cr = node.content_rect;
    const Rect clip = node.clip_rect;
    const int eff_row_id = node.row_id;
    const int eff_col_idx = node.col_index;
    const auto children = node.children;
    const std::size_t n = children.size();

    auto resolve_cross = [](UIAxisAlign align, const UISize &spec, float meas, float avail, float min_v,
                            float max_v) -> float {
        float v = meas;
        if (spec.kind == UISizeKind::Fixed) {
            v = spec.value;
        } else if (spec.kind == UISizeKind::Percent) {
            v = avail * spec.value;
        } else if (spec.kind == UISizeKind::Grow || align == UIAxisAlign::Stretch) {
            v = avail;
        }
        return std::clamp(v, std::min(avail, min_v), std::min(avail, max_v));
    };

    switch (node.kind) {
    case AutoNode::Kind::Root:
    case AutoNode::Kind::AnchorRoot:
    case AutoNode::Kind::VStack:
    case AutoNode::Kind::TableCell:
    case AutoNode::Kind::Card:
    case AutoNode::Kind::CustomBox: {
        if (n == 0) {
            return;
        }
        std::size_t flow_n = 0;
        float fixed_h = 0.0f;
        float grow_sum = 0.0f;
        std::vector<float> child_h(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) {
                continue;
            }
            ++flow_n;
            if (c.height_spec.kind == UISizeKind::Grow) {
                grow_sum += c.height_spec.value;
            } else {
                float h = c.meas_h;
                if (c.height_spec.kind == UISizeKind::Percent) {
                    h = std::clamp(cr.height * c.height_spec.value, c.limits.min_h, c.limits.max_h);
                }
                child_h[i] = h;
                fixed_h += h;
            }
        }
        const float gaps = flow_n > 1 ? node.gap * static_cast<float>(flow_n - 1) : 0.0f;
        const float rem_h = std::max(0.0f, cr.height - fixed_h - gaps);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) {
                continue;
            }
            if (c.height_spec.kind == UISizeKind::Grow && grow_sum > 0.0f) {
                child_h[i] = std::clamp(rem_h * (c.height_spec.value / grow_sum), c.limits.min_h, c.limits.max_h);
            }
        }

        float y = cr.y;
        for (std::size_t i = 0; i < n; ++i) {
            const int cid = children[i];
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            if (c.kind == AutoNode::Kind::AnchorRoot && c.overlay) {
                float cw = resolve_cross(UIAxisAlign::Start, c.width_spec, c.meas_w, cr.width,
                                         c.limits.min_w, c.limits.max_w);
                float ch = resolve_cross(UIAxisAlign::Start, c.height_spec, c.meas_h, cr.height,
                                         c.limits.min_h, c.limits.max_h);
                float cx = cr.x + c.inset_x;
                if (c.h_align == "stretch") {
                    cx = cr.x + c.inset_x;
                    cw = std::clamp(std::max(0.0f, cr.width - 2.0f * c.inset_x), c.limits.min_w, c.limits.max_w);
                } else if (c.h_align == "right") {
                    cx = cr.x + cr.width - cw - c.inset_x;
                } else if (c.h_align == "center") {
                    cx = cr.x + (cr.width - cw) * 0.5f + c.inset_x;
                }
                float cy = cr.y + c.inset_y;
                if (c.v_align == "stretch") {
                    cy = cr.y + c.inset_y;
                    ch = std::clamp(std::max(0.0f, cr.height - 2.0f * c.inset_y), c.limits.min_h, c.limits.max_h);
                } else if (c.v_align == "bottom") {
                    cy = cr.y + cr.height - ch - c.inset_y;
                } else if (c.v_align == "center") {
                    cy = cr.y + (cr.height - ch) * 0.5f + c.inset_y;
                }
                ArrangeAutoNode(cid, {cx, cy, cw, ch}, clip, node.in_scroll, root_id,
                                eff_row_id, eff_col_idx);
                continue;
            }
            const float cw = resolve_cross(node.cross_align, c.width_spec, c.meas_w, cr.width,
                                           c.limits.min_w, c.limits.max_w);
            const float cx = cr.x + AlignOffset(node.cross_align, cr.width, cw) + c.anchor_x;
            const float cy = y + c.anchor_y;
            ArrangeAutoNode(cid, {cx, cy, cw, child_h[i]}, clip, node.in_scroll, root_id,
                            eff_row_id, eff_col_idx);
            y += child_h[i] + node.gap;
        }
        return;
    }
    case AutoNode::Kind::HStack: {
        if (n == 0) {
            return;
        }
        const float gaps = n > 1 ? node.gap * static_cast<float>(n - 1) : 0.0f;
        float fixed_w = 0.0f;
        float grow_sum = 0.0f;
        std::vector<float> child_w(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (c.width_spec.kind == UISizeKind::Grow) {
                grow_sum += c.width_spec.value;
            } else {
                float w = c.meas_w;
                if (c.width_spec.kind == UISizeKind::Percent) {
                    w = std::clamp(cr.width * c.width_spec.value, c.limits.min_w, c.limits.max_w);
                }
                child_w[i] = w;
                fixed_w += w;
            }
        }
        const float rem_w = std::max(0.0f, cr.width - fixed_w - gaps);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (c.width_spec.kind == UISizeKind::Grow && grow_sum > 0.0f) {
                child_w[i] = std::clamp(rem_w * (c.width_spec.value / grow_sum), c.limits.min_w, c.limits.max_w);
            }
        }

        float x = cr.x;
        for (std::size_t i = 0; i < n; ++i) {
            const int cid = children[i];
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            const float ch = resolve_cross(node.cross_align, c.height_spec, c.meas_h, cr.height,
                                           c.limits.min_h, c.limits.max_h);
            const float cy = cr.y + AlignOffset(node.cross_align, cr.height, ch);
            const int cell_col = (eff_row_id >= 0) ? static_cast<int>(i) : eff_col_idx;
            ArrangeAutoNode(cid, {x, cy, child_w[i], ch}, clip, node.in_scroll, root_id,
                            eff_row_id, cell_col);
            x += child_w[i] + node.gap;
        }
        return;
    }
    case AutoNode::Kind::Table: {
        const std::size_t ncols = node.col_specs.size();
        if (ncols == 0 || n == 0) {
            return;
        }
        // Recompute Grow columns for exact final content_rect width
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

        float y = cr.y;
        for (const int rid : children) {
            const float rh = auto_nodes_[static_cast<std::size_t>(rid)].meas_h;
            auto &rn = auto_nodes_[static_cast<std::size_t>(rid)];
            rn.rect = {cr.x, y, cr.width, rh};
            rn.content_rect = {cr.x + rn.pad_l, y + rn.pad_t,
                               std::max(0.0f, cr.width - rn.pad_l - rn.pad_r),
                               std::max(0.0f, rh - rn.pad_t - rn.pad_b)};
            rn.clip_rect = clip;
            rn.root_box_id = root_id;
            rn.in_scroll = node.in_scroll;

            float x = rn.content_rect.x;
            const auto r_cells = rn.children;
            for (std::size_t c = 0; c < ncols && c < r_cells.size(); ++c) {
                const int cid = r_cells[c];
                const auto &cell = auto_nodes_[static_cast<std::size_t>(cid)];
                const float ch = resolve_cross(rn.cross_align, cell.height_spec, cell.meas_h,
                                               rn.content_rect.height, cell.limits.min_h,
                                               cell.limits.max_h);
                const float cy = rn.content_rect.y +
                                 AlignOffset(rn.cross_align, rn.content_rect.height, ch);
                ArrangeAutoNode(cid, {x, cy, col_w[c], ch}, clip, node.in_scroll, root_id,
                                rn.row_id, static_cast<int>(c));
                x += col_w[c] + node.gap;
            }
            y += rh + node.line_gap;
        }
        return;
    }
    case AutoNode::Kind::TableRow: {
        // Handled by Table parent above
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
            float height = 0.0f;
        };
        std::vector<FlowLine> lines(1);
        float cx = 0.0f;
        for (const int cid : children) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            const float cw = std::min(c.meas_w, cr.width);
            const float ch = c.meas_h;
            if (cx > 0.0f && cx + cw > cr.width + 0.01f) {
                lines.emplace_back();
                cx = 0.0f;
            }
            lines.back().ids.push_back(cid);
            lines.back().ws.push_back(cw);
            lines.back().hs.push_back(ch);
            lines.back().height = std::max(lines.back().height, ch);
            cx += cw + node.gap;
        }
        float y = cr.y;
        for (const auto &ln : lines) {
            float x = cr.x;
            for (std::size_t i = 0; i < ln.ids.size(); ++i) {
                const float cy = y + AlignOffset(node.cross_align, ln.height, ln.hs[i]);
                ArrangeAutoNode(ln.ids[i], {x, cy, ln.ws[i], ln.hs[i]}, clip, node.in_scroll,
                                root_id, eff_row_id, eff_col_idx);
                x += ln.ws[i] + node.gap;
            }
            y += ln.height + node.line_gap;
        }
        return;
    }
    case AutoNode::Kind::Scroll: {
        const float sb = ThemeMetric("scrollbar_width");
        const float content_w = std::max(0.0f, cr.width - sb);
        auto &saved = scroll_states_[node.key];
        saved.height = node.content_height;

        // Handle scrollbar drag before placing children; wheel is resolved post-arrange by ResolveDeepestWheel
        const bool old_input_only = scroll_input_only_;
        const bool saved_modal = in_modal_, saved_popup = in_popup_;
        in_modal_ = !node.modal_id.empty() && node.modal_id == modal_;
        in_popup_ = !node.popup_id.empty() && node.popup_id == popup_;
        scroll_input_only_ = true;
        if (input_ && camera_ && renderer_) {
            saved.offset = PaintScrollArea(node.key, cr.x, cr.y, cr.width, cr.height,
                                           node.content_height, saved.offset, false);
        } else {
            saved.offset = std::clamp(saved.offset, 0.0f, std::max(0.0f, node.content_height - cr.height));
        }
        in_modal_ = saved_modal;
        in_popup_ = saved_popup;
        scroll_input_only_ = old_input_only;
        node.scroll_offset = saved.offset;

        float y = cr.y - saved.offset;
        for (const int cid : children) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            const float cw = resolve_cross(node.cross_align, c.width_spec, c.meas_w, content_w,
                                           c.limits.min_w, c.limits.max_w);
            const float cx = cr.x + AlignOffset(node.cross_align, content_w, cw);
            ArrangeAutoNode(cid, {cx, y, cw, c.meas_h}, clip, true, root_id, eff_row_id, eff_col_idx);
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

void UISystem::CommitAutoNode(int idx, const Rect &parent_bounds) {
    const auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
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
        e.clip_rect = clip;
        e.box_id = node.box_id;
        e.root_box_id = node.root_box_id;
        e.box_depth = node.box_depth;
        e.row_id = node.row_id;
        e.col_index = node.col_index;
        e.layer = node.layer;
        e.suppressed = node.suppressed;
        e.in_scroll = node.in_scroll;
        e.disabled = node.disabled;
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

    if (!node.tip_text.empty() && !node.suppressed && tip_recorder_ && vis.width > 0.0f && vis.height > 0.0f) {
        tip_recorder_(vis, node.tip_text, false);
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
                focus_ring_.push_back({node.key, r, is_text, adjustable});
            }
        }
    };

    ApplyClipRectDirect(clip);

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
        emit_audit("panel", node.key);
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
    case AutoNode::Kind::VStack:
    case AutoNode::Kind::HStack:
    case AutoNode::Kind::Table:
    case AutoNode::Kind::TableRow:
    case AutoNode::Kind::TableCell:
    case AutoNode::Kind::Flow:
    case AutoNode::Kind::Card: {
        for (const int cid : node.children) {
            CommitAutoNode(cid, node.content_rect);
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
            CommitAutoNode(cid, node.content_rect);
        }
        break;
    }
    case AutoNode::Kind::Scroll: {
        emit_audit("scroll viewport", node.key);
        const float sb = ThemeMetric("scrollbar_width");
        const Rect scroll_bounds{
            node.content_rect.x,
            node.content_rect.y - node.scroll_offset,
            std::max(0.0f, node.content_rect.width - sb),
            std::max(node.content_rect.height, node.content_height),
        };
        for (const int cid : node.children) {
            CommitAutoNode(cid, scroll_bounds);
        }
        ApplyClipRectDirect(clip);
        const auto &st = widgets_[node.key];
        DrawScrollbarVisual(node.content_rect, node.content_height, node.scroll_offset, st.hover, st.dragging);
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
        const float measured = wrap ? r.width : renderer_->MeasureText(drawn, used);
        const float dx = r.x + (wrap ? 0.0f
                                     : (node.text_align == 1 ? (r.width - measured) * 0.5f
                                        : node.text_align == 2 ? (r.width - measured)
                                                               : 0.0f));
        const Rect text_clip = IntersectClip(clip, r);
        ApplyClipRectDirect(text_clip);
        renderer_->DrawTextEx(drawn, dx * scale_ + scale_offset_.x,
                              r.y * scale_ + scale_offset_.y, used * scale_,
                              wrap ? node.text_align : 0, wrap ? r.width * scale_ : 0.0f,
                              text_color);
        break;
    }
    case AutoNode::Kind::Button: {
        emit_audit("button", node.key);
        register_focus(false, false);
        const auto &st = widgets_[node.key];
        const bool hov = st.hover || (vis.width > 0 && vis.Contains(Mouse()) && CanInteract() && MouseVisible());
        const Color bc = node.has_btn_colors ? node.btn_col : ThemeColor("button");
        const Color hc = node.has_btn_colors ? node.btn_hover : ThemeColor("hover");
        const Color pc = node.has_btn_colors ? node.btn_pressed : ThemeColor("pressed");
        const Color brd = node.has_btn_colors ? node.btn_border : ThemeColor("border");
        const Color tc = node.has_btn_colors ? node.btn_text : ThemeColor("text");
        DrawButtonVisual(r, hov, st.pressed, node.disabled, node.text, node.scale, bc, hc, pc, brd, tc);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::ProgressBar: {
        emit_audit("progress", node.key);
        DrawProgressVisual(r, node.val, node.fill_col, node.track_col);
        break;
    }
    case AutoNode::Kind::Slider: {
        emit_audit("slider", node.key);
        register_focus(false, true);
        const auto &st = widgets_[node.key];
        DrawSliderVisual(r, node.val, node.min_val, node.max_val, st.hover, st.dragging);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::Checkbox:
    case AutoNode::Kind::RadioButton: {
        const bool radio = (node.kind == AutoNode::Kind::RadioButton);
        emit_audit(radio ? "radio" : "checkbox", node.key);
        register_focus(false, false);
        const auto &st = widgets_[node.key];
        DrawCheckboxVisual(r, node.checked, node.text, node.scale, st.hover, radio);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::Tab: {
        emit_audit("tab", node.key);
        register_focus(false, false);
        const auto &st = widgets_[node.key];
        DrawTabVisual(r, node.text, node.checked, st.hover, st.pressed, node.scale);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::Tree: {
        emit_audit("tree", node.key);
        register_focus(false, false);
        const auto &st = widgets_[node.key];
        DrawTreeVisual(r, node.text, node.checked, st.hover, node.scale);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::ComboBegin:
    case AutoNode::Kind::ComboItem: {
        const bool is_item = (node.kind == AutoNode::Kind::ComboItem);
        emit_audit(is_item ? "combo_item" : "combo", node.key);
        register_focus(false, false);
        const auto &st = widgets_[node.key];
        DrawComboVisual(r, node.text, node.checked, node.checked, st.hover, node.scale, is_item);
        FocusIndicator(node.key, r);
        break;
    }
    case AutoNode::Kind::TextInput: {
        emit_audit("text_input", node.key);
        register_focus(true, false);
        EditText(node.key, r, node.max_chars, false, false, true, !recording_);
        break;
    }
    case AutoNode::Kind::TextArea: {
        emit_audit("text_area", node.key);
        register_focus(true, false);
        EditText(node.key, r, node.max_chars, true, false, true, !recording_);
        break;
    }
    case AutoNode::Kind::TextPassword: {
        emit_audit("password", node.key);
        register_focus(true, false);
        EditText(node.key, r, node.max_chars, false, true, true, !recording_);
        break;
    }
    case AutoNode::Kind::Splitter: {
        emit_audit("splitter", node.key);
        register_focus(false, true);
        const auto &st = widgets_[node.key];
        const float eff_min = std::clamp(node.min_val, 0.0f, r.width * 0.5f);
        const float gx = std::clamp(st.grab, eff_min, std::max(eff_min, r.width - eff_min));
        const Rect divider{r.x + gx - 3.0f, r.y, 6.0f, r.height};
        if (renderer_) {
            renderer_->DrawQuad(MapRect(divider), ThemeColor(st.dragging ? "accent" : "border"));
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
        DrawRichTextSolved(node, r, clip);
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
            if (renderer_) {
                const float border = ThemeMetric("border");
                const Color brd = st.hover ? ThemeColor("hover") : ThemeColor("border");
                const Color fill = st.pressed ? ThemeColor("pressed")
                                   : st.hover ? ThemeColor("hover")
                                              : ThemeColor("button");
                renderer_->DrawQuad(MapRect({r.x - border, r.y - border, r.width + 2 * border,
                                             r.height + 2 * border}),
                                    brd);
                renderer_->DrawQuad(MapRect(r), fill);
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
                                const Color &txt_c) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    Color fill = pressed ? prs_c : (hover ? hov_c : btn_c);
    Color label_col = txt_c;
    Color brd = brd_c;
    if (disabled) {
        fill.a *= 0.45f;
        label_col.a *= 0.45f;
        brd.a *= 0.45f;
    }
    const float border = ThemeMetric("border");
    renderer_->DrawQuad(
        MapRect({r.x - border, r.y - border, r.width + border * 2.0f, r.height + border * 2.0f}),
        brd);
    renderer_->DrawQuad(MapRect(r), fill);

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

void UISystem::DrawProgressVisual(const Rect &r, float value, const Color &fill, const Color &track) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float t = std::clamp(value, 0.0f, 1.0f);
    renderer_->DrawQuad(MapRect(r), track);
    if (t > 0.0f) {
        renderer_->DrawQuad(MapRect({r.x, r.y, r.width * t, r.height}), fill);
    }
}

void UISystem::DrawSliderVisual(const Rect &r, float value, float min_v, float max_v, bool hover,
                                bool dragging) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f || !(max_v > min_v)) {
        return;
    }
    const float t = std::clamp((value - min_v) / (max_v - min_v), 0.0f, 1.0f);
    const float track_h = r.height * 0.34f;
    const Rect track{r.x, r.y + (r.height - track_h) * 0.5f, r.width, track_h};
    const float thumb_w = r.height * 0.6f;
    const float range_px = std::max(0.0f, r.width - thumb_w);
    const Color thumb_col = dragging ? ThemeColor("accent") : (hover ? ThemeColor("hover") : kSliderThumb);
    renderer_->DrawQuad(MapRect({r.x - 2.0f, r.y - 2.0f, r.width + 4.0f, r.height + 4.0f}),
                        ThemeColor("track"));
    renderer_->DrawQuad(MapRect(track), ThemeColor("track"));
    renderer_->DrawQuad(MapRect({r.x + t * range_px, r.y, thumb_w, r.height}), thumb_col);
}

void UISystem::DrawCheckboxVisual(const Rect &r, bool checked, std::string_view label, float scale,
                                  bool hover, bool radio) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float size = std::min(r.width, r.height);
    const Rect box{r.x, r.y + (r.height - size) * 0.5f, size, size};
    renderer_->DrawQuad(MapRect({box.x - 2.0f, box.y - 2.0f, size + 4.0f, size + 4.0f}),
                        hover ? ThemeColor("hover") : ThemeColor("border"));
    renderer_->DrawQuad(MapRect(box), ThemeColor("field"));
    if (checked) {
        const float inset = size * (radio ? 0.28f : 0.26f);
        renderer_->DrawQuad(
            MapRect({box.x + inset, box.y + inset, size - inset * 2.0f, size - inset * 2.0f}),
            ThemeColor("check"));
    }
    if (!label.empty()) {
        const float avail_lbl = std::max(0.0f, r.width - size - 8.0f);
        const std::string fitted = Ellipsize(label, avail_lbl, scale);
        const float th = renderer_->GetFont().LineHeight() * scale;
        renderer_->DrawTextEx(fitted, (box.x + size + 8.0f) * scale_ + scale_offset_.x,
                              (box.y + size * 0.5f - th * 0.5f) * scale_ + scale_offset_.y,
                              scale * scale_, 0, 0, ThemeColor("text"));
    }
}

void UISystem::DrawTabVisual(const Rect &r, std::string_view label, bool active, bool hover,
                             bool pressed, float scale) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const Color bg = pressed ? ThemeColor("pressed")
                     : active ? ThemeColor("accent")
                     : hover  ? ThemeColor("hover")
                              : ThemeColor("panel");
    const float border = ThemeMetric("border");
    renderer_->DrawQuad(
        MapRect({r.x - border, r.y - border, r.width + 2 * border, r.height + 2 * border}),
        ThemeColor("border"));
    renderer_->DrawQuad(MapRect(r), bg);
    if (!label.empty()) {
        const std::string fitted = Ellipsize(label, std::max(0.0f, r.width - 8.0f), scale);
        const float tw = renderer_->MeasureText(fitted, scale);
        const float th = renderer_->GetFont().LineHeight() * scale;
        renderer_->DrawTextEx(fitted, (r.x + (r.width - tw) * 0.5f) * scale_ + scale_offset_.x,
                              (r.y + (r.height - th) * 0.5f) * scale_ + scale_offset_.y,
                              scale * scale_, 0, 0, ThemeColor("text"));
    }
}

void UISystem::DrawTreeVisual(const Rect &r, std::string_view label, bool open, bool hover,
                              float scale) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    if (hover) {
        renderer_->DrawQuad(MapRect(r), ThemeColor("hover"));
    }
    const std::string txt = std::string(open ? "v " : "> ") + std::string(label);
    const std::string fitted = Ellipsize(txt, std::max(0.0f, r.width - 6.0f), scale);
    const float th = renderer_->GetFont().LineHeight() * scale;
    renderer_->DrawTextEx(fitted, (r.x + 4.0f) * scale_ + scale_offset_.x,
                          (r.y + (r.height - th) * 0.5f) * scale_ + scale_offset_.y,
                          scale * scale_, 0, 0, ThemeColor("text"));
}

void UISystem::DrawComboVisual(const Rect &r, std::string_view label, bool open, bool selected,
                               bool hover, float scale, bool is_item) {
    if (!renderer_ || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const Color bg = is_item ? (selected ? ThemeColor("accent")
                                : hover   ? ThemeColor("hover")
                                          : ThemeColor("panel"))
                             : (hover ? ThemeColor("hover") : ThemeColor("field"));
    const float border = ThemeMetric("border");
    renderer_->DrawQuad(
        MapRect({r.x - border, r.y - border, r.width + 2 * border, r.height + 2 * border}),
        ThemeColor("border"));
    renderer_->DrawQuad(MapRect(r), bg);
    const std::string txt =
        is_item ? std::string(label) : (std::string(label) + (open ? " ▲" : " ▼"));
    const std::string fitted = Ellipsize(txt, std::max(0.0f, r.width - 10.0f), scale);
    const float th = renderer_->GetFont().LineHeight() * scale;
    renderer_->DrawTextEx(fitted, (r.x + 6.0f) * scale_ + scale_offset_.x,
                          (r.y + (r.height - th) * 0.5f) * scale_ + scale_offset_.y,
                          scale * scale_, 0, 0, ThemeColor("text"));
}

void UISystem::DrawScrollbarVisual(const Rect &vp, float content_h, float offset, bool hover,
                                   bool dragging) {
    if (!renderer_ || content_h <= vp.height || vp.height <= 0.0f) {
        return;
    }
    const float bar_w = 10.0f;
    const float max_scroll = std::max(1e-4f, content_h - vp.height);
    const Rect bar{vp.x + vp.width - bar_w, vp.y, bar_w, vp.height};
    const float thumb_h = std::min(vp.height, std::max(24.0f, vp.height * (vp.height / content_h)));
    const float track = vp.height - thumb_h;
    const float drawn = vp.y + std::clamp(offset / max_scroll, 0.0f, 1.0f) * track;
    renderer_->DrawQuad(MapRect(bar), ThemeColor("track"));
    renderer_->DrawQuad(MapRect({bar.x + 1.0f, drawn, bar_w - 2.0f, thumb_h}),
                        dragging ? ThemeColor("accent") : (hover ? ThemeColor("hover") : kScrollThumb));
}

void UISystem::ResolveDeepestWheel(int root_idx) {
    if (!input_ || !camera_ || !renderer_ || input_->MouseWheel() == 0.0) {
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
                } else if (node.content_height > node.content_rect.height) {
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
    const float max_scroll = std::max(0.0f, wn.content_height - wn.content_rect.height);
    const float next_off = std::clamp(saved.offset - static_cast<float>(input_->MouseWheel()) * 40.0f,
                                      0.0f, max_scroll);
    if (std::abs(next_off - saved.offset) <= 1e-6f) {
        return;
    }
    saved.offset = next_off;
    wn.scroll_offset = next_off;
    const float sb = ThemeMetric("scrollbar_width");
    const float content_w = std::max(0.0f, wn.content_rect.width - sb);
    float y = wn.content_rect.y - next_off;
    const auto children = wn.children;
    for (const int cid : children) {
        const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
        const float avail_w = content_w;
        float cw = c.meas_w;
        if (c.width_spec.kind == UISizeKind::Percent) {
            cw = avail_w * c.width_spec.value;
        } else if (c.width_spec.kind == UISizeKind::Grow || wn.cross_align == UIAxisAlign::Stretch) {
            cw = avail_w;
        }
        cw = std::clamp(cw, std::min(avail_w, c.limits.min_w), std::min(avail_w, c.limits.max_w));
        const float cx = wn.content_rect.x + AlignOffset(wn.cross_align, content_w, cw);
        ArrangeAutoNode(cid, {cx, y, cw, c.meas_h}, wn.clip_rect, true, wn.root_box_id, wn.row_id,
                        wn.col_index);
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
                focus_ring_.push_back({node.key, node.rect, is_text, adjustable});
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
                focus_ring_.push_back({rl.key, rl.rr, false, false});
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
            if (fired) {
                activated_widget_ = node.key;
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
            frame_results_[node.key] = EditText(node.key, r, node.max_chars, multi, pass, false, true);
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
