# Requirements: pose_solver — metric 6-DoF pose from 2D↔3D correspondences

Spec: `specs/2026-10-16-pose-from-correspondences/requirements.md`
Branch: `spec/pose-from-correspondences` · Roadmap: new matcher phase (proposed,
not yet in `roadmap.md` — orchestrator syncs at integration)

## Objective

Deliver `libs/pose_solver`, a portable C++17 library that recovers the metric
6-DoF pose (object frame in camera frame) of a rigid object with known 3D
structure from 2D↔3D point correspondences (object points in metres, image
points in pixels), robust to a majority of outliers via RANSAC. It is the
geometry layer a matcher-driven or depth-lifted 6-DoF path consumes. A 2D↔2D
matcher (LightGlue / SuperGlue-class / MatchAnything) cannot output 6-DoF by
itself: it needs a 3D reference side (a scanned or SfM template model) or
depth/motion, then PnP. This solver is that PnP step. Matcher models, template
scanning/SfM, and app-10 wiring are out of scope.

Constitution trace: `specs/mission.md` (draw only what is measured — the
solver returns a measured pose plus its reprojection RMS, never a guessed
range), `specs/tech-stack.md` (C++17 `-Wall -Wextra -Wpedantic`, host CTest
with `assert`, NDK r29, no third-party engine), `specs/roadmap.md` (enables a
later matcher phase; proposed item text in `plan.md`).

## User decisions (2026-10-16 interview)

1. Single feature now: solver first; matcher and template/SfM work later.
2. TDD mandated: validation defined before implementation; the plan is
   TDD-shaped (contract header + tests + stub first, observe each test group
   go red, implement to green, record red→green evidence).
3. Conventions: pose = object frame in camera frame (camera-from-object),
   quaternion (x, y, z, w) + position in metres; Camera2 basis +X right,
   +Y down, +Z forward; metres/pixels — matching `libs/screen_corners`. The
   caller supplies 3D object points; non-coplanar AND coplanar sets must both
   solve through one path (no separate planar branch).
4. Determinism: fixed default seed, no hidden global state, identical inputs →
   identical (bit-identical) outputs.
5. API style: `bool Function(..., std::string* error)` fail-closed with short
   reason strings in the style of `libs/screen_corners`; no exceptions across
   the API.

## Functional requirements

- R-1 Contract/scope. `bool SolvePose(const Correspondence*, int count,
  const Intrinsics&, const Options&, Result*, std::string* error)` over
  `questlab::pose` types: inputs = 2D↔3D correspondences + pinhole intrinsics
  (fx, fy, cx, cy); outputs = pose, inlier count, optional inlier mask, RMS
  reprojection in pixels; fail-closed reason strings on every false.
- R-2 Exact recovery. Noiseless synthetic data, non-coplanar and coplanar
  point sets, N from 6 to ~200, position error ≤ 2 mm at ~1 m scale, rotation
  error ≤ 0.1° (first-measurement targets).
- R-3 Pixel-noise robustness. Gaussian σ = 0.5 px on ~N=40 correspondences at
  ~1 m, position error ≤ 15 mm, rotation error ≤ 1°.
- R-4 Outlier robustness. ≥ 50% outliers: correct pose within the R-3 noise
  bounds, and the returned inlier mask agrees with ground-truth inliers ≥ 95%.
- R-5 Metric scale + determinism. Scaling all object points by k scales the
  recovered position by k (rel. ≤ 1e-3) and leaves rotation unchanged; two runs
  with identical inputs are bit-identical.
- R-6 Fail-closed degeneracy. Fewer than 6 correspondences, collinear or
  duplicate sampled geometry, invalid intrinsics, and no consensus each return
  false with an exact reason string and no NaN/Inf in any output field.
- R-7 Performance budget. Host wall time under defaults (N ≈ 100, 50%
  outliers) well under 500 ms; the recorded measurement is the evidence.
- R-8 Integration readiness. CMake mirroring repo conventions (standalone +
  `BUILD_TESTING` + xr_math wiring), CTest assert style, a CI step added to
  `.github/workflows/android-ci.yml` following the existing "Test …" step
  pattern, warning-clean under `-Wall -Wextra -Wpedantic`, PIC, float32.

## Constraints

- C-1 C++17, `-Wall -Wextra -Wpedantic`, no third-party dependencies, no
  exceptions across the API, no hidden global mutable state.
- C-2 Host-testable standalone (CMake) and later usable from Android libs;
  `POSITION_INDEPENDENT_CODE ON`; float32 storage throughout.
- C-3 Fail-closed: an empty answer is correct output, never a guess (matches
  `libs/screen_corners`).
- C-4 Deterministic: fixed default seed; the PRNG state is per-call, never
  global.

## Out of scope

- Matcher models (LightGlue / SuperGlue / MatchAnything) and their runtime.
- Template scanning / SfM / 3D reference generation for object models.
- Depth lifting (stereo / environment depth) and app-10 wiring.
- EPnP / P3P / minimal-solver optimizations (future work, see `plan.md`).
- These land in a proposed new roadmap item ("matcher-driven 6-DoF object
  pose"); text sketched in `plan.md`, synced into `specs/roadmap.md` by the
  orchestrator.

## Open questions

- O-1 Numeric tolerances in R-2/R-3/R-4 are first-measurement targets; tighten
  after the first recorded run. Owner: implementer.
- O-2 Inlier mask allocation on the device path: `Result::inlierMask` is a
  heap vector for now; switch to a caller-supplied buffer only if device
  profiling demands. Owner: implementer.

## Definition of Done (requirements level)

- [ ] Every R-1…R-8 implemented or explicitly deferred with a tracked location
- [ ] No out-of-scope work smuggled in; no in-scope behavior silently dropped
