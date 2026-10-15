# Lesson 9 - Frames: Camera, Head, LOCAL

## What you will learn

The four coordinate systems in this pipeline, the conventions of each,
and the one rule that keeps geometry honest: *everything is expressed in
`LOCAL`, using the head pose from the frame's capture time*.

Reference: `docs/coordinate-systems.md`, `libs/detection_projection`,
`libs/camera_source`.

## The four frames

| Frame | Axes | Used for |
|-------|------|----------|
| Pixel | origin top-left, +X right, +Y **down** | boxes, masks, quads, drawing on the image |
| Camera | +X right, +Y down, +Z **forward** (Camera2 optical axes) | the model's view; pose solving (`SolveQuadPose`) |
| Head | OpenXR conventions: +X right, +Y **up**, -Z **forward** | the device's rigid body |
| LOCAL | same axes as head, but **world-locked** | rendering and all geometry exchange |

Two flips live in that table and they are the source of most sign bugs:
image `y` grows downward while head/LOCAL `y` grows upward, and "forward"
is `+Z` in the camera's calibration basis but `-Z` in OpenXR.

## From pixels to a bearing

A pixel plus intrinsics gives a ray, once lens distortion is inverted.
`UnprojectPixel` in `libs/detection_projection` inverts the
Brown-Conrady model with bounded Newton iterations and returns a
direction in the **camera** frame. `BuildDetectionProjection` does this
for the four box corners (and the centre) and returns them in `LOCAL`,
plus the camera origin in `LOCAL`.

That last step is where the head pose enters:

```text
direction_local = R_localFromHead * R_headFromCamera * direction_camera
origin_local    = localFromHead.transform(cameraCenterInHead)
```

## The calibration is not a rigid pose

`LENS_POSE_ROTATION` maps *head axes to camera axes*, while
`LENS_POSE_TRANSLATION` is the optical centre *in head axes*. Composing
them as one `T * R` is subtly wrong - the translation is already in the
target frame of the rotation, not in its source frame. The repository
therefore handles them separately, and the code says so at every use
site ("normalize then transform"): normalize the quaternion, conjugate
it for the inverse direction, transform the optical centre as a point,
directions as directions.

The corner-pose path in `apps/10-rfdetr-detection/src/main/cpp/main.cpp`
("map the capture-camera PnP pose into LOCAL") is the longest example:
for each basis axis it composes camera-to-object, head-from-camera, and
local-from-head directions, then rebuilds an orthonormal quaternion with
`QuatFromBasis`. If any step yields a non-finite or non-normalizable
value, the pose is dropped rather than fixed.

## Why capture-time, not now

Between a frame's exposure and the render loop, the head can move
several centimetres. The pipeline attaches to every capture the head
pose located at the frame's **sensor timestamp**:

```text
record->localFromHead = locate(VIEW in LOCAL, at sensor time)
```

with the `CAPTURE_POSE measured/substituted` counters tracking how often
that conversion succeeded. Depth samples are independently in `LOCAL`
(they arrive world-anchored), which is why fusion needs no per-pixel
registration: bearings and points meet in the same frame with matching
timestamps.

## Why LOCAL and not VIEW

Using the view (head) frame as the base for world content makes that
content head-locked: turn your head and the box turns with you, which
defeats the purpose of measuring the world. `LOCAL` is a world-locked
origin established near startup and recenterable; `STAGE` (floor-level)
exists but is not required for this content. All persistence decisions
(what the renderer draws where, whether a result has expired) are made
in `LOCAL`.

## The quaternion convention

Quaternions here are `(x, y, z, w)` and always mean "rotate from A to B"
with the name written `aFromB` - `localFromHead`, `cameraFromHead`. A
rotation applied to a vector uses the standard sandwich; positions are
transformed as points (rotate, then add translation), directions as
vectors (no translation). Mixing those two up moves geometry by the
camera offset - typically a few centimetres that are easy to blame on
the model.

## Exercises

1. For a capture with `localFromHead.position = (0.1, 0.0, -0.2)` (head
   10 cm right, 20 cm forward of the origin) and a detection straight
   ahead of the camera, predict the sign of the detection's `LOCAL`
   position and verify against the log.
2. Find `QuatFromBasis` in `libs/xr_math` and explain what a
   left-handed input basis would produce.
3. Explain why substituting the render-time head pose on a fast head
   turn biases every measured position in the same direction (and which
   direction).
