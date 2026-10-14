// Host demo consuming quest_vision: video file in, annotated video out.
//
// Per sampled frame: RF-DETR (Triton) detections, screen-quad extraction +
// P4P pose for tv-class boxes, 2D overlay (boxes, quads, projected axes).
// A quest_vision session is owned for the run (params + version MbQ); the
// QvFuse depth stage stays pending until per-frame depth (DA3) lands, at
// which point this same harness feeds it without protocol changes.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "monitor_corners/monitor_corners.h"
#include "quest_vision/quest_vision_c_api.h"
#include "rfdetr_inference/model_contract.h"
#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/triton_session.h"

namespace {

struct Options {
    std::string video;
    std::string manifest;
    std::string tritonUrl = "http://127.0.0.1:8000";
    std::string model = "rfdetr_medium";
    std::string version = "1";
    std::string output = "build/vision-demo/out.mp4";
    int stride = 5;
    int maxFrames = 300;
    float fovDegrees = 60.0F;
    float monitorW = 0.55F;
    float monitorH = 0.32F;
};

bool ParseArgs(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name, std::string* out) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n", name);
                return false;
            }
            *out = argv[++i];
            return true;
        };
        if (arg == "--video") {
            if (!need("--video", &options->video)) return false;
        } else if (arg == "--manifest") {
            if (!need("--manifest", &options->manifest)) return false;
        } else if (arg == "--triton-url") {
            if (!need("--triton-url", &options->tritonUrl)) return false;
        } else if (arg == "--model") {
            if (!need("--model", &options->model)) return false;
        } else if (arg == "--version") {
            if (!need("--version", &options->version)) return false;
        } else if (arg == "--output") {
            if (!need("--output", &options->output)) return false;
        } else if (arg == "--stride") {
            options->stride = std::atoi(argv[++i]);
        } else if (arg == "--max-frames") {
            options->maxFrames = std::atoi(argv[++i]);
        } else if (arg == "--fov") {
            options->fovDegrees = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--monitor-size") {
            if (i + 1 >= argc) return false;
            if (std::sscanf(
                    argv[++i], "%f,%f", &options->monitorW,
                    &options->monitorH) != 2) {
                return false;
            }
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return false;
        }
    }
    return !options->video.empty() && !options->manifest.empty();
}

