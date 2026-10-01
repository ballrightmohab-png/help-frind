#include "Hud.hpp"

#include <pl/ModMenu.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "GlApi.hpp"
#include "LeviBoost.hpp"
#include "Optimizations.hpp"

namespace leviboost::hud {
namespace {

using pl::modmenu::DrawCommand;
using pl::modmenu::DrawCommandType;

int g_lastLines = 0;

constexpr uint32_t kBackground = 0x8C101014; // semi transparent dark panel
constexpr uint32_t kTitleColor = 0xFF66E07A;
constexpr uint32_t kBodyColor = 0xFFF2F6F2;
constexpr uint32_t kMutedColor = 0xFFB8C4B8;
constexpr float kMargin = 8.0f;
constexpr float kLineHeight = 15.0f;
constexpr float kCharWidth = 7.0f; // rough estimate used for the panel size

std::string FormatCount(std::uint64_t value) {
    if (value < 1000) return std::to_string(value);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1fk",
                  static_cast<double>(value) / 1000.0);
    return std::string(buffer);
}

std::string FormatFloat(float value, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals,
                  static_cast<double>(value));
    return std::string(buffer);
}

void AddText(std::vector<DrawCommand> &commands, float x, float y,
             const std::string &text, uint32_t color) {
    DrawCommand command;
    command.type = DrawCommandType::Text;
    command.x = x;
    command.y = y;
    command.text = text;
    command.color = color;
    command.size = 1.0f;
    commands.push_back(std::move(command));
}

} // namespace

int LastLineCount() { return g_lastLines; }

void Publish() {
    if (!g_options.hud.load(std::memory_order_relaxed) ||
        !g_options.enabled.load(std::memory_order_relaxed)) {
        return;
    }

    std::vector<std::string> lines;
    lines.push_back("LeviBoost  " +
                    FormatFloat(g_stats.fps.load(std::memory_order_relaxed), 0) +
                    " FPS  " +
                    FormatFloat(g_stats.frameTimeMs.load(std::memory_order_relaxed),
                                1) +
                    " ms");
    lines.push_back("Optimizations " + std::to_string(EnabledOptimizationCount()) +
                    "/10   hooks " + std::to_string(gl::HookedFunctionCount()));
    if (g_options.fpsCapEnabled.load(std::memory_order_relaxed)) {
        lines.push_back("Frame cap " +
                        std::to_string(g_options.fpsCap.load(
                            std::memory_order_relaxed)) +
                        " FPS   throttled " +
                        FormatCount(g_stats.framesCapped.load(
                            std::memory_order_relaxed)));
    }
    if (g_options.renderScaleEnabled.load(std::memory_order_relaxed)) {
        lines.push_back("Render scale " +
                        std::to_string(g_options.renderScalePercent.load(
                            std::memory_order_relaxed)) +
                        "%");
    }

    const int detail = g_options.hudDetail.load(std::memory_order_relaxed);
    if (detail >= 1) {
        lines.push_back("draws " +
                        FormatCount(g_stats.drawCallsLastFrame.load(
                            std::memory_order_relaxed)) +
                        "   state skips " +
                        FormatCount(g_stats.stateCallsSkipped.load(
                            std::memory_order_relaxed)));
        lines.push_back("aniso " +
                        FormatCount(g_stats.anisoClamped.load(
                            std::memory_order_relaxed)) +
                        "   mips " +
                        FormatCount(g_stats.mipDowngraded.load(
                            std::memory_order_relaxed)) +
                        "   lod " +
                        FormatCount(g_stats.lodBiased.load(
                            std::memory_order_relaxed)));
        lines.push_back("flush " +
                        FormatCount(g_stats.finishesSkipped.load(
                            std::memory_order_relaxed)) +
                        "   clears " +
                        FormatCount(g_stats.clearsSkipped.load(
                            std::memory_order_relaxed)) +
                        "   msaa " +
                        FormatCount(g_stats.msaaConfigs.load(
                            std::memory_order_relaxed)));
    }

    std::size_t longest = 0;
    for (const std::string &line : lines) {
        if (line.size() > longest) longest = line.size();
    }

    const pl::modmenu::HudSurfaceSize surface = pl::modmenu::getHudSurfaceSize();
    const float panelWidth =
        kMargin * 2.0f + kCharWidth * static_cast<float>(longest);
    const float panelHeight =
        kMargin + kLineHeight * static_cast<float>(lines.size());

    float x = kMargin;
    float y = kMargin;
    const int corner = g_options.hudCorner.load(std::memory_order_relaxed);
    if (surface.width > 0.0f && (corner == 1 || corner == 3)) {
        x = surface.width - panelWidth - kMargin;
        if (x < kMargin) x = kMargin;
    }
    if (surface.height > 0.0f && (corner == 2 || corner == 3)) {
        y = surface.height - panelHeight - kMargin;
        if (y < kMargin) y = kMargin;
    }

    std::vector<DrawCommand> commands;
    commands.reserve(lines.size() + 1);

    DrawCommand background;
    background.type = DrawCommandType::RectFilled;
    background.x = x;
    background.y = y - 2.0f;
    background.w = panelWidth;
    background.h = panelHeight;
    background.color = kBackground;
    commands.push_back(background);

    float lineY = y;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const uint32_t color =
            i == 0 ? kTitleColor : (i == 1 ? kBodyColor : kMutedColor);
        AddText(commands, x + kMargin, lineY, lines[i], color);
        lineY += kLineHeight;
    }

    g_lastLines = static_cast<int>(lines.size());
    pl::modmenu::submitDrawCommands(ids::kHud, commands);
}

void Clear() {
    g_lastLines = 0;
    const std::vector<DrawCommand> empty;
    pl::modmenu::submitDrawCommands(ids::kHud, empty);
}

} // namespace leviboost::hud
