#pragma once
//
// LeviBoost - EGL/GLES function resolution and hook plumbing.
//
// Bedrock renders through EGL + GLES (ANGLE on most devices).  Instead of
// patching game code, LeviBoost detours the GL entry points themselves:
//
//   1. every loaded graphics library is discovered by scanning /proc/self/maps,
//   2. the entry points we care about are resolved with dlsym(),
//   3. pl::memory::hook() installs the detour and hands back a trampoline that
//      calls the original driver implementation.
//
// Functions an application fetches through eglGetProcAddress() are covered by
// hooking eglGetProcAddress itself and answering with our detour.
//

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace leviboost::gl {

// Some NDK revisions ship an EGL header without the PFNEGL* function pointer
// typedefs, so LeviBoost declares its own (identical) ones.
using LbEglSwapBuffers = EGLBoolean (*)(EGLDisplay display, EGLSurface surface);
using LbEglSwapBuffersWithDamageKHR = EGLBoolean (*)(EGLDisplay display,
                                                     EGLSurface surface,
                                                     const EGLint *rects,
                                                     EGLint nRects);
using LbEglSwapInterval = EGLBoolean (*)(EGLDisplay display, EGLint interval);
using LbEglGetProcAddress = __eglMustCastToProperFunctionPointerType (*)(
    const char *procName);
using LbEglQuerySurface = EGLBoolean (*)(EGLDisplay display, EGLSurface surface,
                                         EGLint attribute, EGLint *value);
using LbEglChooseConfig = EGLBoolean (*)(EGLDisplay display,
                                         const EGLint *attribs,
                                         EGLConfig *configs, EGLint configSize,
                                         EGLint *numConfig);
using LbEglGetConfigAttrib = EGLBoolean (*)(EGLDisplay display, EGLConfig config,
                                            EGLint attribute, EGLint *value);
using LbEglMakeCurrent = EGLBoolean (*)(EGLDisplay display, EGLSurface draw,
                                        EGLSurface read, EGLContext context);
using LbEglDestroyContext = EGLBoolean (*)(EGLDisplay display,
                                           EGLContext context);

// ---------------------------------------------------------------------------
// Original driver entry points (filled in by Install())
// ---------------------------------------------------------------------------
struct Api {
    // EGL
    LbEglSwapBuffers eglSwapBuffers{};
    LbEglSwapBuffersWithDamageKHR eglSwapBuffersWithDamageKHR{};
    LbEglSwapInterval eglSwapInterval{};
    LbEglGetProcAddress eglGetProcAddress{};
    LbEglQuerySurface eglQuerySurface{};
    LbEglChooseConfig eglChooseConfig{};
    LbEglGetConfigAttrib eglGetConfigAttrib{};
    LbEglMakeCurrent eglMakeCurrent{};
    LbEglDestroyContext eglDestroyContext{};

    // GLES - state that the render-scale path has to save and restore
    PFNGLVIEWPORTPROC glViewport{};
    PFNGLSCISSORPROC glScissor{};
    PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer{};
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer{};
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers{};
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers{};
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D{};
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus{};
    PFNGLGENTEXTURESPROC glGenTextures{};
    PFNGLDELETETEXTURESPROC glDeleteTextures{};
    PFNGLTEXIMAGE2DPROC glTexImage2D{};
    PFNGLGETINTEGERVPROC glGetIntegerv{};
    PFNGLGETERRORPROC glGetError{};
    PFNGLISENABLEDPROC glIsEnabled{};
    PFNGLCOLORMASKPROC glColorMask{};
    PFNGLFINISHPROC glFinish{};
    PFNGLFLUSHPROC glFlush{};
    PFNGLCLEARPROC glClear{};

    // GLES - state the upscaler has to restore, plus the HUD counters
    PFNGLUSEPROGRAMPROC glUseProgram{};
    PFNGLDRAWARRAYSPROC glDrawArrays{};
    PFNGLDRAWELEMENTSPROC glDrawElements{};
    PFNGLACTIVETEXTUREPROC glActiveTexture{};
    PFNGLBINDTEXTUREPROC glBindTexture{};
    PFNGLDEPTHMASKPROC glDepthMask{};
    PFNGLBLENDFUNCPROC glBlendFunc{};
    PFNGLENABLEPROC glEnable{};
    PFNGLDISABLEPROC glDisable{};
    PFNGLBINDBUFFERPROC glBindBuffer{};
    PFNGLCULLFACEPROC glCullFace{};
    PFNGLFRONTFACEPROC glFrontFace{};
    PFNGLTEXPARAMETERIPROC glTexParameteri{};
    PFNGLTEXPARAMETERFPROC glTexParameterf{};
    PFNGLTEXPARAMETERFVPROC glTexParameterfv{};
    PFNGLTEXPARAMETERIVPROC glTexParameteriv{};
};

extern Api g_api;

/// True once Install() has resolved at least the swap functions.
bool Installed();
bool HooksReady();

/// Number of detours that are currently installed (shown by the HUD).
int HookedFunctionCount();

/// Resolves the graphics libraries and installs every detour.  Safe to call
/// repeatedly: it returns true as soon as the hooks are in place.
bool Install();

/// Removes every detour (mod unload).
void RemoveAll();

/// Enables or disables the draw-call counters used by the HUD.  The hooks are
/// only installed while the HUD asks for them.
void SetDrawCallCountingEnabled(bool enabled);

/// Installs/removes the optional hook groups (state dedup, HUD counters) so
/// they follow the live configuration.  Cheap; the worker thread calls it.
void RefreshHookGroups();

/// Human readable report about which libraries were found (for the log).
const std::string &LibraryReport();

} // namespace leviboost::gl
