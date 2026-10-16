#pragma once

// Screen-corner correspondences for the monitor: quad extraction from an
// RGB crop plus P4P/solvePnP pose against the taped display rectangle.
// OpenCV does the solving (calib3d); this header is the pairing contract.
//
// Corner order everywhere is image TL, TR, BR, BL (y down). It pairs with
// the object rectangle (-W/2,+H/2), (+W/2,+H/2), (+W/2,-H/2), (-W/2,-H/2)
// with +Y up: image-top maps to object-top. Keep the pairing; PnP quality
// depends on it, not on which corner is "first" beyond consistency.

#include <array>
#include <cstdint>
#include <string>

namespace questlab::corners {

struct Point2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Quad {
    // TL, TR, BR, BL in pixels of the source image.
    std::array<Point2, 4> corners{};
    // Fit quality in [0, 1]: contour area share of its bounding box.
    float score = 0.0F;
};

struct Intrinsics {
    float fx = 0.0F;
    float fy = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    // Skew (pixels) and Brown-Conrady k1, k2, k3, p1, p2. Zero keeps the
    // pinhole model; SolveQuadPose undistorts before solvePnP so host and
    // device share one camera model.
    float skew = 0.0F;
    std::array<float, 5> distortion{};
};

struct Pose {
    // Unit quaternion (x, y, z, w): object frame to camera frame.
    std::array<float, 4> quat{0.0F, 0.0F, 0.0F, 1.0F};
    // Object origin in the camera frame, metres: +X right, +Y down,
    // +Z forward (Camera2 basis, matching detection pixels directly).
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
};

// Largest plausible screen quad in an RGB image (row-major, 3 bytes/px).
// Returns false with a reason when no quad survives area/aspect gates:
// an empty answer is correct output, never a guess.
bool FindScreenQuad(
    const uint8_t* rgb,
    int width,
    int height,
    Quad* quad,
    std::string* error);

// Largest quad in a binary instance mask (row-major, nonzero is
// foreground: the seg-model path). Same TL/TR/BR/BL order and aspect
// gates as FindScreenQuad, but no subpixel refinement: a binary mask
// carries no gradients, so pixel vertices are the measurement. Score
// is the quad area share of the mask foreground (rectangularity).
bool FindScreenQuadFromMask(
    const uint8_t* mask,
    int width,
    int height,
    Quad* quad,
    std::string* error);

// Pose of the taped W×H display rectangle from its 4 image corners.
// Reprojection RMS (pixels) reports the fit; large values mean the quad
// is not the screen (wrong pairing or bent contour) — distrust the pose.
bool SolveQuadPose(
    const Quad& quad,
    const Intrinsics& intrinsics,
    float monitorWidthMeters,
    float monitorHeightMeters,
    Pose* pose,
    float* reprojectionErrorPixels,
    std::string* error);

}  // namespace questlab::corners
