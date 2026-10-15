# Lesson 1 - Pipeline Overview

## What you will learn

The stages between camera photons and rendered geometry, where each stage
lives in the repository, and the data that crosses each boundary.

## The stages

```text
capture -> preprocess -> model -> postprocess -> geometry -> fusion -> render
```

Each boundary is a data contract, not a function call in one file. That
is deliberate: it lets the same postprocessing feed a host model and the
geometry stages be unit-tested without a headset.

| Stage | Where | Output |
|-------|-------|--------|
| Capture | `libs/camera_source` | `RgbCapture`: owned Y/U/V planes + intrinsics + Camera2 extrinsics |
| Preprocess | `tools/rfdetr_inference` (`preprocessing.*`) | float tensor `[1,3,H,W]`, normally `[1,3,384,384]` |
| Model (host) | Triton over KServe v2 via `libs/triton_client` | `dets [1,Q,4]`, `labels [1,Q,91]`, optional `masks [1,Q,96,96]` |
| Postprocess | `tools/rfdetr_inference` (`postprocessing.*`) | `Detection[]` with **source-pixel** boxes and optional binary masks |
| Screen quad | `libs/screen_corners` | `Quad` (4 corners) + score, or a refusal reason |
| Pose | `libs/screen_corners` | `Pose` (quaternion + position in the capture camera frame) + reprojection RMS |
| Projection | `libs/detection_projection` | 4 corner bearings + origin in `LOCAL` |
| Fusion | `libs/detection_fusion` | `FusedBox` (metric, oriented) or a refusal |
| Render | `apps/10-rfdetr-detection` | debug lines in the world; annotated 2D preview |

## Who owns the clock

Three different clocks appear and conflating them is the classic bug:

- **Sensor timestamp** - when the camera's rolling shutter began exposing
  the frame (`RgbCapture::sensorTimestampNanoseconds`). All geometry must
  use the head pose at *this* time, not the current render time.
- **Arrival timestamp** - when the app received the frame. Used for
  expiry ("is this result still worth drawing?").
- **Inference timestamps** - steady-clock start/end around the model run,
  used only for latency statistics.

## One frame through the system

1. The camera adapter publishes `RgbCapture`. A new frame replaces the
   pending one: the pipeline always works on the newest frame, and the
   drop is counted (`CameraSourceStats::overwrittenFrames`), never queued.
2. The app's worker converts YUV to RGBA once; that buffer serves both
   the detector and the 2D diagnostic preview.
3. Preprocessing produces the normalized float tensor (lesson 3).
4. Inference runs on the host: an HTTP round trip to Triton. The headset
   never executes the model.
5. Postprocessing turns raw tensors into detections in source pixels
   (lesson 4), attaching masks for seg contracts (lesson 5).
6. For each masked detection the worker attempts a screen quad
   (lesson 6) and, for classes with a known planar size, a pose
   (lesson 7).
7. The detection box plus camera calibration become bearings in `LOCAL`
   at the capture-time head pose (lesson 9).
8. Depth fusion turns bearings plus environment-depth samples into a
   metric cuboid, with provenance marking what was measured versus
   assumed (lesson 8).
9. The renderer draws at the display frame rate (roughly 70-90 Hz),
   much faster than inference, so geometry
   is prepared off-thread and swapped in atomically.

## Submissions and rates

Detector submissions are rate-limited (`submission_hz`) and results
expire (`expiry_ms`). Defaults come from `scripts/build_deploy.sh`:

| Backend | submission_hz | expiry_ms | Why |
|---------|---------------|-----------|-----|
| triton | 2 | 5000 | host compute; retain while the next frame is in flight |

The counters you see in the log (`submitted`, `completed`,
`pending_drop`, `rate_drop`, `dispatch_drop`, `unmatched`, `expired`)
exist so that "the screen is empty" can always be answered with a
number instead of a guess.

## Three model variants, one contract

`tools/rfdetr_export/*-manifest.json` pins each variant:

| Variant | Input | Queries | Outputs |
|---------|-------|---------|---------|
| nano | 384x384 | 300 | dets, labels |
| medium | 576x576 | 300 | dets, labels |
| seg-small | 384x384 | 100 | dets, labels, masks (96x96) |

The manifest is the contract: shapes, normalization constants, class
table, thresholds. Code never hard-codes them; if the manifest says
`[1,100,96,96]`, that is what the mask decoder uses.

## Exercises

1. In `docs/rfdetr-detection.md`, find which counters increment when the
   CPU cannot keep up with the camera. Explain the difference between
   `pending_drop` and `overwrittenFrames`.
2. Grep for `kManifestIdentityBytes` and explain what is being compared
   and where those 64 characters come from.
3. List the stages that run on the headset CPU and the stages that can
   run on a host, for the Triton backend.
