# Plan: pose_solver

Run from repo root. TDD-shaped per decision 2: tests + contract header + stub
first, observe red, implement group by group to green, record red→green.

## Interface sketch (exact contract)

Header: `libs/pose_solver/include/pose_solver/pose_solver.h`
Namespace: `questlab::pose`

```cpp
namespace questlab::pose {

struct Intrinsics {
    float fx = 0.0F;   // focal length, pixels
    float fy = 0.0F;
    float cx = 0.0F;   // principal point, pixels
    float cy = 0.0F;
};

struct Correspondence {
    math::Vec3 objectPoint{};  // metres, object frame
    math::Vec2 pixel{};        // pixels, Camera2 basis
};

struct Options {
    float reprojectionThresholdPx = 3.0F;   // RANSAC inlier threshold
    int maxIterations = 1000;               // cap (adaptive early-exit applies)
    uint32_t seed = 0x9E3779B9U;            // fixed default
    int minInliers = 6;                     // minimum consensus
    bool outputInlierMask = false;          // opt-in to N-byte mask
};

struct Result {
    math::Pose pose{};                    // camera-from-object
    int inlierCount = 0;
    std::vector<uint8_t> inlierMask{};    // empty unless outputInlierMask
    float rmsReprojectionPixels = 0.0F;   // RMS over inliers
};

bool SolvePose(
    const Correspondence* correspondences,
    int count,
    const Intrinsics& intrinsics,
    const Options& options,
    Result* result,
    std::string* error);

}  // namespace questlab::pose
```

Reason strings (exact, asserted by tests):

- `"invalid pose input"` — null output, bad options, non-positive/non-finite fx/fy.
- `"non-finite correspondence"` — any object point or pixel non-finite.
- `"insufficient correspondences"` — count < 6.
- `"degenerate sample geometry"` — collinear/duplicate sampled minimal set.
- `"no consensus"` — no inlier set ≥ options.minInliers.
- `"refinement failed"` — the initial consensus pose is unusable (a point
  behind the camera). A non-improving or singular Gauss-Newton step stops the
  iteration at the best-so-far pose; that is convergence, not an error.

Input-validation order (deterministic, exact semantics):

1. `count < 6` → `"insufficient correspondences"`.
2. null result pointer (nothing to clear) or null correspondences pointer
   (with `count >= 6`) → `"invalid pose input"`; a non-null `Result` is
   cleared here, so every false path leaves a cleared, finite result.
3. any non-finite object point or pixel → `"non-finite correspondence"`.
4. `"invalid pose input"` — non-finite/non-positive fx or fy, non-finite cx or
   cy, non-positive reprojectionThresholdPx, `maxIterations < 1`,
   `minInliers < 3`.
5. `"degenerate sample geometry"` — global pre-check: fewer than 3 unique object
   points or all object points collinear within a scale-relative tolerance.
6. `"no consensus"` — no inlier set ≥ `options.minInliers`, re-checked after
   the final refit (a collapsed final consensus fails closed too).
7. Gauss-Newton stops at the best-so-far pose when a step is singular or
   non-improving; `"refinement failed"` is reserved for an unusable initial
   consensus pose (a point behind the camera).

The null result pointer is rejected first; with a non-null result, the
`Result` is cleared before any other check so every false path leaves a
cleared, finite result. No NaN/Inf may remain in any output field on any
path, false included. RANSAC also skips degenerate minimal samples
(duplicate or collinear within the sample) internally.

Type choice: depend on `libs/xr_math` for `Vec2/Vec3/Quat/Pose` (the repo's
geometry vocabulary; portable, host-tested), wired via the
`libs/detection_projection` pattern (`if(NOT TARGET xr_math) add_subdirectory`
then `target_link_libraries(... PUBLIC xr_math)`). Keep a local minimal
`Intrinsics` (fx/fy/cx/cy, mirroring `libs/screen_corners`) rather than pulling
in `camera_source`'s heavier `CameraIntrinsics`. Result pose is `math::Pose`
(Quat x,y,z,w + Vec3 position) so downstream app-10 wiring composes directly;
the quaternion ordering matches `libs/screen_corners`.

## Proposed implementation approach (steer, adjustable)

1. Pre-check inputs; keep an index array for RANSAC sampling.
2. Normalize object points and pixels (Hartley: centroid to origin, mean
   distance to a unit scale); fold `K^-1` into the pixels.
3. RANSAC with a deterministic splitmix64 PRNG (seeded from `options.seed`)
   and adaptive iteration count. Each hypothesis draws 6 correspondences and
   rejects a degenerate sample (collinear object points, duplicate points, or
   near-collinear pixels) before solving.
