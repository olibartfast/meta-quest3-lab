# Requirements — Monocular Depth as Third Range Source

Spec: `specs/2026-10-14-mono-depth-third-source/` · Libs: `detection_fusion`, `quest_vision` (C API) · App 10 (tint only)

Source: reviewer memo 2026-10-14 (monocular-depth evaluation). Verified
against this checkout 2026-10-14: `RangeSource {Stereo, EnvDepthFallback,
Prior}` (`detection_fusion.h:88`), `CheckAgreement` stereo-primary
(`detection_fusion.cpp:481`), C API casts enum to int 0/1/2
(`quest_vision_c_api.cpp:88`), fallback tint precedent at app 10
(`main.cpp:1218-1228`), stereo P1B+P3O open (`PASS_WITH_DEBT`).

## Goal
Add learned monocular depth as a disciplined third source: never primary over
a measured source, always flagged, always compared — so G-3/G-4/G-8 can score
env vs mono vs stereo on the monitor fixture and decide its role (metric aid
vs segmentation densifier).

## In Scope
- [R-1] `RangeSource::Learned` appended AFTER `Prior` (wire values 0/1/2
  frozen; Learned=3; C API comment updated). Selection rule: Learned never
  outranks Stereo or an agreed Env source; it is a fallback with reduced
  confidence and a comparison source. New `AgreementVerdict::FallbackLearned`
  + `CheckLearnedAgreement(...)` host-tested.
- [R-2] Affine-in-disparity alignment to env depth (memo tranello #1):
  `disparity = scale * (1/depth) + shift`, robust RANSAC/Huber fit over valid
  env pixels only; refusal on degenerate geometry. Pure function in
  `detection_fusion`, host-tested (exact recovery, outlier robustness,
  degenerate refusal). Without this the mono normal is unusable — mandatory,
  not optional.
- [R-3] Mission discipline (memo tranello #3): Learned boxes render with
  their own tint (distinct from green stereo / amber env-fallback) +
  `kLearnedConfidenceFactor` applied by the caller at fallback selection;
  C API wire mapping extended. A Learned box must never be silently primary.
- [R-4] Backend contract (specified, not built here): Triton streaming,
  box-crop input, PromptDA-with-env-prompt as the recommended variant;
  ViT-S on Quest CPU explicitly rejected on cost grounds. Model serving,
  weights, and Triton config are out of scope for this slice.
- [R-5] Validation procedure: three-way comparison (env vs mono vs stereo)
  on the monitor fixture scoring G-3 (normal ≤7° bar for mono to qualify as
  metric aid), G-4, G-8. If the monitor normal clears ~7° on mono only, it
  earns the metric-aid role; otherwise it stays a densifier.

## Out of Scope
- Model weights/serving/Triton config; FoundationStereo/RAFT-Stereo (noted
  alternative, stereo-gate dependent); on-device mono inference; tracker/mask
  phases (separate packets).

## Constraints
- C++17, repo style; wire values frozen (static-asserted in tests);
  `detection_fusion` host tests green; no new runtime deps.

## Open Questions
- [Q-1] Exact crop protocol (enlarged-box margin, resolution) — owned by the
  backend slice, decided with G-3 numbers.
- [Q-2] Learned-vs-env disagreement rendering: Hide (like stereo/env) or
  degraded-show? Default Hide; revisit with numbers.
