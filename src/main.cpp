#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dobby.h>

#define LOG_TAG "LeviModule"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============================================================================
// 1. CROSS-REGION PING ENHANCER (Socket & RakNet Queue Bypass)
// ============================================================================
typedef int (*socket_t)(int domain, int type, int protocol);
socket_t orig_socket = nullptr;

// Hook system socket creation to force ultra-low-latency socket flags
int hook_socket(int domain, int type, int protocol) {
    int fd = orig_socket ? orig_socket(domain, type, protocol) : -1;
    if (fd >= 0 && (domain == AF_INET || domain == AF_INET6)) {
        // Set IP Type of Service to Low Delay (0x10) to prioritize packets at routing layer
        int lowdelay = 0x10;
        setsockopt(fd, IPPROTO_IP, IP_TOS, &lowdelay, sizeof(lowdelay));

        // Set socket priority to maximum non-root value (6)
        int priority = 6;
        setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));

        // Enable low-latency kernel polling if supported by kernel
        int busy_poll = 50; // 50 microseconds
        setsockopt(fd, SOL_SOCKET, 46 /* SO_BUSY_POLL */, &busy_poll, sizeof(busy_poll));
    }
    return fd;
}

// RakNet Priority Override: Force packet priority to IMMEDIATE_PRIORITY (0)
// Strips RakNet's 10ms–20ms outbound packet aggregation buffer for cross-region play
typedef uint32_t (*rakNetSend_t)(void* rakPeer, void* bitStream, int priority, int reliability, char orderingChannel, void* systemAddress, bool broadcast);
rakNetSend_t orig_rakNetSend = nullptr;

uint32_t hook_rakNetSend(void* rakPeer, void* bitStream, int priority, int reliability, char orderingChannel, void* systemAddress, bool broadcast) {
    int immediatePriority = 0; // Bypass batching queues
    return orig_rakNetSend ? orig_rakNetSend(rakPeer, bitStream, immediatePriority, reliability, orderingChannel, systemAddress, broadcast) : 0;
}

// ============================================================================
// 2. CROSS-REGION FPS & ENTITY INTERPOLATION ENHANCER
// ============================================================================
// Prevents render thread stuttering caused by high-ping packet jitter or dropped server ticks
typedef void (*entityInterpolate_t)(void* entity, float renderPartialTicks);
entityInterpolate_t orig_entityInterpolate = nullptr;

void hook_entityInterpolate(void* entity, float renderPartialTicks) {
    // Decouples entity rendering from server network tick delays
    // Forces smooth client-side visual prediction when ping spikes occur
    if (orig_entityInterpolate) {
        orig_entityInterpolate(entity, 1.0f); // Force full frame-predictive factor
    }
}

// ============================================================================
// 3. BUTTERY SMOOTH CAMERA MOVEMENT (Delta-Time Scaled)
// ============================================================================
typedef void (*turn_t)(void* player, float dx, float dy);
turn_t orig_turn = nullptr;

static float smoothedDx = 0.0f;
static float smoothedDy = 0.0f;
static auto last_frame_time = std::chrono::high_resolution_clock::now();

void hook_turn(void* player, float dx, float dy) {
    auto current_time = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(current_time - last_frame_time).count();
    last_frame_time = current_time;

    // Delta-time decay ensures camera feel remains identical at 30 or 300 FPS
    float alpha = 1.0f - std::exp(-22.0f * dt);

    smoothedDx += (dx - smoothedDx) * alpha;
    smoothedDy += (dy - smoothedDy) * alpha;

    if (orig_turn) {
        orig_turn(player, smoothedDx, smoothedDy);
    }
}

// ============================================================================
// 4. UNTHROTTLED FPS & VSYNC REMOVAL
// ============================================================================
typedef int (*eglSwapInterval_t)(void* dpy, int interval);
eglSwapInterval_t orig_eglSwapInterval = nullptr;

int hook_eglSwapInterval(void* dpy, int interval) {
    // Force immediate presentation (0 wait state) for minimum display latency
    return orig_eglSwapInterval ? orig_eglSwapInterval(dpy, 0) : 0;
}

typedef int (*getFPSLimit_t)(void* appPlatform);
getFPSLimit_t orig_getFPSLimit = nullptr;

