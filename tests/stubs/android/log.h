// Minimal <android/log.h> for the host test build: LeviBoost only uses the
// log levels and __android_log_print.
#pragma once

#include <cstdarg>
#include <cstdio>

using android_LogPriority = int;

constexpr int ANDROID_LOG_UNKNOWN = 0;
constexpr int ANDROID_LOG_DEFAULT = 1;
constexpr int ANDROID_LOG_VERBOSE = 2;
constexpr int ANDROID_LOG_DEBUG = 3;
constexpr int ANDROID_LOG_INFO = 4;
constexpr int ANDROID_LOG_WARN = 5;
constexpr int ANDROID_LOG_ERROR = 6;
constexpr int ANDROID_LOG_FATAL = 7;
constexpr int ANDROID_LOG_SILENT = 8;

inline int __android_log_print(int priority, const char *tag, const char *format,
                               ...) {
    va_list args;
    va_start(args, format);
    std::fprintf(stderr, "[%s] ", tag ? tag : "?");
    const int written = std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
    va_end(args);
    (void)priority;
    return written;
}
