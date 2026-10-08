// Definitions + runtime resolution for the Windows OpenGL entry points
// declared in gl_loader.h. Empty translation unit elsewhere.
#include "render/gl_loader.h"

#if defined(_WIN32)

#include <GLFW/glfw3.h>

#define FAKE2D_GL_DEFINE(name, type) type p_gl##name = nullptr;
FAKE2D_GL_PROCS(FAKE2D_GL_DEFINE)
#undef FAKE2D_GL_DEFINE

void fake2d_gl_load_functions() {
#define FAKE2D_GL_LOAD(name, type)                                            \
    p_gl##name = reinterpret_cast<type>(glfwGetProcAddress("gl" #name));
    FAKE2D_GL_PROCS(FAKE2D_GL_LOAD)
#undef FAKE2D_GL_LOAD
}

#endif
