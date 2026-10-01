//
// LeviBoost - host test suite.
//
// Compiles the real optimization engine (src/Optimizations.cpp) and the
// settings module against fake GL/EGL entry points and asserts that every Mod
// Menu toggle really changes what reaches the driver.  No Android device is
// needed:
//
//   g++ -std=c++20 -O1 -Isrc -Itests/stubs tests/optimizations_test.cpp
//       src/Optimizations.cpp src/Settings.cpp -o /tmp/leviboost_tests
//       -o /tmp/leviboost_tests && /tmp/leviboost_tests
//

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "GlApi.hpp"
#include "LeviBoost.hpp"
#include "Optimizations.hpp"
#include "Settings.hpp"

using namespace leviboost;

// The tests drive the detours directly, so the hook manager is stubbed out
// here.  (The Android build compiles the real src/GlApi.cpp.)
namespace leviboost::gl {
Api g_api;
bool HooksReady() { return true; }
int HookedFunctionCount() { return 0; }
const std::string &LibraryReport() {
    static const std::string report = "host tests";
    return report;
}
void SetDrawCallCountingEnabled(bool) {}
void RefreshHookGroups() {}
void RemoveAll() {}
} // namespace leviboost::gl

namespace {

// ---------------------------------------------------------------------------
// Call recording
// ---------------------------------------------------------------------------
struct Call {
    std::string name;
    std::vector<double> args;
};

std::vector<Call> g_calls;

void Record(const char *name, std::vector<double> args = {}) {
    g_calls.push_back(Call{name, std::move(args)});
}

void ClearCalls() { g_calls.clear(); }

int CountCalls(const char *name) {
    int count = 0;
    for (const Call &call : g_calls) {
        if (call.name == name) ++count;
    }
    return count;
}

const Call *LastCall(const char *name) {
    for (auto it = g_calls.rbegin(); it != g_calls.rend(); ++it) {
        if (it->name == name) return &*it;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Simulated clock and sleep
// ---------------------------------------------------------------------------
std::int64_t g_now = 1000000000LL;
std::int64_t g_slept = 0;

std::int64_t FakeClock() { return g_now; }
void FakeSleep(std::int64_t nanos) {
    g_slept += nanos;
    g_now += nanos;
}

// ---------------------------------------------------------------------------
// Fake driver
// ---------------------------------------------------------------------------
constexpr GLint kSurfaceWidth = 1000;
constexpr GLint kSurfaceHeight = 800;

GLenum g_framebufferStatus = GL_FRAMEBUFFER_COMPLETE;

EGLBoolean FakeSwapInterval(EGLDisplay, EGLint interval) {
    Record("eglSwapInterval", {static_cast<double>(interval)});
    return EGL_TRUE;
}
EGLBoolean FakeSwapBuffers(EGLDisplay, EGLSurface) {
    Record("eglSwapBuffers");
    return EGL_TRUE;
}
EGLBoolean FakeQuerySurface(EGLDisplay, EGLSurface, EGLint attribute,
                            EGLint *value) {
    if (attribute == EGL_WIDTH) {
        *value = kSurfaceWidth;
        return EGL_TRUE;
    }
    if (attribute == EGL_HEIGHT) {
        *value = kSurfaceHeight;
        return EGL_TRUE;
    }
    return EGL_FALSE;
}
EGLBoolean FakeChooseConfig(EGLDisplay, const EGLint *attribs, EGLConfig *,
                            EGLint, EGLint *) {
    std::vector<double> values;
    for (int i = 0; attribs && attribs[i] != EGL_NONE && i < 200; i += 2) {
        if (attribs[i + 1] == EGL_NONE) break;
        values.push_back(static_cast<double>(attribs[i]));
        values.push_back(static_cast<double>(attribs[i + 1]));
    }
    Record("eglChooseConfig", values);
    return EGL_TRUE;
}
EGLBoolean FakeGetConfigAttrib(EGLDisplay, EGLConfig, EGLint attribute,
                               EGLint *value) {
    *value = attribute == EGL_SAMPLES ? 4 : 0;
    Record("eglGetConfigAttrib", {static_cast<double>(attribute)});
    return EGL_TRUE;
}
EGLBoolean FakeMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext) {
    Record("eglMakeCurrent");
    return EGL_TRUE;
}
__eglMustCastToProperFunctionPointerType FakeGetProcAddress(const GLchar *) {
    return nullptr;
}

void GL_APIENTRY FakeViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    Record("glViewport", {static_cast<double>(x), static_cast<double>(y),
                          static_cast<double>(width), static_cast<double>(height)});
}
void GL_APIENTRY FakeScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    Record("glScissor", {static_cast<double>(x), static_cast<double>(y),
                         static_cast<double>(width), static_cast<double>(height)});
}
void GL_APIENTRY FakeClear(GLbitfield mask) {
    Record("glClear", {static_cast<double>(mask)});
}
void GL_APIENTRY FakeFinish() { Record("glFinish"); }
void GL_APIENTRY FakeFlush() { Record("glFlush"); }

