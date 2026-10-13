#!/usr/bin/env bash
# Extract Triton C++ client libraries from the pinned SDK image.
# Same pattern as tritonic's docker/scripts/extract_triton_libs.sh.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

triton_tag="${NGC_CONTAINER_TAG:-26.08}"
image="nvcr.io/nvidia/tritonserver:${triton_tag}-py3-sdk"
container="questlab-triton-extract-$(date +%s)"
dest="$repo_root/build/triton_client_libs"

mkdir -p "$dest"
echo "Extracting Triton client libs from $image into $dest"
docker run -d --name "$container" "$image" sleep 3600 > /dev/null
trap 'docker rm -f "$container" > /dev/null' EXIT
docker cp "$container:/workspace/install" "$dest/"
echo "TritonClientBuild_DIR=$dest/install"
