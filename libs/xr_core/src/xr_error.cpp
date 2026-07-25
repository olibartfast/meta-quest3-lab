#include "xr_core/xr_error.h"

#include <android/log.h>

#include <cstdarg>
#include <cstdio>

namespace questlab {
namespace {

constexpr const char* kLogTag = "OpenXRBootstrap";

void Log(int priority, const char* format, va_list arguments) {
    __android_log_vprint(priority, kLogTag, format, arguments);
}

}  // namespace

std::string XrResultName(XrInstance instance, XrResult result) {
    char buffer[XR_MAX_RESULT_STRING_SIZE] = {};
    if (instance != XR_NULL_HANDLE &&
        XR_SUCCEEDED(xrResultToString(instance, result, buffer))) {
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "XrResult(%d)", result);
    return buffer;
}

bool CheckXr(XrInstance instance, XrResult result, const char* operation) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    LogError("%s failed: %s", operation, XrResultName(instance, result).c_str());
    return false;
}

bool CheckVk(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    LogError("%s failed: VkResult(%d)", operation, result);
    return false;
}

void LogInfo(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    Log(ANDROID_LOG_INFO, format, arguments);
    va_end(arguments);
}

void LogError(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    Log(ANDROID_LOG_ERROR, format, arguments);
    va_end(arguments);
}

}  // namespace questlab
