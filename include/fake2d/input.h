#pragma once

#include "fake2d/math.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fake2d {

/// Per-frame input snapshot polled from the platform window.
/// Game code reads it; the Engine refreshes it once per frame after event polling.
class Input {
public:
    /// Install window-level callbacks (scroll). Call once after window creation.
    void Attach(void *native_window);
    /// Refresh the snapshot for this frame (edge detection, deltas, wheel).
    void NewFrame(void *native_window);

    // --- keyboard (key names: "left", "space", "a", "f1", "left_shift", ...) ---
    [[nodiscard]] bool KeyDown(std::string_view key) const;
    /// True only on the frame the key went down.
    [[nodiscard]] bool KeyPressed(std::string_view key) const;
    /// True only on the frame the key came back up.
    [[nodiscard]] bool KeyReleased(std::string_view key) const;

    // --- mouse ---
    /// Cursor position in screen pixels, top-left origin.
    [[nodiscard]] Vec2 MousePosition() const { return mouse_pos_; }
    /// Movement since the previous frame.
    [[nodiscard]] Vec2 MouseDelta() const { return mouse_delta_; }
    /// Button index: 0 = left, 1 = right, 2 = middle.
    [[nodiscard]] bool MouseDown(std::int64_t button) const;
    [[nodiscard]] bool MousePressed(std::int64_t button) const;
    /// Accumulated vertical wheel movement this frame (positive = away from user).
    [[nodiscard]] double MouseWheel() const { return wheel_; }

private:
    static constexpr std::size_t kKeyCount = 352;   // GLFW_KEY_LAST + 1
    static constexpr std::size_t kButtonCount = 8;  // GLFW_MOUSE_BUTTON_LAST + 1

    void *window_ = nullptr;
    std::uint8_t keys_[kKeyCount] = {};
    std::uint8_t prev_keys_[kKeyCount] = {};
    std::uint8_t buttons_[kButtonCount] = {};
    std::uint8_t prev_buttons_[kButtonCount] = {};
    Vec2 mouse_pos_{0.0f, 0.0f};
    Vec2 mouse_delta_{0.0f, 0.0f};
    double wheel_ = 0.0;
    double wheel_pending_ = 0.0;
};

} // namespace fake2d
