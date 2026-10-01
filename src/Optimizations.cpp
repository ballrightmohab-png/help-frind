//
// LeviBoost - the ten optimizations.
//
// Reader's guide: every "detour" below is what the driver call becomes while
// LeviBoost is loaded.  Each one starts by looking at the live option flags,
// so the Mod Menu toggles take effect immediately.  When a flag is off the
// detour forwards the call untouched, which is exactly the unmodded behaviour.
//

#include "Optimizations.hpp"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <new>
#include <ctime>
#include <sstream>

#include "GlApi.hpp"

namespace leviboost {

Options g_options;
Stats g_stats;

void (*g_loggerFn)(int level, const char *message) = nullptr;

namespace {

const char *LevelTag(int level) {
    switch (level) {
    case 0: return "I";
    case 1: return "W";
    default: return "E";
    }
}

} // namespace

void LogMessage(int level, const std::string &message) {
    if (g_loggerFn) {
        g_loggerFn(level, message.c_str());
        return;
    }
    __android_log_print(level == 0 ? ANDROID_LOG_INFO
                                   : (level == 1 ? ANDROID_LOG_WARN
                                                 : ANDROID_LOG_ERROR),
                        kLogTag, "%s %s", LevelTag(level), message.c_str());
}

void LogInfo(const std::string &message) { LogMessage(0, message); }
void LogWarn(const std::string &message) { LogMessage(1, message); }
void LogError(const std::string &message) { LogMessage(2, message); }

std::int64_t (*g_clockFn)() = nullptr;
void (*g_sleepFn)(std::int64_t nanos) = nullptr;

std::int64_t MonotonicNanos() {
    if (g_clockFn) return g_clockFn();
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1000000000LL +
           static_cast<std::int64_t>(ts.tv_nsec);
}

void SleepNanos(std::int64_t nanos) {
    if (nanos <= 0) return;
    if (g_sleepFn) {
        g_sleepFn(nanos);
        return;
    }
    timespec ts{};
    ts.tv_sec = static_cast<time_t>(nanos / 1000000000LL);
    ts.tv_nsec = static_cast<long>(nanos % 1000000000LL);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

void ResetOptions() {
    Options fresh;
    g_options.enabled.store(fresh.enabled.load());
    g_options.uncapFps.store(fresh.uncapFps.load());
    g_options.fpsCapEnabled.store(fresh.fpsCapEnabled.load());
    g_options.fpsCap.store(fresh.fpsCap.load());
    g_options.renderScaleEnabled.store(fresh.renderScaleEnabled.load());
    g_options.renderScalePercent.store(fresh.renderScalePercent.load());
    g_options.anisoClamp.store(fresh.anisoClamp.load());
    g_options.mipDowngrade.store(fresh.mipDowngrade.load());
    g_options.lodBiasEnabled.store(fresh.lodBiasEnabled.load());
    g_options.lodBias.store(fresh.lodBias.load());
    g_options.msaaOff.store(fresh.msaaOff.load());
    g_options.stateDedup.store(fresh.stateDedup.load());
    g_options.noGlFinish.store(fresh.noGlFinish.load());
    g_options.skipClears.store(fresh.skipClears.load());
    g_options.hud.store(fresh.hud.load());
    g_options.hudCorner.store(fresh.hudCorner.load());
    g_options.hudDetail.store(fresh.hudDetail.load());
    g_options.panicKey.store(fresh.panicKey.load());
}

void ResetStats() { new (&g_stats) Stats(); }

int EnabledOptimizationCount() {
    if (!g_options.enabled.load(std::memory_order_relaxed)) return 0;
    int count = 0;
    if (g_options.uncapFps.load(std::memory_order_relaxed)) ++count;
    if (g_options.fpsCapEnabled.load(std::memory_order_relaxed)) ++count;
    if (g_options.renderScaleEnabled.load(std::memory_order_relaxed)) ++count;
    if (g_options.anisoClamp.load(std::memory_order_relaxed)) ++count;
    if (g_options.mipDowngrade.load(std::memory_order_relaxed)) ++count;
    if (g_options.lodBiasEnabled.load(std::memory_order_relaxed)) ++count;
    if (g_options.msaaOff.load(std::memory_order_relaxed)) ++count;
    if (g_options.stateDedup.load(std::memory_order_relaxed)) ++count;
    if (g_options.noGlFinish.load(std::memory_order_relaxed)) ++count;
    if (g_options.skipClears.load(std::memory_order_relaxed)) ++count;
    return count;
}

bool OptimizationsReady() { return gl::HooksReady(); }

} // namespace leviboost

namespace leviboost::opt {
namespace {

using gl::g_api;

// Declared here because the render-scale code below invalidates the caches
// that are implemented further down.
void InvalidateDedupCache();
extern thread_local int t_boundDrawFramebuffer;
extern thread_local int t_boundReadFramebuffer;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
constexpr GLenum kAnisotropyExt = GL_TEXTURE_MAX_ANISOTROPY_EXT; // 0x84FE
// GL_EXT_texture_lod_bias, the extension Android drivers expose for ES2/ES3.
constexpr GLenum kLodBiasExt = 0x8501;

bool ScaleActive() {
    return Active(g_options.renderScaleEnabled);
}

int ScaledDimension(int value, int percent) {
    const int scaled = static_cast<int>(
        (static_cast<long long>(value) * percent) / 100);
    return scaled < 1 ? 1 : scaled;
}

// ---------------------------------------------------------------------------
// 2) Software frame cap
// ---------------------------------------------------------------------------
thread_local std::int64_t t_nextDeadline = 0;
thread_local std::int64_t t_lastSwap = 0;

void ApplyFrameCap(std::int64_t now) {
    if (!Active(g_options.fpsCapEnabled)) {
        t_nextDeadline = 0;
        return;
    }
    const int cap = ClampInt(g_options.fpsCap.load(std::memory_order_relaxed), 10, 480);
    const std::int64_t period = 1000000000LL / cap;
    if (t_nextDeadline == 0) {
        t_nextDeadline = now + period;
        return;
    }
    const std::int64_t guard = 400000; // 0.4 ms of scheduling slack
    if (t_nextDeadline > now + guard) {
        g_stats.framesCapped.fetch_add(1, std::memory_order_relaxed);
        SleepNanos(t_nextDeadline - now - guard);
    }
    t_nextDeadline += period;
    // Fell behind by more than a whole frame (a hitch, a loading screen):
    // resynchronise instead of trying to catch up.
    if (t_nextDeadline < now) t_nextDeadline = now + period;
}

void UpdateFrameStats(std::int64_t now) {
    g_stats.swaps.fetch_add(1, std::memory_order_relaxed);
    if (t_lastSwap != 0) {
        const double frameMs = static_cast<double>(now - t_lastSwap) / 1e6;
        const double previous = g_stats.frameTimeMs.load(std::memory_order_relaxed);
        const double smoothed = previous <= 0.0 ? frameMs
                                                : previous * 0.9 + frameMs * 0.1;
        g_stats.frameTimeMs.store(static_cast<float>(smoothed),
                                  std::memory_order_relaxed);
        g_stats.fps.store(smoothed > 0.0 ? static_cast<float>(1000.0 / smoothed) : 0.0f,
                          std::memory_order_relaxed);
    }
    t_lastSwap = now;
}

// ---------------------------------------------------------------------------
// 3) Render scale
// ---------------------------------------------------------------------------
struct ScaleState {
    bool failed = false;
    bool failurePending = false;
    std::uint64_t frame = 0;
    char failure[160]{};
    EGLSurface surface = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    int scaledWidth = 0;
    int scaledHeight = 0;
    bool scaledThisFrame = false;
    GLuint framebuffer = 0;
    GLuint texture = 0;
    int textureWidth = 0;
    int textureHeight = 0;
};

ScaleState g_scale;

void FailScale(const char *why) {
    if (g_scale.failed) return;
    g_scale.failed = true;
    g_scale.failurePending = true;
    std::strncpy(g_scale.failure, why, sizeof(g_scale.failure) - 1);
    g_options.renderScaleEnabled.store(false, std::memory_order_relaxed);
}

bool EnsureScaleTargets() {
    if (g_scale.framebuffer != 0 && g_scale.textureWidth == g_scale.scaledWidth &&
        g_scale.textureHeight == g_scale.scaledHeight) {
        return true;
    }
    if (!g_api.glGenFramebuffers || !g_api.glGenTextures || !g_api.glTexImage2D ||
        !g_api.glFramebufferTexture2D || !g_api.glCheckFramebufferStatus ||
        !g_api.glBindFramebuffer) {
        FailScale("GLES3 framebuffer entry points are unavailable");
        return false;
    }
    if (g_scale.framebuffer != 0 && g_api.glDeleteFramebuffers) {
        g_api.glDeleteFramebuffers(1, &g_scale.framebuffer);
        g_scale.framebuffer = 0;
    }
    if (g_scale.texture != 0 && g_api.glDeleteTextures) {
        g_api.glDeleteTextures(1, &g_scale.texture);
        g_scale.texture = 0;
    }
    g_api.glGenTextures(1, &g_scale.texture);
    if (g_scale.texture == 0) {
        FailScale("could not allocate the upscale texture");
        return false;
    }
    g_api.glBindTexture(GL_TEXTURE_2D, g_scale.texture);
    g_api.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_scale.scaledWidth,
                       g_scale.scaledHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    g_api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g_api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g_api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    g_api.glGenFramebuffers(1, &g_scale.framebuffer);
    if (g_scale.framebuffer == 0) {
        FailScale("could not allocate the upscale framebuffer");
        return false;
    }
    g_api.glBindFramebuffer(GL_FRAMEBUFFER, g_scale.framebuffer);
    g_api.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_scale.texture, 0);
    const GLenum status = g_api.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    g_api.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        FailScale("the upscale framebuffer is incomplete");
        return false;
    }
    g_scale.textureWidth = g_scale.scaledWidth;
    g_scale.textureHeight = g_scale.scaledHeight;
    if (g_api.glGetError) {
        while (g_api.glGetError() != GL_NO_ERROR) {
        }
    }
    return true;
}

/// Copies the rendered (scaled) region into our texture and stretches it back
/// over the whole window.  Runs right before the real swap, so the frame the
/// user sees is always full size.
void UpscaleFrame(EGLDisplay display, EGLSurface surface) {
    if (g_scale.failed || !g_scale.scaledThisFrame || g_scale.scaledWidth <= 0) {
        g_scale.scaledThisFrame = false;
        return;
    }
    g_scale.scaledThisFrame = false;
    if (!g_api.glBlitFramebuffer) {
        FailScale("GLES3 glBlitFramebuffer is unavailable on this device");
        return;
    }
    if (!g_api.glGetIntegerv) {
        FailScale("glGetIntegerv is unavailable, cannot save GL state");
        return;
    }

    // Remember everything we are about to disturb.
    GLint previousDraw = 0;
    GLint previousRead = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint scissor[4] = {0, 0, 0, 0};
    // GL_COLOR_WRITEMASK is a four element array of GLint callers provide.
    GLint colorMask[4] = {1, 1, 1, 1};
    GLint depthMask = 1;
    GLint program = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint texture2d = 0;
    g_api.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    g_api.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    g_api.glGetIntegerv(GL_VIEWPORT, viewport);
    g_api.glGetIntegerv(GL_SCISSOR_BOX, scissor);
    g_api.glGetIntegerv(GL_COLOR_WRITEMASK, colorMask);
    g_api.glGetIntegerv(GL_DEPTH_WRITEMASK, &depthMask);
    g_api.glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    g_api.glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    g_api.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2d);
    const GLboolean scissorEnabled =
        g_api.glIsEnabled ? g_api.glIsEnabled(GL_SCISSOR_TEST) : GL_FALSE;

