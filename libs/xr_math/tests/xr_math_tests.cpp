#include "xr_math/xr_math.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        ++failures;
    }
}

questlab::math::Quat QuarterTurnAroundY() {
    constexpr float kHalfSqrtTwo = 0.70710678118F;
    return {0.0F, kHalfSqrtTwo, 0.0F, kHalfSqrtTwo};
}

void TestVectorAndQuaternionOperations() {
    using namespace questlab::math;
    Expect(
        NearlyEqual(
            Cross({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
            {0.0F, 0.0F, 1.0F}),
        "X cross Y must point along positive Z");
    Vec3 zero{};
    Expect(!Normalize(&zero), "zero vector normalization must fail");
    Expect(
        NearlyEqual(
            Rotate(QuarterTurnAroundY(), {0.0F, 0.0F, -1.0F}),
            {-1.0F, 0.0F, 0.0F},
            1.0e-5F),
        "positive Y quarter-turn must rotate forward toward negative X");
}

void TestRotationFromTo() {
    using namespace questlab::math;
    const auto expectRotation = [](const Vec3& from, const Vec3& to) {
        Quat rotation;
        Expect(
            RotationFromTo(from, to, &rotation),
            "valid vectors must produce a rotation");
        Vec3 expected = to;
        Normalize(&expected);
        Expect(
            NearlyEqual(Rotate(rotation, from), expected, 1.0e-5F),
            "from-to rotation must align its input with the target");
    };

    expectRotation({0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, -1.0F});
    expectRotation({0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, 1.0F});
    expectRotation({0.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 0.0F});
    Vec3 arbitraryFrom{2.0F, -1.0F, 3.0F};
    Normalize(&arbitraryFrom);
    expectRotation(arbitraryFrom, {-0.5F, 4.0F, 1.0F});

    Quat rotation;
    Expect(
        !RotationFromTo({}, {1.0F, 0.0F, 0.0F}, &rotation),
        "zero source vector must fail");
    Expect(
        !RotationFromTo(
            {1.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F},
            nullptr),
        "null rotation output must fail");
}

void TestPoseCompositionAndInverse() {
    using namespace questlab::math;
    const Pose worldFromParent{
        QuarterTurnAroundY(),
        {2.0F, 1.0F, -3.0F},
    };
    const Pose parentFromChild{
        IdentityQuat(),
        {0.0F, 0.0F, -2.0F},
    };
    const Pose worldFromChild =
        Compose(worldFromParent, parentFromChild);
    const Vec3 worldPoint =
        TransformPoint(worldFromChild, {0.25F, 0.0F, 0.0F});
    const Vec3 recoveredPoint = TransformPoint(
        InverseRigid(worldFromChild), worldPoint);
    Expect(
        NearlyEqual(recoveredPoint, {0.25F, 0.0F, 0.0F}, 1.0e-5F),
        "pose inverse must recover a child-space point");

    const Mat4 worldFromChildMatrix = PoseMatrix(worldFromChild);
    Expect(
        NearlyEqual(
            Multiply(
                worldFromChildMatrix,
                InverseRigid(worldFromChildMatrix)),
            IdentityMatrix(),
            1.0e-5F),
        "rigid matrix multiplied by its inverse must be identity");
}

void TestProjectionDepthRange() {
    using namespace questlab::math;
    constexpr float kNear = 0.1F;
    constexpr float kFar = 10.0F;
    const Mat4 projection = VulkanProjectionFromTangents(
        -1.0F, 1.0F, -1.0F, 1.0F, kNear, kFar);

    const auto projectedDepth = [&projection](float viewZ) {
        const float clipZ =
            projection.values[10] * viewZ + projection.values[14];
        const float clipW = projection.values[11] * viewZ;
        return clipZ / clipW;
    };
    Expect(
        NearlyEqual(projectedDepth(-kNear), 0.0F, 1.0e-5F),
        "Vulkan near plane must map to zero depth");
    Expect(
        NearlyEqual(projectedDepth(-kFar), 1.0F, 1.0e-5F),
        "Vulkan far plane must map to one depth");
}

}  // namespace

int main() {
    TestVectorAndQuaternionOperations();
    TestRotationFromTo();
    TestPoseCompositionAndInverse();
    TestProjectionDepthRange();
    if (failures != 0) {
        std::fprintf(stderr, "%d xr_math test(s) failed\n", failures);
        return 1;
    }
    std::puts("xr_math tests passed");
    return 0;
}
