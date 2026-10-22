#pragma once
/**
 * render/gl_context.hpp  --  OpenGL 3.3 Core function loader and context utilities.
 *
 * Uses SDL_GL_GetProcAddress to load OpenGL 3.3 core functions cleanly without
 * requiring external Python generators or platform-specific loader libraries.
 */
#include <SDL.h>

#if defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string>

// Modern GL function pointer types for functions not in basic gl.h
#ifndef GL_R32F
#define GL_R32F                           0x822E
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                    0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0              0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                   0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW                    0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW                   0x88E8
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER                0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER                  0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS                 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS                    0x8B82
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE             0x8642
#endif

namespace render {

struct GLFunctions {
    bool loaded = false;

    // FBOs
    void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;

    // Buffers & VAOs
    void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
    void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferData)(GLenum, ptrdiff_t, const void*, GLenum) = nullptr;
    void (*BufferSubData)(GLenum, ptrdiff_t, ptrdiff_t, const void*) = nullptr;

    void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void (*DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
    void (*BindVertexArray)(GLuint) = nullptr;
    void (*EnableVertexAttribArray)(GLuint) = nullptr;
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;

    // Shaders & Programs
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    void (*ShaderSource)(GLuint, GLsizei, const char* const*, const GLint*) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;

    GLuint (*CreateProgram)() = nullptr;
    void (*DeleteProgram)(GLuint) = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;

    // Uniforms
    GLint (*GetUniformLocation)(GLuint, const char*) = nullptr;
    void (*Uniform1f)(GLint, GLfloat) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;

    bool init();
};

GLFunctions& getGL();

GLuint compileShaderProgram(const char* vert_src, const char* frag_src);

}  // namespace render
