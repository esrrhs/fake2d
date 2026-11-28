#pragma once

#include "fake2d/input.h"
#include "fake2d/math.h"
#include "fake2d/renderer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fake2d {

enum class UITextOverflow { Wrap, Ellipsis, Shrink, Clip };

enum class UISizeKind : std::uint8_t {
    Auto,    // Hug/fit content (measured bottom-up in Pass 1)
    Grow,    // Expand proportionally into remaining space (`value` = weight)
    Fixed,   // Explicit pixels (`value` = px)
    Percent, // Fraction of parent available size (`value` in [0, 1])
};

struct UISize {
    UISizeKind kind = UISizeKind::Auto;
    float value = 0.0f;

    static constexpr UISize Auto() { return {UISizeKind::Auto, 0.0f}; }
    static constexpr UISize Grow(float weight = 1.0f) {
        return {UISizeKind::Grow, weight > 0.0f ? weight : 1.0f};
    }
    static constexpr UISize Fixed(float px) {
        return {UISizeKind::Fixed, px > 0.0f ? px : 0.0f};
    }
    static constexpr UISize Percent(float frac) {
        return {UISizeKind::Percent, frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac)};
    }
};

enum class UIAxisAlign : std::uint8_t {
    Start,
    Center,
    End,
    Stretch,
    Baseline,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class UILayoutDirection : std::uint8_t { LTR, RTL };

struct UISafeAreaInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct UISemanticNode {
    std::string key;
    std::string role;
    std::string label;
    std::string value;
    std::string hint;
    Rect rect{};
    Rect clip_rect{};
    bool enabled = true;
    bool focused = false;
    bool checked = false;
    bool selected = false;
    std::vector<std::string> actions;
    std::vector<UISemanticNode> children;
};

enum class UIScrollDir : std::uint8_t { Vertical, Horizontal, Both };

/// Two-Pass immediate-style 2D UI layout engine (Measure -> Arrange -> Commit/Paint).
/// Widget state (hover, press, drag, focus, text, scroll) persists by string key across frames.
/// Within BeginFrameUI/EndFrameUI consume results after EndFrameUI().

class UISystem {
  public:
    /// Reset per-frame state; the engine calls it after the input snapshot.
    void NewFrame();

    // Explicit declaration -> interaction boundary. User code executes once.
    // During a declaration widgets return unchanged values/false. Read results
    // after EndFrameUI(), in the SAME frame; no hidden script replay.
    bool BeginFrameUI();
    bool EndFrameUI();
    // Optional explicit layout barrier for virtual views whose final position is not yet known.
    bool EndLayout();
    bool ListViewBegin(std::string_view key);
    bool ListViewEnd();
    [[nodiscard]] bool Clicked(std::string_view key) const;
    [[nodiscard]] float ResultNumber(std::string_view key, float fallback = 0) const;
    [[nodiscard]] std::string_view ClickedLink() const { return clicked_link_; }
    [[nodiscard]] std::string_view ActivatedWidget() const { return activated_widget_; }
    [[nodiscard]] Rect WidgetRect(std::string_view key) const;
    [[nodiscard]] int UnscopedWarnings() const { return unscoped_warnings_; }
    struct FrameStats {
        std::size_t rich_batches = 0, rich_glyphs = 0, premeasured_rows = 0;
        std::size_t nodes = 0, visible = 0, draw_commands = 0, tree_steps = 0, measured_rows = 0;
        std::size_t cached_subtrees_hit = 0;
        double layout_ms = 0, interaction_ms = 0;
    };
    [[nodiscard]] const FrameStats &Stats() const { return frame_stats_; }

    bool Undo(std::string_view key);
    bool Redo(std::string_view key);
    /// Optional deterministic monotonic seconds for recording/tests; empty = steady_clock.
    void SetInteractionClock(std::function<double()> clock) {
        interaction_clock_ = std::move(clock);
    }
    [[nodiscard]] std::size_t Selection(std::string_view key, bool end) const;

    bool ConfigureVariableList(std::string_view key, const std::vector<std::string> &ids,
                               float estimate = 24, std::string_view revision = {});
    // Optional exact wrapped-text premeasurement; extra_h covers fixed row adornments.
    bool SetVariableRowText(std::string_view key, std::string_view id, std::string_view text,
                            float scale = 0, float extra_h = 0);
    void SetVariableMeasureBudget(std::size_t rows) { variable_measure_budget_ = rows; }
    void InvalidateVariableList(std::string_view key, std::string_view row_id = {});
    bool VariableListBegin(std::string_view key, float view_h = 0.0f, bool pin_bottom = false);
    int VariableListNext(); // opens an auto-height row; -1 when viewport filled
    float VariableListRowEnd();
    void VariableListEnd();
    [[nodiscard]] float VariableListTotal(std::string_view key) const;
    [[nodiscard]] std::string VariableListAnchor(std::string_view key) const;
    [[nodiscard]] std::string VariableListID(std::string_view key, int index) const;
    [[nodiscard]] float VariableListHeight(std::string_view key, std::string_view row_id) const;
    [[nodiscard]] bool VariableListMeasured(std::string_view key, std::string_view row_id) const;

    struct Constraints {
        float min_w = 0, max_w = 1e9f, min_h = 0, max_h = 1e9f;
    };
    void Constrain(float min_w, float max_w, float min_h = 0, float max_h = 1e9f);
    bool SetScale(float reference_w, float reference_h, std::string_view mode = "contain");
    [[nodiscard]] float Scale() const { return scale_; }
    void Remember(std::string_view name);
    void RememberBox(std::string_view name);
    [[nodiscard]] Rect NamedRect(std::string_view name) const;
    [[nodiscard]] float ThemeMetric(std::string_view name) const;
    bool SetThemeMetric(std::string_view name, float value);
    [[nodiscard]] Color ThemeColor(std::string_view name) const;
    bool SetThemeColor(std::string_view name, const Color &value);
    void SetPanelSlice(const Texture2D *texture, const Rect &source, float corner_x,
                       float corner_y);
    void ThemedPanel();
    void DisabledBegin(bool disabled = true);
    void DisabledEnd();
    void SetRichIcon(std::string_view name, const Texture2D *texture, const Rect &source);
    std::string RichText(std::string_view markup, float scale = 0); // clicked link id or empty
    void OpenModal(std::string_view key); // May be declared late within an explicit frame
    void CloseModal();
    void ModalEnd();
    void ClosePopup() {
        popup_.clear();
        nav_focus_.clear();
    }
    void SetScrollOffset(std::string_view key, float offset);
    void OpenPopup(std::string_view key, float x, float y);
    bool ContextMenu(std::string_view key);
    int Popup(std::string_view key, const std::vector<std::string> &items, float width = 180,
              float item_h = 0.0f);
    [[nodiscard]] std::string_view FocusedWidget() const { return nav_focus_; }
    struct ListRange {
        int first = 0, count = 0;
    };
    ListRange ListBegin(std::string_view key, float view_h, int count, float item_h);
    void ListEnd();
    [[nodiscard]] int ListCount() const { return list_count_; }
    float Tween(std::string_view key, float target, float seconds, float dt,
                std::string_view easing = "linear");
    void SetPage(std::string_view controller, int page);
    [[nodiscard]] int Page(std::string_view controller) const;
    bool PageBegin(std::string_view controller, int page) const;
    void PageEnd() {} // call only for an active page; inactive body must be skipped

