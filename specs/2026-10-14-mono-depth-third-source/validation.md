# Validation — Monocular Depth as Third Range Source

Defined BEFORE implementation (SDD).

| ID | Requirement | Check |
|----|-------------|-------|
| V-1 | R-1 provenance | Unit: `Learned` casts to 3, existing 0/1/2 unchanged; `CheckLearnedAgreement` matrix (env-agree→env kept, env-invalid→FallbackLearned, both-invalid→Hide, disagreement→Hide) |
| V-2 | R-2 alignment | Unit: synthetic plane recovers scale/shift to 1e-4; 30% outliers rejected; collinear/single-point input refused with reason |
| V-3 | R-3 discipline | Code review: tint branch for Learned in app 10; C API comment lists 3=Learned; confidence-factor constant present with caller rule |
| V-4 | Regression | Full `detection_fusion` + `quest_vision` host suites green; `CheckAgreement` behavior byte-identical |

## Evidence
- 2026-10-14 (host): `detection_fusion` suite green incl. new wire/agreement/
  alignment/refusal blocks, `-Wall -Wextra -Wpedantic` clean;
  `quest_vision` standalone configure+build green, both suites
  (`quest_vision_c_api_tests`, `rfdetr_inference_tests`) pass — existing
  `CheckAgreement`/C-API behavior byte-identical (no existing test touched).
- NOT run: device (no headset here); M2 backend serving (Triton config,
  weights, crop protocol Q-1) is a separate slice with its own packet.
