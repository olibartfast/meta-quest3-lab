# Lesson 8 - Depth Fusion and the Metric Cuboid

## What you will learn

How a 2D detection (a bearing) becomes a measured 3D box, which parts of
that box are measurements, and how the code keeps assumptions visible.

Implementation: `libs/detection_fusion` (`FuseDetection`), driven from
the app's pipeline worker.

## The fundamental limit

A monocular detection gives a direction, never a distance. Everything
metric in this pipeline comes from Meta's environment depth: a coarse
sensor-fused depth map delivered by the runtime. Fusion is the process of
intersecting "where the detection says the object is" (a frustum) with
"what the depth sensor sees" (a point cloud).

The repository takes the hard line on this: a box with no depth support
is not drawn at all, because a plausible-looking cuboid from no evidence
is worse than an empty scene - you cannot tell it from a measurement.

## Step 1 - the frustum

`BuildDetectionProjection` (lesson 9) converts the detection's 2D box
corners into four rays from the camera origin in `LOCAL`, plus the
origin itself. The four rays at the object's distance bound the space the
object could occupy - a pyramid. `DetectionFrustum` carries exactly
that.

The border is then inset by `borderInsetFraction = 0.12` on each side: a
2D box always clips a band of background at its edges, so the outer 12%
is discarded *before* clustering rather than cleaned up afterwards.

## Step 2 - selecting the surface

Inside the pyramid, take every depth sample and find the surface the
object is on. Two design choices, both explained at length in the
header:

- **Selection is by occlusion order, not popularity.** The nearest
  surface holding at least `foregroundSampleShare = 5%` of the samples
  wins. That 5% only needs to beat scattered noise: a thin or perforated
  object (a chair back) may legitimately hold a small share, while the
  wall *behind* it routinely holds more than half. A "majority" rule
  would put the box on the wall.
- **Samples are clustered with a tolerance of 25 cm** around that
  surface; farther samples are background or foreground clutter.

Guards: minimum 120 samples, range between 0.25 m and 6 m.

## Step 3 - the plane, and orientation

Distances alone locate a surface but not its tilt. A RANSAC plane fit
answers both:

- 160 iterations, inliers within 2.5 cm, minimum 70% inlier ratio;
- the plane normal gives a **measured orientation** (the object's own
  pitch and roll, not an assumption that it faces the camera);
- `requireMeasuredOrientation = true`: if no plane survives, no box is
  emitted. The fallback - orienting by the viewing bearing - produces a
  cuboid that looks fine and means nothing.

## Step 4 - extents and the one assumption

The near face is measured; the extents are taken from the 5th and 95th
percentiles of the surface samples so a single stray point cannot
inflate the box.

The extent along the viewing axis (how *deep* the object is) can never
be measured from one viewpoint: depth only sees the face towards the
camera. It is filled from a per-class prior and flagged:

```text
FusedBox::depthExtentIsAssumed = true
```

The renderer tints boxes whose depth came from a prior, and the log
spells it out (`FUSED_POSE ... measured=1 source=1`). Position stays
measured even when depth is assumed - and the two must not be confused
in downstream statistics.

## Step 5 - provenance

`RangeSource` records where the box's distance came from:

| Source | Meaning |
|--------|---------|
| `Stereo` | primary measurement (stereo depth path) |
| `EnvDepthFallback` | environment depth agreed or supplied the range (rendered amber) |
| `Prior` | unset/hide sentinel - never rendered |
| `Learned` | monocular learned depth: a prior, confidence halved via `kLearnedConfidenceFactor` |

A mixed or fallback box is never presented as fully measured: the log
and the tinting make the provenance part of the user-visible output.
That is what "R-3 discipline" and "R-7 provenance" in the comments refer
to - requirements that the system never dresses an assumption as a
measurement.

## What this means for screens

A TV measured by depth fusion gives you a *cuboid around the panel* -
useful, but its facing is the panel's plane only if RANSAC found the
plane. A TV measured by the seg path (lessons 5-7) gives you the panel's
exact plane and pose from its corners, with the depth extent assumed 5 cm.
The two answers agree in position and disagree in quality:

- fusion: works for any object, needs depth support, orientation only as
  good as the plane fit;
- seg pose: needs a seg detection and a known object size, gives the
  plane exactly, and its reprojection RMS tells you when to distrust it.

That is why the app draws the seg cuboid when a pose exists and the
depth-fused cuboid otherwise - not both (see the renderer's
`drawsSegCuboid`).

## Exercises

1. Explain why `foregroundSampleShare` being very small (e.g. 0.1%)
   would be a bug, and very large (e.g. 20%) would too. Which failure
   does the 5% compromise between?
2. In the log, find a `FUSED_POSE` line with `planeInlier` below 0.5 and
   reason about whether its orientation was measured or assumed.
3. Sketch a chair at 45 degrees yaw, mark which of its surfaces depth can
   see from one viewpoint, and predict where the fused cuboid's far face
   lands relative to the chair's actual back.
