/* ============================================================
   GL 3.3 CORE -- the exact entry points map.html asked WebGL2
   for, and nothing else.  opengl32.dll on Windows exports GL 1.1
   and no more, so everything past it arrives through
   wglGetProcAddress.  One X-macro list keeps the typedef, the
   pointer and the load in step: adding a call is one line, in
   one place.
   ============================================================ */
#ifndef GL33_H
#define GL33_H
#include <windows.h>
#include <GL/gl.h>
#include <stddef.h>

/* --- tokens the 1.1 header predates ------------------------------- */
#define GL_ARRAY_BUFFER            0x8892
#define GL_ELEMENT_ARRAY_BUFFER    0x8893
#define GL_STATIC_DRAW             0x88E4
#define GL_DYNAMIC_DRAW            0x88E8
#define GL_FRAGMENT_SHADER         0x8B30
#define GL_VERTEX_SHADER           0x8B31
#define GL_COMPILE_STATUS          0x8B81
#define GL_LINK_STATUS             0x8B82
#define GL_INFO_LOG_LENGTH         0x8B84
#define GL_TEXTURE0                0x84C0
#define GL_TEXTURE1                0x84C1
#define GL_TEXTURE2                0x84C2
#define GL_TEXTURE3                0x84C3
#define GL_TEXTURE_2D_ARRAY        0x8C1A
#define GL_TEXTURE_3D              0x806F
#define GL_CLAMP_TO_EDGE           0x812F
#define GL_LINEAR_MIPMAP_LINEAR    0x2703
#define GL_TEXTURE_MAX_ANISOTROPY  0x84FE
#define GL_MAX_TEX_ANISO           0x84FF
#define GL_FRAMEBUFFER             0x8D40
#define GL_RENDERBUFFER            0x8D41
#define GL_COLOR_ATTACHMENT0       0x8CE0
#define GL_COLOR_ATTACHMENT1       0x8CE1
#define GL_COLOR_ATTACHMENT2       0x8CE2
#define GL_DEPTH_ATTACHMENT        0x8D00
#define GL_DEPTH_COMPONENT24       0x81A6
#define GL_FRAMEBUFFER_COMPLETE    0x8CD5
#define GL_READ_FRAMEBUFFER        0x8CA8
#define GL_DRAW_FRAMEBUFFER        0x8CA9
#define GL_RGBA8                   0x8058
#define GL_R8                      0x8229   /* the glyph sheet: one channel */
#define GL_RED                     0x1903
#define GL_RGBA16F                 0x881A
#define GL_RGB16F                  0x881B
#define GL_HALF_FLOAT              0x140B
#define GL_MULTISAMPLE             0x809D
#define GL_TEXTURE_WRAP_R          0x8072
#define GL_SRGB8_ALPHA8            0x8C43
#define GL_FRAMEBUFFER_SRGB        0x8DB9
#define GL_MAX_SAMPLES             0x8D57
#define GL_PROGRAM_POINT_SIZE      0x8642

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

/* --- the list.  X(return, name, args) ------------------------------ */
#define GL_FUNCS \
X(void,   glGenBuffers,        (GLsizei,GLuint*)) \
X(void,   glBindBuffer,        (GLenum,GLuint)) \
X(void,   glBufferData,        (GLenum,GLsizeiptr,const void*,GLenum)) \
X(void,   glBufferSubData,     (GLenum,GLintptr,GLsizeiptr,const void*)) \
X(void,   glDeleteBuffers,     (GLsizei,const GLuint*)) \
X(void,   glGenVertexArrays,   (GLsizei,GLuint*)) \
X(void,   glBindVertexArray,   (GLuint)) \
X(void,   glEnableVertexAttribArray,(GLuint)) \
X(void,   glVertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*)) \
X(void,   glVertexAttribIPointer,(GLuint,GLint,GLenum,GLsizei,const void*)) \
X(void,   glVertexAttribDivisor,(GLuint,GLuint)) \
X(void,   glDrawArraysInstanced,(GLenum,GLint,GLsizei,GLsizei)) \
X(GLuint, glCreateShader,      (GLenum)) \
X(void,   glShaderSource,      (GLuint,GLsizei,const GLchar* const*,const GLint*)) \
X(void,   glCompileShader,     (GLuint)) \
X(void,   glGetShaderiv,       (GLuint,GLenum,GLint*)) \
X(void,   glGetShaderInfoLog,  (GLuint,GLsizei,GLsizei*,GLchar*)) \
X(void,   glDeleteShader,      (GLuint)) \
X(GLuint, glCreateProgram,     (void)) \
X(void,   glAttachShader,      (GLuint,GLuint)) \
X(void,   glLinkProgram,       (GLuint)) \
X(void,   glGetProgramiv,      (GLuint,GLenum,GLint*)) \
X(void,   glGetProgramInfoLog, (GLuint,GLsizei,GLsizei*,GLchar*)) \
X(void,   glUseProgram,        (GLuint)) \
X(void,   glDeleteProgram,     (GLuint)) \
X(GLint,  glGetUniformLocation,(GLuint,const GLchar*)) \
X(void,   glGetUniformfv,      (GLuint,GLint,GLfloat*)) \
X(void,   glUniform1f,         (GLint,GLfloat)) \
X(void,   glUniform1fv,        (GLint,GLsizei,const GLfloat*)) \
X(void,   glUniform1i,         (GLint,GLint)) \
X(void,   glUniform2f,         (GLint,GLfloat,GLfloat)) \
X(void,   glUniform3f,         (GLint,GLfloat,GLfloat,GLfloat)) \
X(void,   glUniform3fv,        (GLint,GLsizei,const GLfloat*)) \
X(void,   glUniform4fv,        (GLint,GLsizei,const GLfloat*)) \
X(void,   glUniformMatrix3fv,  (GLint,GLsizei,GLboolean,const GLfloat*)) \
X(void,   glUniformMatrix4fv,  (GLint,GLsizei,GLboolean,const GLfloat*)) \
X(void,   glActiveTexture,     (GLenum)) \
X(void,   glGenerateMipmap,    (GLenum)) \
X(void,   glTexImage3D,        (GLenum,GLint,GLint,GLsizei,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*)) \
X(void,   glTexSubImage3D,     (GLenum,GLint,GLint,GLint,GLint,GLsizei,GLsizei,GLsizei,GLenum,GLenum,const void*)) \
X(void,   glGenFramebuffers,   (GLsizei,GLuint*)) \
X(void,   glBindFramebuffer,   (GLenum,GLuint)) \
X(void,   glDeleteFramebuffers,(GLsizei,const GLuint*)) \
X(void,   glFramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint)) \
X(void,   glFramebufferRenderbuffer,(GLenum,GLenum,GLenum,GLuint)) \
X(GLenum, glCheckFramebufferStatus,(GLenum)) \
X(void,   glGenRenderbuffers,  (GLsizei,GLuint*)) \
X(void,   glBindRenderbuffer,  (GLenum,GLuint)) \
X(void,   glDeleteRenderbuffers,(GLsizei,const GLuint*)) \
X(void,   glRenderbufferStorage,(GLenum,GLenum,GLsizei,GLsizei)) \
X(void,   glRenderbufferStorageMultisample,(GLenum,GLsizei,GLenum,GLsizei,GLsizei)) \
X(void,   glBlitFramebuffer,   (GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)) \
X(void,   glDrawBuffers,       (GLsizei,const GLenum*))

