# Controller Input

Milestone 4 demonstrates portable OpenXR controller actions, tracked grip and
aim poses, ray hover, trigger selection, and core haptics.

Build, install, and launch:

```bash
./scripts/build_deploy.sh --app 04-controller-input
adb logcat -s ControllerInput:V OpenXR:V '*:S'
```

Expected headset view:

- Small RGB axes at each tracked grip pose.
- A white aim ray from each controller.
- A white wireframe cube fixed two metres ahead at the initial eye height.
- Ray and cube turn yellow on hover.
- Pressing either trigger selects the cube, turns it green, and pulses that
  controller.
- X or A clears selection.

The system Meta button is intentionally not bound. When the system dashboard
owns input, controller actions become inactive and the controller visuals
disappear.