    if (g_api.glGetError) {
        while (g_api.glGetError() != GL_NO_ERROR) {
        }
    }

    if (!EnsureScaleTargets()) return;

    g_api.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    g_api.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_scale.framebuffer);
    g_api.glBlitFramebuffer(0, 0, g_scale.scaledWidth, g_scale.scaledHeight, 0, 0,
                            g_scale.scaledWidth, g_scale.scaledHeight,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);

    g_api.glBindFramebuffer(GL_READ_FRAMEBUFFER, g_scale.framebuffer);
    g_api.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    g_api.glBlitFramebuffer(0, 0, g_scale.scaledWidth, g_scale.scaledHeight, 0, 0,
                            g_scale.surfaceWidth, g_scale.surfaceHeight,
                            GL_COLOR_BUFFER_BIT, GL_LINEAR);

    GLenum error = GL_NO_ERROR;
    if (g_api.glGetError) error = g_api.glGetError();

    // Put the state back the way the engine left it.
    g_api.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    g_api.glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
    g_api.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    g_api.glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    if (g_api.glColorMask) {
        g_api.glColorMask(colorMask[0] != 0 ? GL_TRUE : GL_FALSE,
                          colorMask[1] != 0 ? GL_TRUE : GL_FALSE,
                          colorMask[2] != 0 ? GL_TRUE : GL_FALSE,
                          colorMask[3] != 0 ? GL_TRUE : GL_FALSE);
    }
    if (g_api.glDepthMask) {
        g_api.glDepthMask(depthMask != 0 ? GL_TRUE : GL_FALSE);
    }
    if (g_api.glUseProgram) g_api.glUseProgram(static_cast<GLuint>(program));
    if (g_api.glActiveTexture) {
        g_api.glActiveTexture(static_cast<GLenum>(activeTexture));
    }
    if (g_api.glBindTexture) {
        g_api.glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2d));
    }
    if (g_api.glEnable && g_api.glDisable) {
        if (scissorEnabled) {
            g_api.glEnable(GL_SCISSOR_TEST);
        } else {
            g_api.glDisable(GL_SCISSOR_TEST);
        }
    }
    // Everything above bypassed the detours, so the dedup cache no longer
    // describes the real GL state.  The framebuffer bindings are restored to
    // the values we saved, so those stay accurate.
    InvalidateDedupCache();
    t_boundDrawFramebuffer = static_cast<int>(previousDraw);
    t_boundReadFramebuffer = static_cast<int>(previousRead);

    if (error != GL_NO_ERROR) {
        if (error == GL_INVALID_OPERATION) {
            FailScale("the default framebuffer is multisampled - turn on "
                      "'Disable MSAA' and re-enable Render Scale");
        } else {
            FailScale("the GPU rejected the upscale blit");
        }
    }
    (void)display;
    (void)surface;
}

