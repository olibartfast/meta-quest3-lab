# Lesson 4 - RF-DETR as Set Prediction

## What you will learn

What a DETR-family detector actually outputs, how raw logits become
detections, and why there is no NMS anywhere in this repository.

## DETR in one paragraph

Classic detectors predict thousands of anchor boxes and then remove
duplicates with non-maximum suppression (NMS). A DETR-family model
(*DEtection TRansformer*) instead learns a **fixed set of queries**: the
decoder has, say, 300 learned "slots", and is trained so that each slot
either matches exactly one object in the image or matches nothing. The
training loss is a bipartite matching (Hungarian) between slots and
ground-truth objects, so the network is explicitly optimized to *not*
duplicate. Inference is then a single pass over slots: no anchors, no
NMS, no threshold tuning between stages.

RF-DETR (Roboflow) is a modernized DETR with a lightweight transformer
decoder over a DINOv2 backbone, and is what the manifests in
`tools/rfdetr_export/` pin: nano (300 queries), medium (300 queries),
seg-small (100 queries plus masks).

## What the model actually emits

Per frame, for `Q` queries:

```text
dets   [1, Q, 4]   box parameters per query
labels [1, Q, 91]  class logits per query, one slot per COCO category id
masks  [1, Q, 96, 96]  (seg variants only) raw mask logits per query
```

Crucially these are **raw logits**, not probabilities and not boxes:
- class scores must be pushed through a sigmoid;
- the four box numbers are normalized `(cx, cy, w, h)` relative to the
  input tensor, and can be negative or exceed 1 before mapping.

The 91 logit slots are COCO's *sparse* category ids: there is no class 12
(COCO has gaps). The manifest carries the id-to-name table; the
postprocessor indexes by slot position, then maps the slot to its id for
the wire.

## From logits to detections

`PostprocessDetections` in `tools/rfdetr_inference/src/postprocessing.cpp`
implements exactly the contract the manifest declares
(`global_top_k_query_class_pairs`, threshold 0.5, maximum 300, `nms: false`):

1. **Sigmoid** every one of the `Q x 91` logits (30,000 values for nano;
   9,100 for seg-small).
2. **Global sort** over query-class pairs by score, ties broken by flat
   index, so results are deterministic.
3. **Take the top K = min(300, candidates)**, then drop everything at or
   below 0.5. Note the order: the threshold acts after the top-K cut, so
   "300" is a ceiling on detections, not on candidates.
4. For each surviving pair, unpack `(cx, cy, w, h)` for that query and
   map to source pixels:

   ```text
   x0 = clamp((cx - w/2) * sourceWidth,  0, sourceWidth)
   y0 = clamp((cy - h/2) * sourceHeight, 0, sourceHeight)
   x1 = clamp((cx + w/2) * sourceWidth,  0, sourceWidth)
   y1 = clamp((cy + h/2) * sourceHeight, 0, sourceHeight)
   ```

   The clamps matter: a partially out-of-frame object keeps its visible
   part instead of producing a coordinate outside the image, which
   downstream unprojection would happily accept.
5. Attach the class name via the manifest table. A logit slot with no
   entry is an error, not a silently-dropped detection: the contract is
   wrong and should fail loudly.

## A worked example

Take the `tv` detection from the log:

```text
class_id=72 class=tv confidence=0.727
box=[671.8 511.9 845.4 656.5]      (1280x1280 source)
```

`class_id=72` is the COCO sparse id for "tv". Internally the slot index
was found by scanning the manifest's class table; confidence is
`sigmoid(0.98)` (the raw logit was about 0.98), and the box is the
clamped source mapping of the query's `(cx, cy, w, h)`.

## Why no NMS is a feature

- **Reproducibility**: NMS has an IoU threshold that interacts with
  dataset statistics; a set-prediction decoder has one inference path.
- **Latency**: NMS on 300 boxes is cheap, but the whole anchor machinery
  it implies is not - and the headset runs the decode stage on a tight
  budget while inference itself lives on the host.
- **Semantics**: two *genuinely* overlapping objects (a cup on a book)
  are not merged by a matching-trained decoder, whereas a low NMS
  threshold would merge them.

The price is training complexity and the query budget: a scene with more
objects than queries cannot express all of them. Nano/medium cap at 300;
seg-small at 100.

## Failure modes to recognise

- **Everything at ~0.5**: you are reading logits as probabilities
  (`sigmoid(0) = 0.5`). The threshold is applied after the sigmoid.
- **Boxes clustered at one corner**: `(cx, cy, w, h)` misinterpreted as
  `(x0, y0, x1, y1)`.
- **Off-by-half-pixel everywhere**: preprocessing mismatch (lesson 3).
- **Duplicate boxes**: you are not running the pinned postprocess (some
  other decoder), or the wrong *variant* of the model is deployed - the
  manifest identity check exists to catch that.

## Exercises

1. Compute `sigmoid(z)` for z = -4, 0, 4 and explain where the 0.5
   threshold sits in logit space.
2. For seg-small (100 queries), what is the maximum number of detections
   the contract can ever produce, and which manifest field sets it?
3. In `postprocessing.cpp`, find the tie-break rule and explain why
   determinism matters for the oracle comparison tests.
