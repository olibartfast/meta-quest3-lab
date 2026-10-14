#include "detection_fusion/detection_fusion.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "ransac_plane.h"

#if defined(QUESTLAB_HAS_OCEAN)
#include "ocean/base/Accessor.h"
#include "ocean/geometry/NonLinearOptimizationPlane.h"
#include "ocean/math/Plane3.h"
#include "ocean/math/Vector3.h"
#endif

namespace questlab::fusion {
namespace {

// Inward normals of the four side planes of the pyramid. Each is built from
// two adjacent corner rays, so the test needs no projection back into image
// space and works entirely in LOCAL.
bool BuildInwardNormals(
    const DetectionFrustum& frustum,
    float borderInsetFraction,
    std::array<math::Vec3, 4>* normals) {
    std::array<math::Vec3, 4> corners = frustum.cornerDirectionsInLocal;
    const float inset = std::clamp(borderInsetFraction, 0.0F, 0.45F);
    if (inset > 0.0F) {
        // Shrink each corner ray toward the centre bearing.
        for (math::Vec3& corner : corners) {
            corner = math::Add(
                math::Scale(corner, 1.0F - inset),
                math::Scale(frustum.centerDirectionInLocal, inset));
            if (!math::Normalize(&corner)) {
                return false;
            }
        }
    }

    for (size_t index = 0; index < corners.size(); ++index) {
        const math::Vec3& current = corners[index];
        const math::Vec3& next = corners[(index + 1) % corners.size()];
        math::Vec3 normal = math::Cross(current, next);
        if (!math::Normalize(&normal)) {
            return false;
        }
        // Orient the normal so the centre bearing is on its positive side.
        if (math::Dot(normal, frustum.centerDirectionInLocal) < 0.0F) {
            normal = math::Scale(normal, -1.0F);
        }
        (*normals)[index] = normal;
    }
    return true;
}

// Plane consensus fitting lives in ransac_plane.h, shared verbatim with
// the host-side Ocean comparison eval (tools/plane_eval).

#if defined(QUESTLAB_HAS_OCEAN)
// Robust refinement of a RANSAC seed plane with Ocean's Levenberg-Marquardt
// Huber fit over the same points. Replaces the normal and recomputes the
// inlier set when the refined plane is sane (within 30 degrees of the
// seed) and keeps consensus; otherwise the seed stands.
bool RefinePlaneOcean(
    const std::vector<math::Vec3>& points,
    const FusionParameters& parameters,
    math::Vec3* normal,
    std::vector<math::Vec3>* inliers) {
    math::Vec3 centroid{};
    for (const math::Vec3& point : *inliers) {
        centroid = math::Add(centroid, point);
    }
    centroid = math::Scale(
        centroid, 1.0F / static_cast<float>(inliers->size()));
    const Ocean::Vector3 seedPoint(centroid.x, centroid.y, centroid.z);
    const Ocean::Vector3 seedNormal(normal->x, normal->y, normal->z);
    const Ocean::Plane3 seedPlane(seedPoint, seedNormal);
    std::vector<Ocean::Vector3> oceanPoints;
    oceanPoints.reserve(points.size());
    for (const math::Vec3& point : points) {
        oceanPoints.emplace_back(point.x, point.y, point.z);
    }
    const Ocean::ConstArrayAccessor<Ocean::Vector3> accessor(oceanPoints);
    Ocean::Plane3 refined = seedPlane;
    if (!Ocean::Geometry::NonLinearOptimizationPlane::optimizePlane(
            seedPlane, accessor, refined, 20u, Ocean::Geometry::Estimator::ET_HUBER)) {
        return true;  // Refinement unavailable; seed stands, still success.
    }
    const Ocean::Vector3& refinedNormal = refined.normal();
    math::Vec3 candidate{
        static_cast<float>(refinedNormal.x()),
        static_cast<float>(refinedNormal.y()),
        static_cast<float>(refinedNormal.z()),
    };
    if (!math::Normalize(&candidate)) {
        return true;
    }
    if (std::fabs(math::Dot(candidate, *normal)) < 0.866F) {
        return true;  // Diverged from the seed; do not trust it.
    }
    const float offset = math::Dot(candidate, centroid);
    std::vector<math::Vec3> refinedInliers;
    refinedInliers.reserve(inliers->size());
    for (const math::Vec3& point : points) {
        if (std::fabs(math::Dot(candidate, point) - offset) <=
            parameters.ransacInlierMeters) {
            refinedInliers.push_back(point);
        }
    }
    const float ratio =
        static_cast<float>(refinedInliers.size()) /
        static_cast<float>(points.size());
    if (refinedInliers.size() < parameters.minimumSampleCount ||
        ratio < parameters.ransacMinimumInlierRatio) {
        return true;  // Lost consensus; seed stands, still success.
    }
    *normal = candidate;
    *inliers = std::move(refinedInliers);
    return true;
}
#endif

float Percentile(std::vector<float>* values, float fraction) {
    if (values->empty()) {
        return 0.0F;
    }
    const float clamped = std::clamp(fraction, 0.0F, 1.0F);
    const size_t index = static_cast<size_t>(std::llround(
        clamped * static_cast<float>(values->size() - 1)));
    std::nth_element(values->begin(), values->begin() + index, values->end());
    return (*values)[index];
}

// In-plane X axis for full 6D orientation: the LOCAL X axis projected onto
// the fitted surface, with a deterministic cascade when the surface is
// edge-on to X. Used only for near-horizontal surfaces, where the in-plane
// direction is arbitrary and the axis parallel to the surface is well
// conditioned by construction. Vertical surfaces take the world-up rule
// below instead: this cascade flips reference when the projection crosses
// the threshold, and a screen almost parallel to LOCAL X sits on that
// threshold, which rotated the box by ~125 degrees whenever the fitted
// normal jittered across it.
bool StableInPlaneAxis(
    const math::Vec3& normal,
    math::Vec3* axis) {
    const math::Vec3 references[3] = {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
    for (const math::Vec3& reference : references) {
        math::Vec3 projected = math::Subtract(
            reference,
            math::Scale(normal, math::Dot(reference, normal)));
        // Edge-on when the residual is under ~11 degrees of the reference.
        if (math::Length(projected) > 0.2F && math::Normalize(&projected)) {
            *axis = projected;
            return true;
        }
    }
    return false;
}

}  // namespace

bool PointInsideFrustum(
    const DetectionFrustum& frustum,
    const math::Vec3& pointInLocal,
    float borderInsetFraction) {
    std::array<math::Vec3, 4> normals{};
    if (!BuildInwardNormals(frustum, borderInsetFraction, &normals)) {
        return false;
    }
    const math::Vec3 offset =
        math::Subtract(pointInLocal, frustum.originInLocal);
    // Behind the camera is never inside.
    if (math::Dot(offset, frustum.centerDirectionInLocal) <= 0.0F) {
        return false;
    }
    for (const math::Vec3& normal : normals) {
        if (math::Dot(offset, normal) < 0.0F) {
            return false;
        }
    }
    return true;
}

bool FuseDetection(
    const DetectionFrustum& frustum,
    const std::vector<math::Vec3>& depthPointsInLocal,
    const FusionParameters& parameters,
    const ObjectSizePrior& sizePrior,
    FusedBox* box,
    std::string* reason) {
    const auto fail = [&](const char* message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };
    if (box == nullptr) {
        return fail("Output box is null");
    }
    // FuseDetection is range-agnostic: success and failure both leave Prior (hide sentinel) unless the caller overwrites after CheckAgreement.
    box->rangeSource = RangeSource::Prior;
    if (parameters.clusterToleranceMeters <= 0.0F ||
        parameters.maximumRangeMeters <= parameters.minimumRangeMeters) {
        return fail("Fusion parameters are invalid");
    }

    std::array<math::Vec3, 4> normals{};
    if (!BuildInwardNormals(
            frustum, parameters.borderInsetFraction, &normals)) {
        return fail("Detection frustum is degenerate");
    }

    // Pass one: keep points inside the frustum and within the working range,
    // recording the range along the centre bearing for clustering.
    std::vector<math::Vec3> inside;
    std::vector<float> ranges;
    inside.reserve(depthPointsInLocal.size());
    ranges.reserve(depthPointsInLocal.size());
    for (const math::Vec3& point : depthPointsInLocal) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            continue;
        }
        const math::Vec3 offset =
            math::Subtract(point, frustum.originInLocal);
        const float axial = math::Dot(offset, frustum.centerDirectionInLocal);
        if (axial < parameters.minimumRangeMeters ||
            axial > parameters.maximumRangeMeters) {
            continue;
        }
        bool insideAll = true;
        for (const math::Vec3& normal : normals) {
            if (math::Dot(offset, normal) < 0.0F) {
                insideAll = false;
                break;
            }
        }
        if (!insideAll) {
            continue;
        }
        inside.push_back(point);
        ranges.push_back(axial);
    }

