# Plan — Monitor Pose Ground Truth

Order per reviewer memo: baseline → capture pose → gravity axis → tracker → mask.

## Phase P0 — Baseline run (device, user-executed)
Record monitor + ArUco board + offline solvePnP numbers into validation.md
BEFORE any code lands. R-7. Owner: user (no headset here).

## Phase P1 — Capture-time pose (this slice)
- `XrInstanceOptions`: optional-extension support (enable
  `XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME` when the runtime exposes it;
  never hard-fail when absent) + expose convert PFN availability.
- `XrSessionContext`: `Instance()` accessor + `LocateHeadAtTime(XrTime,
  XrSpaceLocation*)` thin wrapper (pattern: existing `LocateTrackedSpaces`).
- Pure timestamp math (ns→timespec, residual/age computation, validity policy)
  in a host-testable unit with `xr_core` stub tests.
- App 10: `SubmitCapture` takes capture-time pose (locate at submit from the
  capture's sensor timestamp); `poseWasSubstituted` set ONLY on fallback;
  residual + history-age log line per scene (throttled).
- R-1, R-2, R-7(after). Writable: `libs/xr_core/*`, `apps/10-rfdetr-detection/src/main/cpp/main.cpp`.

## Phase P2 — Gravity in-plane axis
R-3 in `libs/detection_fusion` (+ host tests): gravity-right for vertical,
PCA/min-area for horizontal, fallback flag. G-5.

## Phase P3 — Tracker
R-5: association + SE(3) filter + hysteresis (+ optional anchor promotion).
G-6. New `libs/` unit or inside `detection_fusion` — decided at phase start.

## Phase P4 — Isolation
R-4: class–normal coherence gate now; segmentation mask tracked as follow-on
(RF-DETR seg / SAM backend). R-6 numbers feed G-8.

## Integration checkpoints
- After P1: host tests green + `assembleDebug` green (where SDK present) +
  device run PENDING-USER with residual logs; re-run baseline (R-7 after).
- P2–P4 each: host tests + device numbers before/after in validation.md.
