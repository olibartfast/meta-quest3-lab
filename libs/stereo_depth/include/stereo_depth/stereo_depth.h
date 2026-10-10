#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace questlab::stereo {

// Per-device pinhole model from the Group 0 artifact. Distortion is the
// zero-distortion hypothesis (Meta publishes no coefficients for 50/51), so
// there is intentionally no undistort step: RectifyPair documents the vertical
// residual per match instead of hiding it.
struct StereoCalibration {
    float fx = 0.0F;
    float fy = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    float baselineMeters = 0.0F;
    int32_t width = 0;
    int32_t height = 0;
    bool valid = false;
};

struct StereoParameters {
    int32_t maxDisparityPixels = 64;
    int32_t windowRadius = 4;
    // Winner must beat runner-up by this SAD margin, else invalid. Prevents
    // flat-texture hallucinations from becoming range.
    int32_t uniquenessMargin = 64;
    // Vertical search tolerance while V-0d rectification validation is open.
    // A match off the epipolar row records its row offset; callers gate on it.
    int32_t verticalTolerancePixels = 1;
    float minimumRangeMeters = 0.25F;
    float maximumRangeMeters = 6.0F;
};

struct DisparityPixel {
    // -1 when no confident match; otherwise horizontal shift in pixels.
    int16_t disparity = -1;
    // Row offset of the winning match; rectified input keeps this at 0.
    int8_t rowOffset = 0;
    uint8_t valid = 0;
};

// Grayscale pair, row-major, one byte per pixel. Left is the RF-DETR view.
struct StereoPair {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> left;
    std::vector<uint8_t> right;
};

// SAD block-match along the horizontal epipolar line. Deterministic,
// single-threaded, no allocation beyond the output map. Returns false with a
// reason when inputs are malformed; sparse valid output is correct, not failure.
bool ComputeDisparity(
    const StereoPair& pair,
    const StereoParameters& parameters,
    std::vector<DisparityPixel>* disparity,
    std::string* reason);

// disparity → metric range via fx * baseline / d. Rejects non-positive
// disparity and out-of-range depth; invalid stays invalid, never zero-filled.
bool DisparityToRange(
    const std::vector<DisparityPixel>& disparity,
    const StereoCalibration& calibration,
    const StereoParameters& parameters,
    std::vector<float>* rangeMeters,
    std::string* reason);

bool ValidCalibration(const StereoCalibration& calibration);

}  // namespace questlab::stereo