void UpdateSurfaceSize(EGLDisplay display, EGLSurface surface) {
    if (!g_api.eglQuerySurface || surface == nullptr) return;
    const bool sameSurface = surface == g_scale.surface && g_scale.surfaceWidth > 0;
    // Re-query every half second or so: display rotation and resolution
    // changes both invalidate the cached size.
    if (sameSurface && g_scale.frame % 30 != 0) return;
    EGLint width = 0;
    EGLint height = 0;
    if (!g_api.eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
        !g_api.eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
        return;
    }
    if (width <= 0 || height <= 0) return;
    if (sameSurface && width == g_scale.surfaceWidth &&
        height == g_scale.surfaceHeight) {
        return;
    }
    g_scale.surface = surface;
    g_scale.surfaceWidth = width;
    g_scale.surfaceHeight = height;
    g_stats.surfaceWidth.store(width, std::memory_order_relaxed);
    g_stats.surfaceHeight.store(height, std::memory_order_relaxed);
    const int percent = ClampInt(
        g_options.renderScalePercent.load(std::memory_order_relaxed), 50, 100);
    g_scale.scaledWidth = ScaledDimension(width, percent);
    g_scale.scaledHeight = ScaledDimension(height, percent);
}

void PrepareFrame(EGLDisplay display, EGLSurface surface) {
    ++g_scale.frame;
    const std::int64_t now = MonotonicNanos();
    UpdateFrameStats(now);
    ApplyFrameCap(now);
    if (ScaleActive()) {
        UpdateSurfaceSize(display, surface);
        UpscaleFrame(display, surface);
    }
    g_stats.drawCallsLastFrame.store(g_stats.drawCalls.load(std::memory_order_relaxed),
                                     std::memory_order_relaxed);
    g_stats.drawCalls.store(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// 8) Redundant GL state call elimination
// ---------------------------------------------------------------------------
struct StateCache {
    bool activeTextureKnown = false;
    GLenum activeTexture = GL_TEXTURE0;
    struct TextureBinding {
        GLenum target = 0;
        GLuint name = 0xFFFFFFFFu;
    };
    TextureBinding textures[8];
    struct CapState {
        GLenum cap = 0;
        bool known = false;
        bool enabled = false;
    };
    CapState caps[12];
    struct {
        bool known = false;
        GLuint name = 0;
    } program;
    struct {
        bool known = false;
        GLuint name = 0;
    } buffers[2]; // 0 = GL_ARRAY_BUFFER, 1 = GL_ELEMENT_ARRAY_BUFFER
    struct {
        bool known = false;
        GLboolean value = GL_TRUE;
    } depthMask;
    struct {
        bool known = false;
        GLenum source = 0;
        GLenum destination = 0;
    } blend;
    struct {
        bool known = false;
        GLenum mode = 0;
    } cullFace, frontFace;
};

thread_local StateCache t_state;
thread_local int t_boundDrawFramebuffer = 0;
thread_local int t_boundReadFramebuffer = 0;

void ResetBoundFramebuffers() {
    t_boundDrawFramebuffer = 0;
    t_boundReadFramebuffer = 0;
}

bool DefaultFramebufferBound() { return t_boundDrawFramebuffer == 0; }

bool DedupEnabled() { return Active(g_options.stateDedup); }

/// Returns true when the driver already has this capability in the requested
/// state (so the call can be dropped).  The cache is updated either way.
bool CapKnownAndEqual(GLenum cap, bool enabled) {
    for (StateCache::CapState &state : t_state.caps) {
        if (state.cap != cap) continue;
        const bool same = state.known && state.enabled == enabled;
        state.known = true;
        state.enabled = enabled;
        return same;
    }
    for (StateCache::CapState &state : t_state.caps) {
        if (state.cap == 0) {
            state.cap = cap;
            state.known = true;
            state.enabled = enabled;
            return false;
        }
    }
    return false;
}

StateCache::TextureBinding *TextureSlot(GLenum target) {
    for (StateCache::TextureBinding &slot : t_state.textures) {
        if (slot.target == target) return &slot;
    }
    for (StateCache::TextureBinding &slot : t_state.textures) {
        if (slot.target == 0) {
            slot.target = target;
            return &slot;
        }
    }
    return nullptr;
}

void ForgetTextureName(GLuint name) {
    for (StateCache::TextureBinding &slot : t_state.textures) {
        if (slot.name == name) slot.name = 0xFFFFFFFFu;
    }
}

void InvalidateDedupCache() {
    t_state = StateCache{};
    for (StateCache::TextureBinding &slot : t_state.textures) {
        slot.name = 0xFFFFFFFFu; // "unknown", never equal to a real name
    }
}

} // namespace

void InvalidateStateCache() {
    InvalidateDedupCache();
    // GL state is per context: after a context switch nothing is known.
    ResetBoundFramebuffers();
}

bool TakeRenderScaleFailure(std::string &reason) {
    if (!g_scale.failurePending) return false;
    g_scale.failurePending = false;
    reason.assign(g_scale.failure);
    return true;
}

void BeginFrame() { g_scale.scaledThisFrame = false; }

std::string DescribeState() {
    std::ostringstream out;
    out << "enabled=" << (g_options.enabled.load(std::memory_order_relaxed) ? 1 : 0)
        << " optimizations=" << EnabledOptimizationCount() << "/10"
        << " hooks=" << gl::HookedFunctionCount() << " fps="
        << static_cast<int>(g_stats.fps.load(std::memory_order_relaxed))
        << " ms=" << g_stats.frameTimeMs.load(std::memory_order_relaxed);
    return out.str();
}

// ---------------------------------------------------------------------------
// EGL detours
// ---------------------------------------------------------------------------
EGLBoolean SwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!g_api.eglSwapBuffers) return EGL_FALSE;
    PrepareFrame(display, surface);
    const EGLBoolean result = g_api.eglSwapBuffers(display, surface);
    BeginFrame();
    return result;
}

