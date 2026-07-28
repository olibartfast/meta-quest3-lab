#include "xr_hand_tracking/xr_hand_tracker.h"

#include "xr_core/xr_error.h"
#include "xr_math/openxr_conversions.h"

#include <array>

namespace questlab {

XrHandTracker::~XrHandTracker() {
    Shutdown();
}

bool XrHandTracker::Initialize(
    XrInstance instance,
    XrSystemId systemId,
    XrSession session) {
    if (trackers_[0] != XR_NULL_HANDLE &&
        trackers_[1] != XR_NULL_HANDLE) {
        return true;
    }

    instance_ = instance;
    session_ = session;
    if (!LoadFunctions() ||
        !QuerySupport(systemId) ||
        !CreateTracker(0, XR_HAND_LEFT_EXT) ||
        !CreateTracker(1, XR_HAND_RIGHT_EXT)) {
        Shutdown();
        return false;
    }
    LogInfo("Left and right OpenXR hand trackers created");
    return true;
}

bool XrHandTracker::LoadFunctions() {
    const auto load = [this](
                          const char* name,
                          PFN_xrVoidFunction* function) {
        return CheckXr(
            instance_,
            xrGetInstanceProcAddr(instance_, name, function),
            name);
    };
    return
        load(
            "xrCreateHandTrackerEXT",
            reinterpret_cast<PFN_xrVoidFunction*>(&createHandTracker_)) &&
        load(
            "xrDestroyHandTrackerEXT",
            reinterpret_cast<PFN_xrVoidFunction*>(&destroyHandTracker_)) &&
        load(
            "xrLocateHandJointsEXT",
            reinterpret_cast<PFN_xrVoidFunction*>(&locateHandJoints_));
}

bool XrHandTracker::QuerySupport(XrSystemId systemId) {
    XrSystemHandTrackingPropertiesEXT handProperties{
        XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    systemProperties.next = &handProperties;
    if (!CheckXr(
            instance_,
            xrGetSystemProperties(
                instance_, systemId, &systemProperties),
            "xrGetSystemProperties(hand tracking)")) {
        return false;
    }
    if (handProperties.supportsHandTracking != XR_TRUE) {
        LogError("OpenXR system does not support hand tracking");
        return false;
    }
    LogInfo("OpenXR hand tracking is supported");
    return true;
}

bool XrHandTracker::CreateTracker(std::size_t index, XrHandEXT hand) {
    XrHandTrackerCreateInfoEXT createInfo{
        XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
    createInfo.hand = hand;
    createInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
    return CheckXr(
        instance_,
        createHandTracker_(
            session_, &createInfo, &trackers_[index]),
        hand == XR_HAND_LEFT_EXT
            ? "xrCreateHandTrackerEXT(left)"
            : "xrCreateHandTrackerEXT(right)");
}

bool XrHandTracker::UpdateFrame(const XrFrameUpdateInfo& frame) {
    return UpdateHand(0, frame) && UpdateHand(1, frame);
}

bool XrHandTracker::UpdateHand(
    std::size_t index,
    const XrFrameUpdateInfo& frame) {
    std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT>
        jointLocations{};
    XrHandJointLocationsEXT locations{
        XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
    locations.jointCount = static_cast<uint32_t>(jointLocations.size());
    locations.jointLocations = jointLocations.data();

    XrHandJointsLocateInfoEXT locateInfo{
        XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
    locateInfo.baseSpace = frame.baseSpace;
    locateInfo.time = frame.predictedDisplayTime;
    if (!CheckXr(
            instance_,
            locateHandJoints_(
                trackers_[index], &locateInfo, &locations),
            index == 0
                ? "xrLocateHandJointsEXT(left)"
                : "xrLocateHandJointsEXT(right)")) {
        return false;
    }

    const bool wasActive = states_[index].active;
    HandState state;
    state.active = locations.isActive == XR_TRUE;
    if (state.active) {
        for (std::size_t joint = 0; joint < state.joints.size(); ++joint) {
            const XrHandJointLocationEXT& source = jointLocations[joint];
            HandJointState& destination = state.joints[joint];
            destination.pose = math::FromXr(source.pose);
            destination.radius = source.radius;
            destination.positionValid =
                (source.locationFlags &
                 XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
            destination.orientationValid =
                (source.locationFlags &
                 XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
            destination.positionTracked =
                (source.locationFlags &
                 XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
            destination.orientationTracked =
                (source.locationFlags &
                 XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
        }
    }
    states_[index] = state;

    if (wasActive != state.active) {
        if (state.active) {
            const HandJointState& wrist =
                state.joints[XR_HAND_JOINT_WRIST_EXT];
            LogInfo(
                "%s hand tracking active; wrist=(%.3f %.3f %.3f), "
                "position=%s/%s orientation=%s/%s",
                index == 0 ? "Left" : "Right",
                wrist.pose.position.x,
                wrist.pose.position.y,
                wrist.pose.position.z,
                wrist.positionValid ? "valid" : "invalid",
                wrist.positionTracked ? "tracked" : "inferred",
                wrist.orientationValid ? "valid" : "invalid",
                wrist.orientationTracked ? "tracked" : "inferred");
        } else {
            LogInfo(
                "%s hand tracking inactive",
                index == 0 ? "Left" : "Right");
        }
    }
    return true;
}

void XrHandTracker::Shutdown() {
    if (destroyHandTracker_ != nullptr) {
        for (std::size_t index = trackers_.size(); index-- > 0;) {
            if (trackers_[index] != XR_NULL_HANDLE) {
                CheckXr(
                    instance_,
                    destroyHandTracker_(trackers_[index]),
                    "xrDestroyHandTrackerEXT");
                trackers_[index] = XR_NULL_HANDLE;
            }
        }
    }
    states_ = {};
    createHandTracker_ = nullptr;
    destroyHandTracker_ = nullptr;
    locateHandJoints_ = nullptr;
    session_ = XR_NULL_HANDLE;
    instance_ = XR_NULL_HANDLE;
}

}  // namespace questlab
