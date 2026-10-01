#include "fake2d/input.h"

#include <GLFW/glfw3.h>

#include <algorithm>
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

} // namespace fake2d
