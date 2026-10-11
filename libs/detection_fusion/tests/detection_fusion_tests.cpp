#include "detection_fusion/detection_fusion.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
    }
    return condition;
}

// A frustum looking down LOCAL -Z from the origin, half-angle ~11 degrees.
questlab::fusion::DetectionFrustum MakeFrustum(float halfTangent) {
    questlab::fusion::DetectionFrustum frustum;
    frustum.originInLocal = {0.0F, 0.0F, 0.0F};
    frustum.centerDirectionInLocal = {0.0F, 0.0F, -1.0F};
    const std::array<std::pair<float, float>, 4> corners = {{
        {-halfTangent, halfTangent},
        {halfTangent, halfTangent},
        {halfTangent, -halfTangent},
        {-halfTangent, -halfTangent},
    }};
    for (size_t i = 0; i < 4; ++i) {
        questlab::math::Vec3 direction{
            corners[i].first, corners[i].second, -1.0F};
        questlab::math::Normalize(&direction);
        frustum.cornerDirectionsInLocal[i] = direction;
    }
    return frustum;
}

// A flat slab of samples at a given distance, spanning halfWidth/halfHeight.
void AddSlab(
    std::vector<questlab::math::Vec3>* points,
    float distance,
    float halfWidth,
    float halfHeight,
    int steps) {
    for (int ix = -steps; ix <= steps; ++ix) {
        for (int iy = -steps; iy <= steps; ++iy) {
            const float fx = static_cast<float>(ix) / static_cast<float>(steps);
            const float fy = static_cast<float>(iy) / static_cast<float>(steps);
            points->push_back(
                {fx * halfWidth, fy * halfHeight, -distance});
        }
    }
}

}  // namespace

