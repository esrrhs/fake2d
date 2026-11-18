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

enum class UIAxisAlign : std::uint8_t { Start, Center, End, Stretch };

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
    void ScrollBoxEnd();
    void FlowBoxBegin(float col_gap = -1.0f, float row_gap = -1.0f);
    void FlowBoxEnd();
    void CanvasBegin(UISize w = UISize::Grow(), UISize h = UISize::Auto(), float pad = 0.0f);
    void CanvasEnd();
    void Spacer(float px = -1.0f);
    void Divider(const Color &color, float thickness = 1.0f);
    bool ModalBegin(std::string_view key, UISize w = UISize::Auto(), UISize h = UISize::Auto());
    bool ListView(std::string_view key, UISize view_h = UISize::Grow());

    // One-shot layout modifiers applied to the next child/container in Two-Pass layout.
    void NextSize(UISize w, UISize h);
    void NextWidth(UISize w);
    void NextHeight(UISize h);
    void NextGrow(float weight = 1.0f);
    void NextAlign(UIAxisAlign cross_align);
    void NextMainAlign(UIAxisAlign main_align);
    void NextPad(float l, float t, float r, float b);

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
        struct EditSnapshot {
            std::string text;
            std::size_t caret = 0, anchor = 0;
        };
        std::vector<EditSnapshot> undo, redo;
        bool selecting = false;
        double last_click = -1;
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
                  bool password = false, bool allow_draw = true, bool allow_input = true);
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
                                                          {"scrollbar_width", 12}};
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
    };
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

    // --- Two-Pass Auto-Layout Engine (ui_layout2.cpp) ---
    struct AutoLayoutMod {
        bool has_w = false, has_h = false, has_cross = false, has_main = false, has_pad = false;
        UISize w{}, h{};
        UIAxisAlign cross = UIAxisAlign::Start;
        UIAxisAlign main = UIAxisAlign::Start;
        float pad_l = 0, pad_t = 0, pad_r = 0, pad_b = 0;
    };
    struct SolvedWidgetInfo {
        Rect rect{};
        Rect clip_rect{};
        int layer = 0;
        bool disabled = false;
        std::string modal, popup;
    };
    struct AutoCustomCmd {
        enum class Op : std::uint8_t { Quad, Sprite, NineSlice, Text, WrappedText };
        Op op = Op::Quad;
        Rect rect{};
        Color fill{}, border{};
        const Texture2D *texture = nullptr;
        Rect src{};
        std::string text;
        float px = 0, corner_x = 0, corner_y = 0;
        int align = 0, face = 0;
    };
    struct AutoNode {
        enum class Kind : std::uint8_t {
            Root, AnchorRoot, VStack, HStack, Table, TableRow, TableCell,
            Flow, Scroll, Card, CustomBox, Spacer, Divider,
            Text, RichText, Button, Slider, Checkbox, RadioButton, ProgressBar,
            TextInput, TextPassword, TextArea, Tab, Tree, ComboBegin, ComboItem,
            Image, ImageButton, Splitter, Window, ListViewSlot
        };
        Kind kind = Kind::VStack;
        int parent = -1;
        std::vector<int> children;
        std::string key;
        std::string text;
        UISize width_spec = UISize::Auto();
        UISize height_spec = UISize::Auto();
        Constraints limits{};
        float pad_l = 0, pad_t = 0, pad_r = 0, pad_b = 0;
        float gap = 0, line_gap = 0;
        UIAxisAlign cross_align = UIAxisAlign::Stretch;
        UIAxisAlign main_align = UIAxisAlign::Start;
        bool clip_children = false;
        bool block_input = false;
        bool overlay = false;
        bool use_screen_origin = false;
        bool has_clip_override = false;
        bool defer_commit = false;
        Rect clip_override{};
        bool draw_panel = false, themed_panel = false;
        Color fill_col{}, border_col{}, track_col{};
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
        std::string modal_id, popup_id;
        std::string tip_text;
        std::string context_menu_key;
        std::string remember_name;
        bool is_modal_root = false;
        std::vector<int> align_group_ids;
        std::vector<AutoCustomCmd> custom_cmds;
        // Computed by Pass 1 & Pass 2
        float meas_w = 0, meas_h = 0;
        float content_height = 0, scroll_offset = 0;
        Rect rect{};
        Rect content_rect{};
        Rect clip_rect{0.0f, 0.0f, 1e9f, 1e9f};
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
                         bool in_scroll, std::uint32_t root_id, int row_id, int col_idx);
    void CommitAutoNode(int idx, const Rect &parent_bounds);
    void ApplyClipRectDirect(const Rect &clip_rect);
    void DrawButtonVisual(const Rect &r, bool hover, bool pressed, bool disabled,
                          std::string_view text, float scale, const Color &btn_c,
                          const Color &hov_c, const Color &prs_c, const Color &brd_c,
                          const Color &txt_c);
    void DrawProgressVisual(const Rect &r, float value, const Color &fill, const Color &track);
    void DrawSliderVisual(const Rect &r, float value, float min_v, float max_v, bool hover,
                          bool dragging);
    void DrawCheckboxVisual(const Rect &r, bool checked, std::string_view label, float scale,
                            bool hover, bool radio);
    void DrawTabVisual(const Rect &r, std::string_view label, bool active, bool hover,
                       bool pressed, float scale);
    void DrawTreeVisual(const Rect &r, std::string_view label, bool open, bool hover, float scale);
    void DrawComboVisual(const Rect &r, std::string_view label, bool open, bool selected,
                         bool hover, float scale, bool is_item);
    void DrawScrollbarVisual(const Rect &viewport, float content_h, float offset, bool hover,
                             bool dragging);

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
    void DrawRichTextSolved(const AutoNode &node, const Rect &r, const Rect &clip);

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