    float Text(std::string_view text, float scale = 0.0f,
               UITextOverflow overflow = UITextOverflow::Wrap, int align = 0,
               const Color &tint = Color::White(), float min_scale = 0.35f);
    [[nodiscard]] float ScrollContentHeight(std::string_view key) const;
    [[nodiscard]] float ScrollOffset(std::string_view key) const;
    [[nodiscard]] float ScrollContentWidth(std::string_view key) const;
    [[nodiscard]] float ScrollOffsetX(std::string_view key) const;
    void SetScrollOffsetX(std::string_view key, float offset);
    [[nodiscard]] float LayoutToken(std::string_view name) const;
    bool SetLayoutToken(std::string_view name, float value);
    struct AuditRect {
        std::string kind;
        Rect rect;
        Rect bounds;
        bool overflow = false;
        std::string name;
        std::string text;
        Rect clip_rect{0.0f, 0.0f, 1e9f, 1e9f};
        std::uint32_t box_id = 0;
        std::uint32_t parent_box_id = 0;
        std::uint32_t root_box_id = 0;
        std::uint32_t box_depth = 0;
        int row_id = -1;
        int col_index = -1;
        int layer = 0;
        bool suppressed = false;
        bool in_scroll = false;
        bool disabled = false;
        float margin_l = 0, margin_t = 0, margin_r = 0, margin_b = 0;
        float pad_l = 0, pad_t = 0, pad_r = 0, pad_b = 0;
        float meas_w = 0, meas_h = 0;
    };
    struct LayoutIssue {
        std::string category; // "overflow", "viewport", "overlap", "align_h", "align_v", "align_group"
        std::string rule;
        std::string a_name;
        std::string b_name;
        Rect a_rect{};
        Rect b_rect{};
        float delta = 0.0f;
        std::string message;
    };
    [[nodiscard]] static bool IsInteractiveKind(std::string_view kind);
    // Group layout rules: sibling alignment (left|right|center_x|top|bottom|center_y|width|height|
    // no_overlap|equal_spacing_x|equal_spacing_y), parent centering/containment (center_x_in_parent|
    // center_y_in_parent|center_in_parent|group_center_x_in_parent|group_center_y_in_parent|
    // group_center_in_parent|symmetric_x|symmetric_y|fit_parent), and selection modifiers
    // (include_text|widgets).
    void BeginAlignGroup(std::string_view key, std::string_view rules = "top|height|no_overlap");
    void EndAlignGroup();
    void SetAudit(bool enabled) { audit_enabled_ = enabled; }
    [[nodiscard]] const std::vector<AuditRect> &AuditRects() const { return audit_; }
    [[nodiscard]] std::size_t OverflowCount() const;
    [[nodiscard]] std::vector<LayoutIssue> AuditLayout() const;
    [[nodiscard]] std::size_t LayoutIssueCount() const;

    // Input capture, blocking, modal/suppress, clock, screen/root primitives, and multi-face text hooks
    struct TextHooks {
        std::function<float(SpriteBatch &, std::string_view, float x, float y, float px, const Color &, int align, int face)> draw;
        std::function<float(SpriteBatch &, std::string_view, float x, float y, float px, float wrap_w, const Color &, int face)> draw_wrapped;
        std::function<float(std::string_view, float px, int face)> measure;
        std::function<float(std::string_view, float px, float wrap_w, int face)> measure_wrapped;
    };
    void SetTextHooks(TextHooks hooks) { text_hooks_ = std::move(hooks); }
    void BlockInput(const Rect &r);
    void SetModal(bool modal);
    [[nodiscard]] bool IsModal() const { return manual_modal_ || !modal_.empty() || !popup_.empty(); }
    void SetSuppress(bool suppress) { suppress_ = suppress; }
    [[nodiscard]] bool IsSuppressed() const { return suppress_; }
    [[nodiscard]] bool MouseOverUI() const { return pointer_over_prev_; }
    [[nodiscard]] bool WantsMouse() const { return pointer_over_prev_ || IsModal(); }
    [[nodiscard]] int ClickCount() const { return click_count_; }
    void SetClockSeconds(float seconds) { clock_seconds_ = seconds; }
    [[nodiscard]] float ClockSeconds() const { return clock_seconds_; }
    [[nodiscard]] Vec2 LogicalMouse() const { return RawMouse(); }
    [[nodiscard]] Vec2 LogicalToWindow(const Vec2 &logical_pos) const {
        return {logical_pos.x * scale_ + scale_offset_.x, logical_pos.y * scale_ + scale_offset_.y};
    }
    [[nodiscard]] float LogicalWidth() const;
    [[nodiscard]] float LogicalHeight() const;
    [[nodiscard]] bool HoveredRect(const Rect &r) const;
    bool InvisibleButton(std::string_view key, const Rect &rect,
                         const Color &hover_tint = {1.0f, 0.9f, 0.6f, 0.06f});
    void DrawQuadAt(const Rect &rect, const Color &color);
    void DrawFrameAt(const Rect &rect, const Color &color, float thickness = 1.0f);
    void DrawRoundedQuadAt(const Rect &rect, float radius, const Color &color,
                           const Color &border = Color::Clear(), float border_w = 0.0f);
    void DrawRoundedGradientAt(const Rect &rect, float radius, const Color &start_col,
                               const Color &end_col, bool horizontal = false);
    void DrawRoundedFrameAt(const Rect &rect, float radius, float thickness, const Color &color);
    void DrawDropShadowAt(const Rect &rect, float radius, float off_y, float spread, float alpha);
    void DrawSpriteRect(const Texture2D *texture, const Rect &src, const Rect &dst,
                        const Color &tint = Color::White());
    float DrawTextPx(float x, float y, std::string_view text, float px,
                     const Color &color = Color::White(), int align = 0, int face = 0);
    float DrawWrappedTextPx(float x, float y, float wrap_w, std::string_view text, float px,
                            const Color &color = Color::White(), int face = 0);
    [[nodiscard]] float MeasureTextPx(std::string_view text, float px, int face = 0) const;
    [[nodiscard]] float MeasureWrappedTextPx(std::string_view text, float px, float wrap_w, int face = 0) const;

    void Panel(const Color &fill, const Color &border = Color::Clear());
    void DrawNineSliceAt(const Texture2D *texture, float corner,
                         const Color &tint = Color::White());
    void DrawSpriteAt(const Texture2D *texture, float x, float y, float w, float h,
                      const Color &tint = Color::White());
    void FillRect(float x, float y, float w, float h, const Color &fill,
                  const Color &border = Color::Clear());
    [[nodiscard]] std::string_view TextValue(std::string_view key) const;
    void SetTextValue(std::string_view key, std::string_view text);
    [[nodiscard]] std::string_view FocusedTextInput() const { return focused_; }
    bool Window(std::string_view key, std::string_view title, float scale = 0.0f);
    void Tip(std::string_view text); // attaches to current auto-layout node
    void SetTipRecorder(std::function<void(const Rect &clipped_rect, std::string_view text, bool is_occluder)> recorder) {
        tip_recorder_ = std::move(recorder);
    }
    void SetTipDelay(float seconds) { tip_delay_ = std::max(0.0f, seconds); }
    [[nodiscard]] float TipDelay() const { return tip_delay_; }
    void DrawTooltip();

