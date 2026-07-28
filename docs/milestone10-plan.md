# Milestone 10 — Offline RF-DETR Detection

## Goal

Run RF-DETR on recorded Quest camera frames through a reproducible host
pipeline, then reproduce the same detections with C++ ONNX Runtime.

This milestone has no live Quest networking, OpenXR timing, depth, or spatial
overlay. Its only new risk is correct RF-DETR deployment.

Reference:

- [RF-DETR export guide](https://rfdetr.roboflow.com/latest/learn/export/)

## Scope

Create:

- `tools/rfdetr_export`;
- `tools/rfdetr_inference`;
- a versioned model manifest;
- reference detections for approved Milestone 9 fixtures.

Exclude:

- live camera access;
- transport;
- Android inference;
- environment depth;
- model fine-tuning;
- TensorRT as the canonical artifact.

## Atomic implementation sequence

### Sequence 0 — Validate inputs

1. Verify the Milestone 9 fixture manifest.
2. Verify its pixel checksum.
3. Convert the stored frame to a viewable host image.
4. Confirm at least one RF-DETR-supported object is visible.

Gate: the input is approved, deterministic, and useful.

### Sequence 1 — Pin the model contract

1. Select an RF-DETR checkpoint appropriate for available host hardware.
2. Pin RF-DETR, Python, PyTorch, ONNX, and exporter versions.
3. Record the checkpoint source and checksum.
4. Record class IDs and class names.
5. Record model input dimensions.
6. Record RGB/BGR order.
7. Record normalization.
8. Record resize, aspect-ratio, and padding behavior.
9. Record confidence and maximum-detection defaults.

Gate: `model-manifest.json` fully specifies preprocessing and output meaning.

### Sequence 2 — Export and validate ONNX

1. Create the isolated export environment.
2. Download the pinned checkpoint.
3. Verify its checksum.
4. Export to ONNX.
5. Run the ONNX checker.
6. Inspect input and output names, types, and shapes.
7. Hash the ONNX artifact.
8. Record the export command and artifact hash.
9. Keep generated weights outside Git unless explicitly approved.

Gate: a clean environment can reproduce the same model contract.

### Sequence 3 — Save RF-DETR reference output

1. Run the official RF-DETR path on every approved fixture.
2. Save raw model outputs when practical.
3. Save postprocessed boxes in original-image coordinates.
4. Save class IDs and confidence.
5. Save annotated preview images.
6. Define numeric comparison tolerances.

Gate: expected results are machine-readable and visually reviewed.

### Sequence 4 — Implement C++ preprocessing

1. Load the fixture and manifest.
2. Convert YUV to the documented colour space.
3. Resize with the documented interpolation.
4. Apply the documented padding.
5. Normalize into the expected tensor layout.
6. Test intermediate tensor values against a saved reference sample.

Gate: C++ input tensor values match the reference within tolerance.

### Sequence 5 — Implement C++ inference and postprocessing

1. Load ONNX Runtime explicitly.
2. Validate model input/output metadata at startup.
3. Run one warm-up inference.
4. Run measured inference.
5. Decode RF-DETR outputs.
6. Undo padding and resize.
7. Clip boxes to the source image.
8. Reject invalid numeric output.
9. Apply confidence and count limits.
10. Write JSON and annotated image results.

Gate: C++ detections match the saved reference within stated tolerances.

### Sequence 6 — Regression and performance

1. Add tests for preprocessing.
2. Add tests for inverse letterbox mapping.
3. Add reference-output comparison tests.
4. Test empty detections.
5. Test corrupt model and manifest mismatch.
6. Report preprocessing time.
7. Report inference time.
8. Report postprocessing time.
9. Run all existing host tests.

Gate: offline inference is deterministic enough for CI and independently
measured.

## Definition of Done

- a pinned RF-DETR checkpoint exports reproducibly to ONNX;
- the model manifest defines the complete input/output contract;
- reference detections exist for real recorded Quest images;
- C++ ONNX Runtime output matches the reference within documented tolerance;
- annotated output visibly identifies real objects;
- timing is separated into preprocessing, inference, and postprocessing;
- no Quest connection, live transport, depth, or 3D rendering is required.