void GL_APIENTRY FakeTexParameterf(GLenum, GLenum pname, GLfloat param) {
    Record("glTexParameterf",
           {static_cast<double>(pname), static_cast<double>(param)});
}
void GL_APIENTRY FakeTexParameteri(GLenum, GLenum pname, GLint param) {
    Record("glTexParameteri",
           {static_cast<double>(pname), static_cast<double>(param)});
}
void GL_APIENTRY FakeTexParameterfv(GLenum, GLenum pname, const GLfloat *params) {
    Record("glTexParameterfv",
           {static_cast<double>(pname), params ? static_cast<double>(params[0]) : 0.0});
}
void GL_APIENTRY FakeTexParameteriv(GLenum, GLenum pname, const GLint *params) {
    Record("glTexParameteriv",
           {static_cast<double>(pname), params ? static_cast<double>(params[0]) : 0.0});
}

void GL_APIENTRY FakeEnable(GLenum) { Record("glEnable"); }
void GL_APIENTRY FakeDisable(GLenum) { Record("glDisable"); }
void GL_APIENTRY FakeActiveTexture(GLenum) { Record("glActiveTexture"); }
void GL_APIENTRY FakeBindTexture(GLenum, GLuint texture) {
    Record("glBindTexture", {static_cast<double>(texture)});
}
void GL_APIENTRY FakeBindBuffer(GLenum, GLuint) { Record("glBindBuffer"); }
void GL_APIENTRY FakeUseProgram(GLuint) { Record("glUseProgram"); }
void GL_APIENTRY FakeDepthMask(GLboolean) { Record("glDepthMask"); }
void GL_APIENTRY FakeBlendFunc(GLenum, GLenum) { Record("glBlendFunc"); }
void GL_APIENTRY FakeCullFace(GLenum) { Record("glCullFace"); }
void GL_APIENTRY FakeFrontFace(GLenum) { Record("glFrontFace"); }
void GL_APIENTRY FakeDeleteTextures(GLsizei, const GLuint *) {
    Record("glDeleteTextures");
}

