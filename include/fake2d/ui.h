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

/// Immediate-style 2D UI kept per-frame simple: panels and text are pure
/// draws, widgets are keyed by a string and persist their own interaction
/// state (hover, press, drag, focus, text) across frames.
///
/// Values live in the caller. Within BeginFrameUI/EndFrameUI consume results after End.
/// Legacy unscoped calls remain source-compatible: `Slider` returns
/// the updated value, `Checkbox` returns true on the frame it was toggled,
/// and text inputs / scroll boxes keep text and offset keyed by the widget
/// (read them back with `TextValue` / `ScrollOffset`).

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
    bool ListView(std::string_view key, float height);
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

    bool TextPassword(std::string_view key, float height, std::size_t max_chars = 128);
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
    bool VariableListBegin(std::string_view key, float view_h, bool pin_bottom = false);
    int VariableListNext(); // opens an auto-height row; -1 when viewport filled
    float VariableListRowEnd();
    void VariableListEnd();
    [[nodiscard]] float VariableListTotal(std::string_view key) const;
    [[nodiscard]] std::string VariableListAnchor(std::string_view key) const;
    [[nodiscard]] std::string VariableListID(std::string_view key, int index) const;
    [[nodiscard]] float VariableListHeight(std::string_view key, std::string_view row_id) const;
    [[nodiscard]] bool VariableListMeasured(std::string_view key, std::string_view row_id) const;

    // Phase 57+: practical game UI, without an editor or retained widget tree.
    struct Constraints {
        float min_w = 0, max_w = 1e9f, min_h = 0, max_h = 1e9f;
    };
    void Constrain(float min_w, float max_w, float min_h = 0, float max_h = 1e9f);
    void RowAlign(int align); // 0 top, 1 center, 2 bottom; before Col()
    void FlowBegin(float gap = -1, float line_gap = -1);
    void FlowItem(float width); // end each item with FlowItemEnd
    void FlowItemEnd();
    void FlowEnd();
    void GridBegin(int columns, float gap = -1, const std::vector<float> &widths = {});
    void GridCell();
    void GridCellEnd();
    void GridEnd();
    float Splitter(std::string_view key, float height, float initial, float minimum = 24);
    bool SetScale(float reference_w, float reference_h, std::string_view mode = "contain");
    [[nodiscard]] float Scale() const { return scale_; }
    void Remember(std::string_view name);
    [[nodiscard]] Rect NamedRect(std::string_view name) const;
    // Relation edges: left/right/top/bottom/center_x/center_y; empty target = parent.
    bool PushRelated(std::string_view target, std::string_view edge, float offset, float width,
                     float height, float padding = 0, float right_inset = -1);
    [[nodiscard]] float ThemeMetric(std::string_view name) const;
    bool SetThemeMetric(std::string_view name, float value);
    [[nodiscard]] Color ThemeColor(std::string_view name) const;
    bool SetThemeColor(std::string_view name, const Color &value);
    void SetPanelSlice(const Texture2D *texture, const Rect &source, float corner_x,
                       float corner_y);
    void ThemedPanel();
    void DisabledBegin(bool disabled = true);
    void DisabledEnd();
    void Image(const Texture2D *texture, const Rect &source, float height,
               const Color &tint = Color::White());
    bool ImageButton(std::string_view key, const Texture2D *texture, const Rect &source,
                     float height, std::string_view label = {});
    void SetRichIcon(std::string_view name, const Texture2D *texture, const Rect &source);
    std::string RichText(std::string_view markup, float scale = 0); // clicked link id or empty
    bool TextArea(std::string_view key, float height, std::size_t max_chars = 4096);
    void OpenModal(std::string_view key); // May be declared late within an explicit frame
    void CloseModal();
    bool ModalBegin(std::string_view key, float width, float height);
    void ModalEnd();
    void ClosePopup() {
        popup_.clear();
        nav_focus_.clear();
    }
    void SetScrollOffset(std::string_view key, float offset);
    void OpenPopup(std::string_view key, float x, float y);
    bool ContextMenu(std::string_view key); // opens on right press over LastRect
    int Popup(std::string_view key, const std::vector<std::string> &items, float width = 180,
              float item_h = 28);
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

    // Phase 54–56: cursor-box layout, independent of caller-owned values.
    // Nested x/y are relative to the parent's current cursor; Pop reserves y+h.
    void Push(float x, float y, float w, float h, float padding = 0.0f);
    void Pop();
    float Text(std::string_view text, float scale = 0.0f,
               UITextOverflow overflow = UITextOverflow::Wrap, int align = 0,
               const Color &tint = Color::White(), float min_scale = 0.35f);
    [[nodiscard]] float AvailWidth() const;
    [[nodiscard]] Rect LastRect() const { return last_rect_; }
    void Row(float gap = 0.0f, const std::vector<float> &columns = {});
    void Col(float fixed_width = 0.0f);
    void ColEnd();
    void RowEnd();
    // Always measures content from the cursor. Draw commands are deferred
    // until ScrollEnd, so first-frame clamping uses this frame's height.
    void ScrollBegin(std::string_view key, float view_height);
    float ScrollEnd();
    [[nodiscard]] float ScrollContentHeight(std::string_view key) const;
    [[nodiscard]] float ScrollOffset(std::string_view key) const;
    [[nodiscard]] float LayoutToken(std::string_view name) const;
    bool SetLayoutToken(std::string_view name, float value);
    struct AuditRect {
        std::string kind;
        Rect rect;
        Rect bounds;
        bool overflow = false;
    };
    void SetAudit(bool enabled) { audit_enabled_ = enabled; }
    [[nodiscard]] const std::vector<AuditRect> &AuditRects() const { return audit_; }
    [[nodiscard]] std::size_t OverflowCount() const;

    // All widgets use the current box width and advance its cursor by h.
    // Panel and Window decorate the current box without advancing it.
    void Panel(const Color &fill, const Color &border = Color::Clear());
    bool Button(std::string_view key, float h, std::string_view text, float scale = 0.0f);
    float Slider(std::string_view key, float h, float value, float min_value, float max_value);
    bool Checkbox(std::string_view key, float size, bool checked, std::string_view label = {},
                  float scale = 0.0f);
    bool TextInput(std::string_view key, float h, std::size_t max_chars = 24);
    [[nodiscard]] std::string_view TextValue(std::string_view key) const;
    void SetTextValue(std::string_view key, std::string_view text);
    [[nodiscard]] std::string_view FocusedTextInput() const { return focused_; }
    void ProgressBar(float h, float value, const Color &fill,
                     const Color &track = Color{0.14f, 0.17f, 0.24f, 1.0f});
    bool RadioButton(std::string_view key, float size, bool selected, std::string_view label = {},
                     float scale = 0.0f);
    bool ComboBegin(std::string_view key, float h, std::string_view label, bool open,
                    float scale = 0.0f);
    bool ComboItem(std::string_view key, float h, std::string_view label, bool selected,
                   float scale = 0.0f);
    bool Window(std::string_view key, std::string_view title, float scale = 0.0f);
    bool Tab(std::string_view key, float h, std::string_view label, bool active,
             float scale = 0.0f);
    bool Tree(std::string_view key, float h, std::string_view label, bool open, float scale = 0.0f);
    void Tip(std::string_view text); // attaches to LastRect()
    void SetTipDelay(float seconds) { tip_delay_ = std::max(0.0f, seconds); }
    [[nodiscard]] float TipDelay() const { return tip_delay_; }
    void DrawTooltip();

    [[nodiscard]] bool IsHovered(std::string_view key) const;

    /// Wire the per-frame input snapshot, camera (viewport) and renderer.
    void Bind(const Input *input, const Camera2D *camera, Renderer *renderer);

    /// When widgets are placed in world coordinates under a scrolling camera,
    /// set its top-left world position so mouse hit-testing shifts with it.
    /// Zero (default) keeps widgets in screen space.
    void SetWorldOrigin(const Vec2 &origin) { world_origin_ = origin; }

  private:
    // Internal painters are not a second public placement/layout API.
    void PaintPanel(float offset_x, float offset_y, float w, float h, const Color &fill,
                    const Color &border = Color::Clear());
    void PaintTip(float offset_x, float offset_y, float w, float h, std::string_view text);
    bool PaintButton(std::string_view key, float offset_x, float offset_y, float w, float h,
                     std::string_view text, float text_scale = 0.6f);
    [[nodiscard]] float PaintSlider(std::string_view key, float offset_x, float offset_y, float w,
                                    float h, float value, float min_value, float max_value);
    [[nodiscard]] bool PaintCheckbox(std::string_view key, float offset_x, float offset_y,
                                     float size, bool checked, std::string_view label = {},
                                     float text_scale = 0.6f);
    [[nodiscard]] float PaintScrollArea(std::string_view key, float offset_x, float offset_y,
                                        float w, float h, float content_height, float scroll_y,
                                        bool wheel_enabled);
    void PaintProgressBar(float offset_x, float offset_y, float w, float h, float value,
                          const Color &fill, const Color &track = Color{0.14f, 0.17f, 0.24f, 1.0f});
    [[nodiscard]] bool PaintRadioButton(std::string_view key, float offset_x, float offset_y,
                                        float size, bool selected, std::string_view label = {},
                                        float text_scale = 0.6f);
    [[nodiscard]] bool PaintComboBegin(std::string_view key, float offset_x, float offset_y,
                                       float w, float h, std::string_view label, bool open,
                                       float text_scale = 0.6f);
    [[nodiscard]] bool PaintComboItem(std::string_view key, float offset_x, float offset_y, float w,
                                      float h, std::string_view label, bool selected,
                                      float text_scale = 0.6f);
    [[nodiscard]] bool PaintWindow(std::string_view key, float offset_x, float offset_y, float w,
                                   float h, std::string_view title, float text_scale = 0.6f);
    [[nodiscard]] bool PaintTab(std::string_view key, float offset_x, float offset_y, float w,
                                float h, std::string_view label, bool active,
                                float text_scale = 0.6f);
    [[nodiscard]] bool PaintTree(std::string_view key, float offset_x, float offset_y, float w,
                                 float h, std::string_view label, bool open,
                                 float text_scale = 0.6f);
    Rect Allocate(float height, std::string_view kind);
    void Quad(const Rect &rect, const Color &tint);
    float String(std::string_view text, float x, float y, float scale, const Color &tint,
                 bool clipped = false, bool kerning = true);
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

    [[nodiscard]] WidgetState *Find(std::string_view key);
    /// Mouse in the same space widgets are laid out in (screen + world origin).
    [[nodiscard]] Vec2 Mouse() const;
    [[nodiscard]] bool MouseVisible() const;
    /// Fit a caption with ellipsis, or keep a UTF-8-safe editable tail.
    /// Zero width draws nothing. Returns the caret / right-edge x.
    float DrawFittedText(std::string_view text, float x, float y, float w, float scale,
                         const Color &tint, bool tail = false);

    struct LayoutClip {
        Rect rect;
        std::vector<std::size_t> scrolls;
        std::shared_ptr<Rect> measured{};
    };
    struct LayoutBox {
        Rect content;
        float cursor = 0.0f;
        float reserve = 0.0f;
        bool auto_height = false, overlay = false;
        std::size_t audit_start = 0;
        float padding = 0, y_offset = 0;
        Constraints limits;
        std::shared_ptr<Rect> measured;
        std::vector<LayoutClip> clips;
    };
    struct LayoutRow {
        std::size_t depth = 0;
        float gap = 0, x = 0, height = 0;
        std::vector<float> widths;
        std::size_t index = 0;
        int align = 0;
        std::size_t cache_id = 0;
        std::vector<std::pair<std::size_t, float>> offsets;
    };
    struct ScrollState {
        float height = 0, offset = 0;
    };
    struct LayoutScroll {
        std::string key;
        Rect viewport;
        std::size_t index = 0, depth = 0;
    };
    void PushAbsolute(const Rect &rect, float padding, float reserve);
    void ApplyLayoutClip(const std::vector<LayoutClip> &clips);
    void Record(std::string_view kind, const Rect &rect);
    std::string Ellipsize(std::string_view text, float width, float scale) const;
    float ScrollTranslation(const std::vector<std::size_t> &ids) const;
    std::vector<LayoutBox> boxes_;
    std::vector<LayoutRow> rows_;
    std::vector<std::vector<float>> row_offset_cache_;
    std::size_t row_sequence_ = 0;
    std::vector<LayoutScroll> scrolls_;
    std::vector<std::size_t> active_scrolls_;
    std::vector<float> scroll_offsets_;
    std::vector<std::function<void()>> pending_draws_;
    std::vector<std::function<void()>> scroll_resolvers_;
    std::unordered_map<std::string, ScrollState> scroll_states_;
    std::unordered_map<std::string, float> tokens_{
        {"panel_width", 600.0f}, {"sidebar_inset", 410.0f}, {"menu_icon_spacing_cap", 64.0f}};
    struct InteractionNode {
        std::string key, modal, popup;
        Rect rect;
        LayoutBox box;
        std::vector<std::size_t> translations;
        bool disabled = false, text = false, adjustable = false, focusable = true;
        std::function<void()> action;
        std::shared_ptr<std::vector<std::function<void()>>> paints;
        int pointer_button = 0;
    };
    void QueueInteraction(std::string_view key, const Rect &rect, std::function<void()> action,
                          bool text = false, bool adjustable = false, bool focusable = true);
    void NavigateCurrent();
    void ResolveScrollLayout();
    struct ListViewport {
        LayoutBox box;
        std::vector<std::size_t> translations;
        std::string modal, popup;
        bool disabled = false, submitted = false;
        std::shared_ptr<std::vector<std::function<void()>>> paints;
    };
    std::unordered_map<std::string, ListViewport> list_views_;
    std::string active_list_view_;
    std::size_t list_view_paint_start_ = 0, draw_placeholders_ = 0;
    bool layout_resolved_ = false, dismissed_overlay_ = false, resolving_allowed_ = true;
    std::string text_activated_;
    std::function<double()> interaction_clock_;
    bool recording_ = false, resolving_ = false, frame_submitted_ = false;
    bool scroll_layout_only_ = false, scroll_input_only_ = false;
    std::vector<std::size_t> prescrolled_;
    struct ScrollCandidate {
        std::size_t index, depth;
        Rect viewport;
        std::vector<LayoutClip> clips;
        std::vector<std::size_t> parents;
        bool disabled, modal, popup;
    };
    std::vector<ScrollCandidate> scroll_candidates_;
    std::vector<InteractionNode> interactions_;
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
        std::size_t depth;
        bool row_open = false, pin_bottom = false;
        struct SubmittedRow {
            int index;
            std::size_t translation;
            float original_start;
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
    void FlushDraws();
    bool CanInteract() const;
    bool FocusWidget(std::string_view key, const Rect &r, bool text = false,
                     bool adjustable = false);
    void FocusIndicator(std::string_view key, const Rect &r);
    Rect MapRect(Rect r) const;
    Vec2 RawMouse() const;
    void Sprite(const Texture2D *texture, const Rect &src, const Rect &dst, const Color &tint);
    bool EditText(std::string_view key, const Rect &r, std::size_t max_chars, bool multiline,
                  bool password = false);
    Constraints next_limits_;
    int deferred_ = 0;
    struct FlowState {
        std::size_t depth;
        float gap, line_gap, x = 0, height = 0;
        int columns = 0, index = 0;
        std::vector<float> widths;
    };
    std::vector<FlowState> flows_;
    std::vector<bool> disabled_;
    bool IsDisabled() const { return !disabled_.empty() && disabled_.back(); }
    float scale_ = 1;
    float reference_w_ = 0, reference_h_ = 0;
    std::string scale_mode_ = "contain";
    std::vector<std::vector<std::size_t>> overlay_scrolls_;
    Vec2 scale_offset_{};
    std::unordered_map<std::string, Rect> named_rects_;
    std::unordered_map<std::string, float> theme_metrics_{{"padding", 8},
                                                          {"spacing", 8},
                                                          {"font_scale", .6f},
                                                          {"min_font_scale", .35f},
                                                          {"border", 2}};
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
    };
    std::vector<ListState> lists_;
    int list_count_ = 0;
    struct TweenState {
        float value = 0, from = 0, target = 0, elapsed = 0;
    };
    std::unordered_map<std::string, TweenState> tweens_;
    std::unordered_map<std::string, int> pages_;
    Rect last_rect_{};
    Rect paint_bounds_{};
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
};

} // namespace fake2d
