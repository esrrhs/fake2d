#include "fake2d/input.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace fake2d {

namespace {

const std::unordered_map<std::string_view, int> &KeyNameTable() {
    static const std::unordered_map<std::string_view, int> table = {
        // editing & navigation
        {"space", GLFW_KEY_SPACE},         {"escape", GLFW_KEY_ESCAPE},
        {"enter", GLFW_KEY_ENTER},         {"return", GLFW_KEY_ENTER},
        {"tab", GLFW_KEY_TAB},             {"backspace", GLFW_KEY_BACKSPACE},
        {"insert", GLFW_KEY_INSERT},       {"delete", GLFW_KEY_DELETE},
        {"page_up", GLFW_KEY_PAGE_UP},     {"page_down", GLFW_KEY_PAGE_DOWN},
        {"home", GLFW_KEY_HOME},           {"end", GLFW_KEY_END},
        {"caps_lock", GLFW_KEY_CAPS_LOCK},
        // arrows
        {"up", GLFW_KEY_UP}, {"down", GLFW_KEY_DOWN},
        {"left", GLFW_KEY_LEFT}, {"right", GLFW_KEY_RIGHT},
        // modifiers
        {"left_shift", GLFW_KEY_LEFT_SHIFT},     {"right_shift", GLFW_KEY_RIGHT_SHIFT},
        {"left_control", GLFW_KEY_LEFT_CONTROL}, {"right_control", GLFW_KEY_RIGHT_CONTROL},
        {"left_alt", GLFW_KEY_LEFT_ALT},         {"right_alt", GLFW_KEY_RIGHT_ALT},
        {"left_super", GLFW_KEY_LEFT_SUPER},     {"right_super", GLFW_KEY_RIGHT_SUPER},
        {"menu", GLFW_KEY_MENU},
        // punctuation
        {"-", GLFW_KEY_MINUS}, {"=", GLFW_KEY_EQUAL}, {"[", GLFW_KEY_LEFT_BRACKET},
        {"]", GLFW_KEY_RIGHT_BRACKET}, {"\\", GLFW_KEY_BACKSLASH}, {";", GLFW_KEY_SEMICOLON},
        {"'", GLFW_KEY_APOSTROPHE}, {"`", GLFW_KEY_GRAVE_ACCENT}, {",", GLFW_KEY_COMMA},
        {".", GLFW_KEY_PERIOD}, {"/", GLFW_KEY_SLASH},
    };
    return table;
}

int KeyToGlfw(std::string_view key) {
    // letters a-z and digits 0-9 follow a fixed layout
    if (key.size() == 1) {
        const char c = key[0];
        if (c >= 'a' && c <= 'z') {
            return GLFW_KEY_A + (c - 'a');
        }
        if (c >= '0' && c <= '9') {
            return GLFW_KEY_0 + (c - '0');
        }
    }
    // function keys f1..f12
    if (key.size() >= 2 && key[0] == 'f' && key.size() <= 3) {
        int number = 0;
        bool digits_only = true;
        for (const char c : key.substr(1)) {
            if (c < '0' || c > '9') {
                digits_only = false;
                break;
            }
            number = number * 10 + (c - '0');
        }
        if (digits_only && number >= 1 && number <= 12) {
            return GLFW_KEY_F1 + (number - 1);
        }
    }
    const auto it = KeyNameTable().find(key);
    return it != KeyNameTable().end() ? it->second : -1;
}

int PadButtonToGlfw(std::string_view button) {
    static const std::unordered_map<std::string_view, int> table = {
        {"a", GLFW_GAMEPAD_BUTTON_A},
        {"b", GLFW_GAMEPAD_BUTTON_B},
        {"x", GLFW_GAMEPAD_BUTTON_X},
        {"y", GLFW_GAMEPAD_BUTTON_Y},
        {"left_bumper", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER},
        {"right_bumper", GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {"back", GLFW_GAMEPAD_BUTTON_BACK},
        {"start", GLFW_GAMEPAD_BUTTON_START},
        {"guide", GLFW_GAMEPAD_BUTTON_GUIDE},
        {"left_thumb", GLFW_GAMEPAD_BUTTON_LEFT_THUMB},
        {"right_thumb", GLFW_GAMEPAD_BUTTON_RIGHT_THUMB},
        {"dpad_up", GLFW_GAMEPAD_BUTTON_DPAD_UP},
        {"dpad_right", GLFW_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"dpad_down", GLFW_GAMEPAD_BUTTON_DPAD_DOWN},
        {"dpad_left", GLFW_GAMEPAD_BUTTON_DPAD_LEFT},
    };
    const auto it = table.find(button);
    return it != table.end() ? it->second : -1;
}

int PadAxisToGlfw(std::string_view axis) {
    static const std::unordered_map<std::string_view, int> table = {
        {"left_x", GLFW_GAMEPAD_AXIS_LEFT_X},
        {"left_y", GLFW_GAMEPAD_AXIS_LEFT_Y},
        {"right_x", GLFW_GAMEPAD_AXIS_RIGHT_X},
        {"right_y", GLFW_GAMEPAD_AXIS_RIGHT_Y},
        {"left_trigger", GLFW_GAMEPAD_AXIS_LEFT_TRIGGER},
        {"right_trigger", GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER},
    };
    const auto it = table.find(axis);
    return it != table.end() ? it->second : -1;
}

constexpr float kStickDeadzone = 0.20f;

/// Radial dead zone on a stick pair, rescaled so full deflection still
/// reaches 1.0. `other` is the sibling axis (x for y and vice versa).
float ApplyStickDeadzone(const float axes[6], int axis, int other) {
    const float x = axes[axis];
    const float y = axes[other];
    const float magnitude = std::sqrt(x * x + y * y);
    if (!(magnitude > kStickDeadzone)) {
        return 0.0f;
    }
    const float scale = (magnitude - kStickDeadzone) / ((1.0f - kStickDeadzone) * magnitude);
    return x * scale;
}

} // namespace

void Input::Attach(void *native_window) {
    window_ = native_window;
    if (window_ != nullptr) {
        GLFWwindow *window = static_cast<GLFWwindow *>(window_);
        // The engine owns no other user pointer on this window.
        glfwSetWindowUserPointer(window, this);
        glfwSetScrollCallback(window, [](GLFWwindow *win, double /*dx*/, double dy) {
            if (Input *input = static_cast<Input *>(glfwGetWindowUserPointer(win))) {
                input->wheel_pending_ += dy;
            }
        });
    }
}

void Input::NewFrame(void *native_window) {
    GLFWwindow *window = static_cast<GLFWwindow *>(native_window);
    if (window == nullptr) {
        return;
    }

    std::copy(keys_, keys_ + kKeyCount, prev_keys_);
    std::copy(buttons_, buttons_ + kButtonCount, prev_buttons_);

    for (int key = 0; key < static_cast<int>(kKeyCount); ++key) {
        keys_[key] = static_cast<std::uint8_t>(glfwGetKey(window, key) == GLFW_PRESS);
    }
    for (int button = 0; button < static_cast<int>(kButtonCount); ++button) {
        buttons_[button] = static_cast<std::uint8_t>(glfwGetMouseButton(window, button) == GLFW_PRESS);
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    mouse_delta_ = {static_cast<float>(x) - mouse_pos_.x, static_cast<float>(y) - mouse_pos_.y};
    mouse_pos_ = {static_cast<float>(x), static_cast<float>(y)};

    wheel_ = wheel_pending_;
    wheel_pending_ = 0.0;

    // Gamepads: poll every joystick slot. Hot-unplug simply reads neutral.
    for (int pad = 0; pad < kPadCount; ++pad) {
        std::copy(pad_buttons_[pad], pad_buttons_[pad] + kPadButtonCount, prev_pad_buttons_[pad]);
        pad_connected_[pad] = 0;
        std::memset(pad_buttons_[pad], 0, sizeof(pad_buttons_[pad]));
        std::memset(pad_axes_[pad], 0, sizeof(pad_axes_[pad]));
        if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + pad)) {
            GLFWgamepadstate state{};
            if (glfwGetGamepadState(GLFW_JOYSTICK_1 + pad, &state)) {
                pad_connected_[pad] = 1;
                for (int b = 0; b < kPadButtonCount; ++b) {
                    pad_buttons_[pad][b] = static_cast<std::uint8_t>(state.buttons[b] == GLFW_PRESS);
                }
                for (int a = 0; a < kPadAxisCount; ++a) {
                    pad_axes_[pad][a] = state.axes[a];
                }
            }
        }
    }
}

