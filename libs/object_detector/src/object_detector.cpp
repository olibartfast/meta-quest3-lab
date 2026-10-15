#include "object_detector/object_detector.h"

#include "rfdetr_inference/detection_io.h"
#include "rfdetr_inference/model_contract.h"
#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/preprocessing.h"
#include "screen_corners/screen_corners.h"
#include "triton_client/triton_client.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace questlab::detection {
namespace {

int64_t SteadyNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

class AsyncNewestFrameDetector : public IObjectDetector {
public:
    ~AsyncNewestFrameDetector() override {
        Stop();
    }

    bool Start(const DetectorConfig& config) final {
        Stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_ = config;
            stats_ = {};
            stats_.health = DetectorHealth::Starting;
            stopping_ = false;
            lastAcceptedNanoseconds_ = 0;
        }
        worker_ = std::thread(&AsyncNewestFrameDetector::WorkerMain, this);
        return true;
    }

    bool Submit(camera::RgbCapture capture) final {
        const int64_t now = SteadyNanoseconds();
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || (stats_.health != DetectorHealth::Starting &&
                          stats_.health != DetectorHealth::Running)) {
            return false;
        }
        if (config_.maximumSubmissionsPerSecond > 0.0F &&
            lastAcceptedNanoseconds_ > 0) {
            const double minimumInterval =
                1.0e9 / config_.maximumSubmissionsPerSecond;
            if (static_cast<double>(now - lastAcceptedNanoseconds_) <
                minimumInterval) {
                ++stats_.rateLimitedFrames;
                return false;
            }
        }
        lastAcceptedNanoseconds_ = now;
        if (pending_.has_value()) {
            ++stats_.replacedPendingFrames;
        }
        pending_ = std::move(capture);
        ++stats_.submittedFrames;
        stats_.currentQueueDepth = 1;
        stats_.queueHighWaterMark =
            std::max<uint64_t>(stats_.queueHighWaterMark, 1);
        condition_.notify_one();
        return true;
    }

    bool TryConsumeLatest(DetectionResult* result) final {
        if (result == nullptr) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!latestResult_.has_value()) {
            return false;
        }
        *result = std::move(*latestResult_);
        latestResult_.reset();
        return true;
    }

    DetectorStats GetStats() const final {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void Stop() final {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!worker_.joinable()) {
                stats_.health = DetectorHealth::Stopped;
                return;
            }
            stopping_ = true;
            pending_.reset();
            stats_.currentQueueDepth = 0;
        }
        CancelBackend();
        condition_.notify_all();
        worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.health = DetectorHealth::Stopped;
        latestResult_.reset();
    }

protected:
    virtual bool Prepare(
        const DetectorConfig& config,
        std::string* manifestIdentity,
        std::string* error) = 0;
    virtual bool Process(
        camera::RgbCapture capture,
        const std::string& manifestIdentity,
        DetectionResult* result,
        std::string* error) = 0;
    virtual void CancelBackend() {}
    virtual DetectorHealth FailureHealth() const {
        return DetectorHealth::Error;
    }
    virtual std::string BackendDetails() const { return {}; }

private:
    void WorkerMain() {
        DetectorConfig config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = config_;
        }
        std::string manifestIdentity;
        std::string error;
        if (!Prepare(config, &manifestIdentity, &error)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_) {
                stats_.health = FailureHealth();
                stats_.lastError = error;
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                return;
            }
            stats_.manifestIdentity = manifestIdentity;
            stats_.backendDetails = BackendDetails();
            stats_.health = DetectorHealth::Running;
        }

        while (true) {
            camera::RgbCapture capture;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [&]() {
                    return stopping_ || pending_.has_value();
                });
                if (stopping_) {
                    return;
                }
                capture = std::move(*pending_);
                pending_.reset();
                stats_.currentQueueDepth = 0;
            }

            DetectionResult result;
            error.clear();
            if (Process(
                    std::move(capture),
                    manifestIdentity,
                    &result,
                    &error)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_) {
                    latestResult_ = std::move(result);
                    ++stats_.completedFrames;
                    stats_.lastCompletedFrameId = latestResult_->frameId;
                }
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_) {
                    ++stats_.inferenceFailures;
                    stats_.lastError = error;
                    stats_.health = FailureHealth();
                    return;
                }
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    DetectorConfig config_;
    DetectorStats stats_;
    std::optional<camera::RgbCapture> pending_;
    std::optional<DetectionResult> latestResult_;
    std::thread worker_;
    bool stopping_ = true;
    int64_t lastAcceptedNanoseconds_ = 0;
};

