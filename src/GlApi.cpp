//
// LeviBoost - graphics library discovery, symbol resolution, hook installation.
//
// Nothing here depends on a specific Bedrock build: the graphics libraries are
// located by scanning /proc/self/maps, their entry points are resolved with
// dlsym(), and the detours are installed with pl::memory::hook().  Functions
// an application fetches through eglGetProcAddress() are redirected by hooking
// eglGetProcAddress itself.
//

#include "GlApi.hpp"

#include <pl/memory/Hook.hpp>

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Optimizations.hpp"

namespace leviboost::gl {

Api g_api;

namespace {

enum class Group {
    Core,   // always installed
    State,  // only while "Skip Redundant GL State Calls" is on
    Draws,  // only while the HUD wants draw-call statistics
};

struct HookEntry {
    const char *name;
    void *detour;      // null = resolve only, never hook
    void **original;
    Group group;
    bool resolved = false;
    bool hooked = false;
    bool required = false;
    void *target = nullptr; // the raw driver address the detour replaced
};

// (C-style cast: it also accepts the literal nullptr used by resolve-only
//  entries, which reinterpret_cast refuses.)
#define LB_ENTRY(name, detour, field, group, required)                         \
    { name, (void *)(detour), reinterpret_cast<void **>(&g_api.field), group,  \
      false, false, required }

HookEntry g_entries[] = {
    // --- EGL -----------------------------------------------------------------
    LB_ENTRY("eglGetProcAddress", &opt::GetProcAddress, eglGetProcAddress,
             Group::Core, true),
    LB_ENTRY("eglSwapBuffers", &opt::SwapBuffers, eglSwapBuffers, Group::Core, true),
    LB_ENTRY("eglSwapBuffersWithDamageKHR", &opt::SwapBuffersWithDamageKHR,
             eglSwapBuffersWithDamageKHR, Group::Core, false),
    LB_ENTRY("eglSwapInterval", &opt::SwapInterval, eglSwapInterval, Group::Core,
             true),
    LB_ENTRY("eglChooseConfig", &opt::ChooseConfig, eglChooseConfig, Group::Core,
             false),
    LB_ENTRY("eglGetConfigAttrib", &opt::GetConfigAttrib, eglGetConfigAttrib,
             Group::Core, false),
    LB_ENTRY("eglMakeCurrent", &opt::MakeCurrent, eglMakeCurrent, Group::Core,
             true),
    LB_ENTRY("eglQuerySurface", nullptr, eglQuerySurface, Group::Core, false),

    // --- GLES entry points LeviBoost detours ---------------------------------
    LB_ENTRY("glViewport", &opt::Viewport, glViewport, Group::Core, false),
    LB_ENTRY("glScissor", &opt::Scissor, glScissor, Group::Core, false),
    LB_ENTRY("glClear", &opt::Clear, glClear, Group::Core, false),
    LB_ENTRY("glFinish", &opt::Finish, glFinish, Group::Core, false),
    LB_ENTRY("glTexParameterf", &opt::TexParameterf, glTexParameterf, Group::Core,
             false),
    LB_ENTRY("glTexParameteri", &opt::TexParameteri, glTexParameteri, Group::Core,
             false),
    LB_ENTRY("glTexParameterfv", &opt::TexParameterfv, glTexParameterfv,
             Group::Core, false),
    LB_ENTRY("glTexParameteriv", &opt::TexParameteriv, glTexParameteriv,
             Group::Core, false),
    LB_ENTRY("glBindFramebuffer", &opt::BindFramebuffer, glBindFramebuffer,
             Group::Core, false),

    // --- GL state dedup hooks ------------------------------------------------
    LB_ENTRY("glEnable", &opt::Enable, glEnable, Group::State, false),
    LB_ENTRY("glDisable", &opt::Disable, glDisable, Group::State, false),
    LB_ENTRY("glActiveTexture", &opt::ActiveTexture, glActiveTexture, Group::State,
             false),
    LB_ENTRY("glBindTexture", &opt::BindTexture, glBindTexture, Group::State,
             false),
    LB_ENTRY("glBindBuffer", &opt::BindBuffer, glBindBuffer, Group::State, false),
    LB_ENTRY("glUseProgram", &opt::UseProgram, glUseProgram, Group::State, false),
    LB_ENTRY("glDepthMask", &opt::DepthMask, glDepthMask, Group::State, false),
    LB_ENTRY("glBlendFunc", &opt::BlendFunc, glBlendFunc, Group::State, false),
    LB_ENTRY("glCullFace", &opt::CullFace, glCullFace, Group::State, false),
    LB_ENTRY("glFrontFace", &opt::FrontFace, glFrontFace, Group::State, false),
    LB_ENTRY("glDeleteTextures", &opt::DeleteTextures, glDeleteTextures,
             Group::State, false),

    // --- GLES entry points LeviBoost only calls ------------------------------
    LB_ENTRY("glGetIntegerv", nullptr, glGetIntegerv, Group::Core, false),
    LB_ENTRY("glGetError", nullptr, glGetError, Group::Core, false),
    LB_ENTRY("glIsEnabled", nullptr, glIsEnabled, Group::Core, false),
    LB_ENTRY("glColorMask", nullptr, glColorMask, Group::Core, false),
    LB_ENTRY("glFlush", nullptr, glFlush, Group::Core, false),
    LB_ENTRY("glGenTextures", nullptr, glGenTextures, Group::Core, false),
    LB_ENTRY("glTexImage2D", nullptr, glTexImage2D, Group::Core, false),
    LB_ENTRY("glGenFramebuffers", nullptr, glGenFramebuffers, Group::Core, false),
    LB_ENTRY("glDeleteFramebuffers", nullptr, glDeleteFramebuffers, Group::Core,
             false),
    LB_ENTRY("glFramebufferTexture2D", nullptr, glFramebufferTexture2D,
             Group::Core, false),
    LB_ENTRY("glCheckFramebufferStatus", nullptr, glCheckFramebufferStatus,
             Group::Core, false),
    LB_ENTRY("glBlitFramebuffer", nullptr, glBlitFramebuffer, Group::Core, false),

    // --- draw-call counters for the HUD --------------------------------------
    LB_ENTRY("glDrawArrays", &opt::DrawArrays, glDrawArrays, Group::Draws, false),
    LB_ENTRY("glDrawElements", &opt::DrawElements, glDrawElements, Group::Draws,
             false),
};

#undef LB_ENTRY

std::vector<std::string> g_libraries;
bool g_stateHooksActive = false;
std::string g_report = "graphics libraries not found yet";
bool g_ready = false;
int g_hookCount = 0;

bool ContainsInsensitive(const std::string &haystack, const char *needle) {
    const std::size_t length = std::strlen(needle);
    if (haystack.size() < length) return false;
    for (std::size_t i = 0; i + length <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < length; ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

std::string BaseName(const std::string &path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// Collects the mapped graphics libraries in the order the dynamic loader
/// would present them to the game.
std::vector<std::string> ScanProcessMaps() {
    std::vector<std::string> found;
    std::FILE *file = std::fopen("/proc/self/maps", "r");
    if (!file) return found;

    char line[512];
    int lines = 0;
    while (std::fgets(line, sizeof(line), file) && lines < 20000) {
        ++lines;
        const char *start = std::strchr(line, '/');
        if (!start) continue;
        std::string path(start);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r' ||
                                 path.back() == ' ')) {
            path.pop_back();
        }
        const std::string base = BaseName(path);
        const bool interesting =
            base.rfind("libEGL", 0) == 0 || base.rfind("libGLES", 0) == 0 ||
            ContainsInsensitive(base, "angle") ||
            ContainsInsensitive(base, "libgles");
        if (!interesting) continue;
        if (std::find(found.begin(), found.end(), path) == found.end()) {
            found.push_back(path);
        }
    }
    std::fclose(file);

    // Prefer the platform libraries (what the game actually links against)
    // over vendor drivers, then the ANGLE builds, then everything else.
    auto rank = [](const std::string &path) {
        if (path.rfind("/system/", 0) == 0) return 0;
        if (path.rfind("/apex/", 0) == 0) return 1;
        if (path.rfind("/system_ext/", 0) == 0) return 2;
        if (ContainsInsensitive(path, "angle")) return 3;
        if (path.rfind("/vendor/", 0) == 0) return 5;
        return 4;
    };
    std::stable_sort(found.begin(), found.end(),
                     [&](const std::string &left, const std::string &right) {
                         return rank(left) < rank(right);
                     });
    return found;
}

bool ResolveAddress(const char *name, void **out) {
    for (const std::string &library : g_libraries) {
        void *handle = dlopen(library.c_str(), RTLD_NOW | RTLD_NOLOAD);
        if (!handle) continue;
        void *symbol = dlsym(handle, name);
        if (symbol) {
            *out = symbol;
            return true;
        }
    }
    void *symbol = dlsym(RTLD_DEFAULT, name);
    if (symbol) {
        *out = symbol;
        return true;
    }
    return false;
}

void ResolveEntry(HookEntry &entry) {
    if (entry.resolved) return;
    void *address = nullptr;
    if (ResolveAddress(entry.name, &address)) {
        *entry.original = address;
        entry.resolved = true;
    }
}

void HookEntryIfNeeded(HookEntry &entry) {
    if (!entry.detour || entry.hooked || !entry.resolved) return;
    if (*entry.original == nullptr) return;
    void *target = *entry.original;
    void *original = nullptr;
    const int result = pl::memory::hook(target, entry.detour, &original,
                                        pl::memory::HookPriority::Normal);
    if (result != 0 || original == nullptr) {
        LogWarn(std::string("could not hook ") + entry.name);
        return;
    }
    entry.target = target;
    *entry.original = original; // detours call the driver through this
    entry.hooked = true;
    ++g_hookCount;
}

void UnhookEntry(HookEntry &entry) {
    if (!entry.hooked) return;
    if (entry.target != nullptr) {
        pl::memory::unhook(entry.target, entry.detour);
        // Back to the raw driver address: the trampoline handed out by the
        // hook library must not be called after the hook is removed.
        *entry.original = entry.target;
        entry.target = nullptr;
    }
    entry.hooked = false;
    if (g_hookCount > 0) --g_hookCount;
}

bool GroupEnabled(Group group) {
    switch (group) {
    case Group::Core: return true;
    case Group::State: return Active(g_options.stateDedup);
    case Group::Draws: return Active(g_options.hud);
    }
    return false;
}

/// Installs or removes the optional hook groups to match the live options.
void ReconcileOptionalHooks() {
    bool enabledStateHooks = false;
    for (HookEntry &entry : g_entries) {
        if (entry.group == Group::Core) continue;
        if (GroupEnabled(entry.group)) {
            HookEntryIfNeeded(entry);
            if (entry.group == Group::State && entry.hooked) {
                enabledStateHooks = true;
            }
        } else {
            UnhookEntry(entry);
        }
    }
    // The state hooks bypass the dedup cache while they are detached, so the
    // cache has to start from scratch when they come back.
    if (enabledStateHooks && !g_stateHooksActive) {
        opt::InvalidateStateCache();
    }
    g_stateHooksActive = enabledStateHooks;
}

} // namespace

bool Installed() { return g_ready; }
bool HooksReady() { return g_ready; }
int HookedFunctionCount() { return g_hookCount; }
const std::string &LibraryReport() { return g_report; }

bool Install() {
    if (g_libraries.empty()) {
        g_libraries = ScanProcessMaps();
        if (!g_libraries.empty()) {
            g_report = "graphics libraries:";
            for (const std::string &library : g_libraries) {
                g_report += " " + BaseName(library);
            }
        }
    }

    for (HookEntry &entry : g_entries) {
        ResolveEntry(entry);
    }

    // eglGetProcAddress is the first entry in the table, and it is hooked
    // before the rest so the game can never resolve an entry point behind our
    // back, even if it initialises GL between two of our hook calls.
    for (HookEntry &entry : g_entries) {
        if (entry.group == Group::Core) HookEntryIfNeeded(entry);
    }
    ReconcileOptionalHooks();

    if (!g_ready) {
        // Index 1 is eglSwapBuffers (eglGetProcAddress is deliberately first).
        const bool swapReady = g_entries[1].hooked;
        const bool viewportReady = g_api.glViewport != nullptr;
        const bool procReady = g_api.eglGetProcAddress != nullptr;
        if (swapReady && (viewportReady || procReady)) {
            g_ready = true;
            LogInfo("GL hooks active (" + std::to_string(g_hookCount) +
                    " detours) - " + g_report);
        }
    }
    return g_ready;
}

void RemoveAll() {
    for (HookEntry &entry : g_entries) {
        UnhookEntry(entry);
    }
    g_ready = false;
}

void SetDrawCallCountingEnabled(bool enabled) {
    (void)enabled;
    ReconcileOptionalHooks();
}

void RefreshHookGroups() { ReconcileOptionalHooks(); }

} // namespace leviboost::gl
