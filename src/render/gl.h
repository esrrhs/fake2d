#pragma once

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
