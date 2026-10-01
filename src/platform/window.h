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
    /// Logical window size in points.
    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }
    /// Physical framebuffer size in pixels (differs on HiDPI/Retina).
    [[nodiscard]] int FramebufferWidth() const { return fb_width_; }
    [[nodiscard]] int FramebufferHeight() const { return fb_height_; }
    /// framebuffer points -> pixels scale, x/y averaged.
    [[nodiscard]] float ContentScale() const { return content_scale_; }
    /// Re-query sizes after event polling; returns true when any changed.
    bool RefreshSize();

private:
    GLFWwindow *handle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int fb_width_ = 0;
    int fb_height_ = 0;
    float content_scale_ = 1.0f;
};

} // namespace fake2d::platform