    // Two-Pass Auto-Layout (Measure -> Arrange -> Commit/Paint), zero handwritten widget sizes.
    [[nodiscard]] bool InAutoLayout() const { return !auto_stack_.empty(); }
    void RootBegin(float x, float y, UISize w, UISize h, float pad = 0.0f, bool block_input = true);
    void RootAnchorBegin(std::string_view h_align, std::string_view v_align, UISize w, UISize h,
                         float inset_x = 0.0f, float inset_y = 0.0f, float pad = 0.0f,
                         bool overlay = true, bool block_input = true);
    void RootEnd();

    void VStackBegin(float gap = -1.0f, float pad = 0.0f,
                     UIAxisAlign cross_align = UIAxisAlign::Stretch);
    void HStackBegin(float gap = -1.0f, float pad = 0.0f,
                     UIAxisAlign cross_align = UIAxisAlign::Center);
    void StackEnd();
    void CardBegin(float pad = -1.0f, float gap = -1.0f);
    void CardBegin(const Color &fill, const Color &border, float pad = -1.0f, float gap = -1.0f);
    void CardEnd();
    void TableBegin(std::string_view key, const std::vector<UISize> &cols,
                    float col_gap = -1.0f, float row_gap = -1.0f);
    void TableColLimits(std::size_t col, float min_w, float max_w);
    void TableRowBegin(UIAxisAlign cross_align = UIAxisAlign::Center);
    void TableCellBegin(float pad = 0.0f);
    void TableCellEnd();
    void TableRowEnd();
    void TableEnd();
    void ScrollBoxBegin(std::string_view key, UISize view_h = UISize::Grow());
    void ScrollBoxBegin(std::string_view key, UISize view_w, UISize view_h,
                        UIScrollDir dir = UIScrollDir::Vertical);
    void ScrollBoxEnd();
    void FlowBoxBegin(float col_gap = -1.0f, float row_gap = -1.0f);
    void FlowBoxEnd();
    void ZStackBegin(float pad = 0.0f);
    void ZStackEnd();
    void OverlayBegin(std::string_view h_align = "end", std::string_view v_align = "start",
                      float off_x = 0.0f, float off_y = 0.0f);
    void OverlayEnd();
    void AnchorToBegin(std::string_view target_key, std::string_view placement = "bottom_start",
                       float gap = 4.0f, UISize w = UISize::Auto(), UISize h = UISize::Auto());
    void AnchorToEnd();
    void GridBegin(float min_col_w, float col_gap = -1.0f, float row_gap = -1.0f,
                   float pad = 0.0f);
    void GridColumnsBegin(int cols, float col_gap = -1.0f, float row_gap = -1.0f,
                          float pad = 0.0f);
    void GridEnd();
    void SafeAreaBegin(float extra_pad = 0.0f);
    void SafeAreaEnd();
    void SetSafeAreaInsets(float left, float top, float right, float bottom);
    [[nodiscard]] UISafeAreaInsets SafeAreaInsets() const { return safe_area_; }
    void SetLayoutDirection(UILayoutDirection dir) { default_layout_dir_ = dir; }
    [[nodiscard]] UILayoutDirection LayoutDirection() const;
    void LayoutDirectionBegin(UILayoutDirection dir);
    void LayoutDirectionEnd();
    struct VirtualRange {
        int first = 0;
        int count = 0;
    };
    VirtualRange VirtualBegin(std::string_view key, int total_count, float item_h,
                              int overscan = 1);
    [[nodiscard]] const VirtualRange &LastVirtualRange() const { return last_virtual_range_; }
    void VirtualEnd();
    bool CachedSubtreeBegin(std::string_view key, std::uint64_t version_hash);
    void CachedSubtreeEnd();
    void CanvasBegin(UISize w = UISize::Grow(), UISize h = UISize::Auto(), float pad = 0.0f);
    void CanvasEnd();
    void Spacer(float px = -1.0f);
    void Divider(const Color &color, float thickness = 1.0f);
    bool ModalBegin(std::string_view key, UISize w = UISize::Auto(), UISize h = UISize::Auto());
    bool ListView(std::string_view key, UISize view_h = UISize::Grow());

    // ID scoping stack, Focus Scopes, IME caret, Cursor, Scroll-to & live DevTools Inspector
    void PushID(std::string_view id);
    void PushID(int id);
    void PopID();
    [[nodiscard]] std::string ScopedKey(std::string_view key) const;
    void FocusScopeBegin(std::string_view key, bool trap = true);
    void FocusScopeEnd();
    void RequestFocus(std::string_view key);
    void ClearFocus();
    void ScrollToWidget(std::string_view key, bool smooth = true);
    [[nodiscard]] int DesiredCursorShape() const { return desired_cursor_shape_; }
    [[nodiscard]] float PinchDelta() const;
    [[nodiscard]] Rect ImeCursorRect() const { return ime_cursor_rect_; }
    void SetImeRectCallback(std::function<void(const Rect &)> cb) { ime_rect_cb_ = std::move(cb); }
    void SetInspector(bool enabled) { inspector_enabled_ = enabled; }
    [[nodiscard]] bool IsInspectorEnabled() const { return inspector_enabled_; }
    void ToggleInspector() { inspector_enabled_ = !inspector_enabled_; }

    // Drag & Drop across containers
    bool DragSourceBegin(std::string_view key, std::string_view type, std::string_view payload);
    void DragSourceEnd();
    bool DragPreviewBegin();
    void DragPreviewEnd();
    bool DropTargetBegin(std::string_view key, std::string_view accepted_types = {});
    void DropTargetEnd();
    [[nodiscard]] bool IsDragging() const { return drag_active_; }
    [[nodiscard]] std::string_view ActiveDragType() const { return drag_type_; }
    [[nodiscard]] std::string_view ActiveDragPayload() const { return drag_payload_; }
    [[nodiscard]] std::string_view ActiveDragKey() const { return drag_source_key_; }
    [[nodiscard]] bool IsDropHovered(std::string_view key) const;
    [[nodiscard]] std::string DroppedPayload(std::string_view target_key) const;
    [[nodiscard]] std::string DroppedType(std::string_view target_key) const;

    // Hierarchical MenuBar / Submenus
    void MenuBarBegin();
    void MenuBarEnd();
    bool MenuBegin(std::string_view key, std::string_view label);
    void MenuEnd();
    bool MenuItem(std::string_view key, std::string_view label, std::string_view shortcut = {},
                  bool checked = false);
    void MenuSeparator();

