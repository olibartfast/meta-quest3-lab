#include "camera_source/replay_camera_adapter.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace questlab::camera {
namespace {

bool ReadBinaryFile(
    const std::filesystem::path& path,
    std::vector<uint8_t>* bytes) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || bytes == nullptr) {
        return false;
    }
    const std::streamsize length = stream.tellg();
    if (length < 0) {
        return false;
    }
    bytes->resize(static_cast<size_t>(length));
    stream.seekg(0);
    return stream.read(
        reinterpret_cast<char*>(bytes->data()), length).good();
}

}  // namespace

ReplayCameraAdapter::ReplayCameraAdapter(std::string manifestPath)
    : manifestPath_(std::move(manifestPath)) {}

CameraCapabilities ReplayCameraAdapter::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

bool ReplayCameraAdapter::LoadFixture() {
    std::ifstream manifest(manifestPath_);
    if (!manifest) {
        stats_.lastError = "Cannot open replay manifest: " + manifestPath_;
        return false;
    }
    std::string magic;
    std::string yFile;
    std::string uFile;
    std::string vFile;
    manifest >> magic >> fixture_.width >> fixture_.height
             >> fixture_.planes[0].rowStride
             >> fixture_.planes[0].pixelStride
             >> fixture_.planes[1].rowStride
             >> fixture_.planes[1].pixelStride
             >> fixture_.planes[2].rowStride
             >> fixture_.planes[2].pixelStride
             >> yFile >> uFile >> vFile;
    if (!manifest || magic != "QUEST_CAMERA_FIXTURE_V1" ||
        fixture_.width <= 0 || fixture_.height <= 0) {
        stats_.lastError = "Replay manifest is invalid or unsupported";
        return false;
    }
    std::string metadataKey;
    while (manifest >> metadataKey) {
        if (metadataKey == "sensor_timestamp_ns") {
            manifest >> fixture_.sensorTimestampNanoseconds;
        } else if (metadataKey == "intrinsics") {
            manifest >> fixture_.intrinsics.fx
                     >> fixture_.intrinsics.fy
                     >> fixture_.intrinsics.cx
                     >> fixture_.intrinsics.cy
                     >> fixture_.intrinsics.skew;
            fixture_.intrinsics.valid = manifest.good();
        } else if (metadataKey == "distortion") {
            for (float& value : fixture_.intrinsics.distortion) {
                manifest >> value;
            }
        } else if (metadataKey == "camera_from_head") {
            for (float& value : fixture_.cameraFromHead.orientation) {
                manifest >> value;
            }
            for (float& value : fixture_.cameraFromHead.position) {
                manifest >> value;
            }
            fixture_.cameraFromHead.valid = manifest.good();
        } else {
            std::string ignoredLine;
            std::getline(manifest, ignoredLine);
        }
    }
    const std::filesystem::path directory =
        std::filesystem::path(manifestPath_).parent_path();
    if (!ReadBinaryFile(directory / yFile, &fixture_.planes[0].bytes) ||
        !ReadBinaryFile(directory / uFile, &fixture_.planes[1].bytes) ||
        !ReadBinaryFile(directory / vFile, &fixture_.planes[2].bytes)) {
        stats_.lastError = "Replay plane file is missing or unreadable";
        return false;
    }
    fixture_.format = PixelFormat::Yuv420888;
    capabilities_.sourceName = "recorded-replay";
    capabilities_.cameraId = "fixture";
    capabilities_.streams = {{
        fixture_.width,
        fixture_.height,
        30,
    }};
    capabilities_.intrinsics = fixture_.intrinsics;
    capabilities_.cameraFromHead = fixture_.cameraFromHead;
    return true;
}

bool ReplayCameraAdapter::Start(const CameraStreamConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.health == CameraHealth::Running) {
        return true;
    }
    stats_ = {};
    stats_.health = CameraHealth::Starting;
    if (!LoadFixture()) {
        stats_.health = CameraHealth::Error;
        return false;
    }
    streamConfig_ = config;
    if (streamConfig_.framesPerSecond <= 0) {
        streamConfig_.framesPerSecond = 30;
    }
    nextFrameTime_ = std::chrono::steady_clock::now();
    stats_.health = CameraHealth::Running;
    return true;
}

bool ReplayCameraAdapter::TryConsumeLatest(RgbCapture* capture) {
    if (capture == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.health != CameraHealth::Running ||
        std::chrono::steady_clock::now() < nextFrameTime_) {
        return false;
    }
    *capture = fixture_;
    capture->frameId = nextFrameId_++;
    capture->arrivalTimestampNanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    capture->sensorTimestampNanoseconds =
        capture->arrivalTimestampNanoseconds;
    ++stats_.receivedFrames;
    ++stats_.consumedFrames;
    nextFrameTime_ += std::chrono::nanoseconds(
        1'000'000'000LL / streamConfig_.framesPerSecond);
    return true;
}

CameraSourceStats ReplayCameraAdapter::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void ReplayCameraAdapter::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.health = CameraHealth::Stopped;
}

}  // namespace questlab::camera
