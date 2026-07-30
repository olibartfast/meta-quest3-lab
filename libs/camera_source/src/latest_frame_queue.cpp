#include "camera_source/latest_frame_queue.h"

#include <utility>

namespace questlab::camera {

bool LatestFrameQueue::Publish(RgbCapture capture) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool overwritten = latest_.has_value();
    latest_ = std::move(capture);
    return overwritten;
}

bool LatestFrameQueue::TryConsumeLatest(RgbCapture* capture) {
    if (capture == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!latest_.has_value()) {
        return false;
    }
    *capture = std::move(*latest_);
    latest_.reset();
    return true;
}

void LatestFrameQueue::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.reset();
}

}  // namespace questlab::camera
