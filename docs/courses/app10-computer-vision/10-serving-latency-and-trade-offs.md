# Lesson 10 - Serving, Latency, and Trade-offs

## What you will learn

The three ways app 10 can run the model, how the protocol works on the
wire, and how latency budgets shape the pipeline's design.

## The backends

Inference is host-side by design: the headset never executes the model.
It captures, preprocesses, postprocesses, and renders; the tensors come
from a server.

| Backend | Model runs | Extra process | Typical inference | Use it for |
|---------|------------|---------------|-------------------|------------|
| `triton` | Triton server over HTTP | host + Triton | ~0.5-0.9 s (seg-small, CPU) | the only backend; accuracy, seg masks, iteration |

The app-side client for the Triton backend is `libs/triton_client`, a
dependency-free implementation of the KServe v2 HTTP protocol.

## The KServe v2 binary protocol in one page

Plain HTTP, but the body carries tensors efficiently:

```text
POST /v2/models/rfdetr_seg_small/infer?version=1
Content-Type: application/octet-stream
Inference-Header-Content-Length: <n>
Content-Length: <n + payload>

{ "inputs": [ { "name": "input", "shape": [1,3,384,384],
                "datatype": "FP32",
                "parameters": { "binary_data_size": 1769472 } } ],
  "outputs": [ { "name": "dets",   "parameters": { "binary_data": true } },
               { "name": "labels", "parameters": { "binary_data": true } },
               { "name": "masks",  "parameters": { "binary_data": true } } ] }
<1769472 bytes of FP32 input>
```

The response is mirrored: a JSON header whose byte length is declared in
`Inference-Header-Content-Length`, followed by the output tensors
concatenated in request order. The client reads exactly
`header + sum(expected output bytes)` and refuses anything else, which
catches a model/contract mismatch at the first frame rather than as
garbage boxes.

The JSON-number alternative would inflate the 1.7 MB input to roughly 6x
and add parse time; the binary extension is the reason a 384x384 seg
inference round trip is ~0.5 s on CPU rather than several seconds.

## Warm-up, identity, and failure semantics

- The detector's `Prepare` loads the manifest,
  checks the expected identity against the manifest's ONNX SHA-256 when
  one is pinned, probes `/v2/health/live` and `/v2/models/<m>/ready`, and
  runs one zero-input inference. The first real frame therefore pays no
  session-setup cost.
- A server that is down, a model that is not loaded, or a manifest whose
  shape does not match the served model all fail at *start*, with a
  message in the log - not as an empty scene.
- Failures during inference set health `DISCONNECTED` with the transport
  error attached; the counters (`completed`, `submitted`, ...) tell you
  whether anything ever succeeded.

## Latency budgets

The pipeline is built around one hard fact: inference is 1-2 orders of
magnitude slower than rendering (72 Hz). Consequences:

- **Newest frame only.** Captures are a queue of depth one; a newer
  frame replaces the pending one and the drop is counted. Working on an
  old frame never catches up - it only adds latency forever.
- **Rate-limited submission.** `submission_hz` defaults to 2 Hz: the
  round trip plus jitter routinely exceeds a frame or two, and a sparse
  queue is honest about it.
- **Expiry.** Results carry an age; after `expiry_ms` (5 s) they are not
  drawn. A stale box is worse than none.
- **Off-thread preparation.** YUV conversion, inference, quad/pose, and
  fusion happen on the worker; the render loop only consumes immutable
  prepared geometry.

## What the masks cost

For seg-small each frame moves 3.7 MB of mask logits (100 x 96 x 96
floats) plus 1.6 MB of input. On loopback that is negligible next to the
inference time; over Wi-Fi it is the dominant term.

## Choosing a backend

- Iterating on geometry (quads, poses, fusion): `triton` with the seg
  manifest - the model is fixed and fast, the headset only runs the
  cheap parts.
- Without a reachable server there is no inference path on purpose: the
  detector reports `DISCONNECTED` and the scene stays empty rather than
  quietly degrading to an unmeasured model.
- Full offline parity with the pinned oracle: the host tools under
  `tools/rfdetr_inference`, which share every line of pre/postprocessing
  with the app.

## Exercises

1. Compute the exact up-front byte cost of the request body for
   `rfdetr_medium` (576x576) and compare with the seg-small one.
2. Kill Triton while the app is running and describe what the log shows
   and what the scene does (hint: nothing renders, health changes).
3. Change `submission_hz` to 10 in `detector.conf` on the headset and
   explain which counters grow and why the scene does not get "faster".
