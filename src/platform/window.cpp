#include "platform/window.h"

#include <GLFW/glfw3.h>

#include "render/gl.h"

#if defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#endif

#include <cstdio>

namespace fake2d::platform {

Window::~Window() {
    Destroy();
}

bool Window::CreateGLFW(const WindowDesc &desc, int major, int minor, bool core_profile) {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    if (core_profile) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }
    if (desc.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    handle_ = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    if (!handle_) {
        return false;
    }

    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(desc.vsync ? 1 : 0);
    width_ = desc.width;
    height_ = desc.height;
    RefreshSize();
    render_mode_ = RenderMode::GLFW;
    return true;
}

#if defined(__APPLE__)
bool Window::CreateCGLHeadless(int width, int height) {
    // The GL2 software renderer only shows up with offline renderers allowed;
    // try without the attribute as a last resort.
    CGLPixelFormatAttribute attrs[] = {kCGLPFAAllowOfflineRenderers,
                                       (CGLPixelFormatAttribute)0};
    CGLPixelFormatAttribute no_attrs[] = {(CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pix = nullptr;
    GLint npix = 0;
    if (CGLChoosePixelFormat(attrs, &pix, &npix) != kCGLNoError || npix == 0) {
        if (CGLChoosePixelFormat(no_attrs, &pix, &npix) != kCGLNoError || npix == 0) {
            return false;
        }
    }
    CGLContextObj ctx = nullptr;
    const CGLError ctx_err = CGLCreateContext(pix, nullptr, &ctx);
    CGLReleasePixelFormat(pix);
    if (ctx_err != kCGLNoError || !ctx) {
        return false;
    }
    if (CGLSetCurrentContext(ctx) != kCGLNoError) {
        CGLReleaseContext(ctx);
        return false;
    }

    // Offscreen render target. The unsuffixed FBO entry points dispatch
    // correctly on legacy 2.1 contexts (probe-verified on the software
    // renderer: FRAMEBUFFER_COMPLETE + exact-color glReadPixels).
    glGenTextures(1, &fbo_texture_);
    glBindTexture(GL_TEXTURE_2D, fbo_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           fbo_texture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "fake2d: CGL headless framebuffer incomplete (0x%x)\n",
                     glCheckFramebufferStatus(GL_FRAMEBUFFER));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &fbo_texture_);
        fbo_ = 0;
        fbo_texture_ = 0;
        CGLSetCurrentContext(nullptr);
        CGLReleaseContext(ctx);
        return false;
    }
    glViewport(0, 0, width, height);

    cgl_context_ = ctx;
    render_mode_ = RenderMode::CGLHeadlessFBO;
    width_ = width;
    height_ = height;
    fb_width_ = width;
    fb_height_ = height;
    content_scale_ = 1.0f;
    return true;
}
#endif

bool Window::Create(const WindowDesc &desc) {
    glfwSetErrorCallback([](int code, const char *message) {
        std::fprintf(stderr, "fake2d: GLFW error %d: %s\n", code,
                     message ? message : "(no message)");
    });
    if (!glfwInit()) {
        std::fprintf(stderr, "fake2d: glfwInit failed\n");
        return false;
    }

    // Ladder: 3.3 core first; a 2.1 legacy context keeps every machine
    // without a core pixel format rendering (the engine's GL2 path handles
    // the reduced API).
    if (CreateGLFW(desc, 3, 3, true)) {
        return true;
    }
    if (CreateGLFW(desc, 2, 1, false)) {
        std::fprintf(stderr, "fake2d: no GL 3.3 pixel format; created a legacy 2.1 context\n");
        return true;
    }
#if defined(__APPLE__)
    // macOS VMs whose only renderer is the Apple Software Renderer expose no
    // NSGL pixel format at all (GLFW 65545). Headless runs fall back to a
    // drawable-less CGL context + offscreen FBO.
    if (desc.headless) {
        glfwTerminate();
        if (CreateCGLHeadless(desc.width, desc.height)) {
            std::fprintf(stderr, "fake2d: GLFW pixel format unavailable; "
                                 "rendering offscreen through a CGL headless context\n");
            return true;
        }
        std::fprintf(stderr, "fake2d: CGL headless fallback failed\n");
        return false;
    }
#endif
    std::fprintf(stderr, "fake2d: glfwCreateWindow failed\n");
    glfwTerminate();
    return false;
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
    if (render_mode_ == RenderMode::CGLHeadlessFBO) {
#if defined(__APPLE__)
        if (cgl_context_) {
            CGLContextObj ctx = static_cast<CGLContextObj>(cgl_context_);
            CGLSetCurrentContext(ctx);
            if (fbo_) {
                glDeleteFramebuffers(1, &fbo_);
                fbo_ = 0;
            }
            if (fbo_texture_) {
                glDeleteTextures(1, &fbo_texture_);
                fbo_texture_ = 0;
            }
            CGLSetCurrentContext(nullptr);
            CGLReleaseContext(ctx);
            cgl_context_ = nullptr;
        }
#endif
        render_mode_ = RenderMode::GLFW;
        return;
    }
    if (handle_) {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
        glfwTerminate();
    }
}

void Window::PollEvents() {
    if (handle_) {
        glfwPollEvents();
    }
}

void Window::SwapBuffers() {
    if (handle_) {
        glfwSwapBuffers(handle_);
    }
    // CGLHeadlessFBO: no drawable to swap with; the FBO contents are read
    // via SaveScreenshot.
}

bool Window::ShouldClose() const {
    if (render_mode_ == RenderMode::CGLHeadlessFBO) {
        // No window events exist; the loop is driven by the frame budget.
        return false;
    }
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
