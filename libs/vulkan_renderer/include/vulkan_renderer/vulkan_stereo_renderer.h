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
    Ray = 2,
    Box = 3,
    ScreenRectangle = 4,
};

struct DebugLineDraw {
    DebugLineShape shape = DebugLineShape::Axes;
    math::Mat4 model = math::IdentityMatrix();
    std::array<float, 4> color = {0.0F, 0.9F, 1.0F, 1.0F};
};

struct RgbaImageQuad {
    uint64_t frameId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    math::Mat4 model = math::IdentityMatrix();
};

class VulkanSceneProvider {
public:
    virtual ~VulkanSceneProvider() = default;

    virtual bool BuildScene(
        const XrFrameRenderInfo& frame,
        std::vector<DebugLineDraw>* draws) = 0;

    virtual bool GetRgbaImageQuad(RgbaImageQuad*) {
        return false;
    }
};

struct VulkanRendererOptions {
    bool transparentBackground = false;
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
        VulkanSceneProvider* sceneProvider = nullptr,
        const VulkanRendererOptions& options = {});
    bool RenderFrame(
        const XrFrameRenderInfo& frame,
        const XrCompositionLayerBaseHeader** layer) override;
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab
