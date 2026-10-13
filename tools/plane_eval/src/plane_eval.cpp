// Ocean spike: our RANSAC plane normal vs ours + Ocean robust refinement.
//
// Asks "algorithm or data?": identical noisy point sets go through
// (A) questlab FitPlaneRansac (shared verbatim via ransac_plane.h) and
// (B) A seeded into Ocean::Geometry::NonLinearOptimizationPlane::optimizePlane
// (Huber, 20 iters). Angular error vs the known ground-truth normal is
// reported per tilt / noise / clutter cell.
// Verdict rule: refinement gains < 1 deg everywhere  =>  data-dominated,
// the solver is not the fix (correspondences/sync are). Gains >> 1 deg
// consistently  =>  adopt Ocean refinement behind quest_vision.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "detection_fusion/detection_fusion.h"
#include "ransac_plane.h"

#include "ocean/base/Accessor.h"
#include "ocean/geometry/NonLinearOptimizationPlane.h"
#include "ocean/math/Plane3.h"
#include "ocean/math/Vector3.h"

namespace {

uint32_t rngState = 0x12345678U;
uint32_t NextRand() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}
// Uniform in [0, 1).
float UnitRand() {
    return static_cast<float>(NextRand()) / static_cast<float>(0xFFFFFFFFU);
}
// Approx standard normal via summed uniforms.
float GaussRand() {
    return (UnitRand() + UnitRand() + UnitRand() - 1.5F) * 2.0F;
}

float NormalErrorDeg(
    const questlab::math::Vec3& measured,
    const questlab::math::Vec3& truth) {
    const float dot = std::clamp(
        measured.x * truth.x + measured.y * truth.y + measured.z * truth.z,
        -1.0F, 1.0F);
    // Sign-agnostic: a flipped plane normal is the same surface.
    return std::acos(std::fabs(dot)) * 180.0F /
        static_cast<float>(M_PI);
}

struct CellResult {
    float tiltDeg = 0.0F;
    float sigmaMeters = 0.0F;
    float clutterShare = 0.0F;
    float oursDeg = -1.0F;
    float refinedDeg = -1.0F;
    bool oursOk = false;
    bool refinedOk = false;
};

CellResult RunCell(float tiltDeg, float sigma, float clutterShare) {
    CellResult result{tiltDeg, sigma, clutterShare, -1.0F, -1.0F, false,
                      false};
    const float tilt = tiltDeg * static_cast<float>(M_PI) / 180.0F;
    // Ground-truth plane: pitched about X at 2 m, normal away from viewer.
    const questlab::math::Vec3 truth{
        0.0F, std::sin(tilt), -std::cos(tilt)};
    const questlab::math::Vec3 center{0.0F, 0.0F, -2.0F};
    const questlab::math::Vec3 u{1.0F, 0.0F, 0.0F};
    questlab::math::Vec3 v{
        truth.y * u.z - truth.z * u.y,
        truth.z * u.x - truth.x * u.z,
        truth.x * u.y - truth.y * u.x,
    };
    {
        const float length =
            std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        v = {v.x / length, v.y / length, v.z / length};
    }
    std::vector<questlab::math::Vec3> points;
    for (int ix = -8; ix <= 8; ++ix) {
        for (int iy = -8; iy <= 8; ++iy) {
            const float fx = static_cast<float>(ix) / 8.0F;
            const float fy = static_cast<float>(iy) / 8.0F;
            questlab::math::Vec3 point{
                center.x + u.x * fx * 0.15F + v.x * fy * 0.15F,
                center.y + u.y * fx * 0.15F + v.y * fy * 0.15F,
                center.z + u.z * fx * 0.15F + v.z * fy * 0.15F,
            };
            const float noise = GaussRand() * sigma;
            point = {point.x + truth.x * noise, point.y + truth.y * noise,
                     point.z + truth.z * noise};
            points.push_back(point);
        }
    }
    // Clutter: uniform background points in a 1 m box behind the slab.
    const size_t clutterCount = static_cast<size_t>(
        static_cast<float>(points.size()) * clutterShare);
    for (size_t i = 0; i < clutterCount; ++i) {
        points.push_back({
            (UnitRand() - 0.5F) * 1.0F,
            (UnitRand() - 0.5F) * 1.0F,
            -2.0F - UnitRand() * 1.0F,
        });
    }

    questlab::fusion::FusionParameters parameters;
    questlab::math::Vec3 normal{};
    std::vector<questlab::math::Vec3> inliers;
    if (!questlab::fusion::FitPlaneRansac(
            points, parameters, &normal, &inliers)) {
        return result;
    }
    if (normal.x * truth.x + normal.y * truth.y + normal.z * truth.z < 0.0F) {
        normal = {-normal.x, -normal.y, -normal.z};
    }
    result.oursOk = true;
    result.oursDeg = NormalErrorDeg(normal, truth);

    // Ocean refinement seeded with our plane over the same points.
    Ocean::Vector3 seedPoint(inliers[0].x, inliers[0].y, inliers[0].z);
    Ocean::Vector3 seedNormal(normal.x, normal.y, normal.z);
    Ocean::Plane3 seedPlane(seedPoint, seedNormal);
    std::vector<Ocean::Vector3> oceanPoints;
    oceanPoints.reserve(points.size());
    for (const auto& point : points) {
        oceanPoints.emplace_back(point.x, point.y, point.z);
    }
    Ocean::ConstArrayAccessor<Ocean::Vector3> accessor(
        std::move(oceanPoints));
    Ocean::Plane3 refined = seedPlane;
    Ocean::Scalar finalError = 0;
    if (!Ocean::Geometry::NonLinearOptimizationPlane::optimizePlane(
            seedPlane, accessor, refined, 20u,
            Ocean::Geometry::Estimator::ET_HUBER, 0.001, 5.0, nullptr, &finalError)) {
        return result;
    }
    const Ocean::Vector3& refinedNormal = refined.normal();
    questlab::math::Vec3 refinedVec{
        static_cast<float>(refinedNormal.x()),
        static_cast<float>(refinedNormal.y()),
        static_cast<float>(refinedNormal.z()),
    };
    if (refinedVec.x * truth.x + refinedVec.y * truth.y +
            refinedVec.z * truth.z <
        0.0F) {
        refinedVec = {-refinedVec.x, -refinedVec.y, -refinedVec.z};
    }
    result.refinedOk = true;
    result.refinedDeg = NormalErrorDeg(refinedVec, truth);
    return result;
}

}  // namespace

