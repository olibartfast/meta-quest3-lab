#pragma once

#include "camera_source/camera_source.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace questlab::detection {

enum class ObjectDetectorKind {
    OnDeviceOnnxRuntime,
    Streaming,
    Triton,
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
    // Contract identity: on-device it must match the loaded manifest's
    // ONNX SHA-256; streaming sends it in the handshake. detector.conf
    // "model_identity" (deploy script, from --manifest) overrides the
    // app's compiled-in default so the target contract can switch
    // (nano/medium/seg-small) without rebuilding the APK.
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
    // old services keep working. detector.conf "request_corners" (written
    // by the deploy script for seg manifests) turns it on.
    bool requestCorners = false;
    // Direct Triton (KServe v2 HTTP) backend: the app preprocesses and
    // postprocesses locally and only the model tensors run on the server.
    // No bridge process: detector.conf triton_host/triton_port/triton_model.
    std::string tritonHost = "127.0.0.1";
    uint16_t tritonPort = 8000;
    std::string tritonModel;
    std::string tritonVersion = "1";
    // Per-class planar sizes (label -> {width, height} metres) for
    // SolveQuadPose on seg-mask quads. Class-agnostic by construction:
    // every masked detection gets a quad, but a pose is solved only for
    // listed classes - an unlisted class has no known geometry, and a pose
    // from a guessed size would be a wrong measurement, not a missing one.
    std::unordered_map<std::string, std::array<float, 2>> classSizes = {
        {"tv", {0.55F, 0.32F}},
    };
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
    // Screen-corner extension: quad in source pixels (TL, TR, BR, BL)
    // plus the pose in the capture camera frame (+X right, +Y down, +Z
    // forward) with its reprojection RMS. Filled locally for Triton seg
    // contracts, or served by a QLD2 streaming service.
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
