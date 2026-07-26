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

const char* ReferenceSpaceName(XrReferenceSpaceType type) {
    switch (type) {
        case XR_REFERENCE_SPACE_TYPE_VIEW: return "VIEW";
        case XR_REFERENCE_SPACE_TYPE_LOCAL: return "LOCAL";
        case XR_REFERENCE_SPACE_TYPE_STAGE: return "STAGE";
        default: return "OTHER";
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

    uint32_t viewCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurationViews(
                instance_,
                systemId,
                viewConfiguration_,
                0,
                &viewCount,
                nullptr),
            "xrEnumerateViewConfigurationViews(count)")) {
        return false;
    }
    viewConfigurationViews_.assign(
        viewCount,
        XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!CheckXr(
            instance_,
            xrEnumerateViewConfigurationViews(
                instance_,
                systemId,
                viewConfiguration_,
                viewCount,
                &viewCount,
                viewConfigurationViews_.data()),
            "xrEnumerateViewConfigurationViews(list)")) {
        viewConfigurationViews_.clear();
        return false;
    }
    if (viewCount != 2) {
        LogError("PRIMARY_STEREO reported %u views instead of 2", viewCount);
        viewConfigurationViews_.clear();
        return false;
    }
    views_.assign(viewCount, XrView{XR_TYPE_VIEW});
    for (uint32_t index = 0; index < viewCount; ++index) {
        const XrViewConfigurationView& view = viewConfigurationViews_[index];
        LogInfo(
            "Stereo view %u: recommended %ux%u, sample count %u",
            index,
            view.recommendedImageRectWidth,
            view.recommendedImageRectHeight,
            view.recommendedSwapchainSampleCount);
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

    if (!CreateReferenceSpaces()) {
        Shutdown();
        return false;
    }
    return true;
}

bool XrSessionContext::CreateReferenceSpaces() {
    uint32_t typeCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateReferenceSpaces(
                session_, 0, &typeCount, nullptr),
            "xrEnumerateReferenceSpaces(count)")) {
        return false;
    }
    std::vector<XrReferenceSpaceType> types(typeCount);
    if (!CheckXr(
            instance_,
            xrEnumerateReferenceSpaces(
                session_, typeCount, &typeCount, types.data()),
            "xrEnumerateReferenceSpaces(list)")) {
        return false;
    }
    for (XrReferenceSpaceType type : types) {
        LogInfo("Reference space supported: %s", ReferenceSpaceName(type));
    }
    const auto supports = [&types](XrReferenceSpaceType type) {
        return std::find(types.begin(), types.end(), type) != types.end();
    };
    if (!supports(XR_REFERENCE_SPACE_TYPE_VIEW) ||
        !supports(XR_REFERENCE_SPACE_TYPE_LOCAL)) {
        LogError("Runtime must support both VIEW and LOCAL reference spaces");
        return false;
    }

    XrReferenceSpaceCreateInfo spaceCreateInfo{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0F;
    if (!CheckXr(
            instance_,
            xrCreateReferenceSpace(session_, &spaceCreateInfo, &localSpace_),
            "xrCreateReferenceSpace(LOCAL)")) {
        return false;
    }
    LogInfo("LOCAL reference space created");

    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!CheckXr(
            instance_,
            xrCreateReferenceSpace(session_, &spaceCreateInfo, &viewSpace_),
            "xrCreateReferenceSpace(VIEW)")) {
        return false;
    }
    LogInfo("VIEW reference space created");

    if (!supports(XR_REFERENCE_SPACE_TYPE_STAGE)) {
        LogInfo("STAGE reference space is unavailable");
        return true;
    }
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (!CheckXr(
            instance_,
            xrCreateReferenceSpace(session_, &spaceCreateInfo, &stageSpace_),
            "xrCreateReferenceSpace(STAGE)")) {
        return false;
    }
    LogInfo("STAGE reference space created");

    const XrResult boundsResult = xrGetReferenceSpaceBoundsRect(
        session_,
        XR_REFERENCE_SPACE_TYPE_STAGE,
        &stageBounds_);
    if (boundsResult == XR_SPACE_BOUNDS_UNAVAILABLE) {
        LogInfo("STAGE bounds are unavailable");
    } else if (!CheckXr(
            instance_,
            boundsResult,
            "xrGetReferenceSpaceBoundsRect(STAGE)")) {
        return false;
    } else {
        stageBoundsAvailable_ = true;
        LogInfo(
            "STAGE bounds: %.3f x %.3f metres",
            stageBounds_.width,
            stageBounds_.height);
    }
    return true;
}

