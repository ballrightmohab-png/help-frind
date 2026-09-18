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

#ifndef SO_BUSY_POLL
#define SO_BUSY_POLL 46
#endif

// ============================================================================
// 1. Cross-Region Socket Latency Optimization
// ============================================================================
typedef int (*socket_t)(int domain, int type, int protocol);
socket_t orig_socket = nullptr;

int hook_socket(int domain, int type, int protocol) {
    int fd = orig_socket ? orig_socket(domain, type, protocol) : -1;
    if (fd >= 0 && (domain == AF_INET || domain == AF_INET6)) {
        int lowdelay = 0x10; // IP_TOS Low Delay
        setsockopt(fd, IPPROTO_IP, IP_TOS, &lowdelay, sizeof(lowdelay));

        int priority = 6; // High Socket Priority
        setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));

        int busy_poll = 50; // Kernel Busy Polling (us)
        setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll));
    }
    return fd;
}

// ============================================================================
// 2. RakNet Queue Bypass (Immediate Packet Dispatch)
// ============================================================================
typedef uint32_t (*rakNetSend_t)(void* rakPeer, void* bitStream, int priority, int reliability, char orderingChannel, void* systemAddress, bool broadcast);
rakNetSend_t orig_rakNetSend = nullptr;

uint32_t hook_rakNetSend(void* rakPeer, void* bitStream, int priority, int reliability, char orderingChannel, void* systemAddress, bool broadcast) {
    // Override to IMMEDIATE_PRIORITY (0) to strip 10ms-20ms batching buffers
    return orig_rakNetSend ? orig_rakNetSend(rakPeer, bitStream, 0, reliability, orderingChannel, systemAddress, broadcast) : 0;
}

// ============================================================================
// 3. Entity Predictive Visual Interpolation
// ============================================================================
typedef void (*entityInterpolate_t)(void* entity, float renderPartialTicks);
entityInterpolate_t orig_entityInterpolate = nullptr;

void hook_entityInterpolate(void* entity, float renderPartialTicks) {
    if (orig_entityInterpolate) {
        orig_entityInterpolate(entity, 1.0f); // Force smooth visual tick prediction
    }
}

// ============================================================================
// 4. Smooth Camera Rotation (Delta-Time Decay)
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

    float alpha = 1.0f - std::exp(-22.0f * dt);
    smoothedDx += (dx - smoothedDx) * alpha;
    smoothedDy += (dy - smoothedDy) * alpha;

    if (orig_turn) {
        orig_turn(player, smoothedDx, smoothedDy);
    }
}

// ============================================================================
// 5. Zero-VSync & Unthrottled Frame Pacing
// ============================================================================
typedef int (*eglSwapInterval_t)(void* dpy, int interval);
eglSwapInterval_t orig_eglSwapInterval = nullptr;

int hook_eglSwapInterval(void* dpy, int interval) {
    return orig_eglSwapInterval ? orig_eglSwapInterval(dpy, 0) : 0;
}

typedef int (*getFPSLimit_t)(void* appPlatform);
getFPSLimit_t orig_getFPSLimit = nullptr;

int hook_getFPSLimit(void* appPlatform) {
    return 99999;
}

// ============================================================================
// 6. Fullbright Gamma Booster
// ============================================================================
typedef float (*getGamma_t)(void* options);
getGamma_t orig_getGamma = nullptr;

float hook_getGamma(void* options) {
    return 10.0f;
}

// ============================================================================
// 7. CPU Scheduling & Performance Optimization
// ============================================================================
void optimize_cpu_performance() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 4; i < 8; i++) {
        CPU_SET(i, &cpuset);
    }
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    LOGI("CPU Core Affinity and SCHED_FIFO real-time priorities set.");
}

// ============================================================================
// Main Injector Thread
// ============================================================================
void* main_thread(void* arg) {
    LOGI("LeviModule: Initializing native hooks...");

    void* libc_handle = dlopen("libc.so", RTLD_LAZY);
    if (libc_handle) {
        void* socket_sym = dlsym(libc_handle, "socket");
        if (socket_sym) DobbyHook(socket_sym, (void*)hook_socket, (void**)&orig_socket);
    }

    void* egl_handle = dlopen("libEGL.so", RTLD_LAZY);
    if (egl_handle) {
        void* swapInterval_sym = dlsym(egl_handle, "eglSwapInterval");
        if (swapInterval_sym) DobbyHook(swapInterval_sym, (void*)hook_eglSwapInterval, (void**)&orig_eglSwapInterval);
    }

    void* mcpe_handle = nullptr;
    while (!(mcpe_handle = dlopen("libminecraftpe.so", RTLD_NOLOAD))) {
        usleep(100000);
    }

    LOGI("libminecraftpe.so detected. Applying memory optimizations...");
    optimize_cpu_performance();

    void* turn_sym = dlsym(mcpe_handle, "_ZN11LocalPlayer9applyTurnEff");
    if (turn_sym) DobbyHook(turn_sym, (void*)hook_turn, (void**)&orig_turn);

    void* rak_sym = dlsym(mcpe_handle, "_ZN6RakNet7RakPeer4SendEPKNS_10BitStreamE14PacketPriority18PacketReliabilitycNS_13SystemAddressEb");
    if (rak_sym) DobbyHook(rak_sym, (void*)hook_rakNetSend, (void**)&orig_rakNetSend);

    void* interp_sym = dlsym(mcpe_handle, "_ZN11ActorRender19enableInterpolationEP5Actorf");
    if (interp_sym) DobbyHook(interp_sym, (void*)hook_entityInterpolate, (void**)&orig_entityInterpolate);

    void* gamma_sym = dlsym(mcpe_handle, "_ZNK7Options8getGammaEv");
    if (gamma_sym) DobbyHook(gamma_sym, (void*)hook_getGamma, (void**)&orig_getGamma);

    LOGI("LeviModule: All hooks injected successfully.");
    return nullptr;
}

__attribute__((constructor))
void init_module() {
    pthread_t thread;
    pthread_create(&thread, nullptr, main_thread, nullptr);
    pthread_detach(thread);
}