EGLBoolean SwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                    const EGLint *rects, EGLint nRects) {
    if (!g_api.eglSwapBuffersWithDamageKHR) {
        return SwapBuffers(display, surface);
    }
    PrepareFrame(display, surface);
    const EGLBoolean result =
        g_api.eglSwapBuffersWithDamageKHR(display, surface, rects, nRects);
    BeginFrame();
    return result;
}

EGLBoolean SwapInterval(EGLDisplay display, EGLint interval) {
    if (!g_api.eglSwapInterval) return EGL_FALSE;
    // 1) Uncap FPS: presenting with an interval of 0 removes the vsync wait,
    //    which is the difference between 30/60 and several hundred FPS on
    //    devices whose compositor throttles to the display refresh rate.
    if (Active(g_options.uncapFps)) return g_api.eglSwapInterval(display, 0);
    return g_api.eglSwapInterval(display, interval);
}

EGLBoolean ChooseConfig(EGLDisplay display, const EGLint *attribs, EGLConfig *configs,
                        EGLint configSize, EGLint *numConfig) {
    if (!g_api.eglChooseConfig) return EGL_FALSE;
    // 7) Disable MSAA: rewrite EGL_SAMPLE_BUFFERS/EGL_SAMPLES to zero so the
    //    engine picks a single-sampled configuration.
    if (!Active(g_options.msaaOff) || attribs == nullptr) {
        return g_api.eglChooseConfig(display, attribs, configs, configSize, numConfig);
    }
    EGLint rewritten[64];
    int count = 0;
    bool touched = false;
    for (int i = 0; i < 64 && attribs[i] != EGL_NONE; i += 2) {
        if (i + 2 >= 64) break;
        const EGLint key = attribs[i];
        const EGLint value = attribs[i + 1];
        if (key == EGL_SAMPLE_BUFFERS || key == EGL_SAMPLES) {
            rewritten[count++] = key;
            rewritten[count++] = 0;
            touched = true;
        } else {
            rewritten[count++] = key;
            rewritten[count++] = value;
        }
    }
    if (!touched) {
        return g_api.eglChooseConfig(display, attribs, configs, configSize, numConfig);
    }
    rewritten[count++] = EGL_NONE;
    g_stats.msaaConfigs.fetch_add(1, std::memory_order_relaxed);
    return g_api.eglChooseConfig(display, rewritten, configs, configSize, numConfig);
}