void GL_APIENTRY FakeBindFramebuffer(GLenum target, GLuint framebuffer) {
    Record("glBindFramebuffer",
           {static_cast<double>(target), static_cast<double>(framebuffer)});
}
void GL_APIENTRY FakeBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                                     GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                     GLbitfield, GLenum) {
    Record("glBlitFramebuffer",
           {static_cast<double>(sx0), static_cast<double>(sy0),
            static_cast<double>(sx1), static_cast<double>(sy1),
            static_cast<double>(dx0), static_cast<double>(dy0),
            static_cast<double>(dx1), static_cast<double>(dy1)});
}
void GL_APIENTRY FakeGetIntegerv(GLenum pname, GLint *params) {
    switch (pname) {
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
        params[0] = 0;
        params[1] = 0;
        params[2] = kSurfaceWidth;
        params[3] = kSurfaceHeight;
        break;
    case GL_COLOR_WRITEMASK:
        params[0] = params[1] = params[2] = params[3] = 1;
        break;
    case GL_DEPTH_WRITEMASK:
        params[0] = 1;
        break;
    case GL_ACTIVE_TEXTURE:
        params[0] = GL_TEXTURE0;
        break;
    case GL_CURRENT_PROGRAM:
    case GL_TEXTURE_BINDING_2D:
    case GL_DRAW_FRAMEBUFFER_BINDING:
    case GL_READ_FRAMEBUFFER_BINDING:
        params[0] = 0;
        break;
    default:
        params[0] = 0;
        break;
    }
}
GLboolean GL_APIENTRY FakeIsEnabled(GLenum) { return GL_FALSE; }
GLenum GL_APIENTRY FakeGetError() { return GL_NO_ERROR; }
void GL_APIENTRY FakeColorMask(GLboolean, GLboolean, GLboolean, GLboolean) {
    Record("glColorMask");
}
void GL_APIENTRY FakeGenTextures(GLsizei, GLuint *textures) {
    static GLuint next = 100;
    textures[0] = next++;
    Record("glGenTextures");
}
void GL_APIENTRY FakeTexImage2D(GLenum, GLint, GLint, GLsizei width,
                                GLsizei height, GLint, GLenum, GLenum,
                                const void *) {
    Record("glTexImage2D", {static_cast<double>(width), static_cast<double>(height)});
}
void GL_APIENTRY FakeGenFramebuffers(GLsizei, GLuint *framebuffers) {
    static GLuint next = 500;
    framebuffers[0] = next++;
    Record("glGenFramebuffers");
}
void GL_APIENTRY FakeDeleteFramebuffers(GLsizei, const GLuint *) {
    Record("glDeleteFramebuffers");
}
void GL_APIENTRY FakeFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint) {
    Record("glFramebufferTexture2D");
}
GLenum GL_APIENTRY FakeCheckFramebufferStatus(GLenum) {
    Record("glCheckFramebufferStatus");
    return g_framebufferStatus;
}
void GL_APIENTRY FakeDrawArrays(GLenum, GLint, GLsizei) {
    Record("glDrawArrays");
}
void GL_APIENTRY FakeDrawElements(GLenum, GLsizei, GLenum, const void *) {
    Record("glDrawElements");
}

// ---------------------------------------------------------------------------
// Test scaffolding
// ---------------------------------------------------------------------------
int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

void UseFakeDriver() {
    gl::g_api = gl::Api{};
    gl::g_api.eglSwapBuffers = &FakeSwapBuffers;
    gl::g_api.eglSwapInterval = &FakeSwapInterval;
    gl::g_api.eglQuerySurface = &FakeQuerySurface;
    gl::g_api.eglChooseConfig = &FakeChooseConfig;
    gl::g_api.eglGetConfigAttrib = &FakeGetConfigAttrib;
    gl::g_api.eglMakeCurrent = &FakeMakeCurrent;
    gl::g_api.eglGetProcAddress = &FakeGetProcAddress;
    gl::g_api.glViewport = &FakeViewport;
    gl::g_api.glScissor = &FakeScissor;
    gl::g_api.glClear = &FakeClear;
    gl::g_api.glFinish = &FakeFinish;
    gl::g_api.glFlush = &FakeFlush;
    gl::g_api.glTexParameterf = &FakeTexParameterf;
    gl::g_api.glTexParameteri = &FakeTexParameteri;
    gl::g_api.glTexParameterfv = &FakeTexParameterfv;
    gl::g_api.glTexParameteriv = &FakeTexParameteriv;
    gl::g_api.glEnable = &FakeEnable;
    gl::g_api.glDisable = &FakeDisable;
    gl::g_api.glActiveTexture = &FakeActiveTexture;
    gl::g_api.glBindTexture = &FakeBindTexture;
    gl::g_api.glBindBuffer = &FakeBindBuffer;
    gl::g_api.glUseProgram = &FakeUseProgram;
    gl::g_api.glDepthMask = &FakeDepthMask;
    gl::g_api.glBlendFunc = &FakeBlendFunc;
    gl::g_api.glCullFace = &FakeCullFace;
    gl::g_api.glFrontFace = &FakeFrontFace;
    gl::g_api.glDeleteTextures = &FakeDeleteTextures;
    gl::g_api.glBindFramebuffer = &FakeBindFramebuffer;
    gl::g_api.glBlitFramebuffer = &FakeBlitFramebuffer;
    gl::g_api.glGetIntegerv = &FakeGetIntegerv;
    gl::g_api.glGetError = &FakeGetError;
    gl::g_api.glIsEnabled = &FakeIsEnabled;
    gl::g_api.glColorMask = &FakeColorMask;
    gl::g_api.glGenTextures = &FakeGenTextures;
    gl::g_api.glTexImage2D = &FakeTexImage2D;
    gl::g_api.glGenFramebuffers = &FakeGenFramebuffers;
    gl::g_api.glDeleteFramebuffers = &FakeDeleteFramebuffers;
    gl::g_api.glFramebufferTexture2D = &FakeFramebufferTexture2D;
    gl::g_api.glCheckFramebufferStatus = &FakeCheckFramebufferStatus;
    gl::g_api.glDrawArrays = &FakeDrawArrays;
    gl::g_api.glDrawElements = &FakeDrawElements;
}

