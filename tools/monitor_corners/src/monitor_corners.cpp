#include "monitor_corners/monitor_corners.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace questlab::corners {
namespace {

// Order 4 unordered quad vertices as TL, TR, BR, BL (y down): TL has the
// smallest x+y, BR the largest; of the other two, TR has the largest x-y.
bool OrderCorners(std::vector<cv::Point2f>* points) {
    if (points == nullptr || points->size() != 4) {
        return false;
    }
    auto& p = *points;
    const size_t topLeft = static_cast<size_t>(
        std::min_element(
            p.begin(), p.end(),
            [](const cv::Point2f& a, const cv::Point2f& b) {
                return a.x + a.y < b.x + b.y;
            }) -
        p.begin());
    const size_t bottomRight = static_cast<size_t>(
        std::max_element(
            p.begin(), p.end(),
            [](const cv::Point2f& a, const cv::Point2f& b) {
                return a.x + a.y < b.x + b.y;
            }) -
        p.begin());
    size_t rest[2] = {0, 0};
    size_t cursor = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (i != topLeft && i != bottomRight) {
            rest[cursor++] = i;
        }
    }
    const size_t topRight =
        (p[rest[0]].x - p[rest[0]].y) > (p[rest[1]].x - p[rest[1]].y)
        ? rest[0]
        : rest[1];
    const size_t bottomLeft = topRight == rest[0] ? rest[1] : rest[0];
    const std::array<cv::Point2f, 4> ordered = {
        p[topLeft], p[topRight], p[bottomRight], p[bottomLeft]};
    for (size_t i = 0; i < 4; ++i) {
        p[i] = ordered[i];
    }
    return true;
}

}  // namespace

bool FindScreenQuad(
    const uint8_t* rgb,
    int width,
    int height,
    Quad* quad,
    std::string* error) {
    const auto fail = [&](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (rgb == nullptr || quad == nullptr || width < 64 || height < 64) {
        return fail("invalid image or output");
    }
    const cv::Mat image(height, width, CV_8UC3, const_cast<uint8_t*>(rgb));
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_RGB2GRAY);
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 1.2);
    cv::Mat edges;
    cv::Canny(blurred, edges, 60.0, 180.0);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(
        edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    const double imageArea =
        static_cast<double>(width) * static_cast<double>(height);
    double bestScore = 0.0;
    std::vector<cv::Point2f> best;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        // Screens are neither specks nor the whole room.
        if (area < 0.005 * imageArea || area > 0.80 * imageArea) {
            continue;
        }
        std::vector<cv::Point> approx;
        cv::approxPolyDP(
            contour, approx, 0.02 * cv::arcLength(contour, true), true);
        if (approx.size() != 4 || !cv::isContourConvex(approx)) {
            continue;
        }
        const cv::Rect bounds = cv::boundingRect(approx);
        if (bounds.width < 32 || bounds.height < 32) {
            continue;
        }
        // Display aspect window: ultrawide 21:9 (2.33) to portrait 9:16
        // handled sideways; squarish contours are furniture, not screens.
        const double aspect = static_cast<double>(bounds.width) /
            static_cast<double>(bounds.height);
        if (aspect < 1.1 || aspect > 2.6) {
            continue;
        }
        const double boxArea = static_cast<double>(bounds.width) *
            static_cast<double>(bounds.height);
        const double fill = boxArea > 0.0 ? area / boxArea : 0.0;
        if (fill < 0.60) {
            continue;
        }
        if (fill > bestScore) {
            bestScore = fill;
            best.assign(approx.begin(), approx.end());
        }
    }
    if (best.empty()) {
        return fail("no screen-like quad contour");
    }
    std::vector<cv::Point2f> ordered;
    for (const auto& point : best) {
        ordered.emplace_back(
            static_cast<float>(point.x), static_cast<float>(point.y));
    }
    if (!OrderCorners(&ordered)) {
        return fail("corner ordering failed");
    }
    // Subpixel refinement against the gray gradients: corner pixels are
    // what PnP consumes, so pixel-quantized vertices are not enough.
    cv::cornerSubPix(
        gray, ordered, cv::Size(5, 5), cv::Size(-1, -1),
        cv::TermCriteria(
            cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.01));
    for (size_t i = 0; i < 4; ++i) {
        quad->corners[i] = {ordered[i].x, ordered[i].y};
    }
    quad->score = static_cast<float>(bestScore);
    return true;
}

