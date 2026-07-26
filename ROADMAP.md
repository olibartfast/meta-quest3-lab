# Meta Quest 3 Native XR Roadmap

## Purpose

This repository is a native C++ XR laboratory for Meta Quest 3.

The objective is to extend an existing background in C++, computer vision, GPU programming, and inference systems toward:

- native OpenXR development;
- Vulkan-based stereo rendering;
- spatial computing;
- passthrough and mixed reality;
- hand and controller interaction;
- spatial anchors;
- integration of real-time computer-vision pipelines with XR interfaces.

The project should remain primarily native. Unity or Unreal may be used only for comparison, validation, or rapid prototyping, not as the main implementation path.

---

## Agent Instructions

When implementing work from this roadmap:

1. Preserve native C++ as the main application layer.
2. Prefer OpenXR APIs over vendor-specific abstractions whenever possible.
3. Isolate Meta-specific extensions behind small interfaces.
4. Use Vulkan for rendering.
5. Keep examples small, independently buildable, and documented.
6. Avoid copying complete SDK samples without restructuring and explaining them.
7. Add a README to every application describing:
   - what it demonstrates;
   - required OpenXR extensions;
   - build and deployment commands;
   - expected behaviour on the headset;
   - known limitations.
8. Add logging and explicit error handling for every OpenXR and Vulkan call.
9. Do not introduce Unity, Unreal, or a large third-party engine unless explicitly requested.
10. Complete one milestone before adding features from later milestones.

---

## Target Repository Structure

```text
meta-quest3-lab/
├── apps/
│   ├── 01-openxr-bootstrap/
│   ├── 02-vulkan-stereo-triangle/
│   ├── 03-head-pose/
│   ├── 04-controller-input/
│   ├── 05-passthrough/
│   ├── 06-spatial-object/
│   ├── 07-hand-tracking/
│   ├── 08-spatial-anchors/
│   └── 09-cv-overlay/
├── libs/
│   ├── xr_core/
│   ├── vulkan_renderer/
│   ├── xr_math/
│   ├── spatial_ui/
│   └── perception_client/
├── docs/
├── scripts/
├── third_party/
├── README.md
└── ROADMAP.md
```

The structure may be introduced incrementally. Do not perform a large reorganization unless the current build remains functional after each step.

---

# Milestone 0 — Stabilize the Development Environment

## Goal

Make the repository reproducibly buildable and deployable from a Linux development machine to Meta Quest 3.

## Tasks

- Validate `scripts/setup_quest_dev_env.sh`.
- Validate `scripts/udev_env_setup.sh`.
- Confirm Android SDK, Android NDK, CMake, Ninja, Java, ADB, and Gradle versions.
- Verify `adb devices` detects the headset.
- Add a command that prints the complete toolchain configuration.
- Ensure paths are configurable and are not tied to one workstation.
- Add basic troubleshooting documentation.
- Confirm at least one native OpenXR sample builds, installs, launches, and logs correctly.

## Deliverables

- Reproducible setup scripts.
- `docs/development-environment.md`.
- A verified build-and-deploy command.

## Definition of Done

A clean Linux environment can execute the documented setup, build an APK, install it through ADB, and launch it on Quest 3 without manual source edits.

---

# Milestone 1 — Native OpenXR Bootstrap

**Status:** Implemented; Quest 3 acceptance is pending. Do not mark complete until three clean launch/exit cycles and the legacy regression launch pass.

## Goal

Create the smallest application owned by this repository that correctly manages the Android and OpenXR lifecycle.

## Tasks

- Create an Android NativeActivity or equivalent native entry point.
- Initialize the Android application lifecycle.
- Create `XrInstance`.
- Enumerate and log available OpenXR extensions.
- Query `XrSystemId` for the HMD.
- Create `XrSession`.
- Implement the OpenXR event loop.
- Handle session state transitions correctly.
- Create at least one reference space.
- Shut down all resources cleanly.

## Suggested Components

```text
libs/xr_core/
├── xr_instance.*
├── xr_session.*
├── xr_extensions.*
├── xr_reference_space.*
├── xr_error.*
└── xr_logging.*
```

## Definition of Done

The application launches on Quest 3, enters an active OpenXR session, logs lifecycle transitions, and exits without validation errors or leaked OpenXR handles.

---

# Milestone 2 — Vulkan Stereo Rendering

**Status:** Implemented; Quest 3/3S acceptance is pending. Do not mark complete
until the documented visual, lifecycle, validation, and regression checks pass.