    // Semantic / Accessibility Tree export
    [[nodiscard]] std::vector<UISemanticNode> SemanticNodes() const;
    [[nodiscard]] UISemanticNode SemanticTree() const;
    [[nodiscard]] std::string SemanticJson() const;

    // One-shot layout & visual style modifiers applied to the next child/container in Two-Pass layout.
    void NextSize(UISize w, UISize h);
    void NextWidth(UISize w);
    void NextHeight(UISize h);
    void NextGrow(float weight = 1.0f);
    void NextShrink(float weight = 1.0f);
    void NextAspect(float ratio);
    void NextSpan(int cols = 1);
    void NextRowSpan(int rows = 1);
    void NextAlign(UIAxisAlign cross_align);
    void NextMainAlign(UIAxisAlign main_align);
    void NextPad(float l, float t, float r, float b);
    void NextMargin(float l, float t, float r, float b);
    void NextMargin(float all) { NextMargin(all, all, all, all); }
    void NextBg(const Color &col, float radius = 0.0f);
    void NextGradient(const Color &start_col, const Color &end_col, bool horizontal = false);
    void NextBorder(const Color &col, float width = 1.0f);
    void NextRadius(float radius);
    void NextShadow(float offset_y = 4.0f, float spread = 6.0f, float alpha = 0.35f);
    void NextBackdropBlur(float radius = 4.0f);
    void NextAlpha(float alpha);
    void NextClip(bool clip = true);
    void NextDirection(UILayoutDirection dir);
    void NextPlaceholder(std::string_view text);
    void NextTooltip(std::string_view text);
    void NextSemantics(std::string_view role, std::string_view label = {},
                       std::string_view value = {}, std::string_view hint = {});
    void NextVisible(bool visible);
    void NextEnabled(bool enabled);
    void NextTransition(std::string_view anim_key, float duration_sec = 0.18f);
    void NextFontFace(int face);
    void NextTextAlign(int align);
    void NextInnerHighlight(const Color &col);
    void NextInnerFrame(const Color &col, float inset = 3.0f, float thickness = 1.0f);
    void NextCornerOrnaments(const Color &col, float size = 14.0f);
    void NextLeftAccent(const Color &col, float width = 3.0f);

    // Heightless auto-sized widgets (intrinsic size computed from font & theme metrics).
    bool Button(std::string_view key, std::string_view text, float scale = 0.0f);
    float Slider(std::string_view key, float value, float min_value, float max_value);
    bool Checkbox(std::string_view key, bool checked, std::string_view label = {},
                  float scale = 0.0f);
    bool RadioButton(std::string_view key, bool selected, std::string_view label = {},
                     float scale = 0.0f);
    bool TextInput(std::string_view key, std::size_t max_chars = 24);
    bool TextArea(std::string_view key, std::size_t max_chars = 4096);
    bool TextPassword(std::string_view key, std::size_t max_chars = 128);
    float Splitter(std::string_view key, float initial, float minimum = 24.0f);
    void ProgressBar(float value, const Color &fill,
                     const Color &track = Color{0.14f, 0.17f, 0.24f, 1.0f});
    bool Tab(std::string_view key, std::string_view label, bool active, float scale = 0.0f);
    bool Tree(std::string_view key, std::string_view label, bool open, float scale = 0.0f);
    bool ComboBegin(std::string_view key, std::string_view label, bool open, float scale = 0.0f);
    bool ComboItem(std::string_view key, std::string_view label, bool selected, float scale = 0.0f);
    void Image(const Texture2D *texture, const Rect &source,
               const Color &tint = Color::White());
    bool ImageButton(std::string_view key, const Texture2D *texture, const Rect &source,
                     std::string_view label = {});

    [[nodiscard]] bool IsHovered(std::string_view key) const;

    /// Wire the per-frame input snapshot, camera (viewport) and renderer.
    void Bind(const Input *input, const Camera2D *camera, Renderer *renderer);

    /// When widgets are placed in world coordinates under a scrolling camera,
    /// set its top-left world position so mouse hit-testing shifts with it.
    /// Zero (default) keeps widgets in screen space.
    void SetWorldOrigin(const Vec2 &origin) { world_origin_ = origin; }

  private:
    [[nodiscard]] float PaintScrollArea(std::string_view key, float offset_x, float offset_y,
                                        float w, float h, float content_height, float scroll_y,
                                        bool wheel_enabled);
    /// One state record serves every widget kind; the fields a given kind
    /// ignores simply stay at their default.
    struct WidgetState {
        bool hover = false;
        bool pressed = false;  // button held, thumb grabbed
        bool dragging = false; // slider / scrollbar thumb being dragged
        /// Grab offset from the thumb's top-left, so dragging does not make
        /// the thumb jump under the cursor.
        float grab = 0.0f;
        float hover_t = 0.0f;
        float press_t = 0.0f;
        float focus_t = 0.0f;
        struct EditSnapshot {
            std::string text;
            std::size_t caret = 0, anchor = 0;
        };
        std::vector<EditSnapshot> undo, redo;
        bool selecting = false;
        double last_click = -1;
        int click_streak = 0;
        Vec2 last_click_pos{};
        std::size_t caret = 0, anchor = 0;
        std::string text; // text field content
    };
    bool TrackPressWidget(WidgetState &state, bool hover);

    [[nodiscard]] WidgetState *Find(std::string_view key);
    /// Mouse in the same space widgets are laid out in (screen + world origin).
    [[nodiscard]] Vec2 Mouse() const;
    [[nodiscard]] bool MouseVisible() const;
    /// Fit a caption with ellipsis, or keep a UTF-8-safe editable tail.
    /// Zero width draws nothing. Returns the caret / right-edge x.
    float DrawFittedText(std::string_view text, float x, float y, float w, float scale,
                         const Color &tint, bool tail = false);

