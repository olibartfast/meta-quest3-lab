# Native OpenXR Passthrough

Milestone 5 uses `XR_FB_passthrough` to place one system-composited
reconstruction layer behind repository-rendered Vulkan content.

## What the API provides

The application receives a passthrough feature and layer handle. It never
receives camera pixels, images, or video, and therefore requests no Camera2
permission. `XR_FB_triangle_mesh`, projected passthrough, styling, color maps,
depth occlusion, and camera access are outside this milestone.

The Android manifest must declare:

```xml
<uses-feature
    android:name="com.oculus.feature.PASSTHROUGH"
    android:required="true" />
```

The instance enables `XR_FB_passthrough`, and the Meta backend queries
`XrSystemPassthroughProperties2FB` when extension revision 3 or newer is
reported. It falls back to `XrSystemPassthroughPropertiesFB` for older
revisions.

## Architecture

The portable `XrPassthroughInterface` combines two generic hooks:

- `XrEventObserver` receives OpenXR events during `PollEvents`.
- `XrUnderlayProvider` appends stable composition-layer pointers during
  `PumpFrame`.

`MetaPassthroughFB` implements those hooks in `libs/xr_meta_passthrough`.
Apps that do not pass either hook retain the original single-projection-layer
behavior.

## Layer ordering and alpha

Every visible mixed-reality frame submits:

```text
1. XrCompositionLayerPassthroughFB
2. XrCompositionLayerProjection
```

The passthrough layer uses `XR_NULL_HANDLE` space and
`XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT`. The Vulkan renderer
clears its swapchain images to `(0, 0, 0, 0)` and makes debug geometry fully
opaque. Its projection layer sets:

```text
XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT
XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT
```

Meta requires `XR_ENVIRONMENT_BLEND_MODE_OPAQUE` even though passthrough is
visible. The underlay shows through transparent projection pixels.

## Lifecycle

The feature and reconstruction layer are created paused. They become active
only when both conditions are true:

- Android activity is resumed.
- OpenXR session is running.

Deactivation pauses the layer before the feature, saving compositor resources.
Shutdown destroys the layer before the feature, then the session, Vulkan
objects, and instance.

The backend handles `XrEventDataPassthroughStateChangedFB`:

- Recoverable and restored errors are logged while runtime recovery proceeds.
- Reinitialization-required destroys and recreates both feature and layer,
  with a three-attempt limit per session.
- Non-recoverable errors terminate the application cleanly.

## Frame behavior

The session always preserves:

```text
xrWaitFrame → xrBeginFrame → xrEndFrame
```

When `shouldRender` is false, it performs no Vulkan work and submits zero
layers. On renderable frames, an active passthrough proxy remains submitted
even if valid projection poses are temporarily unavailable.

## Performance

Only one passthrough layer is created and active. It is paused whenever the
application is not active. Quest 3/3S acceptance should be performed at 72 Hz
to exercise Meta's recommended camera/display synchronization.

## Build and test

```bash
./scripts/build_deploy.sh --app 05-passthrough
adb logcat -s PassthroughMR:V OpenXR:V '*:S'
```

Confirm the physical environment is visible behind the cyan cube and axes.
Open and close the Meta dashboard, then quit. Logs should show start, pause,
resume, and reverse-order destruction without OpenXR or Vulkan errors.
