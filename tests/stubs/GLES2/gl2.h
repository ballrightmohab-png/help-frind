// Minimal GLES 2.0/3.0 declarations for the host test build.
//
// Provides the types, constants and function-pointer typedefs LeviBoost uses,
// so the optimization engine can be compiled and exercised on a desktop
// machine.  Only on the include path for tests/stubs.
#pragma once

#include <cstddef>
#include <cstdint>

#define GL_APIENTRY
#define GL_APIENTRYP GL_APIENTRY *

using GLenum = std::uint32_t;
using GLboolean = std::uint8_t;
using GLbitfield = std::uint32_t;
using GLbyte = std::int8_t;
using GLshort = std::int16_t;
using GLint = std::int32_t;
using GLsizei = std::int32_t;
using GLubyte = std::uint8_t;
using GLushort = std::uint16_t;
using GLuint = std::uint32_t;
using GLfloat = float;
using GLclampf = float;
using GLchar = char;
using GLvoid = void;

// --- bit masks and enums ---------------------------------------------------
constexpr GLbitfield GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLbitfield GL_STENCIL_BUFFER_BIT = 0x00000400;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLboolean GL_FALSE = 0;
constexpr GLboolean GL_TRUE = 1;
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_INVALID_OPERATION = 0x0502;
constexpr GLenum GL_BYTE = 0x1400;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_SHORT = 0x1402;
constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
constexpr GLenum GL_INT = 0x1404;
constexpr GLenum GL_UNSIGNED_INT = 0x1405;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_DEPTH_COMPONENT = 0x1902;
constexpr GLenum GL_RGB = 0x1907;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr GLenum GL_LINEAR_MIPMAP_NEAREST = 0x2701;
constexpr GLenum GL_NEAREST_MIPMAP_LINEAR = 0x2702;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE = 0x1702;
constexpr GLenum GL_TEXTURE_CUBE_MAP = 0x8513;
constexpr GLenum GL_TEXTURE_BINDING_2D = 0x8069;
constexpr GLenum GL_TEXTURE_BINDING_CUBE_MAP = 0x8514;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_ACTIVE_TEXTURE = 0x84E0;
constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FE;
constexpr GLenum GL_TEXTURE_LOD_BIAS = 0x8501;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_DITHER = 0x0BD0;
constexpr GLenum GL_POLYGON_OFFSET_FILL = 0x8037;
constexpr GLenum GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E;
constexpr GLenum GL_SAMPLE_COVERAGE = 0x80A0;
constexpr GLenum GL_BLEND_SRC_RGB = 0x80C9;
constexpr GLenum GL_BLEND_DST_RGB = 0x80C8;
constexpr GLenum GL_BLEND_SRC_ALPHA = 0x80CB;
constexpr GLenum GL_BLEND_DST_ALPHA = 0x80CA;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_ZERO = 0;
constexpr GLenum GL_CW = 0x0900;
constexpr GLenum GL_CCW = 0x0901;
constexpr GLenum GL_FRONT = 0x0404;
constexpr GLenum GL_BACK = 0x0405;
constexpr GLenum GL_VIEWPORT = 0x0BA2;
constexpr GLenum GL_SCISSOR_BOX = 0x0C10;
constexpr GLenum GL_COLOR_WRITEMASK = 0x0C23;
constexpr GLenum GL_DEPTH_WRITEMASK = 0x0B72;
constexpr GLenum GL_CURRENT_PROGRAM = 0x8B8D;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_ARRAY_BUFFER_BINDING = 0x8894;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING = 0x8895;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING = 0x8CAA;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_RENDERBUFFER_BINDING = 0x8CA7;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_POINTS = 0x0000;

// --- entry point signatures -------------------------------------------------
using PFNGLVIEWPORTPROC = void(GL_APIENTRYP)(GLint x, GLint y, GLsizei w,
                                             GLsizei h);
using PFNGLSCISSORPROC = void(GL_APIENTRYP)(GLint x, GLint y, GLsizei w,
                                            GLsizei h);
