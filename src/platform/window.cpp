#include "platform/window.h"

#include <GLFW/glfw3.h>

#include <cstdio>

namespace fake2d::platform {

Window::~Window() {
    Destroy();
}

bool Window::Create(const WindowDesc &desc) {
    glfwSetErrorCallback([](int code, const char *message) {
        std::fprintf(stderr, "fake2d: GLFW error %d: %s\n", code,
                     message ? message : "(no message)");
    });
    if (!glfwInit()) {
        std::fprintf(stderr, "fake2d: glfwInit failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    if (desc.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    handle_ = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    if (!handle_) {
        std::fprintf(stderr, "fake2d: glfwCreateWindow failed\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(desc.vsync ? 1 : 0);
    width_ = desc.width;
    height_ = desc.height;
    RefreshSize();
    return true;
}

bool Window::RefreshSize() {
    if (!handle_) {
        return false;
    }
    int w = width_;
    int h = height_;
    int fbw = 0;
    int fbh = 0;
    float sx = 1.0f;
    float sy = 1.0f;
    glfwGetWindowSize(handle_, &w, &h);
    glfwGetFramebufferSize(handle_, &fbw, &fbh);
    glfwGetWindowContentScale(handle_, &sx, &sy);

    float scale = (sx + sy) * 0.5f;
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    // Some platforms expose a zero framebuffer size transiently; fall back to
    // the logical window size scaled by the reported content scale.
    if (fbw <= 0 || fbh <= 0) {
        fbw = static_cast<int>(w * scale);
        fbh = static_cast<int>(h * scale);
    }

    const bool changed = w != width_ || h != height_ ||
                         fbw != fb_width_ || fbh != fb_height_ ||
                         scale != content_scale_;
    width_ = w;
    height_ = h;
    fb_width_ = fbw;
    fb_height_ = fbh;
    content_scale_ = scale;
    return changed;
}

void Window::Destroy() {
    if (handle_) {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
        glfwTerminate();
    }
}

void Window::PollEvents() {
    glfwPollEvents();
}

void Window::SwapBuffers() {
    if (handle_) {
        glfwSwapBuffers(handle_);
    }
}

bool Window::ShouldClose() const {
    return handle_ == nullptr || glfwWindowShouldClose(handle_);
}

void Window::SetShouldClose(bool close) {
    if (handle_) {
        glfwSetWindowShouldClose(handle_, close ? GLFW_TRUE : GLFW_FALSE);
    }
}

void Window::SetTitle(const char *title) {
    if (handle_ && title != nullptr) {
        glfwSetWindowTitle(handle_, title);
    }
}

void Window::SetFullscreen(bool fullscreen) {
    if (!handle_ || fullscreen == fullscreen_) {
        return;
    }
    if (fullscreen) {
        glfwGetWindowPos(handle_, &windowed_x_, &windowed_y_);
        glfwGetWindowSize(handle_, &windowed_w_, &windowed_h_);
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        if (!monitor) {
            return;
        }
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(handle_, monitor, 0, 0, mode->width, mode->height,
                             mode->refreshRate);
        fullscreen_ = true;
    } else {
        glfwSetWindowMonitor(handle_, nullptr, windowed_x_, windowed_y_,
                             windowed_w_ > 0 ? windowed_w_ : width_,
                             windowed_h_ > 0 ? windowed_h_ : height_, GLFW_DONT_CARE);
        fullscreen_ = false;
    }
}

} // namespace fake2d::platform