#define X(r,n,a) typedef r (APIENTRY *PFN_##n) a; extern PFN_##n p_##n;
GL_FUNCS
#undef X

/* call sites read exactly as the GL spec does */
#define glGenBuffers            p_glGenBuffers
#define glBindBuffer            p_glBindBuffer
#define glBufferData            p_glBufferData
#define glBufferSubData         p_glBufferSubData
#define glDeleteBuffers         p_glDeleteBuffers
#define glGenVertexArrays       p_glGenVertexArrays
#define glBindVertexArray       p_glBindVertexArray
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glVertexAttribPointer   p_glVertexAttribPointer
#define glVertexAttribIPointer  p_glVertexAttribIPointer
#define glVertexAttribDivisor   p_glVertexAttribDivisor
#define glDrawArraysInstanced   p_glDrawArraysInstanced
#define glCreateShader          p_glCreateShader
#define glShaderSource          p_glShaderSource
#define glCompileShader         p_glCompileShader
#define glGetShaderiv           p_glGetShaderiv
#define glGetShaderInfoLog      p_glGetShaderInfoLog
#define glDeleteShader          p_glDeleteShader
#define glCreateProgram         p_glCreateProgram
#define glAttachShader          p_glAttachShader
#define glLinkProgram           p_glLinkProgram
#define glGetProgramiv          p_glGetProgramiv
#define glGetProgramInfoLog     p_glGetProgramInfoLog
#define glUseProgram            p_glUseProgram
#define glDeleteProgram         p_glDeleteProgram
#define glGetUniformLocation    p_glGetUniformLocation
#define glGetUniformfv          p_glGetUniformfv
#define glUniform1f             p_glUniform1f
#define glUniform1fv            p_glUniform1fv
#define glUniform1i             p_glUniform1i
#define glUniform2f             p_glUniform2f
#define glUniform3f             p_glUniform3f
#define glUniform3fv            p_glUniform3fv
#define glUniform4fv            p_glUniform4fv
#define glUniformMatrix3fv      p_glUniformMatrix3fv
#define glUniformMatrix4fv      p_glUniformMatrix4fv
#define glActiveTexture         p_glActiveTexture
#define glGenerateMipmap        p_glGenerateMipmap
#define glTexImage3D            p_glTexImage3D
#define glTexSubImage3D         p_glTexSubImage3D
#define glGenFramebuffers       p_glGenFramebuffers
#define glBindFramebuffer       p_glBindFramebuffer
#define glDeleteFramebuffers    p_glDeleteFramebuffers
#define glFramebufferTexture2D  p_glFramebufferTexture2D
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glGenRenderbuffers      p_glGenRenderbuffers
#define glBindRenderbuffer      p_glBindRenderbuffer
#define glDeleteRenderbuffers   p_glDeleteRenderbuffers
#define glRenderbufferStorage   p_glRenderbufferStorage
#define glRenderbufferStorageMultisample p_glRenderbufferStorageMultisample
#define glBlitFramebuffer       p_glBlitFramebuffer
#define glDrawBuffers           p_glDrawBuffers

int         glLoad(void);          /* 0 if an entry point is missing */
const char *glLoadMissing(void);   /* which one */

#endif
