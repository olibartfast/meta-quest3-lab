// Internal RANSAC plane fitting shared by the fusion library and the
// host-side Ocean comparison eval. Not part of any public ABI: the only
// supported entry point remains FuseDetection.
#pragma once

#include <cstdint>
#include <vector>

#include "detection_fusion/detection_fusion.h"

namespace questlab::fusion {

// Deterministic pseudo-random index source. A fixed sequence keeps fusion
// reproducible frame to frame, which matters because a box that jitters
// because of RANSAC sampling is indistinguishable from a box that jitters
// because the sensor moved.
struct SampleSequence {
    uint32_t state = 0x9E3779B9U;
    uint32_t Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

// Fits a plane by consensus and returns its unit normal and the inlier set.
inline bool FitPlaneRansac(
    const std::vector<math::Vec3>& points,
    const FusionParameters& parameters,
    math::Vec3* normal,
    std::vector<math::Vec3>* inliers) {
    if (points.size() < 3) {
        return false;
    }
    SampleSequence sequence;
    size_t bestCount = 0;
    math::Vec3 bestNormal{};
    float bestOffset = 0.0F;
    for (uint32_t iteration = 0; iteration < parameters.ransacIterations;
         ++iteration) {
        const math::Vec3& a = points[sequence.Next() % points.size()];
        const math::Vec3& b = points[sequence.Next() % points.size()];
        const math::Vec3& c = points[sequence.Next() % points.size()];
        math::Vec3 candidate =
            math::Cross(math::Subtract(b, a), math::Subtract(c, a));
        if (!math::Normalize(&candidate)) {
            continue;
        }
        const float offset = math::Dot(candidate, a);
        size_t count = 0;
        for (const math::Vec3& point : points) {
            if (std::fabs(math::Dot(candidate, point) - offset) <=
                parameters.ransacInlierMeters) {
                ++count;
            }
        }
        if (count > bestCount) {
            bestCount = count;
            bestNormal = candidate;
            bestOffset = offset;
        }
    }
    const float ratio =
        static_cast<float>(bestCount) / static_cast<float>(points.size());
    if (ratio < parameters.ransacMinimumInlierRatio) {
        return false;
    }
    inliers->clear();
    inliers->reserve(bestCount);
    for (const math::Vec3& point : points) {
        if (std::fabs(math::Dot(bestNormal, point) - bestOffset) <=
            parameters.ransacInlierMeters) {
            inliers->push_back(point);
        }
    }
    *normal = bestNormal;
    return true;
}

}  // namespace questlab::fusion
