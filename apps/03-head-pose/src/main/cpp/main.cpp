#include <android_native_app_glue.h>

#include "vulkan_renderer/vulkan_stereo_renderer.h"
#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"
#include "xr_core/xr_session.h"
#include "xr_math/openxr_conversions.h"

#include <vector>

namespace {

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

bool HasLocationFlags(
    XrSpaceLocationFlags flags,
    XrSpaceLocationFlags required) {
    return (flags & required) == required;
}

class HeadPoseScene final : public questlab::VulkanSceneProvider {
public:
    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override {
        if (draws == nullptr) {
            return false;
        }

        const questlab::math::Mat4 worldAxes =
            questlab::math::Multiply(
                questlab::math::TranslationMatrix(
                    {0.0F, -0.4F, -2.0F}),
                ScaleMatrix(0.35F, 0.35F, 0.35F));
        draws->push_back({
            questlab::DebugLineShape::Axes,
            worldAxes,
        });

        constexpr XrSpaceLocationFlags kValidPose =
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        const bool headPoseValid = HasLocationFlags(
            frame.headInLocal.locationFlags, kValidPose);
        if (headPoseValid) {
            const questlab::math::Pose localFromView =
                questlab::math::FromXr(frame.headInLocal.pose);
            const questlab::math::Pose viewFromIndicator{
                questlab::math::IdentityQuat(),
                {0.0F, 0.0F, -0.6F},
            };
            const questlab::math::Pose localFromIndicator =
                questlab::math::Compose(
                    localFromView,
                    viewFromIndicator);
            draws->push_back({
                questlab::DebugLineShape::Axes,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(localFromIndicator),
                    ScaleMatrix(0.14F, 0.14F, 0.14F)),
            });
        }

        const bool stagePoseValid =
            frame.stageAvailable &&
            HasLocationFlags(
                frame.stageInLocal.locationFlags,
                kValidPose);
        if (stagePoseValid && frame.stageBoundsAvailable) {
            draws->push_back({
                questlab::DebugLineShape::Rectangle,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(
                        questlab::math::FromXr(
                            frame.stageInLocal.pose)),
                    ScaleMatrix(
                        frame.stageBounds.width,
                        1.0F,
                        frame.stageBounds.height)),
            });
        }

        LogPosePeriodically(frame, headPoseValid, stagePoseValid);
        return true;
    }

private:
    void LogPosePeriodically(
        const questlab::XrFrameRenderInfo& frame,
        bool headPoseValid,
        bool stagePoseValid) {
        constexpr XrDuration kLogInterval = 1'000'000'000;
        if (lastLogTime_ != 0 &&
            frame.predictedDisplayTime - lastLogTime_ < kLogInterval) {
            return;
        }
        lastLogTime_ = frame.predictedDisplayTime;

        const XrPosef& pose = frame.headInLocal.pose;
        const XrSpaceLocationFlags flags =
            frame.headInLocal.locationFlags;
        const bool positionTracked = HasLocationFlags(
            flags, XR_SPACE_LOCATION_POSITION_TRACKED_BIT);
        const bool orientationTracked = HasLocationFlags(
            flags, XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT);
        questlab::LogInfo(
            "Head VIEW in LOCAL: valid=%s tracked(pos=%s ori=%s) "
            "p=(%.3f %.3f %.3f) q=(%.3f %.3f %.3f %.3f)",
            headPoseValid ? "yes" : "no",
            positionTracked ? "yes" : "no",
            orientationTracked ? "yes" : "no",
            pose.position.x,
            pose.position.y,
            pose.position.z,
            pose.orientation.x,
            pose.orientation.y,
            pose.orientation.z,
            pose.orientation.w);
        questlab::LogInfo(
            "STAGE: available=%s poseValid=%s bounds=%s",
            frame.stageAvailable ? "yes" : "no",
            stagePoseValid ? "yes" : "no",
            frame.stageBoundsAvailable ? "yes" : "no");
    }

    XrTime lastLogTime_ = 0;
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
    questlab::SetLogTag("HeadPose");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    questlab::LogInfo("Head pose and coordinate-space demo starting");
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    HeadPoseScene scene;
    questlab::VulkanStereoRenderer renderer;

    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif

    if (!xrInstance.Initialize(
            app->activity->vm,
            app->activity->clazz,
            {"Head Pose and Coordinates", 1}) ||
        !vulkanBinding.Initialize(xrInstance, bindingOptions) ||
        !xrSession.Initialize(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            vulkanBinding.GraphicsBinding()) ||
        !renderer.Initialize(
            xrInstance.Instance(),
            xrSession,
            vulkanBinding.DeviceContext(),
            &scene)) {
        questlab::LogError("Head pose initialization failed");
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
                !xrSession.PumpFrame(&renderer)) {
                break;
            }
        }
    }

    if (!androidState.destroyRequested && !app->destroyRequested) {
        ANativeActivity_finish(app->activity);
    }
    renderer.Shutdown();
    xrSession.Shutdown();
    vulkanBinding.Shutdown();
    xrInstance.Shutdown();
    questlab::LogInfo("Head pose demo stopped cleanly");
}
