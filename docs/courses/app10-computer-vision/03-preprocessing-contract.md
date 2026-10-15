# Lesson 3 - The Preprocessing Contract

## What you will learn

Exactly how a camera capture becomes the model's input tensor, and why
every constant comes from the manifest instead of the code.

## Why preprocessing is part of the contract

A detector is only valid for the input distribution it was trained on:
same color order, same resize rule, same normalization, same tensor
layout. If any of these differ between host evaluation and the device,
you are silently feeding the model an image it has never seen. The
repository therefore treats preprocessing as a pinned contract:

```json
"input": {
  "name": "input",
  "shape": [1, 3, 384, 384],
  "layout": "NCHW",
  "source_color_order": "RGB",
  "scale": 0.00392156862745098,
  "normalization": { "mean": [0.485, 0.456, 0.406],
                     "std":  [0.229, 0.224, 0.225] },
  "resize": { "mode": "squash", "preserve_aspect_ratio": false,
              "interpolation": "bilinear", "coordinate_transform": "half_pixel" }
}
```

Note `scale = 1/255` and the ImageNet mean/std - these are not accidental
choices; they are transcribed from the RF-DETR export pipeline so that
the tensor the device feeds the ONNX model is bit-comparable to the
pinned oracle's.

## The four steps

`PreprocessCapture` in `tools/rfdetr_inference/src/preprocessing.cpp`
performs, in order:

1. **RGBA conversion** (done once per frame; reused by the diagnostic
   overlay so both draw from identical pixels).
2. **Squash resize** to 384x384 (or 576x576 for medium). No letterboxing:
   the aspect ratio is deliberately destroyed so the full sensor is
   mapped onto the full tensor.
3. **Bilinear sampling** with half-pixel mapping:

   ```text
   srcX = (dstX + 0.5) * (srcWidth / dstWidth) - 0.5
   ```

   For a 1280 source and 384 target, `scaleX = 3.333...`: target column
   192 samples source column `(192.5)*3.333 - 0.5 = 641.17`, blending
   columns 641 and 642 by 0.17. Half-pixel alignment makes the resize
   exactly invertible in expectation - a centre-sampling (`dstX * scale`)
   formula shifts everything by half a source pixel.
4. **Normalization**, per channel and in RGB order:

   ```text
   value = (pixel/255 - mean[c]) / std[c]
   ```

   Pixel 255 in red becomes `(1.0 - 0.485)/0.229 = 2.249`.

The output is a contiguous `[1,3,384,384]` float array, written in NCHW
order, which is what both the ONNX session and the Triton request carry.

## Squash is not a shortcut - it is required by the mapping

Because the tensor is squashed, a box predicted at normalized
coordinates `(cx, cy, w, h)` in tensor space maps back to source pixels
simply:

```text
centerX_src = cx * sourceWidth
width_src   = w  * sourceWidth
```

No letterbox offsets, no per-axis ratios: the squash makes the tensor
coordinate system affinely identical to the source frame. That is why
`squash` is pinned and `preserve_aspect_ratio` is false. The cost is
that objects get stretched in the tensor: a 1280x1280 sensor into
384x384 is isotropic, but a 1280x720 frame would be deformed 1.78x
horizontally - RF-DETR was trained on a squash-resize pipeline, and the
same deformation exists in its training data.

## What to check when results look wrong

- **Colors swapped**: R and B exchanged (a classic OpenGL/Android
  confusion) - the model still detects things but with degraded scores.
- **Half-pixel off**: everything looks right but IoU against the oracle
  droops slightly.
- **Wrong mean/std**: scores drop globally and class confusion rises.
- **Strides ignored upstream**: diagonal shearing; look at lesson 2.

The tests for this stage run against a checksummed replay fixture and
assert byte-identical outputs to the pinned converter, which is the only
practical way to catch all four at once.

## Exercises

1. Compute the source coordinates for target pixel (0, 0) and
   (383, 383) at scale 3.333. Which source pixels are *never* sampled?
2. In the fixture-based test, change `interpolation` to `nearest` in a
   scratch manifest copy and observe how many detections change.
3. Explain why the diagnostic preview can only be trusted for pixel
   positions (it draws on the RGBA buffer) but not for colour balance
   decisions about the model input.
