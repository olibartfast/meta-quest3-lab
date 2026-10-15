# Lesson 7 - Pose from Four Coplanar Points

## What you will learn

How four image corners and a known screen size become a full 6-DoF pose,
why the solution is a homography rather than a generic PnP, and how to
read the reprojection error.

Implementation: `SolveQuadPose` in `libs/screen_corners`.

## The setup

Known:

- four image points `p_i = (u_i, v_i)` in the pixels of the capture;
- their correspondence to a rectangle of known size `W x H` metres,
  centred at the object origin, on the plane `Z = 0` in the object frame:

```text
object points:  (-W/2, +H/2, 0)  (+W/2, +H/2, 0)  (+W/2, -H/2, 0)  (-W/2, -H/2, 0)
image points:   TL               TR               BR               BL
```

(note the flip: object `+Y` up, image `y` down);

- camera intrinsics `fx, fy, cx, cy`.

Wanted: rotation `R` and translation `t` such that projecting the object
points reproduces the image points.

## Step 1 - the homography

A pinhole camera viewing a plane induces a homography (a projective 2D
map). Writing the object-plane coordinates `(X, Y)`:

```text
u = (h11 X + h12 Y + h13) / (h31 X + h32 Y + 1)
v = (h21 X + h22 Y + h23) / (h31 X + h32 Y + 1)
```

Fixing `h33 = 1` leaves 8 unknowns. Each correspondence contributes two
linear equations, so four points give an 8x8 system - solved exactly by
Gaussian elimination with partial pivoting. No iteration, no RANSAC
(there are exactly zero redundant measurements, so a robust estimator
would be meaningless here).

## Step 2 - splitting intrinsics from extrinsics

The homography factors as `H = K [r1 r2 t]`, where `r1, r2` are the
first two columns of the rotation and `K` is the intrinsic matrix. So:

```text
A = K^-1 H = s [r1 r2 t]     for some scale s
```

Computing `A` is one subtraction and division per entry. Now the crucial
observation: `r1` and `r2` are unit vectors, so

```text
s = 1 / mean(|a1|, |a2|)
```

where `a1`, `a2` are the first two columns of `A`. This is the object's
distance-scale: it is also why **the object's absolute size does not
affect the reprojection error** - a 10 cm card at 50 cm projects
identically to a 1 m screen at 5 m, and the solver will happily return
the pose consistent with the size you told it.

## Step 3 - orthonormalize and disambiguate

`(a1/s, a2/s)` are *almost* orthonormal - noise and the blocky mask make
them slightly off. Gram-Schmidt:

```text
r1 = normalize(a1/s)
r2 = normalize(a2/s - (r1 . a2/s) r1)
r3 = r1 x r2                 (right-handed completion)
t  = a3 / s
```

The homography has a sign ambiguity (`H` and `-H` are the same map).
Resolve it physically: the screen must be in front of the camera, so if
`t.z < 0`, negate `a1, a2, t` (the cross-product `r3` is invariant under
the double flip). The result is one of planar pose's two "twisted pair"
solutions - the front-facing one, which is the physically meaningful one
for a screen.

## Step 4 - measure the fit

Reproject the four object corners with `(R, t, K)` and compute the RMS
pixel error. This number is the honest quality signal:

- **sub-pixel**: the quad is a perspective projection of a `W x H`
  rectangle - the pose is real. On synthetic data the solver reaches
  1e-3 px.
- **a few pixels**: consistent with the 13.3 px mask grid step (lesson
  5); expect 2-6 px on a well-segmented screen.
- **tens of pixels**: the quad does not have the shape of a `W x H`
  screen. Either the mask caught a sub-region (bezel, lit part, glare
  hole), or the object's true aspect ratio is not `W/H`.
- **hundreds**: wrong object size in the other sense - a *shape*
  mismatch, e.g. a portrait phone measured against a landscape screen
  prior. The solver cannot fix a wrong model; it reports the misfit.

Notice what is *not* in that list: wrong distance. A wrong *size* at the
right *shape* shifts `t` proportionally and leaves the RMS nearly
unchanged - so never validate scale with reprojection, validate it with
a tape measure.

## Why not AP3P?

The host tool (`tools/monitor_corners`) uses OpenCV's AP3P for the same
problem. AP3P solves a general 3-point perspective problem (with the 4th
point used to choose among up to four solutions) in closed form, without
using the fact that the points are coplanar. The homography route:

- uses the planarity explicitly (fewer unknowns than AP3P's polynomial);
- is 60 lines of linear algebra with no quartic solver;
- has a single, explainable failure mode (degenerate/ill-conditioned
  homography).

On the numbers that matter - pixel corners that are already quantized by
the mask grid - the two agree within the noise floor. The trade-off is
documented in the header: this is a deliberate, and reversible, choice.

## Reading the live numbers

```text
DETECTION frame=379 class=tv confidence=0.727 corner_reproj_px=16.45
```

16.45 px with a 13.3 px mask grid says: the quad is roughly right but not
crisp - consistent with tips 2-3 above. If you see a sudden jump to
three digits after changing `--class-sizes`, you changed the *shape*,
not the size (e.g. mapping a 16:9 panel to a 4:3 prior).

## Exercises

1. Project a 0.55x0.32 rectangle from a known pose with `fx = fy = 866`,
   feed the corners back, and verify you recover the pose. Then scale
   the size by 2 and the distance by 2 and show the projections are the
   same (the ambiguity, concretely).
2. Keep the size but change the aspect to 0.55x0.55. Predict the
   reprojection error before running it.
3. Explain why the corner order TL/TR/BR/BL must be preserved exactly -
   what would a cyclic rotation of the correspondences do to `R`?
