# Quest Camera Capture

This native OpenXR application displays the left Meta passthrough RGB camera
on a head-relative Vulkan diagnostic quad while passthrough remains active.
It demonstrates runtime permission handling, vendor-metadata camera
selection, owned YUV frames, bounded latest-frame delivery, stride-aware
conversion, and Android pause/resume cleanup.

## Requirements

- Meta Quest 3 or Quest 3S on Horizon OS v76 or newer;
- `android.permission.CAMERA`;
- `horizonos.permission.HEADSET_CAMERA`;
- `android.hardware.camera2.any`;
- `XR_FB_passthrough`.

The camera permission prompt must be accepted in the headset. Denial leaves
OpenXR and passthrough running and changes the preview border to red.

## Build and deploy

From the repository root:

```bash
./scripts/build_deploy.sh --app 09-quest-camera --build-only
adb devices
./scripts/build_deploy.sh --app 09-quest-camera
adb logcat -s QuestCamera:V OpenXR:V '*:S'
```

The preview has a green border while the camera source is running. It keeps
only the latest owned frame and reports received, consumed, and overwritten
frame counts once per second.

## Explicit private capture

Press Volume Down once to arm a single-frame capture. The next frame is saved
under the app-private `files/captures/` directory. No capture occurs at
startup, in the background, or without this action. The log reports the exact
manifest path and byte count.

Inspect file names without copying pixels:

```bash
adb shell run-as com.olibartfast.questlab.questcamera \
  find files/captures -maxdepth 2 -type f
```

Only pull a selected fixture for privacy review. Do not add it to the
repository until its contents are explicitly approved.

## Known limitations

- Quest visual, lifecycle, permission-denial, and 15-minute bounded-memory
  acceptance are device-only and remain pending until recorded.
- The diagnostic conversion/upload path intentionally favors clarity over
  zero-copy performance.
- A sanitized replay fixture is not committed automatically because it
  contains real surroundings and requires human privacy review.
- Camera calibration is logged, attached to live frames, and persisted in
  private fixture manifests when Camera2 exposes it.
