#pragma once

// Windows only: opengl32.dll exports just the OpenGL 1.1 entry points.
// Everything newer (shaders, VAOs/VBOs, ...) must be resolved at runtime via
// wglGetProcAddress, exposed portably through glfwGetProcAddress. The
// macros below reroute the gl* calls used by the engine to function
// pointers that fake2d_gl_load_functions() fills in right after the GL
// context is created. Linux links these symbols from libGL and macOS from
// the OpenGL framework, so this whole file is Windows-only.
#if defined(_WIN32)

#include <GL/gl.h>
#include <GL/glext.h>

void fake2d_gl_load_functions();

#define FAKE2D_GL_PROCS(X)                                                     \
    X(ActiveTexture, PFNGLACTIVETEXTUREPROC)                                   \
    X(AttachShader, PFNGLATTACHSHADERPROC)                                     \
    X(BindBuffer, PFNGLBINDBUFFERPROC)                                         \
    X(BindVertexArray, PFNGLBINDVERTEXARRAYPROC)                               \
    X(BufferData, PFNGLBUFFERDATAPROC)                                         \
    X(BufferSubData, PFNGLBUFFERSUBDATAPROC)                                   \
    X(CompileShader, PFNGLCOMPILESHADERPROC)                                   \
    X(CreateProgram, PFNGLCREATEPROGRAMPROC)                                   \
    X(CreateShader, PFNGLCREATESHADERPROC)                                     \
    X(DeleteBuffers, PFNGLDELETEBUFFERSPROC)                                   \
    X(DeleteProgram, PFNGLDELETEPROGRAMPROC)                                   \
    X(DeleteShader, PFNGLDELETESHADERPROC)                                     \
    X(DeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC)                         \
    X(DisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC)             \
    X(EnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC)              \
    X(GenBuffers, PFNGLGENBUFFERSPROC)                                         \
    X(GenVertexArrays, PFNGLGENVERTEXARRAYSPROC)                               \
    X(GetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC)                           \
    X(GetProgramiv, PFNGLGETPROGRAMIVPROC)                                     \
    X(GetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC)                             \
    X(GetShaderiv, PFNGLGETSHADERIVPROC)                                       \
    X(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC)                         \
    X(LinkProgram, PFNGLLINKPROGRAMPROC)                                       \
    X(ShaderSource, PFNGLSHADERSOURCEPROC)                                     \
    X(Uniform1f, PFNGLUNIFORM1FPROC)                                           \
    X(Uniform1i, PFNGLUNIFORM1IPROC)                                           \
    X(Uniform2f, PFNGLUNIFORM2FPROC)                                           \
    X(Uniform4f, PFNGLUNIFORM4FPROC)                                           \
    X(UniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC)                             \
    X(UseProgram, PFNGLUSEPROGRAMPROC)                                         \
    X(VertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC)

#define FAKE2D_GL_DECL(name, type) extern type p_gl##name;
FAKE2D_GL_PROCS(FAKE2D_GL_DECL)
#undef FAKE2D_GL_DECL

// Route every source-level glFoo call to its loaded function pointer.
#define glActiveTexture           p_glActiveTexture
#define glAttachShader            p_glAttachShader
#define glBindBuffer              p_glBindBuffer
#define glBindVertexArray         p_glBindVertexArray
#define glBufferData              p_glBufferData
#define glBufferSubData           p_glBufferSubData
#define glCompileShader           p_glCompileShader
#define glCreateProgram           p_glCreateProgram
#define glCreateShader            p_glCreateShader
#define glDeleteBuffers           p_glDeleteBuffers
#define glDeleteProgram           p_glDeleteProgram
#define glDeleteShader            p_glDeleteShader
#define glDeleteVertexArrays      p_glDeleteVertexArrays
#define glDisableVertexAttribArray p_glDisableVertexAttribArray
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glGenBuffers              p_glGenBuffers
#define glGenVertexArrays         p_glGenVertexArrays
#define glGetProgramInfoLog       p_glGetProgramInfoLog
#define glGetProgramiv            p_glGetProgramiv
#define glGetShaderInfoLog        p_glGetShaderInfoLog
#define glGetShaderiv             p_glGetShaderiv
#define glGetUniformLocation      p_glGetUniformLocation
#define glLinkProgram             p_glLinkProgram
#define glShaderSource            p_glShaderSource
#define glUniform1f               p_glUniform1f
#define glUniform1i               p_glUniform1i
#define glUniform2f               p_glUniform2f
#define glUniform4f               p_glUniform4f
#define glUniformMatrix4fv        p_glUniformMatrix4fv
#define glUseProgram              p_glUseProgram
#define glVertexAttribPointer     p_glVertexAttribPointer

#endif // _WIN32
