#include "xr_core/capture_time.h"

#include <cstdio>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void TestSensorNanosToTimespec() {
    const questlab::SensorTimespec parts =
        questlab::SensorNanosToTimespec(1500000001LL);
    Expect(parts.tvSec == 1, "timespec seconds");
    Expect(parts.tvNsec == 500000001LL, "timespec nanoseconds");
    const questlab::SensorTimespec zero =
        questlab::SensorNanosToTimespec(0);
    Expect(zero.tvSec == 0 && zero.tvNsec == 0, "zero timestamp rejected");
    const questlab::SensorTimespec negative =
        questlab::SensorNanosToTimespec(-5);
    Expect(
        negative.tvSec == 0 && negative.tvNsec == 0,
        "negative timestamp rejected");
}

void TestCaptureResidualNanoseconds() {
    Expect(
        questlab::CaptureResidualNanoseconds(2000, 1500) == 500,
        "residual is reference minus capture");
    Expect(
        questlab::CaptureResidualNanoseconds(1500, 2000) == -500,
        "future capture gives negative residual");
}

void TestDecideCapturePose() {
    const questlab::CapturePoseVerdict measured =
        questlab::DecideCapturePose(true, true, true, 20000000LL);
    Expect(
        measured.useMeasured && !measured.substituted,
        "valid recent locate is measured");
    const questlab::CapturePoseVerdict noExtension =
        questlab::DecideCapturePose(false, true, true, 20000000LL);
    Expect(
        !noExtension.useMeasured && noExtension.substituted,
        "missing extension substitutes");
    const questlab::CapturePoseVerdict invalidLocate =
        questlab::DecideCapturePose(true, true, false, 20000000LL);
    Expect(
        !invalidLocate.useMeasured && invalidLocate.substituted,
        "invalid locate flags substitute");
    const questlab::CapturePoseVerdict tooOld =
        questlab::DecideCapturePose(
            true, true, true, questlab::kMaxCaptureAgeNanoseconds + 1);
    Expect(
        !tooOld.useMeasured && tooOld.substituted,
        "capture outside history substitutes");
    const questlab::CapturePoseVerdict future =
        questlab::DecideCapturePose(true, true, true, -1);
    Expect(
        !future.useMeasured && future.substituted,
        "future capture timestamp substitutes");
}

}  // namespace

int main() {
    TestSensorNanosToTimespec();
    TestCaptureResidualNanoseconds();
    TestDecideCapturePose();
    if (failures == 0) {
        std::printf("capture_time_tests: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
