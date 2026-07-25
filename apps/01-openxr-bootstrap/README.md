# Native OpenXR Bootstrap

This application demonstrates the smallest repository-owned Android/OpenXR lifecycle. It uses `NativeActivity`, initializes the Android OpenXR loader, creates an HMD instance and session, creates a `LOCAL` reference space, handles every session state, and submits empty frames while the session is running.

The application intentionally renders nothing. A black compositor view is the expected result for Milestone 1.

## Required OpenXR extensions

- `XR_KHR_android_create_instance`
- `XR_KHR_vulkan_enable2`

No Meta/vendor extension is enabled.

## Build and deploy

From the repository root:

```bash
./scripts/print_toolchain_config.sh --strict
./scripts/build_deploy.sh --app 01-openxr-bootstrap --build-only
./scripts/build_deploy.sh --app 01-openxr-bootstrap
```

The deployment command installs and launches `com.olibartfast.questlab.openxrbootstrap/android.app.NativeActivity`. Keep the headset awake and controllers active.

Inspect only relevant logs:

```bash
adb logcat -c
adb logcat -s OpenXRBootstrap:V OpenXR:V '*:S'
```

Exit from the headset system UI or force a test shutdown with:

```bash
adb shell am force-stop com.olibartfast.questlab.openxrbootstrap
```

Successful logs include runtime and system information, extension enumeration, Vulkan device creation, a `READY` transition followed by `FOCUSED`, session begin, `LOCAL` space creation, and reverse-order destruction.

For device acceptance, perform three launch/exit cycles and verify each process terminates without OpenXR/Vulkan errors or leaked-handle warnings. Then build and launch the legacy regression target:

```bash
./scripts/build_deploy.sh --app xrpassthrough
```

## Known limitations

- Frames contain zero composition layers; no image is visible.
- There are no swapchains, command pools, shaders, geometry, actions, input, passthrough, or vendor extensions.
- Only `PRIMARY_STEREO`, `LOCAL` space, ARM64, and Vulkan 1.1 are used.
- Headset validation is required; a desktop build cannot validate the Android/OpenXR runtime lifecycle.
