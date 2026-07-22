# Development Environment

## Current Application

`XrPassthrough` is the repository's working native Android application. It already creates an OpenXR instance and session, handles lifecycle events, creates `VIEW`, `LOCAL`, and `STAGE` spaces, submits stereo frames, reads controller actions, and supports `XR_FB_passthrough`. Its renderer is OpenGL ES; the Vulkan renderer described in the roadmap is not implemented yet.

## Inspect the Toolchain

From the repository root, run:

```bash
./scripts/print_toolchain_config.sh
```

The command reports the repository path, Android SDK and NDK paths, NDK and Gradle wrapper versions, and the resolved Java, CMake, Ninja, ADB, and `sdkmanager` executables. Resolve conflicting `ANDROID_HOME` and `ANDROID_SDK_ROOT` values before invoking Gradle directly.

## Build

Use the repository-owned wrapper command:

```bash
./scripts/build_deploy.sh --build-only
```

This normalizes both Android SDK variables to one path and runs `./gradlew assembleDebug`. SDK selection uses `QUEST_ANDROID_SDK_ROOT`, then `ANDROID_HOME`, then `ANDROID_SDK_ROOT`, followed by common user-local SDK directories. The APK is written to:

```text
XrPassthrough/Projects/Android/build/outputs/apk/debug/XrPassthrough-debug.apk
```

## Connect and Deploy

Enable Developer Mode and USB debugging on the Quest, connect it over USB, accept the authorization prompt in the headset, then verify exactly one device is available:

```bash
adb devices -l
./scripts/build_deploy.sh
```

The script builds, installs with `adb install -r`, and launches `com.oculus.xrpassthrough/com.oculus.NativeActivity`. Keep the headset awake and the controllers active during launch. Inspect runtime output with:

```bash
adb logcat -s XrPassthrough:V OpenXR:V '*:S'
```

## Power During Development

A computer USB port can maintain an ADB data connection while supplying less power than an active Quest 3 consumes. Passthrough, tracking, displays, CPU, and GPU workloads may therefore drain the battery slowly even while Android reports that it is charging.

Inspect the charging state with:

```bash
adb shell dumpsys battery
```

For long sessions, prefer a reputable USB-C Power Delivery charger rated for at least 18 W; a 30 W unit provides useful headroom. Use a USB-C cable rated for at least 60 W, long enough to avoid pulling on the headset connector. For simultaneous wired ADB and external power, use a Quest-compatible data cable with a separate power-injection port. Do not assume that a generic hub's PD input powers its downstream data port.

The simplest development setup is wireless ADB with the headset connected directly to its charger:

```bash
# Run while USB is still connected; obtain the wlan0 address from the output.
adb tcpip 5555
adb shell ip route

# Disconnect USB, then connect over the same trusted local network.
adb connect <QUEST_IP>:5555
adb devices -l
```

Return ADB to USB mode later with `adb usb`. Avoid exposing TCP port 5555 on an untrusted network.

## Troubleshooting

- **Different SDK paths:** set `QUEST_ANDROID_SDK_ROOT` to the intended SDK. The build script aligns both Android SDK variables.
- **No authorized device:** run `adb kill-server`, reconnect the headset, and accept its USB debugging prompt.
- **Linux USB permissions:** run `./scripts/udev_env_setup.sh`, then log out and back in after group changes.
- **Battery drains over USB:** use the power guidance above, close the XR application between tests, and let the headset cool if its reported temperature remains elevated.
- **Launch requires controllers:** wake both controllers and put on the headset before launching the NativeActivity.
- **Java native-access warnings:** the current Gradle wrapper can build despite these warnings on newer Java releases; Java 21 remains the intended setup version.
- **NDK selection:** Gradle currently selects an installed SDK NDK automatically. Pinning and validating one repository-wide NDK version remains Milestone 0 work.

## Verified Baseline

On 2026-07-22, the repository successfully built the debug APK, detected an authorized Quest 3 over ADB, installed the ARM64 package, and launched its native activity. Capturing a clean lifecycle log from a fresh headset session remains part of Milestone 0 validation.
