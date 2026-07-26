# Vulkan Stereo Triangle

This application renders a repository-owned triangle into two OpenXR color
swapchains with Vulkan. Each frame uses the runtime's predicted display time,
locates the left and right views in `LOCAL` space, renders the same scene from
both eye poses, and submits one projection layer.

The expected headset view is a red/green/blue triangle approximately two metres
in front of the initial head position, against a dark background. It should
appear as one stable stereoscopic object while the head rotates and translates.

## Required OpenXR extensions

- `XR_KHR_android_create_instance`
- `XR_KHR_vulkan_enable2`

No Meta/vendor extension is enabled. The first implementation deliberately uses
one color swapchain per eye instead of Vulkan multiview.

## Build and deploy

From the repository root:

```bash
./scripts/print_toolchain_config.sh --strict
./scripts/build_deploy.sh --app 02-vulkan-stereo-triangle --build-only
./scripts/build_deploy.sh --app 02-vulkan-stereo-triangle
```

The deployment command installs and launches
`com.olibartfast.questlab.vulkanstereotriangle/android.app.NativeActivity`.
Keep the headset awake and controllers active.

Inspect relevant logs:

```bash
adb logcat -c
adb logcat -s VulkanStereoTriangle:V OpenXR:V '*:S'
```

Successful initialization logs two recommended stereo view extents, an sRGB
swapchain format, two eye swapchains and their image counts, renderer creation,
and the normal `READY` through `FOCUSED` session transitions.

Exit through the headset system menu. A clean exit logs destruction of the
renderer, both swapchains, the `LOCAL` space, session, Vulkan device/instance,
and OpenXR instance.

## Optional Vulkan validation

Validation is opt-in and the validation layer is not bundled:

```bash
./scripts/build_deploy.sh \
  --app 02-vulkan-stereo-triangle \
  --vulkan-validation
```

If `VK_LAYER_KHRONOS_validation` and `VK_EXT_debug_utils` are discoverable on
the device, the app enables them and forwards messages to Logcat. Otherwise it
logs one warning and continues normally.

## Device acceptance

Perform three launch/exit cycles and one pause/resume cycle. Verify:

- the triangle is visible to both eyes as one stable object;
- moving the head produces correct perspective and parallax;
- no OpenXR call-order, Vulkan synchronization, validation, or leaked-handle
  errors appear;
- the process terminates after system Quit.

Then regression-build and launch:

```bash
./scripts/build_deploy.sh --app 01-openxr-bootstrap
./scripts/build_deploy.sh --app xrpassthrough
```

## Known limitations

- There is no depth buffer, MSAA, texture, vertex buffer, input, passthrough,
  foveation, or multiview path.
- Rendering waits on a fence before releasing each image. This favors explicit
  correctness over a multi-frame GPU pipeline.
- The small matrix helpers are renderer-internal; reusable spatial math remains
  Milestone 3 work.
- Headset acceptance cannot be replaced by a desktop or CI build.