    if (inside.size() < parameters.minimumSampleCount) {
        return fail("Too few depth samples inside the detection");
    }

    // Pass two: find the nearest surface with enough support, not the most
    // populated one. A wall behind the object usually yields more samples
    // than the object itself, so selecting the largest cluster would place
    // the box on the wall. What matters is occlusion order: inside a
    // detection's frustum, the first substantial surface is the object.
    std::vector<float> sortedRanges = ranges;
    std::sort(sortedRanges.begin(), sortedRanges.end());
    const float window = parameters.clusterToleranceMeters * 2.0F;
    const size_t supportThreshold = std::max<size_t>(
        parameters.minimumSampleCount,
        static_cast<size_t>(std::llround(
            static_cast<double>(parameters.foregroundSampleShare) *
            static_cast<double>(sortedRanges.size()))));

    bool haveCluster = false;
    float clusterCentre = 0.0F;
    size_t high = 0;
    for (size_t low = 0; low < sortedRanges.size(); ++low) {
        if (high < low) {
            high = low;
        }
        while (high + 1 < sortedRanges.size() &&
               sortedRanges[high + 1] - sortedRanges[low] <= window) {
            ++high;
        }
        const size_t count = high - low + 1;
        if (count >= supportThreshold) {
            // Median of the window is more stable than its midpoint when the
            // surface is not flat.
            clusterCentre = sortedRanges[low + count / 2];
            haveCluster = true;
            break;
        }
    }
    if (!haveCluster) {
        return fail("No depth surface with enough support");
    }

