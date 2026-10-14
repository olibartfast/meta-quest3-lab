# Requirements — Monitor Pose Ground Truth (surface-6D → measured 6DoF)

Spec: `specs/2026-10-14-monitor-pose-ground-truth/` · App: `apps/10-rfdetr-detection` + `libs/{xr_core,camera_source,detection_fusion}` · Milestone: 13 (Sequences 1–2 applied to the fusion path)

Source: reviewer memo 2026-10-14 (surface-6D analysis, 6 numbered points + order).
Verified against this checkout 2026-10-14; divergences from the memo are listed
under [D] — the memo describes a newer/different tree (`StableInPlaneAxis`,
`QvFuse`, `quest_vision`, Ocean refinement flag, and this spec file itself are
absent here).

## Goal
Replace substituted render-time head pose with measured capture-time pose on the
RGB frustum path, so the frustum and the LOCAL depth cloud agree under head
motion and the RANSAC plane is the object's — then fix the in-plane axis,
tracking, and isolation in that order.

## Verified findings (this checkout)
- [F-1] `BuildScene` pairs the latest RGB capture with `latestLocalFromHead_`
  (render-time pose, `main.cpp:881-889`); no correlation with
  `capture.sensorTimestampNanoseconds`. Confirmed root cause of frustum/cloud
  misalignment under head motion.
- [F-2] `poseWasSubstituted = true` hardcoded (`main.cpp:243, 574`).
- [F-3] `XrInstanceOptions.additionalExtensions` exists (`main.cpp:1375-1382`);
  `XR_KHR_convert_timespec_time` is NOT requested anywhere; no
  `xrConvertTimespecTimeToTimeKHR` usage in repo.
- [F-4] `XrSessionContext` exposes `ViewSpace()/LocalSpace()` but NOT the
  `XrInstance` handle; capture-time locate needs a new seam.
- [F-5] `RgbCapture` carries `sensorTimestampNanoseconds`,
  `arrivalTimestampNanoseconds`, `cameraFromHead` (`camera_source.h:57-67`).
  C++ adapter does NOT expose `SENSOR_INFO_TIMESTAMP_SOURCE` (Java precedent
  only, app 16).
- [F-6] Fusion lib already flags `orientationIsMeasured` / `depthExtentIsAssumed`
  (`detection_fusion.h:79-100`); yaw resolves about `upInLocal` (Y-up).

## In Scope
- [R-1] Capture-time head pose (memo §1, M13 seq 1–2): map Camera2 sensor
  timestamp → `XrTime` via `XR_KHR_convert_timespec_time` (optional extension:
  enable when present, stay substituted + explicit log when absent); call
  `xrLocateSpace(VIEW, LOCAL, captureXrTime)` at submit; store per-record pose;
  set `poseWasSubstituted=false` only on valid locate; log residual
  (render-now minus capture time + history age).
- [R-2] Timestamp-source honesty (memo §1): document the REALTIME/BOOTTIME
  assumption at the mapping site; expose the adapter's timestamp-source enum
  toward the C++ layer when cheap, else file it as a named follow-on with
  owner (no silent assumption).
- [R-3] Gravity in-plane axis (memo §2): vertical surfaces
  `right = normalize(cross(up, normal))`; horizontal surfaces PCA/min-area
  rectangle of projected inliers; world convention only as explicit fallback
  with a separate `inPlaneMeasured`-style flag next to `orientationIsMeasured`.
- [R-4] Object isolation (memo §3): class–normal coherence check first
  (monitor ⇒ near-horizontal normal); segmentation mask (RF-DETR seg / SAM
  backend) as the tracked follow-on, not smuggled in.
- [R-5] Temporal tracker (memo §4): per-object association (3D distance +
  class), SE(3) filter (translation Kalman + quaternion SLERP/angular gating),
  birth/death hysteresis; spatial-anchor promotion optional behind the
  existing `xr_spatial_anchors` lib.
- [R-6] Metric range honesty (memo §5): environment-depth accuracy stays
  unmeasured until G-8; back-face extent stays prior-backed
  (`depthExtentIsAssumed`, per-class thickness OK for monitor). Model-based
  pose (FoundationPose/MegaPose) explicitly out of scope.
- [R-7] Validation first/last (memo §6): baseline device run (monitor + ArUco
  board + offline solvePnP) BEFORE R-1 lands and AFTER, so the sync error
  contribution is isolated. Rule: a tick with no number is not a result.

## Out of Scope
- FoundationPose/MegaPose streaming backend; Unity C-API timestamp exposure
  ([D-2]: no `QvFuse` here); Ocean refinement flag ([D-4]: absent here);
  stereo path P1B+P3O; training/retraining detectors.

## Divergences from the memo [D] (recorded, not silently dropped)
- [D-1] `StableInPlaneAxis` + 11° snap cascade: absent here. R-3 implements
  the memo's correction directly in `detection_fusion`.
- [D-2] `QvFuse` C API + Unity timestamp exposure: absent here.
- [D-3] `quest_vision` module: absent; the code lives in app 10 +
  `libs/{camera_source,detection_fusion,detection_projection}`.
- [D-4] Ocean refinement flag (default-off): absent here; G-3 verdict waits
  for whatever refinement exists in the reviewer's tree.
- [D-5] Canonical gates live in this packet's `validation.md` (recovered from
  origin 2026-10-14: G-1..G-9 with provisional bars). Earlier reconstruction
  notes are superseded.

## Constraints
- C++17, existing Meta-sample style (4-space, same-line braces,
  `#pragma once`); no new runtime deps for P1 (OpenXR loader only).
- `detection_fusion` + `xr_core` host tests keep passing; app 10 device build
  (`assembleDebug`) stays green — device execution itself is PENDING-USER
  (no headset on this machine).
- No silent fallbacks: every degraded path logs + flags.

## Open Questions
- [Q-1] BOOTTIME vs MONOTONIC for convert_timespec on Quest: assumption + residual log now; JNI source check follow-on owner: maintainer.
- [Q-2] Numeric thresholds for G-1..G-9: fixed after baseline run (validation.md).