EGLBoolean GetConfigAttrib(EGLDisplay display, EGLConfig config, EGLint attribute,
                           EGLint *value) {
    if (!g_api.eglGetConfigAttrib) return EGL_FALSE;
    const EGLBoolean result =
        g_api.eglGetConfigAttrib(display, config, attribute, value);
    // Keep the answer consistent with the rewritten configuration list.
    if (result && value != nullptr && Active(g_options.msaaOff) &&
        (attribute == EGL_SAMPLES || attribute == EGL_SAMPLE_BUFFERS)) {
        *value = 0;
    }
    return result;
}

EGLBoolean MakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read,
                       EGLContext context) {
    if (!g_api.eglMakeCurrent) return EGL_FALSE;
    // A context switch invalidates every cached GL state.
    InvalidateStateCache();
    const EGLBoolean result = g_api.eglMakeCurrent(display, draw, read, context);
    (void)display;
    return result;
}

__eglMustCastToProperFunctionPointerType GetProcAddress(const char *name) {
    if (!g_api.eglGetProcAddress) return nullptr;
    const __eglMustCastToProperFunctionPointerType real =
        g_api.eglGetProcAddress(name);
    if (!name) return real;
    // Applications that fetch their GL entry points through eglGetProcAddress
    // (the usual case on Android) must receive our detours, otherwise the
    // optimizations would never run on those builds.
    struct Entry {
        const char *name;
        void *detour;
        void **original;
    };
    const Entry entries[] = {
        {"eglSwapBuffers", reinterpret_cast<void *>(&SwapBuffers),
         reinterpret_cast<void **>(&g_api.eglSwapBuffers)},
        {"eglSwapBuffersWithDamageKHR",
         reinterpret_cast<void *>(&SwapBuffersWithDamageKHR),
         reinterpret_cast<void **>(&g_api.eglSwapBuffersWithDamageKHR)},
        {"eglSwapInterval", reinterpret_cast<void *>(&SwapInterval),
         reinterpret_cast<void **>(&g_api.eglSwapInterval)},
        {"glViewport", reinterpret_cast<void *>(&Viewport),
         reinterpret_cast<void **>(&g_api.glViewport)},
        {"glScissor", reinterpret_cast<void *>(&Scissor),
         reinterpret_cast<void **>(&g_api.glScissor)},
        {"glClear", reinterpret_cast<void *>(&Clear),
         reinterpret_cast<void **>(&g_api.glClear)},
        {"glFinish", reinterpret_cast<void *>(&Finish),
         reinterpret_cast<void **>(&g_api.glFinish)},
        {"glTexParameterf", reinterpret_cast<void *>(&TexParameterf),
         reinterpret_cast<void **>(&g_api.glTexParameterf)},
        {"glTexParameteri", reinterpret_cast<void *>(&TexParameteri),
         reinterpret_cast<void **>(&g_api.glTexParameteri)},
        {"glBlitFramebuffer", reinterpret_cast<void *>(&g_api.glBlitFramebuffer),
         reinterpret_cast<void **>(&g_api.glBlitFramebuffer)},
        {"glBindFramebuffer", reinterpret_cast<void *>(&BindFramebuffer),
         reinterpret_cast<void **>(&g_api.glBindFramebuffer)},
    };
    for (const Entry &entry : entries) {
        if (std::strcmp(name, entry.name) != 0) continue;
        if (*entry.original == nullptr && real != nullptr) {
            *entry.original = reinterpret_cast<void *>(real);
        }
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
            entry.detour);
    }
    return real;
}

