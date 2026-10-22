#include "gl_context.hpp"
#include <iostream>
#include <vector>

namespace render {

static GLFunctions g_gl;

#define LOAD_PROC(name, type) \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name)); \
    if (!name) { success = false; }

bool GLFunctions::init() {
    if (loaded) return true;

    bool success = true;

    LOAD_PROC(GenFramebuffers, void (*)(GLsizei, GLuint*));
    LOAD_PROC(DeleteFramebuffers, void (*)(GLsizei, const GLuint*));
    LOAD_PROC(BindFramebuffer, void (*)(GLenum, GLuint));
    LOAD_PROC(FramebufferTexture2D, void (*)(GLenum, GLenum, GLenum, GLuint, GLint));
    LOAD_PROC(CheckFramebufferStatus, GLenum (*)(GLenum));

    LOAD_PROC(GenBuffers, void (*)(GLsizei, GLuint*));
    LOAD_PROC(DeleteBuffers, void (*)(GLsizei, const GLuint*));
    LOAD_PROC(BindBuffer, void (*)(GLenum, GLuint));
    LOAD_PROC(BufferData, void (*)(GLenum, ptrdiff_t, const void*, GLenum));
    LOAD_PROC(BufferSubData, void (*)(GLenum, ptrdiff_t, ptrdiff_t, const void*));

    LOAD_PROC(GenVertexArrays, void (*)(GLsizei, GLuint*));
    LOAD_PROC(DeleteVertexArrays, void (*)(GLsizei, const GLuint*));
    LOAD_PROC(BindVertexArray, void (*)(GLuint));
    LOAD_PROC(EnableVertexAttribArray, void (*)(GLuint));
    LOAD_PROC(VertexAttribPointer, void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*));

    LOAD_PROC(CreateShader, GLuint (*)(GLenum));
    LOAD_PROC(DeleteShader, void (*)(GLuint));
    LOAD_PROC(ShaderSource, void (*)(GLuint, GLsizei, const char* const*, const GLint*));
    LOAD_PROC(CompileShader, void (*)(GLuint));
    LOAD_PROC(GetShaderiv, void (*)(GLuint, GLenum, GLint*));
    LOAD_PROC(GetShaderInfoLog, void (*)(GLuint, GLsizei, GLsizei*, char*));

    LOAD_PROC(CreateProgram, GLuint (*)());
    LOAD_PROC(DeleteProgram, void (*)(GLuint));
    LOAD_PROC(AttachShader, void (*)(GLuint, GLuint));
    LOAD_PROC(LinkProgram, void (*)(GLuint));
    LOAD_PROC(GetProgramiv, void (*)(GLuint, GLenum, GLint*));
    LOAD_PROC(GetProgramInfoLog, void (*)(GLuint, GLsizei, GLsizei*, char*));
    LOAD_PROC(UseProgram, void (*)(GLuint));

    LOAD_PROC(GetUniformLocation, GLint (*)(GLuint, const char*));
    LOAD_PROC(Uniform1f, void (*)(GLint, GLfloat));
    LOAD_PROC(Uniform1i, void (*)(GLint, GLint));
    LOAD_PROC(Uniform2f, void (*)(GLint, GLfloat, GLfloat));
    LOAD_PROC(ActiveTexture, void (*)(GLenum));

    loaded = success;
    return loaded;
}

#undef LOAD_PROC

GLFunctions& getGL() {
    return g_gl;
}

GLuint compileShaderProgram(const char* vert_src, const char* frag_src) {
    auto& gl = getGL();
    if (!gl.loaded && !gl.init()) return 0;

    GLuint vert_shader = gl.CreateShader(GL_VERTEX_SHADER);
    gl.ShaderSource(vert_shader, 1, &vert_src, nullptr);
    gl.CompileShader(vert_shader);

    GLint compiled = 0;
    gl.GetShaderiv(vert_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        gl.GetShaderInfoLog(vert_shader, sizeof(log), nullptr, log);
        std::cerr << "Vertex shader compile error:\n" << log << std::endl;
        gl.DeleteShader(vert_shader);
        return 0;
    }

    GLuint frag_shader = gl.CreateShader(GL_FRAGMENT_SHADER);
    gl.ShaderSource(frag_shader, 1, &frag_src, nullptr);
    gl.CompileShader(frag_shader);

    gl.GetShaderiv(frag_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        gl.GetShaderInfoLog(frag_shader, sizeof(log), nullptr, log);
        std::cerr << "Fragment shader compile error:\n" << log << std::endl;
        gl.DeleteShader(vert_shader);
        gl.DeleteShader(frag_shader);
        return 0;
    }

    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vert_shader);
    gl.AttachShader(program, frag_shader);
    gl.LinkProgram(program);

    GLint linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Shader program link error:\n" << log << std::endl;
        gl.DeleteShader(vert_shader);
        gl.DeleteShader(frag_shader);
        gl.DeleteProgram(program);
        return 0;
    }

    gl.DeleteShader(vert_shader);
    gl.DeleteShader(frag_shader);
    return program;
}

}  // namespace render
