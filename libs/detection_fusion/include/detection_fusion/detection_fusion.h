#pragma once

#include "xr_math/xr_math.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace questlab::fusion {

// A detection's four corner bearings, expressed in LOCAL from the camera
// position that observed it. Together with the origin these define the
// pyramid of space consistent with the 2D box.
struct DetectionFrustum {
    math::Vec3 originInLocal{};
    std::array<math::Vec3, 4> cornerDirectionsInLocal{};
    math::Vec3 centerDirectionInLocal{};
};

// Depth sees only the surface facing the viewer, so the extent along the
// viewing axis is never measurable from one viewpoint. A per-class prior fills
// it. This is an assumption and is flagged as one on the result: position stays
// measured, depth becomes assumed, and the two must not be confused.
struct ObjectSizePrior {
    bool valid = false;
    // Full extent along the viewing axis, in metres.
    float depthMeters = 0.0F;
};

struct FusionParameters {
    // Fraction of the frustum shrunk away at the border. A 2D box always
    // clips some background at its edges, so the outer band is discarded
    // before clustering rather than being cleaned up afterwards.
    float borderInsetFraction = 0.12F;
    // Minimum share of the frustum's samples a surface must hold to count as
    // a real surface rather than depth noise. Deliberately small, because
    // selection is by occlusion order, not by popularity: the nearest
    // qualifying surface wins. A background wall routinely contributes more
    // samples than the object in front of it, and a thin or perforated object
    // may hold only a small fraction, so a large share would systematically
    // place the box on the wall. This value only has to exceed scattered
    // noise, which does not cluster within clusterToleranceMeters.
    float foregroundSampleShare = 0.05F;
    // Samples further than this from the selected surface are treated as
    // background or foreground clutter.
    float clusterToleranceMeters = 0.25F;
    // Extents are taken from these percentiles rather than min/max, so one
    // stray sample cannot inflate the box.
    float extentLowPercentile = 0.05F;
    float extentHighPercentile = 0.95F;
    // RANSAC over the selected surface. Meta's environment depth disagrees
    // with itself by tens of centimetres across a couple of degrees, so a
    // percentile fit over every point drifts with the outliers. A consensus
    // fit discards them instead, and its plane normal gives a measured
    // orientation rather than the viewing direction used as a stand-in.
    uint32_t ransacIterations = 160;
    // Tightened deliberately. A loose consensus fits a plane to sensor noise
    // and returns a box regardless, which is worse than returning nothing:
    // the caller cannot tell a measurement from an artefact. These thresholds
    // are set so that a surface must genuinely be planar and well sampled.
    float ransacInlierMeters = 0.025F;
    float ransacMinimumInlierRatio = 0.70F;
    // A box is only emitted when the plane fit succeeded. Falling back to the
    // viewing bearing produces a plausible-looking cuboid from no evidence.
    bool requireMeasuredOrientation = true;
    // Ocean refinement (NonLinearOptimizationPlane, Huber) seeded by the
    // RANSAC plane. Off by default: enable for host evaluation or once the
    // plane_eval spike shows consistent gains. Needs QUESTLAB_HAS_OCEAN.
    bool useOceanRefinement = false;
    uint32_t minimumSampleCount = 120;
    // Noise floor only. Surface selection is governed by
    // foregroundSampleShare above; this must stay below it, or a small object
    // in front of a large wall would be selected and then rejected.
    float minimumInlierRatio = 0.10F;
    float minimumRangeMeters = 0.25F;
    float maximumRangeMeters = 6.0F;
    // Up axis in LOCAL. OpenXR LOCAL is Y-up. It is used only for the
    // degenerate fallback (no measurable in-plane extent): the measured
    // path keeps the surface's own pitch and roll, so boxes follow tilted
    // objects instead of staying level with the room.
    math::Vec3 upInLocal{0.0F, 1.0F, 0.0F};
};

// Provenance for the hybrid stereo-primary / env-depth-secondary design.
// Stereo supplies the representative range; env-depth agrees, falls back
// degraded, or loses. A mixed box is never presented as fully measured.
// Learned (appended last, wire value 3) is monocular learned depth: a prior,
// never primary. Existing wire values 0/1/2 are frozen for the C API.
enum class RangeSource {
    Stereo,
    EnvDepthFallback,
    Prior,
    Learned,
};

// Confidence multiplier the caller applies when emitting a Learned fallback
// box (R-3 mission discipline: a prior, not a measurement).
inline constexpr float kLearnedConfidenceFactor = 0.5F;

