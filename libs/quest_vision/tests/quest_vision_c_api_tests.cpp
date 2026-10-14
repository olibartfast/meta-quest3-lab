#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "quest_vision/quest_vision_c_api.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        ++failures;
    }
}

QvFrustum MakeFrustum() {
    QvFrustum frustum{};
    frustum.origin = {0.0F, 0.0F, 0.0F};
    const float corners[4][2] = {
        {-0.2F, 0.2F}, {0.2F, 0.2F}, {0.2F, -0.2F}, {-0.2F, -0.2F}};
    for (int i = 0; i < 4; ++i) {
        float x = corners[i][0];
        float y = corners[i][1];
        float z = -1.0F;
        const float length = std::sqrt(x * x + y * y + z * z);
        frustum.corners[i] = {x / length, y / length, z / length};
    }
    frustum.centerDir = {0.0F, 0.0F, -1.0F};
    return frustum;
}

}  // namespace

int main() {
    Expect(
        std::strncmp(QvVersion(), "quest_vision ", 13) == 0,
        "version string must carry the library name");

    // Slab at 2 m through the C ABI must fuse like the C++ entry point.
    QvFrustum frustum = MakeFrustum();
    std::vector<QvVec3> points;
    for (int ix = -10; ix <= 10; ++ix) {
        for (int iy = -10; iy <= 10; ++iy) {
            points.push_back({
                static_cast<float>(ix) / 10.0F * 0.15F,
                static_cast<float>(iy) / 10.0F * 0.20F,
                -2.0F,
            });
        }
    }
    QvFusionParams params{};
    QvFusionParamsDefault(&params);
    Expect(
        params.requireMeasuredOrientation == 1 &&
            params.minimumSampleCount > 0,
        "defaults must mirror FusionParameters");
    QvSizePrior prior{0, 0.0F};
    QvFusedBox box{};
    char reason[256]{};
    Expect(
        QvFuseDetection(
            &frustum, points.data(), static_cast<int32_t>(points.size()),
            &params, &prior, &box, reason, sizeof(reason)) == QV_OK,
        reason[0] != '\0' ? reason : "slab fusion via C ABI must succeed");
    Expect(
        std::fabs(box.center.z + 2.0F) < 0.05F,
        "C ABI box centre must sit at the slab distance");
    Expect(
        box.orientationMeasured == 1,
        "slab plane must report a measured orientation");
    Expect(
        std::fabs(box.rangeMeters - 2.0F) < 0.15F,
        "C ABI representative range must match the slab");

    // Empty cloud must refuse with a reason, not crash.
    QvFusedBox rejected{};
    char refuseReason[256]{};
    Expect(
        QvFuseDetection(
            &frustum, nullptr, 0, &params, &prior, &rejected, refuseReason,
            sizeof(refuseReason)) == QV_REFUSED,
        "empty depth must refuse through the C ABI");
    Expect(
        refuseReason[0] != '\0',
        "a refusal must carry a reason for Unity logging");

    // Null arguments must not crash.
    Expect(
        QvFuseDetection(
            nullptr, points.data(), 1, &params, &prior, &box, nullptr, 0) ==
            QV_BAD_ARGUMENT,
        "null frustum must be a usage error");
    Expect(
        QvPointInsideFrustum(&frustum, &points[points.size() / 2], 0.12F) ==
            1,
        "an axial point must be inside the frustum");
    Expect(
        QvPointInsideFrustum(nullptr, &points[0], 0.12F) == QV_BAD_ARGUMENT,
        "null frustum containment must be a usage error");

    // Session API: lifecycle, params, fuse with pose matrix, errors.
    {
        QvFusionHandle session = QvCreate();
        Expect(session != nullptr, "session creation must succeed");
        QvFusionParams sessionParams{};
        QvFusionParamsDefault(&sessionParams);
        Expect(
            QvSetParams(session, &sessionParams) == QV_OK,
            "session params must apply");
        Expect(
            QvSetParams(nullptr, &sessionParams) == QV_BAD_HANDLE,
            "null session params must be a handle error");
        QvFuseResult result{};
        char sessionReason[256]{};
        Expect(
            QvFuse(
                session, &frustum, points.data(),
                static_cast<int32_t>(points.size()), &prior, &result,
                sessionReason, sizeof(sessionReason)) == QV_OK,
            sessionReason[0] != '\0' ? sessionReason
                                     : "session fuse must succeed");
        // Column 3 of the pose matrix must equal the box centre.
        Expect(
            result.poseMatrix[12] == result.box.center.x &&
                result.poseMatrix[13] == result.box.center.y &&
                result.poseMatrix[14] == result.box.center.z &&
                result.poseMatrix[15] == 1.0F,
            "pose matrix translation must equal the box centre");
        // 3x3 block must match the quaternion: rotate +X and compare.
        {
            const float x = result.box.orientation.x;
            const float y = result.box.orientation.y;
            const float z = result.box.orientation.z;
            const float w = result.box.orientation.w;
            const float rx[3] = {
                1.0F - 2.0F * (y * y + z * z),
                2.0F * (x * y + z * w),
                2.0F * (x * z - y * w),
            };
            Expect(
                std::fabs(result.poseMatrix[0] - rx[0]) < 1.0e-5F &&
                    std::fabs(result.poseMatrix[1] - rx[1]) < 1.0e-5F &&
                    std::fabs(result.poseMatrix[2] - rx[2]) < 1.0e-5F,
                "pose matrix rotation must match the quaternion");
        }
        Expect(
            QvGetLastError(session) != nullptr &&
                QvGetLastError(session)[0] == '\0',
            "clean session must report an empty last error");
        Expect(
            QvFuse(
                session, &frustum, nullptr, 0, &prior, &result, nullptr,
                0) == QV_REFUSED,
            "empty depth through a session must refuse");
        Expect(
            QvGetLastError(session) != nullptr &&
                QvGetLastError(session)[0] != '\0',
            "refusal must land in the session last error");
        Expect(
            QvFuse(
                nullptr, &frustum, points.data(), 1, &prior, &result,
                nullptr, 0) == QV_BAD_HANDLE,
            "null session fuse must be a handle error");
        QvDestroy(session);
        QvDestroy(nullptr);  // Must not crash.
    }

    if (failures == 0) {
        std::puts("quest_vision C API tests passed");
    }
    return failures == 0 ? 0 : 1;
}