    // Pass three: keep the cluster's inliers.
    std::vector<math::Vec3> cluster;  // NOLINT: replaced by plane inliers
    cluster.reserve(inside.size());
    for (size_t index = 0; index < inside.size(); ++index) {
        if (std::fabs(ranges[index] - clusterCentre) <=
            parameters.clusterToleranceMeters) {
            cluster.push_back(inside[index]);
        }
    }
    const float inlierRatio = static_cast<float>(cluster.size()) /
                              static_cast<float>(inside.size());
    if (cluster.size() < parameters.minimumSampleCount) {
        return fail("Dominant cluster is too small");
    }
    // Intentionally no minimumInlierRatio gate here: pass two already
    // requires the nearest window to hold supportThreshold samples
    // (max(minimumSampleCount, foregroundSampleShare * N)), so an extra
    // share-of-frustum floor duplicates that check and contradicts the
    // occlusion-order rule (nearest substantial surface wins). A small near
    // object in front of a dense wall is legitimately a small fraction of
    // the frustum yet must still succeed; scattered noise cannot pass
    // supportThreshold because it does not cluster within
    // clusterToleranceMeters.
    (void)parameters.minimumInlierRatio;

    // Full 6D orientation from the measured surface. The RANSAC plane
    // normal supplies the out-of-plane rotation; PCA over the in-plane
    // residuals supplies the remaining in-plane rotation. Pitch and roll
    // are preserved, so a tilted surface yields a tilted box. The old
    // yaw-only convention (flatten onto LOCAL horizontal, boxes stay level
    // with the room) is kept only as the degenerate fallback when the
    // cluster has no measurable in-plane extent.
    math::Vec3 up = parameters.upInLocal;
    if (!math::Normalize(&up)) {
        return fail("Up axis is degenerate");
    }
    // Prefer the plane normal: it is a measured orientation. Fall back to the
    // viewing bearing only when no plane reaches consensus.
    math::Vec3 forward = frustum.centerDirectionInLocal;
    math::Vec3 planeNormal{};
    std::vector<math::Vec3> planeInliers;
    bool orientationMeasured = false;
    if (FitPlaneRansac(cluster, parameters, &planeNormal, &planeInliers)) {
        // Point the normal away from the viewer so the box grows backwards.
        if (math::Dot(planeNormal, frustum.centerDirectionInLocal) < 0.0F) {
            planeNormal = math::Scale(planeNormal, -1.0F);
        }
#if defined(QUESTLAB_HAS_OCEAN)
        if (parameters.useOceanRefinement) {
            // Seed-oriented refinement; keeps consensus guarantees by
            // construction (falls back to the seed silently).
            RefinePlaneOcean(cluster, parameters, &planeNormal, &planeInliers);
            if (math::Dot(planeNormal, frustum.centerDirectionInLocal) <
                0.0F) {
                planeNormal = math::Scale(planeNormal, -1.0F);
            }
        }
#endif
        forward = planeNormal;
        orientationMeasured = true;
        cluster = planeInliers;
    }
    if (parameters.requireMeasuredOrientation && !orientationMeasured) {
        return fail("No planar surface reached consensus");
    }
    // Box frame, right-handed: X x Y = Z with Z pointing back toward the
    // viewer (-forward). The in-plane orientation is decided once, here:
    // - A measured surface that is not near-horizontal inherits world up as
    //   its own up (projected onto it). For a monitor, a wall or a tilted
    //   laptop screen this is the only convention with a meaning, and it is
    //   continuous: a normal wobbling by a few degrees moves the box by a few
    //   degrees. Picking a world axis by a threshold instead makes the box
    //   snap by up to 90 degrees the moment the normal crosses the threshold,
    //   which was observed on device: a screen almost parallel to LOCAL X
    //   alternated between the X and Y projections of the old cascade,
    //   rotating the box rigidly against head motion.
    // - A near-horizontal surface keeps the world-axis projection rule: its
    //   in-plane direction is arbitrary, and the axis parallel to the surface
    //   is well conditioned there by construction.
    // - An unmeasured bearing keeps the old yaw-only convention derived from
    //   the viewing geometry.
    math::Vec3 back = math::Scale(forward, -1.0F);
    math::Vec3 right{};
    math::Vec3 boxUp{};
    bool frameOk = false;
    if (orientationMeasured) {
        math::Vec3 projectedUp = math::Subtract(
            up, math::Scale(forward, math::Dot(up, forward)));
        if (math::Length(projectedUp) > 0.2F && math::Normalize(&projectedUp)) {
            boxUp = projectedUp;
            right = math::Cross(boxUp, back);
            frameOk = true;
        } else {
            math::Vec3 inPlane{};
            if (StableInPlaneAxis(forward, &inPlane)) {
                right = inPlane;
                boxUp = math::Cross(back, right);
                frameOk = true;
            }
        }
    }
    if (!frameOk) {
        // Degenerate in-plane extent (or unmeasured bearing fallback):
        // derive X from the viewing geometry as before.
        math::Vec3 flatForward = math::Subtract(
            forward, math::Scale(up, math::Dot(forward, up)));
        if (!math::Normalize(&flatForward)) {
            return fail("Bearing is parallel to the up axis");
        }
        forward = flatForward;
        back = math::Scale(forward, -1.0F);
        right = math::Cross(flatForward, up);
        boxUp = math::Cross(back, right);
    }
    if (!math::Normalize(&right) || !math::Normalize(&boxUp)) {
        return fail("Cannot build a box basis");
    }
    // Re-orthogonalize X against the completed frame for exactness.
    right = math::Cross(boxUp, back);
    if (!math::Normalize(&right)) {
        return fail("Cannot build a box basis");
    }

