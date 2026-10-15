# Lesson 2 - Capture to Pixels

## What you will learn

How Android's camera produces the bytes app 10 consumes, why strides
matter, what "intrinsics" and "calibration extrinsics" mean physically,
and why the calibration is *not* a rigid pose.

## YUV, not RGB

The Quest 3's RGB camera delivers `YUV_420_888` through Camera2: a full
resolution luma plane (Y) and two chroma planes (U, V) at half
resolution in each dimension. A plane is not a contiguous image: it has

- `rowStride` - bytes between the starts of consecutive rows (may exceed
  the visible width: the driver pads rows),
- `pixelStride` - bytes between samples (2 for chroma planes on many
  devices, so U and V samples interleave in one buffer).

Code that ignores strides looks correct on the emulator and shears on the
device. `libs/camera_source` copies planes faithfully and lets the
converter interpret strides; the replay fixture records the exact strides
in its manifest so host tests exercise the same edge cases.

The conversion in `libs/camera_source/src/yuv_converter.cpp` is a
fixed-point YCbCr-to-RGB (BT.601-style coefficients, e.g. `R = Y +
409*V`, `B = Y + 516*U`), producing one owned RGBA buffer.

## What the camera cannot tell you: intrinsics

The pinhole model maps a camera-space point `(X, Y, Z)`, Z forward, to a
pixel:

```text
u = fx * X / Z + cx
v = fy * Y / Z + cy
```

- `fx`, `fy` - focal length in pixels. On the Quest 3 RGB camera
  (1280x1280 stream) roughly 866 px, about a 72 degree horizontal field
  of view. It is calibrated per device.
- `cx`, `cy` - the principal point, near but not exactly the image
  centre: roughly 643, 642 in a 1280-wide frame.
- `k1, k2, p1, p2, k3` - Brown-Conrady distortion. A real lens bends
  straight lines; unprojection inverts the distortion model with bounded
  Newton iterations (`libs/detection_projection`) so corner bearings are
  honest.

Units matter: pixels are not metres, and the focal length is the bridge
between them once depth is known.

## The calibration extrinsics trap

`CameraPose` carries two numbers that look like a rigid transform but are
not one:

- `orientation` - `LENS_POSE_ROTATION`: rotates from the **sensor/head
  axes** into the **camera optical axes** (`cameraFromHead`).
- `position` - `LENS_POSE_TRANSLATION`: the optical centre position
  expressed **in the sensor/head axes**.

Composing them as a single `T(position) * R(orientation)` pose is wrong
in a way that is easy to miss: the rotation acts on vectors, the
translation lives in a different frame. The repository handles this with
"normalize then transform" helpers (lesson 9), and the header comment in
`libs/camera_source/include/camera_source/camera_source.h` states the
contract explicitly.

## Time: the capture-time rule

Every capture carries the sensor timestamp. On the headset, OpenXR
reports head poses on the same clock only after converting Android's
`CLOCK_BOOTTIME` timestamps into XR time. The app converts the sensor
timestamp, locates the head at that exact time, and tags the record. If
the conversion fails, the app records a *substituted* pose (the render
pose) and counts it: `CAPTURE_POSE measured=... substituted=...`.

Why: at arm's length, turning your head 100 degrees per second moves the
camera by ~4 cm in a 20 ms exposure-to-pose gap. Using the render-time
pose would smear every measurement by that amount, and no downstream
filtering can undo it.

## The replay fixture

`QUEST_CAMERA_FIXTURE_V2` records

```text
width height yRow yPixel uRow uPixel vRow vPixel y.bin u.bin v.bin
pixel_sha256 <sha>
sensor_timestamp_ns <ns>
intrinsics fx fy cx cy
camera_from_head <quaternion components>
```

This makes every lesson reproducible without a headset: the fixture is a
real capture, checksummed, with real strides and intrinsics.

## Exercises

1. In `libs/camera_source`, find where `rowStride` is applied and change
   the fixture manifest's `yRow` to see the shearing artefact in a test.
2. Compute the horizontal field of view for `fx = 866` at width 1280 and
   check it against the camera's published specs.
3. Find `CAPTURE_POSE` in the app log; explain what a `substituted`
   count would do to a pose measurement taken during fast motion.
