#!/usr/bin/env bash

set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

find_sdk_root() {
    if [[ -n "${ANDROID_SDK_ROOT:-}" ]]; then
        printf '%s\n' "$ANDROID_SDK_ROOT"
    elif [[ -n "${ANDROID_HOME:-}" ]]; then
        printf '%s\n' "$ANDROID_HOME"
    elif [[ -d "$HOME/Android/Sdk" ]]; then
        printf '%s\n' "$HOME/Android/Sdk"
    elif [[ -d "$HOME/Android" ]]; then
        printf '%s\n' "$HOME/Android"
    elif [[ -d "$HOME/android-sdk" ]]; then
        printf '%s\n' "$HOME/android-sdk"
    fi
}

find_ndk_root() {
    local sdk_root="$1"
    local ndk_build_path
    local latest_ndk

    if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
        printf '%s\n' "$ANDROID_NDK_HOME"
    elif [[ -n "${ANDROID_NDK_ROOT:-}" ]]; then
        printf '%s\n' "$ANDROID_NDK_ROOT"
    elif [[ -n "$sdk_root" && -d "$sdk_root/ndk" ]]; then
        latest_ndk="$(find "$sdk_root/ndk" -mindepth 1 -maxdepth 1 -type d -print | sort -V | tail -n 1)"
        printf '%s\n' "$latest_ndk"
    elif ndk_build_path="$(command -v ndk-build 2>/dev/null)"; then
        dirname "$ndk_build_path"
    fi
}

print_tool() {
    local label="$1"
    local executable="$2"
    shift 2

    if command -v "$executable" >/dev/null 2>&1; then
        printf '%-12s %s\n' "$label path:" "$(command -v "$executable")"
        printf '%-12s %s\n' "$label version:" "$($executable "$@" 2>&1 | head -n 1)"
    else
        printf '%-12s %s\n' "$label path:" "not found"
    fi
}

sdk_root="$(find_sdk_root)"
ndk_root="$(find_ndk_root "$sdk_root")"
gradle_properties="$repo_root/XrPassthrough/Projects/Android/gradle/wrapper/gradle-wrapper.properties"

printf '%-18s %s\n' "Repository:" "$repo_root"
printf '%-18s %s\n' "ANDROID_SDK_ROOT:" "${sdk_root:-not found}"
printf '%-18s %s\n' "ANDROID_NDK_ROOT:" "${ndk_root:-not found}"

if [[ -n "$ndk_root" && -f "$ndk_root/source.properties" ]]; then
    printf '%-18s %s\n' "NDK version:" "$(sed -n 's/^Pkg.Revision[[:space:]]*=[[:space:]]*//p' "$ndk_root/source.properties")"
else
    printf '%-18s %s\n' "NDK version:" "unknown"
fi

if [[ -f "$gradle_properties" ]]; then
    printf '%-18s %s\n' "Gradle wrapper:" "$(sed -n 's|^distributionUrl=.*/gradle-\([^-]*\)-.*|\1|p' "$gradle_properties")"
fi

print_tool "Java" java -version
print_tool "CMake" cmake --version
print_tool "Ninja" ninja --version
print_tool "ADB" adb version
print_tool "sdkmanager" sdkmanager --version
