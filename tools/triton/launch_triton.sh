#!/usr/bin/env bash
# Launch NVIDIA Triton Inference Server for RF-DETR (CPU, ONNX backend).
# Tracked configs live in tools/triton/model_repository; the served
# repository is assembled in build/triton-model-repository so models whose
# ONNX has not been exported yet are skipped instead of failing startup.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
model_repo="$repo_root/tools/triton/model_repository"
runtime_repo="$repo_root/build/triton-model-repository"
onnx_dir="$repo_root/build/models/rfdetr"

rm -rf "$runtime_repo"
served=0
for config in "$model_repo"/*/config.pbtxt; do
  name="$(basename "$(dirname "$config")")"
  # rfdetr_seg_small -> build/models/rfdetr/rfdetr-seg-small.onnx
  onnx_src="$onnx_dir/${name//_/-}.onnx"
  if [[ ! -f "$onnx_src" ]]; then
    echo "Skipping $name: missing $onnx_src" >&2
    continue
  fi
  mkdir -p "$runtime_repo/$name/1"
  cp "$config" "$runtime_repo/$name/config.pbtxt"
  # Hardlink (same filesystem) so the container sees a regular file; a
  # symlink would carry a host path that does not resolve under /models.
  if ! ln "$onnx_src" "$runtime_repo/$name/1/model.onnx" 2>/dev/null; then
    cp "$onnx_src" "$runtime_repo/$name/1/model.onnx"
  fi
  echo "Serving $name: $onnx_src"
  served=$((served + 1))
done
if [[ "$served" -eq 0 ]]; then
  echo "No model ONNX found under $onnx_dir (see tools/rfdetr_export)" >&2
  exit 1
fi

image="${TRITON_IMAGE:-nvcr.io/nvidia/tritonserver:26.08-py3}"
echo "Starting Triton ($image) with repo $runtime_repo"
exec docker run --rm \
  --name questlab-triton \
  -p 8000:8000 -p 8001:8001 -p 8002:8002 \
  -v "$runtime_repo:/models:ro" \
  "$image" tritonserver \
  --model-repository=/models \
  --http-port=8000 --grpc-port=8001 --metrics-port=8002 \
  --log-verbose=1
