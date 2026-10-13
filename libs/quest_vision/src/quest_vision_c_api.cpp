// Unity-marshallable C ABI over the C++ detection-fusion pipeline.
// Never throws across the boundary: every failure maps to a return code
// plus an optional NUL-terminated reason string.

#include "quest_vision/quest_vision_c_api.h"

#include <cmath>
#include <cstring>

#include "detection_fusion/detection_fusion.h"

namespace {

questlab::math::Vec3 ToCpp(const QvVec3& value) {
    return {value.x, value.y, value.z};
}

QvVec3 FromCpp(const questlab::math::Vec3& value) {
    return {value.x, value.y, value.z};
}

QvQuat QuatFromCpp(const questlab::math::Quat& value) {
    return {value.x, value.y, value.z, value.w};
}

questlab::fusion::DetectionFrustum FrustumToCpp(const QvFrustum& frustum) {
    questlab::fusion::DetectionFrustum cpp;
    cpp.originInLocal = ToCpp(frustum.origin);
    for (size_t i = 0; i < 4; ++i) {
        cpp.cornerDirectionsInLocal[i] = ToCpp(frustum.corners[i]);
    }
    cpp.centerDirectionInLocal = ToCpp(frustum.centerDir);
    return cpp;
}

questlab::fusion::FusionParameters ParamsToCpp(const QvFusionParams& params) {
    questlab::fusion::FusionParameters cpp;
    cpp.borderInsetFraction = params.borderInsetFraction;
    cpp.foregroundSampleShare = params.foregroundSampleShare;
    cpp.clusterToleranceMeters = params.clusterToleranceMeters;
    cpp.extentLowPercentile = params.extentLowPercentile;
    cpp.extentHighPercentile = params.extentHighPercentile;
    cpp.ransacIterations = params.ransacIterations;
    cpp.ransacInlierMeters = params.ransacInlierMeters;
    cpp.ransacMinimumInlierRatio = params.ransacMinimumInlierRatio;
    cpp.requireMeasuredOrientation = params.requireMeasuredOrientation != 0;
    cpp.minimumSampleCount = params.minimumSampleCount;
    cpp.minimumInlierRatio = params.minimumInlierRatio;
    cpp.minimumRangeMeters = params.minimumRangeMeters;
    cpp.maximumRangeMeters = params.maximumRangeMeters;
    cpp.upInLocal = ToCpp(params.up);
    return cpp;
}

void WriteReason(
    const std::string& message,
    char* reason,
    int32_t reasonSize) {
    if (reason == nullptr || reasonSize <= 0) {
        return;
    }
    const size_t capacity = static_cast<size_t>(reasonSize) - 1U;
    const size_t count = message.size() < capacity ? message.size() : capacity;
    std::memcpy(reason, message.data(), count);
    reason[count] = '\0';
}

struct FusionSession {
    questlab::fusion::FusionParameters params;
    std::string lastError;
};

void FillResult(
    const questlab::fusion::FusedBox& box,
    QvFusedBox* outBox,
    float* poseMatrix) {
    if (outBox != nullptr) {
        outBox->center = FromCpp(box.centerInLocal);
        outBox->halfExtents = FromCpp(box.halfExtents);
        outBox->orientation = QuatFromCpp(box.orientationInLocal);
        outBox->rangeMeters = box.representativeRangeMeters;
        outBox->depthAssumed = box.depthExtentIsAssumed ? 1 : 0;
        outBox->orientationMeasured = box.orientationIsMeasured ? 1 : 0;
        outBox->planeInlierRatio = box.planeInlierRatio;
        outBox->sampleCount = box.sampleCount;
        outBox->inlierRatio = box.inlierRatio;
        outBox->confidence = box.confidence;
        outBox->rangeSource = static_cast<int32_t>(box.rangeSource);
    }
    if (poseMatrix != nullptr) {
        // Column-major 4x4: rotation from the box quaternion, translation
        // from the centre. Unity reads it column by column.
        const questlab::math::Pose pose{
            box.orientationInLocal, box.centerInLocal};
        const questlab::math::Mat4 matrix =
            questlab::math::PoseMatrix(pose);
        for (size_t i = 0; i < 16; ++i) {
            poseMatrix[i] = matrix.values[i];
        }
    }
}

int32_t FuseImpl(
    const questlab::fusion::FusionParameters& params,
    const QvFrustum* frustum,
    const QvVec3* points,
    int32_t pointCount,
    const QvSizePrior* prior,
    QvFusedBox* outBox,
    float* poseMatrix,
    std::string* error,
    char* reason,
    int32_t reasonSize) {
    if (frustum == nullptr || prior == nullptr || outBox == nullptr ||
        pointCount < 0 || (points == nullptr && pointCount > 0)) {
        if (error != nullptr) {
            *error = "invalid argument";
        }
        WriteReason("invalid argument", reason, reasonSize);
        return QV_BAD_ARGUMENT;
    }
    try {
        const questlab::fusion::DetectionFrustum cppFrustum =
            FrustumToCpp(*frustum);
        std::vector<questlab::math::Vec3> cppPoints;
        cppPoints.reserve(static_cast<size_t>(pointCount));
        for (int32_t i = 0; i < pointCount; ++i) {
            cppPoints.push_back(ToCpp(points[i]));
        }
        questlab::fusion::ObjectSizePrior cppPrior;
        cppPrior.valid = prior->valid != 0;
        cppPrior.depthMeters = prior->depthMeters;
        questlab::fusion::FusedBox box;
        std::string failure;
        if (!questlab::fusion::FuseDetection(
                cppFrustum, cppPoints, params, cppPrior, &box, &failure)) {
            if (error != nullptr) {
                *error = failure;
            }
            WriteReason(failure, reason, reasonSize);
            return QV_REFUSED;
        }
        FillResult(box, outBox, poseMatrix);
        if (error != nullptr) {
            error->clear();
        }
        WriteReason("", reason, reasonSize);
        return QV_OK;
    } catch (...) {
        if (error != nullptr) {
            *error = "internal error";
        }
        WriteReason("internal error", reason, reasonSize);
        return QV_INTERNAL_ERROR;
    }
}

}  // namespace