bool Input::KeyDown(std::string_view key) const {
    const int glfw_key = KeyToGlfw(key);
    return glfw_key >= 0 && keys_[glfw_key] != 0;
}

bool Input::KeyPressed(std::string_view key) const {
    const int glfw_key = KeyToGlfw(key);
    return glfw_key >= 0 && keys_[glfw_key] != 0 && prev_keys_[glfw_key] == 0;
}

bool Input::KeyReleased(std::string_view key) const {
    const int glfw_key = KeyToGlfw(key);
    return glfw_key >= 0 && keys_[glfw_key] == 0 && prev_keys_[glfw_key] != 0;
}

bool Input::MouseDown(std::int64_t button) const {
    return button >= 0 && button < static_cast<std::int64_t>(kButtonCount) && buttons_[button] != 0;
}

bool Input::MousePressed(std::int64_t button) const {
    return button >= 0 && button < static_cast<std::int64_t>(kButtonCount) && buttons_[button] != 0 &&
           prev_buttons_[button] == 0;
}

bool Input::MouseReleased(std::int64_t button) const {
    return button >= 0 && button < static_cast<std::int64_t>(kButtonCount) && buttons_[button] == 0 &&
           prev_buttons_[button] != 0;
}

bool Input::GamepadConnected(int pad) const {
    return pad >= 0 && pad < kPadCount && pad_connected_[pad] != 0;
}