int main() {
    bool passed = true;
    const auto frustum = MakeFrustum(0.2F);
    questlab::fusion::FusionParameters parameters;

    // A single slab at 2 m must produce a box centred on it.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 2.0F, 0.15F, 0.20F, 10);
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            reason.empty() ? "slab fusion must succeed" : reason.c_str());
        passed &= Expect(
            std::fabs(box.centerInLocal.z + 2.0F) < 0.05F,
            "box centre must sit at the slab distance");
        passed &= Expect(
            std::fabs(box.representativeRangeMeters - 2.0F) < 0.15F,
            "representative range must match the slab");
        // Percentile extents trim the outer 5%, so expect slightly under the
        // true half-width rather than exactly it.
        passed &= Expect(
            box.halfExtents.x > 0.10F && box.halfExtents.x < 0.16F,
            "half-width must approximate the slab half-width");
        passed &= Expect(
            box.halfExtents.y > 0.14F && box.halfExtents.y < 0.21F,
            "half-height must approximate the slab half-height");
    }

    // Object in front of a wall: the near surface must win, and the wall must
    // not drag the centre backwards. This is the case a 2D box always creates.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 1.5F, 0.10F, 0.10F, 8);   // object
        AddSlab(&points, 4.0F, 0.60F, 0.60F, 12);  // wall behind
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            reason.empty() ? "object-in-front fusion must succeed"
                           : reason.c_str());
        passed &= Expect(
            std::fabs(box.representativeRangeMeters - 1.5F) < 0.2F,
            "the nearer surface must be selected over the wall");
        passed &= Expect(
            box.centerInLocal.z > -2.0F,
            "the wall must not pull the centre backwards");
    }

    // Worst case: the wall is far denser than the object. The object must
    // still win, and the inlier-ratio floor must not then refuse it.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 1.2F, 0.08F, 0.08F, 6);   // small object, 169 pts
        AddSlab(&points, 3.5F, 0.55F, 0.55F, 20);  // dense wall, 1681 pts
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            reason.empty() ? "dense-wall fusion must succeed" : reason.c_str());
        passed &= Expect(
            std::fabs(box.representativeRangeMeters - 1.2F) < 0.2F,
            "a small near object must beat a dense far wall");
        passed &= Expect(
            box.inlierRatio < 0.3F,
            "this case must genuinely have a low inlier ratio");
    }

    // Points outside the frustum must be ignored entirely.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 2.0F, 0.15F, 0.15F, 10);
        // A dense slab well outside the cone, laterally offset.
        for (int i = 0; i < 4000; ++i) {
            points.push_back({5.0F, 0.0F, -2.0F});
        }
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            "fusion must succeed while ignoring outside points");
        passed &= Expect(
            box.halfExtents.x < 0.3F,
            "points outside the frustum must not widen the box");
        passed &= Expect(
            !questlab::fusion::PointInsideFrustum(
                frustum, {5.0F, 0.0F, -2.0F}, parameters.borderInsetFraction),
            "a laterally distant point must be outside the frustum");
        passed &= Expect(
            questlab::fusion::PointInsideFrustum(
                frustum, {0.0F, 0.0F, -2.0F}, parameters.borderInsetFraction),
            "an axial point must be inside the frustum");
    }

    // Behind the camera is never inside, whatever the lateral position.
    passed &= Expect(
        !questlab::fusion::PointInsideFrustum(
            frustum, {0.0F, 0.0F, 2.0F}, parameters.borderInsetFraction),
        "a point behind the camera must be rejected");

    // Too few samples is a refusal with a reason, not a tiny box.
    {
        std::vector<questlab::math::Vec3> points;
        points.push_back({0.0F, 0.0F, -2.0F});
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            !questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            "sparse evidence must be refused");
        passed &= Expect(!reason.empty(), "a refusal must carry a reason");
    }

    // Empty input must refuse rather than produce a zero box at the origin.
    {
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            !questlab::fusion::FuseDetection(
                frustum, {}, parameters, {}, &box, &reason),
            "empty depth must be refused");
    }

    // The box must be level with the room: its up axis follows LOCAL up even
    // though the bearing is tilted downward.
    {
        questlab::fusion::DetectionFrustum tilted = MakeFrustum(0.2F);
        questlab::math::Vec3 bearing{0.0F, -0.5F, -1.0F};
        questlab::math::Normalize(&bearing);
        tilted.centerDirectionInLocal = bearing;
        for (auto& corner : tilted.cornerDirectionsInLocal) {
            corner = questlab::math::Add(corner, {0.0F, -0.5F, 0.0F});
            questlab::math::Normalize(&corner);
        }
        std::vector<questlab::math::Vec3> points;
        for (int ix = -8; ix <= 8; ++ix) {
            for (int iy = -8; iy <= 8; ++iy) {
                const float fx = static_cast<float>(ix) / 8.0F;
                const float fy = static_cast<float>(iy) / 8.0F;
                points.push_back(questlab::math::Add(
                    questlab::math::Scale(bearing, 2.0F),
                    {fx * 0.12F, fy * 0.12F, 0.0F}));
            }
        }
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                tilted, points, parameters, {}, &box, &reason),
            reason.empty() ? "tilted fusion must succeed" : reason.c_str());
        const questlab::math::Vec3 boxUp =
            questlab::math::Rotate(box.orientationInLocal, {0.0F, 1.0F, 0.0F});
        passed &= Expect(
            std::fabs(boxUp.y - 1.0F) < 0.05F,
            "the box up axis must stay aligned with LOCAL up");
    }

    // With a size prior the near face must stay where depth measured it and
    // the box must grow away from the viewer, not straddle the surface.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 2.0F, 0.15F, 0.15F, 10);
        questlab::fusion::FusedBox measured;
        questlab::fusion::FusedBox prior;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &measured, &reason),
            "measured fusion must succeed");
        questlab::fusion::ObjectSizePrior sizePrior;
        sizePrior.valid = true;
        sizePrior.depthMeters = 0.60F;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, sizePrior, &prior, &reason),
            "prior fusion must succeed");
        passed &= Expect(
            !measured.depthExtentIsAssumed && prior.depthExtentIsAssumed,
            "only the prior result may report an assumed depth extent");
        passed &= Expect(
            std::fabs(prior.halfExtents.z - 0.30F) < 1.0e-3F,
            "the prior must set the half depth");
        // forward is away from the viewer, i.e. -Z here, so the centre must
        // move further from the origin than the measured slab.
        passed &= Expect(
            prior.centerInLocal.z < measured.centerInLocal.z,
            "the prior box must extend away from the viewer");
        passed &= Expect(
            std::fabs(prior.centerInLocal.z + 2.0F + 0.30F) < 0.05F,
            "the near face must stay on the measured surface");
        passed &= Expect(
            std::fabs(prior.halfExtents.x - measured.halfExtents.x) < 1.0e-4F,
            "lateral extents must remain measured");
    }

    // R-7 provenance: FuseDetection leaves Prior (hide sentinel) on success;
    // the caller overwrites after CheckAgreement. Failure also leaves Prior.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 2.0F, 0.15F, 0.15F, 10);
        questlab::fusion::FusedBox box;
        box.rangeSource = questlab::fusion::RangeSource::Stereo;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            "provenance fusion must succeed");
        passed &= Expect(
            box.rangeSource == questlab::fusion::RangeSource::Prior,
            "FuseDetection success must reset rangeSource to Prior");
    }
    {
        questlab::fusion::FusedBox box;
        box.rangeSource = questlab::fusion::RangeSource::Stereo;
        std::string reason;
        passed &= Expect(
            !questlab::fusion::FuseDetection(
                frustum, {}, parameters, {}, &box, &reason),
            "empty fusion must fail");
        passed &= Expect(
            box.rangeSource == questlab::fusion::RangeSource::Prior,
            "FuseDetection failure must leave a defined Prior source");
        passed &= Expect(!reason.empty(), "failure must carry a reason");
    }
    {
        const questlab::fusion::FusedBox box;
        passed &= Expect(
            box.rangeSource == questlab::fusion::RangeSource::Prior,
            "default FusedBox rangeSource must be Prior");
    }

    // R-6 agreement gate: tolerance = toleranceMeters + toleranceRatio * min.
    {
        questlab::fusion::RangeSource source =
            questlab::fusion::RangeSource::Prior;
        const questlab::fusion::AgreementVerdict verdict =
            questlab::fusion::CheckAgreement(
                true, 2.0F, true, 2.05F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::AgreeStereo,
            "agreeing ranges must return AgreeStereo");
        passed &= Expect(
            source == questlab::fusion::RangeSource::Stereo,
            "agreeing ranges must select Stereo");
    }
    {
        questlab::fusion::RangeSource source =
            questlab::fusion::RangeSource::Prior;
        const questlab::fusion::AgreementVerdict verdict =
            questlab::fusion::CheckAgreement(
                true, 2.0F, true, 3.0F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::Hide,
            "disagreeing ranges must hide");
    }
    {
        questlab::fusion::RangeSource source =
            questlab::fusion::RangeSource::Prior;
        const questlab::fusion::AgreementVerdict verdict =
            questlab::fusion::CheckAgreement(
                true, 1.5F, false, 0.0F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::AgreeStereo,
            "stereo-only must return AgreeStereo");
        passed &= Expect(
            source == questlab::fusion::RangeSource::Stereo,
            "stereo-only must select Stereo");
    }
    {
        questlab::fusion::RangeSource source =
            questlab::fusion::RangeSource::Prior;
        const questlab::fusion::AgreementVerdict verdict =
            questlab::fusion::CheckAgreement(
                false, 0.0F, true, 1.5F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::FallbackEnv,
            "env-only must return FallbackEnv");
        passed &= Expect(
            source == questlab::fusion::RangeSource::EnvDepthFallback,
            "env-only must select EnvDepthFallback");
    }
    {
        questlab::fusion::RangeSource source =
            questlab::fusion::RangeSource::Stereo;
        const questlab::fusion::AgreementVerdict verdict =
            questlab::fusion::CheckAgreement(
                false, 0.0F, false, 0.0F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::Hide,
            "neither-valid must hide");
        passed &= Expect(
            source == questlab::fusion::RangeSource::Prior,
            "neither-valid must select Prior");
    }
    {
        questlab::fusion::RangeSource source =
            questlab::fusion::RangeSource::Prior;
        const questlab::fusion::AgreementVerdict verdict =
            questlab::fusion::CheckAgreement(
                true, 2.0F, true, 2.30F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::AgreeStereo,
            "delta at tolerance boundary must agree");
    }
    {
        questlab::fusion::RangeSource source =
            questlab::fusion::RangeSource::Prior;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        questlab::fusion::AgreementVerdict verdict =
            questlab::fusion::CheckAgreement(
                true, 0.0F, true, 2.0F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::FallbackEnv,
            "non-positive stereo must fall back to env");
        verdict = questlab::fusion::CheckAgreement(
            true, nan, false, 0.0F, 0.10F, 0.10F, &source);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::Hide,
            "NaN stereo alone must hide");
        passed &= Expect(
            source == questlab::fusion::RangeSource::Prior,
            "NaN stereo alone must select Prior");
        verdict = questlab::fusion::CheckAgreement(
            true, 1.0F, false, 0.0F, 0.10F, 0.10F, nullptr);
        passed &= Expect(
            verdict == questlab::fusion::AgreementVerdict::AgreeStereo,
            "null selectedSource must still return a verdict");
    }

    // StereoRangeToPoints: Camera2 (+Z forward, +Y down) flips to LOCAL
    // (-Z forward, +Y up): x=(u-cx)*r/fx, y=-((v-cy)*r/fy), z=-r.
    {
        const int32_t width = 2;
        const int32_t height = 2;
        const float fx = 100.0F;
        const float fy = 200.0F;
        const float cx = 0.5F;
        const float cy = 0.5F;
        const std::vector<float> range = {1.0F, 2.0F, 3.0F, 4.0F};
        std::vector<questlab::math::Vec3> points;
        std::string reason;
        passed &= Expect(
            questlab::fusion::StereoRangeToPoints(
                range, width, height, fx, fy, cx, cy, &points, &reason),
            reason.empty() ? "range to points must succeed" : reason.c_str());
        passed &= Expect(points.size() == 4U, "all valid ranges must emit");
        if (points.size() == 4U) {
            passed &= Expect(
                std::fabs(points[0].x + 0.005F) < 1.0e-6F &&
                    std::fabs(points[0].y - 0.0025F) < 1.0e-6F &&
                    std::fabs(points[0].z + 1.0F) < 1.0e-6F,
                "pixel (0,0) flip must match pinhole model");
            passed &= Expect(
                std::fabs(points[1].x - 0.01F) < 1.0e-6F &&
                    std::fabs(points[1].y - 0.005F) < 1.0e-6F &&
                    std::fabs(points[1].z + 2.0F) < 1.0e-6F,
                "pixel (1,0) flip must match pinhole model");
            passed &= Expect(
                std::fabs(points[2].x + 0.015F) < 1.0e-6F &&
                    std::fabs(points[2].y + 0.0075F) < 1.0e-6F &&
                    std::fabs(points[2].z + 3.0F) < 1.0e-6F,
                "pixel (0,1) flip must match pinhole model");
            passed &= Expect(
                std::fabs(points[3].x - 0.02F) < 1.0e-6F &&
                    std::fabs(points[3].y + 0.01F) < 1.0e-6F &&
                    std::fabs(points[3].z + 4.0F) < 1.0e-6F,
                "pixel (1,1) flip must match pinhole model");
        }
    }
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const std::vector<float> range = {2.0F, 0.0F, -1.0F, nan, inf, 3.0F};
        std::vector<questlab::math::Vec3> points;
        std::string reason;
        passed &= Expect(
            questlab::fusion::StereoRangeToPoints(
                range, 6, 1, 100.0F, 100.0F, 3.0F, 0.0F, &points, &reason),
            "invalid ranges must be skipped, not fail");
        passed &= Expect(
            points.size() == 2U, "only two valid ranges must emit");
        if (points.size() == 2U) {
            passed &= Expect(
                std::fabs(points[0].z + 2.0F) < 1.0e-6F &&
                    std::fabs(points[1].z + 3.0F) < 1.0e-6F,
                "valid points must keep their ranges");
            for (const auto& point : points) {
                passed &= Expect(
                    !(point.x == 0.0F && point.y == 0.0F &&
                        point.z == 0.0F),
                    "invalid must never become a zero point");
            }
        }
    }
    {
        std::vector<questlab::math::Vec3> points;
        std::string reason;
        passed &= Expect(
            !questlab::fusion::StereoRangeToPoints(
                std::vector<float>{1.0F, 2.0F}, 2, 2, 100.0F, 100.0F, 1.0F,
                1.0F, &points, &reason),
            "size mismatch must fail");
        passed &= Expect(
            !reason.empty(), "size mismatch must carry a reason");
        reason.clear();
        passed &= Expect(
            !questlab::fusion::StereoRangeToPoints(
                std::vector<float>{1.0F}, 1, 1, 100.0F, 100.0F, 0.0F, 0.0F,
                nullptr, &reason),
            "null output must fail");
        passed &= Expect(!reason.empty(), "null output must carry a reason");
    }
    {
        std::vector<questlab::math::Vec3> points;
        std::string reason;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        passed &= Expect(
            !questlab::fusion::StereoRangeToPoints(
                std::vector<float>{1.0F}, 1, 1, 0.0F, 100.0F, 0.0F, 0.0F,
                &points, &reason),
            "zero fx must fail");
        passed &= Expect(!reason.empty(), "zero fx must carry a reason");
        reason.clear();
        passed &= Expect(
            !questlab::fusion::StereoRangeToPoints(
                std::vector<float>{1.0F}, 1, 1, 100.0F, -5.0F, 0.0F, 0.0F,
                &points, &reason),
            "negative fy must fail");
        reason.clear();
        passed &= Expect(
            !questlab::fusion::StereoRangeToPoints(
                std::vector<float>{1.0F}, 1, 1, nan, 100.0F, 0.0F, 0.0F,
                &points, &reason),
            "NaN fx must fail");
        reason.clear();
        passed &= Expect(
            !questlab::fusion::StereoRangeToPoints(
                std::vector<float>{1.0F}, 0, 1, 100.0F, 100.0F, 0.0F, 0.0F,
                &points, &reason),
            "zero width must fail");
    }

    if (passed) {
        std::cout << "detection fusion tests passed\n";
    }
    return passed ? 0 : 1;
}
