#if !defined(__OPENGL_RENDER_H__)
#define __OPENGL_RENDER_H__

#include "../../nc_types.h"
#include "opengl_platform.h"

// @defines____________________________________________________________________
enum OGLShaderKind {
    SHADER_KIND_RECT,
    SHADER_KIND_BLUR,
    SHADER_KIND_CAD_LINE,
    SHADER_KIND_CAD_ARC,
    SHADER_KIND_CAD_TRIANGLE,
    SHADER_KIND_CAD_RIBBON,
    SHADER_KIND_COUNT
};

#define OGL_PROC_XLIST                                                                                                                                                                                \
    X(glGenBuffers, void, (GLsizei N, GLuint* Buffers))                                                                                                                                               \
    X(glBindBuffer, void, (GLenum Target, GLuint Buffer))                                                                                                                                             \
    X(glDeleteBuffers, void, (GLsizei N, GLuint* Buffers))                                                                                                                                            \
    X(glGenVertexArrays, void, (GLsizei N, GLuint* Arrays))                                                                                                                                           \
    X(glBindVertexArray, void, (GLuint Array))                                                                                                                                                        \
    X(glCreateProgram, GLuint, (void))                                                                                                                                                                \
    X(glGenFramebuffers, void, (GLsizei N, GLuint* FBOs))                                                                                                                                             \
    X(glDeleteFramebuffers, void, (GLsizei N, GLuint* FBOs))                                                                                                                                          \
    X(glCreateShader, GLuint, (GLenum Type))                                                                                                                                                          \
    X(glShaderSource, void, (GLuint Shader, GLsizei Count, char** String, GLint* Length))                                                                                                             \
    X(glCompileShader, void, (GLuint Shader))                                                                                                                                                         \
    X(glGetShaderiv, void, (GLuint Shader, GLenum PName, GLint* Params))                                                                                                                              \
    X(glGetShaderInfoLog, void, (GLuint Shader, GLsizei BufferSize, GLsizei* Length, char* InfoLog))                                                                                                  \
    X(glGetProgramiv, void, (GLuint Program, GLenum PName, GLint* Params))                                                                                                                            \
    X(glGetProgramInfoLog, void, (GLuint Program, GLsizei BufferSize, GLsizei* Length, char* InfoLog))                                                                                                \
    X(glAttachShader, void, (GLuint Program, GLuint Shader))                                                                                                                                          \
    X(glLinkProgram, void, (GLuint Program))                                                                                                                                                          \
    X(glValidateProgram, void, (GLuint Program))                                                                                                                                                      \
    X(glDeleteShader, void, (GLuint Shader))                                                                                                                                                          \
    X(glUseProgram, void, (GLuint Program))                                                                                                                                                           \
    X(glGetUniformLocation, GLint, (GLuint Program, char* Name))                                                                                                                                      \
    X(glGetAttribLocation, GLint, (GLuint Program, char* Name))                                                                                                                                       \
    X(glEnableVertexAttribArray, void, (GLuint Index))                                                                                                                                                \
    X(glVertexAttribPointer, void, (GLuint Index, GLint Size, GLenum Type, GLboolean Normalised, GLsizei Stride, void const* Pointer))                                                                \
    X(glBufferData, void, (GLenum Target, dptr Size, void* Data, GLenum Usage))                                                                                                                       \
    X(glBufferSubData, void, (GLenum Target, dptr Offset, dptr Size, void const* Data))                                                                                                               \
    X(glBlendFuncSeparate, void, (GLenum SFactorRGB, GLenum DFactorRGB, GLenum SFactorAlpha, GLenum DFactorAlpha))                                                                                    \
    X(glUniform1f, void, (GLint Location, GLfloat V0))                                                                                                                                                \
    X(glUniform2f, void, (GLint Location, GLfloat V0, GLfloat V1))                                                                                                                                    \
    X(glUniform3f, void, (GLint Location, GLfloat V0, GLfloat V1, GLfloat V2))                                                                                                                        \
    X(glUniform4f, void, (GLint Location, GLfloat V0, GLfloat V1, GLfloat V2, GLfloat V3))                                                                                                            \
    X(glUniform4fv, void, (GLint Location, GLsizei Count, GLfloat const* Value))                                                                                                                      \
    X(glUniformMatrix3fv, void, (GLint Location, GLsizei Count, GLboolean Transpose, GLfloat const* Value))                                                                                           \
    X(glUniformMatrix4fv, void, (GLint Location, GLsizei Count, GLboolean Transpose, GLfloat const* Value))                                                                                           \
    X(glUniform1i, void, (GLint Location, GLint V0))                                                                                                                                                  \
    X(glTexImage3D, void, (GLenum Target, GLint Level, GLint InternalFormat, GLsizei Width, GLsizei Height, GLsizei Depth, GLint Border, GLenum Format, GLenum Type, void const* Pixels))             \
    X(glTexSubImage3D, void, (GLenum Target, GLint Level, GLint XOffset, GLint YOffset, GLint ZOffset, GLsizei Width, GLsizei Height, GLsizei Depth, GLenum Format, GLenum Type, void const* Pixels)) \
    X(glGenerateMipmap, void, (GLenum Target))                                                                                                                                                        \
    X(glBindAttribLocation, void, (GLuint ProgramObj, GLuint Index, char* Name))                                                                                                                      \
    X(glBindFramebuffer, void, (GLenum Target, GLuint FBO))                                                                                                                                           \
    X(glBindFragDataLocation, void, (GLuint Program, GLuint Colour, char* Name))                                                                                                                      \
    X(glActiveTexture, void, (GLenum Texture))                                                                                                                                                        \
    X(glVertexAttribDivisor, void, (GLuint Index, GLuint Divisor))                                                                                                                                    \
    X(glDrawArraysInstanced, void, (GLenum Mode, GLint First, GLsizei Count, GLsizei InstanceCount))                                                                                                  \
    X(glDebugMessageCallback, void, (void (*)(GLenum Source, GLenum Type, GLuint ID, GLenum Severity, GLsizei Length, GLchar const* Message, void const* UserParam), void* UserData))                 \
    X(glFramebufferTexture2D, void, (GLenum Target, GLenum Attachment, GLenum TexTarget, GLuint Texture, GLint Level))                                                                                \
    X(glBlitFramebuffer, void, (GLint SrcX0, GLint SrcY0, GLint SrcX1, GLint SrcY1, GLint DstX0, GLint DstY0, GLint DstX1, GLint DstY1, GLbitfield Mask, GLenum filter))                              \
    X(glMapBufferRange, void*, (GLenum Target, GLintptr Offset, GLsizeiptr Length, GLbitfield Access))                                                                                                \
    X(glUnmapBuffer, GLboolean, (GLenum Target))                                                                                                                                                      \


