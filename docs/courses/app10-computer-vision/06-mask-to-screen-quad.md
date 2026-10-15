# Lesson 6 - Mask to Screen Quad

## What you will learn

How a binary mask becomes four ordered corners, which gates reject the
not-a-screen cases, and why the algorithm looks the way it does.

Implementation: `libs/screen_corners` (`FindScreenQuadFromMask`), a
dependency-free port of the OpenCV host tool in `tools/monitor_corners`.

## The goal

Not "bounding box" - the axis-aligned box of a screen seen at an angle
is not the screen. We want the four projected corners, in a fixed order:

```text
TL, TR, BR, BL   (image coordinates, y down)
```

so that corner `i` always corresponds to the same physical corner of the
object (critical for pose, lesson 7).

## The pipeline

1. **Plausibility gate on the whole mask.** Foreground pixels must be
   between 0.5% and 80% of the frame. A speck is not a screen; a mask
   covering the whole frame is a depth or segmentation failure.

2. **Largest connected component** (8-connectivity). One screen per mask;
   if the mask has a glare hole splitting it, the larger piece wins - and
   the gates below are what stop the smaller piece from being trusted as
   a screen of its own.

3. **Boundary tracing (Moore neighbourhood).** Walk the outer boundary of
   the component pixel by pixel, clockwise, backtracking from the
   direction you came. The trace is a closed polygon of pixel centres
   (for a 200x120 rectangle, ~640 points).

4. **Polygon simplification (Douglas-Peucker).** Collapse the staircase
   of pixel steps into straight segments: a point is kept when it lies
   farther than `epsilon = 2% of perimeter` from the chord it would be
   removed by. A rasterized straight edge has sub-pixel sawtooth and
   collapses entirely; a true corner deviates enormously.

5. **Shape gates.** Exactly 4 points survive, the quadrilateral is
   convex, its bounding box is at least 32x32 px, and its aspect ratio
   is between 1.1 and 2.6. That window says "screen-like": a phone held
   in portrait (aspect 0.5) is rejected, a square rug (1.0) is rejected,
   an ultrawide monitor (2.33) is accepted. These are *display* priors
   and they exist to trade recall for precision: the pipeline prefers to
   find nothing over to find a tablecloth.

6. **Ordering.** Sort the four corners into TL/TR/BR/BL: TL has the
   smallest `x + y`, BR the largest; of the remaining two, TR has the
   larger `x - y`. This works under rotation up to about 45 degrees,
   which is the range a wall-mounted screen is seen in.

7. **Score.** `quad area / foreground area`, in (0, 1]. A perfect screen
   mask approaches the ratio `(W-px_overhead)(H-px_overhead)/(W*H)`; a
   ragged blob is well below 1. The score is reported, not gated - the
   caller can decide.

## A worked example

The tilt-tolerant test in `screen_corners_tests.cpp` rasterizes a
quadrilateral with corners

```text
(110, 70)  (330, 100)  (310, 220)  (90, 190)    bounding box 240x150
```

The traced boundary contains hundreds of pixel steps; Douglas-Peucker
with epsilon = 2% of the ~640 px perimeter (about 12.8 px) reduces it to
exactly these four vertices, each recovered within 3 px.

## Deliberate refusals

The function returns a reason string, and an empty answer is a correct
answer. From the live log you can see the taxonomy: a screen that is too
small, a mask that is not quadrilateral, a quad outside the aspect
window. Nothing downstream has to special-case "no corners": the pose
stage is simply not attempted and the renderer draws no plane.

## Why not a bounding rectangle or min-area rect?

Both are cheaper and both are wrong for a screen:

- axis-aligned bounding box: inflates under yaw - the pose would then
  solve for a rectangle that does not exist;
- min-area rectangle: finds *a* rectangle but not *the projected quad*;
  it cannot represent perspective (the two vertical edges of a screen
  seen from below have different lengths).

The four-corner quad, in fixed correspondence with the object, is the
minimal representation that a planar pose solver can consume.

## Exercises

1. A 16:9 monitor is seen such that its bounding box aspect is 1.2.
   Sketch why, and compute the yaw for a 0.55x0.32 m screen at 2.5 m
   (hint: `cos(yaw) * 0.55 / 0.32`).
2. Modify the test to rasterize a circle and check which gate fires
   first. Then try an ellipse of aspect 1.6 - explain the outcome.
3. In the live log, `corner_reproj_px` rose to ~15 px while the mask grid
   step is 13.3 px. What does that suggest about the quad's error budget?
