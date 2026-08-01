# Milestone 14 — RF-DETR Spatial Overlay

> **Blocked for redesign after Milestone 16:** do not implement the box-only
> Environment Depth fusion below as the production path. If the per-device
> calibration and optical-sync follow-up passes, revise this milestone around
> depth from the exact stereo pair seen by RF-DETR plus object isolation. The
> existing text is retained as a rejected/alternative design record until that
> gate completes. See `docs/stereo-capability.md`.

## Goal

Fuse live, frame-correlated RF-DETR boxes with aligned Quest environment depth
to create metric 3D annotations in OpenXR `LOCAL`.

This is the integration milestone. All individual platform risks must already
have passed in Milestones 9–13.

## Scope

Create:

- `apps/14-cv-spatial-overlay`;
- reusable detection/depth fusion;
- `docs/milestone14-validation.md`.

Reuse without redesign:

- Milestone 11 live RF-DETR protocol and client;
- Milestone 13 timestamp correlation and depth reprojection;
- existing Vulkan spatial-object rendering.

Exclude:

- model fine-tuning;
- multi-object tracking;
- motion prediction;
- semantic segmentation;
- on-device inference as a requirement;
- production authentication.

Synthetic detections may exercise failure paths but cannot satisfy acceptance.

## Atomic implementation sequence

### Sequence 0 — Integrate validated inputs

1. Create app 14 from app 13.
2. Add the Milestone 11 inference client unchanged.
3. Run RGB, depth, and inference simultaneously.
4. Confirm bounded queues.
5. Confirm all existing diagnostics remain valid.

Gate: all three streams coexist before fusion is added.

### Sequence 1 — Create a complete correlation record

1. Key RGB records by `frameId`.
2. Attach the selected depth snapshot.
3. Attach RGB and depth poses.
4. Attach temporal delta and uncertainty.
5. Attach validated RF-DETR results.
6. Expire incomplete records.
7. Bound the record ring.
8. Reject late responses after record expiry.

Gate: a diagnostic record explains every accepted or rejected inference
response.

### Sequence 2 — Select object depth samples

1. Reproject depth samples into the detection's RGB frame.
2. Select samples inside each 2D box.
3. Exclude a configurable border region.
4. Reject invalid and near-field samples.
5. Build a distance histogram or robust cluster.
6. Select the dominant foreground cluster.
7. Remove outliers.
8. Require minimum sample count and inlier ratio.
9. Compute coverage and spread statistics.

Gate: saved fixtures produce deterministic foreground sample sets.

### Sequence 3 — Construct metric 3D detections

1. Compute a robust metric centre.
2. Compute conservative percentile extents.
3. Transform the centre into `LOCAL`.
4. Define box orientation as an explicit display convention.
5. Compute fusion confidence from coverage, spread, and time delta.
6. Preserve source class, confidence, frame ID, and capture time.
7. Reject insufficient-confidence results.
8. Add host geometry tests.

Gate: measured fixtures produce plausible centres and extents.

### Sequence 4 — Render world-space overlays

1. Render full-size wireframe boxes in `LOCAL`.
2. Colour by fusion confidence.
3. Modulate or indicate detector confidence separately.
4. Fade by capture age.
5. Hide expired results.
6. Clear on a valid empty detection frame.
7. Show camera/depth/inference/correlation status.
8. Keep all fusion work off the render loop.
9. Rate-limit logs.

Gate: boxes remain world-locked while the user moves around static objects.

### Sequence 5 — Recorded replay validation

1. Add a synchronized RGB/depth fixture.
2. Add expected RF-DETR results.
3. Replay the complete pipeline without a live server.
4. Verify deterministic correlation.
5. Verify deterministic sample clustering.
6. Verify centre and extent tolerances.
7. Inject stale, missing, malformed, and low-depth cases.
8. Verify none are rendered as valid.

Gate: CI covers the complete geometry path.

### Sequence 6 — Measure real accuracy

1. Define at least two physical object classes.
2. Measure object position and dimensions.
3. Record distance, lighting, and occlusion.
4. Measure RF-DETR 2D IoU.
5. Measure depth error.
6. Measure 3D centre error in centimetres.
7. Measure extent error per axis.
8. Measure static jitter.
9. Measure slow-head-motion jitter.
10. Record false positives and fusion failures.

Gate: validation contains numeric results and representative failures.

### Sequence 7 — Measure latency and stability

1. Measure camera-frame age.
2. Measure outbound transport.
3. Measure RF-DETR inference.
4. Measure inbound transport.
5. Measure RGB/depth pair delta.
6. Measure fusion time.
7. Measure age at predicted display time.
8. Measure OpenXR frame cadence.
9. Measure queue high-water marks.
10. Measure memory high-water mark.
11. Run for 15 minutes.
12. Repeat after headset reboot.

Gate: the sustainable default submission rate and full latency breakdown are
documented.

### Sequence 8 — Regression acceptance

1. Run all new host tests.
2. Run all existing host tests.
3. Build apps 01–14 that exist.
4. Build legacy passthrough.
5. Test permission denial.
6. Test server absence and reconnect.
7. Test depth unavailable.
8. Run three clean launch/exit cycles.
9. Review every captured artifact for privacy.
10. Complete the validation document.

Gate: every Definition of Done item has objective evidence.

## Definition of Done

- live RF-DETR detections remain keyed to their originating Quest RGB frames;
- those frames are paired with validated environment-depth snapshots;
- robust depth fusion produces metric centres and extents;
- results transform correctly into OpenXR `LOCAL`;
- at least two real object classes receive world-locked 3D overlays;
- stale, incomplete, malformed, and low-confidence results remain hidden;
- 2D, depth, 3D, jitter, latency, cadence, queue, and memory metrics are
  documented;
- replay tests cover the complete fusion path;
- a 15-minute run and post-reboot run pass;
- synthetic detections alone cannot complete the milestone.
