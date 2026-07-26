# Passthrough Mixed Reality

Milestone 5 displays repository-rendered Vulkan debug geometry over one Meta
passthrough reconstruction underlay.

```bash
./scripts/build_deploy.sh --app 05-passthrough
adb logcat -s PassthroughMR:V OpenXR:V '*:S'
```

Expected headset view:

- The physical environment fills the background.
- A cyan wireframe cube appears two metres ahead at initial eye height.
- Small RGB axes below the cube remain fixed with it in LOCAL space.

The application uses no Camera2 permission and receives no camera images.
Passthrough is supplied directly by the system compositor. It is paused when
the Android activity or OpenXR session is not active.