// ---------------------------------------------------------------------------
// GLES detours
// ---------------------------------------------------------------------------
void Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!g_api.glViewport) return;
    if (ScaleActive() && !g_scale.failed && DefaultFramebufferBound() &&
        width == g_scale.surfaceWidth && height == g_scale.surfaceHeight &&
        g_scale.scaledWidth > 0) {
        g_scale.scaledThisFrame = true;
        const GLsizei scaledWidth = g_scale.scaledWidth;
        const GLsizei scaledHeight = g_scale.scaledHeight;
        g_api.glViewport(x, y, scaledWidth, scaledHeight);
        return;
    }
    g_api.glViewport(x, y, width, height);
}

void Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!g_api.glScissor) return;
    if (ScaleActive() && !g_scale.failed && DefaultFramebufferBound() &&
        width == g_scale.surfaceWidth && height == g_scale.surfaceHeight &&
        g_scale.scaledWidth > 0) {
        g_api.glScissor(x, y, g_scale.scaledWidth, g_scale.scaledHeight);
        return;
    }
    g_api.glScissor(x, y, width, height);
}

void BindFramebuffer(GLenum target, GLuint framebuffer) {
    if (g_api.glBindFramebuffer) g_api.glBindFramebuffer(target, framebuffer);
    switch (target) {
    case GL_FRAMEBUFFER:
        t_boundDrawFramebuffer = static_cast<int>(framebuffer);
        t_boundReadFramebuffer = static_cast<int>(framebuffer);
        break;
    case GL_DRAW_FRAMEBUFFER:
        t_boundDrawFramebuffer = static_cast<int>(framebuffer);
        break;
    case GL_READ_FRAMEBUFFER:
        t_boundReadFramebuffer = static_cast<int>(framebuffer);
        break;
    default:
        return;
    }
}

