#include "screen_corners/screen_corners.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using questlab::corners::Intrinsics;
using questlab::corners::Point2;
using questlab::corners::Pose;
using questlab::corners::Quad;

constexpr int kWidth = 640;
constexpr int kHeight = 480;

std::vector<uint8_t> MakeMask() {
    return std::vector<uint8_t>(
        static_cast<size_t>(kWidth) * kHeight, 0U);
}

void FillRect(
    std::vector<uint8_t>* mask, int left, int top, int right, int bottom) {
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            (*mask)[static_cast<size_t>(y) * kWidth + x] = 1U;
        }
    }
}

double Cross2(
    const Point2& origin, const Point2& a, const Point2& b) {
    return (static_cast<double>(a.x) - origin.x) *
               (static_cast<double>(b.y) - origin.y) -
           (static_cast<double>(a.y) - origin.y) *
               (static_cast<double>(b.x) - origin.x);
}

void FillConvexQuad(
    std::vector<uint8_t>* mask,
    const Point2 corners[4]) {
    int minimumX = static_cast<int>(corners[0].x);
    int maximumX = static_cast<int>(corners[0].x);
    int minimumY = static_cast<int>(corners[0].y);
    int maximumY = static_cast<int>(corners[0].y);
    for (int index = 1; index < 4; ++index) {
        minimumX = std::min(minimumX, static_cast<int>(corners[index].x));
        maximumX = std::max(maximumX, static_cast<int>(corners[index].x));
        minimumY = std::min(minimumY, static_cast<int>(corners[index].y));
        maximumY = std::max(maximumY, static_cast<int>(corners[index].y));
    }
    // Normalize winding so the half-plane test sign is known.
    double signedArea = 0.0;
    for (int index = 0; index < 4; ++index) {
        const Point2& a = corners[index];
        const Point2& b = corners[(index + 1) % 4];
        signedArea += static_cast<double>(a.x) * b.y -
                      static_cast<double>(b.x) * a.y;
    }
    const bool clockwise = signedArea < 0.0;
    for (int y = minimumY; y <= maximumY; ++y) {
        for (int x = minimumX; x <= maximumX; ++x) {
            const Point2 point{
                static_cast<float>(x) + 0.5F,
                static_cast<float>(y) + 0.5F};
            bool inside = true;
            for (int index = 0; index < 4; ++index) {
                const double cross = Cross2(
                    corners[index], corners[(index + 1) % 4], point);
                if (clockwise ? cross > 1e-9 : cross < -1e-9) {
                    inside = false;
                    break;
                }
            }
            if (inside && x >= 0 && y >= 0 && x < kWidth &&
                y < kHeight) {
                (*mask)[static_cast<size_t>(y) * kWidth + x] = 1U;
            }
        }
    }
}

void AssertCornerNear(
    const Point2& actual, float expectedX, float expectedY,
    float tolerance) {
    const double deltaX = actual.x - expectedX;
    const double deltaY = actual.y - expectedY;
    assert(std::sqrt(deltaX * deltaX + deltaY * deltaY) <= tolerance);
}