void ResetEverything() {
    ResetOptions();
    ResetStats();
    g_now = 1000000000LL;
    g_slept = 0;
    g_framebufferStatus = GL_FRAMEBUFFER_COMPLETE;
    g_clockFn = &FakeClock;
    g_sleepFn = &FakeSleep;
    ClearCalls();
    UseFakeDriver();
    opt::InvalidateStateCache();
    std::string ignored;
    opt::TakeRenderScaleFailure(ignored);
}

bool ArgsMatch(const Call *call, const std::vector<double> &expected) {
    if (!call) return false;
    if (call->args.size() != expected.size()) return false;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (std::fabs(call->args[i] - expected[i]) > 0.001) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 1) Uncap FPS
// ---------------------------------------------------------------------------
void TestUncapFps() {
    std::printf("1) Uncap FPS (disable VSync)\n");
    ResetEverything();

    g_options.uncapFps.store(false);
    ClearCalls();
    opt::SwapInterval(reinterpret_cast<EGLDisplay>(1), 1);
    Check(ArgsMatch(LastCall("eglSwapInterval"), {1}),
          "toggle off: the game's vsync request is forwarded untouched");

    g_options.uncapFps.store(true);
    ClearCalls();
    opt::SwapInterval(reinterpret_cast<EGLDisplay>(1), 1);
    Check(ArgsMatch(LastCall("eglSwapInterval"), {0}),
          "toggle on: eglSwapInterval(0) reaches the driver");

    g_options.enabled.store(false);
    ClearCalls();
    opt::SwapInterval(reinterpret_cast<EGLDisplay>(1), 1);
    Check(ArgsMatch(LastCall("eglSwapInterval"), {1}),
          "master switch off: uncapping stops immediately");
    g_options.enabled.store(true);
}

// ---------------------------------------------------------------------------
// 2) Frame rate cap
// ---------------------------------------------------------------------------
void TestFrameCap() {
    std::printf("2) Frame rate cap (thermal guard)\n");
    ResetEverything();

    // 500 FPS worth of frames: no pacing while the cap is off.
    g_options.fpsCapEnabled.store(false);
    g_slept = 0;
    for (int i = 0; i < 5; ++i) {
        g_now += 2000000; // 2 ms per frame
        opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                         reinterpret_cast<EGLSurface>(2));
    }
    Check(g_slept == 0, "cap off: frames are presented without delay");
    Check(CountCalls("eglSwapBuffers") == 5, "every frame still reaches the swap");

    // Cap at 60 FPS: the second and later frames must be paced.
    ResetEverything();
    g_options.fpsCapEnabled.store(true);
    g_options.fpsCap.store(60);
    g_slept = 0;
    for (int i = 0; i < 5; ++i) {
        g_now += 2000000;
        opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                         reinterpret_cast<EGLSurface>(2));
    }
    const double expected = 4.0 * (1000000000.0 / 60.0 - 2000000.0);
    Check(g_slept > 0, "cap on: the engine waits before presenting");
    Check(std::fabs(static_cast<double>(g_slept) - expected) < 2000000.0,
          "the wait matches 1/60 s per frame");
    Check(g_stats.framesCapped.load() >= 4, "throttled frames are counted");

    g_options.fpsCapEnabled.store(false);
    g_slept = 0;
    for (int i = 0; i < 3; ++i) {
        g_now += 2000000;
        opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                         reinterpret_cast<EGLSurface>(2));
    }
    Check(g_slept == 0, "toggle off: pacing stops on the next frame");
}

