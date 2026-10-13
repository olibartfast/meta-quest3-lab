#!/usr/bin/env bash
# Sparse checkout of the Ocean modules this repo builds against
# (base/math/geometry). Pinned by commit; sources land in
# build/ocean-src and are never committed (see .gitignore for build/).
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

ocean_commit="${OCEAN_COMMIT:-1b1b2b9}"
dest="$repo_root/build/ocean-src"

if [[ -d "$dest/impl/ocean/geometry" ]]; then
  echo "Ocean sources already present at $dest"
  exit 0
fi
mkdir -p "$dest"
if [[ ! -d "$dest/.git" ]]; then
  git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/facebookresearch/ocean "$dest"
fi
git -C "$dest" sparse-checkout set impl/ocean/base impl/ocean/math \
  impl/ocean/geometry
git -C "$dest" fetch --depth 1 origin "$ocean_commit" 2>/dev/null || true
git -C "$dest" checkout "$ocean_commit" 2>/dev/null || true
echo "Ocean ready at $dest ($(git -C "$dest" rev-parse --short HEAD))"
