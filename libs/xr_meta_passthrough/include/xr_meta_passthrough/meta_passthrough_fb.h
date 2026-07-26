#pragma once

#include "xr_core/passthrough_interface.h"

#include <openxr/openxr.h>

namespace questlab {

class MetaPassthroughFB final : public XrPassthroughInterface {
public:
    MetaPassthroughFB() = default;
    ~MetaPassthroughFB() override;

    MetaPassthroughFB(const MetaPassthroughFB&) = delete;
    MetaPassthroughFB& operator=(const MetaPassthroughFB&) = delete;

    bool Initialize(
        XrInstance instance,
        XrSystemId systemId,
        XrSession session) override;
    bool SetActive(bool active) override;
    bool HandleEvent(const XrEventDataBuffer& event) override;
    bool AppendUnderlayLayers(
        XrTime displayTime,
        std::vector<const XrCompositionLayerBaseHeader*>* layers) override;
    void Shutdown() override;

    bool IsActive() const { return active_; }

private:
    bool LoadFunctions();
    bool QueryCapabilities(XrSystemId systemId);
    bool CreateObjects();
    bool DestroyObjects();
    bool ActivateObjects();
    bool DeactivateObjects();
    bool Reinitialize();

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrPassthroughFB passthrough_ = XR_NULL_HANDLE;
    XrPassthroughLayerFB layer_ = XR_NULL_HANDLE;
    XrCompositionLayerPassthroughFB compositionLayer_{
        XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};

    PFN_xrCreatePassthroughFB createPassthrough_ = nullptr;
    PFN_xrDestroyPassthroughFB destroyPassthrough_ = nullptr;
    PFN_xrPassthroughStartFB startPassthrough_ = nullptr;
    PFN_xrPassthroughPauseFB pausePassthrough_ = nullptr;
    PFN_xrCreatePassthroughLayerFB createLayer_ = nullptr;
    PFN_xrDestroyPassthroughLayerFB destroyLayer_ = nullptr;
    PFN_xrPassthroughLayerPauseFB pauseLayer_ = nullptr;
    PFN_xrPassthroughLayerResumeFB resumeLayer_ = nullptr;

    bool desiredActive_ = false;
    bool active_ = false;
    uint32_t reinitializationCount_ = 0;
};

}  // namespace questlab
