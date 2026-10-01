#pragma once
//
// LeviBoost - the ten optimizations.
//
// Each function below is a detour body installed by GlApi.cpp.  Every detour
// reads the live option flags on entry, so a Mod Menu toggle changes behaviour
// on the next call with no restart and no re-hooking.
//

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdint>
#include <string>

#include "LeviBoost.hpp"

namespace leviboost::opt {

/// Everything the Mod Menu can touch, in one log-friendly line.
std::string DescribeState();

/// Drops per-thread cached GL state (called after eglMakeCurrent and after the
/// upscaler touches GL itself).
void InvalidateStateCache();

/// Resets the render-scale bookkeeping for a new frame.
void BeginFrame();

/// Returns true (once) when a runtime problem switched Render Scale off, with
/// a human readable reason.  The mod entry point reports it and syncs the
/// Mod Menu switch.
bool TakeRenderScaleFailure(std::string &reason);

// --- EGL detours -----------------------------------------------------------
EGLBoolean SwapBuffers(EGLDisplay display, EGLSurface surface);
EGLBoolean SwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                    const EGLint *rects, EGLint nRects);
EGLBoolean SwapInterval(EGLDisplay display, EGLint interval);
EGLBoolean ChooseConfig(EGLDisplay display, const EGLint *attribs,
                        EGLConfig *configs, EGLint configSize,
                        EGLint *numConfig);
EGLBoolean GetConfigAttrib(EGLDisplay display, EGLConfig config,
                           EGLint attribute, EGLint *value);
EGLBoolean MakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read,
                       EGLContext context);
__eglMustCastToProperFunctionPointerType GetProcAddress(const char *name);

// --- GLES detours ---------------------------------------------------------
void Viewport(GLint x, GLint y, GLsizei width, GLsizei height);
void Scissor(GLint x, GLint y, GLsizei width, GLsizei height);
void BindFramebuffer(GLenum target, GLuint framebuffer);
void Enable(GLenum cap);
void Disable(GLenum cap);
void ActiveTexture(GLenum texture);
void BindTexture(GLenum target, GLuint texture);
void BindBuffer(GLenum target, GLuint buffer);
void UseProgram(GLuint program);
void DepthMask(GLboolean flag);
void BlendFunc(GLenum source, GLenum destination);
void CullFace(GLenum mode);
void FrontFace(GLenum mode);
void DeleteTextures(GLsizei count, const GLuint *textures);
void TexParameterf(GLenum target, GLenum pname, GLfloat param);
void TexParameteri(GLenum target, GLenum pname, GLint param);
void TexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void TexParameteriv(GLenum target, GLenum pname, const GLint *params);
void Finish();
void Flush();
void Clear(GLbitfield mask);
void ColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void DrawArrays(GLenum mode, GLint first, GLsizei count);
void DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);

} // namespace leviboost::opt
