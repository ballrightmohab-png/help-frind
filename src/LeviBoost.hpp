#pragma once
//
// LeviBoost - shared declarations.
//
// LeviBoost is a preload-native mod for Minecraft Bedrock on Android running
// under LeviLaunchroid/LeviLauncher.  Every optimization is implemented as a
// detour on an EGL/GLES entry point, so the mod never has to guess a game
// offset and keeps working across Bedrock builds.
//
// All ten optimizations can be switched on and off from the LeviLaunchroid Mod
// Menu; each toggle flips an atomic flag that the render-thread detours read,
// which makes changes take effect on the very next frame.
//

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <android/log.h>

namespace leviboost {

inline constexpr const char *kModId = "leviboost";
inline constexpr const char *kVersion = "1.0.0";
inline constexpr const char *kLogTag = "LeviBoost";

// ---------------------------------------------------------------------------
// Mod Menu identifiers
// ---------------------------------------------------------------------------
namespace ids {

inline constexpr const char *kUncapFps = "lb_uncap_fps";
inline constexpr const char *kFpsCap = "lb_fps_cap";
inline constexpr const char *kRenderScale = "lb_render_scale";
inline constexpr const char *kAnisoClamp = "lb_aniso_clamp";
inline constexpr const char *kMipDowngrade = "lb_mip_downgrade";
inline constexpr const char *kLodBias = "lb_lod_bias";
inline constexpr const char *kMsaaOff = "lb_msaa_off";
inline constexpr const char *kStateDedup = "lb_state_dedup";
inline constexpr const char *kNoGlFinish = "lb_no_glfinish";
inline constexpr const char *kSkipClears = "lb_skip_clears";
inline constexpr const char *kHud = "lb_hud";
inline constexpr const char *kControl = "lb_control";

/// Module ids that map one-to-one onto the ten optimizations.
inline constexpr const char *kOptimizationIds[] = {
    kUncapFps,      kFpsCap,       kRenderScale, kAnisoClamp, kMipDowngrade,
    kLodBias,       kMsaaOff,      kStateDedup,  kNoGlFinish, kSkipClears,
};
inline constexpr int kOptimizationCount = 10;

} // namespace ids

namespace keys {

inline constexpr const char *kFpsCap = "target_fps";
inline constexpr const char *kRenderScale = "scale_percent";
inline constexpr const char *kLodBias = "lod_bias";
inline constexpr const char *kPanicKey = "panic_key";
inline constexpr const char *kHudCorner = "hud_corner";
inline constexpr const char *kHudDetail = "hud_detail";

} // namespace keys

// ---------------------------------------------------------------------------
// Live settings
//
// One atomic per user-visible option.  The render thread reads these on every
// frame, so they must stay lock-free and cheap.
// ---------------------------------------------------------------------------
struct Options {
    std::atomic<bool> enabled{true};             // master switch
    std::atomic<bool> uncapFps{true};            // 1. force eglSwapInterval(0)
    std::atomic<bool> fpsCapEnabled{false};      // 2. software frame cap
    std::atomic<int> fpsCap{60};                 //    target frames per second
    std::atomic<bool> renderScaleEnabled{false}; // 3. render at <100% + upscale
    std::atomic<int> renderScalePercent{85};     //    50..100
    std::atomic<bool> anisoClamp{true};          // 4. clamp anisotropy to 1x
    std::atomic<bool> mipDowngrade{true};        // 5. trilinear -> bilinear mips
    std::atomic<bool> lodBiasEnabled{false};     // 6. add a LOD bias
    std::atomic<float> lodBias{0.5f};            //    0.0..4.0
    std::atomic<bool> msaaOff{false};            // 7. strip MSAA from EGL configs
    std::atomic<bool> stateDedup{true};          // 8. skip redundant GL calls
    std::atomic<bool> noGlFinish{true};          // 9. glFinish -> glFlush
    std::atomic<bool> skipClears{false};         // 10. drop redundant glClear
    std::atomic<bool> hud{true};                 // HUD overlay module
    std::atomic<int> hudCorner{0};               // 0 TL, 1 TR, 2 BL, 3 BR
    std::atomic<int> hudDetail{1};               // 0 compact, 1 full
    std::atomic<int> panicKey{0};                // Android key code, 0 = unset
};

extern Options g_options;

/// Returns true when the option is on and the master switch allows it.
inline bool Active(const std::atomic<bool> &option) {
    return g_options.enabled.load(std::memory_order_relaxed) &&
           option.load(std::memory_order_relaxed);
}

inline int ClampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

inline float ClampFloat(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

// ---------------------------------------------------------------------------
// Runtime statistics (shown by the HUD, useful in bug reports)
// ---------------------------------------------------------------------------
struct Stats {
    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::uint64_t> swaps{0};
    std::atomic<std::uint64_t> anisoClamped{0};
    std::atomic<std::uint64_t> mipDowngraded{0};
    std::atomic<std::uint64_t> lodBiased{0};
    std::atomic<std::uint64_t> msaaConfigs{0};
    std::atomic<std::uint64_t> stateCallsSkipped{0};
    std::atomic<std::uint64_t> finishesSkipped{0};
    std::atomic<std::uint64_t> clearsSkipped{0};
    std::atomic<std::uint64_t> framesCapped{0};
    std::atomic<std::uint64_t> drawCalls{0};
    std::atomic<std::uint64_t> drawCallsLastFrame{0};
    std::atomic<float> fps{0.0f};
    std::atomic<float> frameTimeMs{0.0f};
    std::atomic<int> surfaceWidth{0};
    std::atomic<int> surfaceHeight{0};
};

extern Stats g_stats;

/// Re-initialises the option flags / counters (used by the host tests and the
/// panic path).  The structs contain atomics and cannot be assigned.
void ResetOptions();
void ResetStats();

/// How many of the ten optimizations are currently switched on.
int EnabledOptimizationCount();

/// True when the GL hooks are installed and the optimizations can take effect.
bool OptimizationsReady();

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
/// Logger function installed by the mod entry point; may be null.
extern void (*g_loggerFn)(int level, const char *message);

/// 0 = info, 1 = warn, 2 = error.
void LogMessage(int level, const std::string &message);

void LogInfo(const std::string &message);
void LogWarn(const std::string &message);
void LogError(const std::string &message);

// ---------------------------------------------------------------------------
// Time helpers
//
// The function pointers exist so the host tests can run on a simulated clock;
// both are null in the mod itself.
// ---------------------------------------------------------------------------
extern std::int64_t (*g_clockFn)();
extern void (*g_sleepFn)(std::int64_t nanos);

std::int64_t MonotonicNanos();
void SleepNanos(std::int64_t nanos);

} // namespace leviboost
