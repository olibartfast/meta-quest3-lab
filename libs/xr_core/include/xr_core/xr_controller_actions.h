#pragma once

#include "xr_core/xr_session.h"
#include "xr_math/xr_math.h"

#include <openxr/openxr.h>

#include <array>
#include <cstddef>

namespace questlab {

enum class XrHand : std::size_t {
    Left = 0,
    Right = 1,
};

struct XrControllerPoseState {
    bool active = false;
    bool valid = false;
    bool positionTracked = false;
    bool orientationTracked = false;
    math::Pose pose{};
};

struct XrControllerState {
    XrControllerPoseState aim{};
    XrControllerPoseState grip{};
    bool triggerActive = false;
    float trigger = 0.0F;
    bool squeezeActive = false;
    float squeeze = 0.0F;
    bool thumbstickActive = false;
    math::Vec2 thumbstick{};
    bool primaryActive = false;
    bool primary = false;
    bool secondaryActive = false;
    bool secondary = false;
    bool thumbstickClickActive = false;
    bool thumbstickClick = false;
    bool stateChanged = false;
};

class XrControllerActions final : public XrFrameUpdater {
public:
    XrControllerActions() = default;
    ~XrControllerActions() override;

    XrControllerActions(const XrControllerActions&) = delete;
    XrControllerActions& operator=(const XrControllerActions&) = delete;

    bool Initialize(XrInstance instance, XrSession session);
    bool UpdateFrame(const XrFrameUpdateInfo& frame) override;
    bool ApplyHaptic(
        XrHand hand,
        float amplitude,
        XrDuration duration);
    void Shutdown();

    const XrControllerState& State(XrHand hand) const {
        return states_[static_cast<std::size_t>(hand)];
    }

private:
    bool CreateActions();
    bool SuggestBindings();
    bool CreateActionSpaces();
    bool AttachActionSet();
    bool UpdateHand(std::size_t hand, const XrFrameUpdateInfo& frame);
    void LogInteractionProfileChanges();

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction gripPoseAction_ = XR_NULL_HANDLE;
    XrAction aimPoseAction_ = XR_NULL_HANDLE;
    XrAction triggerAction_ = XR_NULL_HANDLE;
    XrAction squeezeAction_ = XR_NULL_HANDLE;
    XrAction thumbstickAction_ = XR_NULL_HANDLE;
    XrAction primaryAction_ = XR_NULL_HANDLE;
    XrAction secondaryAction_ = XR_NULL_HANDLE;
    XrAction thumbstickClickAction_ = XR_NULL_HANDLE;
    XrAction vibrationAction_ = XR_NULL_HANDLE;
    std::array<XrPath, 2> handPaths_{};
    std::array<XrSpace, 2> gripSpaces_{};
    std::array<XrSpace, 2> aimSpaces_{};
    std::array<XrPath, 2> currentProfiles_{};
    std::array<XrControllerState, 2> states_{};
};

}  // namespace questlab