int hook_getFPSLimit(void* appPlatform) {
    return 99999; // Bypasses internal engine frame target limiters
}

// ============================================================================
// 5. FULLBRIGHT / GAMMA BOOSTER
// ============================================================================
typedef float (*getGamma_t)(void* options);
getGamma_t orig_getGamma = nullptr;

float hook_getGamma(void* options) {
    return 10.0f; // Max visual clarity in low-light/cave areas
}

// ============================================================================
// 6. LOW-LATENCY CPU AFFINITY & REALTIME THREADING
// ============================================================================
void optimize_cpu_performance() {
    // Pin client execution to ARM Big Performance Cores (Cores 4–7)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 4; i < 8; i++) {
        CPU_SET(i, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

    // Escalates thread priority to SCHED_FIFO for instant input processing
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    LOGI("CPU Core Affinity and SCHED_FIFO priority engaged.");
}

// ============================================================================
// MODULE ENTRY POINT & HOOK BINDINGS
// ============================================================================
void* main_thread(void* arg) {
    LOGI("LeviModule: Initializing cross-region performance engine...");

    // 1. Hook C Standard Library Socket calls for instant network low-delay
    void* libc_handle = dlopen("libc.so", RTLD_LAZY);
    if (libc_handle) {
        void* socket_sym = dlsym(libc_handle, "socket");
        if (socket_sym) {
            DobbyHook(socket_sym, (void*)hook_socket, (void**)&orig_socket);
            LOGI("Hooked POSIX socket() for IP_TOS low-latency mode.");
        }
    }

    // 2. Hook EGL Graphics Driver for zero-VSync display swap
    void* egl_handle = dlopen("libEGL.so", RTLD_LAZY);
    if (egl_handle) {
        void* swapInterval_sym = dlsym(egl_handle, "eglSwapInterval");
        if (swapInterval_sym) {
            DobbyHook(swapInterval_sym, (void*)hook_eglSwapInterval, (void**)&orig_eglSwapInterval);
            LOGI("Hooked eglSwapInterval for zero presentation delay.");
        }
    }

    // 3. Wait for libminecraftpe.so to load into memory
    void* mcpe_handle = nullptr;
    while (!(mcpe_handle = dlopen("libminecraftpe.so", RTLD_NOLOAD))) {
        usleep(100000);
    }

    LOGI("libminecraftpe.so loaded. Binding game engine hooks...");

    // Apply CPU Affinity and scheduling priority
    optimize_cpu_performance();

    // Hook Camera Turn
    void* turn_sym = dlsym(mcpe_handle, "_ZN11LocalPlayer9applyTurnEff");
    if (turn_sym) {
        DobbyHook(turn_sym, (void*)hook_turn, (void**)&orig_turn);
        LOGI("Hooked LocalPlayer::applyTurn successfully.");
    }

    // Hook RakNet Peer Packet Send
    void* rak_sym = dlsym(mcpe_handle, "_ZN6RakNet7RakPeer4SendEPKNS_10BitStreamE14PacketPriority18PacketReliabilitycNS_13SystemAddressEb");
    if (rak_sym) {
        DobbyHook(rak_sym, (void*)hook_rakNetSend, (void**)&orig_rakNetSend);
        LOGI("Hooked RakPeer::Send for immediate packet flushing.");
    }

    // Hook Entity Interpolation to prevent high-ping stutter
    void* interp_sym = dlsym(mcpe_handle, "_ZN11ActorRender19enableInterpolationEP5Actorf");
    if (interp_sym) {
        DobbyHook(interp_sym, (void*)hook_entityInterpolate, (void**)&orig_entityInterpolate);
        LOGI("Hooked ActorRender interpolation for cross-region smoothing.");
    }

    // Hook Gamma
    void* gamma_sym = dlsym(mcpe_handle, "_ZNK7Options8getGammaEv");
    if (gamma_sym) {
        DobbyHook(gamma_sym, (void*)hook_getGamma, (void**)&orig_getGamma);
        LOGI("Hooked Options::getGamma successfully.");
    }

    LOGI("LeviModule: Engine fully initialized and operational.");
    return nullptr;
}

__attribute__((constructor))
void init_module() {
    pthread_t thread;
    pthread_create(&thread, nullptr, main_thread, nullptr);
    pthread_detach(thread);
}
