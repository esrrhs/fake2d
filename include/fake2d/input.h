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
    /// True only on the frame the button came back up.
    [[nodiscard]] bool MouseReleased(std::int64_t button) const;
    /// Accumulated vertical wheel movement this frame (positive = away from user).
    [[nodiscard]] double MouseWheel() const { return wheel_; }

    // --- gamepad (GLFW standard mapping, Xbox layout) ---
    // pad is a 0-based joystick slot; disconnected pads read as neutral.
    /// True when a gamepad (Standard Mapping recognized) is present on `pad`.
    [[nodiscard]] bool GamepadConnected(int pad) const;
    /// Button names: "a","b","x","y","left_bumper","right_bumper","back",
    /// "start","guide","left_thumb","right_thumb",
    /// "dpad_up","dpad_right","dpad_down","dpad_left".
    [[nodiscard]] bool GamepadButtonDown(int pad, std::string_view button) const;
    [[nodiscard]] bool GamepadButtonPressed(int pad, std::string_view button) const;
    [[nodiscard]] bool GamepadButtonReleased(int pad, std::string_view button) const;
    /// Axis names: "left_x","left_y","right_x","right_y" (-1..1, radial dead
    /// zone removed), "left_trigger","right_trigger" (0..1).
    [[nodiscard]] float GamepadAxis(int pad, std::string_view axis) const;
    /// GLFW-reported gamepad GUID/name, or "" when disconnected.
    [[nodiscard]] std::string_view GamepadName(int pad) const;

private:
    static constexpr std::size_t kKeyCount = 352;   // GLFW_KEY_LAST + 1
    static constexpr std::size_t kButtonCount = 8;  // GLFW_MOUSE_BUTTON_LAST + 1
    static constexpr int kPadCount = 16;            // GLFW_JOYSTICK_LAST + 1
    static constexpr int kPadButtonCount = 15;      // GLFW_GAMEPAD_BUTTON_LAST + 1
    static constexpr int kPadAxisCount = 6;         // GLFW_GAMEPAD_AXIS_LAST + 1

    void *window_ = nullptr;
    std::uint8_t keys_[kKeyCount] = {};
    std::uint8_t prev_keys_[kKeyCount] = {};
    std::uint8_t buttons_[kButtonCount] = {};
    std::uint8_t prev_buttons_[kButtonCount] = {};
    std::uint8_t pad_connected_[kPadCount] = {};
    std::uint8_t pad_buttons_[kPadCount][kPadButtonCount] = {};
    std::uint8_t prev_pad_buttons_[kPadCount][kPadButtonCount] = {};
    float pad_axes_[kPadCount][kPadAxisCount] = {};
    Vec2 mouse_pos_{0.0f, 0.0f};
    Vec2 mouse_delta_{0.0f, 0.0f};
    double wheel_ = 0.0;
    double wheel_pending_ = 0.0;
};

} // namespace fake2d
