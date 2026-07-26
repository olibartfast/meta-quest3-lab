#include "xr_math/xr_math.h"

#include <cmath>

namespace questlab::math {

Vec3 Add(const Vec3& left, const Vec3& right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 Subtract(const Vec3& left, const Vec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 Scale(const Vec3& vector, float scalar) {
    return {vector.x * scalar, vector.y * scalar, vector.z * scalar};
}

float Dot(const Vec3& left, const Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 Cross(const Vec3& left, const Vec3& right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

float Length(const Vec3& vector) {
    return std::sqrt(Dot(vector, vector));
}

bool Normalize(Vec3* vector, float epsilon) {
    if (vector == nullptr) {
        return false;
    }
    const float length = Length(*vector);
    if (!std::isfinite(length) || length <= epsilon) {
        return false;
    }
    *vector = Scale(*vector, 1.0F / length);
    return true;
}

Quat IdentityQuat() {
    return {};
}

Quat Conjugate(const Quat& quaternion) {
    return {
        -quaternion.x,
        -quaternion.y,
        -quaternion.z,
        quaternion.w,
    };
}

Quat Multiply(const Quat& left, const Quat& right) {
    return {
        left.w * right.x + left.x * right.w +
            left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z +
            left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y -
            left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x -
            left.y * right.y - left.z * right.z,
    };
}

bool Normalize(Quat* quaternion, float epsilon) {
    if (quaternion == nullptr) {
        return false;
    }
    const float squaredLength =
        quaternion->x * quaternion->x +
        quaternion->y * quaternion->y +
        quaternion->z * quaternion->z +
        quaternion->w * quaternion->w;
    if (!std::isfinite(squaredLength) ||
        squaredLength <= epsilon * epsilon) {
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(squaredLength);
    quaternion->x *= inverseLength;
    quaternion->y *= inverseLength;
    quaternion->z *= inverseLength;
    quaternion->w *= inverseLength;
    return true;
}

Vec3 Rotate(const Quat& quaternion, const Vec3& vector) {
    const Vec3 imaginary{
        quaternion.x,
        quaternion.y,
        quaternion.z,
    };
    const Vec3 twiceCross = Scale(Cross(imaginary, vector), 2.0F);
    return Add(
        vector,
        Add(
            Scale(twiceCross, quaternion.w),
            Cross(imaginary, twiceCross)));
}

Pose IdentityPose() {
    return {};
}

Pose Compose(const Pose& parentFromMiddle, const Pose& middleFromChild) {
    return {
        Multiply(
            parentFromMiddle.orientation,
            middleFromChild.orientation),
        Add(
            parentFromMiddle.position,
            Rotate(
                parentFromMiddle.orientation,
                middleFromChild.position)),
    };
}

Pose InverseRigid(const Pose& parentFromChild) {
    Quat inverseOrientation = Conjugate(parentFromChild.orientation);
    Normalize(&inverseOrientation);
    return {
        inverseOrientation,
        Rotate(
            inverseOrientation,
            Scale(parentFromChild.position, -1.0F)),
    };
}

Vec3 TransformPoint(const Pose& parentFromChild, const Vec3& pointInChild) {
    return Add(
        parentFromChild.position,
        Rotate(parentFromChild.orientation, pointInChild));
}

Vec3 TransformDirection(
    const Pose& parentFromChild,
    const Vec3& directionInChild) {
    return Rotate(parentFromChild.orientation, directionInChild);
}

Mat4 IdentityMatrix() {
    Mat4 matrix;
    matrix.values[0] = 1.0F;
    matrix.values[5] = 1.0F;
    matrix.values[10] = 1.0F;
    matrix.values[15] = 1.0F;
    return matrix;
}

Mat4 TranslationMatrix(const Vec3& translation) {
    Mat4 matrix = IdentityMatrix();
    matrix.values[12] = translation.x;
    matrix.values[13] = translation.y;
    matrix.values[14] = translation.z;
    return matrix;
}

Mat4 RotationMatrix(const Quat& rotation) {
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    Mat4 matrix = IdentityMatrix();
    matrix.values[0] = 1.0F - 2.0F * (yy + zz);
    matrix.values[1] = 2.0F * (xy + wz);
    matrix.values[2] = 2.0F * (xz - wy);
    matrix.values[4] = 2.0F * (xy - wz);
    matrix.values[5] = 1.0F - 2.0F * (xx + zz);
    matrix.values[6] = 2.0F * (yz + wx);
    matrix.values[8] = 2.0F * (xz + wy);
    matrix.values[9] = 2.0F * (yz - wx);
    matrix.values[10] = 1.0F - 2.0F * (xx + yy);
    return matrix;
}

Mat4 PoseMatrix(const Pose& parentFromChild) {
    Mat4 matrix = RotationMatrix(parentFromChild.orientation);
    matrix.values[12] = parentFromChild.position.x;
    matrix.values[13] = parentFromChild.position.y;
    matrix.values[14] = parentFromChild.position.z;
    return matrix;
}

Mat4 Multiply(const Mat4& left, const Mat4& right) {
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (int element = 0; element < 4; ++element) {
                value +=
                    left.values[element * 4 + row] *
                    right.values[column * 4 + element];
            }
            result.values[column * 4 + row] = value;
        }
    }
    return result;
}

Mat4 InverseRigid(const Mat4& matrix) {
    Mat4 inverse = IdentityMatrix();
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            inverse.values[column * 4 + row] =
                matrix.values[row * 4 + column];
        }
    }
    const Vec3 translation{
        matrix.values[12],
        matrix.values[13],
        matrix.values[14],
    };
    const Vec3 inverseTranslation = TransformDirection(
        inverse,
        Scale(translation, -1.0F));
    inverse.values[12] = inverseTranslation.x;
    inverse.values[13] = inverseTranslation.y;
    inverse.values[14] = inverseTranslation.z;
    return inverse;
}

Vec3 TransformPoint(const Mat4& matrix, const Vec3& point) {
    return {
        matrix.values[0] * point.x +
            matrix.values[4] * point.y +
            matrix.values[8] * point.z +
            matrix.values[12],
        matrix.values[1] * point.x +
            matrix.values[5] * point.y +
            matrix.values[9] * point.z +
            matrix.values[13],
        matrix.values[2] * point.x +
            matrix.values[6] * point.y +
            matrix.values[10] * point.z +
            matrix.values[14],
    };
}

Vec3 TransformDirection(const Mat4& matrix, const Vec3& direction) {
    return {
        matrix.values[0] * direction.x +
            matrix.values[4] * direction.y +
            matrix.values[8] * direction.z,
        matrix.values[1] * direction.x +
            matrix.values[5] * direction.y +
            matrix.values[9] * direction.z,
        matrix.values[2] * direction.x +
            matrix.values[6] * direction.y +
            matrix.values[10] * direction.z,
    };
}

Mat4 VulkanProjectionFromTangents(
    float tanLeft,
    float tanRight,
    float tanDown,
    float tanUp,
    float nearDistance,
    float farDistance) {
    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;
    Mat4 matrix;
    matrix.values[0] = 2.0F / tanWidth;
    matrix.values[5] = 2.0F / tanHeight;
    matrix.values[8] = (tanRight + tanLeft) / tanWidth;
    matrix.values[9] = (tanUp + tanDown) / tanHeight;
    matrix.values[10] = -farDistance / (farDistance - nearDistance);
    matrix.values[11] = -1.0F;
    matrix.values[14] =
        -(farDistance * nearDistance) /
        (farDistance - nearDistance);
    return matrix;
}

bool NearlyEqual(float left, float right, float epsilon) {
    return std::fabs(left - right) <= epsilon;
}

bool NearlyEqual(const Vec3& left, const Vec3& right, float epsilon) {
    return NearlyEqual(left.x, right.x, epsilon) &&
        NearlyEqual(left.y, right.y, epsilon) &&
        NearlyEqual(left.z, right.z, epsilon);
}

bool NearlyEqual(const Quat& left, const Quat& right, float epsilon) {
    return NearlyEqual(left.x, right.x, epsilon) &&
        NearlyEqual(left.y, right.y, epsilon) &&
        NearlyEqual(left.z, right.z, epsilon) &&
        NearlyEqual(left.w, right.w, epsilon);
}

bool NearlyEqual(const Mat4& left, const Mat4& right, float epsilon) {
    for (size_t index = 0; index < left.values.size(); ++index) {
        if (!NearlyEqual(
                left.values[index],
                right.values[index],
                epsilon)) {
            return false;
        }
    }
    return true;
}

}  // namespace questlab::math
