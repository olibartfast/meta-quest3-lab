#include "xr_meta_passthrough/meta_passthrough_fb.h"

#include "xr_core/xr_error.h"

#include <cstring>
#include <vector>

namespace questlab {
namespace {

constexpr uint32_t kMaximumReinitializations = 3;

bool HasFlag(
    XrPassthroughStateChangedFlagsFB flags,
    XrPassthroughStateChangedFlagsFB flag) {
    return (flags & flag) != 0;
}

}  // namespace

MetaPassthroughFB::~MetaPassthroughFB() {
    Shutdown();
}

bool MetaPassthroughFB::Initialize(
    XrInstance instance,
    XrSystemId systemId,
    XrSession session) {
    if (passthrough_ != XR_NULL_HANDLE) {
        return true;
    }
    instance_ = instance;
    session_ = session;
    if (!LoadFunctions() ||
        !QueryCapabilities(systemId) ||
        !CreateObjects()) {
        Shutdown();
        return false;
    }
    LogInfo("Meta passthrough initialized in paused state");
    return true;
}

bool MetaPassthroughFB::LoadFunctions() {
    const auto load = [this](
                          const char* name,
                          PFN_xrVoidFunction* function) {
        return CheckXr(
            instance_,
            xrGetInstanceProcAddr(instance_, name, function),
            name);
    };
    return
        load(
            "xrCreatePassthroughFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&createPassthrough_)) &&
        load(
            "xrDestroyPassthroughFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&destroyPassthrough_)) &&
        load(
            "xrPassthroughStartFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&startPassthrough_)) &&
        load(
            "xrPassthroughPauseFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&pausePassthrough_)) &&
        load(
            "xrCreatePassthroughLayerFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&createLayer_)) &&
        load(
            "xrDestroyPassthroughLayerFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&destroyLayer_)) &&
        load(
            "xrPassthroughLayerPauseFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&pauseLayer_)) &&
        load(
            "xrPassthroughLayerResumeFB",
            reinterpret_cast<PFN_xrVoidFunction*>(&resumeLayer_));
}

bool MetaPassthroughFB::QueryCapabilities(XrSystemId systemId) {
    uint32_t extensionCount = 0;
    if (!CheckXr(
            instance_,
            xrEnumerateInstanceExtensionProperties(
                nullptr, 0, &extensionCount, nullptr),
            "xrEnumerateInstanceExtensionProperties(passthrough count)")) {
        return false;
    }
    std::vector<XrExtensionProperties> extensions(
        extensionCount,
        XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
    if (!CheckXr(
            instance_,
            xrEnumerateInstanceExtensionProperties(
                nullptr,
                extensionCount,
                &extensionCount,
                extensions.data()),
            "xrEnumerateInstanceExtensionProperties(passthrough list)")) {
        return false;
    }
    uint32_t passthroughVersion = 0;
    for (const XrExtensionProperties& extension : extensions) {
        if (std::strcmp(
                extension.extensionName,
                XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0) {
            passthroughVersion = extension.extensionVersion;
            break;
        }
    }

    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    if (passthroughVersion >= 3) {
        XrSystemPassthroughProperties2FB passthroughProperties{
            XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB};
        systemProperties.next = &passthroughProperties;
        if (!CheckXr(
                instance_,
                xrGetSystemProperties(
                    instance_, systemId, &systemProperties),
                "xrGetSystemProperties(passthrough capabilities)")) {
            return false;
        }
        const bool supported =
            (passthroughProperties.capabilities &
             XR_PASSTHROUGH_CAPABILITY_BIT_FB) != 0;
        const bool color =
            (passthroughProperties.capabilities &
             XR_PASSTHROUGH_CAPABILITY_COLOR_BIT_FB) != 0;
        LogInfo(
            "Passthrough extension v%u capabilities=0x%llx "
            "(supported=%s color=%s)",
            passthroughVersion,
            static_cast<unsigned long long>(
                passthroughProperties.capabilities),
            supported ? "yes" : "no",
            color ? "yes" : "no");
        if (!supported) {
            LogError("OpenXR system does not support passthrough");
            return false;
        }
        return true;
    }

    XrSystemPassthroughPropertiesFB passthroughProperties{
        XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
    systemProperties.next = &passthroughProperties;
    if (!CheckXr(
            instance_,
            xrGetSystemProperties(instance_, systemId, &systemProperties),
            "xrGetSystemProperties(passthrough support)")) {
        return false;
    }
    LogInfo(
        "Passthrough extension v%u legacy support=%s",
        passthroughVersion,
        passthroughProperties.supportsPassthrough == XR_TRUE
            ? "yes"
            : "no");
    if (passthroughProperties.supportsPassthrough != XR_TRUE) {
        LogError("OpenXR system does not support passthrough");
        return false;
    }
    return true;
}

bool MetaPassthroughFB::CreateObjects() {
    XrPassthroughCreateInfoFB featureInfo{
        XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    featureInfo.flags = 0;
    if (!CheckXr(
            instance_,
            createPassthrough_(
                session_, &featureInfo, &passthrough_),
            "xrCreatePassthroughFB")) {
        return false;
    }
    LogInfo("Passthrough feature created");

    XrPassthroughLayerCreateInfoFB layerInfo{
        XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    layerInfo.passthrough = passthrough_;
    layerInfo.flags = 0;
    layerInfo.purpose =
        XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    if (!CheckXr(
            instance_,
            createLayer_(session_, &layerInfo, &layer_),
            "xrCreatePassthroughLayerFB(reconstruction)")) {
        DestroyObjects();
        return false;
    }
    compositionLayer_ =
        XrCompositionLayerPassthroughFB{
            XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
    compositionLayer_.flags =
        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    compositionLayer_.space = XR_NULL_HANDLE;
    compositionLayer_.layerHandle = layer_;
    LogInfo("Passthrough reconstruction layer created");
    return true;
}

bool MetaPassthroughFB::ActivateObjects() {
    if (active_) {
        return true;
    }
    if (!CheckXr(
            instance_,
            startPassthrough_(passthrough_),
            "xrPassthroughStartFB")) {
        return false;
    }
    if (!CheckXr(
            instance_,
            resumeLayer_(layer_),
            "xrPassthroughLayerResumeFB")) {
        pausePassthrough_(passthrough_);
        return false;
    }
    active_ = true;
    LogInfo("Passthrough feature and layer active");
    return true;
}

bool MetaPassthroughFB::DeactivateObjects() {
    if (!active_) {
        return true;
    }
    const bool layerPaused = CheckXr(
        instance_,
        pauseLayer_(layer_),
        "xrPassthroughLayerPauseFB");
    const bool featurePaused = CheckXr(
        instance_,
        pausePassthrough_(passthrough_),
        "xrPassthroughPauseFB");
    active_ = false;
    if (layerPaused && featurePaused) {
        LogInfo("Passthrough feature and layer paused");
        return true;
    }
    return false;
}

bool MetaPassthroughFB::SetActive(bool active) {
    desiredActive_ = active;
    return active ? ActivateObjects() : DeactivateObjects();
}

bool MetaPassthroughFB::HandleEvent(
    const XrEventDataBuffer& event) {
    if (event.type !=
        XR_TYPE_EVENT_DATA_PASSTHROUGH_STATE_CHANGED_FB) {
        return true;
    }
    const auto& state =
        *reinterpret_cast<
            const XrEventDataPassthroughStateChangedFB*>(&event);
    LogInfo(
        "Passthrough state changed: flags=0x%llx",
        static_cast<unsigned long long>(state.flags));

    if (HasFlag(
            state.flags,
            XR_PASSTHROUGH_STATE_CHANGED_NON_RECOVERABLE_ERROR_BIT_FB)) {
        LogError("Passthrough reported a non-recoverable error");
        return false;
    }
    if (HasFlag(
            state.flags,
            XR_PASSTHROUGH_STATE_CHANGED_REINIT_REQUIRED_BIT_FB)) {
        return Reinitialize();
    }
    if (HasFlag(
            state.flags,
            XR_PASSTHROUGH_STATE_CHANGED_RECOVERABLE_ERROR_BIT_FB)) {
        LogWarning(
            "Passthrough temporarily unavailable; runtime is recovering");
    }
    if (HasFlag(
            state.flags,
            XR_PASSTHROUGH_STATE_CHANGED_RESTORED_ERROR_BIT_FB)) {
        LogInfo("Passthrough availability restored");
    }
    return true;
}

bool MetaPassthroughFB::Reinitialize() {
    if (reinitializationCount_ >= kMaximumReinitializations) {
        LogError("Passthrough reinitialization retry limit reached");
        return false;
    }
    ++reinitializationCount_;
    LogWarning(
        "Reinitializing passthrough feature and layer (attempt %u/%u)",
        reinitializationCount_,
        kMaximumReinitializations);
    const bool shouldReactivate = desiredActive_;
    active_ = false;
    if (!DestroyObjects() || !CreateObjects()) {
        return false;
    }
    return shouldReactivate ? ActivateObjects() : true;
}

bool MetaPassthroughFB::AppendUnderlayLayers(
    XrTime displayTime,
    std::vector<const XrCompositionLayerBaseHeader*>* layers) {
    static_cast<void>(displayTime);
    if (layers == nullptr) {
        return false;
    }
    if (!active_) {
        return true;
    }
    if (layer_ == XR_NULL_HANDLE) {
        LogError("Active passthrough has no reconstruction layer");
        return false;
    }
    layers->push_back(
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &compositionLayer_));
    return true;
}

bool MetaPassthroughFB::DestroyObjects() {
    bool succeeded = true;
    active_ = false;
    if (layer_ != XR_NULL_HANDLE) {
        succeeded =
            CheckXr(
                instance_,
                destroyLayer_(layer_),
                "xrDestroyPassthroughLayerFB") &&
            succeeded;
        layer_ = XR_NULL_HANDLE;
        compositionLayer_.layerHandle = XR_NULL_HANDLE;
        LogInfo("Passthrough reconstruction layer destroyed");
    }
    if (passthrough_ != XR_NULL_HANDLE) {
        succeeded =
            CheckXr(
                instance_,
                destroyPassthrough_(passthrough_),
                "xrDestroyPassthroughFB") &&
            succeeded;
        passthrough_ = XR_NULL_HANDLE;
        LogInfo("Passthrough feature destroyed");
    }
    return succeeded;
}

void MetaPassthroughFB::Shutdown() {
    if (active_) {
        DeactivateObjects();
    }
    DestroyObjects();
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
    createPassthrough_ = nullptr;
    destroyPassthrough_ = nullptr;
    startPassthrough_ = nullptr;
    pausePassthrough_ = nullptr;
    createLayer_ = nullptr;
    destroyLayer_ = nullptr;
    pauseLayer_ = nullptr;
    resumeLayer_ = nullptr;
    desiredActive_ = false;
    reinitializationCount_ = 0;
}

}  // namespace questlab