bool Input::GamepadButtonDown(int pad, std::string_view button) const {
    const int b = PadButtonToGlfw(button);
    return GamepadConnected(pad) && b >= 0 && pad_buttons_[pad][b] != 0;
}

bool Input::GamepadButtonPressed(int pad, std::string_view button) const {
    const int b = PadButtonToGlfw(button);
    return GamepadConnected(pad) && b >= 0 &&
           pad_buttons_[pad][b] != 0 && prev_pad_buttons_[pad][b] == 0;
}

bool Input::GamepadButtonReleased(int pad, std::string_view button) const {
    const int b = PadButtonToGlfw(button);
    return b >= 0 && pad >= 0 && pad < kPadCount &&
           pad_buttons_[pad][b] == 0 && prev_pad_buttons_[pad][b] != 0;
}

float Input::GamepadAxis(int pad, std::string_view axis) const {
    const int a = PadAxisToGlfw(axis);
    if (!GamepadConnected(pad) || a < 0) {
        return 0.0f;
    }
    switch (a) {
    case GLFW_GAMEPAD_AXIS_LEFT_X:
        return ApplyStickDeadzone(pad_axes_[pad], GLFW_GAMEPAD_AXIS_LEFT_X,
                                  GLFW_GAMEPAD_AXIS_LEFT_Y);
    case GLFW_GAMEPAD_AXIS_LEFT_Y:
        return ApplyStickDeadzone(pad_axes_[pad], GLFW_GAMEPAD_AXIS_LEFT_Y,
                                  GLFW_GAMEPAD_AXIS_LEFT_X);
    case GLFW_GAMEPAD_AXIS_RIGHT_X:
        return ApplyStickDeadzone(pad_axes_[pad], GLFW_GAMEPAD_AXIS_RIGHT_X,
                                  GLFW_GAMEPAD_AXIS_RIGHT_Y);
    case GLFW_GAMEPAD_AXIS_RIGHT_Y:
        return ApplyStickDeadzone(pad_axes_[pad], GLFW_GAMEPAD_AXIS_RIGHT_Y,
                                  GLFW_GAMEPAD_AXIS_RIGHT_X);
    default:
        // Triggers rest at -1; remap the full travel to 0..1.
        return std::clamp((pad_axes_[pad][a] + 1.0f) * 0.5f, 0.0f, 1.0f);
    }
}

std::string_view Input::GamepadName(int pad) const {
    if (!GamepadConnected(pad) || window_ == nullptr) {
        return {};
    }
    const char *name = glfwGetGamepadName(GLFW_JOYSTICK_1 + pad);
    return name ? std::string_view{name} : std::string_view{};
}

} // namespace fake2d
