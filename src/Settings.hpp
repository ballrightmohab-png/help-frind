#pragma once
//
// LeviBoost - persisted settings.
//
// The Mod Menu keeps its own copy of the values while the game is running;
// this file makes them survive a restart.  The format is a trivial
// "key=value" text file so it can be inspected and edited by hand, and so a
// half-written file can never make the mod unloadable.
//

#include <string>
#include <string_view>

#include "LeviBoost.hpp"

namespace leviboost::settings {

/// Mirror of every user-visible option (plain values, safe to copy).
struct Values {
    bool enabled{true};
    bool uncapFps{true};
    bool fpsCapEnabled{false};
    int fpsCap{60};
    bool renderScaleEnabled{false};
    int renderScalePercent{85};
    bool anisoClamp{true};
    bool mipDowngrade{true};
    bool lodBiasEnabled{false};
    float lodBias{0.5f};
    bool msaaOff{false};
    bool stateDedup{true};
    bool noGlFinish{true};
    bool skipClears{false};
    bool hud{true};
    int hudCorner{0};
    int hudDetail{1};
    int panicKey{0};
};

/// File name inside the mod's config directory.
inline constexpr const char *kFileName = "leviboost.cfg";

Values Defaults();
Values Capture();
void Apply(const Values &values);

std::string Serialize(const Values &values);
bool Deserialize(std::string_view text, Values &values);

/// Reads kFileName from `configDir`.  Missing files are not an error: the
/// defaults are returned instead.  Returns false when the file could not be
/// read at all (which the caller may want to log).
bool LoadFromDirectory(const std::string &configDir, Values &values);
bool SaveToDirectory(const std::string &configDir, const Values &values);

} // namespace leviboost::settings
