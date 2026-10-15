#pragma once

// Metric 6-DoF pose (camera-from-object) from 2D<->3D correspondences.
// Recovers the object frame pose in the camera frame from object points in
// metres paired with image points in pixels (Camera2 basis: +X right, +Y down,
// +Z forward), robust to a majority of outliers via RANSAC, deterministic
// (fixed default seed, no hidden global state), and fail-closed: an empty
// answer is correct output, never a guess. The returned pose plus reprojection
// RMS is a measurement, not an extrapolated range.

#include <cstdint>
#include <string>
#include <vector>

#include "xr_math/xr_math.h"

namespace questlab::pose {

struct Intrinsics {
    float fx = 0.0F;   // focal length, pixels
    float fy = 0.0F;
    float cx = 0.0F;   // principal point, pixels
    float cy = 0.0F;
};

struct Correspondence {
    math::Vec3 objectPoint{};  // metres, object frame
    math::Vec2 pixel{};        // pixels, Camera2 basis
};

struct Options {
    float reprojectionThresholdPx = 3.0F;   // RANSAC inlier threshold
    int maxIterations = 1000;               // cap (adaptive early-exit applies)
    uint32_t seed = 0x9E3779B9U;            // fixed default
    int minInliers = 6;                     // minimum consensus
    bool outputInlierMask = false;          // opt-in to N-byte mask
};

struct Result {
    math::Pose pose{};                    // camera-from-object
    int inlierCount = 0;
    std::vector<uint8_t> inlierMask{};    // empty unless outputInlierMask
    float rmsReprojectionPixels = 0.0F;   // RMS over inliers
};

bool SolvePose(
    const Correspondence* correspondences,
    int count,
    const Intrinsics& intrinsics,
    const Options& options,
    Result* result,
    std::string* error);

}  // namespace questlab::pose