void TestAxisAlignedRectangle() {
    std::vector<uint8_t> mask = MakeMask();
    FillRect(&mask, 100, 80, 299, 199);  // 200x120, aspect 1.67
    Quad quad;
    std::string error;
    assert(questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    AssertCornerNear(quad.corners[0], 100.0F, 80.0F, 1.5F);
    AssertCornerNear(quad.corners[1], 299.0F, 80.0F, 1.5F);
    AssertCornerNear(quad.corners[2], 299.0F, 199.0F, 1.5F);
    AssertCornerNear(quad.corners[3], 100.0F, 199.0F, 1.5F);
    assert(quad.score > 0.9F && quad.score <= 1.0F);
}

void TestTiltedQuad() {
    std::vector<uint8_t> mask = MakeMask();
    const Point2 corners[4] = {
        {110.0F, 70.0F},
        {330.0F, 100.0F},
        {310.0F, 220.0F},
        {90.0F, 190.0F},
    };
    FillConvexQuad(&mask, corners);
    Quad quad;
    std::string error;
    assert(questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    AssertCornerNear(quad.corners[0], 110.0F, 70.0F, 3.0F);
    AssertCornerNear(quad.corners[1], 330.0F, 100.0F, 3.0F);
    AssertCornerNear(quad.corners[2], 310.0F, 220.0F, 3.0F);
    AssertCornerNear(quad.corners[3], 90.0F, 190.0F, 3.0F);
}

void TestLargestComponentWins() {
    std::vector<uint8_t> mask = MakeMask();
    FillRect(&mask, 40, 40, 239, 159);   // 200x120
    FillRect(&mask, 400, 300, 479, 349);  // 80x50
    Quad quad;
    std::string error;
    assert(questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    AssertCornerNear(quad.corners[0], 40.0F, 40.0F, 1.5F);
    AssertCornerNear(quad.corners[2], 239.0F, 159.0F, 1.5F);
}

void TestTriangleRejected() {
    std::vector<uint8_t> mask = MakeMask();
    // Right triangle, bounding box 220x140: aspect passes, 3 vertices.
    for (int y = 70; y <= 209; ++y) {
        const int extent = (y - 70) * 220 / 140;
        for (int x = 100; x <= 100 + extent; ++x) {
            mask[static_cast<size_t>(y) * kWidth + x] = 1U;
        }
    }
    Quad quad;
    std::string error;
    assert(!questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    assert(error.find("quadrilateral") != std::string::npos);
}

void TestAspectGateRejectsSquare() {
    std::vector<uint8_t> mask = MakeMask();
    FillRect(&mask, 200, 150, 349, 299);  // 150x150 square
    Quad quad;
    std::string error;
    assert(!questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    assert(error.find("aspect") != std::string::npos);
}

void TestForegroundShareGate() {
    std::vector<uint8_t> small = MakeMask();
    FillRect(&small, 10, 10, 39, 29);  // 30x20, 0.2% share
    Quad quad;
    std::string error;
    assert(!questlab::corners::FindScreenQuadFromMask(
        small.data(), kWidth, kHeight, &quad, &error));
    assert(error.find("foreground") != std::string::npos);

    std::vector<uint8_t> large = MakeMask();
    FillRect(&large, 5, 5, kWidth - 6, kHeight - 6);  // ~97% share
    assert(!questlab::corners::FindScreenQuadFromMask(
        large.data(), kWidth, kHeight, &quad, &error));
    assert(error.find("foreground") != std::string::npos);
}

void TestMinimumSizeGate() {
    constexpr int kSmallWidth = 200;
    constexpr int kSmallHeight = 200;
    std::vector<uint8_t> mask(
        static_cast<size_t>(kSmallWidth) * kSmallHeight, 0U);
    for (int y = 40; y <= 64; ++y) {
        for (int x = 30; x <= 59; ++x) {  // 30x25, aspect 1.2
            mask[static_cast<size_t>(y) * kSmallWidth + x] = 1U;
        }
    }
    Quad quad;
    std::string error;
    assert(!questlab::corners::FindScreenQuadFromMask(
        mask.data(), kSmallWidth, kSmallHeight, &quad, &error));
    assert(error.find("minimum size") != std::string::npos);
}

void TestEmptyAndInvalidInputs() {
    Quad quad;
    std::string error;
    assert(!questlab::corners::FindScreenQuadFromMask(
        nullptr, kWidth, kHeight, &quad, &error));
    std::vector<uint8_t> mask = MakeMask();
    assert(!questlab::corners::FindScreenQuadFromMask(
        mask.data(), 32, 32, &quad, &error));
    assert(!questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    assert(error.find("foreground") != std::string::npos);
}

// --- Pose -------------------------------------------------------------------

struct Rotation {
    double r[3][3];
};

Rotation Multiply(const Rotation& a, const Rotation& b) {
    Rotation result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double sum = 0.0;
            for (int inner = 0; inner < 3; ++inner) {
                sum += a.r[row][inner] * b.r[inner][column];
            }
            result.r[row][column] = sum;
        }
    }
    return result;
}

Rotation RotateX(double radians) {
    Rotation result{};
    result.r[0][0] = 1.0;
    result.r[1][1] = std::cos(radians);
    result.r[1][2] = -std::sin(radians);
    result.r[2][1] = std::sin(radians);
    result.r[2][2] = std::cos(radians);
    return result;
}

Rotation RotateY(double radians) {
    Rotation result{};
    result.r[0][0] = std::cos(radians);
    result.r[0][2] = std::sin(radians);
    result.r[1][1] = 1.0;
    result.r[2][0] = -std::sin(radians);
    result.r[2][2] = std::cos(radians);
    return result;
}

Rotation RotateZ(double radians) {
    Rotation result{};
    result.r[0][0] = std::cos(radians);
    result.r[0][1] = -std::sin(radians);
    result.r[1][0] = std::sin(radians);
    result.r[1][1] = std::cos(radians);
    result.r[2][2] = 1.0;
    return result;
}

// The upright-screen convention: object +Y up against camera +Y down.
Rotation UprightFlip() {
    Rotation result{};
    result.r[0][0] = 1.0;
    result.r[1][1] = -1.0;
    result.r[2][2] = -1.0;
    return result;
}

struct PoseSample {
    Rotation rotation;
    double position[3];
};

Quad ProjectCorners(
    const PoseSample& pose,
    const Intrinsics& intrinsics,
    float widthMeters,
    float heightMeters,
    bool quantize) {
    const double halfWidth = widthMeters * 0.5;
    const double halfHeight = heightMeters * 0.5;
    const double object[4][3] = {
        {-halfWidth, halfHeight, 0.0},
        {halfWidth, halfHeight, 0.0},
        {halfWidth, -halfHeight, 0.0},
        {-halfWidth, -halfHeight, 0.0},
    };
    Quad quad;
    for (int index = 0; index < 4; ++index) {
        double camera[3];
        for (int row = 0; row < 3; ++row) {
            camera[row] = pose.rotation.r[row][0] * object[index][0] +
                          pose.rotation.r[row][1] * object[index][1] +
                          pose.rotation.r[row][2] * object[index][2] +
                          pose.position[row];
        }
        assert(camera[2] > 0.0);
        double imageX = intrinsics.fx * camera[0] / camera[2] + intrinsics.cx;
        double imageY = intrinsics.fy * camera[1] / camera[2] + intrinsics.cy;
        if (quantize) {
            imageX = std::round(imageX);
            imageY = std::round(imageY);
        }
        quad.corners[index] = {
            static_cast<float>(imageX), static_cast<float>(imageY)};
    }
    return quad;
}

void AssertSameRotation(
    const std::array<float, 4>& quaternion,
    const Rotation& expected,
    double toleranceRadians) {
    // Rotation matrix from the quaternion, then the relative angle against
    // the expected matrix: robust across the trace-branch differences.
    const double x = quaternion[0];
    const double y = quaternion[1];
    const double z = quaternion[2];
    const double w = quaternion[3];
    const double actual[3][3] = {
        {1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),
         2.0 * (x * z + y * w)},
        {2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z),
         2.0 * (y * z - x * w)},
        {2.0 * (x * z - y * w), 2.0 * (y * z + x * w),
         1.0 - 2.0 * (x * x + y * y)},
    };
    double trace = 0.0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            trace += actual[row][column] * expected.r[row][column];
        }
    }
    const double cosine = std::clamp((trace - 1.0) * 0.5, -1.0, 1.0);
    const double angle = std::acos(cosine);
    assert(angle <= toleranceRadians);
}