## Goal

Render repository-owned graphics correctly in both eyes using OpenXR swapchains and Vulkan.

## Tasks

- Initialize Vulkan through OpenXR graphics requirements.
- Select a compatible physical device.
- Create the Vulkan instance, device, queues, and command pools.
- Enumerate OpenXR view configuration.
- Create stereo colour swapchains.
- Implement the frame loop:
  - `xrWaitFrame`;
  - `xrBeginFrame`;
  - locate views;
  - acquire swapchain images;
  - render both eyes;
  - release swapchain images;
  - `xrEndFrame`.
- Render a triangle or simple cube.
- Add optional Vulkan validation support for development builds.

## Deliverables

- `apps/02-vulkan-stereo-triangle`.
- Reusable `libs/vulkan_renderer`.
- Documentation of the OpenXR/Vulkan frame lifecycle.

## Definition of Done

A stable stereoscopic object is visible in the headset with correct per-eye projection and no persistent OpenXR or Vulkan validation errors.

---

# Milestone 3 — Tracking and Coordinate Systems

**Status:** In progress; implementation requires host, CI, and Quest 3/3S
acceptance before completion.

## Goal

Understand and expose the spatial foundations required for XR and computer-vision integration.

## Tasks

- Support `VIEW`, `LOCAL`, and `STAGE` reference spaces where available.
- Read and log head pose.
- Visualize coordinate axes.
- Implement reusable vector, matrix, pose, and quaternion conversions.
- Document coordinate-system conventions:
  - handedness;
  - axis directions;
  - units;
  - transform multiplication order;
  - OpenXR pose semantics.
- Add tests for transform composition and inversion where practical.

## Deliverables

- `apps/03-head-pose`.
- `libs/xr_math`.
- `docs/coordinate-systems.md`.

## Definition of Done

The application displays a stable world-space reference object while separately visualizing or logging the changing HMD pose.

---

# Milestone 4 — Controller Input and Interaction

Status: complete. Quest device acceptance passed.

## Goal

Implement portable OpenXR input rather than hard-coding device-specific controller events.

## Tasks

- Create an OpenXR action set.
- Add pose, trigger, grip, thumbstick, and button actions.
- Suggest bindings for Quest Touch controllers.
- Create action spaces for both hands.
- Render controller poses or rays.
- Implement ray selection of a simple virtual object.
- Add object highlighting or selection feedback.

## Deliverables

- `apps/04-controller-input`.
- Reusable action and interaction abstractions.
- `libs/xr_interaction`.
- `docs/openxr-actions.md`.

## Definition of Done

The user can point at and select a virtual object with either controller using OpenXR actions.

---

# Milestone 5 — Passthrough Mixed Reality

Status: complete. Quest visual acceptance passed.

## Goal

Create a minimal mixed-reality application with passthrough and native Vulkan content.

## Tasks

- Detect and enable the required Meta passthrough extension.
- Isolate the vendor-specific passthrough implementation behind an interface.
- Create, start, pause, resume, and destroy passthrough resources safely.
- Submit the correct composition layers.
- Render a virtual object over passthrough.
- Handle application pause and resume.

## Deliverables

- `apps/05-passthrough`.
- `libs/xr_core/passthrough_interface.*`.
- Meta-specific implementation in a clearly named module.
- `libs/xr_meta_passthrough`.
- `docs/passthrough.md`.

## Definition of Done

The headset shows passthrough with a stable repository-owned Vulkan object composited into the scene.

---

# Milestone 6 — Stable Spatial Object

## Goal

Place a virtual object in the physical environment and keep it visually stable while the user moves.

## Tasks

- Place an object relative to a `LOCAL` or `STAGE` space.
- Allow placement using a controller ray.
- Store the selected pose in application state.
- Render the object using the correct view and projection transforms.
- Add recentering and reset behaviour.
- Measure and log frame timing.

## Deliverables

- `apps/06-spatial-object`.
- A simple placement interaction.

## Definition of Done

The user can place a cube or marker in the room, move around it, and reset or reposition it without restarting the application.

---

# Milestone 7 — Hand Tracking

## Goal

Add natural hand interaction using OpenXR or Meta extensions while preserving a clean abstraction boundary.

## Tasks

- Detect hand-tracking support.
- Create trackers for left and right hands.
- Retrieve joint poses each frame.
- Render a lightweight joint skeleton.
- Implement pinch detection.
- Use pinch to select or move an object.
- Handle partial tracking and lost tracking gracefully.

