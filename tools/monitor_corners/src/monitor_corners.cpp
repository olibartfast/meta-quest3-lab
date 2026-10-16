#include "monitor_corners/monitor_corners.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace questlab::corners {
namespace {

// Order 4 unordered quad vertices as TL, TR, BR, BL (y down) by polar
// angle around the centroid: robust under head roll and strong
// perspective, where the x+y / x-y sums mislabel corners.
bool OrderCorners(std::vector<cv::Point2f>* points) {
    if (points == nullptr || points->size() != 4) {
        return false;
    }
    auto& p = *points;
    double centroidX = 0.0;
    double centroidY = 0.0;
    for (const auto& point : p) {
        centroidX += point.x;
        centroidY += point.y;
    }
    centroidX *= 0.25;
    centroidY *= 0.25;
    std::array<double, 4> angles{};
    for (size_t i = 0; i < 4; ++i) {
        angles[i] =
            std::atan2(p[i].y - centroidY, p[i].x - centroidX);
    }
    std::array<size_t, 4> order = {0, 1, 2, 3};
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return angles[a] < angles[b];
    });
    std::array<cv::Point2f, 4> sorted = {
        p[order[0]], p[order[1]], p[order[2]], p[order[3]]};
    size_t topLeft = 0;
    for (size_t i = 1; i < 4; ++i) {
        if (sorted[i].x + sorted[i].y < sorted[topLeft].x + sorted[topLeft].y) {
            topLeft = i;
        }
    }
    std::array<cv::Point2f, 4> rotated{};
    for (size_t i = 0; i < 4; ++i) {
        rotated[i] = sorted[(topLeft + i) % 4];
    }
    double twice = 0.0;
    for (size_t i = 0; i < 4; ++i) {
        const auto& a = rotated[i];
        const auto& b = rotated[(i + 1) % 4];
        twice += static_cast<double>(a.x) * b.y -
                 static_cast<double>(b.x) * a.y;
    }
    if (twice < 0.0) {
        const std::array<cv::Point2f, 4> fixed = {
            rotated[0], rotated[3], rotated[2], rotated[1]};
        rotated = fixed;
    }
    for (size_t i = 0; i < 4; ++i) {
        p[i] = rotated[i];
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
        // Display aspect window on the longer/shorter ratio: ultrawide
        // 21:9 (2.33) to portrait handled the same way; squarish contours
        // are furniture, not screens.
        const double longer =
            static_cast<double>(std::max(bounds.width, bounds.height));
        const double shorter = static_cast<double>(
            std::max(1, std::min(bounds.width, bounds.height)));
        const double aspect = longer / shorter;
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

bool FindScreenQuadFromMask(
    const uint8_t* mask,
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
    if (mask == nullptr || quad == nullptr || width < 64 || height < 64) {
        return fail("invalid mask or output");
    }
    const cv::Mat binary(
        height, width, CV_8UC1, const_cast<uint8_t*>(mask));
    cv::Mat foreground;
    cv::threshold(binary, foreground, 0, 255, cv::THRESH_BINARY);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(
        foreground, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    const double imageArea =
        static_cast<double>(width) * static_cast<double>(height);
    const double foregroundArea = cv::countNonZero(foreground);
    if (foregroundArea < 0.005 * imageArea ||
        foregroundArea > 0.80 * imageArea) {
        return fail("mask foreground outside plausible screen share");
    }
    double bestArea = 0.0;
    std::vector<cv::Point> best;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area > bestArea) {
            bestArea = area;
            best = contour;
        }
    }
    if (best.empty()) {
        return fail("no mask contour");
    }
    std::vector<cv::Point> approx;
    cv::approxPolyDP(
        best, approx, 0.02 * cv::arcLength(best, true), true);
    if (approx.size() != 4 || !cv::isContourConvex(approx)) {
        return fail("mask contour is not quadrilateral");
    }
    const cv::Rect bounds = cv::boundingRect(approx);
    if (bounds.width < 32 || bounds.height < 32) {
        return fail("mask quad below minimum size");
    }
    if (bounds.x <= 3 || bounds.y <= 3 ||
        bounds.x + bounds.width >= width - 3 ||
        bounds.y + bounds.height >= height - 3) {
        return fail("mask quad touches the image border");
    }
    const double longer =
        static_cast<double>(std::max(bounds.width, bounds.height));
    const double shorter =
        static_cast<double>(std::max(1, std::min(bounds.width, bounds.height)));
    const double aspect = longer / shorter;
    if (aspect < 1.1 || aspect > 2.6) {
        return fail("mask quad outside display aspect window");
    }
    std::vector<cv::Point2f> ordered;
    for (const auto& point : approx) {
        ordered.emplace_back(
            static_cast<float>(point.x), static_cast<float>(point.y));
    }
    if (!OrderCorners(&ordered)) {
        return fail("corner ordering failed");
    }
    for (size_t i = 0; i < 4; ++i) {
        quad->corners[i] = {ordered[i].x, ordered[i].y};
    }
    quad->score = static_cast<float>(
        bestArea / (foregroundArea > 0.0 ? foregroundArea : 1.0));
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
    const cv::Mat cameraMatrix =
        (cv::Mat_<double>(3, 3) << intrinsics.fx, intrinsics.skew,
         intrinsics.cx, 0.0, intrinsics.fy, intrinsics.cy, 0.0, 0.0, 1.0);
    cv::Mat distCoeffs = (cv::Mat_<double>(5, 1) << intrinsics.distortion[0],
                          intrinsics.distortion[1], intrinsics.distortion[2],
                          intrinsics.distortion[3], intrinsics.distortion[4]);
    cv::Mat rvec;
    cv::Mat tvec;
    // AP3P is the stable 4-point choice; RANSAC is meaningless with n=4
    // (no redundancy), so a direct solve plus reprojection gate instead.
    if (!cv::solvePnP(
            object, image, cameraMatrix, distCoeffs, rvec, tvec, false,
            cv::SOLVEPNP_AP3P)) {
        return fail("solvePnP failed");
    }
    std::vector<cv::Point2f> reprojected;
    cv::projectPoints(object, rvec, tvec, cameraMatrix, distCoeffs,
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
    // Shepperd's method: branch on the largest of trace, R00, R11, R22.
    // The screen convention sits at trace = -1 (w ~= 0 nominal), so the
    // old trace-or-signless-fallback mirrored yaw near 180 deg.
    const double trace =
        rotation.at<double>(0, 0) + rotation.at<double>(1, 1) +
        rotation.at<double>(2, 2);
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    double qw = 0.0;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        qw = 0.25 * s;
        qx = (rotation.at<double>(2, 1) - rotation.at<double>(1, 2)) / s;
        qy = (rotation.at<double>(0, 2) - rotation.at<double>(2, 0)) / s;
        qz = (rotation.at<double>(1, 0) - rotation.at<double>(0, 1)) / s;
    } else if (
        rotation.at<double>(0, 0) > rotation.at<double>(1, 1) &&
        rotation.at<double>(0, 0) > rotation.at<double>(2, 2)) {
        const double s =
            std::sqrt(
                1.0 + rotation.at<double>(0, 0) - rotation.at<double>(1, 1) -
                rotation.at<double>(2, 2)) *
            2.0;
        qw = (rotation.at<double>(2, 1) - rotation.at<double>(1, 2)) / s;
        qx = 0.25 * s;
        qy = (rotation.at<double>(0, 1) + rotation.at<double>(1, 0)) / s;
        qz = (rotation.at<double>(0, 2) + rotation.at<double>(2, 0)) / s;
    } else if (
        rotation.at<double>(1, 1) > rotation.at<double>(2, 2)) {
        const double s =
            std::sqrt(
                1.0 + rotation.at<double>(1, 1) - rotation.at<double>(0, 0) -
                rotation.at<double>(2, 2)) *
            2.0;
        qw = (rotation.at<double>(0, 2) - rotation.at<double>(2, 0)) / s;
        qx = (rotation.at<double>(0, 1) + rotation.at<double>(1, 0)) / s;
        qy = 0.25 * s;
        qz = (rotation.at<double>(1, 2) + rotation.at<double>(2, 1)) / s;
    } else {
        const double s =
            std::sqrt(
                1.0 + rotation.at<double>(2, 2) - rotation.at<double>(0, 0) -
                rotation.at<double>(1, 1)) *
            2.0;
        qw = (rotation.at<double>(1, 0) - rotation.at<double>(0, 1)) / s;
        qx = (rotation.at<double>(0, 2) + rotation.at<double>(2, 0)) / s;
        qy = (rotation.at<double>(1, 2) + rotation.at<double>(2, 1)) / s;
        qz = 0.25 * s;
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
