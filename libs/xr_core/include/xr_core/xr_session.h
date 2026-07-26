#pragma once

#include <openxr/openxr.h>

#include <vector>

namespace questlab {

struct XrFrameUpdateInfo {
    XrTime predictedDisplayTime = 0;
    XrSpace baseSpace = XR_NULL_HANDLE;
};

class XrFrameUpdater {
public:
    virtual ~XrFrameUpdater() = default;
    virtual bool UpdateFrame(const XrFrameUpdateInfo& frame) = 0;
};

struct XrFrameRenderInfo {
    XrTime predictedDisplayTime = 0;
    XrSpace space = XR_NULL_HANDLE;
    const XrView* views = nullptr;
    uint32_t viewCount = 0;
    XrViewStateFlags viewStateFlags = 0;
    XrSpaceLocation headInLocal{XR_TYPE_SPACE_LOCATION};
    XrSpaceLocation stageInLocal{XR_TYPE_SPACE_LOCATION};
    bool stageAvailable = false;
    bool stageBoundsAvailable = false;
    XrExtent2Df stageBounds{};
};

class XrFrameRenderer {
public:
    virtual ~XrFrameRenderer() = default;

    virtual bool RenderFrame(
        const XrFrameRenderInfo& frame,
        const XrCompositionLayerBaseHeader** layer) = 0;
};

class XrSessionContext {
public:
    XrSessionContext() = default;
    ~XrSessionContext();

    XrSessionContext(const XrSessionContext&) = delete;
    XrSessionContext& operator=(const XrSessionContext&) = delete;

    bool Initialize(
        XrInstance instance,
        XrSystemId systemId,
        const void* graphicsBindingChain);
    bool PollEvents();
    bool PumpFrame(
        XrFrameRenderer* renderer,
        XrFrameUpdater* updater = nullptr);
    bool PumpEmptyFrame();
    void RequestExit();
    void Shutdown();

    bool IsRunning() const { return running_; }
    bool ShouldExit() const { return shouldExit_; }
    XrSession Session() const { return session_; }
    XrSpace ViewSpace() const { return viewSpace_; }
    XrSpace LocalSpace() const { return localSpace_; }
    XrSpace StageSpace() const { return stageSpace_; }
    bool HasStageSpace() const { return stageSpace_ != XR_NULL_HANDLE; }
    bool HasStageBounds() const { return stageBoundsAvailable_; }
    XrExtent2Df StageBounds() const { return stageBounds_; }
    XrViewConfigurationType ViewConfiguration() const { return viewConfiguration_; }
    XrEnvironmentBlendMode BlendMode() const { return blendMode_; }
    const std::vector<XrViewConfigurationView>& ViewConfigurationViews() const {
        return viewConfigurationViews_;
    }

private:
    bool HandleSessionStateChanged(const XrEventDataSessionStateChanged& event);
    bool CreateReferenceSpaces();
    bool LocateTrackedSpaces(XrTime time, XrFrameRenderInfo* renderInfo);

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace viewSpace_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSpace stageSpace_ = XR_NULL_HANDLE;
    XrExtent2Df stageBounds_{};
    bool stageBoundsAvailable_ = false;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    XrViewConfigurationType viewConfiguration_ =
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrEnvironmentBlendMode blendMode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    std::vector<XrViewConfigurationView> viewConfigurationViews_;
    std::vector<XrView> views_;
    bool running_ = false;
    bool shouldExit_ = false;
    bool invalidViewsLogged_ = false;
};

}  // namespace questlab
