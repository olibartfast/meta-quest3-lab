# Triton Inference Server for RF-DETR (C++, like tritonic)

Host-side C++ path: the Quest APK is unchanged (still `--backend streaming`
over `adb reverse` to `127.0.0.1:48110`). Tensor execution moves from the
local ONNX session to NVIDIA Triton Server. Preprocessing and postprocessing
stay in `tools/rfdetr_inference`, so detections remain bit-compatible with
the pinned oracle.

Pattern follows https://github.com/olibartfast/tritonic:
Triton C++ HTTP client (`r26.08`) from the `-py3-sdk` image, `rfdetr`
model type (input `input` 1x3x384x384 FP32, outputs `dets` 1x300x4,
`labels` 1x300x91), client-CPU preprocessing.

## 1. Model repository

`model_repository/rfdetr_nano/{config.pbtxt,1/model.onnx}` already points at
`build/models/rfdetr/rfdetr-nano.onnx` (symlinked by `launch_triton.sh`).

## 2. Start Triton (needs the server image)

```bash
./tools/triton/launch_triton.sh
# TRITON_IMAGE=nvcr.io/nvidia/tritonserver:26.08-py3 ./tools/triton/launch_triton.sh
```

Serves `rfdetr_nano:1` on HTTP 8000 / gRPC 8001 / metrics 8002 (CPU,
ONNX backend).

## 3. Extract Triton C++ client libs (cached SDK image, offline OK)

```bash
./tools/triton/extract_triton_libs.sh
export TritonClientBuild_DIR=$PWD/build/triton_client_libs/install
```

## 4. Build the service with the Triton backend

```bash
onnxruntime_root="$(tools/rfdetr_inference/prepare_onnxruntime.sh)"
cmake -S tools/rfdetr_inference -B build/rfdetr-inference \
  -DQUESTLAB_ENABLE_ONNXRUNTIME=ON \
  -DQUESTLAB_ENABLE_TRITON=ON \
  -DONNXRUNTIME_ROOT="$onnxruntime_root" \
  -DTritonClientBuild_DIR="$PWD/build/triton_client_libs/install"
cmake --build build/rfdetr-inference --parallel
```

## 5. Run the bridge (Quest protocol on 48110, Triton on 8000)

```bash
build/rfdetr-inference/rfdetr_service \
  --manifest tools/rfdetr_export/model-manifest.json \
  --triton-url localhost:8000 \
  --triton-model rfdetr_nano --triton-version 1 \
  --port 48110
```

Then deploy the headset as usual (no APK change):

```bash
./scripts/build_deploy.sh --app 10-rfdetr-detection --backend streaming
```

Omit `--triton-url` to fall back to local ONNX Runtime.

Note: Triton fixes where compute runs, not pose quality. Boxes stay
yaw-only plane fits until a 6D estimator (e.g. FoundationPose) is added as a
second Triton model with depth + masks.
