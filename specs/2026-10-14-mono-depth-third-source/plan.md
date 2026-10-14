# Plan — Monocular Depth as Third Range Source

## Phase M1 — Provenance + gate + alignment (this slice, host-testable)
- `RangeSource::Learned` (appended, wire 3) + `AgreementVerdict::FallbackLearned`
  + `CheckLearnedAgreement(...)` in `libs/detection_fusion`.
- `AlignMonoDisparity(...)` robust scale+shift fit in `libs/detection_fusion`.
- `kLearnedConfidenceFactor` (0.5) caller rule, documented.
- C API comment + mapping note (`quest_vision_c_api.h`).
- App 10 Learned tint (violet/blue, distinct from green/amber).
- Host tests: agreement matrix, alignment exact/robust/degenerate, wire
  values frozen, confidence-factor application.
- Writable: `libs/detection_fusion/**`, `libs/quest_vision/include/**` (comment
  only), `apps/10-rfdetr-detection/src/main/cpp/main.cpp` (tint only).

## Phase M2 — Backend serving (separate slice, device)
Triton model config, box-crop protocol (Q-1), PromptDA variant, three-way
G-3/G-4/G-8 comparison runs. Not started here.

## Integration checkpoints
- After M1: `detection_fusion` + `quest_vision` host tests green; no behavior
  change for existing sources (Stereo/env agreement untouched).
