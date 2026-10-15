# Lesson 5 - Instance Masks

## What you will learn

Where the seg variant's per-object masks come from, how a 96x96 grid of
logits becomes a full-resolution binary mask, and what that costs.

## The masks output

`rfdetr_seg_small` adds a third output:

```text
masks [1, Q, 96, 96]  raw logits, one mask grid per query
```

Two things are easy to miss:

- These are **logits**, not 0/1: the decision boundary is 0.0, not 0.5.
  The reference implementation (`rf-detr-cpp-inference`'s
  `resize_threshold_mask`) thresholds raw logits at 0.0, and
  `PostprocessDetectionsWithMasks` uses the same convention
  (`maskThreshold = 0.0F` at the call sites).
- The grid is **not** the input resolution. For seg-small the encoder's
  mask head works at 96x96 while the image is 384x384: 16x coarser in
  each dimension. A 200-pixel-wide screen in the source frame is roughly
  15 grid cells wide.

## Decoding: only the detections you kept

The model computes 100 grids; a typical frame keeps one to ten
detections. The decoder:

1. runs the box/class pipeline from lesson 4;
2. for each **kept** detection, takes that query's 96x96 grid and
   nearest-neighbour upsamples it to the source resolution
   (1280x1280), thresholding at 0.0:

   ```text
   gridY = y * 96 / sourceHeight
   gridX = x * 96 / sourceWidth
   mask[y][x] = logit(gridY, gridX) > 0.0
   ```

   Nearest-neighbour (not bilinear) is deliberate: interpolating logits
   would invent soft edges that the 0.0 threshold then turns into
   arbitrary boundary decisions. With nearest sampling, every output
   pixel's decision comes from a real model logit.

3. stores a row-major `uint8` mask the size of the source frame.

The result rides along inside `Detection::mask` with `maskWidth` /
`maskHeight` so downstream stages can iterate it without knowing the
contract.

## What the upsampling does to corners

Because the grid is blocky, every mask boundary moves in steps of
`sourceWidth / 96` = 13.3 px at 1280. A screen whose true corner is at
(670.0, 512.0) can only be represented at grid-aligned positions. This
is the dominant error term for the screen-quad stage (lesson 6) and it
is why **subpixel refinement is intentionally absent**: there is no
gradient information in a binary blocky mask to refine against, and
smearing one in would produce a false sense of precision. The pipeline
instead reports the fit quality (quad score, reprojection RMS) and lets
the caller distrust it.

## The size problem, and RLE

A full-frame float mask at 1280x1280 is 6.5 MB; 300 queries would be
2 GB. Even one binary mask per detection is 1.6 MB; row-major RLE
(`EncodeMaskRle`, per-row run-length/value pairs) is the library's compact
mask representation.

```text
per row: (runLength, value) uint32 pairs
```

- Runs never cross a row boundary: a dropped tail corrupts one row's
  alignment, not the whole mask. Decoding therefore needs no width
  knowledge to resynchronize rows.
- The encoder lives in `postprocessing.cpp` (`EncodeMaskRle` /
  `DecodeMaskRle`); its twin on the client side is exercised by tests so
  the two ends cannot drift.

For the current architecture (host inference, app-side postprocessing) no
mask leaves the app: decoding and quad extraction happen in the same
process that received the inference response.

## When masks are not masks

- **Blur**: a moving hand makes the mesh soft; logits near 0.0 become
  noisy and the mask frays. Expect quad refusals, not wrong quads.
- **Glare**: a bright window inside a screen flips pixels above 0.0,
  punching holes. Largest-component selection (lesson 6) can then pick
  the wrong region.
- **Small objects**: under roughly 1 grid cell wide, the mask is a
  single block; the quad gates will reject it, correctly.

## Exercises

1. Compute the horizontal grid step for a 1280-wide source (96-wide
   grid) and estimate the best achievable corner accuracy, in pixels.
2. Write down an example where a glare hole changes the largest
   component of a mask and what the quad stage would report.
3. Find `maskThreshold` in the app's call sites and explain why 0.0 is
   the right value for raw logits.
