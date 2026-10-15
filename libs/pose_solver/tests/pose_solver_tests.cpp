#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "pose_solver/pose_solver.h"

#include "xr_math/xr_math.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using questlab::pose::Correspondence;
using questlab::pose::Intrinsics;
using questlab::pose::Options;
using questlab::pose::Result;
using questlab::pose::SolvePose;
using questlab::math::Pose;
using questlab::math::Quat;
using questlab::math::Rotate;
using questlab::math::TransformPoint;
using questlab::math::Vec2;
using questlab::math::Vec3;

constexpr double kPi = 3.14159265358979323846;

// --- Deterministic PRNG (splitmix64) ----------------------------------------

uint64_t SplitMix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double Uniform01(uint64_t* state) {
    return static_cast<double>(SplitMix64(state) >> 11) * 0x1.0p-53;
}

double Gaussian(uint64_t* state) {
    const double u1 = Uniform01(state);
    const double u2 = Uniform01(state);
    const double radius = std::sqrt(-2.0 * std::log(u1 + 1e-12));
    return radius * std::cos(2.0 * kPi * u2);
}

// --- Pose / rotation helpers ------------------------------------------------

Pose MakeGroundTruth() {
    Pose pose;
    pose.orientation = Quat{0.12F, -0.23F, 0.31F, 0.87F};
    questlab::math::Normalize(&pose.orientation);
    pose.position = Vec3{0.10F, -0.08F, 1.05F};
    return pose;
}

void QuatToMatrix(const Quat& q, double r[3][3]) {
    const double x = q.x;
    const double y = q.y;
    const double z = q.z;
    const double w = q.w;
    r[0][0] = 1.0 - 2.0 * (y * y + z * z);
    r[0][1] = 2.0 * (x * y - z * w);
    r[0][2] = 2.0 * (x * z + y * w);
    r[1][0] = 2.0 * (x * y + z * w);
    r[1][1] = 1.0 - 2.0 * (x * x + z * z);
    r[1][2] = 2.0 * (y * z - x * w);
    r[2][0] = 2.0 * (x * z - y * w);
    r[2][1] = 2.0 * (y * z + x * w);
    r[2][2] = 1.0 - 2.0 * (x * x + y * y);
}

double RotationErrorRadians(const Quat& a, const Quat& b) {
    double ra[3][3];
    double rb[3][3];
    QuatToMatrix(a, ra);
    QuatToMatrix(b, rb);
    double trace = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            trace += ra[i][j] * rb[i][j];
        }
    }
    const double cosine = std::clamp((trace - 1.0) * 0.5, -1.0, 1.0);
    return std::acos(cosine);
}

double PositionErrorMeters(const Vec3& a, const Vec3& b) {
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double DegreesToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

// --- Synthetic data ---------------------------------------------------------

std::vector<Vec3> MakeNonCoplanarPoints(int n, uint64_t* state) {
    std::vector<Vec3> points;
    points.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float x = static_cast<float>((Uniform01(state) - 0.5) * 0.5);
        const float y = static_cast<float>((Uniform01(state) - 0.5) * 0.5);
        const float z = static_cast<float>((Uniform01(state) - 0.5) * 0.5);
        points.push_back(Vec3{x, y, z});
    }
    return points;
}

std::vector<Vec3> MakeCoplanarPoints(int n, uint64_t* state) {
    std::vector<Vec3> points;
    points.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float x = static_cast<float>((Uniform01(state) - 0.5) * 0.5);
        const float y = static_cast<float>((Uniform01(state) - 0.5) * 0.5);
        points.push_back(Vec3{x, y, 0.0F});
    }
    return points;
}

std::vector<Correspondence> BuildCorrespondences(
    const Pose& groundTruth,
    const Intrinsics& intrinsics,
    const std::vector<Vec3>& objectPoints,
    double sigmaPx,
    uint64_t* state) {
    std::vector<Correspondence> correspondences;
    correspondences.reserve(objectPoints.size());
    for (const Vec3& object : objectPoints) {
        const Vec3 camera = TransformPoint(groundTruth, object);
        assert(camera.z > 0.0F);
        double x = static_cast<double>(intrinsics.fx) * camera.x / camera.z +
                   intrinsics.cx;
        double y = static_cast<double>(intrinsics.fy) * camera.y / camera.z +
                   intrinsics.cy;
        if (sigmaPx > 0.0) {
            x += sigmaPx * Gaussian(state);
            y += sigmaPx * Gaussian(state);
        }
        Correspondence correspondence;
        correspondence.objectPoint = object;
        correspondence.pixel = Vec2{static_cast<float>(x), static_cast<float>(y)};
        correspondences.push_back(correspondence);
    }
    return correspondences;
}

Intrinsics DefaultIntrinsics() {
    return Intrinsics{600.0F, 600.0F, 640.0F, 400.0F};
}

// --- Contract smoke (V-1) ---------------------------------------------------

void TestContract() {
    // Defaults must match the plan.md interface sketch exactly.
    const Intrinsics intrinsics;
    assert(intrinsics.fx == 0.0F && intrinsics.fy == 0.0F);
    assert(intrinsics.cx == 0.0F && intrinsics.cy == 0.0F);

    const Options options;
    assert(options.reprojectionThresholdPx == 3.0F);
    assert(options.maxIterations == 1000);
    assert(options.seed == 0x9E3779B9U);
    assert(options.minInliers == 6);
    assert(options.outputInlierMask == false);

    const Result result;
    assert(result.inlierCount == 0);
    assert(result.inlierMask.empty());
    assert(result.rmsReprojectionPixels == 0.0F);
    assert(result.pose.orientation.w == 1.0F);

    // Signature must be the exact contract type.
    bool (*function)(
        const Correspondence*, int, const Intrinsics&, const Options&,
        Result*, std::string*) = &SolvePose;
    (void)function;

    // A minimal valid solve must succeed (the solver is real beyond the stub).
    uint64_t state = 0x12345678U;
    const Pose groundTruth = MakeGroundTruth();
    const Intrinsics k = DefaultIntrinsics();
    std::vector<Vec3> points = MakeNonCoplanarPoints(6, &state);
    std::vector<Correspondence> correspondences =
        BuildCorrespondences(groundTruth, k, points, 0.0, &state);
    Result solved;
    std::string error;
    assert(SolvePose(
        correspondences.data(), static_cast<int>(correspondences.size()), k,
        Options{}, &solved, &error));
    assert(solved.inlierCount == static_cast<int>(correspondences.size()));
}

// --- Exact recovery (V-2) ---------------------------------------------------

void AssertExactRecovery(
    const std::vector<Vec3>& points,
    const std::vector<Correspondence>& correspondences,
    const Intrinsics& intrinsics,
    const Pose& groundTruth) {
    Result result;
    std::string error;
    assert(SolvePose(
        correspondences.data(), static_cast<int>(correspondences.size()),
        intrinsics, Options{}, &result, &error));
    const double positionError =
        PositionErrorMeters(result.pose.position, groundTruth.position);
    const double rotationError =
        RotationErrorRadians(result.pose.orientation, groundTruth.orientation);
    assert(positionError <= 2.0e-4);
    assert(rotationError <= DegreesToRadians(0.05));
    assert(result.rmsReprojectionPixels <= 2.0e-4F);
    assert(result.inlierCount == static_cast<int>(points.size()));
    std::printf(
        "  exact n=%d position_error=%.3e m rotation_error=%.4f deg rms=%.3e px\n",
        static_cast<int>(points.size()), positionError,
        rotationError * 180.0 / kPi,
        static_cast<double>(result.rmsReprojectionPixels));
}

void TestExactRecoveryNonCoplanar() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    for (const int n : {6, 40, 200}) {
        uint64_t state = 0xC0FFEE00U + static_cast<uint64_t>(n);
        std::vector<Vec3> points = MakeNonCoplanarPoints(n, &state);
        std::vector<Correspondence> correspondences =
            BuildCorrespondences(groundTruth, k, points, 0.0, &state);
        AssertExactRecovery(points, correspondences, k, groundTruth);
    }
}

