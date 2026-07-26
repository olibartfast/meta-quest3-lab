# Coordinate Systems

Milestone 3 uses OpenXR coordinate conventions directly and makes transform
direction explicit in names.

## OpenXR coordinates

OpenXR spaces are right-handed and measured in metres:

- `+X` points right;
- `+Y` points up;
- `-Z` points forward.

The axis visualization therefore uses red for `+X`, green for `+Y`, and blue
for `-Z`.

`VIEW` follows the HMD. `LOCAL` is a world-locked origin established near
startup and may be adjusted by a runtime recenter operation. `STAGE`, when
supported, represents a floor-level play area. A runtime can support `STAGE`
while reporting no boundary dimensions.

Stereo content is located with `xrLocateViews` using `LOCAL` as the base space.
Using `VIEW` as that base would make ordinary world content head-locked.

## Pose and transform direction

For:

```text
xrLocateSpace(targetSpace, baseSpace, time, &location)
```

`location.pose` is the pose of `targetSpace` expressed in `baseSpace`. In this
repository it is named `baseFromTarget`: applying it to a point expressed in
the target space produces that point expressed in the base space.

For example, the head pose is obtained by locating `VIEW` relative to `LOCAL`
and is named `localFromView`.

Pose composition follows the same convention:

```text
worldFromChild = Compose(worldFromParent, parentFromChild)
```

The rightmost transform is applied first.

## Math representation

`xr_math` stores matrices in column-major order and multiplies column vectors:

```text
pointInWorld = worldFromObject * pointInObject
clip = projection * viewFromWorld * worldFromObject * vertex
```

Quaternions use OpenXR component order `(x, y, z, w)`, with the scalar
component last. Conversions between OpenXR and repository math types copy
values explicitly; they do not rely on binary layout aliasing.

Only rigid transforms are inverted. The Milestone 3 API intentionally excludes
a general matrix inverse, Euler-angle decomposition, and implicit conversions.

## Vulkan projection convention

The projection maps view-space depth into Vulkan's `[0, 1]` normalized depth
range. The renderer sets a negative Vulkan viewport height, which performs the
framebuffer Y correction. Consequently, `VulkanProjectionFromFov` does not
also flip Y.

## Tracking validity

Every `xrLocateSpace` result is inspected using:

- `XR_SPACE_LOCATION_POSITION_VALID_BIT`;
- `XR_SPACE_LOCATION_ORIENTATION_VALID_BIT`;
- `XR_SPACE_LOCATION_POSITION_TRACKED_BIT`;
- `XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT`.

Valid means the value can be used. Tracked indicates whether the value is
currently derived from active tracking. Applications must not assume VIEW or
STAGE poses are valid on every frame.