void TestPoseRoundTrip() {
    const Intrinsics intrinsics{866.1F, 866.1F, 640.5F, 640.5F};
    constexpr float kScreenWidthMeters = 0.55F;
    constexpr float kScreenHeightMeters = 0.32F;

    std::vector<PoseSample> samples;
    {
        PoseSample front;
        front.rotation = UprightFlip();
        front.position[0] = 0.0;
        front.position[1] = 0.0;
        front.position[2] = 1.5;
        samples.push_back(front);
    }
    {
        PoseSample yawed;
        yawed.rotation = Multiply(UprightFlip(), RotateY(0.44));  // 25 deg
        yawed.position[0] = 0.3;
        yawed.position[1] = -0.4;
        yawed.position[2] = 2.0;
        samples.push_back(yawed);
    }
    {
        PoseSample tilted;
        tilted.rotation = Multiply(
            Multiply(Multiply(UprightFlip(), RotateY(-0.35)), RotateX(0.26)),
            RotateZ(0.17));
        tilted.position[0] = -0.5;
        tilted.position[1] = 0.2;
        tilted.position[2] = 3.0;
        samples.push_back(tilted);
    }

    for (const PoseSample& sample : samples) {
        for (const bool quantize : {false, true}) {
            const Quad quad = ProjectCorners(
                sample, intrinsics, kScreenWidthMeters, kScreenHeightMeters,
                quantize);
            Pose pose;
            float reprojection = 0.0F;
            std::string error;
            assert(questlab::corners::SolveQuadPose(
                quad, intrinsics, kScreenWidthMeters, kScreenHeightMeters,
                &pose, &reprojection, &error));
            const double tolerance = quantize ? 0.12 : 1e-3;
            for (int axis = 0; axis < 3; ++axis) {
                assert(
                    std::abs(pose.position[axis] - sample.position[axis]) <=
                    tolerance);
            }
            AssertSameRotation(
                pose.quat, sample.rotation,
                quantize ? 0.10 : 0.002);
            assert(reprojection <= (quantize ? 3.0F : 0.05F));
        }
    }
}

