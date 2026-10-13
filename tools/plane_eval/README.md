# Ocean plane-refinement spike

Asks "algorithm or data?": our RANSAC plane normal (A) vs ours seeded into
Ocean's `NonLinearOptimizationPlane::optimizePlane` with Huber (B), on
identical noisy tilted slabs with clutter. Ground-truth normals are known,
so angular error is exact.

```bash
third_party/ocean/fetch_ocean.sh
cmake -S tools/plane_eval -B build/plane-eval
cmake --build build/plane-eval --parallel
./build/plane-eval/plane_eval
```

Verdict rule (printed by the tool): refinement gains < 1° everywhere →
data-dominated, the solver is not the fix (correspondences/sync are).
Consistent gains ≫ 1° → enable `useOceanRefinement` behind
`QUESTLAB_USE_OCEAN` (already wired in `libs/detection_fusion`, default
OFF so the device APK is unaffected).

Ocean sources stay in `build/ocean-src` (pinned commit in
`fetch_ocean.sh`) and are never committed.
