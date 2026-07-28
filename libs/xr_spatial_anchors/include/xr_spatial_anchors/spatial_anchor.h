#pragma once

#include "xr_spatial_anchors/anchor_record.h"
#include "xr_core/passthrough_interface.h"
#include "xr_core/xr_session.h"
#include "xr_math/xr_math.h"

#include <openxr/openxr.h>

#include <string>

namespace questlab {

static_assert(kAnchorUuidSize == XR_UUID_SIZE);

enum class AnchorLifecycle {
    Empty,
    Restoring,
    Creating,
    EnablingStorage,
    Saving,
    Ready,
    Erasing,
    Error,
};

struct SpatialAnchorState {
    AnchorLifecycle lifecycle = AnchorLifecycle::Empty;
    AnchorUuid uuid{};
    math::Pose pose{};
    bool hasUuid = false;
    bool persisted = false;
    bool positionValid = false;
    bool orientationValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;
    XrResult lastResult = XR_SUCCESS;
};

const char* AnchorLifecycleName(AnchorLifecycle lifecycle);

class MetaSpatialAnchorManager final : public XrEventObserver {
public:
    MetaSpatialAnchorManager() = default;
    ~MetaSpatialAnchorManager() override;

    MetaSpatialAnchorManager(const MetaSpatialAnchorManager&) = delete;
    MetaSpatialAnchorManager& operator=(const MetaSpatialAnchorManager&) =
        delete;

    bool Initialize(
        XrInstance instance,
        XrSystemId systemId,
        XrSession session,
        const char* internalDataPath);
    bool UpdateFrame(const XrFrameUpdateInfo& frame);
    bool Create(
        const math::Pose& poseInBaseSpace,
        const XrFrameUpdateInfo& frame);
    bool Erase();
    bool HandleEvent(const XrEventDataBuffer& event) override;
    void Shutdown();

    const SpatialAnchorState& State() const { return state_; }

private:
    enum class PendingOperation {
        None,
        Query,
        Create,
        EnableLocatable,
        EnableStorable,
        Save,
        Erase,
    };

    bool ResolveFunctions();
    bool LoadRecord();
    bool BeginRestore();
    bool RetrieveQueryResults(XrAsyncRequestIdFB requestId);
    bool ContinueComponents();
    bool RequestComponent(XrSpaceComponentTypeFB component);
    bool BeginSave();
    bool WriteRecord();
    void RemoveRecord();
    void ClearLiveSpace();
    void ResetEmpty();
    bool Fail(XrResult result, const char* operation);
    bool Matches(PendingOperation operation, XrAsyncRequestIdFB requestId);
    void SetLifecycle(AnchorLifecycle lifecycle);
    XrUuidEXT NativeUuid() const;
    static AnchorUuid PortableUuid(const XrUuidEXT& uuid);

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace space_ = XR_NULL_HANDLE;
    XrAsyncRequestIdFB requestId_ = 0;
    PendingOperation pending_ = PendingOperation::None;
    SpatialAnchorState state_{};
    std::string recordPath_;
    bool restoreRequested_ = false;
    bool restoreFlow_ = false;
    bool queryResultsReceived_ = false;
    bool queryComplete_ = false;
    bool compensatingErase_ = false;
    bool lastLocationValid_ = false;

    PFN_xrCreateSpatialAnchorFB createSpatialAnchor_ = nullptr;
    PFN_xrGetSpaceUuidFB getSpaceUuid_ = nullptr;
    PFN_xrEnumerateSpaceSupportedComponentsFB enumerateComponents_ = nullptr;
    PFN_xrGetSpaceComponentStatusFB getComponentStatus_ = nullptr;
    PFN_xrSetSpaceComponentStatusFB setComponentStatus_ = nullptr;
    PFN_xrSaveSpacesMETA saveSpaces_ = nullptr;
    PFN_xrEraseSpacesMETA eraseSpaces_ = nullptr;
    PFN_xrQuerySpacesFB querySpaces_ = nullptr;
    PFN_xrRetrieveSpaceQueryResultsFB retrieveQueryResults_ = nullptr;
};

}  // namespace questlab
