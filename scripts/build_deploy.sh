#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
android_project="$repo_root/XrPassthrough/Projects/Android"
apk_path="$android_project/build/outputs/apk/debug/XrPassthrough-debug.apk"
application_id="com.oculus.xrpassthrough"
activity="com.oculus.NativeActivity"
build_only=false

if [[ "${1:-}" == "--build-only" ]]; then
    build_only=true
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--build-only]" >&2
    exit 2
fi

if [[ -n "${QUEST_ANDROID_SDK_ROOT:-}" ]]; then
    sdk_root="$QUEST_ANDROID_SDK_ROOT"
elif [[ -n "${ANDROID_HOME:-}" ]]; then
    sdk_root="$ANDROID_HOME"
elif [[ -n "${ANDROID_SDK_ROOT:-}" ]]; then
    sdk_root="$ANDROID_SDK_ROOT"
elif [[ -d "$HOME/Android/Sdk" ]]; then
    sdk_root="$HOME/Android/Sdk"
elif [[ -d "$HOME/Android" ]]; then
    sdk_root="$HOME/Android"
else
    echo "Android SDK not found. Set QUEST_ANDROID_SDK_ROOT or ANDROID_HOME." >&2
    exit 1
fi

if [[ ! -d "$sdk_root" ]]; then
    echo "Android SDK directory does not exist: $sdk_root" >&2
    exit 1
fi

# AGP rejects conflicting SDK variables. ANDROID_HOME is the canonical value;
# keep ANDROID_SDK_ROOT aligned for tools that still read the legacy variable.
export ANDROID_HOME="$sdk_root"
export ANDROID_SDK_ROOT="$sdk_root"
export PATH="$sdk_root/platform-tools:$PATH"

echo "Building XrPassthrough with Android SDK: $sdk_root"
(
    cd "$android_project"
    ./gradlew assembleDebug
)

echo "APK: $apk_path"
if [[ "$build_only" == true ]]; then
    exit 0
fi

if ! command -v adb >/dev/null 2>&1; then
    echo "adb not found. Install Android platform-tools or use --build-only." >&2
    exit 1
fi

device_count="$(adb devices | awk '$2 == "device" { count++ } END { print count + 0 }')"
if [[ "$device_count" -ne 1 ]]; then
    echo "Expected exactly one authorized device; found $device_count." >&2
    echo "Check 'adb devices -l' or use --build-only." >&2
    exit 1
fi

adb install -r "$apk_path"
adb shell am start -n "$application_id/$activity"

echo "Application launched. Inspect logs with:"
echo "  adb logcat -s XrPassthrough:V OpenXR:V '*:E'"
