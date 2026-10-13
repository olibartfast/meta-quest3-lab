# Validation: Monitor 6D Pose Ground Truth

First scored run for app-10 pose. Until this fills in, "pose is wrong" is
unquantified: `FUSED_POSE` log lines exist but no reference exists to score
them against. Fill **Result** as evidence lands. `[x]` passed ·
`[ ]` not run · `[!]` failed. A tick with no number is not a result.

**Revision:** `0145b48` (class filter + pose diagnostics) on top of `be593af`
(surface-6D orientation) · **Headset:** `2G0YC5ZH0K0293`
**Backend:** streaming via Triton `rfdetr_nano:1` · **Filter:** `--allowed-classes 72`

## Fixture (tape + print, no mocap)

- Monitor with taped width/height/thickness (±5 mm) and screen-plane offset
  from the housing front. Record: `W ___ H ___ T ___`.
- Printed ArUco board (from PoseTrack fixtures or `cv2.aruco`, marker size
  ___ mm, board layout recorded) rigidly mounted beside the screen, same
  plane assumed unless measured otherwise.
- Standoffs on the floor at 1 m / 2 m / 3 m from the screen plane, viewing
  angles 0° (head-on) and ~30° yaw. Phone level for the 30° mark (±2°).
- Lighting: normal room light; note time of day (passthrough/depth vary).

## Reference (ground truth without new hardware)

- solvePnP on the ArUco board from the same captured frames, offline on the
  host, gives the trusted camera→board pose per frame. Board→screen offset
  is taped, yielding GT box centre/quat in the same LOCAL frame as
  `FUSED_POSE`. (Fiducial PnP as GT source, not as product path.)
- Privacy: scoring uses logged poses + taped numbers only. No pixels leave
  the headset; `FUSED_POSE`/`DETECTION`/`DEPTH_DIAG` lines are stats.

## Procedure

1. Deploy monitor-only: `./scripts/build_deploy.sh --app 10-rfdetr-detection
   --backend streaming --allowed-classes 72`.
2. At each standoff × angle: hold still 10 s (static), then slow ±15° head
   yaw 10 s (motion). Call out marks on video/notes for segmentation.
3. Pull `adb logcat -s RFDetrDetection:V`, keep `FUSED_POSE`,
   `FUSE_REJECT`, `WORLD_PROJECTION`, `DEPTH_DIAG`, `RFDETR status`.
4. Score offline per frame with a GT pose: translation err, angular err
   (quat angle), plane-normal err, 3D IoU vs taped box, plus rates
   (fused / rejected / filtered / expired).

## Gate (provisional bars for the first run — record, then judge)

| # | Check | Expected | Result |
|---|---|---|---|
| G-1 | Static translation err @2 m head-on | mean ≤10 cm, max ≤20 cm | `[ ]` |
| G-2 | Static angular err @2 m head-on | mean ≤10°, max ≤20° | `[ ]` |
| G-3 | Plane-normal err (screen) | mean ≤7° | `[ ]` |
| G-4 | 3D IoU vs taped box | mean ≥0.25 | `[ ]` |
| G-5 | 30° yaw: angular err | mean ≤15° (no axis snap) | `[ ]` |
| G-6 | Slow-yaw: track continuity | gaps ≤2 s, no teleport >30 cm | `[ ]` |
| G-7 | Rejection honesty | `FUSE_REJECT` rate ___; wrong boxes preferred over silent? record | `[ ]` |
| G-8 | Depth GT on screen plane | per-pixel bias/RMS vs plane fit at 2 m: ___ | `[ ]` |
| G-9 | Latency at score time | `LATENCY` capture_to_now p50/p95: ___ | `[ ]` |

Failing G-1/G-2 head-on means the surface fit is on clutter (frustum/depth
misalignment), not that orientation math needs tuning — fix sync first
(Milestone 13 capture-time pose). Failing only G-5/G-6 means orientation:
revisit in-plane axis and jitter handling.

**Verdict:** `________` **Date:** `________`
