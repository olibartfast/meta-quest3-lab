#!/usr/bin/env bash

# Shared, pinned Android toolchain configuration.
# shellcheck disable=SC2034  # Constants are consumed by scripts that source this file.
readonly QUEST_JAVA_VERSION="21"
readonly QUEST_GRADLE_JAVA_VERSION="17"
readonly QUEST_COMPILE_SDK="34"
readonly QUEST_BUILD_TOOLS_VERSION="34.0.0"
readonly QUEST_CMAKE_VERSION="3.22.1"
readonly QUEST_NDK_VERSION="29.0.14206865"
readonly QUEST_GRADLE_VERSION="8.5"
readonly QUEST_AGP_VERSION="8.2.0"

quest_sdk_root() {
    if [[ -n "${QUEST_ANDROID_SDK_ROOT:-}" ]]; then
        printf '%s\n' "$QUEST_ANDROID_SDK_ROOT"
    elif [[ -n "${ANDROID_HOME:-}" ]]; then
        printf '%s\n' "$ANDROID_HOME"
    elif [[ -n "${ANDROID_SDK_ROOT:-}" ]]; then
        printf '%s\n' "$ANDROID_SDK_ROOT"
    else
        printf '%s\n' "$HOME/Android"
    fi
}

quest_java_home() {
    if [[ -n "${QUEST_JAVA_HOME:-}" ]]; then
        printf '%s\n' "$QUEST_JAVA_HOME"
    elif [[ -d "/usr/lib/jvm/java-${QUEST_JAVA_VERSION}-openjdk-amd64" ]]; then
        printf '%s\n' "/usr/lib/jvm/java-${QUEST_JAVA_VERSION}-openjdk-amd64"
    elif [[ -n "${JAVA_HOME:-}" ]]; then
        printf '%s\n' "$JAVA_HOME"
    fi
}

quest_gradle_java_home() {
    if [[ -n "${QUEST_GRADLE_JAVA_HOME:-}" ]]; then
        printf '%s\n' "$QUEST_GRADLE_JAVA_HOME"
    elif [[ -d "/usr/lib/jvm/java-${QUEST_GRADLE_JAVA_VERSION}-openjdk-amd64" ]]; then
        printf '%s\n' "/usr/lib/jvm/java-${QUEST_GRADLE_JAVA_VERSION}-openjdk-amd64"
    else
        quest_java_home
    fi
}

quest_export_toolchain() {
    local sdk_root
    local java_home
    local gradle_java_home
    sdk_root="$(quest_sdk_root)"
    java_home="$(quest_java_home)"
    gradle_java_home="$(quest_gradle_java_home)"

    export ANDROID_HOME="$sdk_root"
    export ANDROID_SDK_ROOT="$sdk_root"
    export ANDROID_NDK_HOME="$sdk_root/ndk/$QUEST_NDK_VERSION"
    export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
    if [[ -n "$java_home" ]]; then
        export JAVA_HOME="$java_home"
        export PATH="$JAVA_HOME/bin:$PATH"
    fi
    if [[ -n "$gradle_java_home" ]]; then
        export QUEST_GRADLE_JAVA_HOME="$gradle_java_home"
        if [[ " ${GRADLE_OPTS:-} " != *" -Dorg.gradle.java.home="* ]]; then
            export GRADLE_OPTS="${GRADLE_OPTS:-} -Dorg.gradle.java.home=$gradle_java_home"
        fi
    fi
    export PATH="$sdk_root/platform-tools:$sdk_root/cmdline-tools/latest/bin:$PATH"
}
