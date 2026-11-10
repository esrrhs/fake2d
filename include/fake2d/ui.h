#pragma once

#include "fake2d/input.h"
#include "fake2d/math.h"
#include "fake2d/renderer.h"

#include <algorithm>
#include <cstdint>
#include <functional>
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
/// Values live in the caller, like every immediate-mode UI: `Slider` returns
/// the updated value, `Checkbox` returns true on the frame it was toggled,
/// and text inputs / scroll boxes keep text and offset keyed by the widget
/// (read them back with `TextValue` / `ScrollOffset`).

class UISystem {
public:
    /// Reset per-frame state; the engine calls it after the input snapshot.
    void NewFrame();

    // Phase 54–56: cursor-box layout, independent of caller-owned values.
    // Nested x/y are relative to the parent's current cursor; Pop reserves y+h.
    void Push(float x, float y, float w, float h, float padding = 0.0f);
    void Pop();
    float Text(std::string_view text, float scale = 0.6f,
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
    bool Button(std::string_view key, float h, std::string_view text, float scale = 0.6f);
    float Slider(std::string_view key, float h, float value, float min_value, float max_value);
    bool Checkbox(std::string_view key, float size, bool checked,
                  std::string_view label = {}, float scale = 0.6f);
    bool TextInput(std::string_view key, float h, std::size_t max_chars = 24);
    [[nodiscard]] std::string_view TextValue(std::string_view key) const;
    void SetTextValue(std::string_view key, std::string_view text);
    [[nodiscard]] std::string_view FocusedTextInput() const { return focused_; }
    void ProgressBar(float h, float value, const Color &fill,
                     const Color &track = Color{0.14f, 0.17f, 0.24f, 1.0f});
    bool RadioButton(std::string_view key, float size, bool selected,
                     std::string_view label = {}, float scale = 0.6f);
    bool ComboBegin(std::string_view key, float h, std::string_view label,
                    bool open, float scale = 0.6f);
    bool ComboItem(std::string_view key, float h, std::string_view label,
                   bool selected, float scale = 0.6f);
    bool Window(std::string_view key, std::string_view title, float scale = 0.6f);
    bool Tab(std::string_view key, float h, std::string_view label,
             bool active, float scale = 0.6f);
    bool Tree(std::string_view key, float h, std::string_view label,
              bool open, float scale = 0.6f);
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
    void PaintPanel(float offset_x, float offset_y,
               float w, float h, const Color &fill,
               const Color &border = Color::Clear());
    void PaintTip(float offset_x, float offset_y, float w, float h,
             std::string_view text);
    bool PaintButton(std::string_view key,
                float offset_x, float offset_y, float w, float h,
                std::string_view text, float text_scale = 0.6f);
    [[nodiscard]] float PaintSlider(std::string_view key,
                               float offset_x, float offset_y, float w, float h,
                               float value, float min_value, float max_value);
    [[nodiscard]] bool PaintCheckbox(std::string_view key,
                                float offset_x, float offset_y, float size,
                                bool checked, std::string_view label = {},
                                float text_scale = 0.6f);
    [[nodiscard]] bool PaintTextInput(std::string_view key,
                                 float offset_x, float offset_y, float w,
                                 float h, std::size_t max_chars = 24);
    [[nodiscard]] float PaintScrollArea(std::string_view key,
                                   float offset_x, float offset_y, float w,
                                   float h, float content_height, float scroll_y, bool wheel_enabled);
    void PaintProgressBar(float offset_x, float offset_y, float w,
                     float h, float value, const Color &fill,
                     const Color &track = Color{0.14f, 0.17f, 0.24f, 1.0f});
    [[nodiscard]] bool PaintRadioButton(std::string_view key,
                                   float offset_x, float offset_y, float size,
                                   bool selected, std::string_view label = {},
                                   float text_scale = 0.6f);
    [[nodiscard]] bool PaintComboBegin(std::string_view key,
                                  float offset_x, float offset_y, float w,
                                  float h, std::string_view label, bool open,
                                  float text_scale = 0.6f);
    [[nodiscard]] bool PaintComboItem(std::string_view key,
                                 float offset_x, float offset_y, float w,
                                 float h, std::string_view label, bool selected,
                                 float text_scale = 0.6f);
    [[nodiscard]] bool PaintWindow(std::string_view key,
                              float offset_x, float offset_y, float w, float h,
                              std::string_view title, float text_scale = 0.6f);
    [[nodiscard]] bool PaintTab(std::string_view key,
                           float offset_x, float offset_y, float w, float h,
                           std::string_view label, bool active,
                           float text_scale = 0.6f);
    [[nodiscard]] bool PaintTree(std::string_view key,
                            float offset_x, float offset_y, float w, float h,
                            std::string_view label, bool open,
                            float text_scale = 0.6f);
    Rect Allocate(float height, std::string_view kind);
    void Quad(const Rect &rect, const Color &tint);
    float String(std::string_view text, float x, float y, float scale, const Color &tint);
    /// One state record serves every widget kind; the fields a given kind
    /// ignores simply stay at their default.
    struct WidgetState {
        bool hover = false;
        bool pressed = false;  // button held, thumb grabbed
        bool dragging = false; // slider / scrollbar thumb being dragged
        /// Grab offset from the thumb's top-left, so dragging does not make
        /// the thumb jump under the cursor.
        float grab = 0.0f;
        std::string text;      // text field content
    };

    [[nodiscard]] WidgetState *Find(std::string_view key);
    /// Mouse in the same space widgets are laid out in (screen + world origin).
    [[nodiscard]] Vec2 Mouse() const;
    [[nodiscard]] bool MouseVisible() const;
    /// Fit a caption with ellipsis, or keep a UTF-8-safe editable tail.
    /// Zero width draws nothing. Returns the caret / right-edge x.
    float DrawFittedText(std::string_view text, float x, float y, float w,
                         float scale, const Color &tint, bool tail = false);

    struct LayoutClip {
        Rect rect;
        std::vector<std::size_t> scrolls;
    };
    struct LayoutBox {
        Rect content;
        float cursor = 0.0f;
        float reserve = 0.0f;
        std::vector<LayoutClip> clips;
    };
    struct LayoutRow {
        std::size_t depth = 0;
        float gap = 0, x = 0, height = 0;
        std::vector<float> widths;
        std::size_t index = 0;
    };
    struct ScrollState { float height = 0, offset = 0; };
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
    std::vector<LayoutScroll> scrolls_;
    std::vector<std::size_t> active_scrolls_;
    std::vector<float> scroll_offsets_;
    std::vector<std::function<void()>> pending_draws_;
    std::vector<std::function<void()>> scroll_resolvers_;
    std::unordered_map<std::string, ScrollState> scroll_states_;
    std::unordered_map<std::string, float> tokens_ {
        {"panel_width", 600.0f}, {"sidebar_inset", 410.0f},
        {"menu_icon_spacing_cap", 64.0f}};
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
