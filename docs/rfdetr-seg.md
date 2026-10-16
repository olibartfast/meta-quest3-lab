# RF-DETR-Seg

`RFDETRSegSmall` (384x384) serves the segmentation contract from Triton:
the headset captures and preprocesses, the host runs the model, and the
app decodes masks and solves screen quads and poses locally.

## Model contract

`QUESTLAB_RFDETR_MODEL_V1` accepts variant `seg-small` with a third
output: `masks`, float32 `[1, 100, 96, 96]`, meaning
`per_query_instance_masks`. Box-only manifests are unchanged
(`hasMasks == false`). Mask attach follows `rf-detr-cpp-inference`
(`olibartfast/rf-detr-cpp-inference`, `postprocess_segmentation_outputs`):
the kept detection's query slice is nearest-neighbour upsampled to source
pixels through pixel centres (`u = (g + 0.5) * W/gw - 0.5`), and
`logit > 0.0` on the raw logits keeps the pixel.

Nearest-neighbour upsampling is deliberate: interpolating logits would
invent soft edges that the threshold then turns into arbitrary boundary
decisions. With nearest sampling every output pixel's decision comes from
a real model logit, at the cost of a 13.3 px grid step at 1280 wide.

## Mask compression

`EncodeMaskRle` / `DecodeMaskRle` in `postprocessing.h` compress binary
masks as per-row `(runLength, value)` pairs. Row restarting keeps a
truncated tail to one row instead of desynchronizing the whole mask;
decoding needs no width knowledge to resynchronize rows. The encoder and
decoder are exercised together in the test suite so the two ends cannot
drift.

## Corners from masks

`libs/screen_corners` (OpenCV-free, mirroring `tools/monitor_corners`):

- largest 8-connected component of the mask,
- Moore boundary tracing, then Douglas-Peucker at 2% of the perimeter,
- exactly four points, convex, bbox >= 32 px, longer/shorter aspect in
  1.1-2.6 (portrait and rolled screens pass; squares still fail),
- border refusal: quads touching the frame edge are clipped screens with
  wrong poses, never valid output,
- centroid-angle cycle ordering with TL-first start (robust cycle under
  roll; the start still assumes a roughly upright camera - a best-reproj
  rotation search was tried and reverted because with 4 points every
  rotation fits exactly, so it flipped upright poses 180 deg),
- planar P4P from the object-plane homography in undistorted normalized
  coordinates (`H = s [r1 r2 t]`, K = I after Brown-Conrady + skew
  inversion, symmetric orthonormalization, front-facing branch) with
  reprojection RMS through the full distort+K model,
- Shepperd quaternion branches (the screen sits at trace = -1, w ~= 0
  nominal); any future smoothing/slerp must hemisphere-check
  (`if dot(q, q_prev) < 0: q = -q`) since the sign flips frame to frame,
- facing/grazing refusal: the plane normal must face the camera.

Poses are solved only when the quad rectangularity score >= 0.70 and the
reprojection RMS <= 8 px; tens of pixels mean the quad is not this class
size, so the pose is refused, not drawn.

There is no subpixel refinement yet: a binary mask has no gradients, so
pixel vertices are the measurement. The score is the quad share of the mask
foreground (rectangularity); refusals carry reasons and empty masks fail
closed. Verified on synthetic tilted quads (corners < 3 px, sub-pixel
reprojection on exact data), yaw +/-20 deg sign regression, distorted-model
round-trip, border/portrait gates, and end-to-end from a rasterized mask.

## Headset rendering

A detection with a gated plane draws a cuboid built from the seg plan:
the front face sits on the solved screen plane and the body extrudes
backward by a display thickness (5 cm), replacing the depth-fused cuboid
for that detection. Range is purely the `class_sizes` prior, so the cuboid
is tinted cyan as a prior, never the measured green; a `SEG_SCALE_MISMATCH`
warning logs when the prior range disagrees with the fused depth range by
more than 0.15 m + 15%. The 2D diagnostic preview outlines the quad
(TL to TR to BR to BL) in place of the axis-aligned box. Poses are solved
for the classes listed in `class_sizes` (default `tv:0.55,0.32`); every
masked detection still gets a quad.

## Open work

1. Deploy validation on the headset (seg manifest + `--class-sizes` for
   the actual panel), thermal and accuracy acceptance.
2. Panel-specific size/aspect tuning: reprojection RMS in the tens of
   pixels means the quad's shape does not match the `class_sizes` prior
   (now refused at 8 px instead of drawn).
3. Accuracy: subpixel isoline corners (marching squares + edge line fits),
   IPPE two-fold disambiguation (depth normal or temporal), LM refinement
   on the true reprojection error, ray-plane metric corners to drop the
   size prior, per-track SE(3) EMA filtering.
