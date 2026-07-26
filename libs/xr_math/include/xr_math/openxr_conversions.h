#pragma once

#include "xr_math/xr_math.h"

#include <openxr/openxr.h>

#include <cmath>

namespace questlab::math {

inline Vec3 FromXr(const XrVector3f& vector) {
    return {vector.x, vector.y, vector.z};
}

inline Quat FromXr(const XrQuaternionf& quaternion) {
    return {
        quaternion.x,
        quaternion.y,
        quaternion.z,
        quaternion.w,
    };
}

inline Pose FromXr(const XrPosef& pose) {
    return {FromXr(pose.orientation), FromXr(pose.position)};
}

inline XrVector3f ToXr(const Vec3& vector) {
    return {vector.x, vector.y, vector.z};
}

inline XrQuaternionf ToXr(const Quat& quaternion) {
    return {
        quaternion.x,
        quaternion.y,
        quaternion.z,
        quaternion.w,
    };
}

inline XrPosef ToXr(const Pose& pose) {
    return {ToXr(pose.orientation), ToXr(pose.position)};
}

inline Mat4 VulkanProjectionFromFov(
    const XrFovf& fov,
    float nearDistance,
    float farDistance) {
    return VulkanProjectionFromTangents(
        std::tan(fov.angleLeft),
        std::tan(fov.angleRight),
        std::tan(fov.angleDown),
        std::tan(fov.angleUp),
        nearDistance,
        farDistance);
}

}  // namespace questlab::math