bool XrSessionContext::PollEvents(XrEventObserver* observer) {
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
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
                const auto* change =
                    reinterpret_cast<
                        const XrEventDataReferenceSpaceChangePending*>(&event);
                LogInfo(
                    "%s reference space change pending at time %lld",
                    ReferenceSpaceName(change->referenceSpaceType),
                    static_cast<long long>(change->changeTime));
                break;
            }
            default:
                LogInfo("OpenXR event type: %d", event.type);
                break;
        }
        if (observer != nullptr && !observer->HandleEvent(event)) {
            LogError("OpenXR event observer requested exit");
            shouldExit_ = true;
            return false;
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

bool XrSessionContext::LocateTrackedSpaces(
    XrTime time,
    XrFrameRenderInfo* renderInfo) {
    renderInfo->headInLocal = XrSpaceLocation{XR_TYPE_SPACE_LOCATION};
    if (!CheckXr(
            instance_,
            xrLocateSpace(
                viewSpace_,
                localSpace_,
                time,
                &renderInfo->headInLocal),
            "xrLocateSpace(VIEW in LOCAL)")) {
        return false;
    }
    renderInfo->stageAvailable = stageSpace_ != XR_NULL_HANDLE;
    renderInfo->stageBoundsAvailable = stageBoundsAvailable_;
    renderInfo->stageBounds = stageBounds_;
    renderInfo->stageInLocal = XrSpaceLocation{XR_TYPE_SPACE_LOCATION};
    if (stageSpace_ != XR_NULL_HANDLE &&
        !CheckXr(
            instance_,
            xrLocateSpace(
                stageSpace_,
                localSpace_,
                time,
                &renderInfo->stageInLocal),
            "xrLocateSpace(STAGE in LOCAL)")) {
        return false;
    }
    return true;
}

bool XrSessionContext::PumpFrame(
    XrFrameRenderer* renderer,
    XrFrameUpdater* updater,
    XrUnderlayProvider* underlayProvider) {
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

    bool frameSucceeded = true;
    const XrCompositionLayerBaseHeader* layer = nullptr;
    if (updater != nullptr &&
        !updater->UpdateFrame({
            frameState.predictedDisplayTime,
            localSpace_,
        })) {
        LogError("Frame updater failed; submitting an empty frame");
        frameSucceeded = false;
    }
    if (frameSucceeded &&
        frameState.shouldRender == XR_TRUE &&
        renderer != nullptr) {
        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = viewConfiguration_;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = localSpace_;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        if (!CheckXr(
                instance_,
                xrLocateViews(
                    session_,
                    &locateInfo,
                    &viewState,
                    static_cast<uint32_t>(views_.size()),
                    &viewCount,
                    views_.data()),
                "xrLocateViews")) {
            frameSucceeded = false;
        } else if (viewCount != views_.size()) {
            LogError(
                "xrLocateViews returned %u views; expected %zu",
                viewCount,
                views_.size());
            frameSucceeded = false;
        } else {
            constexpr XrViewStateFlags kRequiredViewFlags =
                XR_VIEW_STATE_POSITION_VALID_BIT |
                XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            if ((viewState.viewStateFlags & kRequiredViewFlags) !=
                kRequiredViewFlags) {
                if (!invalidViewsLogged_) {
                    LogWarning(
                        "Stereo view poses are not valid; submitting an empty frame");
                    invalidViewsLogged_ = true;
                }
            } else {
                invalidViewsLogged_ = false;
                XrFrameRenderInfo renderInfo;
                renderInfo.predictedDisplayTime = frameState.predictedDisplayTime;
                renderInfo.space = localSpace_;
                renderInfo.views = views_.data();
                renderInfo.viewCount = viewCount;
                renderInfo.viewStateFlags = viewState.viewStateFlags;
                if (!LocateTrackedSpaces(
                        frameState.predictedDisplayTime,
                        &renderInfo)) {
                    frameSucceeded = false;
                } else if (!renderer->RenderFrame(renderInfo, &layer)) {
                    LogError("Frame renderer failed; submitting an empty frame");
                    layer = nullptr;
                    frameSucceeded = false;
                }
            }
        }
    }

    std::vector<const XrCompositionLayerBaseHeader*> layers;
    layers.reserve(2);
    if (frameSucceeded &&
        frameState.shouldRender == XR_TRUE &&
        underlayProvider != nullptr &&
        !underlayProvider->AppendUnderlayLayers(
            frameState.predictedDisplayTime,
            &layers)) {
        LogError("Underlay provider failed; submitting an empty frame");
        frameSucceeded = false;
        layers.clear();
    }
    if (frameSucceeded && layer != nullptr) {
        layers.push_back(layer);
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = blendMode_;
    endInfo.layerCount = static_cast<uint32_t>(layers.size());
    endInfo.layers = layers.empty() ? nullptr : layers.data();
    const bool endSucceeded = CheckXr(
            instance_,
            xrEndFrame(session_, &endInfo),
            layers.empty()
                ? "xrEndFrame(empty)"
                : layers.size() == 1
                    ? layer == nullptr
                        ? "xrEndFrame(underlay)"
                        : "xrEndFrame(projection)"
                    : "xrEndFrame(underlay+projection)");
    if (!endSucceeded || !frameSucceeded) {
        shouldExit_ = true;
        return false;
    }
    return true;
}

bool XrSessionContext::PumpEmptyFrame() {
    return PumpFrame(nullptr, nullptr);
}

void XrSessionContext::RequestExit() {
    shouldExit_ = true;
}

void XrSessionContext::Shutdown() {
    if (stageSpace_ != XR_NULL_HANDLE) {
        CheckXr(instance_, xrDestroySpace(stageSpace_), "xrDestroySpace(STAGE)");
        stageSpace_ = XR_NULL_HANDLE;
        LogInfo("STAGE reference space destroyed");
    }
    if (viewSpace_ != XR_NULL_HANDLE) {
        CheckXr(instance_, xrDestroySpace(viewSpace_), "xrDestroySpace(VIEW)");
        viewSpace_ = XR_NULL_HANDLE;
        LogInfo("VIEW reference space destroyed");
    }
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
    viewConfigurationViews_.clear();
    views_.clear();
    invalidViewsLogged_ = false;
    stageBounds_ = {};
    stageBoundsAvailable_ = false;
}

}  // namespace questlab
