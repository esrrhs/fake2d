#include "platform/window.h"

#include <GLFW/glfw3.h>

#include <cstdio>

namespace fake2d::platform {

Window::~Window() {
    Destroy();
}

bool Window::Create(const WindowDesc &desc) {
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
    return true;
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

} // namespace fake2d::platform
