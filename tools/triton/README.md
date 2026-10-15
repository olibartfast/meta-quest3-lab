# Triton Inference Server for RF-DETR

The headset app carries its own Triton client: the APK preprocesses the
camera frame, calls Triton over the KServe v2 HTTP protocol (binary tensor
extension) with `libs/triton_client`, and runs postprocessing - including
mask decoding, screen-quad extraction, and PnP pose solving - on-device
(`libs/screen_corners`). The model itself never runs on the headset.

## 1. Model repository

Tracked configs live under `tools/triton/model_repository`:
`rfdetr_nano` (384), `rfdetr_medium` (576), `rfdetr_seg_small`
(384 + masks). `launch_triton.sh` assembles the served repository in
`build/triton-model-repository`, taking each `config.pbtxt` and
hard-linking the matching `build/models/rfdetr/*.onnx` (`rfdetr_seg_small`
-> `rfdetr-seg-small.onnx`; copied if a hardlink is not possible). Models
whose ONNX has not been exported yet are skipped with a warning instead of
failing Triton startup.

## 2. Start Triton (needs the server image)

```bash
./tools/triton/launch_triton.sh
# TRITON_IMAGE=nvcr.io/nvidia/tritonserver:26.08-py3 ./tools/triton/launch_triton.sh
```

Serves the exported models on HTTP 8000 / gRPC 8001 / metrics 8002 (CPU,
ONNX backend). The APK reaches it through `adb reverse tcp:8000 tcp:8000`
(set up by the deploy script), or over the LAN with `--service-host`.

## 3. Deploy the headset

The deploy script pushes the model manifest (shape contract), writes the
`detector.conf` Triton keys, and derives the Triton model name from the
manifest variant (`seg-small` -> `rfdetr_seg_small`):

```bash
./scripts/build_deploy.sh --app 10-rfdetr-detection \
  --manifest tools/rfdetr_export/seg-small-manifest.json \
  --class-sizes tv:0.55,0.32 \
  --allowed-classes tv
```

`--class-sizes` lists the planar classes a pose is solved for (the app
defaults to `tv:0.55,0.32`); every masked detection still gets a quad.
Box-only manifests (nano/medium) work the same way without quads:

```bash
./scripts/build_deploy.sh --app 10-rfdetr-detection \
  --manifest tools/rfdetr_export/model-manifest.json
```

The manifest identity (ONNX SHA-256) is verified against the pushed
contract at detector start, so a manifest/model mismatch fails fast.

## 4. Host-side checks

`libs/triton_client` has unit tests with a scripted local server:

```bash
cmake -S libs/triton_client -B build/triton-client
cmake --build build/triton-client --parallel
ctest --test-dir build/triton-client --output-on-failure
```

`libs/screen_corners` (mask -> quad -> pose) has its own suite:

```bash
cmake -S libs/screen_corners -B build/screen-corners
cmake --build build/screen-corners --parallel
ctest --test-dir build/screen-corners --output-on-failure
```