#define X(Y, Z, W) typedef Z Y##FuncType W;
OGL_PROC_XLIST
#undef X

#define GL_USE_PROGRAM_SCOPE(...)          DEFER(glUseProgram(__VA_ARGS__), glUseProgram(0))
#define GL_BIND_VERTEX_ARRAY_SCOPE(...)    DEFER(glBindVertexArray(__VA_ARGS__), glBindVertexArray(0))
#define GL_BIND_FRAME_BUFFER_SCOPE(X, ...) DEFER(glBindFramebuffer((X), __VA_ARGS__), glBindFramebuffer((X), 0))
#define GL_BIND_TEXTURE_SCOPE(X, ...)      DEFER(glBindTexture((X), __VA_ARGS__), glBindTexture((X), 0))
#define GL_ENABLE_SCOPE(...)               DEFER(glEnable(__VA_ARGS__), glDisable(__VA_ARGS__))

// @types______________________________________________________________________
struct OGLAttribute {
    u64    Index;
    Str8   Name;
    GLenum Type;
    u64    Count;
};

struct OGLAttributeArray {
    OGLAttribute* V;
    u64           Count;
};

struct OGLFormatInfo {
    GLint  InternalFormat;
    GLenum Format;
    GLenum BaseType;
};

struct OGLTex2D {
    OGLTex2D*        Next;
    OGLTex2D*        UploadNext;
    void*            UploadData;
    GLuint           ID;
    RendResourceKind ResourceKind;
    RendTex2DFmt     Format;
    v2i32            Size;
    b32              IsUploadPending;
};