// ---------------------------------------------------------------------------
// 3) Render scale
// ---------------------------------------------------------------------------
void TestRenderScale() {
    std::printf("3) Render scale\n");
    ResetEverything();

    g_options.renderScaleEnabled.store(true);
    g_options.renderScalePercent.store(50);

    ClearCalls();
    opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2)); // learns the surface size
    Check(g_stats.surfaceWidth.load() == kSurfaceWidth &&
              g_stats.surfaceHeight.load() == kSurfaceHeight,
          "surface size comes from eglQuerySurface");

    ClearCalls();
    opt::Viewport(0, 0, kSurfaceWidth, kSurfaceHeight);
    Check(ArgsMatch(LastCall("glViewport"),
                    {0, 0, kSurfaceWidth / 2, kSurfaceHeight / 2}),
          "full surface viewport is reduced to the render scale");

    ClearCalls();
    opt::Scissor(0, 0, kSurfaceWidth, kSurfaceHeight);
    Check(ArgsMatch(LastCall("glScissor"),
                    {0, 0, kSurfaceWidth / 2, kSurfaceHeight / 2}),
          "the matching scissor rectangle is scaled too");

    ClearCalls();
    opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2));
    Check(CountCalls("glBlitFramebuffer") == 2,
          "swap copies the scaled frame and stretches it back");
    Check(ArgsMatch(LastCall("glBlitFramebuffer"),
                    {0, 0, kSurfaceWidth / 2, kSurfaceHeight / 2, 0, 0,
                     kSurfaceWidth, kSurfaceHeight}),
          "the stretch covers the whole window");
    Check(CountCalls("glFramebufferTexture2D") == 1,
          "an upscale target is created once");
    Check(LastCall("glViewport") != nullptr,
          "the engine's viewport is restored after the upscale");

    // A frame that only renders UI at full size is left alone.
    ClearCalls();
    opt::Viewport(10, 10, 200, 100);
    opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2));
    Check(CountCalls("glBlitFramebuffer") == 0,
          "frames without a scaled full-surface viewport are not stretched");
    Check(ArgsMatch(LastCall("glViewport"), {10, 10, 200, 100}),
          "sub-rectangle viewports pass through untouched");

    // Toggle off restores pass-through rendering.
    g_options.renderScaleEnabled.store(false);
    ClearCalls();
    opt::Viewport(0, 0, kSurfaceWidth, kSurfaceHeight);
    Check(ArgsMatch(LastCall("glViewport"), {0, 0, kSurfaceWidth, kSurfaceHeight}),
          "toggle off: viewports are untouched");

    // A GPU that refuses the blit disables the module instead of corrupting
    // the image, and reports why.
    ResetEverything();
    g_options.renderScaleEnabled.store(true);
    g_options.renderScalePercent.store(75);
    opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2));
    gl::g_api.glBlitFramebuffer = nullptr;
    opt::Viewport(0, 0, kSurfaceWidth, kSurfaceHeight);
    ClearCalls();
    opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2));
    Check(g_options.renderScaleEnabled.load() == false,
          "without GLES3 blit support the feature switches itself off");
}

