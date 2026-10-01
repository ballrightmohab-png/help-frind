#include "Settings.hpp"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace leviboost::settings {
namespace {

std::string Trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() &&
           (text[first] == ' ' || text[first] == '\t' || text[first] == '\r')) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first &&
           (text[last - 1] == ' ' || text[last - 1] == '\t' || text[last - 1] == '\r')) {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

bool ParseBool(const std::string &text, bool fallback) {
    if (text == "1" || text == "true" || text == "on" || text == "yes") return true;
    if (text == "0" || text == "false" || text == "off" || text == "no") return false;
    return fallback;
}

std::string Join(const std::string &directory, const char *name) {
    if (directory.empty()) return std::string(name);
    if (directory.back() == '/') return directory + name;
    return directory + "/" + name;
}

} // namespace

Values Defaults() { return Values{}; }

Values Capture() {
    Values values;
    values.enabled = g_options.enabled.load(std::memory_order_relaxed);
    values.uncapFps = g_options.uncapFps.load(std::memory_order_relaxed);
    values.fpsCapEnabled = g_options.fpsCapEnabled.load(std::memory_order_relaxed);
    values.fpsCap = g_options.fpsCap.load(std::memory_order_relaxed);
    values.renderScaleEnabled =
        g_options.renderScaleEnabled.load(std::memory_order_relaxed);
    values.renderScalePercent =
        g_options.renderScalePercent.load(std::memory_order_relaxed);
    values.anisoClamp = g_options.anisoClamp.load(std::memory_order_relaxed);
    values.mipDowngrade = g_options.mipDowngrade.load(std::memory_order_relaxed);
    values.lodBiasEnabled =
        g_options.lodBiasEnabled.load(std::memory_order_relaxed);
    values.lodBias = g_options.lodBias.load(std::memory_order_relaxed);
    values.msaaOff = g_options.msaaOff.load(std::memory_order_relaxed);
    values.stateDedup = g_options.stateDedup.load(std::memory_order_relaxed);
    values.noGlFinish = g_options.noGlFinish.load(std::memory_order_relaxed);
    values.skipClears = g_options.skipClears.load(std::memory_order_relaxed);
    values.hud = g_options.hud.load(std::memory_order_relaxed);
    values.hudCorner = g_options.hudCorner.load(std::memory_order_relaxed);
    values.hudDetail = g_options.hudDetail.load(std::memory_order_relaxed);
    values.panicKey = g_options.panicKey.load(std::memory_order_relaxed);
    return values;
}

void Apply(const Values &values) {
    g_options.enabled.store(values.enabled, std::memory_order_relaxed);
    g_options.uncapFps.store(values.uncapFps, std::memory_order_relaxed);
    g_options.fpsCapEnabled.store(values.fpsCapEnabled, std::memory_order_relaxed);
    g_options.fpsCap.store(ClampInt(values.fpsCap, 10, 480),
                           std::memory_order_relaxed);
    g_options.renderScaleEnabled.store(values.renderScaleEnabled,
                                       std::memory_order_relaxed);
    g_options.renderScalePercent.store(ClampInt(values.renderScalePercent, 50, 100),
                                       std::memory_order_relaxed);
    g_options.anisoClamp.store(values.anisoClamp, std::memory_order_relaxed);
    g_options.mipDowngrade.store(values.mipDowngrade, std::memory_order_relaxed);
    g_options.lodBiasEnabled.store(values.lodBiasEnabled,
                                   std::memory_order_relaxed);
    g_options.lodBias.store(ClampFloat(values.lodBias, 0.0f, 4.0f),
                            std::memory_order_relaxed);
    g_options.msaaOff.store(values.msaaOff, std::memory_order_relaxed);
    g_options.stateDedup.store(values.stateDedup, std::memory_order_relaxed);
    g_options.noGlFinish.store(values.noGlFinish, std::memory_order_relaxed);
    g_options.skipClears.store(values.skipClears, std::memory_order_relaxed);
    g_options.hud.store(values.hud, std::memory_order_relaxed);
    g_options.hudCorner.store(ClampInt(values.hudCorner, 0, 3),
                              std::memory_order_relaxed);
    g_options.hudDetail.store(ClampInt(values.hudDetail, 0, 1),
                              std::memory_order_relaxed);
    g_options.panicKey.store(ClampInt(values.panicKey, 0, 400),
                             std::memory_order_relaxed);
}

