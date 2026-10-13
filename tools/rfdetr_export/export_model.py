#!/usr/bin/env python3
"""Download, verify, export, and inspect pinned RF-DETR models."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import shutil
import sys
import urllib.request
from pathlib import Path
from typing import Any

CHECKPOINT_URL = (
    "https://storage.googleapis.com/rfdetr/nano_coco/"
    "checkpoint_best_regular.pth"
)
CHECKPOINT_SHA256 = (
    "d8d6b9ee57d4d0ed2b1f305163624712a0532cb7bce0c747317984fc5457440d"
)
# Per-variant export parameters. sha256 is None when only the registry MD5
# is known (medium): the checkpoint is MD5-verified and the observed
# SHA-256 is recorded in the export record.
VARIANTS = {
    "nano": {
        "model_class": "RFDETRNano",
        "shape": (384, 384),
        "input_shape": [1, 3, 384, 384],
        "onnx_filename": "rfdetr-nano.onnx",
        "checkpoint_url": CHECKPOINT_URL,
        "checkpoint_sha256": CHECKPOINT_SHA256,
        "checkpoint_md5": "fb6504cce7fbdc783f7a46991f07639f",
    },
    "medium": {
        "model_class": "RFDETRMedium",
        "shape": (576, 576),
        "input_shape": [1, 3, 576, 576],
        "onnx_filename": "rfdetr-medium.onnx",
        "checkpoint_url": (
            "https://storage.googleapis.com/rfdetr/medium_coco/"
            "checkpoint_best_regular.pth"
        ),
        "checkpoint_sha256": None,
        "checkpoint_md5": "7223f764a87b863f02eb8d52bf0ce2ee",
    },
}
EXPECTED_OUTPUTS = {
    "dets": [1, 300, 4],
    "labels": [1, 300, 91],
}
EXPECTED_VERSIONS = {
    "rfdetr": "1.9.0",
    "torch": "2.10.0+cpu",
    "torchvision": "0.25.0+cpu",
    "onnx": "1.20.1",
    "onnxruntime": "1.21.0",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def md5_file(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_checkpoint(
    path: Path,
    allow_download: bool,
    variant: dict[str, Any],
) -> None:
    if not path.exists():
        if not allow_download:
            raise FileNotFoundError(
                f"Checkpoint is missing: {path}. Pass --download-checkpoint."
            )
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        try:
            with urllib.request.urlopen(variant["checkpoint_url"]) as response:
                with temporary.open("wb") as output:
                    shutil.copyfileobj(response, output, 1024 * 1024)
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    actual_md5 = md5_file(path)
    if actual_md5 != variant["checkpoint_md5"]:
        raise ValueError(
            f"Checkpoint MD5 mismatch: expected {variant['checkpoint_md5']}, "
            f"actual {actual_md5}"
        )
    if variant["checkpoint_sha256"] is not None:
        actual_sha256 = sha256_file(path)
        if actual_sha256 != variant["checkpoint_sha256"]:
            raise ValueError(
                f"Checkpoint SHA-256 mismatch: expected "
                f"{variant['checkpoint_sha256']}, actual {actual_sha256}"
            )


def tensor_shape(value_info: Any) -> list[int]:
    dimensions: list[int] = []
    for dimension in value_info.type.tensor_type.shape.dim:
        if not dimension.HasField("dim_value"):
            raise ValueError(f"Dynamic dimension is not allowed: {value_info.name}")
        dimensions.append(int(dimension.dim_value))
    return dimensions


def inspect_onnx(path: Path, expected_input_shape: list[int]) -> dict[str, Any]:
    import onnx

    model = onnx.load(path)
    onnx.checker.check_model(model)
    inputs = {value.name: tensor_shape(value) for value in model.graph.input}
    outputs = {value.name: tensor_shape(value) for value in model.graph.output}
    if inputs != {"input": expected_input_shape}:
        raise ValueError(f"Unexpected ONNX inputs: {inputs}")
    if outputs != EXPECTED_OUTPUTS:
        raise ValueError(f"Unexpected ONNX outputs: {outputs}")
    return {"inputs": inputs, "outputs": outputs}


def inspect_environment() -> dict[str, str]:
    if sys.version_info[:2] != (3, 11):
        raise ValueError(
            "RF-DETR export requires Python 3.11; actual "
            f"{sys.version.split()[0]}"
        )
    versions = {
        package: importlib.metadata.version(package)
        for package in EXPECTED_VERSIONS
    }
    if versions != EXPECTED_VERSIONS:
        raise ValueError(
            f"Export environment differs from the pin: expected "
            f"{EXPECTED_VERSIONS}, actual {versions}"
        )
    return {"python": sys.version.split()[0], **versions}


def update_manifest(
    manifest_path: Path,
    variant_name: str,
    variant: dict[str, Any],
    onnx_sha256: str,
    checkpoint_sha256: str,
) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["model"]["variant"] = variant_name
    source = manifest["model"]["source"]
    source["checkpoint_filename"] = variant["onnx_filename"].replace(
        "rfdetr-", "rf-detr-"
    ).replace(".onnx", ".pth")
    source["checkpoint_url"] = variant["checkpoint_url"]
    source["checkpoint_md5"] = variant["checkpoint_md5"]
    source["checkpoint_sha256"] = checkpoint_sha256
    manifest["model"]["onnx"]["filename"] = variant["onnx_filename"]
    manifest["model"]["onnx"]["sha256"] = onnx_sha256
    manifest["input"]["shape"] = list(variant["input_shape"])
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--variant", choices=sorted(VARIANTS), default="nano"
    )
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--download-checkpoint", action="store_true")
    parser.add_argument("--update-manifest", action="store_true")
    args = parser.parse_args()
    variant = VARIANTS[args.variant]

    versions = inspect_environment()
    ensure_checkpoint(args.checkpoint, args.download_checkpoint, variant)

    import rfdetr

    model_class = getattr(rfdetr, variant["model_class"])
    args.output_dir.mkdir(parents=True, exist_ok=True)
    model = model_class(
        pretrain_weights=str(args.checkpoint.resolve()),
        device="cpu",
    )
    exported = Path(
        model.export(
            format="onnx",
            output_dir=str(args.output_dir),
            shape=variant["shape"],
            batch_size=1,
            dynamic_batch=False,
            opset_version=17,
            verbose=False,
            notes={
                "contract": "QUESTLAB_RFDETR_MODEL_V1",
                "rfdetr": "1.9.0",
                "variant": args.variant,
            },
        )
    )
    if not exported.is_absolute():
        exported = Path.cwd() / exported
    exported = exported.resolve()
    if exported.name != variant["onnx_filename"]:
        raise ValueError(
            f"Unexpected ONNX filename: expected "
            f"{variant['onnx_filename']}, actual {exported.name}"
        )
    inspection = inspect_onnx(exported, list(variant["input_shape"]))
    onnx_sha256 = sha256_file(exported)
    checkpoint_sha256 = sha256_file(args.checkpoint)
    if args.update_manifest:
        update_manifest(
            args.manifest, args.variant, variant, onnx_sha256,
            checkpoint_sha256,
        )

    record = {
        "schema": "QUESTLAB_RFDETR_EXPORT_RECORD_V1",
        "command": "uv run export_model.py --variant "
        f"{args.variant} --checkpoint <path> "
        "--output-dir <path> --manifest <manifest> "
        "--update-manifest",
        "variant": args.variant,
        "checkpoint_md5": variant["checkpoint_md5"],
        "checkpoint_sha256": checkpoint_sha256,
        "onnx_path": str(exported),
        "onnx_sha256": onnx_sha256,
        "versions": versions,
        **inspection,
    }
    record_path = args.output_dir / f"export-record-{args.variant}.json"
    record_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exception:  # noqa: BLE001 - CLI reports one precise error.
        print(f"RF-DETR export failed: {exception}", file=sys.stderr)
        sys.exit(1)