// ---------------------------------------------------------------------------
// 4-6) Texture sampling
// ---------------------------------------------------------------------------
void TestTextureSampling() {
    std::printf("4) Anisotropic filtering 1x\n");
    ResetEverything();

    g_options.anisoClamp.store(false);
    ClearCalls();
    opt::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0f);
    Check(ArgsMatch(LastCall("glTexParameterf"),
                    {static_cast<double>(GL_TEXTURE_MAX_ANISOTROPY_EXT), 16.0}),
          "toggle off: anisotropy is forwarded");

    g_options.anisoClamp.store(true);
    ClearCalls();
    opt::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0f);
    Check(ArgsMatch(LastCall("glTexParameterf"),
                    {static_cast<double>(GL_TEXTURE_MAX_ANISOTROPY_EXT), 1.0}),
          "toggle on: anisotropy is clamped to 1x");
    Check(g_stats.anisoClamped.load() == 1, "the clamp is counted for the HUD");

    ClearCalls();
    opt::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8);
    Check(ArgsMatch(LastCall("glTexParameteri"),
                    {static_cast<double>(GL_TEXTURE_MAX_ANISOTROPY_EXT), 1.0}),
          "the integer entry point is clamped as well");

    std::printf("5) Fast mipmap filtering\n");
    ResetEverything();
    g_options.mipDowngrade.store(false);
    ClearCalls();
    opt::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                       GL_LINEAR_MIPMAP_LINEAR);
    Check(ArgsMatch(LastCall("glTexParameteri"),
                    {static_cast<double>(GL_TEXTURE_MIN_FILTER),
                     static_cast<double>(GL_LINEAR_MIPMAP_LINEAR)}),
          "toggle off: trilinear filtering is kept");

    g_options.mipDowngrade.store(true);
    ClearCalls();
    opt::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                       GL_LINEAR_MIPMAP_LINEAR);
    Check(ArgsMatch(LastCall("glTexParameteri"),
                    {static_cast<double>(GL_TEXTURE_MIN_FILTER),
                     static_cast<double>(GL_LINEAR_MIPMAP_NEAREST)}),
          "toggle on: trilinear becomes bilinear-between-mips");
    ClearCalls();
    opt::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    Check(ArgsMatch(LastCall("glTexParameteri"),
                    {static_cast<double>(GL_TEXTURE_MIN_FILTER),
                     static_cast<double>(GL_LINEAR)}),
          "unrelated filters are forwarded untouched");

    std::printf("6) Texture LOD bias\n");
    ResetEverything();
    g_options.lodBiasEnabled.store(true);
    g_options.lodBias.store(1.5f);
    ClearCalls();
    opt::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.25f);
    Check(ArgsMatch(LastCall("glTexParameterf"),
                    {static_cast<double>(GL_TEXTURE_LOD_BIAS), 1.75}),
          "the bias is added to the game's value");

    g_options.lodBiasEnabled.store(false);
    ClearCalls();
    opt::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.25f);
    Check(ArgsMatch(LastCall("glTexParameterf"),
                    {static_cast<double>(GL_TEXTURE_LOD_BIAS), 0.25}),
          "toggle off: the game's value is untouched");
}

// ---------------------------------------------------------------------------
// 7) MSAA
// ---------------------------------------------------------------------------
void TestMsaa() {
    std::printf("7) Disable MSAA\n");
    ResetEverything();

    const EGLint msaa[] = {EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4,
                           EGL_BUFFER_SIZE, 32, EGL_NONE};

    ClearCalls();
    opt::ChooseConfig(reinterpret_cast<EGLDisplay>(1), msaa, nullptr, 0, nullptr);
    Check(ArgsMatch(LastCall("eglChooseConfig"),
                    {EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4, EGL_BUFFER_SIZE, 32}),
          "toggle off: the request is forwarded unchanged");

    g_options.msaaOff.store(true);
    ClearCalls();
    opt::ChooseConfig(reinterpret_cast<EGLDisplay>(1), msaa, nullptr, 0, nullptr);
    Check(ArgsMatch(LastCall("eglChooseConfig"),
                    {EGL_SAMPLE_BUFFERS, 0, EGL_SAMPLES, 0, EGL_BUFFER_SIZE, 32}),
          "toggle on: multisampling is removed from the config request");
    Check(g_stats.msaaConfigs.load() == 1, "the rewrite is counted");

    ClearCalls();
    EGLint samples = 4;
    opt::GetConfigAttrib(reinterpret_cast<EGLDisplay>(1), nullptr, EGL_SAMPLES,
                         &samples);
    Check(samples == 0, "queries for EGL_SAMPLES report zero as well");

    const EGLint noMsaa[] = {EGL_BUFFER_SIZE, 32, EGL_NONE};
    ClearCalls();
    opt::ChooseConfig(reinterpret_cast<EGLDisplay>(1), noMsaa, nullptr, 0, nullptr);
    Check(ArgsMatch(LastCall("eglChooseConfig"), {EGL_BUFFER_SIZE, 32}),
          "requests without multisampling are passed straight through");
}