void Enable(GLenum cap) {
    if (!g_api.glEnable) return;
    if (DedupEnabled() && CapKnownAndEqual(cap, true)) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glEnable(cap);
}

void Disable(GLenum cap) {
    if (!g_api.glDisable) return;
    if (DedupEnabled() && CapKnownAndEqual(cap, false)) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glDisable(cap);
}

void ActiveTexture(GLenum texture) {
    if (!g_api.glActiveTexture) return;
    if (DedupEnabled() && t_state.activeTextureKnown &&
        t_state.activeTexture == texture) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glActiveTexture(texture);
    t_state.activeTextureKnown = true;
    t_state.activeTexture = texture;
}

void BindTexture(GLenum target, GLuint texture) {
    if (!g_api.glBindTexture) return;
    StateCache::TextureBinding *slot = DedupEnabled() ? TextureSlot(target) : nullptr;
    if (slot != nullptr && slot->name == texture) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glBindTexture(target, texture);
    if (slot != nullptr) slot->name = texture;
}

void BindBuffer(GLenum target, GLuint buffer) {
    if (!g_api.glBindBuffer) return;
    const int index = target == GL_ARRAY_BUFFER ? 0
                      : (target == GL_ELEMENT_ARRAY_BUFFER ? 1 : -1);
    if (DedupEnabled() && index >= 0 && t_state.buffers[index].known &&
        t_state.buffers[index].name == buffer) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glBindBuffer(target, buffer);
    if (index >= 0) {
        t_state.buffers[index].known = true;
        t_state.buffers[index].name = buffer;
    }
}

void UseProgram(GLuint program) {
    if (!g_api.glUseProgram) return;
    if (DedupEnabled() && t_state.program.known && t_state.program.name == program) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glUseProgram(program);
    t_state.program.known = true;
    t_state.program.name = program;
}

void DepthMask(GLboolean flag) {
    if (!g_api.glDepthMask) return;
    if (DedupEnabled() && t_state.depthMask.known &&
        t_state.depthMask.value == flag) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glDepthMask(flag);
    t_state.depthMask.known = true;
    t_state.depthMask.value = flag;
}

void BlendFunc(GLenum source, GLenum destination) {
    if (!g_api.glBlendFunc) return;
    if (DedupEnabled() && t_state.blend.known &&
        t_state.blend.source == source && t_state.blend.destination == destination) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glBlendFunc(source, destination);
    t_state.blend.known = true;
    t_state.blend.source = source;
    t_state.blend.destination = destination;
}

void CullFace(GLenum mode) {
    if (!g_api.glCullFace) return;
    if (DedupEnabled() && t_state.cullFace.known && t_state.cullFace.mode == mode) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glCullFace(mode);
    t_state.cullFace.known = true;
    t_state.cullFace.mode = mode;
}

void FrontFace(GLenum mode) {
    if (!g_api.glFrontFace) return;
    if (DedupEnabled() && t_state.frontFace.known && t_state.frontFace.mode == mode) {
        g_stats.stateCallsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_api.glFrontFace(mode);
    t_state.frontFace.known = true;
    t_state.frontFace.mode = mode;
}

void DeleteTextures(GLsizei count, const GLuint *textures) {
    if (!g_api.glDeleteTextures) return;
    g_api.glDeleteTextures(count, textures);
    if (DedupEnabled() && textures != nullptr) {
        for (GLsizei i = 0; i < count; ++i) ForgetTextureName(textures[i]);
    }
}

void TexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (!g_api.glTexParameterf) return;
    if (pname == kAnisotropyExt && Active(g_options.anisoClamp)) {
        g_stats.anisoClamped.fetch_add(1, std::memory_order_relaxed);
        g_api.glTexParameterf(target, pname, 1.0f);
        return;
    }
    if (pname == kLodBiasExt && Active(g_options.lodBiasEnabled)) {
        g_stats.lodBiased.fetch_add(1, std::memory_order_relaxed);
        g_api.glTexParameterf(
            target, pname,
            param + g_options.lodBias.load(std::memory_order_relaxed));
        return;
    }
    g_api.glTexParameterf(target, pname, param);
}