// An oriented cuboid: the thing a 2D box plus depth can actually support.
struct FusedBox {
    math::Vec3 centerInLocal{};
    // Half-extents along the box's own right, up, and forward axes.
    math::Vec3 halfExtents{};
    math::Quat orientationInLocal{};
    float representativeRangeMeters = 0.0F;
    // True when halfExtents.z came from a prior rather than from depth. The
    // near face is measured either way; only the far face is assumed.
    bool depthExtentIsAssumed = false;
    // True when a plane survived RANSAC and supplied the orientation. When
    // false the orientation falls back to the viewing bearing and is a display
    // convention, not a measurement. When true the full 6D surface frame is
    // measured: plane normal plus the in-plane principal axis, so pitch and
    // roll follow the object.
    bool orientationIsMeasured = false;
    float planeInlierRatio = 0.0F;
    uint32_t sampleCount = 0;
    float inlierRatio = 0.0F;
    // Coverage of the frustum's cross-section that produced samples. Low
    // coverage means the depth barely saw the object, which is a reason to
    // distrust the extents even when the centre is sound.
    float confidence = 0.0F;
    // R-7 provenance: which range produced this box. Defaults to Prior as the
    // sentinel meaning unset/hide unless the caller overwrites after CheckAgreement.
    RangeSource rangeSource = RangeSource::Prior;
};

// Agreement gate for the stereo-primary / env-depth-secondary design.
enum class AgreementVerdict {
    AgreeStereo,   // within tolerance: emit stereo box
    FallbackEnv,   // stereo invalid, env valid: emit degraded, flagged
    Hide,          // disagreement or both invalid: draw nothing
    FallbackLearned,  // env invalid, learned valid: emit degraded, flagged,
                      // confidence scaled by kLearnedConfidenceFactor
};

AgreementVerdict CheckAgreement(
    bool stereoValid,
    float stereoRangeMeters,
    bool envValid,
    float envRangeMeters,
    float toleranceMeters,
    float toleranceRatio,
    RangeSource* selectedSource);

// Learned monocular depth as third source (R-1): it never outranks a measured
// source. Env agreed: env kept (Learned stays a comparison source for G-3).
// Env invalid + learned valid: degraded learned fallback. Disagreement or
// both invalid: Hide (Q-2 default).
AgreementVerdict CheckLearnedAgreement(
    bool learnedValid,
    float learnedRangeMeters,
    bool envValid,
    float envRangeMeters,
    float toleranceMeters,
    float toleranceRatio,
    RangeSource* selectedSource);

// One learned-disparity sample paired with metric env depth at the same pixel.
// Only env-valid pixels may be supplied; non-positive env depth is skipped.
struct MonoAlignSample {
    float disparity = 0.0F;
    float envDepthMeters = 0.0F;
};

struct MonoAlignResult {
    // disparity = scale * (1 / depth) + shift.
    float scale = 0.0F;
    float shift = 0.0F;
    float inlierRatio = 0.0F;
    bool valid = false;
};

// Robust affine-in-disparity fit of relative learned disparity against metric
// env depth (R-2, mandatory: a wrong shift bends planes and corrupts the very
// normal the fit is for). RANSAC over minimal pairs in disparity space, least
// squares refinement on inliers. Refuses degenerate geometry (fewer than two
// distinct inverse depths, non-positive scale) with a reason.
bool AlignMonoDisparity(
    const std::vector<MonoAlignSample>& samples,
    int ransacIterations,
    float inlierDisparityTolerance,
    float minimumInlierRatio,
    MonoAlignResult* result,
    std::string* reason);

// Pinhole range image to LOCAL points for the host/identity-pose case.
// Camera frame is +Z-forward/+Y-down (Camera2 basis); output flips to
// OpenXR LOCAL (-Z forward, +Y up) via a half-turn about X. Invalid (<=0)
// range stays dropped, never zero-filled.
bool StereoRangeToPoints(
    const std::vector<float>& rangeMeters,
    int32_t width,
    int32_t height,
    float fx,
    float fy,
    float cx,
    float cy,
    std::vector<math::Vec3>* pointsInLocal,
    std::string* reason);

// True when the point lies inside the frustum pyramid, after the border inset.
bool PointInsideFrustum(
    const DetectionFrustum& frustum,
    const math::Vec3& pointInLocal,
    float borderInsetFraction);

// Selects the depth samples belonging to the detection and fits an oriented
// box to them. Returns false, with a reason, when the evidence is too weak:
// an empty result is correct output, not a failure to paper over.
bool FuseDetection(
    const DetectionFrustum& frustum,
    const std::vector<math::Vec3>& depthPointsInLocal,
    const FusionParameters& parameters,
    const ObjectSizePrior& sizePrior,
    FusedBox* box,
    std::string* reason);

}  // namespace questlab::fusion
