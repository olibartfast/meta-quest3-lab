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

    if (failures == 0) {
        std::puts("quest_vision C API tests passed");
    }
    return failures == 0 ? 0 : 1;
}
