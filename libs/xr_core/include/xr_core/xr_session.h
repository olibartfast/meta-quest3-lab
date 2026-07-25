#pragma once

#include <openxr/openxr.h>

namespace questlab {

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
    bool PumpEmptyFrame();
    void RequestExit();
    void Shutdown();

    bool IsRunning() const { return running_; }
    bool ShouldExit() const { return shouldExit_; }

private:
    bool HandleSessionStateChanged(const XrEventDataSessionStateChanged& event);

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    XrViewConfigurationType viewConfiguration_ =
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrEnvironmentBlendMode blendMode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool running_ = false;
    bool shouldExit_ = false;
};

}  // namespace questlab