void TestExactRecoveryCoplanar() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    for (const int n : {6, 40, 200}) {
        uint64_t state = 0xDEADBEEFU + static_cast<uint64_t>(n);
        std::vector<Vec3> points = MakeCoplanarPoints(n, &state);
        std::vector<Correspondence> correspondences =
            BuildCorrespondences(groundTruth, k, points, 0.0, &state);
        AssertExactRecovery(points, correspondences, k, groundTruth);
    }
}

// --- Noise (V-3) ------------------------------------------------------------

void TestNoise() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    const int n = 40;
    uint64_t state = 0xA11CE000U;
    std::vector<Vec3> points = MakeNonCoplanarPoints(n, &state);
    std::vector<Correspondence> correspondences =
        BuildCorrespondences(groundTruth, k, points, 0.5, &state);

    Result result;
    std::string error;
    assert(SolvePose(
        correspondences.data(), n, k, Options{}, &result, &error));
    const double positionError =
        PositionErrorMeters(result.pose.position, groundTruth.position);
    const double rotationError =
        RotationErrorRadians(result.pose.orientation, groundTruth.orientation);
    assert(positionError <= 8.0e-3);
    assert(rotationError <= DegreesToRadians(0.5));
    std::printf(
        "  noise sigma=0.5px n=%d position_error=%.3e m rotation_error=%.4f deg rms=%.4f px\n",
        n, positionError, rotationError * 180.0 / kPi,
        static_cast<double>(result.rmsReprojectionPixels));
}

// --- Outliers (V-4) ---------------------------------------------------------

void TestOutliers() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    const int inlierCount = 20;
    const int outlierCount = 20;
    const int total = inlierCount + outlierCount;

    uint64_t state = 0xBEEF1234U;
    std::vector<Vec3> inlierPoints = MakeNonCoplanarPoints(inlierCount, &state);
    std::vector<Correspondence> correspondences =
        BuildCorrespondences(groundTruth, k, inlierPoints, 0.5, &state);
    for (int i = 0; i < outlierCount; ++i) {
        const Vec3 object{
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
        };
        const Vec2 pixel{
            static_cast<float>(Uniform01(&state) * 1280.0),
            static_cast<float>(Uniform01(&state) * 800.0),
        };
        Correspondence outlier;
        outlier.objectPoint = object;
        outlier.pixel = pixel;
        correspondences.push_back(outlier);
    }

    Options options;
    options.outputInlierMask = true;
    options.seed = 0x5EED5EEDU;
    Result result;
    std::string error;
    assert(SolvePose(
        correspondences.data(), total, k, options, &result, &error));
    const double positionError =
        PositionErrorMeters(result.pose.position, groundTruth.position);
    const double rotationError =
        RotationErrorRadians(result.pose.orientation, groundTruth.orientation);
    assert(positionError <= 8.0e-3);
    assert(rotationError <= DegreesToRadians(0.5));

    assert(result.inlierMask.size() == static_cast<size_t>(total));
    int agreement = 0;
    for (int i = 0; i < total; ++i) {
        const bool groundTruthInlier = i < inlierCount;
        const bool reportedInlier = result.inlierMask[i] != 0U;
        if (groundTruthInlier == reportedInlier) {
            ++agreement;
        }
    }
    assert(agreement >= 19);  // >= 95% of 40
    std::printf(
        "  outliers 50%% n=%d mask_agreement=%d/%d position_error=%.3e m rotation_error=%.4f deg\n",
        total, agreement, total, positionError, rotationError * 180.0 / kPi);
}

// --- Metric scale (V-5) -----------------------------------------------------

