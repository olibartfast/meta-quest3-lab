#pragma once

#include <openxr/openxr.h>

#include <vector>

namespace questlab {

struct XrInstanceOptions {
    const char* applicationName = "QuestLab";
    uint32_t applicationVersion = 1;
    std::vector<const char*> additionalExtensions{};
    // Requested only when the runtime exposes them; absence is logged and the
    // app continues without them (capture-time pose falls back to substituted).
    // XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME belongs here, never in
    // additionalExtensions: a missing convert extension must not fail startup.
    std::vector<const char*> optionalExtensions{};
};

class XrInstanceContext {
public:
    XrInstanceContext() = default;
    ~XrInstanceContext();

    XrInstanceContext(const XrInstanceContext&) = delete;
    XrInstanceContext& operator=(const XrInstanceContext&) = delete;

    bool Initialize(
        void* applicationVm,
        void* applicationActivity,
        const XrInstanceOptions& options = {});
    void Shutdown();

    XrInstance Instance() const { return instance_; }
    XrSystemId SystemId() const { return systemId_; }

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
};

}  // namespace questlab
