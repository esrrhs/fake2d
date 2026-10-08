#pragma once

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