using PFNGLBLITFRAMEBUFFERPROC = void(GL_APIENTRYP)(
    GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,
    GLint dy1, GLbitfield mask, GLenum filter);
using PFNGLBINDFRAMEBUFFERPROC = void(GL_APIENTRYP)(GLenum target,
                                                    GLuint framebuffer);
using PFNGLGENFRAMEBUFFERSPROC = void(GL_APIENTRYP)(GLsizei n, GLuint *ids);
using PFNGLDELETEFRAMEBUFFERSPROC = void(GL_APIENTRYP)(GLsizei n,
                                                       const GLuint *ids);
using PFNGLFRAMEBUFFERTEXTURE2DPROC = void(GL_APIENTRYP)(
    GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
    GLint level);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC = GLenum(GL_APIENTRYP)(GLenum target);
using PFNGLGENTEXTURESPROC = void(GL_APIENTRYP)(GLsizei n, GLuint *textures);
using PFNGLDELETETEXTURESPROC = void(GL_APIENTRYP)(GLsizei n,
                                                   const GLuint *textures);
using PFNGLTEXIMAGE2DPROC = void(GL_APIENTRYP)(GLenum target, GLint level,
                                               GLint internalFormat, GLsizei width,
                                               GLsizei height, GLint border,
                                               GLenum format, GLenum type,
                                               const void *pixels);
using PFNGLGETINTEGERVPROC = void(GL_APIENTRYP)(GLenum pname, GLint *params);
using PFNGLGETERRORPROC = GLenum(GL_APIENTRYP)(void);
using PFNGLISENABLEDPROC = GLboolean(GL_APIENTRYP)(GLenum cap);
using PFNGLCOLORMASKPROC = void(GL_APIENTRYP)(GLboolean r, GLboolean g,
                                              GLboolean b, GLboolean a);
using PFNGLFINISHPROC = void(GL_APIENTRYP)(void);
using PFNGLFLUSHPROC = void(GL_APIENTRYP)(void);
using PFNGLCLEARPROC = void(GL_APIENTRYP)(GLbitfield mask);
using PFNGLUSEPROGRAMPROC = void(GL_APIENTRYP)(GLuint program);
using PFNGLDRAWARRAYSPROC = void(GL_APIENTRYP)(GLenum mode, GLint first,
                                               GLsizei count);
using PFNGLDRAWELEMENTSPROC = void(GL_APIENTRYP)(GLenum mode, GLsizei count,
                                                 GLenum type,
                                                 const void *indices);
using PFNGLACTIVETEXTUREPROC = void(GL_APIENTRYP)(GLenum texture);
using PFNGLBINDTEXTUREPROC = void(GL_APIENTRYP)(GLenum target, GLuint texture);
using PFNGLDEPTHMASKPROC = void(GL_APIENTRYP)(GLboolean flag);
using PFNGLBLENDFUNCPROC = void(GL_APIENTRYP)(GLenum sfactor, GLenum dfactor);
using PFNGLENABLEPROC = void(GL_APIENTRYP)(GLenum cap);
using PFNGLDISABLEPROC = void(GL_APIENTRYP)(GLenum cap);
using PFNGLBINDBUFFERPROC = void(GL_APIENTRYP)(GLenum target, GLuint buffer);
using PFNGLCULLFACEPROC = void(GL_APIENTRYP)(GLenum mode);
using PFNGLFRONTFACEPROC = void(GL_APIENTRYP)(GLenum mode);
using PFNGLTEXPARAMETERIPROC = void(GL_APIENTRYP)(GLenum target, GLenum pname,
                                                  GLint param);
using PFNGLTEXPARAMETERFPROC = void(GL_APIENTRYP)(GLenum target, GLenum pname,
                                                  GLfloat param);
using PFNGLTEXPARAMETERFVPROC = void(GL_APIENTRYP)(GLenum target, GLenum pname,
                                                   const GLfloat *params);
using PFNGLTEXPARAMETERIVPROC = void(GL_APIENTRYP)(GLenum target, GLenum pname,
                                                   const GLint *params);
