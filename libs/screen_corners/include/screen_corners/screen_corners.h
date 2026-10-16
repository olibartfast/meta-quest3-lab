#pragma once

// Screen-corner correspondences, OpenCV-free: quad extraction from a
// binary instance mask plus planar P4P pose. This is the on-device port of
// tools/monitor_corners (the host tool keeps its OpenCV implementation for
// reference); the pairing contract and gates match it.
//
// Corner order everywhere is image TL, TR, BR, BL (y down). It pairs with
// the object rectangle (-W/2,+H/2), (+W/2,+H/2), (+W/2,-H/2), (-W/2,-H/2)
// with +Y up: image-top maps to object-top. Keep the pairing; pose quality
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
    // Fit quality in [0, 1]: quad area share of the mask foreground
    // (rectangularity).
    float score = 0.0F;
};

struct Intrinsics {
    float fx = 0.0F;
    float fy = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    // Skew (pixels) and Brown-Conrady k1, k2, k3, p1, p2, matching
    // camera_source::CameraIntrinsics and libs/detection_projection. Zero
    // means the pinhole model; SolveQuadPose undistorts corners before the
    // homography so the PnP pose and the box-ray frustum share one model.
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

// Single gate for a solved corner pose, shared by the detector worker and
// the render path so the threshold cannot drift between the two:
//   - rectangularity (quad area share of mask foreground) >= 0.70,
//   - absolute reprojection RMS <= 8 px (coarse aspect-mismatch catch),
//   - relative reprojection RMS <= 3% of the quad diagonal (an absolute
//     threshold is lenient for a 60 px quad and strict for a 600 px one).
// With 4 points much of the corner noise is absorbed into a wrong pose
// that still reprojects well, so this gate is necessary but not
// sufficient: subpixel isoline corners are the real fix (see roadmap).
inline constexpr float kMaxCornerReprojectionPixels = 8.0F;
inline constexpr float kMaxRelativeCornerReprojection = 0.03F;
inline constexpr float kMinimumQuadRectangularity = 0.70F;

bool IsCornerPoseReliable(const Quad& quad, float reprojectionPixels);

// Symmetric orthonormalization of two noisy homography columns into a
// rotation frame: splits the angular error evenly around the bisector
// instead of Gram-Schmidt keeping the first column exact and dumping it
// all into the second. Returns false with no output written when the
// columns are degenerate (zero or parallel).
bool SymmetricOrthonormalize(
    const double first[3],
    const double second[3],
    double axisX[3],
    double axisY[3],
    double axisZ[3]);

// Largest quad in a binary instance mask (row-major, nonzero is
// foreground). Returns false with a reason when no quad survives the
// area/aspect gates: an empty answer is correct output, never a guess.
// Binary masks carry no gradients, so pixel vertices are the measurement
// (no subpixel refinement).
bool FindScreenQuadFromMask(
    const uint8_t* mask,
    int width,
    int height,
    Quad* quad,
    std::string* error);

// Pose of the W×H display rectangle from its 4 image corners. The corners
// are exactly planar, so the pose comes from the homography of the object
// plane in undistorted normalized coordinates: H = s [r1 r2 t] with K = I
// after Brown-Conrady + skew inversion, orthonormalized - the standard
// planar P4P. Reprojection RMS (pixels, through the full distort+K model)
// reports the fit; large values mean the quad is not the screen (wrong
// pairing, bent contour, or class-size aspect mismatch) - distrust the pose.
// Refuses planes facing away from the camera or seen at grazing incidence.
bool SolveQuadPose(
    const Quad& quad,
    const Intrinsics& intrinsics,
    float monitorWidthMeters,
    float monitorHeightMeters,
    Pose* pose,
    float* reprojectionErrorPixels,
    std::string* error);

}  // namespace questlab::corners
