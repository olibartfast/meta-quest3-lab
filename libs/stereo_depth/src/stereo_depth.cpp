#include "stereo_depth/stereo_depth.h"

#include <cmath>
#include <limits>

namespace questlab::stereo {

bool ValidCalibration(const StereoCalibration& calibration) {
    return calibration.valid && calibration.fx > 0.0F && calibration.fy > 0.0F &&
           calibration.baselineMeters >= 0.02F &&
           calibration.baselineMeters <= 0.20F && calibration.width > 0 &&
           calibration.height > 0;
}

namespace {

int32_t BlockSad(
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int32_t width,
    int32_t height,
    int32_t x,
    int32_t y,
    int32_t disparity,
    int32_t rowOffset,
    int32_t radius) {
    int32_t sad = 0;
    for (int32_t dy = -radius; dy <= radius; ++dy) {
        const int32_t ly = y + dy;
        const int32_t ry = y + dy + rowOffset;
        if (ly < 0 || ly >= height || ry < 0 || ry >= height) {
            return std::numeric_limits<int32_t>::max();
        }
        for (int32_t dx = -radius; dx <= radius; ++dx) {
            const int32_t lx = x + dx;
            const int32_t rx = x + dx - disparity;
            if (lx < 0 || lx >= width || rx < 0 || rx >= width) {
                return std::numeric_limits<int32_t>::max();
            }
            const int32_t diff = static_cast<int32_t>(left[static_cast<size_t>(ly) * static_cast<size_t>(width) + static_cast<size_t>(lx)]) -
                                 static_cast<int32_t>(right[static_cast<size_t>(ry) * static_cast<size_t>(width) + static_cast<size_t>(rx)]);
            sad += diff < 0 ? -diff : diff;
        }
    }
    return sad;
}

}  // namespace

bool ComputeDisparity(
    const StereoPair& pair,
    const StereoParameters& parameters,
    std::vector<DisparityPixel>* disparity,
    std::string* reason) {
    if (disparity == nullptr) {
        if (reason != nullptr) {
            *reason = "null output";
        }
        return false;
    }
    if (pair.width <= 0 || pair.height <= 0 ||
        pair.left.size() != static_cast<size_t>(pair.width) * static_cast<size_t>(pair.height) ||
        pair.right.size() != static_cast<size_t>(pair.width) * static_cast<size_t>(pair.height)) {
        if (reason != nullptr) {
            *reason = "malformed pair dimensions";
        }
        return false;
    }
    if (parameters.maxDisparityPixels <= 0 || parameters.windowRadius < 0) {
        if (reason != nullptr) {
            *reason = "malformed parameters";
        }
        return false;
    }
    disparity->assign(
        static_cast<size_t>(pair.width) * static_cast<size_t>(pair.height),
        DisparityPixel{});
    const int32_t radius = parameters.windowRadius;
    for (int32_t y = radius; y < pair.height - radius; ++y) {
        for (int32_t x = parameters.maxDisparityPixels + radius;
             x < pair.width - radius;
             ++x) {
            int32_t bestSad = std::numeric_limits<int32_t>::max();
            int32_t runnerSad = std::numeric_limits<int32_t>::max();
            int32_t bestD = -1;
            int8_t bestRow = 0;
            for (int32_t d = 0; d <= parameters.maxDisparityPixels; ++d) {
                for (int32_t ro = -parameters.verticalTolerancePixels;
                     ro <= parameters.verticalTolerancePixels;
                     ++ro) {
                    const int32_t sad = BlockSad(
                        pair.left, pair.right, pair.width, pair.height, x, y, d,
                        ro, radius);
                    if (sad < bestSad) {
                        runnerSad = bestSad;
                        bestSad = sad;
                        bestD = d;
                        bestRow = static_cast<int8_t>(ro);
                    } else if (sad < runnerSad) {
                        runnerSad = sad;
                    }
                }
            }
            DisparityPixel pixel;
            if (bestD > 0 && bestSad < std::numeric_limits<int32_t>::max() &&
                runnerSad - bestSad >= parameters.uniquenessMargin) {
                pixel.disparity = static_cast<int16_t>(bestD);
                pixel.rowOffset = bestRow;
                pixel.valid = 1;
            }
            (*disparity)[static_cast<size_t>(y) * static_cast<size_t>(pair.width) + static_cast<size_t>(x)] = pixel;
        }
    }
    return true;
}

bool DisparityToRange(
    const std::vector<DisparityPixel>& disparity,
    const StereoCalibration& calibration,
    const StereoParameters& parameters,
    std::vector<float>* rangeMeters,
    std::string* reason) {
    if (rangeMeters == nullptr) {
        if (reason != nullptr) {
            *reason = "null output";
        }
        return false;
    }
    if (!ValidCalibration(calibration)) {
        if (reason != nullptr) {
            *reason = "invalid calibration";
        }
        return false;
    }
    if (disparity.size() !=
        static_cast<size_t>(calibration.width) * static_cast<size_t>(calibration.height)) {
        if (reason != nullptr) {
            *reason = "disparity/calibration size mismatch";
        }
        return false;
    }
    rangeMeters->assign(disparity.size(), -1.0F);
    const float fxB = calibration.fx * calibration.baselineMeters;
    for (size_t i = 0; i < disparity.size(); ++i) {
        if (disparity[i].valid == 0 || disparity[i].disparity <= 0) {
            continue;
        }
        const float range =
            fxB / static_cast<float>(disparity[i].disparity);
        if (!std::isfinite(range) || range < parameters.minimumRangeMeters ||
            range > parameters.maximumRangeMeters) {
            continue;
        }
        (*rangeMeters)[i] = range;
    }
    return true;
}

}  // namespace questlab::stereo
