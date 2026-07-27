# Spatial Object

Milestone 6 combines controller placement with Meta passthrough. One object
is stored in OpenXR `LOCAL` space for stable in-session rendering.

```bash
./scripts/build_deploy.sh --app 06-spatial-object
adb logcat -s SpatialObject:V OpenXR:V '*:S'
```

Expected headset behaviour:

- Passthrough fills the background.
- A large yellow placement guide appears 1.5 metres ahead immediately, even
  before controllers become active.
- Both tracked controllers show grip axes, white aim rays, and cyan preview
  boxes two metres along each ray.
- Pulling either trigger places or repositions one green wireframe box and
  pulses that controller.
- X or A clears the placed object and restores the yellow guide.
- The placed box and its RGB axes remain fixed while the user moves.
- Frame-cadence mean and maximum are logged once per second.

The object is not a spatial anchor. A system recenter may move its physical
location because the stored coordinates remain relative to `LOCAL`; place it
again with either trigger after recentering.
