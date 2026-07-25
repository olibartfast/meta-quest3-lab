#!/usr/bin/env bash

set -uo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/toolchain_config.sh
source "$script_dir/toolchain_config.sh"

strict=false
if [[ "${1:-}" == "--strict" ]]; then
    strict=true
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--strict]" >&2
    exit 2
fi

quest_export_toolchain
sdk_root="$ANDROID_HOME"
ndk_root="$sdk_root/ndk/$QUEST_NDK_VERSION"
failures=0

report_path() {
    local label="$1"
    local path="$2"
    printf '%-22s %s\n' "$label:" "$path"
}

require_path() {
    local label="$1"
    local path="$2"
    report_path "$label" "$path"
    if [[ ! -e "$path" ]]; then
        echo "  ERROR: required path is missing" >&2
        failures=$((failures + 1))
    fi
}

require_version() {
    local label="$1"
    local expected="$2"
    local actual="$3"
    printf '%-22s %s (expected %s)\n' "$label:" "${actual:-not found}" "$expected"
    if [[ "$actual" != "$expected" ]]; then
        echo "  ERROR: $label version mismatch" >&2
        failures=$((failures + 1))
    fi
}

java_version="$(java -version 2>&1 | sed -n '1s/.*version "\([0-9]*\).*/\1/p')"
cmake_version=""
if [[ -x "$sdk_root/cmake/$QUEST_CMAKE_VERSION/bin/cmake" ]]; then
    cmake_version="$("$sdk_root/cmake/$QUEST_CMAKE_VERSION/bin/cmake" --version | sed -n '1s/^cmake version //p')"
    cmake_version="${cmake_version%%-*}"
fi
ndk_version=""
if [[ -f "$ndk_root/source.properties" ]]; then
    ndk_version="$(sed -n 's/^Pkg.Revision[[:space:]]*=[[:space:]]*//p' "$ndk_root/source.properties")"
fi
gradle_version="$(sed -n 's|^distributionUrl=.*/gradle-\([^-]*\)-.*|\1|p' \
    "$repo_root/XrPassthrough/Projects/Android/gradle/wrapper/gradle-wrapper.properties")"
agp_version="$(sed -n 's/.*com\.android\.application.*version "\([^"]*\)".*/\1/p' \
    "$repo_root/build.gradle")"
ninja_version="$(ninja --version 2>/dev/null || true)"
adb_version="$("$sdk_root/platform-tools/adb" version 2>/dev/null | sed -n '1s/^Android Debug Bridge version //p')"
sdkmanager_version="$("$sdk_root/cmdline-tools/latest/bin/sdkmanager" --version 2>/dev/null | sed -n '1p')"

report_path "Repository" "$repo_root"
report_path "Android SDK" "$sdk_root"
report_path "JAVA_HOME" "${JAVA_HOME:-not found}"
require_version "Java" "$QUEST_JAVA_VERSION" "$java_version"
require_path "SDK platform" "$sdk_root/platforms/android-$QUEST_COMPILE_SDK"
require_path "Build tools" "$sdk_root/build-tools/$QUEST_BUILD_TOOLS_VERSION"
require_version "CMake" "$QUEST_CMAKE_VERSION" "$cmake_version"
require_version "NDK" "$QUEST_NDK_VERSION" "$ndk_version"
require_version "Gradle wrapper" "$QUEST_GRADLE_VERSION" "$gradle_version"
require_version "Android Gradle plugin" "$QUEST_AGP_VERSION" "$agp_version"
require_path "ADB" "$sdk_root/platform-tools/adb"
require_path "sdkmanager" "$sdk_root/cmdline-tools/latest/bin/sdkmanager"
report_path "ADB version" "${adb_version:-not found}"
report_path "sdkmanager version" "${sdkmanager_version:-not found}"
report_path "Ninja version" "${ninja_version:-not found}"

if [[ "$strict" == true && "$failures" -ne 0 ]]; then
    echo "$failures toolchain requirement(s) not satisfied." >&2
    exit 1
fi

if [[ "$failures" -eq 0 ]]; then
    echo "Toolchain matches all repository pins."
else
    echo "$failures toolchain requirement(s) not satisfied (run with --strict to fail)."
fi
