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

## Remaining slices

1. Pin the seg-small checkpoint and extend `export_model.py`
   (`RFDETRSegSmall`, 384) plus `inspect_onnx` for three outputs.
2. Service: `RunWithMasks` + RLE mask payload on the streaming
   protocol (box-only clients fail closed, as today).
3. Client + app 10: consume masks; fit the monitor quad from the
   `tv` mask instead of Canny edges (`tools/monitor_corners`).
4. On-device seg only after host validation and latency budget.