void TexParameteri(GLenum target, GLenum pname, GLint param) {
    if (!g_api.glTexParameteri) return;
    if (pname == kAnisotropyExt && Active(g_options.anisoClamp)) {
        g_stats.anisoClamped.fetch_add(1, std::memory_order_relaxed);
        g_api.glTexParameteri(target, pname, 1);
        return;
    }
    if (pname == GL_TEXTURE_MIN_FILTER && Active(g_options.mipDowngrade)) {
        if (param == GL_LINEAR_MIPMAP_LINEAR) {
            g_stats.mipDowngraded.fetch_add(1, std::memory_order_relaxed);
            g_api.glTexParameteri(target, pname, GL_LINEAR_MIPMAP_NEAREST);
            return;
        }
        if (param == GL_NEAREST_MIPMAP_LINEAR) {
            g_stats.mipDowngraded.fetch_add(1, std::memory_order_relaxed);
            g_api.glTexParameteri(target, pname, GL_NEAREST_MIPMAP_NEAREST);
            return;
        }
    }
    g_api.glTexParameteri(target, pname, param);
}

void TexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (!g_api.glTexParameterfv) return;
    if (params == nullptr) return;
    if (pname == kAnisotropyExt && Active(g_options.anisoClamp)) {
        const GLfloat clamped = 1.0f;
        g_stats.anisoClamped.fetch_add(1, std::memory_order_relaxed);
        g_api.glTexParameterfv(target, pname, &clamped);
        return;
    }
    if (pname == kLodBiasExt && Active(g_options.lodBiasEnabled)) {
        const GLfloat biased =
            params[0] + g_options.lodBias.load(std::memory_order_relaxed);
        g_stats.lodBiased.fetch_add(1, std::memory_order_relaxed);
        g_api.glTexParameterfv(target, pname, &biased);
        return;
    }
    g_api.glTexParameterfv(target, pname, params);
}

void TexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    if (!g_api.glTexParameteriv) return;
    if (params == nullptr) return;
    if (pname == kAnisotropyExt && Active(g_options.anisoClamp)) {
        const GLint clamped = 1;
        g_stats.anisoClamped.fetch_add(1, std::memory_order_relaxed);
        g_api.glTexParameteriv(target, pname, &clamped);
        return;
    }
    if (pname == GL_TEXTURE_MIN_FILTER && Active(g_options.mipDowngrade) &&
        (params[0] == GL_LINEAR_MIPMAP_LINEAR ||
         params[0] == GL_NEAREST_MIPMAP_LINEAR)) {
        const GLint downgraded = params[0] == GL_LINEAR_MIPMAP_LINEAR
                                     ? GL_LINEAR_MIPMAP_NEAREST
                                     : GL_NEAREST_MIPMAP_NEAREST;
        g_stats.mipDowngraded.fetch_add(1, std::memory_order_relaxed);
        g_api.glTexParameteriv(target, pname, &downgraded);
        return;
    }
    g_api.glTexParameteriv(target, pname, params);
}

void Finish() {
    // 9) glFinish stalls until the GPU is idle; glFlush submits the queued
    //    commands, which is all a frame boundary needs.
    if (Active(g_options.noGlFinish) && g_api.glFlush) {
        g_stats.finishesSkipped.fetch_add(1, std::memory_order_relaxed);
        g_api.glFlush();
        return;
    }
    if (g_api.glFinish) g_api.glFinish();
}

void Flush() {
    if (g_api.glFlush) g_api.glFlush();
}

void Clear(GLbitfield mask) {
    if (!g_api.glClear) return;
    // 10) Skip the colour half of a clear on the window: the first full-screen
    //     draw of the frame covers those pixels anyway.
    if (Active(g_options.skipClears) && (mask & GL_COLOR_BUFFER_BIT) != 0 &&
        DefaultFramebufferBound()) {
        g_stats.clearsSkipped.fetch_add(1, std::memory_order_relaxed);
        mask &= ~static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT);
        if (mask == 0) return;
    }
    g_api.glClear(mask);
}

void ColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    if (g_api.glColorMask) g_api.glColorMask(red, green, blue, alpha);
    // The upscaler saves and restores this, so nothing to cache here.
}

void DrawArrays(GLenum mode, GLint first, GLsizei count) {
    g_stats.drawCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_api.glDrawArrays) g_api.glDrawArrays(mode, first, count);
}

void DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    g_stats.drawCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_api.glDrawElements) g_api.glDrawElements(mode, count, type, indices);
}

} // namespace leviboost::opt
