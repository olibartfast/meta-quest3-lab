#include "camera_source/camera_source.h"

#include "camera_source/replay_camera_adapter.h"
#if defined(__ANDROID__)
#include "camera_source/meta_camera2_adapter.h"
#endif

namespace questlab::camera {

std::unique_ptr<IRgbCameraSource> CreateCameraSource(
    const CameraSourceConfig& config,
    const CameraPlatformContext& platform) {
    if (config.kind == CameraSourceKind::Replay) {
        return std::make_unique<ReplayCameraAdapter>(
            config.replayManifestPath);
    }
#if defined(__ANDROID__)
    if (config.kind == CameraSourceKind::MetaCamera2) {
        return std::make_unique<MetaCamera2Adapter>(
            static_cast<JavaVM*>(platform.javaVm),
            static_cast<jobject>(platform.activity));
    }
#else
    (void)platform;
#endif
    return nullptr;
}

}  // namespace questlab::camera