extern "C" {

QV_EXPORT const char* QvVersion(void) {
    return "quest_vision 1.0.0";
}

QV_EXPORT QvFusionHandle QvCreate(void) {
    try {
        return new FusionSession();
    } catch (...) {
        return nullptr;
    }
}

QV_EXPORT void QvDestroy(QvFusionHandle handle) {
    delete static_cast<FusionSession*>(handle);
}

QV_EXPORT int32_t QvSetParams(
    QvFusionHandle handle,
    const QvFusionParams* params) {
    FusionSession* session = static_cast<FusionSession*>(handle);
    if (session == nullptr || params == nullptr) {
        return QV_BAD_ARGUMENT;
    }
    try {
        session->params = ParamsToCpp(*params);
        session->lastError.clear();
        return QV_OK;
    } catch (...) {
        return QV_INTERNAL_ERROR;
    }
}

QV_EXPORT int32_t QvFuse(
    QvFusionHandle handle,
    const QvFrustum* frustum,
    const QvVec3* points,
    int32_t pointCount,
    const QvSizePrior* prior,
    QvFuseResult* outResult,
    char* reason,
    int32_t reasonSize) {
    FusionSession* session = static_cast<FusionSession*>(handle);
    if (session == nullptr || outResult == nullptr) {
        WriteReason("invalid argument", reason, reasonSize);
        return session == nullptr ? QV_BAD_HANDLE : QV_BAD_ARGUMENT;
    }
    return FuseImpl(
        session->params, frustum, points, pointCount, prior,
        &outResult->box, outResult->poseMatrix, &session->lastError, reason,
        reasonSize);
}

QV_EXPORT const char* QvGetLastError(QvFusionHandle handle) {
    const FusionSession* session =
        static_cast<const FusionSession*>(handle);
    if (session == nullptr) {
        return "invalid handle";
    }
    return session->lastError.c_str();
}

QV_EXPORT void QvFusionParamsDefault(QvFusionParams* params) {
    if (params == nullptr) {
        return;
    }
    const questlab::fusion::FusionParameters cpp;
    params->borderInsetFraction = cpp.borderInsetFraction;
    params->foregroundSampleShare = cpp.foregroundSampleShare;
    params->clusterToleranceMeters = cpp.clusterToleranceMeters;
    params->extentLowPercentile = cpp.extentLowPercentile;
    params->extentHighPercentile = cpp.extentHighPercentile;
    params->ransacIterations = cpp.ransacIterations;
    params->ransacInlierMeters = cpp.ransacInlierMeters;
    params->ransacMinimumInlierRatio = cpp.ransacMinimumInlierRatio;
    params->requireMeasuredOrientation =
        cpp.requireMeasuredOrientation ? 1 : 0;
    params->minimumSampleCount = cpp.minimumSampleCount;
    params->minimumInlierRatio = cpp.minimumInlierRatio;
    params->minimumRangeMeters = cpp.minimumRangeMeters;
    params->maximumRangeMeters = cpp.maximumRangeMeters;
    params->up = FromCpp(cpp.upInLocal);
}

QV_EXPORT int32_t QvFuseDetection(
    const QvFrustum* frustum,
    const QvVec3* points,
    int32_t pointCount,
    const QvFusionParams* params,
    const QvSizePrior* prior,
    QvFusedBox* outBox,
    char* reason,
    int32_t reasonSize) {
    if (params == nullptr) {
        WriteReason("invalid argument", reason, reasonSize);
        return QV_BAD_ARGUMENT;
    }
    std::string ignored;
    return FuseImpl(
        ParamsToCpp(*params), frustum, points, pointCount, prior, outBox,
        nullptr, &ignored, reason, reasonSize);
}

QV_EXPORT int32_t QvPointInsideFrustum(
    const QvFrustum* frustum,
    const QvVec3* point,
    float borderInsetFraction) {
    if (frustum == nullptr || point == nullptr) {
        return QV_BAD_ARGUMENT;
    }
    try {
        return questlab::fusion::PointInsideFrustum(
                   FrustumToCpp(*frustum), ToCpp(*point),
                   borderInsetFraction)
            ? 1
            : 0;
    } catch (...) {
        return QV_INTERNAL_ERROR;
    }
}

}  // extern "C"
