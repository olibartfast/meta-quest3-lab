#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include "monitor_corners/monitor_corners.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        ++failures;
    }
}

float CornerError(
    const questlab::corners::Quad& quad,
    const std::vector<cv::Point2f>& expected) {
    // Set match: each extracted corner must land near some projected one.
    // (Index order differs by construction: image TL-first vs object order.
    // Pairing correctness is proven by the pose assertions below instead.)
    float worst = 0.0F;
    for (size_t i = 0; i < 4; ++i) {
        float best = 1.0e9F;
        for (const auto& point : expected) {
            const float dx = quad.corners[i].x - point.x;
            const float dy = quad.corners[i].y - point.y;
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        worst = std::max(worst, best);
    }
    return worst;
}

}  // namespace

int main() {
    constexpr int kWidth = 640;
    constexpr int kHeight = 480;
    constexpr float kScreenW = 0.55F;
    constexpr float kScreenH = 0.32F;
    const cv::Mat cameraMatrix =
        (cv::Mat_<double>(3, 3) << 600.0, 0.0, 320.0, 0.0, 600.0, 240.0,
         0.0, 0.0, 1.0);
    // Tilted screen: 20 deg yaw, 10 deg pitch, 2 m out. Built as
    // yaw * pitch-from-facing so the display stays upright in the image:
    // image-TL truly is the object top-left (the pairing contract).
    const cv::Mat yawVec = (cv::Mat_<double>(3, 1) << 0.0, 0.3491, 0.0);
    const cv::Mat pitchVec = (cv::Mat_<double>(3, 1)
                                  << 3.14159265 - 0.1745, 0.0, 0.0);
    cv::Mat yawMat;
    cv::Rodrigues(yawVec, yawMat);
    cv::Mat pitchMat;
    cv::Rodrigues(pitchVec, pitchMat);
    const cv::Mat trueRotation = yawMat * pitchMat;
    cv::Mat rvec;
    cv::Rodrigues(trueRotation, rvec);
    const cv::Mat tvec = (cv::Mat_<double>(3, 1) << 0.1, -0.05, 2.0);
    const std::vector<cv::Point3f> object = {
        {-kScreenW / 2, kScreenH / 2, 0.0F},
        {kScreenW / 2, kScreenH / 2, 0.0F},
        {kScreenW / 2, -kScreenH / 2, 0.0F},
        {-kScreenW / 2, -kScreenH / 2, 0.0F},
    };
    std::vector<cv::Point2f> projected;
    cv::projectPoints(
        object, rvec, tvec, cameraMatrix, cv::noArray(), projected);

    cv::Mat gray(kHeight, kWidth, CV_8UC1, cv::Scalar(18));
    std::vector<cv::Point> polygon;
    for (const auto& point : projected) {
        polygon.emplace_back(
            static_cast<int>(std::lround(point.x)),
            static_cast<int>(std::lround(point.y)));
    }
    cv::fillConvexPoly(gray, polygon, cv::Scalar(235));
    cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0.6);
    cv::Mat rgb;
    cv::cvtColor(gray, rgb, cv::COLOR_GRAY2RGB);

    questlab::corners::Quad quad{};
    std::string error;
    Expect(
        questlab::corners::FindScreenQuad(
            rgb.data, kWidth, kHeight, &quad, &error),
        error.empty() ? "synthetic screen must be found" : error.c_str());
    Expect(
        CornerError(quad, projected) < 3.0F,
        "extracted corners must match projection within 3 px");

    questlab::corners::Intrinsics intrinsics{600.0F, 600.0F, 320.0F, 240.0F};
    questlab::corners::Pose pose{};
    float reprojection = -1.0F;
    Expect(
        questlab::corners::SolveQuadPose(
            quad, intrinsics, kScreenW, kScreenH, &pose, &reprojection,
            &error),
        error.empty() ? "synthetic pose must solve" : error.c_str());
    Expect(reprojection >= 0.0F && reprojection < 1.0F,
           "synthetic reprojection must be sub-pixel");
    {
        // Rotation error vs ground truth via matrix trace.
        // Rebuild solved rotation from the quaternion.
        const float x = pose.quat[0];
        const float y = pose.quat[1];
        const float z = pose.quat[2];
        const float w = pose.quat[3];
        float solved[3][3] = {
            {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),
             2 * (x * z + y * w)},
            {2 * (x * y + z * w), 1 - 2 * (x * x + z * z),
             2 * (y * z - x * w)},
            {2 * (x * z - y * w), 2 * (y * z + x * w),
             1 - 2 * (x * x + y * y)},
        };
        float productTrace = 0.0F;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                productTrace += solved[r][c] *
                    static_cast<float>(trueRotation.at<double>(r, c));
            }
        }
        const float angle =
            std::acos(
                std::clamp((productTrace - 1.0F) * 0.5F, -1.0F, 1.0F)) *
            180.0F / 3.14159265F;
        Expect(angle < 2.0F, "synthetic rotation error must be under 2 deg");
        const float tx = pose.position[0] - 0.1F;
        const float ty = pose.position[1] + 0.05F;
        const float tz = pose.position[2] - 2.0F;
        const float translationError =
            std::sqrt(tx * tx + ty * ty + tz * tz) / 2.0F;
        Expect(
            translationError < 0.02F,
            "synthetic translation error must be under 2%");
    }

    // Blank wall: refusal with a reason, never a hallucinated screen.
    cv::Mat blank(kHeight, kWidth, CV_8UC3, cv::Scalar(40, 40, 40));
    questlab::corners::Quad none{};
    Expect(
        !questlab::corners::FindScreenQuad(
            blank.data, kWidth, kHeight, &none, &error),
        "a blank wall must not produce a quad");
    Expect(!error.empty(), "a refusal must carry a reason");

    if (failures == 0) {
        std::puts("monitor corners tests passed");
    }
    return failures == 0 ? 0 : 1;
}
