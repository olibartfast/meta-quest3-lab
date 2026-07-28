# Hand Tracking

Milestone 7 adds portable `XR_EXT_hand_tracking` joints and pinch interaction
to the passthrough renderer.

```bash
./scripts/build_deploy.sh --app 07-hand-tracking
adb logcat -s HandTracking:V OpenXR:V '*:S'
```

Expected headset behaviour:

- Passthrough fills the background and a yellow guide appears ahead.
- The left skeleton and joint markers are cyan; the right ones are white.
- Thumb/index pinch markers turn orange while pinched.
- The first pinch places a green box.
- Starting another pinch within 15 cm of the box grabs it; the box follows
  that pinch until release or tracking loss.
- If both pinches start in the same frame, the right hand wins.
- The object remains in OpenXR `LOCAL` space for the current session.

Enable hand tracking in Quest system settings and put the controllers down.
The app requires hand-tracking and passthrough support.
