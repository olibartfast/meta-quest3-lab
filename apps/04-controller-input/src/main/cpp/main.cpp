#include <android_native_app_glue.h>

#include "vulkan_renderer/vulkan_stereo_renderer.h"
#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_controller_actions.h"
#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"
#include "xr_core/xr_session.h"
#include "xr_interaction/xr_interaction.h"
#include "xr_math/openxr_conversions.h"

#include <array>
#include <optional>
#include <vector>

namespace {

constexpr float kRayLength = 3.0F;
constexpr float kTargetSize = 0.30F;

struct AndroidState {
    bool resumed = false;
    bool destroyRequested = false;
};

questlab::math::Mat4 ScaleMatrix(float x, float y, float z) {
    questlab::math::Mat4 matrix = questlab::math::IdentityMatrix();
    matrix.values[0] = x;
    matrix.values[5] = y;
    matrix.values[10] = z;
    return matrix;
}

bool HasValidPose(XrSpaceLocationFlags flags) {
    constexpr XrSpaceLocationFlags kValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (flags & kValid) == kValid;
}

class ControllerScene final :
    public questlab::XrFrameUpdater,
    public questlab::VulkanSceneProvider {
public:
    bool Initialize(XrInstance instance, XrSession session) {
        return actions_.Initialize(instance, session);
    }

    bool UpdateFrame(
        const questlab::XrFrameUpdateInfo& frame) override {
        if (!actions_.UpdateFrame(frame)) {
            return false;
        }

        questlab::interaction::SelectionFrameInput input;
        input.target = {
            targetCenter_,
            {kTargetSize * 0.5F,
             kTargetSize * 0.5F,
             kTargetSize * 0.5F},
        };
        input.maxRayDistance = kRayLength;
        for (std::size_t hand = 0; hand < 2; ++hand) {
            const questlab::XrHand xrHand =
                hand == 0
                    ? questlab::XrHand::Left
                    : questlab::XrHand::Right;
            const questlab::XrControllerState& state =
                actions_.State(xrHand);
            if (targetInitialized_ && state.aim.active && state.aim.valid) {
                questlab::math::Vec3 direction =
                    questlab::math::Rotate(
                        state.aim.pose.orientation,
                        {0.0F, 0.0F, -1.0F});
                if (questlab::math::Normalize(&direction)) {
                    input.rays[hand] =
                        questlab::interaction::Ray{
                            state.aim.pose.position,
                            direction,
                        };
                }
            }
            input.triggerValues[hand] =
                state.triggerActive ? state.trigger : 0.0F;
            input.primaryButtons[hand] =
                state.primaryActive && state.primary;
            if (state.stateChanged) {
                LogState(hand, state);
            }
        }

        selectionResult_ = selection_.Update(input);
        if (selectionResult_.selectionTriggered) {
            const questlab::XrHand hand =
                selectionResult_.selectingHand ==
                        questlab::interaction::Hand::Left
                    ? questlab::XrHand::Left
                    : questlab::XrHand::Right;
            questlab::LogInfo(
                "%s controller selected target",
                hand == questlab::XrHand::Left ? "Left" : "Right");
            if (!actions_.ApplyHaptic(
                    hand, 0.5F, 50'000'000)) {
                return false;
            }
        } else if (selectionResult_.cleared) {
            questlab::LogInfo("Target selection cleared");
        }
        return true;
    }

    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override {
        if (draws == nullptr) {
            return false;
        }
        if (!targetInitialized_ &&
            HasValidPose(frame.headInLocal.locationFlags)) {
            const questlab::math::Pose localFromHead =
                questlab::math::FromXr(frame.headInLocal.pose);
            questlab::math::Vec3 horizontalForward =
                questlab::math::TransformDirection(
                    localFromHead, {0.0F, 0.0F, -1.0F});
            horizontalForward.y = 0.0F;
            if (!questlab::math::Normalize(&horizontalForward)) {
                horizontalForward = {0.0F, 0.0F, -1.0F};
            }
            targetCenter_ = questlab::math::Add(
                localFromHead.position,
                questlab::math::Scale(horizontalForward, 2.0F));
            targetInitialized_ = true;
            questlab::LogInfo(
                "Target fixed in LOCAL at (%.3f %.3f %.3f)",
                targetCenter_.x,
                targetCenter_.y,
                targetCenter_.z);
        }

        for (std::size_t hand = 0; hand < 2; ++hand) {
            const questlab::XrControllerState& state =
                actions_.State(
                    hand == 0
                        ? questlab::XrHand::Left
                        : questlab::XrHand::Right);
            if (state.grip.active && state.grip.valid) {
                draws->push_back({
                    questlab::DebugLineShape::Axes,
                    questlab::math::Multiply(
                        questlab::math::PoseMatrix(state.grip.pose),
                        ScaleMatrix(0.08F, 0.08F, 0.08F)),
                });
            }
            if (state.aim.active && state.aim.valid) {
                draws->push_back({
                    questlab::DebugLineShape::Ray,
                    questlab::math::Multiply(
                        questlab::math::PoseMatrix(state.aim.pose),
                        ScaleMatrix(1.0F, 1.0F, kRayLength)),
                    selectionResult_.hovered[hand]
                        ? std::array<float, 4>{1.0F, 0.85F, 0.0F, 1.0F}
                        : std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F},
                });
            }
        }
        if (targetInitialized_) {
            const bool hovered =
                selectionResult_.hovered[0] ||
                selectionResult_.hovered[1];
            const std::array<float, 4> color =
                selectionResult_.selected
                    ? std::array<float, 4>{0.1F, 1.0F, 0.2F, 1.0F}
                    : hovered
                        ? std::array<float, 4>{1.0F, 0.85F, 0.0F, 1.0F}
                        : std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F};
            draws->push_back({
                questlab::DebugLineShape::Box,
                questlab::math::Multiply(
                    questlab::math::TranslationMatrix(targetCenter_),
                    ScaleMatrix(kTargetSize, kTargetSize, kTargetSize)),
                color,
            });
        }
        return true;
    }

    void Shutdown() {
        actions_.Shutdown();
        selection_.Reset();
    }