    struct AlignGroupRecord {
        std::string key;
        std::string rules;
        std::size_t depth = 0;
        std::uint32_t parent_box_id = 0;
        int parent_auto_idx = -1;
        Rect parent_rect{};
        int layer = 0;
        bool suppressed = false;
        std::size_t audit_start = 0;
        std::size_t audit_end = 0;
    };
    struct ScrollState {
        float height = 0, offset = 0;
        float width = 0, offset_x = 0;
        float target_offset = 0, target_offset_x = 0;
        bool has_target = false, has_target_x = false;
    };
    void Record(std::string_view kind, const Rect &rect, std::string_view name = {});
    std::string Ellipsize(std::string_view text, float width, float scale) const;
    std::unordered_map<std::string, ScrollState> scroll_states_;
    std::unordered_map<std::string, float> tokens_{
        {"panel_width", 600.0f},   {"sidebar_inset", 410.0f},    {"menu_icon_spacing_cap", 64.0f},
        {"sheet_pad_x", 14.0f},    {"sheet_pad_top", 8.0f},      {"sheet_pad_bottom", 12.0f},
        {"sheet_header_h", 40.0f}, {"sheet_header_pad_y", 5.0f}, {"sheet_btn_size", 22.0f},
        {"card_pad", 8.0f},        {"card_pad_lg", 12.0f},       {"gap_sm", 4.0f},
        {"gap_md", 8.0f},          {"gap_lg", 12.0f}};
    void NavigateCurrent();
    struct ListViewport {
        Rect rect{};
        Rect clip_rect{0.0f, 0.0f, 1e9f, 1e9f};
        std::string modal, popup;
        bool disabled = false, submitted = false;
    };
    std::unordered_map<std::string, ListViewport> list_views_;
    std::string active_list_view_;
    bool layout_resolved_ = false, dismissed_overlay_ = false, resolving_allowed_ = true;
    std::string text_activated_;
    std::function<double()> interaction_clock_;
    bool recording_ = false, resolving_ = false, frame_submitted_ = false;
    bool scroll_input_only_ = false;
    std::unordered_map<std::string, float> frame_results_;
    std::unordered_map<std::string, Rect> widget_rects_;
    std::string saved_nav_focus_, saved_text_focus_;
    bool saved_stick_ = false;
    std::string clicked_link_, activated_widget_, resolving_key_, pointer_owner_, pointer_active_;
    std::unordered_map<std::string, float> rich_advances_;
    float rich_advance_scale_ = -1;
    std::uint64_t rich_advance_revision_ = 0;
    bool unscoped_warned_ = false;
    int unscoped_warnings_ = 0;
    FrameStats frame_stats_;
    std::chrono::steady_clock::time_point frame_started_;
    struct VariableListState {
        std::vector<std::string> ids;
        std::unordered_map<std::string, std::size_t> positions;
        std::unordered_map<std::string, float> measured;
        struct TextMeasure {
            std::string text;
            float scale = 0, extra = 0;
        };
        std::unordered_map<std::string, TextMeasure> texts;
        std::size_t measure_cursor = 0;
        std::vector<float> heights, fenwick;
        std::string revision, anchor;
        float estimate = 24, anchor_inset = 0, width = -1, font_scale = -1;
        std::uint64_t font_revision = 0;
        bool bottom = false, anchor_pending = false;
    };
    struct VariableListScope {
        std::string key;
        float view_h;
        int next;
        bool row_open = false, pin_bottom = false;
        Rect view{};
        Rect vp_clip{0.0f, 0.0f, 1e9f, 1e9f};
        struct SubmittedRow {
            int index;
            int root_idx = -1;
        };
        std::vector<SubmittedRow> submitted;
        std::unordered_map<int, bool> visited;
    };
    void RebuildVariable(VariableListState &state);
    float VariablePrefix(const VariableListState &state, std::size_t count) const;
    std::size_t VariableFind(const VariableListState &state, float offset);
    void SaveVariableAnchor(const std::string &key, VariableListState &state);
    void RestoreVariableAnchor(const std::string &key, VariableListState &state, float view_h);
    std::unordered_map<std::string, VariableListState> variable_lists_;
    std::vector<VariableListScope> variable_scopes_;
    std::size_t variable_measure_budget_ = 64;
    bool CanInteract() const;
    bool FocusWidget(std::string_view key, const Rect &r, bool text = false,
                     bool adjustable = false);
    void FocusIndicator(std::string_view key, const Rect &r);
    Rect MapRect(Rect r) const;
    Vec2 RawMouse() const;
    bool EditText(std::string_view key, const Rect &r, std::size_t max_chars, bool multiline,
                  bool password = false, bool allow_draw = true, bool allow_input = true,
                  std::string_view placeholder = {}, float radius = 0.0f, float alpha = 1.0f);
    Constraints next_limits_;
    int deferred_ = 0;
    std::vector<bool> disabled_;
    bool IsDisabled() const { return !disabled_.empty() && disabled_.back(); }
    float scale_ = 1;
    float reference_w_ = 0, reference_h_ = 0;
    std::string scale_mode_ = "contain";
    Vec2 scale_offset_{};
    std::unordered_map<std::string, Rect> named_rects_;
    std::unordered_map<std::string, float> theme_metrics_{{"padding", 8},
                                                          {"spacing", 8},
                                                          {"font_scale", .6f},
                                                          {"min_font_scale", .35f},
                                                          {"border", 2},
                                                          {"button_pad_x", 10},
                                                          {"button_pad_y", 5},
                                                          {"tab_pad_x", 10},
                                                          {"tab_pad_y", 5},
                                                          {"input_pad_x", 8},
                                                          {"input_pad_y", 5},
                                                          {"bar_height", 6},
                                                          {"slider_height", 20},
                                                          {"row_gap", 4},
                                                          {"col_gap", 6},
                                                          {"card_pad", 8},
                                                          {"scrollbar_width", 12},
                                                          {"corner_radius", 0},
                                                          {"state_transition_ms", 0}};
    std::unordered_map<std::string, Color> theme_colors_{
        {"panel", {.07f, .09f, .14f, 1}},  {"button", {.16f, .22f, .34f, 1}},
        {"hover", {.20f, .35f, .65f, 1}},  {"pressed", {.25f, .45f, .85f, 1}},
        {"text", Color::White()},          {"accent", {.34f, .55f, .92f, 1}},
        {"border", {.35f, .45f, .65f, 1}}, {"field", {.10f, .12f, .18f, 1}},
        {"track", {.13f, .16f, .24f, 1}},  {"check", {.35f, .72f, .45f, 1}}};
    struct Slice {
        const Texture2D *texture = nullptr;
        Rect source{};
        float x = 0, y = 0;
    } panel_slice_;
    std::unordered_map<std::string, Slice> rich_icons_;
    std::string modal_, popup_;
    bool in_modal_ = false, in_popup_ = false;
    Vec2 popup_pos_{};
    int popup_index_ = 0;
    bool popup_just_opened_ = false;
    std::string nav_focus_;
    struct FocusEntry {
        std::string key;
        Rect rect;
        bool text;
        bool adjustable;
        std::string scope_id;
    };
    struct FocusScopeRecord {
        std::string id;
        std::string prev_focus;
        bool trap = true;
    };
    std::vector<FocusScopeRecord> focus_scopes_;
    std::vector<std::string> active_focus_scope_keys_;
    std::vector<FocusEntry> focus_ring_, prior_focus_;
    bool nav_activate_ = false, stick_held_ = false;
    int nav_adjust_step_ = 0;
    struct ListState {
        int count;
        float item_h;
        std::string key;
        Rect view{};
        Rect vp_clip{0.0f, 0.0f, 1e9f, 1e9f};
    };
    std::vector<ListState> lists_;
    int list_count_ = 0;
    struct TweenState {
        float value = 0, from = 0, target = 0, elapsed = 0;
    };
    std::unordered_map<std::string, TweenState> tweens_;
    std::unordered_map<std::string, int> pages_;
    Rect last_rect_{};
    std::vector<AuditRect> audit_;
    bool audit_enabled_ = false;
    std::size_t wheel_scroll_ = static_cast<std::size_t>(-1);
    std::size_t wheel_depth_ = 0;

