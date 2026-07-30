#pragma once

#include "camera_source/camera_source.h"

#include <mutex>
#include <optional>

namespace questlab::camera {

class LatestFrameQueue {
public:
    bool Publish(RgbCapture capture);
    bool TryConsumeLatest(RgbCapture* capture);
    void Clear();

private:
    std::mutex mutex_;
    std::optional<RgbCapture> latest_;
};

}  // namespace questlab::camera
