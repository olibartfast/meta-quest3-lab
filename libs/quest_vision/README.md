# quest_vision — isolated computer-vision shared library

One distributable binary (`quest_vision.so` on Android/Linux,
`quest_vision.dll` on Windows) bundling the portable detection pipeline:

- `libs/xr_math` — vectors, quaternions, poses, basis conversion
- `libs/artifact_integrity` — checksums, manifest identity
- `libs/camera_source` — captures, YUV/RGBA conversion, fixtures
- `libs/detection_projection` — 2D box to LOCAL frustum bearings
- `libs/detection_fusion` — depth clustering, 6D surface orientation
- `libs/object_detector` — on-device / streaming / replay backends

Deliberately excluded: `depth_source` and everything under `xr_*`
(OpenXR runtime, renderers, passthrough). This library builds without a
headset toolchain.

Host check:

```bash
cmake -S libs/quest_vision -B /tmp/opencode/qv -DBUILD_TESTING=ON
cmake --build /tmp/opencode/qv --parallel
```

Android consumers (e.g. app 10) link `quest_vision` instead of the five
archives. `WINDOWS_EXPORT_ALL_SYMBOLS` is on until an explicit
`QUEST_VISION_API` export macro is introduced.
