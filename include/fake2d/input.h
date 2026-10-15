#pragma once

#include "fake2d/math.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

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
    /// Unicode codepoints typed this frame, in arrival order — everything a
    /// text field needs. GLFW's char callback already applies the active
    /// keyboard layout and modifiers, so shift+A arrives as 'A'. Consumed at
    /// NewFrame, so a frame sees exactly what arrived since the last one.
    /// Editing keys such as backspace/enter are NOT included: those arrive as
    /// key events and widgets handle them through KeyPressed.
    [[nodiscard]] const std::vector<unsigned int> &TextCodepoints() const {
        return text_input_;
    }
    /// True when any text arrived this frame.
    [[nodiscard]] bool HasTextInput() const { return !text_input_.empty(); }

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

    // --- synthetic frames ---------------------------------------------------
    /// Fill the snapshot by hand instead of polling a window — headless tests
    /// and input replays have no window to poll. Edge detection behaves the
    /// same as a real frame: whatever was current becomes "previous", so a
    /// button held across two injected frames reads as *pressed* only on the
    /// first and *released* on the frame it stops. A real `NewFrame` (windowed
    /// run) overwrites this state, so injection can never leak into gameplay.
    /// Key names are the same as `KeyDown`'s; unknown names are ignored.
    void InjectFrame(const Vec2 &mouse_pos,
                     const std::vector<std::string_view> &keys_down,
                     const std::vector<std::int64_t> &buttons_down,
                     double wheel = 0.0,
                     const std::vector<unsigned int> &text = {});

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
    static constexpr std::size_t kKeyCount = 349;   // GLFW_KEY_LAST + 1
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
    /// Codepoints handed to the current frame; the callback fills the pending
    /// buffer, which NewFrame swaps in (see TextCodepoints).
    std::vector<unsigned int> text_input_;
    std::vector<unsigned int> text_pending_;
};

} // namespace fake2d
