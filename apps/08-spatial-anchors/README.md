# Spatial Anchors

Milestone 8 creates one Meta spatial anchor, persists its UUID in app-private
storage, restores it after relaunch, and erases it explicitly.

```bash
./scripts/build_deploy.sh --app 08-spatial-anchors
adb logcat -s SpatialAnchors:V OpenXR:V '*:S'
```

- Aim a controller and press its trigger to create and save an anchor.
- Press A or X to erase it.
- Yellow is the placement guide, amber is an asynchronous operation, bright
  green is tracked, dim green is inferred, and red is an error.
- The marker is hidden while its anchor pose is invalid.
