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
    for (const auto &ag : align_stack_) {
        // Track active align group indices on align_groups_ matching key/depth
        for (int gi = static_cast<int>(align_groups_.size()) - 1; gi >= 0; --gi) {
            if (align_groups_[static_cast<std::size_t>(gi)].key == ag.key &&
                align_groups_[static_cast<std::size_t>(gi)].depth == ag.depth) {
                node.align_group_ids.push_back(gi);
                break;
            }
        }
    }

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

    float rx = ox + root.anchor_x;
    float ry = oy + root.anchor_y;
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
    ArrangeAutoNode(closed_idx, root_rect, vp_clip, false, root.box_id, -1, -1);

    if (root.block_input) {
        BlockInput(root.rect);
        if (!root.suppressed && tip_recorder_) {
            tip_recorder_(root.rect, {}, true);
        }
    }

    CommitAutoNode(closed_idx, vp_clip);
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
    node.gap = ThemeMetric("row_gap");
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

bool UISystem::HitTestSolvedKey(std::string_view key, bool text, bool adjustable, bool focusable) {
    (void)adjustable;
    (void)focusable;
    if (!camera_ || !renderer_ || !input_) {
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
    const bool clicked = TrackPressWidget(state, hover);
    return clicked || activate;
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
        if (node.parent >= 0 && auto_nodes_[static_cast<std::size_t>(node.parent)].kind == AutoNode::Kind::VStack) {
            node.width_spec = UISize::Grow(1.0f);
        }
    }
    // Return estimated height for callers checking Text() return
    const float w = std::max(0.0f, node.est_avail_w);
    if (overflow == UITextOverflow::Wrap && renderer_ && w > 0.0f && !text.empty()) {
        return renderer_->GetFont().MeasureTextEx(text, sc, w);
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

    if (recording_ && !resolving_) {
        const Rect est_r = prev_solved_widgets_.count(node.key) ? prev_solved_widgets_.at(node.key).rect : Rect{};
        QueueInteraction(
            key, est_r,
            [this, k = node.key, idx]() {
                const auto &n = auto_nodes_[static_cast<std::size_t>(idx)];
                const bool activate = FocusWidget(k, n.rect);
                const bool hover = n.rect.Contains(Mouse()) && CanInteract() && MouseVisible();
                WidgetState &st = widgets_[k];
                const bool clicked = TrackPressWidget(st, hover);
                frame_results_[k] = (clicked || activate) ? 1.0f : 0.0f;
            },
            false, false, true);
        return false;
    }
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

    float next = value;
    if (max_v > min_v) {
        next = std::clamp(value, min_v, max_v);
        if (camera_ && renderer_ && input_) {
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
    return input_ && CanInteract() && focused_ == key && input_->KeyPressed("enter");
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
    return HitTestSolvedKey(key, false, false, true);
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

void UISystem::AutoPushCustom(float x, float y, float w, float h, float pad) {
    const bool had_w = next_auto_mod_.has_w;
    const bool had_h = next_auto_mod_.has_h;
    const bool had_pad = next_auto_mod_.has_pad;
    const int idx = AllocAutoNode(AutoNode::Kind::CustomBox);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.anchor_x = x;
    node.anchor_y = y;
    if (!had_w) {
        node.width_spec = w > 0.0f ? UISize::Fixed(w) : UISize::Grow(1.0f);
    }
    if (!had_h) {
        node.height_spec = h > 0.0f ? UISize::Fixed(h) : (h < 0.0f ? UISize::Grow(1.0f) : UISize::Auto());
    }
    if (!had_pad && pad > 0.0f) {
        node.pad_l = node.pad_t = node.pad_r = node.pad_b = pad;
    }
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = 0.0f;
    const float bw = w > 0.0f ? w : node.est_avail_w;
    node.est_avail_w = std::max(0.0f, bw - node.pad_l - node.pad_r);
    auto_stack_.push_back(idx);
}

void UISystem::AutoPushCustomInset(float l, float t, float r, float b, float w, float h) {
    const int idx = AllocAutoNode(AutoNode::Kind::CustomBox);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.width_spec = w > 0.0f ? UISize::Fixed(w) : UISize::Grow(1.0f);
    node.height_spec = h > 0.0f ? UISize::Fixed(h) : (h == 0.0f ? UISize::Auto() : UISize::Grow(1.0f));
    node.pad_l = std::max(0.0f, l);
    node.pad_t = std::max(0.0f, t);
    node.pad_r = std::max(0.0f, r);
    node.pad_b = std::max(0.0f, b);
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = 0.0f;
    const float bw = w > 0.0f ? w : node.est_avail_w;
    node.est_avail_w = std::max(0.0f, bw - node.pad_l - node.pad_r);
    auto_stack_.push_back(idx);
}

void UISystem::AutoPushCustomAnchor(std::string_view h_align, std::string_view v_align, float w,
                                    float h, float ix, float iy, float pad, bool overlay) {
    (void)h_align;
    (void)v_align;
    (void)ix;
    (void)iy;
    (void)overlay;
    AutoPushCustom(0.0f, 0.0f, w, h, pad);
}

void UISystem::AutoRow(float gap, const std::vector<float> &columns) {
    const int idx = AllocAutoNode(AutoNode::Kind::HStack);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.width_spec = UISize::Grow(1.0f);
    node.height_spec = UISize::Auto();
    node.cross_align = UIAxisAlign::Start;
    node.gap = std::max(0.0f, gap);
    node.row_id = auto_row_seq_++;
    node.legacy_row_cols = columns;
    node.legacy_row_idx = 0;
    auto_stack_.push_back(idx);
}

void UISystem::AutoCol(float fixed_w) {
    if (auto_stack_.empty()) {
        return;
    }
    auto &row = auto_nodes_[static_cast<std::size_t>(auto_stack_.back())];
    UISize w_spec = UISize::Grow(1.0f);
    if (!row.legacy_row_cols.empty()) {
        if (row.legacy_row_idx < row.legacy_row_cols.size()) {
            const float cw = row.legacy_row_cols[row.legacy_row_idx];
            w_spec = cw < 0.0f ? UISize::Grow(-cw) : (cw == 0.0f ? UISize::Auto() : UISize::Fixed(cw));
        } else {
            w_spec = UISize::Fixed(0.0f);
        }
    } else if (fixed_w > 0.0f) {
        w_spec = UISize::Fixed(fixed_w);
    }
    const int col_idx_num = static_cast<int>(row.legacy_row_idx++);
    const int parent_row_id = row.row_id;

    const int idx = AllocAutoNode(AutoNode::Kind::VStack);
    auto &node = auto_nodes_[static_cast<std::size_t>(idx)];
    node.width_spec = w_spec;
    node.height_spec = UISize::Auto();
    node.cross_align = UIAxisAlign::Stretch;
    node.gap = 0.0f;
    node.row_id = parent_row_id;
    node.col_index = col_idx_num;
    if (w_spec.kind == UISizeKind::Fixed) {
        node.est_avail_w = w_spec.value;
    }
    auto_stack_.push_back(idx);
}

void UISystem::AutoColEnd() {
    CloseAutoNode();
}

void UISystem::AutoRowEnd() {
    CloseAutoNode();
}

// ---------------------------------------------------------------------------
// Heightless Widget Public Overloads
// ---------------------------------------------------------------------------

bool UISystem::Button(std::string_view key, std::string_view text, float scale) {
    return Button(key, 0.0f, text, scale);
}

float UISystem::Slider(std::string_view key, float value, float min_value, float max_value) {
    return Slider(key, 0.0f, value, min_value, max_value);
}

bool UISystem::Checkbox(std::string_view key, bool checked, std::string_view label, float scale) {
    return Checkbox(key, 0.0f, checked, label, scale);
}

bool UISystem::RadioButton(std::string_view key, bool selected, std::string_view label, float scale) {
    return RadioButton(key, 0.0f, selected, label, scale);
}

bool UISystem::TextInput(std::string_view key) {
    return TextInput(key, 0.0f, 24);
}

void UISystem::ProgressBar(float value, const Color &fill, const Color &track) {
    ProgressBar(0.0f, value, fill, track);
}

bool UISystem::Tab(std::string_view key, std::string_view label, bool active, float scale) {
    return Tab(key, 0.0f, label, active, scale);
}

bool UISystem::Tree(std::string_view key, std::string_view label, bool open, float scale) {
    return Tree(key, 0.0f, label, open, scale);
}

bool UISystem::ComboBegin(std::string_view key, std::string_view label, bool open, float scale) {
    return ComboBegin(key, 0.0f, label, open, scale);
}

bool UISystem::ComboItem(std::string_view key, std::string_view label, bool selected, float scale) {
    return ComboItem(key, 0.0f, label, selected, scale);
}

void UISystem::Image(const Texture2D *texture, const Rect &source, const Color &tint) {
    Image(texture, source, 0.0f, tint);
}

bool UISystem::ImageButton(std::string_view key, const Texture2D *texture, const Rect &source,
                           std::string_view label) {
    return ImageButton(key, texture, source, 0.0f, label);
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
        std::size_t count = 0;
        const auto children = node.children;
        for (const int cid : children) {
            MeasureAutoNode(cid, inner_avail_w, kUnbounded);
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
            max_w = std::max(max_w, c.meas_w);
            sum_h += c.meas_h;
            ++count;
        }
        const float gaps = count > 1 ? node.gap * static_cast<float>(count - 1) : 0.0f;
        apply_explicit_size(max_w + pad_x, sum_h + gaps + pad_y);
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
        const float gaps = n > 1 ? node.gap * static_cast<float>(n - 1) : 0.0f;
        float fixed_h = 0.0f;
        float grow_sum = 0.0f;
        std::vector<float> child_h(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
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
        const float rem_h = std::max(0.0f, cr.height - fixed_h - gaps);
        for (std::size_t i = 0; i < n; ++i) {
            const auto &c = auto_nodes_[static_cast<std::size_t>(children[i])];
            if (c.height_spec.kind == UISizeKind::Grow && grow_sum > 0.0f) {
                child_h[i] = std::clamp(rem_h * (c.height_spec.value / grow_sum), c.limits.min_h, c.limits.max_h);
            }
        }

        float y = cr.y;
        for (std::size_t i = 0; i < n; ++i) {
            const int cid = children[i];
            const auto &c = auto_nodes_[static_cast<std::size_t>(cid)];
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

        // Handle wheel/drag input before placing children so scrolling applies in the same frame
        const Rect vis_vp = IntersectClip(cr, clip);
        const bool can_wheel = !node.disabled &&
                               (modal_.empty() || modal_ == node.modal_id) &&
                               (popup_.empty() || popup_ == node.popup_id) &&
                               vis_vp.Contains(RawMouse()) &&
                               node.content_height > cr.height;
        const bool old_input_only = scroll_input_only_;
        scroll_input_only_ = true;
        if (input_ && camera_ && renderer_) {
            saved.offset = PaintScrollArea(node.key, cr.x, cr.y, cr.width, cr.height,
                                           node.content_height, saved.offset, can_wheel);
        } else {
            saved.offset = std::clamp(saved.offset, 0.0f, std::max(0.0f, node.content_height - cr.height));
        }
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

    auto emit_audit = [&](std::string_view kind, std::string_view name) {
        constexpr float eps = 0.05f;
        const bool ov = r.x < parent_bounds.x - eps || r.y < parent_bounds.y - eps ||
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

    // Focus ring registration for interactive widgets
    auto register_focus = [&](bool is_text, bool adjustable) {
        if (!node.disabled && (modal_.empty() || modal_ == node.modal_id) &&
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
        emit_audit("text", node.key);
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
        EditText(node.key, r, node.max_chars, false, false);
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

} // namespace fake2d
