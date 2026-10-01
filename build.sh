#!/usr/bin/env bash
#
# LeviBoost - Android build script.
#
# Builds libleviboost.so for arm64-v8a with the Android NDK, runs the host
# test suite, and packages dist/LeviBoost.levipack.
#
# Requirements:
#   * Android NDK r26b or newer (r27+ recommended for 16 KB page support)
#   * CMake 3.22+ and Ninja
#
# Usage:
#   ANDROID_NDK_HOME=~/Android/Sdk/ndk/27.0.12077973 ./build.sh
#   ./build.sh --skip-tests --api 28
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
DIST_DIR="${REPO_ROOT}/dist"
API_LEVEL="${ANDROID_API_LEVEL:-28}"
ABI="${ANDROID_ABI:-arm64-v8a}"
SKIP_TESTS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --api) API_LEVEL="$2"; shift 2 ;;
        --abi) ABI="$2"; shift 2 ;;
        --skip-tests) SKIP_TESTS=1; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    for candidate in \
        "${ANDROID_NDK_ROOT:-}" \
        "${ANDROID_HOME:-}/ndk/"* \
        "$HOME/Android/Sdk/ndk/"* \
        "/opt/android-sdk/ndk/"*; do
        if [[ -n "$candidate" && -f "$candidate/build/cmake/android.toolchain.cmake" ]]; then
            ANDROID_NDK_HOME="$candidate"
            break
        fi
    done
fi

if [[ -z "${ANDROID_NDK_HOME:-}" || ! -f "${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" ]]; then
    echo "error: set ANDROID_NDK_HOME to an Android NDK (r26b or newer)" >&2
    exit 1
fi

CMAKE="${CMAKE:-cmake}"
command -v "$CMAKE" >/dev/null || { echo "error: cmake not found" >&2; exit 1; }
command -v ninja >/dev/null || { echo "error: ninja not found" >&2; exit 1; }

echo "==> host tests"
if [[ "$SKIP_TESTS" -eq 0 ]]; then
    "${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter \
        -I"${REPO_ROOT}/src" -I"${REPO_ROOT}/tests/stubs" \
        "${REPO_ROOT}/tests/optimizations_test.cpp" \
        "${REPO_ROOT}/src/Optimizations.cpp" \
        "${REPO_ROOT}/src/Settings.cpp" \
        -o "${BUILD_DIR}/leviboost_tests"
    "${BUILD_DIR}/leviboost_tests"
fi

echo "==> android build (${ABI}, api ${API_LEVEL})"
"$CMAKE" -S "$REPO_ROOT" -B "$BUILD_DIR/android" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-${API_LEVEL}" \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-Wl,-z,max-page-size=16384"
"$CMAKE" --build "$BUILD_DIR/android" --target leviboost

LIBRARY="${BUILD_DIR}/android/out/${ABI}/libleviboost.so"
[[ -f "$LIBRARY" ]] || { echo "error: $LIBRARY was not produced" >&2; exit 1; }

echo "==> package"
python3 "${REPO_ROOT}/scripts/package_levipack.py" \
    --library "$LIBRARY" \
    --output "${DIST_DIR}/LeviBoost.levipack"
python3 "${REPO_ROOT}/scripts/verify_levipack.py" "${DIST_DIR}/LeviBoost.levipack"

echo
echo "done: ${DIST_DIR}/LeviBoost.levipack"
echo "copy it to your device and install it from the LeviLaunchroid mod manager."
