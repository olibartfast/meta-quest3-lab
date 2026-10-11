# Plan: Stereo 2D→3D Prototype

Run from repo root. A group is complete only when its rows are recorded in
`validation.md`. This packet shipped always-on (no `--stereo-prototype` flag):
revert is a git revert of the app-10 wiring, per V-18.

## Group 0 — P1B + P3O gate (blocks production, not this prototype)

1. Read `LENS_INTRINSIC_CALIBRATION`, `LENS_POSE_ROTATION/TRANSLATION` for
   cameras 50/51 via `libs/camera_source`; persist versioned artifact via
   `libs/artifact_integrity`.
2. Straight-line residuals on natural edges (zero-distortion hypothesis),
   baseline vs tape, reprojection error, rectified vertical disparity from
   natural features.
3. Optical sync stimulus: shared irregular light, per-frame luminance /
   correlation statistics only, no pixels stored.
4. Verdict Pass → Groups 1–4 become production; Fail → drop stereo, keep
   env-depth path, rewrite Phases 3–5 per roadmap.
Status: **pending**.

## Group 1 — Concurrent pair capture

1. Extend `MetaCamera2Adapter` or add `StereoCameraSource`: open 50+51 at one
   common size (default 640×480), repeating request, nearest-timestamp pairing,
   300+ pairs smoke run, unmatched <1%.
2. Capacity-one latest-pair queue; drop-oldest, never block OpenXR; leak check
   (no held `Image`/camera handle).
Status: pending.

## Group 2 — Disparity → range, host-testable

1. New `libs/stereo_depth` (or extend `depth_source`): rectify with Group 0
   model, block-match disparity, metric range, reject invalid + near-field.
2. Host CTest over recorded stereo fixtures with known geometry (deterministic
   range image, tolerance-bound).
3. Tape-measured surfaces (≥3 standoffs): report range error, never tune fixture
   to fit.
Status: pending.

## Group 3 — Fusion in app 10

1. FrameId-keyed record: left frame + intrinsics + stereo extrinsics +
   capture pose + RF-DETR result + stereo range + env-depth snapshot.
2. Stereo points → `FuseDetection` (`libs/detection_fusion` params per R-5);
   env-depth reprojected, agreement gate (default 0.10 m / 10%), fallback with
   `rangeSource=env-depth` + degraded confidence, provenance preserved.
3. `LOCAL` transform, confidence/age coding, hide rules, valid-empty clears.
4. Off-render-thread fusion; render loop consumes prepared draws only.
Status: pending.

## Group 4 — Headset measure (≥2 real classes, no mocks)

1. 2D IoU vs hand label, range error, 3D centre error, per-axis extent error,
   static + slow-motion jitter, false-positive / fusion-failure log.
2. Env-depth on/off ablation: agreement rate, fallback rate, delta in errors.
3. 15-min thermal smoke (inference + stereo + fusion) with queue high-waters.
Status: pending.

## Task groups for delegation

- Planner owns Group 0 verdict + acceptance command; worker packets per group
  carry writable paths (`libs/stereo_depth/**`, `libs/camera_source/**`,
  `apps/10-rfdetr-detection/**`, `specs/2026-10-10-stereo-2d-to-3d/**`),
  read-only acceptance suite, one acceptance run, stop on unexpected diffs.
- Reviewer: broad read, zero write.
