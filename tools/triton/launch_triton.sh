#!/usr/bin/env bash
# Launch NVIDIA Triton Inference Server for RF-DETR Nano (CPU, ONNX backend).
# Model repository lives in tools/triton/model_repository.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
model_repo="$repo_root/tools/triton/model_repository"
onnx_src="$repo_root/build/models/rfdetr/rfdetr-nano.onnx"
version_dir="$model_repo/rfdetr_nano/1"

mkdir -p "$version_dir"
if [[ ! -f "$onnx_src" ]]; then
  echo "Missing ONNX model: $onnx_src" >&2
  exit 1
fi
# Link (not copy) the 100MB model into the version dir so Triton sees it.
if [[ ! -f "$version_dir/model.onnx" ]]; then
  ln -s "$onnx_src" "$version_dir/model.onnx"
fi

image="${TRITON_IMAGE:-nvcr.io/nvidia/tritonserver:26.08-py3}"
echo "Starting Triton ($image) with repo $model_repo"
exec docker run --rm \
  --name questlab-triton \
  -p 8000:8000 -p 8001:8001 -p 8002:8002 \
  -v "$model_repo:/models:ro" \
  "$image" tritonserver \
  --model-repository=/models \
  --http-port=8000 --grpc-port=8001 --metrics-port=8002 \
  --log-verbose=1
