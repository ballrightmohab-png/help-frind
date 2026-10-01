cmake_minimum_required(VERSION 3.22)

project(leviboost LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LEVIBOOST_BUILD_TESTS "Build the host optimization tests" OFF)

# ---------------------------------------------------------------------------
# Host build: only the test suite, which needs no Android toolchain.
# ---------------------------------------------------------------------------
if(NOT ANDROID)
    if(NOT LEVIBOOST_BUILD_TESTS)
        message(FATAL_ERROR
            "LeviBoost targets Android. Configure with the Android NDK "
            "toolchain file (see build.sh / .github/workflows/build.yml), or "
            "pass -DLEVIBOOST_BUILD_TESTS=ON to build the host test suite.")
    endif()

    enable_testing()
    add_executable(leviboost_tests
        tests/optimizations_test.cpp
        src/Optimizations.cpp
        src/Settings.cpp)
    target_include_directories(leviboost_tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${CMAKE_CURRENT_LIST_DIR}/tests/stubs")
    target_compile_options(leviboost_tests PRIVATE -Wall -Wextra)
    add_test(NAME leviboost_tests COMMAND leviboost_tests)
    return()
endif()

# ---------------------------------------------------------------------------
# Preloader SDK headers
#
# By default the bundled preloader_headers/ shim is used, which keeps the build
# hermetic (no network access, no submodules) and pins the exact API surface
# the mod was written against.  Pass -DLEVIBOOST_PRELOADER_ROOT=<checkout> to
# build against a fresh https://github.com/LiteLDev/preloader-android instead.
# ---------------------------------------------------------------------------
set(LEVIBOOST_PRELOADER_ROOT "" CACHE PATH
    "Optional path to a LiteLDev/preloader-android checkout")

if(LEVIBOOST_PRELOADER_ROOT AND EXISTS "${LEVIBOOST_PRELOADER_ROOT}/include/pl/Mod.hpp")
    set(LEVIBOOST_PL_INCLUDE "${LEVIBOOST_PRELOADER_ROOT}/include")
    message(STATUS "LeviBoost: using external preloader headers from ${LEVIBOOST_PL_INCLUDE}")
else()
    set(LEVIBOOST_PL_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/preloader_headers")
    message(STATUS "LeviBoost: using bundled preloader headers from ${LEVIBOOST_PL_INCLUDE}")
endif()

if(NOT EXISTS "${LEVIBOOST_PL_INCLUDE}/pl/ModMenu.hpp")
    message(FATAL_ERROR "Missing pl/ModMenu.hpp under ${LEVIBOOST_PL_INCLUDE}")
endif()

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
if(ANDROID_ABI)
    set(LEVIBOOST_OUTPUT_ABI "${ANDROID_ABI}")
else()
    set(LEVIBOOST_OUTPUT_ABI "android")
endif()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${LEVIBOOST_OUTPUT_ABI}")

add_library(leviboost SHARED
    src/Main.cpp
    src/Optimizations.cpp
    src/Settings.cpp
    src/Hud.cpp
    src/GlApi.cpp)

set_target_properties(leviboost PROPERTIES
    OUTPUT_NAME "leviboost"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON)

target_include_directories(leviboost PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${LEVIBOOST_PL_INCLUDE}")

# Android logs are written through __android_log_print (liblog).
find_library(LEVIBOOST_LOG_LIB log)
if(LEVIBOOST_LOG_LIB)
    target_link_libraries(leviboost PRIVATE "${LEVIBOOST_LOG_LIB}")
else()
    target_link_libraries(leviboost PRIVATE log)
endif()

# dlopen/dlsym for the GL bridge (libdl is part of libc on modern API levels).
find_library(LEVIBOOST_DL_LIB dl)
if(LEVIBOOST_DL_LIB)
    target_link_libraries(leviboost PRIVATE "${LEVIBOOST_DL_LIB}")
endif()

# The mod does not throw; turning exceptions and RTTI off keeps the binary
# small and matches the rest of the SDK-driven mods.
target_compile_options(leviboost PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:
        -Wall -Wextra
        -fvisibility=hidden -fvisibility-inlines-hidden
        -fno-exceptions -fno-rtti>)

# Keep the shipped library lean: drop debug sections and the symbols of any
# static library that ends up linked in.  Neither affects the runtime, but the
# .levipack is what users download.
target_link_options(leviboost PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wl,--strip-debug -Wl,--exclude-libs,ALL>)

# pl:: symbols are resolved at runtime from the libpreloader.so the launcher
# already has loaded (RTLD_GLOBAL).  Link against the preloader target when it
# is available so the dependency is explicit, otherwise leave the symbols
# undefined and let the dynamic linker resolve them at dlopen() time.
if(TARGET preloader)
    target_link_libraries(leviboost PRIVATE preloader)
else()
    target_link_options(leviboost PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wl,--unresolved-symbols=ignore-all>)
endif()

# libc++ is shared inside the launcher process (ANDROID_STL=c++_shared), so the
# mod has to use the same C++ runtime.  Linking it explicitly makes the
# DT_NEEDED entry show up even when the toolchain does not add it.
target_compile_options(leviboost PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-stdlib=libc++>)
target_link_options(leviboost PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-stdlib=libc++>)
if(NOT DEFINED ANDROID_STL OR ANDROID_STL STREQUAL "c++_shared")
    target_link_libraries(leviboost PRIVATE c++_shared)
endif()