void TestPoseRefusals() {
    const Intrinsics intrinsics{866.1F, 866.1F, 640.5F, 640.5F};
    Quad quad;
    quad.corners = {{
        {100.0F, 100.0F},
        {300.0F, 110.0F},
        {290.0F, 220.0F},
        {90.0F, 210.0F},
    }};
    Pose pose;
    std::string error;
    assert(questlab::corners::SolveQuadPose(
        quad, intrinsics, 0.55F, 0.32F, &pose, nullptr, &error));

    Quad nanQuad = quad;
    nanQuad.corners[2].x = std::nanf("");
    assert(!questlab::corners::SolveQuadPose(
        nanQuad, intrinsics, 0.55F, 0.32F, &pose, nullptr, &error));
    assert(error.find("non-finite") != std::string::npos);

    const Intrinsics badIntrinsics{0.0F, 0.0F, 0.0F, 0.0F};
    assert(!questlab::corners::SolveQuadPose(
        quad, badIntrinsics, 0.55F, 0.32F, &pose, nullptr, &error));
    assert(error.find("invalid pose input") != std::string::npos);

    assert(!questlab::corners::SolveQuadPose(
        quad, intrinsics, 0.0F, 0.32F, &pose, nullptr, &error));
}

void TestMaskPoseEndToEnd() {
    // A perspective-projected 16:9-ish screen rasterized as a mask, then
    // the full mask -> quad -> pose path (intrinsics match the 640x480
    // test image).
    const Intrinsics intrinsics{433.05F, 433.05F, 320.25F, 240.25F};
    PoseSample sample;
    sample.rotation = Multiply(UprightFlip(), RotateY(0.3));
    sample.position[0] = 0.1;
    sample.position[1] = -0.05;
    sample.position[2] = 1.8;
    Quad projected = ProjectCorners(
        sample, intrinsics, 0.55F, 0.32F, false);
    std::vector<uint8_t> mask = MakeMask();
    const Point2 corners[4] = {
        projected.corners[0], projected.corners[1], projected.corners[2],
        projected.corners[3]};
    FillConvexQuad(&mask, corners);

    Quad quad;
    std::string error;
    assert(questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    Pose pose;
    float reprojection = 0.0F;
    assert(questlab::corners::SolveQuadPose(
        quad, intrinsics, 0.55F, 0.32F, &pose, &reprojection, &error));
    for (int axis = 0; axis < 3; ++axis) {
        assert(std::abs(pose.position[axis] - sample.position[axis]) <= 0.03);
    }
    assert(reprojection <= 3.0F);
}

void TestYawSignNear180Degrees() {
    // Regression for the Shepperd bug: at trace ~= -1 the old
    // signless fallback mirrored yaw (40 deg error, ~0 reproj).
    const Intrinsics intrinsics{866.1F, 866.1F, 640.5F, 640.5F};
    for (const double yawDegrees : {-20.0, 20.0}) {
        PoseSample sample;
        sample.rotation = Multiply(
            UprightFlip(), RotateY(yawDegrees * M_PI / 180.0));
        sample.position[0] = 0.1;
        sample.position[1] = 0.05;
        sample.position[2] = 2.0;
        const Quad quad = ProjectCorners(
            sample, intrinsics, 0.55F, 0.32F, false);
        Pose pose;
        float reprojection = 0.0F;
        std::string error;
        assert(questlab::corners::SolveQuadPose(
            quad, intrinsics, 0.55F, 0.32F, &pose, &reprojection, &error));
        AssertSameRotation(pose.quat, sample.rotation, 0.002);
        assert(reprojection <= 0.05F);
        // X-axis sign must follow the yaw sign, not mirror it.
        const double x = pose.quat[0];
        const double y = pose.quat[1];
        const double z = pose.quat[2];
        const double w = pose.quat[3];
        const double solvedXx = 1.0 - 2.0 * (y * y + z * z);
        const double solvedZx = 2.0 * (x * z - y * w);
        const double expectedSign = yawDegrees > 0.0 ? 1.0 : -1.0;
        // True X axis is (cos yaw, 0, +/-sin yaw) up to the flip; the
        // z-component sign must match sin(yaw).
        assert(solvedZx * expectedSign > 0.0);
        (void)solvedXx;
    }
}

void TestDistortionAwareSolve() {
    // Corners distorted with Brown-Conrady must solve back to the true
    // pose when the same model is supplied; a pinhole solve is biased.
    Intrinsics intrinsics{600.0F, 600.0F, 320.0F, 240.0F};
    intrinsics.skew = 1.5F;
    intrinsics.distortion = {-0.22F, 0.05F, 0.0F, 0.001F, -0.001F};
    PoseSample sample;
    sample.rotation = Multiply(UprightFlip(), RotateY(0.3));
    sample.position[0] = 0.1;
    sample.position[1] = -0.05;
    sample.position[2] = 2.0;
    const double halfW = 0.275;
    const double halfH = 0.16;
    const double object[4][2] = {
        {-halfW, halfH}, {halfW, halfH}, {halfW, -halfH}, {-halfW, -halfH}};
    Quad quad;
    for (int index = 0; index < 4; ++index) {
        double camera[3] = {0.0, 0.0, 0.0};
        for (int row = 0; row < 3; ++row) {
            camera[row] = sample.rotation.r[row][0] * object[index][0] +
                          sample.rotation.r[row][1] * object[index][1] +
                          sample.position[row];
        }
        const double xn = camera[0] / camera[2];
        const double yn = camera[1] / camera[2];
        const double r2 = xn * xn + yn * yn;
        const double r4 = r2 * r2;
        const double radial = 1.0 + intrinsics.distortion[0] * r2 +
                              intrinsics.distortion[1] * r4;
        const double xd = xn * radial + 2.0 * intrinsics.distortion[3] * xn * yn +
                          intrinsics.distortion[4] * (r2 + 2.0 * xn * xn);
        const double yd = yn * radial + intrinsics.distortion[3] * (r2 + 2.0 * yn * yn) +
                          2.0 * intrinsics.distortion[4] * xn * yn;
        quad.corners[index] = {
            static_cast<float>(intrinsics.fx * xd + intrinsics.skew * yd + intrinsics.cx),
            static_cast<float>(intrinsics.fy * yd + intrinsics.cy)};
    }
    Pose pose;
    float reprojection = 0.0F;
    std::string error;
    assert(questlab::corners::SolveQuadPose(
        quad, intrinsics, 0.55F, 0.32F, &pose, &reprojection, &error));
    AssertSameRotation(pose.quat, sample.rotation, 0.01);
    assert(reprojection <= 0.5F);
    for (int axis = 0; axis < 3; ++axis) {
        assert(std::abs(pose.position[axis] - sample.position[axis]) <= 0.01);
    }
}

void TestBorderQuadRejected() {
    std::vector<uint8_t> mask = MakeMask();
    FillRect(&mask, 0, 80, 299, 199);  // touches the left border
    Quad quad;
    std::string error;
    assert(!questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
    assert(error.find("border") != std::string::npos);
}

void TestPortraitAspectPasses() {
    // 121x241 portrait: longer/shorter = 1.99, must pass; the old
    // width/height gate rejected every portrait quad.
    std::vector<uint8_t> mask = MakeMask();
    FillRect(&mask, 250, 100, 370, 340);
    Quad quad;
    std::string error;
    assert(questlab::corners::FindScreenQuadFromMask(
        mask.data(), kWidth, kHeight, &quad, &error));
}

}  // namespace

int main() {
    TestAxisAlignedRectangle();
    TestTiltedQuad();
    TestLargestComponentWins();
    TestTriangleRejected();
    TestAspectGateRejectsSquare();
    TestForegroundShareGate();
    TestMinimumSizeGate();
    TestEmptyAndInvalidInputs();
    TestPoseRoundTrip();
    TestPoseRefusals();
    TestMaskPoseEndToEnd();
    TestYawSignNear180Degrees();
    TestDistortionAwareSolve();
    TestBorderQuadRejected();
    TestPortraitAspectPasses();
    return 0;
}
