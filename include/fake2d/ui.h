#pragma once

#include "fake2d/input.h"
#include "fake2d/math.h"
#include "fake2d/renderer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fake2d {

/// Widget anchoring: offsets are measured from the anchored corner/edge of
/// the camera viewport (logical points), so layouts stay put at any window
/// size and DPI scale.
enum class UIAnchor : std::int64_t {
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3,
    Center = 4,
};

/// Immediate-style 2D UI kept per-frame simple: panels and labels are pure
/// draws, widgets are keyed by a string and persist their own interaction
/// state (hover, press, drag, focus, text) across frames.
///
/// Values live in the caller, like every immediate-mode UI: `Slider` returns
/// the updated value, `Checkbox` returns true on the frame it was toggled,
/// and `TextInput`/`ScrollArea` keep their content/offset inside the widget
/// (read them back with `TextValue` / pass the offset in again each frame).
class UISystem {
public:
    /// Reset per-frame state; the engine calls it after the input snapshot.
    void NewFrame();

    void Panel(UIAnchor anchor, float offset_x, float offset_y,
               float w, float h, const Color &fill,
               const Color &border = Color::Clear());

    void Label(UIAnchor anchor, float offset_x, float offset_y,
               std::string_view text, float scale, const Color &tint);

    /// Draw a button and return true exactly on the frame it is clicked.
    /// The label is centered on the button.
    bool Button(std::string_view key, UIAnchor anchor,
                float offset_x, float offset_y, float w, float h,
                std::string_view text, float text_scale = 0.6f);

    /// Horizontal slider: drag the thumb, or click anywhere on the track to
    /// jump there. Returns `value` clamped to [min_value, max_value] (and
    /// clamped for input too, so an out-of-range value snaps into range).
    /// A zero-width range degenerates to min_value.
    [[nodiscard]] float Slider(std::string_view key, UIAnchor anchor,
                               float offset_x, float offset_y, float w, float h,
                               float value, float min_value, float max_value);

    /// Checkbox with an optional right-side label. Returns true exactly on
    /// the frame it was clicked; the caller owns the `checked` value.
    [[nodiscard]] bool Checkbox(std::string_view key, UIAnchor anchor,
                                float offset_x, float offset_y, float size,
                                bool checked, std::string_view label = {},
                                float text_scale = 0.6f);

    /// Single-line text field. Click to focus (which also unfocuses every
    /// other field), then it swallows this frame's typed codepoints and
    /// handles backspace. Returns true when the text changed.
    /// `max_chars` counts whole characters, not bytes; 0 means unlimited.
    [[nodiscard]] bool TextInput(std::string_view key, UIAnchor anchor,
                                 float offset_x, float offset_y, float w,
                                 float h, std::size_t max_chars = 24);
    /// Current content of a text field ("" for an unknown key).
    [[nodiscard]] std::string_view TextValue(std::string_view key) const;
    /// Overwrite a text field's content (also used to clear it).
    void SetTextValue(std::string_view key, std::string_view text);
    /// Currently focused text field, or "" when none is.
    [[nodiscard]] std::string_view FocusedTextInput() const { return focused_; }

    /// Vertical scroll container: consumes the wheel while hovered and lets
    /// the thumb be dragged, returning `scroll_y` clamped to
    /// [0, content_height - h] (0 when the content fits). The scrollbar is
    /// drawn along the right edge; **the caller clips the content** with
    /// Renderer::SetClipRect and draws items at `y - scroll_y`.
    [[nodiscard]] float ScrollArea(std::string_view key, UIAnchor anchor,
                                   float offset_x, float offset_y, float w,
                                   float h, float content_height, float scroll_y);

    [[nodiscard]] bool IsHovered(std::string_view key) const;

    /// Wire the per-frame input snapshot, camera (viewport) and renderer.
    void Bind(const Input *input, const Camera2D *camera, Renderer *renderer);

    /// When widgets are placed in world coordinates under a scrolling camera,
    /// set its top-left world position so mouse hit-testing shifts with it.
    /// Zero (default) keeps widgets in screen space.
    void SetWorldOrigin(const Vec2 &origin) { world_origin_ = origin; }

private:
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

    [[nodiscard]] Vec2 Resolve(UIAnchor anchor, float offset_x, float offset_y,
                               float w, float h, float viewport_w, float viewport_h) const;
    [[nodiscard]] WidgetState *Find(std::string_view key);
    /// Mouse in the same space widgets are laid out in (screen + world origin).
    [[nodiscard]] Vec2 Mouse() const;
    /// Render a string, trimming the head if it overflows `w` so the tail
    /// (what was typed last) stays visible. `w <= 0` disables trimming.
    /// Returns the pen x just past the last drawn glyph (caret position).
    float DrawFittedText(std::string_view text, float x, float y, float w,
                         float scale, const Color &tint);

    const Input *input_ = nullptr;
    const Camera2D *camera_ = nullptr;
    Renderer *renderer_ = nullptr;
    Vec2 world_origin_{0.0f, 0.0f};
    std::unordered_map<std::string, WidgetState> widgets_;
    std::string focused_;
};

} // namespace fake2d