void TestMetricScale() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    const int n = 40;
    uint64_t state = 0x5CA1E000U;
    std::vector<Vec3> points = MakeNonCoplanarPoints(n, &state);
    std::vector<Correspondence> correspondences =
        BuildCorrespondences(groundTruth, k, points, 0.0, &state);

    Result reference;
    std::string error;
    assert(SolvePose(
        correspondences.data(), n, k, Options{}, &reference, &error));

    const double scale = 2.5;
    std::vector<Correspondence> scaledCorrespondences;
    scaledCorrespondences.reserve(correspondences.size());
    for (const Correspondence& c : correspondences) {
        Correspondence scaled;
        scaled.objectPoint = Vec3{
            static_cast<float>(c.objectPoint.x * scale),
            static_cast<float>(c.objectPoint.y * scale),
            static_cast<float>(c.objectPoint.z * scale),
        };
        scaled.pixel = c.pixel;  // same pixels, k-times-larger object
        scaledCorrespondences.push_back(scaled);
    }

    Result scaledResult;
    assert(SolvePose(
        scaledCorrespondences.data(), n, k, Options{}, &scaledResult, &error));

    const double expectedX = reference.pose.position.x * scale;
    const double expectedY = reference.pose.position.y * scale;
    const double expectedZ = reference.pose.position.z * scale;
    const double norm =
        std::sqrt(expectedX * expectedX + expectedY * expectedY +
                  expectedZ * expectedZ);
    const double dx = scaledResult.pose.position.x - expectedX;
    const double dy = scaledResult.pose.position.y - expectedY;
    const double dz = scaledResult.pose.position.z - expectedZ;
    const double relative = std::sqrt(dx * dx + dy * dy + dz * dz) / norm;
    assert(relative <= 1.0e-3);

    const double rotationError = RotationErrorRadians(
        scaledResult.pose.orientation, reference.pose.orientation);
    assert(rotationError <= DegreesToRadians(0.1));
    std::printf(
        "  metric scale k=2.5 relative_position_error=%.3e rotation_error=%.4f deg\n",
        relative, rotationError * 180.0 / kPi);
}

// --- Determinism (V-5) ------------------------------------------------------

void TestDeterminism() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    const int inlierCount = 20;
    const int outlierCount = 20;
    const int total = inlierCount + outlierCount;

    uint64_t state = 0xDE7E2D1EU;
    std::vector<Vec3> inlierPoints = MakeNonCoplanarPoints(inlierCount, &state);
    std::vector<Correspondence> correspondences =
        BuildCorrespondences(groundTruth, k, inlierPoints, 0.5, &state);
    for (int i = 0; i < outlierCount; ++i) {
        Correspondence outlier;
        outlier.objectPoint = Vec3{
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
        };
        outlier.pixel = Vec2{
            static_cast<float>(Uniform01(&state) * 1280.0),
            static_cast<float>(Uniform01(&state) * 800.0),
        };
        correspondences.push_back(outlier);
    }

    Options options;
    options.outputInlierMask = true;
    options.seed = 0x1234ABCDU;

    Result first;
    Result second;
    std::string firstError;
    std::string secondError;
    assert(SolvePose(
        correspondences.data(), total, k, options, &first, &firstError));
    assert(SolvePose(
        correspondences.data(), total, k, options, &second, &secondError));

    assert(
        std::memcmp(
            &first.pose.orientation, &second.pose.orientation,
            sizeof(Quat)) == 0);
    assert(
        std::memcmp(
            &first.pose.position, &second.pose.position, sizeof(Vec3)) == 0);
    assert(first.inlierCount == second.inlierCount);
    assert(
        std::memcmp(
            &first.rmsReprojectionPixels, &second.rmsReprojectionPixels,
            sizeof(float)) == 0);
    assert(first.inlierMask == second.inlierMask);
    assert(firstError == secondError);
    std::printf("  determinism: two runs bit-identical (pose, count, rms, mask)\n");
}

// --- Degenerate / refusal (V-6) ---------------------------------------------

void AssertAllFieldsFinite(const Result& result) {
    assert(std::isfinite(result.pose.orientation.x));
    assert(std::isfinite(result.pose.orientation.y));
    assert(std::isfinite(result.pose.orientation.z));
    assert(std::isfinite(result.pose.orientation.w));
    assert(std::isfinite(result.pose.position.x));
    assert(std::isfinite(result.pose.position.y));
    assert(std::isfinite(result.pose.position.z));
    assert(std::isfinite(result.rmsReprojectionPixels));
    // inlierCount is an int; no NaN/Inf concept, but it must be reset.
    assert(result.inlierCount == 0);
    assert(result.inlierMask.empty());
}