    // Extents in the box basis, from percentiles. boxUp is the measured
    // surface-up, not LOCAL up: a tilted surface yields tilted extents.
    std::vector<float> alongRight;
    std::vector<float> alongUp;
    std::vector<float> alongForward;
    alongRight.reserve(cluster.size());
    alongUp.reserve(cluster.size());
    alongForward.reserve(cluster.size());
    for (const math::Vec3& point : cluster) {
        alongRight.push_back(math::Dot(point, right));
        alongUp.push_back(math::Dot(point, boxUp));
        alongForward.push_back(math::Dot(point, forward));
    }

    const float rightLow =
        Percentile(&alongRight, parameters.extentLowPercentile);
    const float rightHigh =
        Percentile(&alongRight, parameters.extentHighPercentile);
    const float upLow = Percentile(&alongUp, parameters.extentLowPercentile);
    const float upHigh = Percentile(&alongUp, parameters.extentHighPercentile);
    const float forwardLow =
        Percentile(&alongForward, parameters.extentLowPercentile);
    const float forwardHigh =
        Percentile(&alongForward, parameters.extentHighPercentile);

    const float halfRight = std::max(0.01F, (rightHigh - rightLow) * 0.5F);
    const float halfUp = std::max(0.01F, (upHigh - upLow) * 0.5F);
    // Depth from one viewpoint only sees the near surface, so the measured
    // forward spread is a lower bound on the object's true depth. It is
    // reported as measured rather than padded to look like a full cuboid.
    const float halfForward = std::max(0.01F, (forwardHigh - forwardLow) * 0.5F);