    const Input *input_ = nullptr;
    const Camera2D *camera_ = nullptr;
    Renderer *renderer_ = nullptr;
    Vec2 world_origin_{0.0f, 0.0f};
    std::unordered_map<std::string, WidgetState> widgets_;
    std::string focused_;
    std::string tooltip_;
    Vec2 tooltip_mouse_{0.0f, 0.0f};
    float tip_delay_ = 0.3f;
    double tip_hover_since_ = 0.0;
    std::string tip_hover_key_;
    bool pointer_over_ = false;
    bool pointer_over_prev_ = false;
    bool manual_modal_ = false;
    int manual_modal_layer_ = 0;
    int modal_layer_seq_ = 0;
    bool suppress_ = false;
    int click_count_ = 0;
    float clock_seconds_ = 0.0f;
    std::uint32_t box_sequence_ = 1;
    std::vector<int> align_stack_;
    std::vector<AlignGroupRecord> align_groups_;
    TextHooks text_hooks_;
    std::function<void(const Rect &clipped_rect, std::string_view text, bool is_occluder)> tip_recorder_;

    // --- Two-Pass Auto-Layout Engine (ui_layout2.cpp & ui_modern.cpp) ---
    struct AutoLayoutMod {
        bool has_w = false, has_h = false, has_cross = false, has_main = false, has_pad = false;
        bool has_margin = false, has_shrink = false, has_aspect = false, has_span = false;
        bool has_row_span = false;
        bool has_bg = false, has_border = false, has_radius = false, has_shadow = false;
        bool has_alpha = false, has_anim = false, has_grad = false, has_blur = false;
        bool has_clip = false, clip_val = false, has_dir = false;
        bool has_placeholder = false, has_tip = false, has_sem = false;
        bool has_visible = false, visible_val = true;
        bool has_enabled = false, enabled_val = true;
        bool has_font_face = false, has_text_align = false;
        bool has_bevel = false, has_inner_frame = false, has_corners = false, has_left_accent = false;
        UISize w{}, h{};
        UIAxisAlign cross = UIAxisAlign::Start;
        UIAxisAlign main = UIAxisAlign::Start;
        UILayoutDirection dir = UILayoutDirection::LTR;
        float pad_l = 0, pad_t = 0, pad_r = 0, pad_b = 0;
        float margin_l = 0, margin_t = 0, margin_r = 0, margin_b = 0;
        float shrink = 0.0f, aspect_ratio = 0.0f;
        int col_span = 1, row_span = 1;
        int font_face = 0, text_align = 0;
        Color bg_col{}, grad_start{}, grad_end{}, border_col{};
        Color bevel_col{}, inner_frame_col{}, corner_col{}, left_accent_col{};
        bool grad_horiz = false;
        float border_w = 1.0f, radius = 0.0f, alpha_mul = 1.0f, backdrop_blur = 0.0f;
        float shadow_off_y = 0.0f, shadow_spread = 0.0f, shadow_alpha = 0.0f;
        float inner_frame_inset = 3.0f, inner_frame_w = 1.0f, corner_size = 14.0f, left_accent_w = 3.0f;
        std::string anim_key;
        float anim_duration = 0.0f;
        std::string placeholder, tip, sem_role, sem_label, sem_value, sem_hint;
    };
    struct SolvedWidgetInfo {
        Rect rect{};
        Rect clip_rect{};
        int layer = 0;
        bool disabled = false;
        std::string modal, popup;
    };
    struct AutoCustomCmd {
        enum class Op : std::uint8_t {
            Quad, Sprite, NineSlice, Text, WrappedText,
            RoundedQuad, RoundedGrad, RoundedFrame, DropShadow
        };
        Op op = Op::Quad;
        Rect rect{};
        Color fill{}, border{0.0f, 0.0f, 0.0f, 0.0f}, col2{0.0f, 0.0f, 0.0f, 0.0f};
        const Texture2D *texture = nullptr;
        Rect src{};
        std::string text;
        float px = 0, corner_x = 0, corner_y = 0;
        float radius = 0.0f, thickness = 1.0f, off_y = 0.0f, spread = 0.0f, alpha = 0.0f;
        int align = 0, face = 0;
        bool horizontal = false;
    };
    struct AutoNode {
        enum class Kind : std::uint8_t {
            Root, AnchorRoot, VStack, HStack, ZStack, Grid, Table, TableRow, TableCell,
            Flow, Scroll, Card, CustomBox, CachedBox, Spacer, Divider,
            SafeArea, DragSource, DropTarget, MenuBar, Menu, MenuItem, MenuSeparator,
            Text, RichText, Button, Slider, Checkbox, RadioButton, ProgressBar,
            TextInput, TextPassword, TextArea, Tab, Tree, ComboBegin, ComboItem,
            Image, ImageButton, Splitter, Window, ListViewSlot
        };
        Kind kind = Kind::VStack;
        int parent = -1;
        std::vector<int> children;
        std::string key;
        std::string anchor_target_key;
        std::string text;
        std::string shortcut;
        std::string placeholder;
        std::string drag_type, drag_payload, drop_accept;
        std::string sem_role, sem_label, sem_value, sem_hint;
        UISize width_spec = UISize::Auto();
        UISize height_spec = UISize::Auto();
        Constraints limits{};
        float pad_l = 0, pad_t = 0, pad_r = 0, pad_b = 0;
        float margin_l = 0, margin_t = 0, margin_r = 0, margin_b = 0;
        float shrink = 0.0f, aspect_ratio = 0.0f;
        int col_span = 1, row_span = 1;
        int grid_fixed_cols = 0;
        float min_col_w = 120.0f;
        float gap = 0, line_gap = 0;
        UIAxisAlign cross_align = UIAxisAlign::Stretch;
        UIAxisAlign main_align = UIAxisAlign::Start;
        bool has_cross_override = false;
        UILayoutDirection layout_dir = UILayoutDirection::LTR;
        UIScrollDir scroll_dir = UIScrollDir::Vertical;
        bool clip_children = false;
        float clip_radius = 0.0f;
        bool visible = true;
        bool block_input = false;
        bool overlay = false;
        bool use_screen_origin = false;
        bool has_clip_override = false;
        bool defer_commit = false;
        bool input_processed = false; // immediate text interaction already resolved from prior geometry
        Rect clip_override{};
        bool draw_panel = false, themed_panel = false;
        bool has_bg = false, has_border = false, has_shadow = false;
        bool has_grad = false, grad_horiz = false;
        bool has_pad_override = false, has_text_align_override = false;
        bool has_bevel = false, has_inner_frame = false, has_corners = false, has_left_accent = false;
        int font_face = 0;
        Color fill_col{}, border_col{}, track_col{}, bg_col{}, custom_border_col{};
        Color grad_start{}, grad_end{};
        Color bevel_col{}, inner_frame_col{}, corner_col{}, left_accent_col{};
        float border_w = 1.0f, radius = 0.0f, alpha_mul = 1.0f, backdrop_blur = 0.0f;
        float shadow_off_y = 0.0f, shadow_spread = 0.0f, shadow_alpha = 0.0f;
        float inner_frame_inset = 3.0f, inner_frame_w = 1.0f, corner_size = 14.0f, left_accent_w = 3.0f;
        std::string anim_key;
        float anim_duration = 0.0f;
        bool has_btn_colors = false;
        Color btn_col{}, btn_hover{}, btn_pressed{}, btn_border{}, btn_text{};
        float scale = 0, min_scale = 0.35f;
        UITextOverflow overflow = UITextOverflow::Wrap;
        int text_align = 0;
        float val = 0, min_val = 0, max_val = 1;
        bool checked = false;
        std::size_t max_chars = 24;
        const Texture2D *texture = nullptr;
        Rect tex_src{};
        std::string h_align, v_align;
        float anchor_x = 0, anchor_y = 0, inset_x = 0, inset_y = 0;
        // Table specifics
        std::vector<UISize> col_specs;
        std::vector<Constraints> col_limits;
        std::vector<float> col_widths;
        // Metadata
        std::uint32_t box_id = 0;
        std::uint32_t root_box_id = 0;
        std::uint32_t box_depth = 0;
        int row_id = -1;
        int col_index = -1;
        int layer = 0;
        bool disabled = false, suppressed = false, in_scroll = false;
        std::string modal_id, popup_id, focus_scope_id;
        std::string tip_text;
        std::string context_menu_key;
        std::string remember_name;
        bool is_modal_root = false;
        bool cached_hit = false;
        std::uint64_t cache_hash = 0;
        std::vector<int> align_group_ids;
        std::vector<AutoCustomCmd> custom_cmds;
        // Computed by Pass 1 & Pass 2
        float meas_w = 0, meas_h = 0, baseline = 0;
        float content_height = 0, scroll_offset = 0;
        float content_width = 0, scroll_offset_x = 0;
        Rect rect{};
        Rect content_rect{};
        Rect clip_rect{0.0f, 0.0f, 1e9f, 1e9f};
        Rect clip_rrect{0.0f, 0.0f, 1e9f, 1e9f};
        float est_avail_w = 0;
    };

