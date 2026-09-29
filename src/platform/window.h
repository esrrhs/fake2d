#pragma once

struct GLFWwindow;

namespace fake2d::platform {

struct WindowDesc {
    const char *title = "fake2d";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool headless = false;
};

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool Create(const WindowDesc &desc);
    void Destroy();
    void PollEvents();
    void SwapBuffers();
    [[nodiscard]] bool ShouldClose() const;
    [[nodiscard]] GLFWwindow *Handle() const { return handle_; }
    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }

private:
    GLFWwindow *handle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace fake2d::platform
