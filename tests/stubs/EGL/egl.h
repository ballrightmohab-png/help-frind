// Minimal EGL declarations for the host test build.
//
// The tests run on a desktop machine with no Android EGL headers, so this
// header provides exactly the types and constants LeviBoost uses.  It is only
// ever on the include path for tests/stubs (never for the Android build).
#pragma once

#include <cstddef>
#include <cstdint>

using EGLBoolean = std::uint32_t;
using EGLDisplay = void *;
using EGLConfig = void *;
using EGLContext = void *;
using EGLSurface = void *;
using EGLNativeDisplayType = void *;
using EGLNativePixmapType = void *;
using EGLNativeWindowType = void *;
using EGLint = std::int32_t;
using EGLenum = std::uint32_t;
using EGLClientBuffer = void *;
using EGLImage = void *;

using __eglMustCastToProperFunctionPointerType = void (*)(void);

constexpr EGLBoolean EGL_FALSE = 0;
constexpr EGLBoolean EGL_TRUE = 1;
constexpr EGLint EGL_SUCCESS = 0x3000;
constexpr EGLint EGL_NONE = 0x3038;
constexpr EGLint EGL_WIDTH = 0x3057;
constexpr EGLint EGL_HEIGHT = 0x3056;
constexpr EGLint EGL_SAMPLE_BUFFERS = 0x3032;
constexpr EGLint EGL_SAMPLES = 0x3031;
constexpr EGLint EGL_BUFFER_SIZE = 0x3020;
constexpr EGLint EGL_DEPTH_SIZE = 0x3025;
constexpr EGLint EGL_STENCIL_SIZE = 0x3026;
constexpr EGLint EGL_DONT_CARE = -1;
constexpr EGLContext EGL_NO_CONTEXT = nullptr;
constexpr EGLDisplay EGL_NO_DISPLAY = nullptr;
constexpr EGLSurface EGL_NO_SURFACE = nullptr;