void TestDegenerateAndRefusal() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    uint64_t state = 0xDE6E0000U;
    std::vector<Vec3> points = MakeNonCoplanarPoints(8, &state);
    std::vector<Correspondence> correspondences =
        BuildCorrespondences(groundTruth, k, points, 0.0, &state);

    Result result;
    std::string error;

    // count < 6
    assert(!SolvePose(
        correspondences.data(), 5, k, Options{}, &result, &error));
    assert(error == "insufficient correspondences");
    AssertAllFieldsFinite(result);

    // non-finite object point
    {
        std::vector<Correspondence> bad = correspondences;
        bad[2].objectPoint.x = std::nanf("");
        assert(!SolvePose(
            bad.data(), static_cast<int>(bad.size()), k, Options{}, &result,
            &error));
        assert(error == "non-finite correspondence");
        AssertAllFieldsFinite(result);
    }

    // non-finite pixel
    {
        std::vector<Correspondence> bad = correspondences;
        bad[3].pixel.y = std::numeric_limits<float>::infinity();
        assert(!SolvePose(
            bad.data(), static_cast<int>(bad.size()), k, Options{}, &result,
            &error));
        assert(error == "non-finite correspondence");
        AssertAllFieldsFinite(result);
    }

    // null result
    assert(!SolvePose(
        correspondences.data(), static_cast<int>(correspondences.size()), k,
        Options{}, nullptr, &error));
    assert(error == "invalid pose input");

    // bad intrinsics
    {
        Intrinsics bad = k;
        bad.fx = 0.0F;
        assert(!SolvePose(
            correspondences.data(), static_cast<int>(correspondences.size()),
            bad, Options{}, &result, &error));
        assert(error == "invalid pose input");
    }
    {
        Intrinsics bad = k;
        bad.fy = std::nanf("");
        assert(!SolvePose(
            correspondences.data(), static_cast<int>(correspondences.size()),
            bad, Options{}, &result, &error));
        assert(error == "invalid pose input");
    }
    {
        Intrinsics bad = k;
        bad.cx = std::numeric_limits<float>::infinity();
        assert(!SolvePose(
            correspondences.data(), static_cast<int>(correspondences.size()),
            bad, Options{}, &result, &error));
        assert(error == "invalid pose input");
    }

    // bad options
    {
        Options bad;
        bad.reprojectionThresholdPx = 0.0F;
        assert(!SolvePose(
            correspondences.data(), static_cast<int>(correspondences.size()), k,
            bad, &result, &error));
        assert(error == "invalid pose input");
    }
    {
        Options bad;
        bad.maxIterations = 0;
        assert(!SolvePose(
            correspondences.data(), static_cast<int>(correspondences.size()), k,
            bad, &result, &error));
        assert(error == "invalid pose input");
    }
    {
        Options bad;
        bad.minInliers = 2;
        assert(!SolvePose(
            correspondences.data(), static_cast<int>(correspondences.size()), k,
            bad, &result, &error));
        assert(error == "invalid pose input");
    }

    // collinear object points
    {
        std::vector<Correspondence> collinear;
        for (int i = 0; i < 6; ++i) {
            Correspondence c;
            c.objectPoint = Vec3{static_cast<float>(i), 0.0F, 0.0F};
            c.pixel = Vec2{static_cast<float>(100 + i), 200.0F};
            collinear.push_back(c);
        }
        assert(!SolvePose(
            collinear.data(), static_cast<int>(collinear.size()), k, Options{},
            &result, &error));
        assert(error == "degenerate sample geometry");
        AssertAllFieldsFinite(result);
    }

    // fewer than 3 unique points
    {
        std::vector<Correspondence> duplicate;
        for (int i = 0; i < 6; ++i) {
            Correspondence c;
            c.objectPoint = Vec3{
                i < 3 ? 0.1F : 0.2F, 0.0F, 0.0F};
            c.pixel = Vec2{static_cast<float>(100 + i), 200.0F};
            duplicate.push_back(c);
        }
        assert(!SolvePose(
            duplicate.data(), static_cast<int>(duplicate.size()), k, Options{},
            &result, &error));
        assert(error == "degenerate sample geometry");
        AssertAllFieldsFinite(result);
    }

    // no consensus: non-degenerate geometry but pure-noise pixels
    {
        std::vector<Correspondence> random;
        uint64_t randomState = 0x0BAD5EEDU;
        std::vector<Vec3> object = MakeNonCoplanarPoints(8, &randomState);
        for (int i = 0; i < 8; ++i) {
            Correspondence c;
            c.objectPoint = object[i];
            c.pixel = Vec2{
                static_cast<float>(Uniform01(&randomState) * 1280.0),
                static_cast<float>(Uniform01(&randomState) * 800.0),
            };
            random.push_back(c);
        }
        assert(!SolvePose(
            random.data(), static_cast<int>(random.size()), k, Options{},
            &result, &error));
        assert(error == "no consensus");
        AssertAllFieldsFinite(result);
    }
}

