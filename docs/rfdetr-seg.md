# RF-DETR-Seg rollout (streaming-host-first)

Target: `RFDETRSegSmall` (384x384, Apache-2.0) served from the host
`rfdetr_service`; the Quest keeps running the box-only on-device model
until the seg pipeline is validated end to end.

## Why streaming first

The seg ONNX carries a third `masks` output per query. On-device
XNNPACK mask decode plus full-frame mask transport is unmeasured;
the host service can decode and RLE-compress masks before anything
crosses `adb reverse`.

## Contract (landed)

`QUESTLAB_RFDETR_MODEL_V1` accepts variant `seg-small` with an
optional third output: `masks`, float32 `[1, 300, H, W]`,
meaning `per_query_instance_masks`. Box-only manifests are unchanged
(`hasMasks == false`). Mask attach follows
`rf-detr-cpp-inference` (`olibartfast/rf-detr-cpp-inference`,
`postprocess_segmentation_outputs`): the kept detection's query slice
is nearest-neighbour upsampled to source pixels, logit > threshold
(0.0 on raw logits) keeps the pixel.

## Wire format (landed)

`EncodeMaskRle` / `DecodeMaskRle` in `postprocessing.h`: per-row
`(runLength, value)` pairs. Row restart keeps a truncated tail to one
row. Service/client transport of RLE masks is the next slice.

## Corners from masks (landed)

`FindScreenQuadFromMask` (`tools/monitor_corners`): largest mask
contour -> `approxPolyDP` -> convexity + display-aspect gates ->
TL/TR/BR/BL order -> `SolveQuadPose` (AP3P + reprojection gate).
No `cornerSubPix`: a binary mask has no gradients, pixel vertices are
the measurement. Score is quad share of mask foreground
(rectangularity). Refusals carry reasons; empty masks fail closed.
Host-verified on a synthetic tilted screen: corners < 3 px,
sub-pixel reprojection, empty-mask refusal.

## Headset rendering (landed)

App 10 draws the served corner pose for any class: `ScreenRectangle`
face outline (W×H in the object XY plane) + `Axes` tripod at the
`LOCAL` pose, converted on the worker with the detection_projection
camera→LOCAL convention. Stale scenes draw nothing extra; the
depth-fused box path is untouched.

## Remaining slices

1. ~~Pin the seg-small checkpoint and extend `export_model.py`~~ done:
   `rfdetr-seg-small` (100 queries, masks 96×96, SHA pinned in
   `tools/rfdetr_export/seg-small-manifest.json`).
2. ~~Service RLE wire~~ superseded: service serves QLD2 quads + poses
   from seg masks; per-class planar sizes via `--class-sizes`
   (pose only for listed classes, quad for all).
3. Deploy validation on headset (`--backend streaming` + seg service
   + `--allowed-classes`), thermal + accuracy acceptance.
4. On-device seg only after host validation and latency budget.
