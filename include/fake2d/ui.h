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
/// draws, buttons are keyed widgets whose hover/pressed state persists
/// across frames and return "clicked this frame" on a left-button release
/// while hovered (classic button semantics).
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

    [[nodiscard]] bool IsHovered(std::string_view key) const;

    /// Wire the per-frame input snapshot, camera (viewport) and renderer.
    void Bind(const Input *input, const Camera2D *camera, Renderer *renderer);

    /// When widgets are placed in world coordinates under a scrolling camera,
    /// set its top-left world position so mouse hit-testing shifts with it.
    /// Zero (default) keeps widgets in screen space.
    void SetWorldOrigin(const Vec2 &origin) { world_origin_ = origin; }

private:
    struct ButtonState {
        bool hover = false;
        bool pressed = false;
    };

    [[nodiscard]] Vec2 Resolve(UIAnchor anchor, float offset_x, float offset_y,
                               float w, float h, float viewport_w, float viewport_h) const;
    [[nodiscard]] ButtonState *Find(std::string_view key);

    const Input *input_ = nullptr;
    const Camera2D *camera_ = nullptr;
    Renderer *renderer_ = nullptr;
    Vec2 world_origin_{0.0f, 0.0f};
    std::unordered_map<std::string, ButtonState> buttons_;
};

} // namespace fake2d