bool SolveQuadPose(
    const Quad& quad,
    const Intrinsics& intrinsics,
    float monitorWidthMeters,
    float monitorHeightMeters,
    Pose* pose,
    float* reprojectionErrorPixels,
    std::string* error) {
    const auto fail = [&](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (pose == nullptr || monitorWidthMeters <= 0.0F ||
        monitorHeightMeters <= 0.0F || intrinsics.fx <= 0.0F ||
        intrinsics.fy <= 0.0F) {
        return fail("invalid pose input");
    }
    // Object frame: +X right, +Y up, origin at screen centre. Image TL
    // pairs with object top-left (-W/2, +H/2) per the header contract.
    const float halfW = monitorWidthMeters * 0.5F;
    const float halfH = monitorHeightMeters * 0.5F;
    const std::vector<cv::Point3f> object = {
        {-halfW, halfH, 0.0F},
        {halfW, halfH, 0.0F},
        {halfW, -halfH, 0.0F},
        {-halfW, -halfH, 0.0F},
    };
    std::vector<cv::Point2f> image;
    for (const Point2& corner : quad.corners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y)) {
            return fail("non-finite corner");
        }
        image.emplace_back(corner.x, corner.y);
    }
    const cv::Mat cameraMatrix = (cv::Mat_<double>(3, 3) << intrinsics.fx, 0.0,
                                  intrinsics.cx, 0.0, intrinsics.fy,
                                  intrinsics.cy, 0.0, 0.0, 1.0);
    cv::Mat rvec;
    cv::Mat tvec;
    // AP3P is the stable 4-point choice; RANSAC is meaningless with n=4
    // (no redundancy), so a direct solve plus reprojection gate instead.
    if (!cv::solvePnP(
            object, image, cameraMatrix, cv::noArray(), rvec, tvec, false,
            cv::SOLVEPNP_AP3P)) {
        return fail("solvePnP failed");
    }
    std::vector<cv::Point2f> reprojected;
    cv::projectPoints(object, rvec, tvec, cameraMatrix, cv::noArray(),
                      reprojected);
    double squared = 0.0;
    for (size_t i = 0; i < 4; ++i) {
        const double dx = reprojected[i].x - image[i].x;
        const double dy = reprojected[i].y - image[i].y;
        squared += dx * dx + dy * dy;
    }
    const float rms = static_cast<float>(std::sqrt(squared / 4.0));
    if (reprojectionErrorPixels != nullptr) {
        *reprojectionErrorPixels = rms;
    }
    cv::Mat rotation;
    cv::Rodrigues(rvec, rotation);
    // Camera frame is +Y down while the object frame is +Y up: the solved
    // rotation already encodes that flip, keep it verbatim.
    const double trace =
        rotation.at<double>(0, 0) + rotation.at<double>(1, 1) +
        rotation.at<double>(2, 2);
    double qw = std::sqrt(std::max(0.0, 1.0 + trace)) * 0.5;
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    if (qw > 1.0e-6) {
        qx = (rotation.at<double>(2, 1) - rotation.at<double>(1, 2)) /
            (4.0 * qw);
        qy = (rotation.at<double>(0, 2) - rotation.at<double>(2, 0)) /
            (4.0 * qw);
        qz = (rotation.at<double>(1, 0) - rotation.at<double>(0, 1)) /
            (4.0 * qw);
    } else {
        // 180-degree case: fall back to the largest diagonal.
        qx = std::sqrt(
                 std::max(
                     0.0, 1.0 + rotation.at<double>(0, 0) -
                         rotation.at<double>(1, 1) -
                         rotation.at<double>(2, 2))) *
            0.5;
        qy = std::sqrt(
                 std::max(
                     0.0, 1.0 - rotation.at<double>(0, 0) +
                         rotation.at<double>(1, 1) -
                         rotation.at<double>(2, 2))) *
            0.5;
        qz = std::sqrt(
                 std::max(
                     0.0, 1.0 - rotation.at<double>(0, 0) -
                         rotation.at<double>(1, 1) +
                         rotation.at<double>(2, 2))) *
            0.5;
    }
    const double norm =
        std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (!(norm > 1.0e-9)) {
        return fail("rotation conversion failed");
    }
    pose->quat = {
        static_cast<float>(qx / norm),
        static_cast<float>(qy / norm),
        static_cast<float>(qz / norm),
        static_cast<float>(qw / norm),
    };
    pose->position = {
        static_cast<float>(tvec.at<double>(0)),
        static_cast<float>(tvec.at<double>(1)),
        static_cast<float>(tvec.at<double>(2)),
    };
    return true;
}

}  // namespace questlab::corners
