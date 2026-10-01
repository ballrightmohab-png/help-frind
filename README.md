# LeviBoost

Ten performance optimizations for **Minecraft Bedrock on Android** running under
[LeviLaunchroid / LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid) — each
one a real, switchable Mod Menu module.

## Download and install

**Download `LeviBoost.levipack` from the
[Releases page](https://github.com/ballrightmohab-png/help-frind/releases/latest).**
It is also committed at [`dist/LeviBoost.levipack`](dist/LeviBoost.levipack).

Then, in LeviLaunchroid:

1. **Mods → Import / Add mod** (the `+` button) and pick `LeviBoost.levipack`.
2. Launch Minecraft — the modules appear under **LeviBoost** in the in-game
   Mod Menu, and a floating **LB** button opens the menu on the spot.

> Do not use the *Actions* tab to get the mod: workflow artifacts can only be
> downloaded by a signed-in GitHub account, and an unfinished build produces no
> artifact at all. Releases are public and need no account.

`LeviBoost.levipack` is a preload-native mod package (a zip holding
`manifest.json`, `libleviboost.so` and `icon.png`).

## The ten optimizations

| # | Module | What it actually does |
|---|--------|------------------------|
| 1 | **Uncap FPS (disable VSync)** | Forces `eglSwapInterval(0)`. Removes the display-refresh ceiling that holds many phones at 30/60 FPS even when the GPU has headroom. |
| 2 | **Frame Rate Cap (thermal guard)** | Paces frames in the swap hook to a target FPS (15–240). Capping below the thermal limit usually raises the *sustained* frame rate. |
| 3 | **Render Scale (experimental)** | Renders the world at 50–100 % and stretches the frame back to the window in the swap hook. Self-disables with a logged reason if the engine uses a render path it cannot split (e.g. a multisampled window — use *Disable MSAA* first). |
| 4 | **Anisotropic Filtering 1x** | Clamps `GL_TEXTURE_MAX_ANISOTROPY_EXT` to 1. Anisotropic filtering is the most expensive texture sampling mode on mobile GPUs. |
| 5 | **Fast Mipmap Filtering** | Turns trilinear minification (`GL_LINEAR_MIPMAP_LINEAR`) into bilinear-between-mips, halving texture fetches for distant terrain. |
| 6 | **Texture LOD Bias** | Adds a bias to `GL_TEXTURE_LOD_BIAS` (0.0–4.0) so textures sample smaller mip levels. |
| 7 | **Disable MSAA** | Rewrites `EGL_SAMPLE_BUFFERS`/`EGL_SAMPLES` to 0 in `eglChooseConfig` and keep the answers consistent in `eglGetConfigAttrib`. Applies when the game next creates its context. |
| 8 | **Skip Redundant GL State Calls** | Caches GL state and drops repeated `glEnable`/`glDisable`/`glBindTexture`/`glUseProgram`/`glBindBuffer`/… calls before they reach the driver. The hooks for these entry points are only installed while the module is on. |
| 9 | **Remove glFinish() Stalls** | Converts `glFinish()` into `glFlush()`. `glFinish` idles the GPU and serialises the CPU against the driver; `glFlush` submits and returns. |
| 10 | **Skip Redundant Colour Clears** | Drops the colour half of a `glClear()` on the game window (the first full-screen draw covers those pixels). Depth/stencil clears are kept; clears on render targets are never touched. |

Two more modules manage everything else:

* **LeviBoost HUD** — FPS, frame time, active optimizations, hook count and the
  per-optimization counters, in any screen corner, with a minimal/detailed mode.
* **LeviBoost Control** — master switch and a configurable panic key that turns
  everything off from inside the game. A floating **LB** button opens the Mod
  Menu.

Every toggle takes effect on the next frame (the detours read the live option
flags), and the settings are written to `<mod config dir>/leviboost.cfg` so they
survive a restart.

## How it works

LeviBoost never patches game code and never needs a game offset, which is what
makes it survive Bedrock updates:

1. A worker thread scans `/proc/self/maps` for the graphics libraries and waits
   for the game to load them.
2. The EGL/GLES entry points are resolved with `dlsym()`.
3. `pl::memory::hook()` installs a detour per entry point and returns a
   trampoline that calls the driver.
4. `eglGetProcAddress` is hooked too, so applications that resolve their GL
   entry points dynamically still execute the detours.
5. Each detour starts by reading its option flag — off means the call is
   forwarded untouched, i.e. exactly unmodded behaviour.

```
src/LeviBoost.hpp      options, statistics, logging, clock helpers
src/Main.cpp           mod lifecycle, Mod Menu registration, callbacks, worker
src/Optimizations.cpp  the ten detours + render-scale framebuffer path + pacing
src/GlApi.cpp/.hpp     /proc/self/maps scan, dlsym, hook install/removal
src/Settings.cpp/.hpp  leviboost.cfg load/save/clamp
src/Hud.cpp/.hpp       overlay published through the Mod Menu draw-command API
preloader_headers/     pinned copy of the LiteLDev SDK headers used to build
tests/                 host test suite with a fake GL driver
```

The Mod Menu API is the one documented by the SDK: `ModuleBuilder` modules with
`onToggle` / `onConfigChanged` / `onKeybind` callbacks, a v2 config schema for
the sliders and radio groups, a `ButtonBuilder` floating button, and
`submitDrawCommands` for the HUD.

## Building

Requirements: Android NDK **r26b+** (r27+ recommended for 16 KB pages), CMake
3.22+, Ninja, a host C++20 compiler for the tests.

```bash
export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/27.0.12077973
./build.sh                  # tests -> arm64-v8a build -> dist/LeviBoost.levipack
./build.sh --skip-tests     # build and package only
```

Or by hand:

```bash
cmake -S . -B build/android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/android --target leviboost
python3 scripts/package_levipack.py --library build/android/out/arm64-v8a/libleviboost.so \
  --output dist/LeviBoost.levipack
python3 scripts/verify_levipack.py dist/LeviBoost.levipack
```

`-DANDROID_STL=c++_shared` matters: the launcher process already has
`libc++_shared.so` loaded and the mod links against it. Pass
`-DLEVIBOOST_PRELOADER_ROOT=<preloader-android checkout>` to build against fresh
SDK headers instead of the pinned `preloader_headers/` copy.

## Tests

The optimization engine is exercised on the host against a fake GL driver, so
no device is needed:

```bash
g++ -std=c++20 -O1 -Isrc -Itests/stubs \
    tests/optimizations_test.cpp src/Optimizations.cpp src/Settings.cpp \
    -o /tmp/leviboost_tests && /tmp/leviboost_tests
```

The suite drives every detour with an option on and off and asserts what
reaches the driver: the swap interval, the pacing sleep, viewport/scissor
scaling and the upscale blit, filter and LOD rewrites, MSAA attribute
rewriting, dedup hit/miss behaviour (including context switches and deleted
textures), `glFinish` conversion, clear skipping, the master switch, the frame
statistics and settings round-tripping.

## Notes and caveats

* **Render Scale** and **Skip Redundant Colour Clears** are marked experimental
  because they change how a frame is composed. Both self-disable (or can be
  switched off) if the image looks wrong; render scale reports the reason to
  logcat and flips its own switch back.
* **Disable MSAA** changes the configuration the game requests at startup, so it
  applies the next time the graphics context is created.
* The mod only detours GL/EGL, so it stays compatible across Bedrock builds. It
  targets arm64-v8a, Android 8.0+ (minSdk 28, like the launcher).
* If the launcher does not load the mod, check logcat for `LeviBoost`: the mod
  logs the libraries it found, every hook it installed, and any hook it could
  not install.

## License

MIT — see `LICENSE`.