// --- Performance budget (V-7) -----------------------------------------------

void TestPerformanceBudget() {
    const Intrinsics k = DefaultIntrinsics();
    const Pose groundTruth = MakeGroundTruth();
    const int inlierCount = 50;
    const int outlierCount = 50;
    const int total = inlierCount + outlierCount;

    uint64_t state = 0x0FF1CE00U;
    std::vector<Vec3> inlierPoints = MakeNonCoplanarPoints(inlierCount, &state);
    std::vector<Correspondence> correspondences =
        BuildCorrespondences(groundTruth, k, inlierPoints, 0.5, &state);
    for (int i = 0; i < outlierCount; ++i) {
        Correspondence outlier;
        outlier.objectPoint = Vec3{
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
            static_cast<float>((Uniform01(&state) - 0.5) * 0.5),
        };
        outlier.pixel = Vec2{
            static_cast<float>(Uniform01(&state) * 1280.0),
            static_cast<float>(Uniform01(&state) * 800.0),
        };
        correspondences.push_back(outlier);
    }

    Options options;
    options.seed = 0x9E3779B9U;

    const auto solveOnce = [&]() {
        Result result;
        std::string error;
        return SolvePose(
            correspondences.data(), total, k, options, &result, &error);
    };

    // Warmup run, then best of 3.
    assert(solveOnce());
    double bestMilliseconds = 1.0e12;
    for (int trial = 0; trial < 3; ++trial) {
        const auto begin = std::chrono::steady_clock::now();
        assert(solveOnce());
        const auto end = std::chrono::steady_clock::now();
        const double milliseconds =
            std::chrono::duration<double, std::milli>(end - begin).count();
        if (milliseconds < bestMilliseconds) {
            bestMilliseconds = milliseconds;
        }
    }
    std::printf(
        "performance: best-of-3 SolvePose(N=100, 50%% outliers) = %.3f ms\n",
        bestMilliseconds);
    assert(bestMilliseconds < 500.0);
}

// --- Group registry ---------------------------------------------------------

struct TestGroup {
    const char* name;
    void (*run)();
};

const TestGroup kGroups[] = {
    {"contract", TestContract},
    {"exact non-coplanar", TestExactRecoveryNonCoplanar},
    {"exact coplanar", TestExactRecoveryCoplanar},
    {"noise", TestNoise},
    {"outliers", TestOutliers},
    {"metric scale", TestMetricScale},
    {"determinism", TestDeterminism},
    {"degenerate", TestDegenerateAndRefusal},
    {"performance", TestPerformanceBudget},
};

bool Matches(const char* name, const char* filter) {
    const std::string lowerName(name);
    std::string nameLower;
    nameLower.reserve(lowerName.size());
    for (char c : lowerName) {
        nameLower.push_back(
            static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    const std::string lowerFilter(filter);
    std::string filterLower;
    filterLower.reserve(lowerFilter.size());
    for (char c : lowerFilter) {
        filterLower.push_back(
            static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    return nameLower.find(filterLower) != std::string::npos;
}

void RunGroup(const TestGroup& group) {
    std::printf("[start] %s\n", group.name);
    group.run();
    std::printf("[end] %s\n", group.name);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc <= 1) {
        for (const TestGroup& group : kGroups) {
            RunGroup(group);
        }
    } else {
        for (int i = 1; i < argc; ++i) {
            bool matched = false;
            for (const TestGroup& group : kGroups) {
                if (Matches(group.name, argv[i])) {
                    RunGroup(group);
                    matched = true;
                }
            }
            if (!matched) {
                std::fprintf(
                    stderr, "unknown test group filter: %s\n", argv[i]);
                return 2;
            }
        }
    }
    std::printf("pose_solver tests passed\n");
    return 0;
}