private:
    static void LogState(
        std::size_t hand,
        const questlab::XrControllerState& state) {
        questlab::LogInfo(
            "%s input: aim=%s grip=%s trigger=%.2f squeeze=%.2f "
            "stick=(%.2f %.2f) primary=%s secondary=%s click=%s",
            hand == 0 ? "Left" : "Right",
            state.aim.active && state.aim.valid ? "valid" : "inactive",
            state.grip.active && state.grip.valid ? "valid" : "inactive",
            state.trigger,
            state.squeeze,
            state.thumbstick.x,
            state.thumbstick.y,
            state.primary ? "down" : "up",
            state.secondary ? "down" : "up",
            state.thumbstickClick ? "down" : "up");
    }

    questlab::XrControllerActions actions_;
    questlab::interaction::SelectionState selection_;
    questlab::interaction::SelectionFrameResult selectionResult_;
    questlab::math::Vec3 targetCenter_{};
    bool targetInitialized_ = false;
};

void HandleAppCommand(android_app* app, int32_t command) {
    auto* state = static_cast<AndroidState*>(app->userData);
    switch (command) {
        case APP_CMD_RESUME:
            state->resumed = true;
            questlab::LogInfo("Android lifecycle: RESUME");
            break;
        case APP_CMD_PAUSE:
            state->resumed = false;
            questlab::LogInfo("Android lifecycle: PAUSE");
            break;
        case APP_CMD_DESTROY:
            state->destroyRequested = true;
            questlab::LogInfo("Android lifecycle: DESTROY");
            break;
        default:
            break;
    }
}

}  // namespace

void android_main(android_app* app) {
    questlab::SetLogTag("ControllerInput");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    questlab::LogInfo("Controller input and interaction demo starting");
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    ControllerScene scene;
    questlab::VulkanStereoRenderer renderer;

    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif

    const questlab::XrInstanceOptions instanceOptions{
        "Controller Input",
        1,
    };
    if (!xrInstance.Initialize(
            app->activity->vm,
            app->activity->clazz,
            instanceOptions) ||
        !vulkanBinding.Initialize(xrInstance, bindingOptions) ||
        !xrSession.Initialize(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            vulkanBinding.GraphicsBinding()) ||
        !scene.Initialize(
            xrInstance.Instance(),
            xrSession.Session()) ||
        !renderer.Initialize(
            xrInstance.Instance(),
            xrSession,
            vulkanBinding.DeviceContext(),
            &scene)) {
        questlab::LogError("Controller input initialization failed");
        ANativeActivity_finish(app->activity);
    } else {
        while (!androidState.destroyRequested && !app->destroyRequested &&
               !xrSession.ShouldExit()) {
            const int timeoutMilliseconds =
                xrSession.IsRunning() ? 0 : (androidState.resumed ? 10 : -1);
            int events = 0;
            android_poll_source* source = nullptr;
            int pollResult = ALooper_pollOnce(
                timeoutMilliseconds,
                nullptr,
                &events,
                reinterpret_cast<void**>(&source));
            while (pollResult >= 0) {
                if (source != nullptr) {
                    source->process(app, source);
                }
                if (androidState.destroyRequested || app->destroyRequested) {
                    break;
                }
                source = nullptr;
                pollResult = ALooper_pollOnce(
                    0,
                    nullptr,
                    &events,
                    reinterpret_cast<void**>(&source));
            }

            if (androidState.destroyRequested || app->destroyRequested) {
                xrSession.RequestExit();
                break;
            }
            if (!xrSession.PollEvents() ||
                !xrSession.PumpFrame(&renderer, &scene)) {
                break;
            }
        }
    }

    if (!androidState.destroyRequested && !app->destroyRequested) {
        ANativeActivity_finish(app->activity);
    }
    renderer.Shutdown();
    scene.Shutdown();
    xrSession.Shutdown();
    vulkanBinding.Shutdown();
    xrInstance.Shutdown();
    questlab::LogInfo("Controller input demo stopped cleanly");
}