    void ResetAutoLayoutFrame();
    int AllocAutoNode(AutoNode::Kind kind);
    void CloseAutoNode(int expected_idx = -1);
    float FontLineHeightPx(float scale) const;
    float AutoWidgetTextScale(float scale) const;
    bool HitTestSolvedKey(std::string_view key, bool text = false, bool adjustable = false,
                          bool focusable = true, bool release_anywhere = false);
    void MeasureAutoNode(int idx, float avail_w, float avail_h);
    void ArrangeAutoNode(int idx, const Rect &final_rect, const Rect &parent_clip,
                         bool in_scroll, std::uint32_t root_id, int row_id, int col_idx,
                         float parent_clip_radius = 0.0f,
                         const Rect &parent_clip_rrect = {0.0f, 0.0f, 1e9f, 1e9f});
    void CommitAutoNode(int idx, const Rect &parent_bounds, bool overlay_pass = false,
                        std::vector<int> *overlay_queue = nullptr);
    void ApplyClipRectDirect(const Rect &clip_rect, float clip_radius = 0.0f,
                             const Rect *clip_rrect = nullptr);
    void DrawButtonVisual(const Rect &r, bool hover, bool pressed, bool disabled,
                          std::string_view text, float scale, const Color &btn_c,
                          const Color &hov_c, const Color &prs_c, const Color &brd_c,
                          const Color &txt_c, float radius = 0.0f, float hover_t = 0.0f,
                          float press_t = 0.0f, float alpha = 1.0f,
                          const AutoNode *style_node = nullptr);
    void DrawProgressVisual(const Rect &r, float value, const Color &fill, const Color &track,
                            float radius = 0.0f, float alpha = 1.0f,
                            const AutoNode *style_node = nullptr);
    void DrawSliderVisual(const Rect &r, float value, float min_v, float max_v, bool hover,
                          bool dragging, float radius = 0.0f, float hover_t = 0.0f,
                          float alpha = 1.0f);
    void DrawCheckboxVisual(const Rect &r, bool checked, std::string_view label, float scale,
                            bool hover, bool radio, float radius = 0.0f, float hover_t = 0.0f,
                            float alpha = 1.0f);
    void DrawTabVisual(const Rect &r, std::string_view label, bool active, bool hover,
                       bool pressed, float scale, float radius = 0.0f, float hover_t = 0.0f,
                       float alpha = 1.0f);
    void DrawTreeVisual(const Rect &r, std::string_view label, bool open, bool hover, float scale,
                        float radius = 0.0f, float alpha = 1.0f);
    void DrawComboVisual(const Rect &r, std::string_view label, bool open, bool selected,
                         bool hover, float scale, bool is_item, float radius = 0.0f,
                         float hover_t = 0.0f, float alpha = 1.0f);
    void DrawScrollbarVisual(const Rect &viewport, float content_h, float offset, bool hover,
                             bool dragging, float radius = 0.0f, float alpha = 1.0f);
    void DrawHScrollbarVisual(const Rect &viewport, float content_w, float offset_x, bool hover,
                              bool dragging, float radius = 0.0f, float alpha = 1.0f);
    void DrawRoundedQuad(const Rect &r, float radius, const Color &color);
    void DrawRoundedGradientQuad(const Rect &r, float radius, const Color &start_col,
                                 const Color &end_col, bool horizontal = false);
    void DrawRoundedFrame(const Rect &r, float radius, float thickness, const Color &color);
    void DrawDropShadow(const Rect &r, float radius, float off_y, float spread, float alpha);
    void DrawNodeBackdrop(const Rect &r, const AutoNode &node);
    void DrawNodeBorderAccents(const Rect &r, const AutoNode &node);
    void DrawNodeChromeOverlay(const Rect &r, const AutoNode &node);
    void StepWidgetTransition(WidgetState &st, bool hover, bool pressed, bool focused);
    void EnsureFocusedVisible();
    void ProcessDragAndDrop();
    void UpdateImeCaret(const Rect &caret_logical_rect);
    void DrawInspectorOverlay();
    UISemanticNode BuildSemanticNode(int idx) const;
    [[nodiscard]] std::string ResolveKeyLookup(std::string_view key) const;
    Rect ApplyTransitionBox(const AutoNode &node, const Rect &target_rect);
    void CaptureSubtreeCache(int cached_box_idx);
    bool ReplaySubtreeCache(int cached_box_idx, float origin_x, float origin_y,
                            const Rect &parent_clip, float parent_clip_radius, bool in_scroll,
                            std::uint32_t root_id);

