# Validation: pose_solver

Defined before implementation (TDD). `[x]` passed · `[ ]` not run · `[!]` failed.
A tick with no number is not a result. Tolerances were tightened after the
first recorded run (O-1); the Run block below is the executable check.

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
| V-1 | `pose_solver_tests` builds; `TestContract` runs | header path `libs/pose_solver/include/pose_solver/pose_solver.h`, namespace `questlab::pose`, signature + defaults match `plan.md` interface sketch | `[x]` |
| V-2 | `TestExactRecoveryNonCoplanar` + `TestExactRecoveryCoplanar` (noiseless, N ∈ {6, 40, 200}) | position ≤ 2e-4 m (at ~1 m scale), rotation ≤ 0.05°, RMS ≤ 2e-4 px | `[x]` |
| V-3 | `TestNoise` (σ = 0.5 px, N ≈ 40, ~1 m) | position ≤ 8e-3 m, rotation ≤ 0.5° | `[x]` |
| V-4 | `TestOutliers` (≥ 50% outliers) | position/rotation within V-3 bounds; inlier-mask agreement ≥ 95% | `[x]` |
| V-5 | `TestMetricScale` + `TestDeterminism` | position scales by k (rel. ≤ 1e-3), rotation unchanged; repeat runs bit-identical | `[x]` |
| V-6 | `TestDegenerateAndRefusal` | `<6` → `"insufficient correspondences"`; collinear/duplicate → `"degenerate sample geometry"`; bad intrinsics → `"invalid pose input"`; non-finite input → `"non-finite correspondence"`; no consensus → `"no consensus"`; no NaN/Inf in any `Result` field | `[x]` |
| V-7 | `TestPerformanceBudget` (N ≈ 100, 50% outliers, defaults) | wall time recorded and well under 500 ms | `[x]` |
| V-8 | warning-clean build + PIC + CI step | no `-Wall -Wextra -Wpedantic` diagnostics; `POSITION_INDEPENDENT_CODE ON`; CI "Test pose solver geometry" step present and green | `[x]` |

## Evidence log

| ID | Command / check | Result | Date | Notes |
|----|-----------------|--------|------|-------|
| V-1 | `cmake -S libs/pose_solver -B build/pose-solver-tests -DBUILD_TESTING=ON` · `cmake --build build/pose-solver-tests --parallel` · `ctest --test-dir build/pose-solver-tests --output-on-failure` | `[x]` build clean, `TestContract` passes, `ctest` 1/1 green | 2026-10-16 | red run recorded below |
| V-2 | `build/pose-solver-tests/pose_solver_tests exact` | `[x]` non-coplanar ≤ 3.6e-7 m / 0.006° / RMS 4.5e-5 px; coplanar ≤ 2.4e-7 m / 0.0034° / RMS 2.1e-5 px (N ∈ {6, 40, 200}) | 2026-10-16 | bounds tightened after measurement |
| V-3 | `build/pose-solver-tests/pose_solver_tests noise` | `[x]` 2.517e-3 m / 0.0885°, RMS 0.608 px | 2026-10-16 | σ = 0.5 px |
| V-4 | `build/pose-solver-tests/pose_solver_tests outliers` | `[x]` mask 40/40 (100%); 2.795e-3 m / 0.1701° | 2026-10-16 | ≥ 95% required |
| V-5 | `build/pose-solver-tests/pose_solver_tests "metric scale"` · `… determinism` | `[x]` rel. 7.0e-9, rotation change 0.0126°; repeat runs bit-identical (pose/count/rms/mask) | 2026-10-16 |  |
| V-6 | `build/pose-solver-tests/pose_solver_tests degenerate` | `[x]` each refusal returns the exact reason string; cleared finite `Result` on every false path | 2026-10-16 |  |
| V-7 | `build/pose-solver-tests/pose_solver_tests performance` | `[x]` best-of-3 81.1 ms | 2026-10-16 | budget 500 ms, kept for CI headroom |
| V-8 | warning scan · `POSITION_INDEPENDENT_CODE` property · CI step | `[x]` zero warnings; PIC ON; "Test pose solver geometry" step added to `.github/workflows/android-ci.yml` | 2026-10-16 | CI run watched after push |

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

- D-1 Coplanar samples cannot use the 12-parameter DLT: for a planar sample the
  second projection column is unconstrained (the normal matrix's null space is
  4-dimensional), so the smallest eigenvector is arbitrary and no
  orthonormalization recovers the pose — empirically it always ended in
  `"no consensus"`. Fix: coplanar clouds project once onto a PCA plane basis
  and use a 9×9 plane-homography DLT, while non-coplanar clouds keep the 12×12
  DLT; both share RANSAC, scoring, and Gauss-Newton. `plan.md` item 4 records
  the two-path approach.
- D-2 Green-phase fix: `JacobiEigen` was fed the 9×9 homography normal matrix
  stored in a 12-wide array while the routine indexes `aIn` as an n×n
  row-major block — it read the wrong elements and the homography path
  collapsed. Fix: the block is a contiguous `double a[9][9]`; the stride
  requirement is stated in `plan.md` item 4.
- D-3 Gauss-Newton semantics: a singular or non-improving step now stops the
  iteration at the best-so-far pose instead of failing the whole solve, which
  noisy data made reachable. `"refinement failed"` is reserved for an unusable
  initial consensus pose.
- D-4 Non-coplanar rotation recovery: columns 0/1 of the DLT block are scaled
  to unit length and column 2 is completed by their cross product (orthonormal,
  det = +1 by construction) instead of the sketched Frobenius-√3 scaling plus
  polar iteration — the exact-recovery measurements confirm the simpler
  construction. `plan.md` item 4 records the implemented method.

## Definition of Done (integration)

- [x] Every V-1…V-8 executed with evidence recorded
- [x] Evidence traced to R-n; deviations documented
- [x] Spec, code, and roadmap (Phase 9a added to `specs/roadmap.md`) agree —
  merged as one change on `spec/pose-from-correspondences`
