#include "fake2d/renderer.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

namespace fake2d {

bool Renderer::Init(int width, int height) {
    width_ = width;
    height_ = height;
    ready_ = true;
    Resize(width, height);
    return true;
}

void Renderer::Resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (ready_) {
        glViewport(0, 0, width_, height_);
    }
}

void Renderer::BeginFrame(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::EndFrame() {
    // Present is handled by the platform window (SwapBuffers).
}

void Renderer::Shutdown() {
    ready_ = false;
}

} // namespace fake2d
