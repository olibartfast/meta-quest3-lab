#pragma once

#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_session.h"
#include "xr_math/xr_math.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace questlab {

enum class DebugLineShape : int32_t {
    Axes = 0,
    Rectangle = 1,
};

struct DebugLineDraw {
    DebugLineShape shape = DebugLineShape::Axes;
    math::Mat4 model = math::IdentityMatrix();
};

class VulkanSceneProvider {
public:
    virtual ~VulkanSceneProvider() = default;

    virtual bool BuildScene(
        const XrFrameRenderInfo& frame,
        std::vector<DebugLineDraw>* draws) = 0;
};

class VulkanStereoRenderer final : public XrFrameRenderer {
public:
    VulkanStereoRenderer();
    ~VulkanStereoRenderer() override;

    VulkanStereoRenderer(const VulkanStereoRenderer&) = delete;
    VulkanStereoRenderer& operator=(const VulkanStereoRenderer&) = delete;

    bool Initialize(
        XrInstance xrInstance,
        const XrSessionContext& xrSession,
        const VulkanDeviceContext& deviceContext,
        VulkanSceneProvider* sceneProvider = nullptr);
    bool RenderFrame(
        const XrFrameRenderInfo& frame,
        const XrCompositionLayerBaseHeader** layer) override;
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab
