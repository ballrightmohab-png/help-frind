//
// LeviBoost - mod entry point.
//
// Ten performance optimizations for Minecraft Bedrock on Android, each one
// switchable from the LeviLaunchroid Mod Menu.  Nothing here patches game
// code: every optimization is a detour on an EGL/GLES entry point, which keeps
// the mod working across Bedrock builds.
//
//  1. Uncap FPS                  force eglSwapInterval(0)
//  2. Frame Rate Cap             software pacing before the swap
//  3. Render Scale               render below 100% and stretch in the swap
//  4. Anisotropic Filtering 1x   clamp GL_TEXTURE_MAX_ANISOTROPY_EXT
//  5. Fast Mipmap Filtering      trilinear -> bilinear between mips
//  6. Texture LOD Bias           bias texture sampling towards smaller mips
//  7. Disable MSAA               strip multisampling from eglChooseConfig
//  8. State Call Dedup           drop redundant GL state calls
//  9. Remove glFinish() Stalls   glFinish -> glFlush
// 10. Skip Redundant Clears      drop the colour half of the frame clear
//

#include <pl/Logger.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/ModMenuConfig.hpp>

#include <atomic>
#include <cstdlib>
#include <string_view>
#include <string>
#include <vector>

#include <pthread.h>
#include <unistd.h>

#include "GlApi.hpp"
#include "Hud.hpp"
#include "LeviBoost.hpp"
#include "Optimizations.hpp"
#include "Settings.hpp"

