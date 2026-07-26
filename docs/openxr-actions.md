# OpenXR Actions

Milestone 4 uses one portable action set shared by left and right controller
subaction paths. The action set contains grip and aim poses, trigger, squeeze,
thumbstick, X/A, Y/B, thumbstick click, and vibration.

## Lifecycle

The application follows this order:

1. Create the action set and all actions.
2. Suggest bindings for Oculus Touch and Khronos simple controllers.
3. Create left and right grip/aim action spaces.
4. Attach the action set once.
5. Each running frame, call `xrSyncActions`, query action states, and locate
   active action spaces at `XrFrameState::predictedDisplayTime`.
6. Destroy action spaces and the action set before destroying the session.

Pose input is usable only when both the pose action is active and the returned
space location has valid position and orientation. Tracking flags are retained
for diagnostics. When the Meta dashboard owns input, actions can become
inactive; the application removes controller visuals instead of reusing stale
poses.

## Bindings

The full binding targets:

```text
/interaction_profiles/oculus/touch_controller
```

It uses the standard grip pose, aim pose, trigger value, squeeze value,
thumbstick, thumbstick click, X/Y or A/B, and haptic paths. The Meta/system
button is deliberately not bound.

A limited `/interaction_profiles/khr/simple_controller` fallback provides
grip, aim, select, and haptics. A boolean select binding is converted by
OpenXR to the trigger float action as zero or one.

The current profile for each hand is read with
`xrGetCurrentInteractionProfile` and logged when it changes.

## Interaction contract

Aim rays originate at the valid aim pose and point along controller-local
negative Z. Rendering and ray/AABB testing use the same three-metre maximum.
Trigger selection uses 0.75 press and 0.55 release thresholds, so a held
trigger emits one rising edge. Selection stays latched until X or A is pressed.
If both hands select in one frame, the closest hit wins and an exact tie goes
to the left hand.

The target is fixed in LOCAL space two metres along the horizontal projection
of the first valid head-forward direction, at the initial eye height. Ignoring
startup pitch keeps it visible ahead while still avoiding any assumption that
LOCAL zero is floor height.

## Device test

```bash
./scripts/build_deploy.sh --app 04-controller-input
adb logcat -s ControllerInput:V OpenXR:V '*:S'
```

Verify both grip axes and aim rays track independently, hover is yellow,
trigger selection is green with a short haptic pulse, and X/A clears it.
Open the Meta dashboard and confirm controller visuals disappear. Quit and
repeat three times, checking for orderly renderer, action, session, Vulkan,
and instance teardown.
