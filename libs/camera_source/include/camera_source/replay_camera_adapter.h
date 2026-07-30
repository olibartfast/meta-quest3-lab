#pragma once

#include "camera_source/camera_source.h"

#include <chrono>
#include <mutex>

namespace questlab::camera {

class ReplayCameraAdapter final : public IRgbCameraSource {
public:
    explicit ReplayCameraAdapter(std::string manifestPath);

    CameraCapabilities GetCapabilities() const override;
    bool Start(const CameraStreamConfig& config) override;
    bool TryConsumeLatest(RgbCapture* capture) override;
    CameraSourceStats GetStats() const override;
    void Stop() override;

private:
    bool LoadFixture();

    std::string manifestPath_;
    CameraCapabilities capabilities_;
    RgbCapture fixture_;
    CameraSourceStats stats_;
    CameraStreamConfig streamConfig_;
    std::chrono::steady_clock::time_point nextFrameTime_{};
    uint64_t nextFrameId_ = 1;
    mutable std::mutex mutex_;
};

}  // namespace questlab::camera
