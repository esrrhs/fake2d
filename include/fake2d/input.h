#pragma once

#include "fake2d/math.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fake2d {

struct InputFrameSnapshot;

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

    // --- AI overlay --------------------------------------------------------
    /// Hold (or release) a key for the attract driver. Unlike InjectFrame,
    /// which one real frame of polling wipes out, the overlay is merged into
    /// every snapshot -- polled or headless -- so a demo driven by it reads
    /// exactly like a player holding that key, in a real window included.
    /// The merge is an OR, so a human pressing the same key changes nothing.
    void AiHoldKey(std::string_view key, bool down);
    /// Hold/release a mouse button (0 = left, 1 = right, 2 = middle).
    void AiHoldButton(std::int64_t button, bool down);
    /// Park the cursor at `pos` on every frame while held; releasing hands it
    /// back to the real mouse.
    void AiHoldMouse(const Vec2 &pos, bool down);
    /// Drop the whole overlay.
    void AiClear();

    // --- deterministic recording / replay (Phase 52) -------------------------
    /// Public sizes shared with InputFrameSnapshot (must match the private
    /// arrays below). Exposed so the recording header stays header-only POD.
    static constexpr std::size_t kSnapshotKeyCount = 349;   // GLFW_KEY_LAST + 1
    static constexpr std::size_t kSnapshotButtonCount = 8;
    static constexpr int kSnapshotPadCount = 16;
    static constexpr int kSnapshotPadButtonCount = 15;
    static constexpr int kSnapshotPadAxisCount = 6;

    /// Pack the current snapshot into a fixed-layout sample (no heap).
    void CaptureSnapshot(InputFrameSnapshot &out,
                         std::uint64_t step_index) const;
    /// Replace the device poll: write `snap` as the new current state and
    /// advance edge detection against the previous frame (same contract as
    /// InjectFrame, plus gamepad fields InjectFrame does not touch).
    void ApplySnapshot(const InputFrameSnapshot &snap);

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

    // --- action mapping ------------------------------------------------------
    /// Bind one key to a named action (multiple keys per action). Games keep
    /// the bindings in their settings and rebuild them on startup, so
    /// rebinding is just ClearActions() + BindAction().
    void BindAction(std::string_view action, std::string_view key);
    /// Remove all bindings for one action (or every action when empty).
    void ClearActions(std::string_view action = {});
    [[nodiscard]] bool ActionDown(std::string_view action) const;
    [[nodiscard]] bool ActionPressed(std::string_view action) const;
    [[nodiscard]] bool ActionReleased(std::string_view action) const;

private:
    /// OR the AI overlay into the snapshot. `advance_edges` is set on the
    /// headless path, where there is no poll step to move the current state
    /// into the previous one -- without it a held key would read as *pressed*
    /// on every frame.
    void ApplyAiOverlay(bool advance_edges);

    static constexpr std::size_t kKeyCount = kSnapshotKeyCount;
    static constexpr std::size_t kButtonCount = kSnapshotButtonCount;
    static constexpr int kPadCount = kSnapshotPadCount;
    static constexpr int kPadButtonCount = kSnapshotPadButtonCount;
    static constexpr int kPadAxisCount = kSnapshotPadAxisCount;

    void *window_ = nullptr;
    std::uint8_t keys_[kKeyCount] = {};
    std::uint8_t prev_keys_[kKeyCount] = {};
    std::uint8_t buttons_[kButtonCount] = {};
    std::uint8_t prev_buttons_[kButtonCount] = {};
    std::uint8_t had_mouse_press_[kButtonCount] = {};
    std::uint8_t tap_hold_[kButtonCount] = {};
    std::uint8_t had_key_press_[kKeyCount] = {};
    std::uint8_t key_tap_hold_[kKeyCount] = {};
    std::uint8_t ai_keys_[kKeyCount] = {};
    std::uint8_t ai_buttons_[kButtonCount] = {};
    bool ai_mouse_held_ = false;
    std::uint8_t pad_connected_[kPadCount] = {};
    std::uint8_t pad_buttons_[kPadCount][kPadButtonCount] = {};
    std::uint8_t prev_pad_buttons_[kPadCount][kPadButtonCount] = {};
    float pad_axes_[kPadCount][kPadAxisCount] = {};
    Vec2 mouse_pos_{0.0f, 0.0f};
    Vec2 mouse_delta_{0.0f, 0.0f};
    Vec2 ai_mouse_pos_{0.0f, 0.0f};
    double wheel_ = 0.0;
    double wheel_pending_ = 0.0;
    /// Codepoints handed to the current frame; the callback fills the pending
    /// buffer, which NewFrame swaps in (see TextCodepoints).
    std::vector<unsigned int> text_input_;
    std::vector<unsigned int> text_pending_;
    std::unordered_map<std::string, std::vector<std::string>> actions_;
};

} // namespace fake2d