// ---------------------------------------------------------------------------
// 8) State dedup
// ---------------------------------------------------------------------------
void TestStateDedup() {
    std::printf("8) Skip redundant GL state calls\n");
    ResetEverything();

    g_options.stateDedup.store(false);
    ClearCalls();
    opt::Enable(GL_BLEND);
    opt::Enable(GL_BLEND);
    Check(CountCalls("glEnable") == 2,
          "toggle off: duplicate glEnable calls reach the driver");

    g_options.stateDedup.store(true);
    opt::InvalidateStateCache();
    ClearCalls();
    opt::Enable(GL_BLEND);
    opt::Enable(GL_BLEND);
    opt::Enable(GL_DEPTH_TEST);
    Check(CountCalls("glEnable") == 2, "toggle on: the duplicate is dropped");
    Check(g_stats.stateCallsSkipped.load() == 1, "skipped calls are counted");

    ClearCalls();
    opt::Disable(GL_BLEND);
    opt::Enable(GL_BLEND);
    Check(CountCalls("glEnable") == 1 && CountCalls("glDisable") == 1,
          "state changes are still forwarded");

    opt::InvalidateStateCache();
    ClearCalls();
    opt::BindTexture(GL_TEXTURE_2D, 7);
    opt::BindTexture(GL_TEXTURE_2D, 7);
    Check(CountCalls("glBindTexture") == 1, "duplicate texture binds are dropped");

    const GLuint deleted = 7;
    opt::DeleteTextures(1, &deleted);
    ClearCalls();
    opt::BindTexture(GL_TEXTURE_2D, deleted);
    Check(CountCalls("glBindTexture") == 1,
          "a deleted texture name is never assumed to still be bound");

    ClearCalls();
    opt::UseProgram(3);
    opt::UseProgram(3);
    Check(CountCalls("glUseProgram") == 1, "duplicate program binds are dropped");

    opt::MakeCurrent(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2),
                     reinterpret_cast<EGLSurface>(2),
                     reinterpret_cast<EGLContext>(3));
    ClearCalls();
    opt::UseProgram(3);
    Check(CountCalls("glUseProgram") == 1,
          "a context switch clears the cached state");

    g_options.stateDedup.store(false);
    ClearCalls();
    opt::BindTexture(GL_TEXTURE_2D, 9);
    opt::BindTexture(GL_TEXTURE_2D, 9);
    Check(CountCalls("glBindTexture") == 2,
          "toggle off: caching stops immediately");
}

// ---------------------------------------------------------------------------
// 9) glFinish
// ---------------------------------------------------------------------------
void TestGlFinish() {
    std::printf("9) Remove glFinish() stalls\n");
    ResetEverything();

    g_options.noGlFinish.store(false);
    ClearCalls();
    opt::Finish();
    Check(CountCalls("glFinish") == 1 && CountCalls("glFlush") == 0,
          "toggle off: glFinish is called as before");

    g_options.noGlFinish.store(true);
    ClearCalls();
    opt::Finish();
    Check(CountCalls("glFlush") == 1 && CountCalls("glFinish") == 0,
          "toggle on: glFinish becomes glFlush");
    Check(g_stats.finishesSkipped.load() == 1, "the conversion is counted");
}

// ---------------------------------------------------------------------------
// 10) Redundant clears
// ---------------------------------------------------------------------------
void TestClears() {
    std::printf("10) Skip redundant colour clears\n");
    ResetEverything();

    const GLbitfield both = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;

    g_options.skipClears.store(false);
    ClearCalls();
    opt::Clear(both);
    Check(ArgsMatch(LastCall("glClear"), {static_cast<double>(both)}),
          "toggle off: the clear is forwarded");

    g_options.skipClears.store(true);
    ClearCalls();
    opt::Clear(both);
    Check(ArgsMatch(LastCall("glClear"), {static_cast<double>(GL_DEPTH_BUFFER_BIT)}),
          "toggle on: the colour bit is dropped, depth is kept");
    Check(g_stats.clearsSkipped.load() == 1, "the skip is counted");

    ClearCalls();
    opt::Clear(GL_DEPTH_BUFFER_BIT);
    Check(ArgsMatch(LastCall("glClear"), {static_cast<double>(GL_DEPTH_BUFFER_BIT)}),
          "depth-only clears are always forwarded");

    // Clears on an off-screen target must never be touched.
    ClearCalls();
    opt::BindFramebuffer(GL_FRAMEBUFFER, 42);
    opt::Clear(both);
    Check(ArgsMatch(LastCall("glClear"), {static_cast<double>(both)}),
          "clears on a render target are left alone");
}

