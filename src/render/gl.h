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
#endif