4. Minimal solver: 6-point Hartley-normalized DLT, with two sample-geometry
   paths (deviation D-1, recorded in `validation.md`):
   - Non-coplanar cloud: build the 12×12 normal matrix, take the smallest
     eigenvector via the self-contained symmetric Jacobi eigensolver, and
     reshape it into the 3×4 projection P. DLT resolves P only up to sign and
     scale, so (a) negate the whole 12-vector so the sampled points' camera
     depths are positive (cheirality, whole-vector flip — never rotation and
     translation separately), (b) scale columns 0/1 of the 3×3 block — they
     carry the metric scale λ — to unit length, (c) complete column 2 by
     their cross product so the block is orthonormal with det = +1 by
     construction (deviation D-4: the originally sketched Frobenius-√3
     scaling plus polar iteration was not needed), and use the last column,
     rescaled by the same λ, as translation.
   - Coplanar cloud: the 12-parameter DLT is degenerate for planar samples
     (the second projection column is unconstrained, so the smallest
     eigenvector is arbitrary). Project the object points once onto a PCA
     plane basis (centroid plus right-handed u/v/n) and solve the 9×9 plane
     homography DLT instead; decompose H = λ[R·u, R·v, R·centroid + t] with
     the same cheirality/scale/orthonormalization discipline, compose the
     in-plane rotation back through the basis, and translate by the centroid.
   Both paths share the RANSAC, scoring, and Gauss-Newton stages; the normal
   matrix for any n×n Jacobi block must be a contiguous n×n array because
   `JacobiEigen` indexes it with row stride n.
5. Score each hypothesis against all points (reprojection < threshold px);
   keep the best inlier set.
6. Gauss-Newton refinement on inliers (analytic Jacobian preferred, numeric
   acceptable), then final refit on all inliers; recompute inlier mask, count,
   and RMS reprojection.
7. Fail-closed on every path; no NaN/Inf may reach a Result.

EPnP / P3P are future optimizations (out of scope): the Jacobi DLT is simpler,
dependency-free, and sufficient for the R-2/R-3 budgets.

## Phase P-1 — Red: contract + tests + stub

- T-1 Write `libs/pose_solver/include/pose_solver/pose_solver.h` (contract
  above) and `libs/pose_solver/CMakeLists.txt` (mirror `detection_projection`:
  standalone project + CTest + xr_math wiring).
- T-2 Write `libs/pose_solver/tests/pose_solver_tests.cpp` with one test group
  per requirement: exact recovery (coplanar + non-coplanar, N in {6, 40, 200}),
  noise, outliers, scale/determinism, degeneracy, performance.
- T-3 Write `libs/pose_solver/src/pose_solver.cpp` as a stub returning false.
- Deliverable: builds clean; `ctest` runs and each group fails (assert aborts).
- Acceptance: record the red run (command + failing group list) in
  `validation.md` evidence.

## Phase P-2 — Green: implement group by group

Each group implements only that slice, runs its test to green, records evidence.

- G-1 Exact recovery (R-2). DLT + Jacobi eigensolver + orthonormalization +
  denormalization; coplanar and non-coplanar pass at R-2 tolerances.
- G-2 Noise (R-3). GN refinement lands noisy data within R-3 bounds.
- G-3 Outliers (R-4). RANSAC loop + adaptive iterations + deterministic PRNG;
  mask agreement ≥ 95% at 50% outliers.
- G-4 Scale + determinism (R-5). Assert the scale property and bit-identical
  repeats.
- G-5 Degeneracy (R-6). Input/pre-check/consensus failures return exact reason
  strings; no NaN.
- G-6 Performance (R-7). Record wall time under defaults; assert the budget.
- Deliverable: all `pose_solver_tests` green.
- Acceptance: red→green recorded per group.

## Phase P-3 — Hardening

- T-1 Address reviewer findings; keep the packet in sync if the contract or
  approach deviated (update requirements/plan/validation in the same branch).
- T-2 Add the CI step to `.github/workflows/android-ci.yml`, after
  "Test detection projection geometry":

  ```yaml
  - name: Test pose solver geometry
    run: |
      cmake -S libs/pose_solver -B build/pose-solver-tests -DBUILD_TESTING=ON
      cmake --build build/pose-solver-tests --parallel
      ctest --test-dir build/pose-solver-tests --output-on-failure
  ```

- T-3 Fill `validation.md` evidence (dates, commands, results); confirm
  warning-clean build and PIC.

## Integration checkpoint

- CI step above added and green on this branch.
- Proposed roadmap item (orchestrator syncs into `specs/roadmap.md`):

  ```text
  ## Phase N — Matcher-driven 6-DoF object pose
  - Status: planned
  - Outcome: app-10 renders a metric 6-DoF box for a non-coplanar object from
    matcher 2D↔2D correspondences lifted to 2D↔3D.
  - Proves: matcher + template/SfM 3D reference + libs/pose_solver PnP compose
    into measured 6-DoF (draw only what is measured).
  - Depends on: libs/pose_solver (this packet), template scanning/SfM, matcher
    runtime.
  - Spec: specs/2026-10-16-pose-from-correspondences/  Branch: spec/pose-from-correspondences
  ```

- The orchestrator edits `specs/roadmap.md`; this packet does not touch it.

## Notes

- Mirrors: `libs/screen_corners` (fail-closed + reason strings, self-contained
  pose), `libs/detection_projection` / `libs/detection_fusion` (xr_math wiring).
- The solver is a device-capable geometry lib; Triton-side inference and
  on-device postprocessing stay separate — no inference code lives here.
