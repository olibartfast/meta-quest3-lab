#pragma once

#include "xr_core/xr_session.h"
#include "xr_math/xr_math.h"

#include <openxr/openxr.h>

#include <array>
#include <cstddef>

namespace questlab {

enum class HandSide : std::size_t {
    Left = 0,
    Right = 1,
};

struct HandJointState {
    math::Pose pose{};
    float radius = 0.0F;
    bool positionValid = false;
    bool orientationValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;
};

struct HandState {
    bool active = false;
    std::array<HandJointState, XR_HAND_JOINT_COUNT_EXT> joints{};
};

class XrHandTracker final : public XrFrameUpdater {
public:
    XrHandTracker() = default;
    ~XrHandTracker() override;

    XrHandTracker(const XrHandTracker&) = delete;
    XrHandTracker& operator=(const XrHandTracker&) = delete;

    bool Initialize(
        XrInstance instance,
        XrSystemId systemId,
        XrSession session);
    bool UpdateFrame(const XrFrameUpdateInfo& frame) override;
    void Shutdown();

    const HandState& State(HandSide side) const {
        return states_[static_cast<std::size_t>(side)];
    }

private:
    bool LoadFunctions();
    bool QuerySupport(XrSystemId systemId);
    bool CreateTracker(std::size_t index, XrHandEXT hand);
    bool UpdateHand(std::size_t index, const XrFrameUpdateInfo& frame);

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    PFN_xrCreateHandTrackerEXT createHandTracker_ = nullptr;
    PFN_xrDestroyHandTrackerEXT destroyHandTracker_ = nullptr;
    PFN_xrLocateHandJointsEXT locateHandJoints_ = nullptr;
    std::array<XrHandTrackerEXT, 2> trackers_{};
    std::array<HandState, 2> states_{};
};

}  // namespace questlab
