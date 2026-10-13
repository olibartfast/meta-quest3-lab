#pragma once

// C ABI of the quest_vision shared library, designed for Unity P/Invoke
// marshalling: plain structs of floats/ints only, opaque error codes, no
// exceptions or C++ types across the boundary. Include from C or C#
// (see unity/QuestVisionBridge.cs for the C# mirror).

#include <stdint.h>

#ifdef __cplusplus
#define QV_API_BEGIN \
    extern "C" {
#define QV_API_END \
    }
#else
#define QV_API_BEGIN
#define QV_API_END
#endif

#if defined(_WIN32)
#if defined(QUEST_VISION_BUILDING)
#define QV_EXPORT __declspec(dllexport)
#else
#define QV_EXPORT __declspec(dllimport)
#endif
#else
#define QV_EXPORT __attribute__((visibility("default")))
#endif

QV_API_BEGIN

// Must match questlab::math::Vec3 layout: three consecutive floats.
typedef struct QvVec3 {
    float x;
    float y;
    float z;
} QvVec3;

// Must match questlab::math::Quat layout.
typedef struct QvQuat {
    float x;
    float y;
    float z;
    float w;
} QvQuat;

// Detection frustum: camera origin plus four corner bearings and the
// centre bearing, all in the same world space as the depth points.
typedef struct QvFrustum {
    QvVec3 origin;
    QvVec3 corners[4];
    QvVec3 centerDir;
} QvFrustum;

// Mirrors FusionParameters with C-marshallable fields. int32_t replaces
// bool so C# bool/int marshalling stays blittable.
typedef struct QvFusionParams {
    float borderInsetFraction;
    float foregroundSampleShare;
    float clusterToleranceMeters;
    float extentLowPercentile;
    float extentHighPercentile;
    uint32_t ransacIterations;
    float ransacInlierMeters;
    float ransacMinimumInlierRatio;
    int32_t requireMeasuredOrientation;
    uint32_t minimumSampleCount;
    float minimumInlierRatio;
    float minimumRangeMeters;
    float maximumRangeMeters;
    QvVec3 up;
} QvFusionParams;

typedef struct QvSizePrior {
    int32_t valid;
    float depthMeters;
} QvSizePrior;

// Mirrors FusedBox. rangeSource: 0 = Stereo, 1 = EnvDepthFallback,
// 2 = Prior (unset/hide sentinel).
typedef struct QvFusedBox {
    QvVec3 center;
    QvVec3 halfExtents;
    QvQuat orientation;
    float rangeMeters;
    int32_t depthAssumed;
    int32_t orientationMeasured;
    float planeInlierRatio;
    uint32_t sampleCount;
    float inlierRatio;
    float confidence;
    int32_t rangeSource;
} QvFusedBox;

//=============================================================================
// Opaque handle: stateful fusion session (PoseTrack-style). Holds the
// parameters plus the last error, so Unity creates once and fuses per
// frame without re-sending configuration.
//=============================================================================
typedef void* QvFusionHandle;

typedef enum {
    QV_OK = 0,
    QV_REFUSED = 1,
    QV_BAD_HANDLE = -1,
    QV_BAD_ARGUMENT = -2,
    QV_INTERNAL_ERROR = -3,
} QvResultCode;

// Fuse result: the oriented box plus its rigid pose as a column-major 4x4
// matrix (rotation from the box quaternion, translation from the centre),
// ready to copy into a Unity Matrix4x4 column by column.
typedef struct QvFuseResult {
    QvFusedBox box;
    float poseMatrix[16];
} QvFuseResult;

// Create a fusion session with default parameters. Never NULL on success;
// NULL means allocation failure.
QV_EXPORT QvFusionHandle QvCreate(void);

// Destroy a fusion session. NULL-safe.
QV_EXPORT void QvDestroy(QvFusionHandle handle);

// Replace the session parameters. Returns QV_OK or an error code.
QV_EXPORT int32_t QvSetParams(
    QvFusionHandle handle,
    const QvFusionParams* params);

// Fuse one detection through the session. Returns QV_OK, QV_REFUSED
// (weak evidence, see reason / QvGetLastError), or an error code.
QV_EXPORT int32_t QvFuse(
    QvFusionHandle handle,
    const QvFrustum* frustum,
    const QvVec3* points,
    int32_t pointCount,
    const QvSizePrior* prior,
    QvFuseResult* outResult,
    char* reason,
    int32_t reasonSize);

// Stateless one-shot entry points (no session needed).

// Last error/reason string of the session. Never NULL; empty when clean.
QV_EXPORT const char* QvGetLastError(QvFusionHandle handle);

// Library version string, e.g. "quest_vision 1.0.0". Never NULL.
QV_EXPORT const char* QvVersion(void);

// Fill params with the same defaults as FusionParameters.
QV_EXPORT void QvFusionParamsDefault(QvFusionParams* params);

// Fuse one 2D detection frustum with a depth cloud into an oriented box.
// points: array of pointCount world-space samples (may contain
// non-finite values; they are skipped). reason: optional buffer of
// reasonSize bytes receiving the refusal explanation (always
// NUL-terminated when reasonSize > 0).
QV_EXPORT int32_t QvFuseDetection(
    const QvFrustum* frustum,
    const QvVec3* points,
    int32_t pointCount,
    const QvFusionParams* params,
    const QvSizePrior* prior,
    QvFusedBox* outBox,
    char* reason,
    int32_t reasonSize);

// True when the point is inside the frustum pyramid (1) or not (0),
// negative on usage error. Mirrors PointInsideFrustum.
QV_EXPORT int32_t QvPointInsideFrustum(
    const QvFrustum* frustum,
    const QvVec3* point,
    float borderInsetFraction);

QV_API_END
