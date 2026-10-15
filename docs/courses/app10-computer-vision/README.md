# Computer Vision Behind App 10

A self-contained course on the computer-vision pipeline that turns the
Quest 3's RGB camera into located, measured objects: the model, the
tensor contracts, the geometry, and the coordinate frames. Every lesson
is grounded in the code in this repository - file paths are given so you
can read the real implementation after each concept.

## Who this is for

Engineers who can read C++ and are comfortable with vectors and matrices.
No prior deep-learning or SLAM background is assumed; every model term is
introduced before it is used. The maths is written out where it matters
(pinhole projection, homography pose, percentiles) and skipped where it
does not (transformer internals).

## The pipeline in one picture

```text
Camera2 (YUV_420_888)
   |  camera_source: strides, timestamps, intrinsics, calibration
   v
RgbCapture  --YUV->RGBA-->  frame pixels (1280x1280 on Quest 3)
   |                             |                    \
   |                      2D diagnostic preview       \
   v                                                   v
PreprocessCapture (squash-resize 384x384, normalize)   depth (env depth)
   |                                                   |
   v                                                   |
RF-DETR (host: Triton over HTTP)                        |
   |  dets [1,Q,4], labels [1,Q,91], (masks [1,Q,96,96])
   v                                                   |
PostprocessDetections[_WithMasks] --> Detection[]      |
   |  source-pixel boxes, optional binary masks        |
   +--> screen quad (libs/screen_corners)              |
   |        |                                          |
   |        v                                          |
   |     PnP pose (object size known)                  |
   |        |                                          |
   v        v                                          v
BuildDetectionProjection --> bearings in LOCAL --> FuseDetection
   |                                                   |
   v                                                   v
render: seg planar cuboid / depth-fused cuboid + diagnostics
```

## Syllabus

| # | Lesson | Question it answers |
|---|--------|--------------------|
| 1 | [Pipeline overview](01-pipeline-overview.md) | What are the stages, where do they run, what data moves between them? |
| 2 | [Capture to pixels](02-capture-to-pixels.md) | How does camera light become an RGBA frame with a calibration? |
| 3 | [The preprocessing contract](03-preprocessing-contract.md) | Exactly how is a capture turned into the model's input tensor? |
| 4 | [RF-DETR as set prediction](04-detr-set-prediction.md) | Why are there no anchors and no NMS, and how are logits turned into boxes? |
| 5 | [Instance masks](05-instance-masks.md) | Where do per-object masks come from and how are they decoded? |
| 6 | [Mask to screen quad](06-mask-to-screen-quad.md) | How does a blob of pixels become four corners? |
| 7 | [Pose from four coplanar points](07-pose-from-four-coplanar-points.md) | How do four image corners become a 6-DoF pose? |
| 8 | [Depth fusion and the metric cuboid](08-depth-fusion-metric-cuboid.md) | How does a bearing become a measured 3D box - and what stays assumed? |
| 9 | [Frames: camera, head, LOCAL](09-frames-camera-head-local.md) | Which coordinate system is which, and when is each used? |
| 10 | [Serving, latency, trade-offs](10-serving-latency-and-trade-offs.md) | On-device or Triton? How do budgets shape the pipeline? |

## How to run the pieces

```bash
# RF-DETR contract, preprocessing and postprocessing tests
cmake -S tools/rfdetr_inference -B build/rfdetr-inference-tests \
  -DBUILD_TESTING=ON -DQUESTLAB_ENABLE_ONNXRUNTIME=OFF
cmake --build build/rfdetr-inference-tests --parallel
ctest --test-dir build/rfdetr-inference-tests --output-on-failure

# Screen-quad and pose library (this course's lessons 6-7)
cmake -S libs/screen_corners -B build/screen-corners
cmake --build build/screen-corners --parallel
ctest --test-dir build/screen-corners --output-on-failure

# Triton client (lesson 10)
cmake -S libs/triton_client -B build/triton-client
cmake --build build/triton-client --parallel
ctest --test-dir build/triton-client --output-on-failure
```

Deploy and watch live values:

```bash
./tools/triton/launch_triton.sh                        # host side
./scripts/build_deploy.sh --app 10-rfdetr-detection \
  --manifest tools/rfdetr_export/seg-small-manifest.json \
  --class-sizes tv:0.55,0.32
adb logcat -s RFDetrDetection:V
```

## Capstone

Pick a screen-like object (a TV, monitor, or a taped rectangle). Deploy
app 10 with the seg manifest and your object's real width/height, then:

1. Compare the seg planar cuboid against a tape measure (position error).
2. Take the same measurement with `--class-sizes` deliberately wrong and
   explain what changes and what does not (hint: lesson 7).
3. Watch `corner_reproj_px` in the log while moving: when does the quad
   stop being reliable, and which gate fired?

## Reference material

- `docs/rfdetr-detection.md`, `docs/rfdetr-seg.md`,
  `docs/coordinate-systems.md`, `docs/quest-camera.md` in this repository.
- RF-DETR: `https://github.com/roboflow/rf-detr` (DETR-family detector
  used here in nano/medium/seg-small variants).
- KServe v2 / Open Inference Protocol binary tensor data extension.
- Khronos OpenXR specification (reference spaces, view poses).
- Android Camera2 `LENS_POSE_ROTATION` / `LENS_POSE_TRANSLATION` docs.