## Deliverables

- `apps/07-hand-tracking`.
- Reusable hand-state representation.

## Definition of Done

Both hands are visualized when tracked, and a pinch gesture can trigger an interaction without controllers.

---

# Milestone 8 — Spatial Anchors

## Goal

Persist or restore virtual content at meaningful physical locations when supported by the runtime.

## Tasks

- Investigate the current Quest-supported anchor extensions and permissions.
- Create an anchor from a selected world-space pose.
- Create a space associated with the anchor.
- Locate the anchor every frame.
- Save and restore anchors where the platform API permits it.
- Expose anchor lifecycle states and errors in logs or UI.

## Deliverables

- `apps/08-spatial-anchors`.
- `docs/spatial-anchors.md` documenting portable and Meta-specific behaviour.

## Definition of Done

A user can create an anchor, attach a virtual marker to it, and restore it according to the capabilities currently exposed by the Quest runtime.

---

# Milestone 9 — Computer-Vision Overlay

## Goal

Connect a real-time C++ computer-vision or inference pipeline to the XR application and render its results spatially.

## Initial Architecture

```text
Camera or recorded source
        ↓
C++ perception service on PC or Jetson
        ↓
UDP, WebSocket, or another lightweight transport
        ↓
Native Quest OpenXR application
        ↓
Labels, boxes, keypoints, trajectories, or alerts
```

Do not initially make the project depend on unrestricted access to Quest passthrough RGB frames. Treat external perception and on-device XR as separate systems connected through an explicit protocol.

## Tasks

- Define a versioned perception message schema.
- Implement a network client on Quest.
- Receive timestamped detections or poses.
- Transform perception coordinates into an OpenXR reference space.
- Render one or more of:
  - labels;
  - 3D markers;
  - bounding boxes;
  - skeletal keypoints;
  - trajectories;
  - confidence indicators.
- Handle stale data, dropped packets, disconnects, and reconnection.
- Measure end-to-end latency.
- Document calibration requirements between the external camera and XR space.

## Suggested Data Model

```cpp
struct Detection3D {
    std::uint32_t id;
    float position[3];
    float extent[3];
    float confidence;
    char label[32];
};
```

Replace the example with a safe, serialized, versioned protocol before production use.

## Deliverables

- `apps/09-cv-overlay`.
- `libs/perception_client`.
- A small mock perception server.
- `docs/perception-coordinate-calibration.md`.

## Definition of Done

The Quest application receives simulated or real detections from a C++ service and renders stable, timestamp-aware spatial annotations in XR.

---

# Milestone 10 — Performance and Production Quality

## Goal

Improve the experiments with performance instrumentation, robustness, documentation, and production-quality engineering practices.

## Tasks

- Add CPU and GPU frame-time instrumentation.
- Track missed frames and stale perception data.
- Minimize allocations in the frame loop.
- Introduce RAII wrappers for OpenXR and Vulkan handles where useful.
- Add structured logging levels.
- Add CI checks for formatting and host-buildable unit tests.
- Add sanitizers for host-side libraries where applicable.
- Document Quest thermal and mobile GPU constraints.
- Add screenshots or short recordings for each completed application.
- Add an architecture diagram.

## Definition of Done

The repository demonstrates clean native architecture, repeatable builds, measured performance, and multiple independently understandable XR examples.

---

# Recommended Execution Order

1. Stabilize build and deployment.
2. Implement the OpenXR lifecycle from first principles.
3. Add Vulkan stereo rendering.
4. Master poses, spaces, and transforms.
5. Add controller interaction.
6. Add passthrough.
7. Implement stable object placement.
8. Add hand tracking.
9. Add anchors.
10. Integrate an external CV pipeline.
11. Profile, document, and consolidate the project.

---

# First Agent Assignment

The first coding-agent task should be:

> Inspect the existing repository and implement or repair the smallest repository-owned native OpenXR application that launches on Meta Quest 3. It must create an OpenXR instance and session, handle Android and OpenXR lifecycle events, log available extensions and session-state transitions, build through the existing scripts, install through ADB, and shut down cleanly. Preserve the current working setup and document every command required to reproduce the result. Do not add Unity or Unreal.

Expected output:

- a concise summary of the existing architecture;
- a list of files added or changed;
- a working build and deployment path;
- documentation of assumptions and unresolved platform issues;
- no unrelated refactoring.
