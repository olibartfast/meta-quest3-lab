# Hand tracking

Milestone 7 uses the portable Khronos `XR_EXT_hand_tracking` extension. The
reusable `xr_hand_tracking` library owns one tracker per hand and exposes a
small engine-facing state: 26 joint poses and radii, per-component validity
and tracking flags, and the hand-level active state.

## Runtime flow

1. Request `XR_EXT_hand_tracking` when creating the OpenXR instance.
2. Chain `XrSystemHandTrackingPropertiesEXT` into
   `xrGetSystemProperties` and require `supportsHandTracking`.
3. Resolve the extension entry points and create left/right trackers with
   `XR_HAND_JOINT_SET_DEFAULT_EXT`.
4. Once per rendered frame, locate exactly `XR_HAND_JOINT_COUNT_EXT` joints
   at the predicted display time relative to that frame's base space.
5. Ignore joint data when `XrHandJointLocationsEXT::isActive` is false.
   Treat OpenXR position/orientation valid and tracked flags independently.

The app uses `LOCAL` as supplied by the shared session frame loop. These
coordinates are stable only for the current session and can move physically
after a system recenter.

## Pinch interaction

`xr_interaction::PinchState` compares tracked thumb-tip and index-tip
positions. A pinch starts at 25 mm or less and releases at 40 mm or more.
This hysteresis avoids repeated edges near one threshold. Losing either
tracked position immediately ends an active pinch.

App 07 uses the pinch midpoint to:

- place one object on the first pinch;
- grab it when a new pinch starts within 15 cm;
- move it while that pinch remains active;
- drop it on release or tracking loss.

Left/right pinch machines are independent. A simultaneous start chooses the
right hand deterministically.

## Quest manifest and validation

The Android app declares:

```xml
<uses-permission android:name="com.oculus.permission.HAND_TRACKING" />
<uses-feature
    android:name="oculus.software.handtracking"
    android:required="true" />
```

Build and install:

```bash
./scripts/build_deploy.sh --app 07-hand-tracking
adb logcat -s HandTracking:V OpenXR:V '*:S'
```

Verify both hands become active, all five finger chains remain connected,
pinch edges occur once, tracking loss drops a grabbed object, passthrough
survives pause/resume, and steady frame cadence remains near the headset's
selected refresh interval.
