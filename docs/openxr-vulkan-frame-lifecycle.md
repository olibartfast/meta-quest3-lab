# OpenXR and Vulkan frame lifecycle

Milestone 2 keeps OpenXR frame pacing in `XrSessionContext` and Vulkan image
ownership in `VulkanStereoRenderer`.

For each running frame:

1. `xrWaitFrame` returns the predicted display time and `shouldRender`.
2. `xrBeginFrame` starts the frame. Every successful begin is paired with
   `xrEndFrame`, including error paths.
3. When rendering is requested, `xrLocateViews` locates both eyes in `LOCAL`
   space at the predicted display time.
4. For each eye, the renderer:
   - acquires and waits for its swapchain image;
   - waits and resets that image's Vulkan fence and command buffer;
   - clears the image and draws the triangle using the eye view and field of
     view;
   - waits for submitted GPU work to complete;
   - releases the image to OpenXR.
5. The renderer supplies two `XrCompositionLayerProjectionView` values in the
   runtime-defined view order.
6. `xrEndFrame` submits one projection layer. It submits zero layers when the
   runtime says not to render, view poses are invalid, or rendering fails.

OpenXR owns the swapchain images. Vulkan image views and framebuffers merely
reference them and are destroyed before their parent OpenXR swapchains.
Renderer shutdown waits for the Vulkan device, destroys all renderer resources
and swapchains, then the application destroys the session, Vulkan binding, and
OpenXR instance.

The implementation uses separate per-eye swapchains for clarity. A texture
array and multiview renderer is a later optimization, not a second Milestone 2
code path.