    // With a prior, keep the measured near face where depth put it and grow
    // the box away from the viewer. Centring the prior on the visible surface
    // instead would push half the cuboid through the object's front face.
    float halfDepth = halfForward;
    float forwardCentre = (forwardLow + forwardHigh) * 0.5F;
    bool depthAssumed = false;
    if (sizePrior.valid && sizePrior.depthMeters > 0.0F) {
        halfDepth = sizePrior.depthMeters * 0.5F;
        // forward points away from the viewer, so the near face is the
        // smaller coordinate along it.
        forwardCentre = forwardLow + halfDepth;
        depthAssumed = true;
    }

    const math::Vec3 centre = math::Add(
        math::Add(
            math::Scale(right, (rightLow + rightHigh) * 0.5F),
            math::Scale(boxUp, (upLow + upHigh) * 0.5F)),
        math::Scale(forward, forwardCentre));

    // Full 6D orientation from the measured basis. Local +X maps to right,
    // +Y to boxUp, +Z back toward the viewer, preserving pitch and roll.
    math::Quat orientation{};
    if (!math::QuatFromBasis(right, boxUp, back, &orientation)) {
        return fail("Cannot orient the box");
    }

    box->centerInLocal = centre;
    box->halfExtents = {halfRight, halfUp, halfDepth};
    box->depthExtentIsAssumed = depthAssumed;
    box->orientationIsMeasured = orientationMeasured;
    box->rangeSource = RangeSource::Prior;
    box->planeInlierRatio = orientationMeasured
        ? static_cast<float>(cluster.size()) /
              static_cast<float>(std::max<size_t>(1U, inside.size()))
        : 0.0F;
    box->orientationInLocal = orientation;
    box->representativeRangeMeters = clusterCentre;
    box->sampleCount = static_cast<uint32_t>(cluster.size());
    box->inlierRatio = inlierRatio;
    box->confidence = std::clamp(
        inlierRatio * std::min(
            1.0F,
            static_cast<float>(cluster.size()) /
                static_cast<float>(parameters.minimumSampleCount * 4U)),
        0.0F,
        1.0F);
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

AgreementVerdict CheckAgreement(
    bool stereoValid,
    float stereoRangeMeters,
    bool envValid,
    float envRangeMeters,
    float toleranceMeters,
    float toleranceRatio,
    RangeSource* selectedSource) {
    const auto select = [&](RangeSource source) {
        if (selectedSource != nullptr) {
            *selectedSource = source;
        }
    };
    if (stereoValid && std::isfinite(stereoRangeMeters) && stereoRangeMeters > 0.0F) {
        if (envValid && std::isfinite(envRangeMeters) && envRangeMeters > 0.0F) {
            const float delta = std::fabs(stereoRangeMeters - envRangeMeters);
            const float allowed = toleranceMeters +
                                  toleranceRatio * std::min(stereoRangeMeters, envRangeMeters);
            if (delta <= allowed) {
                select(RangeSource::Stereo);
                return AgreementVerdict::AgreeStereo;
            }
            select(RangeSource::Stereo);
            return AgreementVerdict::Hide;
        }
        select(RangeSource::Stereo);
        return AgreementVerdict::AgreeStereo;
    }
    if (envValid && std::isfinite(envRangeMeters) && envRangeMeters > 0.0F) {
        select(RangeSource::EnvDepthFallback);
        return AgreementVerdict::FallbackEnv;
    }
    select(RangeSource::Prior);
    return AgreementVerdict::Hide;
}

namespace {

bool FinitePositiveRange(float rangeMeters) {
    return std::isfinite(rangeMeters) && rangeMeters > 0.0F;
}

}  // namespace

AgreementVerdict CheckLearnedAgreement(
    bool learnedValid,
    float learnedRangeMeters,
    bool envValid,
    float envRangeMeters,
    float toleranceMeters,
    float toleranceRatio,
    RangeSource* selectedSource) {
    const auto select = [&](RangeSource source) {
        if (selectedSource != nullptr) {
            *selectedSource = source;
        }
    };
    const bool learnedOk =
        learnedValid && FinitePositiveRange(learnedRangeMeters);
    const bool envOk = envValid && FinitePositiveRange(envRangeMeters);
    if (learnedOk && envOk) {
        // Env is kept; Learned stays a comparison source for the G-3 gate.
        const float delta = std::fabs(learnedRangeMeters - envRangeMeters);
        const float allowed = toleranceMeters +
                              toleranceRatio *
                                  std::min(learnedRangeMeters, envRangeMeters);
        if (delta <= allowed) {
            select(RangeSource::EnvDepthFallback);
            return AgreementVerdict::FallbackEnv;
        }
        select(RangeSource::Prior);
        return AgreementVerdict::Hide;
    }
    if (!envOk && learnedOk) {
        select(RangeSource::Learned);
        return AgreementVerdict::FallbackLearned;
    }
    select(RangeSource::Prior);
    return AgreementVerdict::Hide;
}

bool AlignMonoDisparity(
    const std::vector<MonoAlignSample>& samples,
    int ransacIterations,
    float inlierDisparityTolerance,
    float minimumInlierRatio,
    MonoAlignResult* result,
    std::string* reason) {
    const auto fail = [&](const char* message) {
        if (result != nullptr) {
            *result = MonoAlignResult{};
        }
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };
    if (result == nullptr) {
        return fail("null result output");
    }
    if (ransacIterations <= 0 || inlierDisparityTolerance <= 0.0F) {
        return fail("invalid RANSAC budget or tolerance");
    }
    // Work in inverse depth: x = 1/depth, model d = scale*x + shift.
    std::vector<float> invDepth;
    std::vector<float> disparity;
    invDepth.reserve(samples.size());
    disparity.reserve(samples.size());
    for (const MonoAlignSample& sample : samples) {
        if (!std::isfinite(sample.disparity) ||
            !std::isfinite(sample.envDepthMeters) ||
            sample.envDepthMeters <= 0.0F) {
            continue;
        }
        invDepth.push_back(1.0F / sample.envDepthMeters);
        disparity.push_back(sample.disparity);
    }
    if (invDepth.size() < 2) {
        return fail("fewer than two valid env samples");
    }
    // Minimal solver on pairs with distinct inverse depth; deterministic
    // stream so host tests are repeatable.
    std::mt19937 rng(0xC10Cu);
    std::uniform_int_distribution<size_t> pick(0, invDepth.size() - 1);
    float bestScale = 0.0F;
    float bestShift = 0.0F;
    size_t bestInliers = 0;
    constexpr float kDistinctInverseDepth = 1e-6F;
    for (int iteration = 0; iteration < ransacIterations; ++iteration) {
        const size_t i = pick(rng);
        const size_t j = pick(rng);
        if (i == j) {
            continue;
        }
        const float dx = invDepth[i] - invDepth[j];
        if (std::fabs(dx) < kDistinctInverseDepth) {
            continue;
        }
        const float scale = (disparity[i] - disparity[j]) / dx;
        if (!(scale > 0.0F) || !std::isfinite(scale)) {
            continue;  // Disparity must grow with proximity.
        }
        const float shift = disparity[i] - scale * invDepth[i];
        size_t inliers = 0;
        for (size_t k = 0; k < invDepth.size(); ++k) {
            const float predicted = scale * invDepth[k] + shift;
            if (std::fabs(disparity[k] - predicted) <=
                inlierDisparityTolerance) {
                ++inliers;
            }
        }
        if (inliers > bestInliers) {
            bestInliers = inliers;
            bestScale = scale;
            bestShift = shift;
        }
    }
    if (bestInliers < 2) {
        return fail("no consensus pair: degenerate geometry");
    }
    // Least-squares refinement on the consensus set.
    double sumX = 0.0;
    double sumD = 0.0;
    double sumXX = 0.0;
    double sumXD = 0.0;
    size_t refined = 0;
    for (size_t k = 0; k < invDepth.size(); ++k) {
        const float predicted = bestScale * invDepth[k] + bestShift;
        if (std::fabs(disparity[k] - predicted) <=
            inlierDisparityTolerance) {
            const double x = invDepth[k];
            const double d = disparity[k];
            sumX += x;
            sumD += d;
            sumXX += x * x;
            sumXD += x * d;
            ++refined;
        }
    }
    const double denom =
        static_cast<double>(refined) * sumXX - sumX * sumX;
    if (!(denom > 0.0) || !std::isfinite(denom)) {
        return fail("refinement degenerate: collinear samples");
    }
    const double scale =
        (static_cast<double>(refined) * sumXD - sumX * sumD) / denom;
    const double shift = (sumD - scale * sumX) / static_cast<double>(refined);
    if (!(scale > 0.0) || !std::isfinite(scale) ||
        !std::isfinite(shift)) {
        return fail("refined scale invalid");
    }
    const float inlierRatio = static_cast<float>(refined) /
                              static_cast<float>(invDepth.size());
    if (inlierRatio < minimumInlierRatio) {
        return fail("inlier ratio below minimum");
    }
    result->scale = static_cast<float>(scale);
    result->shift = static_cast<float>(shift);
    result->inlierRatio = inlierRatio;
    result->valid = true;
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

bool StereoRangeToPoints(
    const std::vector<float>& rangeMeters,
    int32_t width,
    int32_t height,
    float fx,
    float fy,
    float cx,
    float cy,
    std::vector<math::Vec3>* pointsInLocal,
    std::string* reason) {
    const auto fail = [&](const char* message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };
    if (pointsInLocal == nullptr) {
        return fail("null output");
    }
    if (width <= 0 || height <= 0 || !std::isfinite(fx) ||
        !std::isfinite(fy) || !std::isfinite(cx) || !std::isfinite(cy) ||
        fx <= 0.0F || fy <= 0.0F) {
        return fail("invalid image or intrinsics");
    }
    if (rangeMeters.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return fail("range/image size mismatch");
    }
    pointsInLocal->clear();
    pointsInLocal->reserve(rangeMeters.size() / 4);
    for (int32_t v = 0; v < height; ++v) {
        for (int32_t u = 0; u < width; ++u) {
            const float range =
                rangeMeters[static_cast<size_t>(v) * static_cast<size_t>(width) + static_cast<size_t>(u)];
            if (!std::isfinite(range) || range <= 0.0F) {
                continue;
            }
            // Camera2 pinhole (+Z forward, +Y down) then half-turn about X
            // into LOCAL (-Z forward, +Y up): (x, y, z) -> (x, -y, -z).
            const float xCam = (static_cast<float>(u) - cx) * range / fx;
            const float yCam = (static_cast<float>(v) - cy) * range / fy;
            pointsInLocal->push_back({xCam, -yCam, -range});
        }
    }
    return true;
}

}  // namespace questlab::fusion