#if !defined(_WIN32)

// Direct Triton backend: preprocessing and postprocessing run in the app;
// Triton executes the model tensors over KServe v2 HTTP.
class TritonDetector final : public AsyncNewestFrameDetector {
public:
    DetectorCapabilities GetCapabilities() const override {
        return {

            "Triton Inference Server (KServe v2 HTTP)",
            true,
            true,
            true,
        };
    }

protected:
    bool Prepare(
        const DetectorConfig& config,
        std::string* manifestIdentity,
        std::string* error) override {
        if (config.manifestPath.empty() || config.tritonModel.empty()) {
            if (error != nullptr) {
                *error = "Triton backend requires a manifest and a model name";
            }
            return false;
        }
        if (!rfdetr::LoadModelContract(
                config.manifestPath, &contract_, error)) {
            return false;
        }
        if (!config.expectedManifestIdentity.empty() &&
            config.expectedManifestIdentity != contract_.onnxSha256) {
            if (error != nullptr) {
                *error =
                    "Configured manifest identity does not match model manifest";
            }
            return false;
        }
        if (contract_.outputs.size() < 2U) {
            if (error != nullptr) {
                *error = "Manifest declares fewer than two outputs";
            }
            return false;
        }
        // Requested outputs mirror the contract; masks for seg contracts.
        outputTemplates_.clear();
        for (const rfdetr::TensorContract& output : contract_.outputs) {
            questlab::triton::FloatTensor tensor;
            tensor.name = output.name;
            tensor.shape = output.shape;
            outputTemplates_.push_back(std::move(tensor));
        }
        modelName_ = config.tritonModel;
        modelVersion_ = config.tritonVersion;
        classSizes_ = config.classSizes;
        const std::string url =
            config.tritonHost + ':' + std::to_string(config.tritonPort);
        if (!client_.Initialize(url, error) ||
            !client_.IsServerLive(error) ||
            !client_.IsModelReady(modelName_, modelVersion_, error)) {
            return false;
        }
        // Warm-up runs the full path, masks included for seg contracts.
        size_t inputCount = 1;
        for (const int64_t dimension : contract_.input.shape) {
            inputCount *= static_cast<size_t>(dimension);
        }
        questlab::triton::FloatTensor warmupInput;
        warmupInput.name = contract_.input.name;
        warmupInput.shape = contract_.input.shape;
        warmupInput.values.assign(inputCount, 0.0F);
        std::vector<questlab::triton::FloatTensor> warmupOutputs =
            outputTemplates_;
        if (!client_.Infer(
                modelName_, modelVersion_, warmupInput, &warmupOutputs,
                error)) {
            return false;
        }
        backendDetails_ = "Triton " + url + " " + modelName_;
        *manifestIdentity = contract_.onnxSha256;
        return true;
    }

