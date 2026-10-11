#include "stereo_depth/stereo_depth.h"

#include <cmath>
#include <iostream>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
    }
    return condition;
}

// Textured synthetic pair: left is a pseudo-random gradient, right is left
// shifted by kDisparity. Flat regions would be correctly invalid, so texture
// is required for a valid match.
constexpr int32_t kWidth = 96;
constexpr int32_t kHeight = 64;
constexpr int32_t kDisparity = 12;

questlab::stereo::StereoPair MakeShiftedPair() {
    questlab::stereo::StereoPair pair;
    pair.width = kWidth;
    pair.height = kHeight;
    pair.left.resize(static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight));
    pair.right.resize(static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight));
    for (int32_t y = 0; y < kHeight; ++y) {
        for (int32_t x = 0; x < kWidth; ++x) {
            const uint8_t value = static_cast<uint8_t>(
                (x * 37 + y * 91 + (x * y) % 251) % 256);
            pair.left[static_cast<size_t>(y) * static_cast<size_t>(kWidth) + static_cast<size_t>(x)] = value;
            const int32_t sx = x + kDisparity;
            pair.right[static_cast<size_t>(y) * static_cast<size_t>(kWidth) + static_cast<size_t>(x)] =
                (sx < kWidth) ? static_cast<uint8_t>(
                    (sx * 37 + y * 91 + (sx * y) % 251) % 256)
                              : 0;
        }
    }
    return pair;
}

}  // namespace

int main() {
    bool passed = true;
    questlab::stereo::StereoParameters parameters;
    parameters.maxDisparityPixels = 16;
    parameters.windowRadius = 2;
    parameters.uniquenessMargin = 32;
    parameters.verticalTolerancePixels = 0;

    // Known-shift fixture must recover the exact disparity in the interior.
    const auto pair = MakeShiftedPair();
    std::vector<questlab::stereo::DisparityPixel> disparity;
    std::string reason;
    passed &= Expect(
        questlab::stereo::ComputeDisparity(pair, parameters, &disparity, &reason),
        "compute disparity on shifted fixture");
    int32_t valid = 0;
    int32_t exact = 0;
    for (int32_t y = 8; y < kHeight - 8; ++y) {
        for (int32_t x = 24; x < kWidth - 8; ++x) {
            const auto& pixel =
                disparity[static_cast<size_t>(y) * static_cast<size_t>(kWidth) + static_cast<size_t>(x)];
            if (pixel.valid != 0) {
                ++valid;
                if (pixel.disparity == kDisparity) {
                    ++exact;
                }
            }
        }
    }
    passed &= Expect(valid > 1000, "enough valid matches on texture");
    passed &= Expect(
        exact * 20 >= valid * 19, "interior disparity equals known shift");

    // Metric range from the fresh-device artifact values: fx=866.4, B=0.06419.
    questlab::stereo::StereoCalibration calibration;
    calibration.fx = 866.4F;
    calibration.fy = 866.4F;
    calibration.cx = 640.0F;
    calibration.cy = 480.0F;
    calibration.baselineMeters = 0.06419F;
    calibration.width = kWidth;
    calibration.height = kHeight;
    calibration.valid = true;
    std::vector<float> range;
    passed &= Expect(
        questlab::stereo::DisparityToRange(disparity, calibration, parameters, &range, &reason),
        "disparity to range");
    const float expected = 866.4F * 0.06419F / static_cast<float>(kDisparity);
    int32_t close = 0;
    int32_t checked = 0;
    for (size_t i = 0; i < disparity.size(); ++i) {
        if (disparity[i].valid != 0 && disparity[i].disparity == kDisparity) {
            ++checked;
            if (std::fabs(range[i] - expected) < 0.01F) {
                ++close;
            }
        }
    }
    passed &= Expect(checked > 0, "range checked on exact matches");
    passed &= Expect(close == checked, "range equals fx*B/d within 1 cm");

    // Flat pair must stay invalid, never hallucinate range.
    questlab::stereo::StereoPair flat = pair;
    std::fill(flat.left.begin(), flat.left.end(), 128);
    std::fill(flat.right.begin(), flat.right.end(), 128);
    std::vector<questlab::stereo::DisparityPixel> flatDisp;
    passed &= Expect(
        questlab::stereo::ComputeDisparity(flat, parameters, &flatDisp, &reason),
        "flat pair runs");
    int32_t flatValid = 0;
    for (const auto& pixel : flatDisp) {
        flatValid += (pixel.valid != 0) ? 1 : 0;
    }
    passed &= Expect(flatValid == 0, "flat texture yields zero valid");

    // Malformed inputs fail closed.
    questlab::stereo::StereoPair bad;
    std::vector<questlab::stereo::DisparityPixel> out;
    passed &= Expect(
        !questlab::stereo::ComputeDisparity(bad, parameters, &out, &reason),
        "empty pair rejected");
    questlab::stereo::StereoCalibration badCalib;
    std::vector<float> badRange;
    passed &= Expect(
        !questlab::stereo::DisparityToRange(disparity, badCalib, parameters, &badRange, &reason),
        "invalid calibration rejected");

    if (passed) {
        std::cout << "stereo_depth_tests: PASS "
                  << "(valid=" << valid << " exact=" << exact
                  << " range=" << expected << "m)\n";
        return 0;
    }
    std::cerr << "stereo_depth_tests: FAIL\n";
    return 1;
}
