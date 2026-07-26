#pragma once

#include <array>

namespace questlab::math {

constexpr float kDefaultEpsilon = 1.0e-6F;

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

struct Pose {
    Quat orientation{};
    Vec3 position{};
};

struct Mat4 {
    // Column-major storage. A transformed point is matrix * columnVector.
    std::array<float, 16> values{};
};

Vec3 Add(const Vec3& left, const Vec3& right);
Vec3 Subtract(const Vec3& left, const Vec3& right);
Vec3 Scale(const Vec3& vector, float scalar);
float Dot(const Vec3& left, const Vec3& right);
Vec3 Cross(const Vec3& left, const Vec3& right);
float Length(const Vec3& vector);
bool Normalize(Vec3* vector, float epsilon = kDefaultEpsilon);

Quat IdentityQuat();
Quat Conjugate(const Quat& quaternion);
Quat Multiply(const Quat& left, const Quat& right);
bool Normalize(Quat* quaternion, float epsilon = kDefaultEpsilon);
Vec3 Rotate(const Quat& quaternion, const Vec3& vector);

Pose IdentityPose();
Pose Compose(const Pose& parentFromMiddle, const Pose& middleFromChild);
Pose InverseRigid(const Pose& parentFromChild);
Vec3 TransformPoint(const Pose& parentFromChild, const Vec3& pointInChild);
Vec3 TransformDirection(
    const Pose& parentFromChild,
    const Vec3& directionInChild);

Mat4 IdentityMatrix();
Mat4 TranslationMatrix(const Vec3& translation);
Mat4 RotationMatrix(const Quat& rotation);
Mat4 PoseMatrix(const Pose& parentFromChild);
Mat4 Multiply(const Mat4& left, const Mat4& right);
Mat4 InverseRigid(const Mat4& matrix);
Vec3 TransformPoint(const Mat4& matrix, const Vec3& point);
Vec3 TransformDirection(const Mat4& matrix, const Vec3& direction);

// OpenXR FOV projection for Vulkan's [0, 1] depth range. The renderer uses a
// negative-height viewport, so this matrix intentionally does not flip Y.
Mat4 VulkanProjectionFromTangents(
    float tanLeft,
    float tanRight,
    float tanDown,
    float tanUp,
    float nearDistance,
    float farDistance);

bool NearlyEqual(float left, float right, float epsilon = kDefaultEpsilon);
bool NearlyEqual(
    const Vec3& left,
    const Vec3& right,
    float epsilon = kDefaultEpsilon);
bool NearlyEqual(
    const Quat& left,
    const Quat& right,
    float epsilon = kDefaultEpsilon);
bool NearlyEqual(
    const Mat4& left,
    const Mat4& right,
    float epsilon = kDefaultEpsilon);

}  // namespace questlab::math
