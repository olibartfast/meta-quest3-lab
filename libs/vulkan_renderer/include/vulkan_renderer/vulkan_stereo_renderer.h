#pragma once

#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_session.h"

#include <memory>

namespace questlab {

class VulkanStereoRenderer final : public XrFrameRenderer {
public:
    VulkanStereoRenderer();
    ~VulkanStereoRenderer() override;

    VulkanStereoRenderer(const VulkanStereoRenderer&) = delete;
    VulkanStereoRenderer& operator=(const VulkanStereoRenderer&) = delete;

    bool Initialize(
        XrInstance xrInstance,
        const XrSessionContext& xrSession,
        const VulkanDeviceContext& deviceContext);
    bool RenderFrame(
        const XrFrameRenderInfo& frame,
        const XrCompositionLayerBaseHeader** layer) override;
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab
