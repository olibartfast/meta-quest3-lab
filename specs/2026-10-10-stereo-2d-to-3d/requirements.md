# Requirements: Stereo 2D→3D Prototype (same-pair primary, env-depth secondary)

## Objective

Lift frame-correlated RF-DETR 2D detections into metric 3D boxes in OpenXR
`LOCAL`, using the Quest passthrough stereo pair as the primary range source
and `XR_META_environment_depth` as agreement / fallback / occlusion only.
Delivery vehicle is `apps/10-rfdetr-detection` extended; no new app.

Constitution trace: `specs/mission.md` (measured-render discipline — draw only
what is measured; no billboard boxes), `specs/tech-stack.md` (C++17, NDK r29,
OpenXR 1.1.51, Vulkan stereo, ONNX 1.21.0 / RF-DETR Nano), `specs/roadmap.md`
Phases 2–5 (gate → stereo depth → time/pose registration → metric overlay).

## User decisions (2026-10-10 interview)

1. Depth source: same-pair stereo primary (detector on left image of cameras
   50/51, disparity from the exact pair RF-DETR saw).
2. Gate handling: prototype in parallel with the Phase 2 gate (throwaway risk
   accepted). See Deviation D-1.
3. Vehicle: extend app 10 (`libs/detection_fusion`, `libs/camera_source`,
   `libs/detection_projection`); do not create `apps/14-cv-spatial-overlay`.
4. Isolation: robust box clustering first (border exclusion, foreground cluster);
   no segmentation in this slice.
5. Env-depth role (follow-up): agreement gate + degraded fallback + occlusion,
   never silent primary.

## Deviation D-1 (deliberate, recorded)

`specs/roadmap.md` Phase 2 states: no stereo-depth or object-pose work may
start before the P1B+P3O gate passes. This slice prototypes anyway while the
gate runs. If the gate verdict is Fail, the stereo prototype is dropped and the
slice falls back to the env-depth fusion app 10 already ships, per
`docs/stereo-capability.md`. Prototype code stays isolated behind a config flag
so deletion is one revert.

## Functional requirements

- R-1 Concurrent capture of passthrough cameras 50 (left) + 51 (right) at one
  common YUV size, nearest-timestamp pairing with bounded window, per-frame
  statistics only unless a privacy-reviewed fixture action runs.
- R-2 Per-device stereo calibration model sourced from Camera2
  `LENS_INTRINSIC_CALIBRATION`, `LENS_POSE_ROTATION`, `LENS_POSE_TRANSLATION`
  (shared `GYROSCOPE` reference), zero-distortion pinhole as hypothesis (Meta
  publishes no distortion coefficients), persisted versioned + checksummed via
  `libs/artifact_integrity`, keyed to device + stream config.
- R-3 Rectify both images with the validated model; disparity → metric range;
  reject invalid and near-field (`<0.25 m`) samples; bound far field at 6 m
  (matches `FusionParameters`).
- R-4 FrameId-keyed retained record: left pixels, intrinsics, stereo extrinsics,
  capture-time pose, RF-DETR result, stereo range image, env-depth snapshot
  with its own time/pose/FOV; expire incomplete records; bounded ring; reject
  temporally distant pairs.
- R-5 Box clustering per `libs/detection_fusion`: border inset 0.12, foreground
  share 0.05, cluster tolerance 0.25 m, percentile extents 0.05/0.95, RANSAC
  160 iters / 0.025 m / 0.70 inlier ratio, min 120 samples; empty result is valid
  output, never papered over.
- R-6 Env-depth secondary: reproject via its own pose/time/FOV; stereo vs
  env-depth agreement gate within stated tolerance or hide; fallback emits only
  with degraded confidence + provenance flag; occlusion path unchanged.
- R-7 Provenance on every `FusedBox`: range source enum
  (stereo / env-depth / prior), `depthExtentIsAssumed`, `orientationIsMeasured`;
  assumed far face tinted; stale/low-confidence/uncorrelated hidden; valid empty
  clears prior geometry.
- R-8 Transform centre into OpenXR `LOCAL` via capture-time head+camera pose
  (substituted-pose logging until Milestone 13 timebase is established); boxes
  world-locked; orientation from RANSAC plane or bearing convention flagged.

## Constraints

- C-1 Passthrough / camera apps: no Operator capture sessions for 05–10; no
  pixels to disk except explicit fixture action; no capture in performance
  evidence.
- C-2 ARM64 only, C++17 `-Wall -Wextra -Wpedantic`, no engine, no vendor layer
  outside `libs/xr_meta_*` / `libs/camera_source` / `libs/depth_source`.
- C-3 Calibration artifact is per-device, invalidated on firmware / stream
  crop/ROI / distortion-correction / mechanical change.

## Out of scope

- Manual target calibration; 6DoF semantic pose (FoundationPose/MegaPose);
  tracking / motion prediction; model fine-tune; transport hardening (M11);
  M12 occlusion app; M15 perf baselines; M9 verdict changes; M9
  source-switching fix (separate defect); production Operator dependency.

## Open questions

- O-1 Which two physical object classes for acceptance? (default: chair + crate
  box unless operator substitutes).
- O-2 Common stereo size: 640×480 (probe default) or higher intersected size?
- O-3 Agreement tolerance stereo↔env-depth (default: 0.10 m or 10% whichever
  greater, to be tightened by measurement)?
