#pragma once

// Pure capture-time math for the RGB frustum path (requirements R-1, R-2).
// Header-only and dependency-free apart from XrTime so the xr_core host tests
// can exercise it against the stub OpenXR header without the loader.
//
// Clock assumption (explicit, R-2): Android Camera2 with
// SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME stamps in CLOCK_BOOTTIME, while
// XR_KHR_convert_timespec_time consumes a CLOCK_MONOTONIC timespec. Inside one
// awake session BOOTTIME == MONOTONIC; a suspend/resume cycle breaks that.
// The mapping below does NOT hide this: the assumption is logged once at
// startup by the caller, and CaptureResidualNanoseconds is the instrument
// that would expose a violation (implausible age jumps).

#include <openxr/openxr.h>

#include <cstdint>

namespace questlab {

// Maximum age of a capture-time pose the runtime is expected to still have in
// history. Generous on purpose: the residual log shows reality, and anything
// older (or from the future) is rejected rather than silently used.
inline constexpr int64_t kMaxCaptureAgeNanoseconds = 1000000000LL;

// Camera2 sensor timestamp (nanoseconds, BOOTTIME domain) split into the
// timespec fields XR_KHR_convert_timespec_time consumes.
struct SensorTimespec {
    int64_t tvSec = 0;
    int64_t tvNsec = 0;
};

inline SensorTimespec SensorNanosToTimespec(int64_t sensorNanos) {
    SensorTimespec parts;
    if (sensorNanos <= 0) {
        return parts;
    }
    parts.tvSec = sensorNanos / 1000000000LL;
    parts.tvNsec = sensorNanos % 1000000000LL;
    return parts;
}

// How far in the past the capture time is relative to a reference XrTime
// (normally the render predicted display time). Negative means the capture
// timestamp lies in the future, i.e. the domains disagree.
inline int64_t CaptureResidualNanoseconds(
    XrTime referenceXrTime,
    XrTime captureXrTime) {
    return referenceXrTime - captureXrTime;
}

// Validity policy for a capture-time locate result. Every rejection keeps the
// render-time substituted pose and says why: an empty result is correct
// output, not a failure to paper over.
struct CapturePoseVerdict {
    bool useMeasured = false;
    bool substituted = true;
    const char* reason = "undecided";
};

inline CapturePoseVerdict DecideCapturePose(
    bool extensionAvailable,
    bool locateSucceeded,
    bool locateValid,
    int64_t residualNanoseconds) {
    if (!extensionAvailable) {
        return {false, true, "convert-timespec-time unavailable"};
    }
    if (!locateSucceeded || !locateValid) {
        return {false, true, "capture-time locate invalid"};
    }
    if (residualNanoseconds < 0 ||
        residualNanoseconds > kMaxCaptureAgeNanoseconds) {
        return {false, true, "capture time outside runtime history"};
    }
    return {true, false, "measured"};
}

}  // namespace questlab
