#!/usr/bin/env python3
"""Build per-device stereo calibration artifact from a stereo-probe report.

Reads build/stereo-probe/stereo-probe-report.json (Group A), validates the
shared-pose-reference + baseline plausibility gate, and writes a versioned
artifact keyed to device + stream config plus a .sha256 sidecar.

Zero-distortion pinhole is recorded as hypothesis, not fact: Meta publishes
no distortion coefficients for 50/51. Fails closed on missing fields.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path


ARTIFACT_SCHEMA_VERSION = 1


def _fail(msg: str) -> int:
    print(f"error: {msg}", file=sys.stderr)
    return 2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", default="build/stereo-probe/stereo-probe-report.json")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--out-dir", default="build/stereo-calibration")
    args = ap.parse_args()

    report_path = Path(args.report)
    if not report_path.is_file():
        return _fail(f"report not found: {report_path}")
    report = json.loads(report_path.read_text())

    if report.get("schema_version") != 1:
        return _fail(f"unsupported probe schema_version {report.get('schema_version')!r}")

    group_a = report.get("group_a", {})
    cams = {c.get("camera_id"): c for c in group_a.get("passthrough_cameras", [])}
    left = cams.get("50")
    right = cams.get("51")
    if left is None or right is None:
        return _fail("cameras 50/51 not both present in group_a")
    for cid, cam in (("50", left), ("51", right)):
        if len(cam.get("intrinsics", [])) < 5:
            return _fail(f"camera {cid}: intrinsics missing/short")
        if len(cam.get("pose_rotation", [])) < 4:
            return _fail(f"camera {cid}: pose_rotation missing/short")
        if len(cam.get("pose_translation", [])) < 3:
            return _fail(f"camera {cid}: pose_translation missing/short")
        if cam.get("pose_reference") not in ("GYROSCOPE", "PRIMARY_CAMERA"):
            return _fail(f"camera {cid}: unexpected pose_reference {cam.get('pose_reference')!r}")

    pair = group_a.get("selected_pair", {})
    if pair.get("shared_pose_reference") is not True:
        return _fail("selected_pair.shared_pose_reference is not true")
    baseline = pair.get("derived_baseline_meters")
    if not isinstance(baseline, (int, float)) or not (0.02 <= baseline <= 0.20):
        return _fail(f"derived_baseline_meters implausible: {baseline!r}")

    common = {(s.get("width"), s.get("height")) for s in pair.get("common_yuv_sizes", [])}
    if (args.width, args.height) not in common:
        return _fail(f"{args.width}x{args.height} not in common_yuv_sizes")

    device = report.get("device", {})
    artifact = {
        "schema_version": ARTIFACT_SCHEMA_VERSION,
        "device": {
            "manufacturer": device.get("manufacturer"),
            "model": device.get("model"),
            "device": device.get("device"),
            "display": device.get("display"),
            "fingerprint": device.get("fingerprint"),
            "sdk": device.get("sdk"),
        },
        "stream": {"width": args.width, "height": args.height},
        "pair": {"left_id": "50", "right_id": "51", "mechanism": pair.get("mechanism")},
        "left": {
            "intrinsics_fxfycxcyk": list(left["intrinsics"][:5]),
            "distortion": list(left.get("distortion", [])),
            "distortion_model": "zero-distortion-pinhole-hypothesis",
            "pose_rotation_xyzw": list(left["pose_rotation"][:4]),
            "pose_translation_xyz": list(left["pose_translation"][:3]),
            "pose_reference": left["pose_reference"],
        },
        "right": {
            "intrinsics_fxfycxcyk": list(right["intrinsics"][:5]),
            "distortion": list(right.get("distortion", [])),
            "distortion_model": "zero-distortion-pinhole-hypothesis",
            "pose_rotation_xyzw": list(right["pose_rotation"][:4]),
            "pose_translation_xyz": list(right["pose_translation"][:3]),
            "pose_reference": right["pose_reference"],
        },
        "derived_baseline_meters": baseline,
        "provenance": {
            "probe_report": str(report_path),
            "note": "per-device; invalidate on firmware/stream-crop/ROI/distortion/mechanical change",
        },
    }

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    fp = (device.get("fingerprint") or "unknown").replace("/", "_")
    name = f"stereo-calibration-{device.get('device', 'unknown')}-{args.width}x{args.height}.json"
    out = out_dir / name
    blob = json.dumps(artifact, indent=2, sort_keys=True) + "\n"
    out.write_text(blob)
    digest = hashlib.sha256(blob.encode()).hexdigest()
    (out.with_suffix(".json.sha256")).write_text(digest + f"  {name}\n")
    print(f"artifact: {out}")
    print(f"sha256: {digest}")
    print(f"baseline_m: {baseline}")
    _ = fp
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
