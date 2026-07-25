#include "xr_core/xr_session.h"

#include "xr_core/xr_error.h"

#include <algorithm>
#include <array>
#include <vector>

namespace questlab {
namespace {

const char* SessionStateName(XrSessionState state) {
    switch (state) {
        case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
        case XR_SESSION_STATE_IDLE: return "IDLE";
        case XR_SESSION_STATE_READY: return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
        case XR_SESSION_STATE_STOPPING: return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING: return "EXITING";
        default: return "INVALID";
    }
}

const char* BlendModeName(XrEnvironmentBlendMode mode) {
    switch (mode) {
        case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "OPAQUE";
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "ADDITIVE";
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
        default: return "INVALID";
    }
}

}  // namespace

XrSessionContext::~XrSessionContext() {
    Shutdown();
}

bool XrSessionContext::Initialize(
    XrInstance instance,
    XrSystemId systemId,
    const void* graphicsBindingChain) {
    if (session_ != XR_NULL_HANDLE) {
        return true;
    }
    instance_ = instance;

    uint32_t viewConfigurationCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurations(
                instance_, systemId, 0, &viewConfigurationCount, nullptr),
            "xrEnumerateViewConfigurations(count)")) {
        return false;
    }
    std::vector<XrViewConfigurationType> viewConfigurations(viewConfigurationCount);
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurations(
                instance_,
                systemId,
                viewConfigurationCount,
                &viewConfigurationCount,
                viewConfigurations.data()),
            "xrEnumerateViewConfigurations(list)")) {
        return false;
    }
    if (std::find(
            viewConfigurations.begin(),
            viewConfigurations.end(),
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) == viewConfigurations.end()) {
        LogError("PRIMARY_STEREO view configuration is unavailable");
        return false;
    }

    uint32_t blendModeCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateEnvironmentBlendModes(
                instance_,
                systemId,
                viewConfiguration_,
                0,
                &blendModeCount,
                nullptr),
            "xrEnumerateEnvironmentBlendModes(count)")) {
        return false;
    }
    std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
    if (!CheckXr(
            instance_,
            xrEnumerateEnvironmentBlendModes(
                instance_,
                systemId,
                viewConfiguration_,
                blendModeCount,
                &blendModeCount,
                blendModes.data()),
            "xrEnumerateEnvironmentBlendModes(list)")) {
        return false;
    }
    constexpr std::array<XrEnvironmentBlendMode, 3> kBlendPreference = {
        XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
        XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
    };
    const auto selectedBlendMode = std::find_first_of(
        kBlendPreference.begin(),
        kBlendPreference.end(),
        blendModes.begin(),
        blendModes.end());
    if (selectedBlendMode == kBlendPreference.end()) {
        LogError("Runtime reported no supported environment blend mode");
        return false;
    }
    blendMode_ = *selectedBlendMode;
    LogInfo("Selected PRIMARY_STEREO with %s blend mode", BlendModeName(blendMode_));

    XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = graphicsBindingChain;
    sessionCreateInfo.systemId = systemId;
    if (!CheckXr(
            instance_,
            xrCreateSession(instance_, &sessionCreateInfo, &session_),
            "xrCreateSession")) {
        return false;
    }
    LogInfo("OpenXR session created");

    XrReferenceSpaceCreateInfo spaceCreateInfo{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0F;
    if (!CheckXr(
            instance_,
            xrCreateReferenceSpace(session_, &spaceCreateInfo, &localSpace_),
            "xrCreateReferenceSpace(LOCAL)")) {
        Shutdown();
        return false;
    }
    LogInfo("LOCAL reference space created");
    return true;
}

bool XrSessionContext::PollEvents() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (true) {
        const XrResult result = xrPollEvent(instance_, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return true;
        }
        if (!CheckXr(instance_, result, "xrPollEvent")) {
            shouldExit_ = true;
            return false;
        }

        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                if (!HandleSessionStateChanged(
                        *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event))) {
                    return false;
                }
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                LogError("OpenXR instance loss pending; exiting");
                shouldExit_ = true;
                break;
            case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
                const auto* lost =
                    reinterpret_cast<const XrEventDataEventsLost*>(&event);
                LogError("OpenXR runtime reported %u lost events", lost->lostEventCount);
                break;
            }
            default:
                LogInfo("OpenXR event type: %d", event.type);
                break;
        }
        event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool XrSessionContext::HandleSessionStateChanged(
    const XrEventDataSessionStateChanged& event) {
    if (event.session != session_) {
        LogError("Received a state change for an unknown OpenXR session");
        shouldExit_ = true;
        return false;
    }
    LogInfo(
        "OpenXR session state: %s -> %s",
        SessionStateName(state_),
        SessionStateName(event.state));
    state_ = event.state;

    if (state_ == XR_SESSION_STATE_READY && !running_) {
        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = viewConfiguration_;
        if (!CheckXr(
                instance_,
                xrBeginSession(session_, &beginInfo),
                "xrBeginSession")) {
            shouldExit_ = true;
            return false;
        }
        running_ = true;
        LogInfo("OpenXR session begun");
    } else if (state_ == XR_SESSION_STATE_STOPPING && running_) {
        if (!CheckXr(instance_, xrEndSession(session_), "xrEndSession")) {
            shouldExit_ = true;
            return false;
        }
        running_ = false;
        LogInfo("OpenXR session ended");
    } else if (
        state_ == XR_SESSION_STATE_EXITING ||
        state_ == XR_SESSION_STATE_LOSS_PENDING) {
        shouldExit_ = true;
    }
    return true;
}

bool XrSessionContext::PumpEmptyFrame() {
    if (!running_) {
        return true;
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (!CheckXr(
            instance_,
            xrWaitFrame(session_, &waitInfo, &frameState),
            "xrWaitFrame")) {
        shouldExit_ = true;
        return false;
    }

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!CheckXr(
            instance_,
            xrBeginFrame(session_, &beginInfo),
            "xrBeginFrame")) {
        shouldExit_ = true;
        return false;
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = blendMode_;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;
    if (!CheckXr(
            instance_,
            xrEndFrame(session_, &endInfo),
            "xrEndFrame(empty)")) {
        shouldExit_ = true;
        return false;
    }
    return true;
}

void XrSessionContext::RequestExit() {
    shouldExit_ = true;
}

void XrSessionContext::Shutdown() {
    if (localSpace_ != XR_NULL_HANDLE) {
        CheckXr(instance_, xrDestroySpace(localSpace_), "xrDestroySpace(LOCAL)");
        localSpace_ = XR_NULL_HANDLE;
        LogInfo("LOCAL reference space destroyed");
    }
    if (session_ != XR_NULL_HANDLE) {
        // xrEndSession is only legal after the runtime enters STOPPING. When
        // Android destroys the activity earlier, destroying the session is the
        // deterministic fallback and avoids an invalid-state xrEndSession call.
        running_ = false;
        CheckXr(instance_, xrDestroySession(session_), "xrDestroySession");
        session_ = XR_NULL_HANDLE;
        LogInfo("OpenXR session destroyed");
    }
    instance_ = XR_NULL_HANDLE;
    state_ = XR_SESSION_STATE_UNKNOWN;
}

}  // namespace questlab