// ---------------------------------------------------------------------------
// Master switch + stats
// ---------------------------------------------------------------------------
void TestMasterSwitchAndStats() {
    std::printf("Master switch and statistics\n");
    ResetEverything();

    Check(EnabledOptimizationCount() == 5, "the default profile counts 5/10");
    g_options.enabled.store(false);
    Check(EnabledOptimizationCount() == 0, "master switch off reports 0/10");

    ClearCalls();
    opt::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    opt::Finish();
    opt::Clear(GL_COLOR_BUFFER_BIT);
    opt::SwapInterval(reinterpret_cast<EGLDisplay>(1), 1);
    Check(ArgsMatch(LastCall("glTexParameterf"),
                    {static_cast<double>(GL_TEXTURE_MAX_ANISOTROPY_EXT), 8.0}),
          "master switch off: anisotropy untouched");
    Check(CountCalls("glFlush") == 0 && CountCalls("glFinish") == 1,
          "master switch off: glFinish is kept");
    Check(ArgsMatch(LastCall("glClear"), {static_cast<double>(GL_COLOR_BUFFER_BIT)}),
          "master switch off: clears are forwarded");
    Check(ArgsMatch(LastCall("eglSwapInterval"), {1}),
          "master switch off: vsync is left alone");
    g_options.enabled.store(true);

    ResetEverything();
    const std::uint64_t swaps = g_stats.swaps.load();
    g_now += 16666666; // 16.67 ms frame
    opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2));
    g_now += 16666666;
    opt::SwapBuffers(reinterpret_cast<EGLDisplay>(1),
                     reinterpret_cast<EGLSurface>(2));
    Check(g_stats.swaps.load() == swaps + 2, "every present is counted");
    Check(std::fabs(g_stats.fps.load() - 60.0f) < 1.0f,
          "the FPS estimate settles on the frame rate");
    Check(std::fabs(g_stats.frameTimeMs.load() - 16.666f) < 0.5f,
          "the frame time estimate matches");
}

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------
void TestSettings() {
    std::printf("Settings persistence\n");
    ResetEverything();

    g_options.uncapFps.store(false);
    g_options.fpsCapEnabled.store(true);
    g_options.fpsCap.store(45);
    g_options.renderScaleEnabled.store(true);
    g_options.renderScalePercent.store(70);
    g_options.lodBiasEnabled.store(true);
    g_options.lodBias.store(1.25f);
    g_options.stateDedup.store(false);
    g_options.panicKey.store(25);
    const settings::Values saved = settings::Capture();

    const std::string text = settings::Serialize(saved);
    settings::Values parsed;
    Check(settings::Deserialize(text, parsed), "the settings text is parsed");
    Check(parsed.fpsCap == 45 && parsed.renderScalePercent == 70 &&
              std::fabs(parsed.lodBias - 1.25f) < 0.001f &&
              parsed.fpsCapEnabled && parsed.renderScaleEnabled &&
              parsed.lodBiasEnabled && !parsed.uncapFps && !parsed.stateDedup &&
              parsed.panicKey == 25,
          "every value survives a round trip");

    g_options.fpsCap.store(60);
    g_options.lodBias.store(0.0f);
    settings::Apply(parsed);
    Check(g_options.fpsCap.load() == 45 &&
              std::fabs(g_options.lodBias.load() - 1.25f) < 0.001f,
          "applying the settings restores the live options");

    // Out of range values are clamped, not trusted.
    settings::Values silly;
    silly.fpsCap = 5000;
    silly.renderScalePercent = 5;
    silly.hudCorner = 99;
    settings::Apply(silly);
    Check(g_options.fpsCap.load() == 480 &&
              g_options.renderScalePercent.load() == 50 &&
              g_options.hudCorner.load() == 3,
          "out of range settings are clamped");
}

} // namespace

int main() {
    std::printf("LeviBoost optimization engine tests\n");
    std::printf("===================================\n");

    TestUncapFps();
    TestFrameCap();
    TestRenderScale();
    TestTextureSampling();
    TestMsaa();
    TestStateDedup();
    TestGlFinish();
    TestClears();
    TestMasterSwitchAndStats();
    TestSettings();

    std::printf("===================================\n");
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
