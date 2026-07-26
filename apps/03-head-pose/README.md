# Head Pose and Coordinate Spaces

This native OpenXR application demonstrates the spatial foundations used by
later interaction and computer-vision milestones.

It renders:

- a large `LOCAL`-space axis triad that remains world-stable;
- a small axis triad placed 0.6 metres in front of the current `VIEW` pose;
- the cyan `STAGE` boundary when the runtime exposes both `STAGE` and bounds.

Axis colours are red `+X`, green `+Y`, and blue `-Z` (OpenXR forward).
The application logs the predicted-time head pose once per second.

## OpenXR extensions

Only `XR_KHR_android_create_instance` and `XR_KHR_vulkan_enable2` are enabled.
`VIEW`, `LOCAL`, and `STAGE` are core OpenXR reference spaces, not extensions.
`STAGE` and its bounds are optional.

## Build and deploy

```bash
./scripts/build_deploy.sh --app 03-head-pose
```

Build without installing:

```bash
./scripts/build_deploy.sh --app 03-head-pose --build-only
```

Filtered logging:

```bash
adb logcat -s HeadPose:V OpenXR:V '*:S'
```

Use the Meta system button and select **Quit** for an orderly lifecycle test,
or run:

```bash
adb shell am force-stop com.olibartfast.questlab.headpose
```

## Expected behaviour

The background is dark because this milestone does not use passthrough. The
large axes should remain stable while the headset moves. The smaller axes
should follow head translation and rotation while remaining visibly in front
of the viewer.

## Known limitations

- Debug geometry uses one-pixel Vulkan line primitives.
- There is no depth buffer, interaction, persistence, or recenter UI.
- `LOCAL` may change when the runtime recentres its origin.
- A runtime may expose `STAGE` without providing usable boundary dimensions.
