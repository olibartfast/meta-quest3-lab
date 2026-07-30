#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/toolchain_config.sh
source "$script_dir/toolchain_config.sh"

sudo apt-get update
sudo apt-get install -y \
    adb \
    android-sdk-platform-tools-common \
    build-essential \
    git \
    ninja-build \
    openjdk-17-jdk \
    openjdk-21-jdk \
    unzip \
    wget

sdk_root="$(quest_sdk_root)"
command_line_tools_version="11076708"
command_line_tools_url="https://dl.google.com/android/repository/commandlinetools-linux-${command_line_tools_version}_latest.zip"
archive="$(mktemp --suffix=.zip)"
extract_dir="$(mktemp -d)"
trap 'rm -f "$archive"; rm -rf "$extract_dir"' EXIT

mkdir -p "$sdk_root/cmdline-tools"
if [[ ! -x "$sdk_root/cmdline-tools/latest/bin/sdkmanager" ]]; then
    wget -O "$archive" "$command_line_tools_url"
    unzip -q "$archive" -d "$extract_dir"
    mv "$extract_dir/cmdline-tools" "$sdk_root/cmdline-tools/latest"
fi

quest_export_toolchain
yes | sdkmanager --sdk_root="$sdk_root" --licenses >/dev/null || true
sdkmanager --sdk_root="$sdk_root" \
    "build-tools;$QUEST_BUILD_TOOLS_VERSION" \
    "cmake;$QUEST_CMAKE_VERSION" \
    "ndk;$QUEST_NDK_VERSION" \
    "platform-tools" \
    "platforms;android-$QUEST_COMPILE_SDK"

profile_block="$(cat <<EOF
# Quest native development (managed by meta-quest3-lab)
export QUEST_ANDROID_SDK_ROOT=\"$sdk_root\"
export ANDROID_HOME=\"\$QUEST_ANDROID_SDK_ROOT\"
export ANDROID_SDK_ROOT=\"\$QUEST_ANDROID_SDK_ROOT\"
export ANDROID_NDK_HOME=\"\$QUEST_ANDROID_SDK_ROOT/ndk/$QUEST_NDK_VERSION\"
export ANDROID_NDK_ROOT=\"\$ANDROID_NDK_HOME\"
export JAVA_HOME=\"/usr/lib/jvm/java-${QUEST_JAVA_VERSION}-openjdk-amd64\"
export QUEST_GRADLE_JAVA_HOME=\"/usr/lib/jvm/java-${QUEST_GRADLE_JAVA_VERSION}-openjdk-amd64\"
export PATH=\"\$ANDROID_SDK_ROOT/platform-tools:\$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:\$PATH\"
EOF
)"

profile_file="$HOME/.bashrc"
if ! grep -Fq "managed by meta-quest3-lab" "$profile_file"; then
    printf '\n%s\n' "$profile_block" >> "$profile_file"
fi

echo "Pinned Quest toolchain installed in $sdk_root"
"$script_dir/print_toolchain_config.sh" --strict