std::string Serialize(const Values &values) {
    std::ostringstream out;
    out << "# LeviBoost settings. Deleting a line restores that option's default.\n";
    out << "enabled=" << (values.enabled ? 1 : 0) << '\n';
    out << "uncap_fps=" << (values.uncapFps ? 1 : 0) << '\n';
    out << "fps_cap_enabled=" << (values.fpsCapEnabled ? 1 : 0) << '\n';
    out << "fps_cap=" << values.fpsCap << '\n';
    out << "render_scale_enabled=" << (values.renderScaleEnabled ? 1 : 0) << '\n';
    out << "render_scale_percent=" << values.renderScalePercent << '\n';
    out << "aniso_clamp=" << (values.anisoClamp ? 1 : 0) << '\n';
    out << "mip_downgrade=" << (values.mipDowngrade ? 1 : 0) << '\n';
    out << "lod_bias_enabled=" << (values.lodBiasEnabled ? 1 : 0) << '\n';
    out << "lod_bias=" << values.lodBias << '\n';
    out << "msaa_off=" << (values.msaaOff ? 1 : 0) << '\n';
    out << "state_dedup=" << (values.stateDedup ? 1 : 0) << '\n';
    out << "no_glfinish=" << (values.noGlFinish ? 1 : 0) << '\n';
    out << "skip_clears=" << (values.skipClears ? 1 : 0) << '\n';
    out << "hud=" << (values.hud ? 1 : 0) << '\n';
    out << "hud_corner=" << values.hudCorner << '\n';
    out << "hud_detail=" << values.hudDetail << '\n';
    out << "panic_key=" << values.panicKey << '\n';
    return out.str();
}

bool Deserialize(std::string_view text, Values &values) {
    values = Values{};
    std::size_t position = 0;
    int applied = 0;
    while (position <= text.size()) {
        const std::size_t end = text.find('\n', position);
        const std::string_view line =
            text.substr(position, end == std::string_view::npos
                                      ? std::string_view::npos
                                      : end - position);
        position = (end == std::string_view::npos) ? text.size() + 1 : end + 1;

        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = Trim(std::string_view(trimmed).substr(0, equals));
        const std::string value =
            Trim(std::string_view(trimmed).substr(equals + 1));

        auto asBool = [&](bool &target) { target = ParseBool(value, target); };
        auto toInt = [&](int fallback) {
            return value.empty() ? fallback : std::atoi(value.c_str());
        };
        auto toFloat = [&](float fallback) {
            return value.empty() ? fallback : static_cast<float>(std::atof(value.c_str()));
        };

        if (key == "enabled") asBool(values.enabled);
        else if (key == "uncap_fps") asBool(values.uncapFps);
        else if (key == "fps_cap_enabled") asBool(values.fpsCapEnabled);
        else if (key == "fps_cap") values.fpsCap = toInt(values.fpsCap);
        else if (key == "render_scale_enabled") asBool(values.renderScaleEnabled);
        else if (key == "render_scale_percent")
            values.renderScalePercent = toInt(values.renderScalePercent);
        else if (key == "aniso_clamp") asBool(values.anisoClamp);
        else if (key == "mip_downgrade") asBool(values.mipDowngrade);
        else if (key == "lod_bias_enabled") asBool(values.lodBiasEnabled);
        else if (key == "lod_bias") values.lodBias = toFloat(values.lodBias);
        else if (key == "msaa_off") asBool(values.msaaOff);
        else if (key == "state_dedup") asBool(values.stateDedup);
        else if (key == "no_glfinish") asBool(values.noGlFinish);
        else if (key == "skip_clears") asBool(values.skipClears);
        else if (key == "hud") asBool(values.hud);
        else if (key == "hud_corner") values.hudCorner = toInt(values.hudCorner);
        else if (key == "hud_detail") values.hudDetail = toInt(values.hudDetail);
        else if (key == "panic_key") values.panicKey = toInt(values.panicKey);
        else continue;
        ++applied;
    }
    return applied > 0;
}

bool LoadFromDirectory(const std::string &configDir, Values &values) {
    const std::string path = Join(configDir, kFileName);
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        values = Values{};
        return false;
    }
    std::string text;
    char buffer[512];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, read);
    }
    std::fclose(file);
    Deserialize(text, values);
    return true;
}

bool SaveToDirectory(const std::string &configDir, const Values &values) {
    if (configDir.empty()) return false;
    const std::string path = Join(configDir, kFileName);
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const std::string text = Serialize(values);
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    return written == text.size();
}

} // namespace leviboost::settings