// Manifest-driven preprocess (squash bilinear via OpenCV, RGB normalize).
bool Preprocess(
    const cv::Mat& bgr,
    const questlab::rfdetr::ModelContract& contract,
    std::vector<float>* tensor) {
    const int targetH = static_cast<int>(contract.input.shape[2]);
    const int targetW = static_cast<int>(contract.input.shape[3]);
    cv::Mat resized;
    cv::resize(
        bgr, resized, cv::Size(targetW, targetH), 0, 0, cv::INTER_LINEAR);
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    cv::Mat floatImage;
    rgb.convertTo(floatImage, CV_32FC3,
                  contract.scale);
    std::vector<cv::Mat> channels(3);
    cv::split(floatImage, channels);
    tensor->assign(static_cast<size_t>(targetW) * targetH * 3U, 0.0F);
    const size_t plane = static_cast<size_t>(targetW) * targetH;
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < targetH; ++y) {
            const float* row = channels[c].ptr<float>(y);
            for (int x = 0; x < targetW; ++x) {
                (*tensor)[static_cast<size_t>(c) * plane +
                          static_cast<size_t>(y) * targetW + x] =
                    (row[x] - contract.mean[c]) /
                    contract.standardDeviation[c];
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, &options)) {
        std::fprintf(
            stderr,
            "usage: vision_demo --video PATH --manifest PATH "
            "[--triton-url URL] [--model NAME] [--version VER] "
            "[--output PATH] [--stride N] [--max-frames N] [--fov DEG] "
            "[--monitor-size W,H]\n");
        return 2;
    }
    std::printf("quest_vision: %s\n", QvVersion());
    QvFusionHandle session = QvCreate();
    if (session == nullptr) {
        std::fprintf(stderr, "quest_vision session failed\n");
        return 1;
    }
    QvFusionParams fusionParams{};
    QvFusionParamsDefault(&fusionParams);
    if (QvSetParams(session, &fusionParams) != QV_OK) {
        std::fprintf(stderr, "quest_vision params rejected\n");
        QvDestroy(session);
        return 1;
    }

    questlab::rfdetr::ModelContract contract;
    std::string error;
    if (!questlab::rfdetr::LoadModelContract(
            options.manifest, &contract, &error)) {
        std::fprintf(stderr, "manifest rejected: %s\n", error.c_str());
        QvDestroy(session);
        return 1;
    }
    questlab::rfdetr::TritonSession detector;
    if (!detector.Initialize(
            options.tritonUrl, options.model, options.version, contract,
            &error)) {
        std::fprintf(stderr, "triton init failed: %s\n", error.c_str());
        QvDestroy(session);
        return 1;
    }

    cv::VideoCapture capture(options.video);
    if (!capture.isOpened()) {
        std::fprintf(stderr, "cannot open video\n");
        QvDestroy(session);
        return 1;
    }
    const int sourceW = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int sourceH =
        static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const double fps = capture.get(cv::CAP_PROP_FPS);
    cv::VideoWriter writer(
        options.output, cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
        fps > 0.0 ? fps : 30.0, cv::Size(sourceW, sourceH));
    if (!writer.isOpened()) {
        std::fprintf(stderr, "cannot open output\n");
        QvDestroy(session);
        return 1;
    }
    // Assumed pinhole when the clip has no calibration (spike-grade).
    const float halfFov = options.fovDegrees * 3.14159265F / 360.0F;
    const questlab::corners::Intrinsics intrinsics{
        sourceW / (2.0F * std::tan(halfFov)),
        sourceH / (2.0F * std::tan(halfFov)),
        sourceW * 0.5F,
        sourceH * 0.5F,
    };

    int64_t frames = 0;
    int64_t inferred = 0;
    int64_t quads = 0;
    int64_t poses = 0;
    cv::Mat frame;
    while (frames < options.maxFrames && capture.read(frame)) {
        if (frames % options.stride == 0) {
            std::vector<float> input;
            Preprocess(frame, contract, &input);
            std::vector<float> boxes;
            std::vector<float> logits;
            if (detector.Run(input, &boxes, &logits, &error)) {
                std::vector<questlab::rfdetr::Detection> detections;
                if (questlab::rfdetr::PostprocessDetections(
                        boxes, logits, sourceW, sourceH, contract,
                        &detections, &error)) {
                    ++inferred;
                    for (const auto& detection : detections) {
                        const cv::Rect box(
                            static_cast<int>(detection.boxXyxy[0]),
                            static_cast<int>(detection.boxXyxy[1]),
                            static_cast<int>(detection.boxXyxy[2] -
                                             detection.boxXyxy[0]),
                            static_cast<int>(detection.boxXyxy[3] -
                                             detection.boxXyxy[1]));
                        cv::rectangle(frame, box, cv::Scalar(0, 255, 0), 2);
                        cv::putText(
                            frame,
                            detection.className + " " +
                                std::to_string(detection.confidence)
                                    .substr(0, 4),
                            box.tl(), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 255, 0), 2);
                        if (detection.className != "tv") {
                            continue;
                        }
                        // Quad over the full frame, kept when centred in
                        // the tv box (avoids strays elsewhere).
                        cv::Mat rgb;
                        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
                        questlab::corners::Quad quad{};
                        if (!questlab::corners::FindScreenQuad(
                                rgb.data, sourceW, sourceH, &quad, nullptr)) {
                            continue;
                        }
                        const float qcx =
                            (quad.corners[0].x + quad.corners[2].x) * 0.5F;
                        const float qcy =
                            (quad.corners[0].y + quad.corners[2].y) * 0.5F;
                        if (qcx < detection.boxXyxy[0] ||
                            qcx > detection.boxXyxy[2] ||
                            qcy < detection.boxXyxy[1] ||
                            qcy > detection.boxXyxy[3]) {
                            continue;
                        }
                        ++quads;
                        std::vector<cv::Point> polygon;
                        for (const auto& corner : quad.corners) {
                            polygon.emplace_back(
                                static_cast<int>(corner.x),
                                static_cast<int>(corner.y));
                        }
                        cv::polylines(
                            frame, polygon, true, cv::Scalar(255, 255, 0),
                            2);
                        questlab::corners::Pose pose{};
                        float reprojection = -1.0F;
                        if (questlab::corners::SolveQuadPose(
                                quad, intrinsics, options.monitorW,
                                options.monitorH, &pose, &reprojection,
                                nullptr) &&
                            reprojection < 2.0F) {
                            ++poses;
                            // Project object axes (0.2 m) for the overlay.
                            const cv::Mat cameraMatrix =
                                (cv::Mat_<double>(3, 3)
                                     << intrinsics.fx, 0.0, intrinsics.cx,
                                 0.0, intrinsics.fy, intrinsics.cy, 0.0,
                                 0.0, 1.0);
                            const float qw = pose.quat[3];
                            const float qx = pose.quat[0];
                            const float qy = pose.quat[1];
                            const float qz = pose.quat[2];
                            const cv::Mat rotation =
                                (cv::Mat_<double>(3, 3)
                                     << 1 - 2 * (qy * qy + qz * qz),
                                 2 * (qx * qy - qz * qw),
                                 2 * (qx * qz + qy * qw),
                                 2 * (qx * qy + qz * qw),
                                 1 - 2 * (qx * qx + qz * qz),
                                 2 * (qy * qz - qx * qw),
                                 2 * (qx * qz - qy * qw),
                                 2 * (qy * qz + qx * qw),
                                 1 - 2 * (qx * qx + qy * qy));
                            cv::Mat rvec;
                            cv::Rodrigues(rotation, rvec);
                            const cv::Mat tvec =
                                (cv::Mat_<double>(3, 1)
                                     << pose.position[0], pose.position[1],
                                 pose.position[2]);
                            const std::vector<cv::Point3f> axes = {
                                {0, 0, 0},
                                {0.2F, 0, 0},
                                {0, 0.2F, 0},
                                {0, 0, 0.2F},
                            };
                            std::vector<cv::Point2f> projected;
                            cv::projectPoints(
                                axes, rvec, tvec, cameraMatrix,
                                cv::noArray(), projected);
                            cv::line(
                                frame, projected[0], projected[1],
                                cv::Scalar(0, 0, 255), 3);
                            cv::line(
                                frame, projected[0], projected[2],
                                cv::Scalar(0, 255, 0), 3);
                            cv::line(
                                frame, projected[0], projected[3],
                                cv::Scalar(255, 0, 0), 3);
                            cv::putText(
                                frame,
                                "rms " +
                                    std::to_string(reprojection).substr(0, 4),
                                cv::Point(box.x, box.y - 8),
                                cv::FONT_HERSHEY_SIMPLEX, 0.6,
                                cv::Scalar(255, 255, 0), 2);
                        }
                    }
                }
            }
        }
        writer.write(frame);
        ++frames;
    }
    QvDestroy(session);
    std::printf(
        "frames=%lld inferred=%lld quads=%lld poses=%lld out=%s\n",
        static_cast<long long>(frames), static_cast<long long>(inferred),
        static_cast<long long>(quads), static_cast<long long>(poses),
        options.output.c_str());
    return 0;
}
