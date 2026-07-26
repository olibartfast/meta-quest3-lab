#include <android_native_app_glue.h>

#include "vulkan_renderer/vulkan_stereo_renderer.h"
#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"
#include "xr_core/xr_session.h"
#include "xr_math/openxr_conversions.h"
#include "xr_meta_passthrough/meta_passthrough_fb.h"

#include <array>
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

bool HasValidPose(XrSpaceLocationFlags flags) {
    constexpr XrSpaceLocationFlags kValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (flags & kValid) == kValid;
}

class PassthroughScene final : public questlab::VulkanSceneProvider {
public:
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
                "MR target fixed in LOCAL at (%.3f %.3f %.3f)",
                targetCenter_.x,
                targetCenter_.y,
                targetCenter_.z);
        }
        if (!targetInitialized_) {
            return true;
        }

        draws->push_back({
            questlab::DebugLineShape::Box,
            questlab::math::Multiply(
                questlab::math::TranslationMatrix(targetCenter_),
                ScaleMatrix(0.35F, 0.35F, 0.35F)),
            {0.0F, 1.0F, 1.0F, 1.0F},
        });
        draws->push_back({
            questlab::DebugLineShape::Axes,
            questlab::math::Multiply(
                questlab::math::TranslationMatrix({
                    targetCenter_.x,
                    targetCenter_.y - 0.30F,
                    targetCenter_.z,
                }),
                ScaleMatrix(0.20F, 0.20F, 0.20F)),
        });
        return true;
    }

private:
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
    questlab::SetLogTag("PassthroughMR");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    questlab::LogInfo("Passthrough mixed-reality demo starting");
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    questlab::MetaPassthroughFB passthrough;
    PassthroughScene scene;
    questlab::VulkanStereoRenderer renderer;

    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif
    const questlab::XrInstanceOptions instanceOptions{
        "Passthrough Mixed Reality",
        1,
        {XR_FB_PASSTHROUGH_EXTENSION_NAME},
    };
    const questlab::VulkanRendererOptions rendererOptions{
        true,
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
        xrSession.BlendMode() != XR_ENVIRONMENT_BLEND_MODE_OPAQUE ||
        !passthrough.Initialize(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            xrSession.Session()) ||
        !renderer.Initialize(
            xrInstance.Instance(),
            xrSession,
            vulkanBinding.DeviceContext(),
            &scene,
            rendererOptions)) {
        questlab::LogError("Passthrough MR initialization failed");
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
            if (!xrSession.PollEvents(&passthrough) ||
                !passthrough.SetActive(
                    androidState.resumed && xrSession.IsRunning()) ||
                !xrSession.PumpFrame(
                    &renderer, nullptr, &passthrough)) {
                break;
            }
        }
    }

    if (!androidState.destroyRequested && !app->destroyRequested) {
        ANativeActivity_finish(app->activity);
    }
    renderer.Shutdown();
    passthrough.Shutdown();
    xrSession.Shutdown();
    vulkanBinding.Shutdown();
    xrInstance.Shutdown();
    questlab::LogInfo("Passthrough MR demo stopped cleanly");
}