namespace {

pl::log::Logger *g_logger = nullptr;
std::atomic<bool> g_stopWorker{false};
pthread_t g_workerThread{};
bool g_workerRunning = false;
std::string g_configDir;
bool g_hooksWarned = false;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
void LogSink(int level, const char *message) {
    if (!message) return;
    if (!g_logger) {
        __android_log_print(level == 0 ? ANDROID_LOG_INFO
                                       : (level == 1 ? ANDROID_LOG_WARN
                                                     : ANDROID_LOG_ERROR),
                            leviboost::kLogTag, "%s", message);
        return;
    }
    switch (level) {
    case 0: g_logger->info("{}", message); break;
    case 1: g_logger->warn("{}", message); break;
    default: g_logger->error("{}", message); break;
    }
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
void PersistSettings() {
    const leviboost::settings::Values values = leviboost::settings::Capture();
    if (!leviboost::settings::SaveToDirectory(g_configDir, values)) {
        leviboost::LogWarn("could not write the settings file");
    }
}

// ---------------------------------------------------------------------------
// Mod Menu callbacks
// ---------------------------------------------------------------------------
void OnToggle(std::string_view moduleId, bool enabled) {
    using namespace leviboost;
    const std::string id(moduleId);

    if (id == ids::kUncapFps) {
        g_options.uncapFps.store(enabled);
    } else if (id == ids::kFpsCap) {
        g_options.fpsCapEnabled.store(enabled);
    } else if (id == ids::kRenderScale) {
        g_options.renderScaleEnabled.store(enabled);
    } else if (id == ids::kAnisoClamp) {
        g_options.anisoClamp.store(enabled);
    } else if (id == ids::kMipDowngrade) {
        g_options.mipDowngrade.store(enabled);
    } else if (id == ids::kLodBias) {
        g_options.lodBiasEnabled.store(enabled);
    } else if (id == ids::kMsaaOff) {
        g_options.msaaOff.store(enabled);
    } else if (id == ids::kStateDedup) {
        g_options.stateDedup.store(enabled);
        gl::RefreshHookGroups();
    } else if (id == ids::kNoGlFinish) {
        g_options.noGlFinish.store(enabled);
    } else if (id == ids::kSkipClears) {
        g_options.skipClears.store(enabled);
    } else if (id == ids::kHud) {
        g_options.hud.store(enabled);
        if (!enabled) hud::Clear();
        gl::RefreshHookGroups();
    } else if (id == ids::kControl) {
        g_options.enabled.store(enabled);
        if (!enabled) {
            hud::Clear();
        }
    } else {
        return;
    }

    LogInfo(id + (enabled ? " enabled" : " disabled") + " - " +
            opt::DescribeState());
    PersistSettings();
}

void OnConfigChanged(std::string_view moduleId, std::string_view key,
                     std::string_view value) {
    using namespace leviboost;
    const std::string id(moduleId);
    const std::string setting(key);
    const std::string text(value);

    auto asInt = [&](int fallback) {
        return text.empty() ? fallback : std::atoi(text.c_str());
    };
    auto asFloat = [&](float fallback) {
        return text.empty() ? fallback
                            : static_cast<float>(std::atof(text.c_str()));
    };

    if (id == ids::kFpsCap && setting == keys::kFpsCap) {
        g_options.fpsCap.store(ClampInt(asInt(g_options.fpsCap.load()), 10, 480));
    } else if (id == ids::kRenderScale && setting == keys::kRenderScale) {
        g_options.renderScalePercent.store(
            ClampInt(asInt(g_options.renderScalePercent.load()), 50, 100));
    } else if (id == ids::kLodBias && setting == keys::kLodBias) {
        g_options.lodBias.store(
            ClampFloat(asFloat(g_options.lodBias.load()), 0.0f, 4.0f));
    } else if (id == ids::kHud && setting == keys::kHudCorner) {
        g_options.hudCorner.store(ClampInt(asInt(0), 0, 3));
    } else if (id == ids::kHud && setting == keys::kHudDetail) {
        g_options.hudDetail.store(ClampInt(asInt(1), 0, 1));
    } else if (id == ids::kControl && setting == keys::kPanicKey) {
        g_options.panicKey.store(ClampInt(asInt(0), 0, 400));
    } else {
        return;
    }

    PersistSettings();
}

void OnKeybind(std::string_view moduleId, std::string_view key, bool isDown) {
    using namespace leviboost;
    if (moduleId != ids::kControl || key != keys::kPanicKey || !isDown) return;
    if (g_options.panicKey.load() == 0) return;

    // Panic: every optimization off, the mod stays loaded so the user can
    // switch things back on from the Mod Menu.
    g_options.enabled.store(false);
    g_options.uncapFps.store(false);
    g_options.fpsCapEnabled.store(false);
    g_options.renderScaleEnabled.store(false);
    g_options.anisoClamp.store(false);
    g_options.mipDowngrade.store(false);
    g_options.lodBiasEnabled.store(false);
    g_options.msaaOff.store(false);
    g_options.stateDedup.store(false);
    g_options.noGlFinish.store(false);
    g_options.skipClears.store(false);
    gl::RefreshHookGroups();
    hud::Clear();
    LogWarn("panic key pressed - all optimizations disabled");
    PersistSettings();
}

void OnButtonEvent(std::string_view buttonId, pl::modmenu::ButtonEvent event,
                   float value) {
    (void)value;
    if (buttonId != "lb_open_menu") return;
    if (event == pl::modmenu::ButtonEvent::Click) {
        pl::modmenu::requestOpenMenu();
    }
}

// ---------------------------------------------------------------------------
// Worker thread: waits for the graphics library to show up, installs the
// hooks, keeps the HUD fed and reports runtime problems.
// ---------------------------------------------------------------------------
void *WorkerMain(void *) {
    using namespace leviboost;
    int ticks = 0;
    while (!g_stopWorker.load()) {
        const bool ready = gl::Install();
        if (!ready && !g_hooksWarned && ticks > 240) {
            g_hooksWarned = true;
            LogWarn("graphics libraries not found yet - " + gl::LibraryReport());
        }
        if (ready) {
            // Keeps the optional hook groups (state dedup, HUD counters) in
            // sync with the live configuration.
            gl::RefreshHookGroups();

            std::string failure;
            if (opt::TakeRenderScaleFailure(failure)) {
                LogWarn("Render Scale switched itself off: " + failure);
                g_options.renderScaleEnabled.store(false);
                pl::modmenu::setModuleEnabled(ids::kRenderScale, false);
                PersistSettings();
            }

            if ((ticks % 8) == 0 &&
                g_options.hud.load(std::memory_order_relaxed) &&
                g_options.enabled.load(std::memory_order_relaxed)) {
                hud::Publish();
            }
        }
        ++ticks;
        usleep(ready ? 250 * 1000 : 50 * 1000);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Mod Menu registration
// ---------------------------------------------------------------------------
using pl::modmenu::ConfigControlTypeV2;
using pl::modmenu::ConfigNodeV2;
using pl::modmenu::ConfigOptionV2;
using pl::modmenu::ConfigSchemaBuilder;
using pl::modmenu::ConfigType;
using pl::modmenu::ModuleBuilder;

const char *kCategoryOptimizations = "optimizations";
const char *kCategoryExtras = "extras";

pl::modmenu::ConfigOptionV2 Option(std::string value, std::string label) {
    pl::modmenu::ConfigOptionV2 option;
    option.value = std::move(value);
    option.label = std::move(label);
    return option;
}

pl::modmenu::ConfigNodeV2 MakeNode(std::string id, std::string key,
                                   std::string category, std::string title,
                                   std::string description,
                                   pl::modmenu::ConfigControlTypeV2 type,
                                   std::string defaultValue,
                                   std::string minValue = {},
                                   std::string maxValue = {}) {
    pl::modmenu::ConfigNodeV2 node;
    node.id = std::move(id);
    node.key = std::move(key);
    node.category = std::move(category);
    node.title = std::move(title);
    node.description = std::move(description);
    node.type = type;
    node.defaultValue = std::move(defaultValue);
    node.minValue = std::move(minValue);
    node.maxValue = std::move(maxValue);
    return node;
}

/// One slider / switch schema for a module that has a single numeric option.
void PublishSliderSchema(const char *moduleId, const char *category,
                         const char *title, ConfigNodeV2 node,
                         const char *moduleTitle) {
    ConfigSchemaBuilder builder;
    builder.defaultCategory(category);
    builder.category(category, title,
                     std::string(moduleTitle) + " settings.");
    builder.node(std::move(node));
    pl::modmenu::setConfigSchemaJson(moduleId, builder.toJson());
}

bool RegisterModules(const leviboost::settings::Values &values) {
    using namespace leviboost;
    bool ok = true;

    // --- 1) Uncap FPS --------------------------------------------------------
    ok &= ModuleBuilder(ids::kUncapFps, "Uncap FPS (disable VSync)")
              .description("Forces eglSwapInterval(0). Many Android builds "
                           "present on the display refresh rate, which caps "
                           "the frame rate at 60 or even 30 FPS regardless of "
                           "how fast the GPU is. This is usually the single "
                           "biggest win, at the cost of battery life and "
                           "screen tearing.")
              .modId(kModId)
              .defaultEnabled(values.uncapFps)
              .onToggle(OnToggle)
              .registerModule();

    // --- 2) Frame rate cap ---------------------------------------------------
    ok &= ModuleBuilder(ids::kFpsCap, "Frame Rate Cap (thermal guard)")
              .description("Delays each frame so the frame rate never goes "
                           "above the target. Sustained performance on a phone "
                           "is limited by heat: capping at 40-50 FPS often "
                           "raises the average after a few minutes because the "
                           "SoC never throttles.")
              .modId(kModId)
              .defaultEnabled(values.fpsCapEnabled)
              .onToggle(OnToggle)
              .config(keys::kFpsCap, "Target FPS", ConfigType::SliderInt,
                      std::to_string(values.fpsCap), "15", "240")
              .onConfigChanged(OnConfigChanged)
              .registerModule();

    // --- 3) Render scale -----------------------------------------------------
    ok &= ModuleBuilder(ids::kRenderScale, "Render Scale (experimental)")
              .description("Renders the world at a lower resolution and "
                           "stretches the result back to the screen in the "
                           "swap hook. Fill-rate bound GPUs gain a lot; text "
                           "and edges get softer. If the image stays wrong the "
                           "module switches itself off and the reason is "
                           "logged - a multisampled framebuffer requires "
                           "'Disable MSAA' first.")
              .modId(kModId)
              .defaultEnabled(values.renderScaleEnabled)
              .onToggle(OnToggle)
              .config(keys::kRenderScale, "Render scale %", ConfigType::SliderInt,
                      std::to_string(values.renderScalePercent), "50", "100")
              .onConfigChanged(OnConfigChanged)
              .registerModule();

    // --- 4) Anisotropic filtering -------------------------------------------
    ok &= ModuleBuilder(ids::kAnisoClamp, "Anisotropic Filtering 1x")
              .description("Clamps GL_TEXTURE_MAX_ANISOTROPY_EXT to 1. "
                           "Anisotropic filtering multiplies texture cache "
                           "traffic for grazing-angle terrain, which is the "
                           "most expensive filtering mode on mobile GPUs.")
              .modId(kModId)
              .defaultEnabled(values.anisoClamp)
              .onToggle(OnToggle)
              .registerModule();

    // --- 5) Mipmap filtering -------------------------------------------------
    ok &= ModuleBuilder(ids::kMipDowngrade, "Fast Mipmap Filtering")
              .description("Turns trilinear minification "
                           "(GL_LINEAR_MIPMAP_LINEAR) into bilinear-between-"
                           "mips. Trilinear reads two mip levels per texture "
                           "fetch, so this halves texture bandwidth at the "
                           "cost of visible transitions between mip levels.")
              .modId(kModId)
              .defaultEnabled(values.mipDowngrade)
              .onToggle(OnToggle)
              .registerModule();

    // --- 6) LOD bias ---------------------------------------------------------
    ok &= ModuleBuilder(ids::kLodBias, "Texture LOD Bias")
              .description("Adds a bias to GL_TEXTURE_LOD_BIAS so the sampler "
                           "picks smaller mip levels, trading sharpness for "
                           "texture bandwidth. 0.5 is barely visible, 2.0 is "
                           "a strong blur that can rescue very low end GPUs.")
              .modId(kModId)
              .defaultEnabled(values.lodBiasEnabled)
              .onToggle(OnToggle)
              .config(keys::kLodBias, "LOD bias", ConfigType::SliderFloat,
                      std::to_string(values.lodBias), "0.0", "4.0")
              .onConfigChanged(OnConfigChanged)
              .registerModule();

    // --- 7) MSAA -------------------------------------------------------------
    ok &= ModuleBuilder(ids::kMsaaOff, "Disable MSAA")
              .description("Rewrites EGL_SAMPLE_BUFFERS/EGL_SAMPLES to zero in "
                           "eglChooseConfig, so the engine picks a "
                           "single-sampled configuration. Saves a lot of fill "
                           "rate and memory bandwidth. Takes effect the next "
                           "time the game creates its graphics context.")
              .modId(kModId)
              .defaultEnabled(values.msaaOff)
              .onToggle(OnToggle)
              .registerModule();

    // --- 8) State call dedup -------------------------------------------------
    ok &= ModuleBuilder(ids::kStateDedup, "Skip Redundant GL State Calls")
              .description("Caches the current GL state and drops repeated "
                           "glEnable/glDisable/glBindTexture/glUseProgram/"
                           "glBindBuffer calls before they reach the driver. "
                           "The hooks for these entry points are only "
                           "installed while this module is on.")
              .modId(kModId)
              .defaultEnabled(values.stateDedup)
              .onToggle(OnToggle)
              .registerModule();

    // --- 9) glFinish ---------------------------------------------------------
    ok &= ModuleBuilder(ids::kNoGlFinish, "Remove glFinish() Stalls")
              .description("Converts glFinish() into glFlush(). glFinish waits "
                           "for the GPU to go idle and serialises the CPU "
                           "against the driver; glFlush only submits the "
                           "queued commands, which is all a frame boundary "
                           "needs.")
              .modId(kModId)
              .defaultEnabled(values.noGlFinish)
              .onToggle(OnToggle)
              .registerModule();

    // --- 10) Redundant clears ------------------------------------------------
    ok &= ModuleBuilder(ids::kSkipClears, "Skip Redundant Colour Clears")
              .description("Drops the colour half of a glClear() on the window "
                           "framebuffer: the first full-screen draw of the "
                           "frame overwrites those pixels anyway, so on a "
                           "tile-based GPU the clear is a wasted full-screen "
                           "write. Depth and stencil clears are kept. "
                           "Experimental - switch it off if the image looks "
                           "wrong.")
              .modId(kModId)
              .defaultEnabled(values.skipClears)
              .onToggle(OnToggle)
              .registerModule();

    // --- HUD -----------------------------------------------------------------
    ok &= ModuleBuilder(ids::kHud, "LeviBoost HUD")
              .description("Small overlay with the live frame rate and frame "
                           "time, how many optimizations are active and how "
                           "many GL calls they saved.")
              .modId(kModId)
              .defaultEnabled(values.hud)
              .onToggle(OnToggle)
              .config(keys::kHudCorner, "HUD corner", ConfigType::Radio, "0",
                      "Top left,Top right,Bottom left,Bottom right")
              .config(keys::kHudDetail, "HUD detail", ConfigType::Radio, "1",
                      "Minimal,Detailed")
              .onConfigChanged(OnConfigChanged)
              .registerModule();

    // --- master control ------------------------------------------------------
    ok &= ModuleBuilder(ids::kControl, "LeviBoost Control")
              .description("Master switch for every LeviBoost optimization. "
                           "Switching this off pauses the whole mod without "
                           "unloading it; the panic key does the same from "
                           "inside the game.")
              .modId(kModId)
              .defaultEnabled(values.enabled)
              .onToggle(OnToggle)
              .config(keys::kPanicKey, "Panic key (Android key code)",
                      ConfigType::Keybind, std::to_string(values.panicKey))
              .onConfigChanged(OnConfigChanged)
              .onKeybind(OnKeybind)
              .registerModule();

    return ok;
}

/// Publishes the modern (v2) configuration schema so the launcher renders
/// sliders, switches and radio groups instead of raw text fields.
void PublishSchemas(const leviboost::settings::Values &values) {
    using namespace leviboost;

    PublishSliderSchema(
        ids::kFpsCap, kCategoryOptimizations, "Frame rate",
        MakeNode("fps_cap_value", keys::kFpsCap, kCategoryOptimizations,
                   "Target FPS", "Frame rate the cap holds the game to.",
                   ConfigControlTypeV2::SliderInt, std::to_string(values.fpsCap),
                   "15", "240"),
        "Frame Rate Cap");

    PublishSliderSchema(
        ids::kRenderScale, kCategoryOptimizations, "Resolution",
        MakeNode("scale_percent", keys::kRenderScale, kCategoryOptimizations,
                   "Render scale %",
                   "Resolution the world is rendered at before it is stretched "
                   "to the screen.",
                   ConfigControlTypeV2::SliderInt,
                   std::to_string(values.renderScalePercent), "50", "100"),
        "Render Scale");

    PublishSliderSchema(
        ids::kLodBias, kCategoryOptimizations, "Texture detail",
        MakeNode("lod_bias_value", keys::kLodBias, kCategoryOptimizations,
                   "LOD bias",
                   "How far texture sampling is pushed towards smaller mip "
                   "levels.",
                   ConfigControlTypeV2::SliderFloat,
                   std::to_string(values.lodBias), "0.0", "4.0"),
        "Texture LOD Bias");

    {
        ConfigSchemaBuilder builder;
        builder.defaultCategory(kCategoryExtras);
        builder.category(kCategoryExtras, "HUD");
        const auto corner = [&]() {
            ConfigNodeV2 node = MakeNode(
                "hud_corner", keys::kHudCorner, kCategoryExtras, "HUD corner",
                "Screen corner the overlay is drawn in.",
                ConfigControlTypeV2::Choice, std::to_string(values.hudCorner));
            node.choiceStyle = pl::modmenu::ConfigChoiceStyleV2::Radio;
            node.options = {
                Option("0", "Top left"),
                Option("1", "Top right"),
                Option("2", "Bottom left"),
                Option("3", "Bottom right"),
            };
            return node;
        }();
        builder.node(corner);

        ConfigNodeV2 detail = MakeNode(
            "hud_detail", keys::kHudDetail, kCategoryExtras, "HUD detail",
            "How much information the overlay shows.",
            ConfigControlTypeV2::Choice, std::to_string(values.hudDetail));
        detail.choiceStyle = pl::modmenu::ConfigChoiceStyleV2::Radio;
        detail.options = {Option("0", "Minimal"), Option("1", "Detailed")};
        builder.node(detail);
        pl::modmenu::setConfigSchemaJson(ids::kHud, builder.toJson());
    }

    {
        ConfigSchemaBuilder builder;
        builder.defaultCategory(kCategoryExtras);
        builder.category(kCategoryExtras, "Safety");
        builder.node(MakeNode("panic_key_value", keys::kPanicKey,
                                kCategoryExtras, "Panic key",
                                "Android key code that disables every "
                                "optimization at once. Use the key picker.",
                                ConfigControlTypeV2::Keybind,
                                std::to_string(values.panicKey)));
        pl::modmenu::setConfigSchemaJson(ids::kControl, builder.toJson());
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Mod lifecycle
// ---------------------------------------------------------------------------
class LeviBoostMod {
public:
    LeviBoostMod() : mSelf(*ll::mod::NativeMod::current()) {}

    bool load() {
        g_logger = &getSelf().getLogger();
        leviboost::g_loggerFn = &LogSink;

        g_configDir = getSelf().getConfigDir().string();
        leviboost::settings::Values values;
        if (leviboost::settings::LoadFromDirectory(g_configDir, values)) {
            g_logger->info("settings loaded from {}", g_configDir);
        } else {
            g_logger->info("no settings file yet, using defaults ({})",
                           g_configDir);
        }
        leviboost::settings::Apply(values);
        mValues = values;
        g_logger->info("LeviBoost {} loaded - {}", leviboost::kVersion,
                       leviboost::opt::DescribeState());
        return true;
    }

    bool enable() {
        if (!RegisterModules(mValues)) {
            getSelf().getLogger().warn(
                "some Mod Menu modules could not be registered");
        }
        PublishSchemas(mValues);

        const bool button = pl::modmenu::ButtonBuilder("lb_open_menu", "LeviBoost")
                                .moduleId(leviboost::ids::kControl)
                                .modId(leviboost::kModId)
                                .label("LB")
                                .stylePreset(pl::modmenu::ButtonStylePreset::Accent)
                                .behavior(pl::modmenu::ButtonBehavior::Click)
                                .onEvent(OnButtonEvent)
                                .registerButton();
        if (!button) {
            getSelf().getLogger().warn("could not register the floating button");
        }

        // The graphics library is usually loaded after the mod, so a worker
        // thread waits for it and installs the detours as soon as it appears.
        g_stopWorker.store(false);
        if (pthread_create(&g_workerThread, nullptr, &WorkerMain, nullptr) == 0) {
            g_workerRunning = true;
        } else {
            leviboost::gl::Install();
        }

        getSelf().getLogger().info("enabled with {} of 10 optimizations active",
                                   leviboost::EnabledOptimizationCount());
        return true;
    }

    bool disable() {
        // The hooks stay installed but every detour becomes a pass-through,
        // which is safer than unpatching code the render thread is executing.
        leviboost::g_options.enabled.store(false);
        leviboost::hud::Clear();
        leviboost::gl::RefreshHookGroups();
        PersistSettings();
        return true;
    }

    bool unload() {
        StopWorker();
        leviboost::hud::Clear();
        leviboost::gl::RemoveAll();
        leviboost::g_loggerFn = nullptr;
        g_logger = nullptr;
        return true;
    }

private:
    void StopWorker() {
        if (!g_workerRunning) return;
        g_stopWorker.store(true);
        pthread_join(g_workerThread, nullptr);
        g_workerRunning = false;
    }

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    ll::mod::NativeMod &mSelf;
    leviboost::settings::Values mValues{};
};

PL_REGISTER_MOD(LeviBoostMod, LeviBoostMod{});