struct OGLFlushBuffer {
    OGLFlushBuffer* Next;
    GLuint          ID;
};

struct OGLRenderTarget {
    GLuint FBO;
    GLuint ColourTexture;
};

struct OGLWindow {
    OGLWindow*      Next;
    RendHandle      Handle;
    v2f32           PrevClientRectLength;
    OGLRenderTarget StageTarget;
    OGLRenderTarget StageScratchTarget;
    GLuint          CapturePBO;
    r2i32           CaptureRect;
    b32             IsCapturePending;
};

struct OGLState {
    Arena*          MemPool;
    OGLWindow*      FreeWindow;
    OGLTex2D*       FreeTex2D;
    OGLTex2D*       ToFreeTex2D;
    OGLTex2D*       HeadUploadTex2D;
    OGLTex2D*       TailUploadTex2D;
    Arena*          UploadMemPool;
    Handle          Tex2DMtx;
    GLuint          Shaders[SHADER_KIND_COUNT];
    GLuint          AllPurposeVAO;
    GLuint          ScratchBuffer64KB;
    GLuint          WhiteTexture;
    GLuint          InstanceBuffer;
    u64             InstanceBufferSize;
    Arena*          BufferFlushMemPool;
    OGLFlushBuffer* HeadBufferToFlush;
    OGLFlushBuffer* TailBufferToFlush;
};

// @runtime____________________________________________________________________
#define X(Y, Z, W) global Y##FuncType* Y = NULL;
OGL_PROC_XLIST
#undef X

EXTERN_C_LINK_BEGIN
extern Str8 OGL_SHADER_KIND_NAME_TABLE[SHADER_KIND_COUNT];
extern Str8* OGL_SHADER_KIND_VSHADER_SRC_TABLE[SHADER_KIND_COUNT];
extern Str8* OGL_SHADER_KIND_PSHADER_SRC_TABLE[SHADER_KIND_COUNT];
extern OGLAttributeArray OGL_SHADER_KIND_INPUT_ATTRIBUTES_TABLE[SHADER_KIND_COUNT];
extern OGLAttributeArray OGL_SHADER_KIND_OUTPUT_ATTRIBUTES_TABLE[SHADER_KIND_COUNT];
EXTERN_C_LINK_END

extern OGLState* OGL_STATE;

// @functions__________________________________________________________________
extern vproc* OGLLoadProcedure(char* Name);
extern void OGLInit(CommandLine* CLI);
extern RendHandle OGLEquipWindow(Handle WindowHandle);
extern void OGLUnequipWindow(Handle WindowHandle, RendHandle Renderer);
extern void OGLSelectWindow(Handle WindowHandle, RendHandle Renderer);
extern void OGLSwapWindow(Handle WindowHandle, RendHandle Renderer);
extern b32 OGLIsContextCurrent(void);

RendHandle RendHandleFromTex2D(OGLTex2D* Tex);
OGLTex2D* RendTex2DFromHandle(RendHandle Handle);
OGLFormatInfo OGLFormatInfoFromTex2DFmt(RendTex2DFmt Format);
void OGLInstanceBufferBind(u64 Size);
void OGLDebugMessageFunc(GLenum Source, GLenum Tupe, GLuint ID, GLenum Severity, GLsizei Length, GLchar const* Message, void const* UserParam);

#endif // __OPENGL_RENDER_H__
