#pragma once

#include <openxr/openxr.h>

namespace questlab {

class XrInstanceContext {
public:
    XrInstanceContext() = default;
    ~XrInstanceContext();

    XrInstanceContext(const XrInstanceContext&) = delete;
    XrInstanceContext& operator=(const XrInstanceContext&) = delete;

    bool Initialize(void* applicationVm, void* applicationActivity);
    void Shutdown();

    XrInstance Instance() const { return instance_; }
    XrSystemId SystemId() const { return systemId_; }

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
};

}  // namespace questlab