    bool Process(
        camera::RgbCapture capture,
        const std::string& manifestIdentity,
        DetectionResult* result,
        std::string* error) override {
        if (result == nullptr) {
            return false;
        }
        std::vector<uint8_t> rgba;
        std::vector<float> input;
        if (!rfdetr::PreprocessCapture(
                capture, contract_, &rgba, &input, error)) {
            return false;
        }
        questlab::triton::FloatTensor inputTensor;
        inputTensor.name = contract_.input.name;
        inputTensor.shape = contract_.input.shape;
        inputTensor.values = std::move(input);
        std::vector<questlab::triton::FloatTensor> outputs = outputTemplates_;
        const int64_t inferenceStart = SteadyNanoseconds();
        if (!client_.Infer(
                modelName_, modelVersion_, inputTensor, &outputs, error)) {
            return false;
        }
        const int64_t inferenceEnd = SteadyNanoseconds();
        std::vector<rfdetr::Detection> detections;
        if (contract_.hasMasks && outputs.size() >= 3U) {
            if (!rfdetr::PostprocessDetectionsWithMasks(
                    outputs[0].values, outputs[1].values, outputs[2].values,
                    capture.width, capture.height, 0.0F, contract_,
                    &detections, error)) {
                return false;
            }
        } else if (!rfdetr::PostprocessDetections(
                       outputs[0].values, outputs[1].values, capture.width,
                       capture.height, contract_, &detections, error)) {
            return false;
        }
        DetectionResult completed;
        completed.frameId = capture.frameId;
        completed.captureTimestampNanoseconds =
            capture.sensorTimestampNanoseconds;
        completed.inferenceStartNanoseconds = inferenceStart;
        completed.inferenceEndNanoseconds = inferenceEnd;
        completed.sourceWidth = capture.width;
        completed.sourceHeight = capture.height;
        completed.manifestIdentity = manifestIdentity;
        completed.detections.reserve(detections.size());
        const questlab::corners::Intrinsics intrinsics{
            capture.intrinsics.fx, capture.intrinsics.fy,
            capture.intrinsics.cx, capture.intrinsics.cy};
        for (const rfdetr::Detection& detection : detections) {
            Detection converted;
            converted.classId = detection.classId;
            converted.className = detection.className;
            converted.confidence = detection.confidence;
            converted.boxXyxy = detection.boxXyxy;
            if (detection.hasMask && detection.maskWidth > 0 &&
                detection.maskHeight > 0) {
                // Class-agnostic: every masked detection gets a quad
                // attempt; a mask that is not quadrilateral yields no
                // corners rather than a guess.
                questlab::corners::Quad quad;
                std::string cornerError;
                if (questlab::corners::FindScreenQuadFromMask(
                        detection.mask.data(), detection.maskWidth,
                        detection.maskHeight, &quad, &cornerError)) {
                    converted.hasCorners = true;
                    for (size_t corner = 0; corner < 4U; ++corner) {
                        converted.cornerXy[corner * 2U] =
                            quad.corners[corner].x;
                        converted.cornerXy[corner * 2U + 1U] =
                            quad.corners[corner].y;
                    }
                    const auto known =
                        classSizes_.find(detection.className);
                    if (known != classSizes_.end()) {
                        questlab::corners::Pose pose;
                        float reprojection = -1.0F;
                        std::string poseError;
                        if (questlab::corners::SolveQuadPose(
                                quad, intrinsics, known->second[0],
                                known->second[1], &pose, &reprojection,
                                &poseError)) {
                            converted.hasCornerPose = true;
                            converted.cornerQuatXyzw = pose.quat;
                            converted.cornerPosition = pose.position;
                            converted.cornerReprojectionPixels =
                                reprojection;
                            converted.cornerSizeWH = known->second;
                        }
                    }
                }
            }
            completed.detections.push_back(std::move(converted));
        }
        *result = std::move(completed);
        return true;
    }

    void CancelBackend() override {}

    DetectorHealth FailureHealth() const override {
        return DetectorHealth::Disconnected;
    }

    std::string BackendDetails() const override { return backendDetails_; }

private:
    questlab::triton::TritonHttpClient client_;
    rfdetr::ModelContract contract_;
    std::vector<questlab::triton::FloatTensor> outputTemplates_;
    std::unordered_map<std::string, std::array<float, 2>> classSizes_;
    std::string modelName_;
    std::string modelVersion_;
    std::string backendDetails_;
};


#else

class TritonDetector final : public IObjectDetector {
public:
    DetectorCapabilities GetCapabilities() const override {
        return {"Unavailable on Windows"};
    }
    bool Start(const DetectorConfig&) override { return false; }
    bool Submit(camera::RgbCapture) override { return false; }
    bool TryConsumeLatest(DetectionResult*) override { return false; }
    DetectorStats GetStats() const override {
        DetectorStats stats;
        stats.health = DetectorHealth::Unsupported;
        stats.lastError = "Triton backend is unavailable on Windows";
        return stats;
    }
    void Stop() override {}
};

#endif

}  // namespace

std::unique_ptr<IObjectDetector> CreateObjectDetector(
    const DetectorConfig& config) {
    (void)config;
    return std::make_unique<TritonDetector>();
}

bool AnnotateRgba(
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* rgba,
    const std::vector<Detection>& detections,
    const std::vector<std::string>& statusLines,
    std::string* error) {
    std::vector<rfdetr::Detection> converted;
    converted.reserve(detections.size());
    for (const Detection& detection : detections) {
        rfdetr::Detection convertedDetection{
            detection.classId,
            detection.className,
            detection.confidence,
            detection.boxXyxy,
        };
        convertedDetection.hasCorners = detection.hasCorners;
        convertedDetection.cornerXy = detection.cornerXy;
        converted.push_back(std::move(convertedDetection));
    }
    return rfdetr::AnnotateRgba(
               width, height, rgba, converted, error) &&
           rfdetr::AnnotateRgbaStatus(
               width, height, rgba, statusLines, error);
}

}  // namespace questlab::detection
