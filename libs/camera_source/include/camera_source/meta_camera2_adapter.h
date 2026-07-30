#pragma once

#include "camera_source/camera_source.h"
#include "camera_source/latest_frame_queue.h"

#if defined(__ANDROID__)
#include <jni.h>
#endif

#include <mutex>

namespace questlab::camera {

class MetaCamera2Adapter final : public IRgbCameraSource {
public:
#if defined(__ANDROID__)
    MetaCamera2Adapter(JavaVM* javaVm, jobject activity);
#else
    MetaCamera2Adapter(void*, void*) {}
#endif
    ~MetaCamera2Adapter() override;

    CameraCapabilities GetCapabilities() const override;
    bool Start(const CameraStreamConfig& config) override;
    bool TryConsumeLatest(RgbCapture* capture) override;
    CameraSourceStats GetStats() const override;
    void Stop() override;

    void SetPermissionState(bool granted);
    void OnConfigured(const CameraCapabilities& capabilities);
    void OnFrame(RgbCapture capture);
    void OnError(const std::string& message);

private:
#if defined(__ANDROID__)
    bool CallActivityMethod(
        const char* name,
        const char* signature,
        const CameraStreamConfig* config);

    JavaVM* javaVm_ = nullptr;
    jobject activity_ = nullptr;
#endif
    mutable std::mutex mutex_;
    LatestFrameQueue queue_;
    CameraCapabilities capabilities_;
    CameraSourceStats stats_;
    bool permissionGranted_ = false;
};

}  // namespace questlab::camera
