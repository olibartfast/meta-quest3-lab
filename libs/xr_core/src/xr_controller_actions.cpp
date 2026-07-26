#include "xr_core/xr_controller_actions.h"

#include "xr_core/xr_error.h"
#include "xr_math/openxr_conversions.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace questlab {
namespace {

constexpr std::array<const char*, 2> kHandPathNames = {
    "/user/hand/left",
    "/user/hand/right",
};

bool StringToPath(
    XrInstance instance,
    const char* pathString,
    XrPath* path) {
    return CheckXr(
        instance,
        xrStringToPath(instance, pathString, path),
        pathString);
}

bool CreateAction(
    XrInstance instance,
    XrActionSet actionSet,
    XrActionType type,
    const char* name,
    const char* localizedName,
    const std::array<XrPath, 2>& handPaths,
    XrAction* action) {
    XrActionCreateInfo createInfo{XR_TYPE_ACTION_CREATE_INFO};
    createInfo.actionType = type;
    std::strncpy(
        createInfo.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(
        createInfo.localizedActionName,
        localizedName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    createInfo.countSubactionPaths =
        static_cast<uint32_t>(handPaths.size());
    createInfo.subactionPaths = handPaths.data();
    return CheckXr(
        instance,
        xrCreateAction(actionSet, &createInfo, action),
        name);
}

bool IsPoseValid(XrSpaceLocationFlags flags) {
    constexpr XrSpaceLocationFlags kValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (flags & kValid) == kValid;
}

std::string PathString(XrInstance instance, XrPath path) {
    if (path == XR_NULL_PATH) {
        return "<none>";
    }
    uint32_t length = 0;
    if (XR_FAILED(xrPathToString(instance, path, 0, &length, nullptr))) {
        return "<unavailable>";
    }
    std::vector<char> buffer(length);
    if (XR_FAILED(
            xrPathToString(
                instance, path, length, &length, buffer.data()))) {
        return "<unavailable>";
    }
    return buffer.data();
}

}  // namespace

XrControllerActions::~XrControllerActions() {
    Shutdown();
}

bool XrControllerActions::Initialize(
    XrInstance instance,
    XrSession session) {
    if (actionSet_ != XR_NULL_HANDLE) {
        return true;
    }
    instance_ = instance;
    session_ = session;
    for (std::size_t hand = 0; hand < handPaths_.size(); ++hand) {
        if (!StringToPath(
                instance_, kHandPathNames[hand], &handPaths_[hand])) {
            Shutdown();
            return false;
        }
    }
    if (!CreateActions() ||
        !SuggestBindings() ||
        !CreateActionSpaces() ||
        !AttachActionSet()) {
        Shutdown();
        return false;
    }
    LogInfo("Controller action set attached");
    return true;
}

bool XrControllerActions::CreateActions() {
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(
        setInfo.actionSetName,
        "controllers",
        XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(
        setInfo.localizedActionSetName,
        "Controllers",
        XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    setInfo.priority = 0;
    if (!CheckXr(
            instance_,
            xrCreateActionSet(instance_, &setInfo, &actionSet_),
            "xrCreateActionSet(controllers)")) {
        return false;
    }

    return
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_POSE_INPUT,
                     "grip_pose", "Grip Pose", handPaths_, &gripPoseAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_POSE_INPUT,
                     "aim_pose", "Aim Pose", handPaths_, &aimPoseAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_FLOAT_INPUT,
                     "trigger", "Trigger", handPaths_, &triggerAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_FLOAT_INPUT,
                     "squeeze", "Squeeze", handPaths_, &squeezeAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_VECTOR2F_INPUT,
                     "thumbstick", "Thumbstick", handPaths_, &thumbstickAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_BOOLEAN_INPUT,
                     "primary", "Primary Button", handPaths_, &primaryAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_BOOLEAN_INPUT,
                     "secondary", "Secondary Button", handPaths_, &secondaryAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_BOOLEAN_INPUT,
                     "stick_click", "Thumbstick Click", handPaths_,
                     &thumbstickClickAction_) &&
        CreateAction(instance_, actionSet_, XR_ACTION_TYPE_VIBRATION_OUTPUT,
                     "vibration", "Vibration", handPaths_, &vibrationAction_);
}

bool XrControllerActions::SuggestBindings() {
    const auto path = [this](const char* value, XrPath* output) {
        return StringToPath(instance_, value, output);
    };
    XrPath touchProfile = XR_NULL_PATH;
    XrPath simpleProfile = XR_NULL_PATH;
    if (!path(
            "/interaction_profiles/oculus/touch_controller",
            &touchProfile) ||
        !path(
            "/interaction_profiles/khr/simple_controller",
            &simpleProfile)) {
        return false;
    }

    std::vector<XrActionSuggestedBinding> touchBindings;
    std::vector<XrActionSuggestedBinding> simpleBindings;
    const auto add = [&](std::vector<XrActionSuggestedBinding>* bindings,
                         XrAction action,
                         const std::string& bindingPath) {
        XrPath binding = XR_NULL_PATH;
        if (!path(bindingPath.c_str(), &binding)) {
            return false;
        }
        bindings->push_back({action, binding});
        return true;
    };
    for (std::size_t hand = 0; hand < 2; ++hand) {
        const std::string root = kHandPathNames[hand];
        if (!add(&touchBindings, gripPoseAction_, root + "/input/grip/pose") ||
            !add(&touchBindings, aimPoseAction_, root + "/input/aim/pose") ||
            !add(&touchBindings, triggerAction_, root + "/input/trigger/value") ||
            !add(&touchBindings, squeezeAction_, root + "/input/squeeze/value") ||
            !add(&touchBindings, thumbstickAction_, root + "/input/thumbstick") ||
            !add(&touchBindings, thumbstickClickAction_,
                 root + "/input/thumbstick/click") ||
            !add(&touchBindings, vibrationAction_, root + "/output/haptic") ||
            !add(&simpleBindings, gripPoseAction_, root + "/input/grip/pose") ||
            !add(&simpleBindings, aimPoseAction_, root + "/input/aim/pose") ||
            !add(&simpleBindings, triggerAction_, root + "/input/select/click") ||
            !add(&simpleBindings, vibrationAction_, root + "/output/haptic")) {
            return false;
        }
        const bool left = hand == 0;
        if (!add(&touchBindings, primaryAction_,
                 root + (left ? "/input/x/click" : "/input/a/click")) ||
            !add(&touchBindings, secondaryAction_,
                 root + (left ? "/input/y/click" : "/input/b/click"))) {
            return false;
        }
    }

    XrInteractionProfileSuggestedBinding suggested{
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = touchProfile;
    suggested.countSuggestedBindings =
        static_cast<uint32_t>(touchBindings.size());
    suggested.suggestedBindings = touchBindings.data();
    if (!CheckXr(
            instance_,
            xrSuggestInteractionProfileBindings(instance_, &suggested),
            "xrSuggestInteractionProfileBindings(Touch)")) {
        return false;
    }
    suggested.interactionProfile = simpleProfile;
    suggested.countSuggestedBindings =
        static_cast<uint32_t>(simpleBindings.size());
    suggested.suggestedBindings = simpleBindings.data();
    return CheckXr(
        instance_,
        xrSuggestInteractionProfileBindings(instance_, &suggested),
        "xrSuggestInteractionProfileBindings(simple)");
}

bool XrControllerActions::CreateActionSpaces() {
    XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    spaceInfo.poseInActionSpace.orientation.w = 1.0F;
    for (std::size_t hand = 0; hand < 2; ++hand) {
        spaceInfo.subactionPath = handPaths_[hand];
        spaceInfo.action = gripPoseAction_;
        if (!CheckXr(
                instance_,
                xrCreateActionSpace(
                    session_, &spaceInfo, &gripSpaces_[hand]),
                "xrCreateActionSpace(grip)")) {
            return false;
        }
        spaceInfo.action = aimPoseAction_;
        if (!CheckXr(
                instance_,
                xrCreateActionSpace(
                    session_, &spaceInfo, &aimSpaces_[hand]),
                "xrCreateActionSpace(aim)")) {
            return false;
        }
    }
    return true;
}

bool XrControllerActions::AttachActionSet() {
    XrSessionActionSetsAttachInfo attachInfo{
        XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &actionSet_;
    return CheckXr(
        instance_,
        xrAttachSessionActionSets(session_, &attachInfo),
        "xrAttachSessionActionSets");
}

bool XrControllerActions::UpdateFrame(
    const XrFrameUpdateInfo& frame) {
    XrActiveActionSet activeActionSet{actionSet_, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    if (!CheckXr(
            instance_,
            xrSyncActions(session_, &syncInfo),
            "xrSyncActions")) {
        return false;
    }
    LogInteractionProfileChanges();
    return UpdateHand(0, frame) && UpdateHand(1, frame);
}

bool XrControllerActions::UpdateHand(
    std::size_t hand,
    const XrFrameUpdateInfo& frame) {
    XrControllerState& state = states_[hand];
    const bool previousAimActive = state.aim.active;
    const bool previousGripActive = state.grip.active;
    state.stateChanged = false;

    const auto getPose = [&](XrAction action,
                             XrSpace space,
                             XrControllerPoseState* pose) {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = handPaths_[hand];
        XrActionStatePose actionState{XR_TYPE_ACTION_STATE_POSE};
        if (!CheckXr(
                instance_,
                xrGetActionStatePose(session_, &getInfo, &actionState),
                "xrGetActionStatePose")) {
            return false;
        }
        pose->active = actionState.isActive == XR_TRUE;
        pose->valid = false;
        pose->positionTracked = false;
        pose->orientationTracked = false;
        if (!pose->active) {
            return true;
        }
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if (!CheckXr(
                instance_,
                xrLocateSpace(
                    space,
                    frame.baseSpace,
                    frame.predictedDisplayTime,
                    &location),
                "xrLocateSpace(controller)")) {
            return false;
        }
        pose->valid = IsPoseValid(location.locationFlags);
        pose->positionTracked =
            (location.locationFlags &
             XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
        pose->orientationTracked =
            (location.locationFlags &
             XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
        if (pose->valid) {
            pose->pose = math::FromXr(location.pose);
        }
        return true;
    };
    const auto getFloat = [&](XrAction action,
                              bool* active,
                              float* value) {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = handPaths_[hand];
        XrActionStateFloat actionState{XR_TYPE_ACTION_STATE_FLOAT};
        if (!CheckXr(
                instance_,
                xrGetActionStateFloat(session_, &getInfo, &actionState),
                "xrGetActionStateFloat")) {
            return false;
        }
        *active = actionState.isActive == XR_TRUE;
        *value = *active ? actionState.currentState : 0.0F;
        state.stateChanged |= actionState.changedSinceLastSync == XR_TRUE;
        return true;
    };
    const auto getBoolean = [&](XrAction action,
                                bool* active,
                                bool* value) {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = handPaths_[hand];
        XrActionStateBoolean actionState{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (!CheckXr(
                instance_,
                xrGetActionStateBoolean(session_, &getInfo, &actionState),
                "xrGetActionStateBoolean")) {
            return false;
        }
        *active = actionState.isActive == XR_TRUE;
        *value = *active && actionState.currentState == XR_TRUE;
        state.stateChanged |= actionState.changedSinceLastSync == XR_TRUE;
        return true;
    };

    if (!getPose(aimPoseAction_, aimSpaces_[hand], &state.aim) ||
        !getPose(gripPoseAction_, gripSpaces_[hand], &state.grip) ||
        !getFloat(
            triggerAction_, &state.triggerActive, &state.trigger) ||
        !getFloat(
            squeezeAction_, &state.squeezeActive, &state.squeeze)) {
        return false;
    }
    XrActionStateGetInfo stickInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    stickInfo.action = thumbstickAction_;
    stickInfo.subactionPath = handPaths_[hand];
    XrActionStateVector2f stickState{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (!CheckXr(
            instance_,
            xrGetActionStateVector2f(session_, &stickInfo, &stickState),
            "xrGetActionStateVector2f")) {
        return false;
    }
    state.thumbstickActive = stickState.isActive == XR_TRUE;
    state.thumbstick = state.thumbstickActive
        ? math::Vec2{
              stickState.currentState.x,
              stickState.currentState.y}
        : math::Vec2{};
    state.stateChanged |= stickState.changedSinceLastSync == XR_TRUE;
    if (!getBoolean(
            primaryAction_, &state.primaryActive, &state.primary) ||
        !getBoolean(
            secondaryAction_, &state.secondaryActive, &state.secondary) ||
        !getBoolean(
            thumbstickClickAction_,
            &state.thumbstickClickActive,
            &state.thumbstickClick)) {
        return false;
    }
    state.stateChanged |=
        previousAimActive != state.aim.active ||
        previousGripActive != state.grip.active;
    return true;
}

void XrControllerActions::LogInteractionProfileChanges() {
    for (std::size_t hand = 0; hand < 2; ++hand) {
        XrInteractionProfileState profile{
            XR_TYPE_INTERACTION_PROFILE_STATE};
        if (!CheckXr(
                instance_,
                xrGetCurrentInteractionProfile(
                    session_, handPaths_[hand], &profile),
                "xrGetCurrentInteractionProfile")) {
            continue;
        }
        if (profile.interactionProfile == currentProfiles_[hand]) {
            continue;
        }
        currentProfiles_[hand] = profile.interactionProfile;
        LogInfo(
            "%s interaction profile: %s",
            hand == 0 ? "Left" : "Right",
            PathString(instance_, profile.interactionProfile).c_str());
    }
}

bool XrControllerActions::ApplyHaptic(
    XrHand hand,
    float amplitude,
    XrDuration duration) {
    const std::size_t index = static_cast<std::size_t>(hand);
    XrHapticActionInfo actionInfo{XR_TYPE_HAPTIC_ACTION_INFO};
    actionInfo.action = vibrationAction_;
    actionInfo.subactionPath = handPaths_[index];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::clamp(amplitude, 0.0F, 1.0F);
    vibration.duration = std::max<XrDuration>(duration, 0);
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    return CheckXr(
        instance_,
        xrApplyHapticFeedback(
            session_,
            &actionInfo,
            reinterpret_cast<const XrHapticBaseHeader*>(&vibration)),
        "xrApplyHapticFeedback");
}

void XrControllerActions::Shutdown() {
    for (XrSpace& space : aimSpaces_) {
        if (space != XR_NULL_HANDLE) {
            CheckXr(instance_, xrDestroySpace(space), "xrDestroySpace(aim)");
            space = XR_NULL_HANDLE;
        }
    }
    for (XrSpace& space : gripSpaces_) {
        if (space != XR_NULL_HANDLE) {
            CheckXr(instance_, xrDestroySpace(space), "xrDestroySpace(grip)");
            space = XR_NULL_HANDLE;
        }
    }
    if (actionSet_ != XR_NULL_HANDLE) {
        CheckXr(
            instance_,
            xrDestroyActionSet(actionSet_),
            "xrDestroyActionSet");
        actionSet_ = XR_NULL_HANDLE;
        LogInfo("Controller action set destroyed");
    }
    gripPoseAction_ = XR_NULL_HANDLE;
    aimPoseAction_ = XR_NULL_HANDLE;
    triggerAction_ = XR_NULL_HANDLE;
    squeezeAction_ = XR_NULL_HANDLE;
    thumbstickAction_ = XR_NULL_HANDLE;
    primaryAction_ = XR_NULL_HANDLE;
    secondaryAction_ = XR_NULL_HANDLE;
    thumbstickClickAction_ = XR_NULL_HANDLE;
    vibrationAction_ = XR_NULL_HANDLE;
    handPaths_ = {};
    currentProfiles_ = {};
    states_ = {};
    session_ = XR_NULL_HANDLE;
    instance_ = XR_NULL_HANDLE;
}

}  // namespace questlab
