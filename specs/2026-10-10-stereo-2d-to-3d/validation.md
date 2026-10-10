# Validation: Stereo 2D→3D Prototype

Fill **Result** as evidence lands. `[x]` passed · `[ ]` not run · `[!]` failed.
A tick with no number is not a result.

**Revision:** `agent/stereo-2d-to-3d` based on `fbad8aa`
**Host date:** 2026-10-10 · **Headset:** `2G0YC5ZH0K0293` (available, not yet run)

## Gate (Group 0 → R-2)

| # | Check | Expected | Result |
|---|---|---|---|
| V-0a | API model persisted + checksummed | artifact + SHA, device+config key | `[x]` fresh 2026-10-10 2D-panel run: `stereo-calibration-eureka-640x480.json` sha `22da3ae2…00db9b74`, baseline 0.06419 m, fp `…52433670048800520`; probe verdict `PASS_WITH_DEBT` (P1B+P3O open) |
| V-0b | Straight-line residuals | ≤1.0 px p95 natural edges | `[ ]` |
| V-0c | Baseline vs tape | \|API−tape\| ≤5 mm | `[ ]` |
| V-0d | Rectified vertical disparity | bound recorded, p95 ≤2 px | `[ ]` |
| V-0e | Optical sync stimulus | correlation proves shared exposure or Fail | `[ ]` |
| V-0f | Gate verdict | Pass→produce / Fail→drop stereo | `[ ]` |

## Pair + range (Groups 1–2 → R-1/R-2/R-3)

| # | Check | Expected | Result |
|---|---|---|---|
| V-1 | 300+ pairs, unmatched <1% | `pull` report numbers | `[x]` 320 matched / 300 req, 0+0 unmatched, 49.53 pairs/s, skew med 0ns p95 2µs max 3µs, 2026-10-10 |
| V-2 | No handle leak | 3 cycles, `Image`/camera clean | `[ ]` |
| V-3 | Host fixture range | deterministic ±tolerance | `[x]` `stereo_depth_tests` PASS 2026-10-10: 3072/3072 exact d=12, range 4.63452m (fx866.4×B0.06419), flat→0 valid, malformed rejected |
| V-4 | Tape range error | per-standoff mean/max recorded | `[ ]` |

## Fusion (Group 3 → R-4/R-5/R-6/R-7/R-8)

| # | Check | Command / probe | Result |
|---|---|---|---|
| V-5 | Correlation rejects | stale/distant/mismatched hidden, counters rise | `[ ]` |
| V-6 | Empty clears | valid-empty → no boxes | `[ ]` |
| V-7 | Agreement gate | stereo↔env Δ within 0.10 m/10% or hidden; rate ___ | `[ ]` |
| V-8 | Fallback provenance | env-depth boxes flagged, tinted, lower confidence | `[ ]` |
| V-9 | World-lock | static object stable during head move | `[ ]` |

## Accuracy (Group 4 → R-5/R-8)

| # | Check | Expected | Result |
|---|---|---|---|
| V-10 | 2D IoU (≥2 classes) | per-class IoU ___ | `[ ]` classes ___ |
| V-11 | 3D centre error | cm mean/max per standoff | `[ ]` |
| V-12 | Extent error per axis | cm per axis | `[ ]` |
| V-13 | Jitter static / slow-motion | cm + numeric flicker note | `[ ]` |
| V-14 | Ablation env on/off | Δ errors + fallback rate | `[ ]` |
| V-15 | 15-min + queues | bounded, high-waters ___ | `[ ]` |

## Guardrails

| # | Check | Expected | Result |
|---|---|---|---|
| V-16 | No pixel leak | stats-only unless fixture action | `[ ]` |
| V-17 | Perf evidence clean | no prototype build in `docs/performance` | `[ ]` |
| V-18 | Revert cost | `--stereo-prototype` off = prior app-10 behavior | `[ ]` |

**Verdict:** `________` **Date:** `________`
