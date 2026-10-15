#pragma once

#include "camera_source/camera_source.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace questlab::detection {

enum class ObjectDetectorKind {
    OnDeviceOnnxRuntime,
    Streaming,
    Replay,
};

enum class DetectorHealth {
    Stopped,
    Starting,
    Running,
    Disconnected,
    Unsupported,
    Error,
};

struct DetectorConfig {
    ObjectDetectorKind kind = ObjectDetectorKind::OnDeviceOnnxRuntime;
    std::string modelPath;
    std::string manifestPath;
    std::string expectedManifestIdentity;
    std::string serviceHost = "127.0.0.1";
    uint16_t servicePort = 48110;
    float maximumSubmissionsPerSecond = 2.0F;
    int64_t resultExpiryNanoseconds = 1'500'000'000;
    int32_t xnnpackThreads = 4;
    std::string replayDetectionsPath;
    uint64_t replayFrameId = 0;
    // Comma-separated detector label allowlist (e.g. "tv"). Empty means
    // every class is processed. App-level filter: the detector still runs
    // full COCO, but only listed labels reach projection/fusion/rendering.
    std::string allowedClassesCsv;
    // Ask the streaming service for screen-corner quads + PnP poses
    // (QLD2 protocol). Needs a corners-capable service; off by default so
    // old services keep working.
    bool requestCorners = false;
};

struct DetectorCapabilities {
    ObjectDetectorKind kind = ObjectDetectorKind::OnDeviceOnnxRuntime;
    std::string backendName;
    bool asynchronous = true;
    bool newestFrameOnly = true;
    bool cancellable = true;
};

struct Detection {
    int32_t classId = -1;
    std::string className;
    float confidence = 0.0F;
    std::array<float, 4> boxXyxy{};
    // Screen-corner extension (QLD2 services only): quad in source pixels
    // (TL, TR, BR, BL) plus the PnP pose in the capture camera frame
    // (+X right, +Y down, +Z forward) with its reprojection RMS. Absent
    // unless the service fitted a quad.
    bool hasCorners = false;
    std::array<float, 8> cornerXy{};
    bool hasCornerPose = false;
    std::array<float, 4> cornerQuatXyzw{};
    std::array<float, 3> cornerPosition{};
    float cornerReprojectionPixels = -1.0F;
    // Object width,height (metres) the pose was solved with.
    std::array<float, 2> cornerSizeWH{{0.0F, 0.0F}};
};

struct DetectionResult {
    uint64_t frameId = 0;
    int64_t captureTimestampNanoseconds = 0;
    int64_t inferenceStartNanoseconds = 0;
    int64_t inferenceEndNanoseconds = 0;
    int32_t sourceWidth = 0;
    int32_t sourceHeight = 0;
    std::string manifestIdentity;
    std::vector<Detection> detections;
};

struct DetectorStats {
    DetectorHealth health = DetectorHealth::Stopped;
    uint64_t submittedFrames = 0;
    uint64_t completedFrames = 0;
    uint64_t replacedPendingFrames = 0;
    uint64_t rateLimitedFrames = 0;
    uint64_t unknownReplayFrames = 0;
    uint64_t inferenceFailures = 0;
    uint64_t currentQueueDepth = 0;
    uint64_t queueHighWaterMark = 0;
    uint64_t lastCompletedFrameId = 0;
    std::string manifestIdentity;
    std::string backendDetails;
    std::string lastError;
};

class IObjectDetector {
public:
    virtual ~IObjectDetector() = default;

    virtual DetectorCapabilities GetCapabilities() const = 0;
    virtual bool Start(const DetectorConfig& config) = 0;
    // Ownership is transferred so a backend can process off-thread without
    // retaining references to camera queue memory.
    virtual bool Submit(camera::RgbCapture capture) = 0;
    virtual bool TryConsumeLatest(DetectionResult* result) = 0;
    virtual DetectorStats GetStats() const = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IObjectDetector> CreateObjectDetector(
    const DetectorConfig& config);

bool AnnotateRgba(
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* rgba,
    const std::vector<Detection>& detections,
    const std::vector<std::string>& statusLines,
    std::string* error);

}  // namespace questlab::detection
