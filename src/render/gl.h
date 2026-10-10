#pragma once

#include <cstdio>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#if defined(_WIN32)
// MinGW's GL/gl.h pulls in windows.h; wingdi.h #defines DrawText to
// DrawTextA/DrawTextW, which collides with the Renderer/Font DrawText API.
#ifdef DrawText
#undef DrawText
#endif
// opengl32 only exports GL 1.1; pull in the runtime function-pointer layer.
#include "render/gl_loader.h"
#else
static inline void fake2d_gl_load_functions() {}
#endif

namespace fake2d::glcaps {

// True when the active context predates GL 3.0 (legacy 2.1). On such
// contexts the engine takes its GL2 compatibility path: GLSL 120 shader
// variants, no VAO (attribute pointers re-specified per flush), and
// GL_LUMINANCE for single-channel textures. Detection happens once in
// Renderer::Init while the context is current.
inline bool legacy_mode = false;

inline void Detect() {
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    int major = 0;
    if (version) {
        // "2.1 APPLE-..." / "3.3 Core ..." / "4.6.0 ..."
        if (std::sscanf(version, "%d", &major) != 1) {
            major = 3;
        }
    }
    legacy_mode = major < 3;
}

} // namespace fake2d::glcaps