int main() {
    std::printf(
        "%-8s %-8s %-8s %-10s %-10s %-8s\n", "tilt", "sigma", "clutter",
        "ours_deg", "ocean_deg", "gain");
    float worstGain = 0.0F;
    float worstLoss = 0.0F;
    float worstOurs = 0.0F;
    bool anyCompared = false;
    for (float tilt : {0.0F, 15.0F, 30.0F, 45.0F}) {
        for (float sigma : {0.01F, 0.05F, 0.10F, 0.20F}) {
            for (float clutter : {0.0F, 0.30F}) {
                const CellResult cell = RunCell(tilt, sigma, clutter);
                const float gain = cell.refinedOk && cell.oursOk
                    ? cell.oursDeg - cell.refinedDeg
                    : 0.0F;
                if (cell.oursOk && cell.oursDeg > worstOurs) {
                    worstOurs = cell.oursDeg;
                }
                if (cell.refinedOk && cell.oursOk) {
                    anyCompared = true;
                    if (gain > worstGain) {
                        worstGain = gain;
                    }
                    if (-gain > worstLoss) {
                        worstLoss = -gain;
                    }
                }
                std::printf(
                    "%-8.1f %-8.2f %-8.2f %-10s %-10s %-8s\n", tilt, sigma,
                    clutter,
                    cell.oursOk
                        ? (char*)(std::to_string(cell.oursDeg).c_str())
                        : "REFUSED",
                    cell.refinedOk
                        ? (char*)(std::to_string(cell.refinedDeg).c_str())
                        : "FAILED",
                    cell.refinedOk && cell.oursOk
                        ? (char*)(std::to_string(gain).c_str())
                        : "-");
            }
        }
    }
    std::printf(
        "worst ours error: %.2f deg, best refinement gain: %.2f deg, "
        "worst refinement loss: %.2f deg\n",
        worstOurs, worstGain, worstLoss);
    // Adopt only on consistent wins: Huber refinement over all cluster
    // points lets clutter drag the plane that RANSAC had excluded.
    std::printf(
        "%s\n",
        anyCompared && worstLoss < 1.0F && worstGain > 1.0F
            ? "VERDICT: refinement helps consistently; adopt Ocean behind "
              "quest_vision."
            : "VERDICT: data-dominated (clutter losses and/or refusal wall); "
              "the solver is not the fix.");
    return 0;
}