    // Auto-layout entry points called from UISystem methods when InAutoLayout()
    float AutoText(std::string_view text, float scale, UITextOverflow overflow, int align,
                   const Color &tint, float min_scale);
    bool AutoButton(std::string_view key, UISize h_spec, std::string_view text, float scale);
    float AutoSlider(std::string_view key, UISize h_spec, float value, float min_v, float max_v);
    bool AutoCheckbox(std::string_view key, UISize sz_spec, bool checked, std::string_view label,
                      float scale, bool is_radio);
    bool AutoTextInput(std::string_view key, UISize h_spec, std::size_t max_chars);
    void AutoProgressBar(UISize h_spec, float value, const Color &fill, const Color &track);
    bool AutoTab(std::string_view key, UISize h_spec, std::string_view label, bool active,
                 float scale);
    bool AutoTree(std::string_view key, UISize h_spec, std::string_view label, bool open,
                  float scale);
    bool AutoCombo(std::string_view key, UISize h_spec, std::string_view label, bool open_or_sel,
                   float scale, bool is_item);
    void AutoImage(const Texture2D *texture, const Rect &src, UISize h_spec, const Color &tint);
    bool AutoImageButton(std::string_view key, const Texture2D *texture, const Rect &src,
                         UISize h_spec, std::string_view label);
    std::string AutoRichText(std::string_view markup, float scale);
    float AutoSplitter(std::string_view key, UISize h_spec, float initial, float minimum);
    bool AutoWindow(std::string_view key, std::string_view title, float scale);
    bool AutoTextArea(std::string_view key, UISize h_spec, std::size_t max_chars);
    bool AutoTextPassword(std::string_view key, UISize h_spec, std::size_t max_chars);
    float MeasureRichTextHeight(std::string_view markup, float scale, float avail_w);
    void DrawRichTextSolved(const AutoNode &node, const Rect &r, const Rect &clip,
                            bool allow_draw = true, bool allow_input = true);

    void ResolveDeepestWheel(int root_idx);
    void ResolveDeferredAutoWidgets();
    struct DeferredRichLink {
        std::string link, key, state_key;
        Rect rr{}, vis{};
        int node_idx = -1;
    };
    std::vector<AutoNode> auto_nodes_;
    std::vector<int> auto_stack_;
    std::vector<int> deferred_auto_nodes_;
    std::vector<DeferredRichLink> deferred_rich_links_;
    int last_auto_node_ = -1;
    AutoLayoutMod next_auto_mod_{};
    int auto_row_seq_ = 0;
    bool in_auto_commit_ = false;
    std::unordered_map<std::string, SolvedWidgetInfo> prev_solved_widgets_;
    std::unordered_map<std::string, SolvedWidgetInfo> curr_solved_widgets_;
    std::unordered_map<std::string, Rect> prev_named_rects_;
    std::vector<std::string> id_stack_;
    Rect ime_cursor_rect_{};
    Rect last_emitted_ime_rect_{};
    std::function<void(const Rect &)> ime_rect_cb_;
    bool inspector_enabled_ = false;
    std::string inspector_selected_key_;
    bool inspector_show_issues_ = false;
    bool inspector_show_semantics_ = false;
    Vec2 inspector_panel_pos_{16.0f, 16.0f};
    bool inspector_dragging_ = false;
    Vec2 inspector_drag_grab_{};
    int desired_cursor_shape_ = 0;
    UILayoutDirection default_layout_dir_ = UILayoutDirection::LTR;
    std::vector<UILayoutDirection> dir_stack_;
    UISafeAreaInsets safe_area_{};
    std::string pending_scroll_key_;
    bool pending_scroll_smooth_ = true;
    std::vector<std::string> pending_drop_targets_;
    std::vector<std::string> pending_drag_sources_;
    bool drag_pending_ = false;
    bool drag_active_ = false;
    Vec2 drag_press_pos_{};
    std::string drag_pending_key_, drag_pending_type_, drag_pending_payload_;
    std::string drag_source_key_, drag_type_, drag_payload_;
    std::string drop_hovered_key_;
    struct DropPayloadRecord {
        std::string type;
        std::string payload;
    };
    std::unordered_map<std::string, DropPayloadRecord> drop_results_;
    std::vector<std::string> open_menu_path_;
    std::vector<std::string> menu_stack_;
    std::string hovered_menu_key_;
    float hovered_menu_timer_ = 0.0f;
    bool menu_bar_active_ = false;
    float last_frame_clock_ = 0.0f;
    float frame_dt_ = 1.0f / 60.0f;
    struct AnimBoxState {
        Rect current{}, target{};
        float elapsed = 0.0f, duration = 0.0f;
        bool init = false;
    };
    std::unordered_map<std::string, AnimBoxState> anim_boxes_;
    struct VirtualScope {
        std::string key;
        int total = 0, first = 0, count = 0;
        float item_h = 24.0f;
    };
    std::vector<VirtualScope> virtual_stack_;
    VirtualRange last_virtual_range_{};
    struct CachedLayoutSnapshot {
        std::uint64_t hash = 0;
        float est_avail_w = -1.0f;
        float font_scale = -1.0f;
        float origin_x = 0.0f, origin_y = 0.0f;
        float meas_w = 0.0f, meas_h = 0.0f, baseline = 0.0f;
        std::vector<AutoNode> nodes; // subtree nodes (root at index 0, indices relative to 0)
    };
    std::unordered_map<std::string, CachedLayoutSnapshot> layout_cache_;
};

/// Cached file-backed `Texture2D` loader with negative memoization.
class UITextureCache {
  public:
    const Texture2D *GetOrLoad(const std::string &name,
                               const std::string &root_dir,
                               const std::string &default_subdir = "assets/ui/");
    void Clear();
    /// 按名字前缀驱逐（如开屏大图进游戏后不再绘制）：纹理析构须在 GL context current 时调用；
    /// 被驱逐的名字同时从 failed_ 摘除，之后 GetOrLoad 可按需重载（如退回主菜单）。
    void EvictPrefix(std::string_view prefix);

  private:
    std::unordered_map<std::string, std::unique_ptr<Texture2D>> cache_;
    std::unordered_map<std::string, bool> failed_;
};

/// Scroll-clipped tooltip entry + occluder hit-testing stack.
class UITipTracker {
  public:
    struct ScrollScope {
        std::string key;
        Rect viewport{};
        float offset = 0.0f;
    };

    void BeginFrame();
    void PushScroll(std::string key, const Rect &viewport, float offset);
    void PopScroll();
    [[nodiscard]] float CurrentScrollOffset() const;
    [[nodiscard]] Rect ToScreenRect(Rect r) const;

    void RecordEntry(const Rect &r, std::string text);
    void RecordClippedEntry(const Rect &r, std::string text);
    void RecordOccluder(const Rect &r);
    void SetFallbackTip(std::string text) { fallback_tip_ = std::move(text); }
    [[nodiscard]] const std::string &FallbackTip() const { return fallback_tip_; }

    [[nodiscard]] std::string Poll(Vec2 mouse, std::string_view fallback_tip = {});
    [[nodiscard]] const Rect &PolledRect() const { return polled_rect_; }

  private:
    struct TipEntry {
        Rect rect{};
        std::string text;
    };

    std::vector<ScrollScope> scroll_stack_;
    std::vector<TipEntry> entries_;
    Rect polled_rect_{};
    std::string fallback_tip_;
};

/// Ensure `renderer` has at least one primary or OS CJK fallback font loaded,
/// plus fallback fonts registered for missing glyphs. Returns true if usable.
bool EnsureSystemCjkFonts(Renderer &renderer,
                          const std::string &primary_font_path,
                          float pixel_height = 32.0f);

} // namespace fake2d
