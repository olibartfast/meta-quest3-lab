# Validation: pose_solver

Defined before implementation (TDD). `[x]` passed · `[ ]` not run · `[!]` failed.
A tick with no number is not a result. Numeric tolerances are first-measurement
targets; tighten after the first recorded run (see O-1).

Run from the repo root:

```text
cmake -S libs/pose_solver -B build/pose-solver-tests -DBUILD_TESTING=ON
cmake --build build/pose-solver-tests --parallel
ctest --test-dir build/pose-solver-tests --output-on-failure
```

## Traceability

| Requirement | Evidence |
|---|---|
| R-1 contract/scope | V-1 |
| R-2 exact recovery | V-2 |
| R-3 noise | V-3 |
| R-4 outliers | V-4 |
| R-5 scale + determinism | V-5 |
| R-6 degeneracy / fail-closed | V-6 |
| R-7 performance | V-7 |
| R-8 integration | V-8 |

## Checks

| # | Check | Pass criteria | Result |
|---|---|---|---|
| V-1 | `pose_solver_tests` builds; `TestContract` runs | header path `libs/pose_solver/include/pose_solver/pose_solver.h`, namespace `questlab::pose`, signature + defaults match `plan.md` interface sketch | `[ ]` |
| V-2 | `TestExactRecoveryNonCoplanar` + `TestExactRecoveryCoplanar` (noiseless, N ∈ {6, 40, 200}) | position ≤ 2e-3 m (at ~1 m scale), rotation ≤ 0.1°, RMS ≤ 1e-3 px | `[ ]` |
| V-3 | `TestNoise` (σ = 0.5 px, N ≈ 40, ~1 m) | position ≤ 15e-3 m, rotation ≤ 1° | `[ ]` |
| V-4 | `TestOutliers` (≥ 50% outliers) | position/rotation within V-3 bounds; inlier-mask agreement ≥ 95% | `[ ]` |
| V-5 | `TestMetricScale` + `TestDeterminism` | position scales by k (rel. ≤ 1e-3), rotation unchanged; repeat runs bit-identical | `[ ]` |
| V-6 | `TestDegenerateAndRefusal` | `<6` → `"insufficient correspondences"`; collinear/duplicate sample → `"degenerate sample geometry"`; bad intrinsics → `"invalid pose input"`; non-finite input → `"non-finite correspondence"`; no consensus → `"no consensus"`; no NaN/Inf in any `Result` field | `[ ]` |
| V-7 | `TestPerformanceBudget` (N ≈ 100, 50% outliers, defaults) | wall time recorded and well under 500 ms | `[ ]` |
| V-8 | warning-clean build + PIC + CI step | no `-Wall -Wextra -Wpedantic` diagnostics; `POSITION_INDEPENDENT_CODE ON`; CI "Test pose solver geometry" step present and green | `[ ]` |

## Evidence log

| ID | Command / check | Result | Date | Notes |
|----|-----------------|--------|------|-------|
| V-1 | `cmake … && cmake --build … && ctest …` |  |  | red→green |
| V-2 | `pose_solver_tests` exact-recovery groups |  |  | red→green |
| V-3 | `pose_solver_tests` noise group |  |  | red→green |
| V-4 | `pose_solver_tests` outlier group |  |  | red→green |
| V-5 | `pose_solver_tests` scale/determinism group |  |  | red→green |
| V-6 | `pose_solver_tests` degeneracy group |  |  | red→green |
| V-7 | `pose_solver_tests` performance group |  |  | wall time ___ ms |
| V-8 | `ctest` in CI + warning scan |  |  |  |

### Red run (commit 1, TDD phase P-1)

Command (repo root):

```text
cmake -S libs/pose_solver -B build/pose-solver-tests -DBUILD_TESTING=ON
cmake --build build/pose-solver-tests --parallel
ctest --test-dir build/pose-solver-tests --output-on-failure
```

Result: build clean (zero warnings), `ctest` fails `1/1 pose_solver_tests
(Subprocess aborted)` — the stub returns `false`/`"not implemented"`, so every
group assert-aborts. Per-group red evidence (each aborts at the first assert):

| Group | First failing assert |
|---|---|
| contract | `TestContract` `SolvePose(...)` returned false |
| exact (non-coplanar + coplanar) | `AssertExactRecovery` `SolvePose(...)` false |
| noise | `TestNoise` `SolvePose(...)` false |
| outliers | `TestOutliers` `SolvePose(...)` false |
| metric scale | `TestMetricScale` `SolvePose(...)` false |
| determinism | `TestDeterminism` `SolvePose(...)` false |
| degenerate | `TestDegenerateAndRefusal` `error == "insufficient correspondences"` |
| performance | `TestPerformanceBudget` `solveOnce()` false |

## Deviations

- (none yet)

## Definition of Done (integration)

- [ ] Every V-1…V-8 executed with evidence recorded
- [ ] Evidence traced to R-n; deviations documented
- [ ] Spec, code, and roadmap (synced by orchestrator) agree — merge as one change
