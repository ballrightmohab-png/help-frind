#pragma once
//
// LeviBoost - on-screen HUD.
//
// The overlay is published through the Mod Menu draw-command API, so it is
// rendered by the launcher's own renderer on top of the game and needs no
// font or image assets of its own.
//

#include <string>

namespace leviboost::hud {

/// Builds the current overlay.  Cheap enough to call a few times per second.
void Publish();

/// Clears the overlay (module off / mod disabled).
void Clear();

/// Last published line count (used by the tests and the log).
int LastLineCount();

} // namespace leviboost::hud
