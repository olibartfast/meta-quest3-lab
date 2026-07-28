#include "xr_spatial_anchors/spatial_anchor.h"

#include "xr_core/xr_error.h"
#include "xr_math/openxr_conversions.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include <unistd.h>

namespace questlab {
namespace {

constexpr XrDuration kComponentTimeout = 5'000'000'000;
constexpr XrDuration kQueryTimeout = 10'000'000'000;
constexpr char kRecordFilename[] = "questlab-spatial-anchor-v1.txt";

template <typename Function>
bool ResolveFunction(
    XrInstance instance,
    const char* name,
    Function* destination) {
    PFN_xrVoidFunction function = nullptr;
    const XrResult result = xrGetInstanceProcAddr(instance, name, &function);
    if (XR_FAILED(result) || function == nullptr) {
        LogError(
            "Required OpenXR function %s unavailable (result=%d)",
            name,
            result);
        return false;
    }
    *destination = reinterpret_cast<Function>(function);
    return true;
}

bool ContainsComponent(
    const std::vector<XrSpaceComponentTypeFB>& components,
    XrSpaceComponentTypeFB component) {
    return std::find(components.begin(), components.end(), component) !=
        components.end();
}

}  // namespace

const char* AnchorLifecycleName(AnchorLifecycle lifecycle) {
    switch (lifecycle) {
        case AnchorLifecycle::Empty: return "Empty";
        case AnchorLifecycle::Restoring: return "Restoring";
        case AnchorLifecycle::Creating: return "Creating";
        case AnchorLifecycle::EnablingStorage: return "EnablingStorage";
        case AnchorLifecycle::Saving: return "Saving";
        case AnchorLifecycle::Ready: return "Ready";
        case AnchorLifecycle::Erasing: return "Erasing";
        case AnchorLifecycle::Error: return "Error";
    }
    return "Unknown";
}

MetaSpatialAnchorManager::~MetaSpatialAnchorManager() {
    Shutdown();
}

bool MetaSpatialAnchorManager::Initialize(
    XrInstance instance,
    XrSystemId systemId,
    XrSession session,
    const char* internalDataPath) {
    if (instance == XR_NULL_HANDLE || systemId == XR_NULL_SYSTEM_ID ||
        session == XR_NULL_HANDLE || internalDataPath == nullptr ||
        internalDataPath[0] == '\0') {
        LogError("Invalid spatial-anchor initialization arguments");
        return false;
    }

    XrSystemSpacePersistencePropertiesMETA persistence{
        XR_TYPE_SYSTEM_SPACE_PERSISTENCE_PROPERTIES_META};
    XrSystemSpatialEntityPropertiesFB spatialEntity{
        XR_TYPE_SYSTEM_SPATIAL_ENTITY_PROPERTIES_FB};
    spatialEntity.next = &persistence;
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    properties.next = &spatialEntity;
    if (!CheckXr(
            instance,
            xrGetSystemProperties(instance, systemId, &properties),
            "xrGetSystemProperties(spatial anchors)")) {
        return false;
    }
    LogInfo(
        "Spatial anchor capabilities: entity=%s persistence=%s",
        spatialEntity.supportsSpatialEntity == XR_TRUE ? "yes" : "no",
        persistence.supportsSpacePersistence == XR_TRUE ? "yes" : "no");
    if (spatialEntity.supportsSpatialEntity != XR_TRUE ||
        persistence.supportsSpacePersistence != XR_TRUE) {
        LogError("Runtime does not support persistent spatial anchors");
        return false;
    }

    instance_ = instance;
    session_ = session;
    recordPath_ =
        std::string(internalDataPath) + "/" + kRecordFilename;
    if (!ResolveFunctions() || !LoadRecord()) {
        Shutdown();
        return false;
    }
    LogInfo("Persistent spatial-anchor manager initialized");
    return true;
}

bool MetaSpatialAnchorManager::ResolveFunctions() {
    return
        ResolveFunction(
            instance_,
            "xrCreateSpatialAnchorFB",
            &createSpatialAnchor_) &&
        ResolveFunction(instance_, "xrGetSpaceUuidFB", &getSpaceUuid_) &&
        ResolveFunction(
            instance_,
            "xrEnumerateSpaceSupportedComponentsFB",
            &enumerateComponents_) &&
        ResolveFunction(
            instance_,
            "xrGetSpaceComponentStatusFB",
            &getComponentStatus_) &&
        ResolveFunction(
            instance_,
            "xrSetSpaceComponentStatusFB",
            &setComponentStatus_) &&
        ResolveFunction(instance_, "xrSaveSpacesMETA", &saveSpaces_) &&
        ResolveFunction(instance_, "xrEraseSpacesMETA", &eraseSpaces_) &&
        ResolveFunction(instance_, "xrQuerySpacesFB", &querySpaces_) &&
        ResolveFunction(
            instance_,
            "xrRetrieveSpaceQueryResultsFB",
            &retrieveQueryResults_);
}

bool MetaSpatialAnchorManager::LoadRecord() {
    std::ifstream stream(recordPath_, std::ios::binary);
    if (!stream) {
        if (errno != ENOENT) {
            LogError(
                "Could not open anchor record %s (errno=%d)",
                recordPath_.c_str(),
                errno);
        }
        return true;
    }
    const std::string contents{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    AnchorUuid uuid;
    if (!ParseAnchorRecord(contents, &uuid)) {
        LogError("Malformed anchor record removed");
        RemoveRecord();
        return true;
    }
    state_.uuid = uuid;
    state_.hasUuid = true;
    state_.persisted = true;
    LogInfo("Loaded saved anchor UUID %s", FormatAnchorUuid(uuid).c_str());
    return true;
}

bool MetaSpatialAnchorManager::UpdateFrame(
    const XrFrameUpdateInfo& frame) {
    if (!restoreRequested_) {
        restoreRequested_ = true;
        if (state_.hasUuid && !BeginRestore()) {
            return false;
        }
    }

    state_.positionValid = false;
    state_.orientationValid = false;
    state_.positionTracked = false;
    state_.orientationTracked = false;
    if (space_ == XR_NULL_HANDLE || frame.baseSpace == XR_NULL_HANDLE) {
        return true;
    }

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    const XrResult result = xrLocateSpace(
        space_,
        frame.baseSpace,
        frame.predictedDisplayTime,
        &location);
    if (XR_FAILED(result)) {
        Fail(result, "xrLocateSpace(anchor)");
        return true;
    }
    state_.pose = math::FromXr(location.pose);
    state_.positionValid =
        (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    state_.orientationValid =
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    state_.positionTracked =
        (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    state_.orientationTracked =
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) !=
        0;
    const bool valid =
        state_.positionValid && state_.orientationValid;
    if (valid != lastLocationValid_) {
        LogInfo("Spatial anchor location %s", valid ? "valid" : "invalid");
        lastLocationValid_ = valid;
    }
    return true;
}

bool MetaSpatialAnchorManager::BeginRestore() {
    XrUuidEXT uuid = NativeUuid();
    XrSpaceStorageLocationFilterInfoFB storageFilter{
        XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB};
    storageFilter.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
    XrSpaceUuidFilterInfoFB uuidFilter{
        XR_TYPE_SPACE_UUID_FILTER_INFO_FB};
    uuidFilter.next = &storageFilter;
    uuidFilter.uuidCount = 1;
    uuidFilter.uuids = &uuid;
    XrSpaceQueryInfoFB query{XR_TYPE_SPACE_QUERY_INFO_FB};
    query.queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB;
    query.maxResultCount = 1;
    query.timeout = kQueryTimeout;
    query.filter =
        reinterpret_cast<const XrSpaceFilterInfoBaseHeaderFB*>(&uuidFilter);

    XrAsyncRequestIdFB requestId = 0;
    const XrResult result = querySpaces_(
        session_,
        reinterpret_cast<const XrSpaceQueryInfoBaseHeaderFB*>(&query),
        &requestId);
    if (XR_FAILED(result)) {
        return Fail(result, "xrQuerySpacesFB");
    }
    requestId_ = requestId;
    pending_ = PendingOperation::Query;
    restoreFlow_ = true;
    queryResultsReceived_ = false;
    queryComplete_ = false;
    SetLifecycle(AnchorLifecycle::Restoring);
    LogInfo(
        "Restore query started request=%llu uuid=%s",
        static_cast<unsigned long long>(requestId_),
        FormatAnchorUuid(state_.uuid).c_str());
    return true;
}

bool MetaSpatialAnchorManager::Create(
    const math::Pose& poseInBaseSpace,
    const XrFrameUpdateInfo& frame) {
    if (state_.lifecycle != AnchorLifecycle::Empty ||
        pending_ != PendingOperation::None ||
        frame.baseSpace == XR_NULL_HANDLE) {
        return false;
    }
    XrSpatialAnchorCreateInfoFB createInfo{
        XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_FB};
    createInfo.space = frame.baseSpace;
    createInfo.poseInSpace = math::ToXr(poseInBaseSpace);
    createInfo.time = frame.predictedDisplayTime;
    XrAsyncRequestIdFB requestId = 0;
    const XrResult result =
        createSpatialAnchor_(session_, &createInfo, &requestId);
    if (XR_FAILED(result)) {
        return Fail(result, "xrCreateSpatialAnchorFB");
    }
    requestId_ = requestId;
    pending_ = PendingOperation::Create;
    restoreFlow_ = false;
    state_.persisted = false;
    SetLifecycle(AnchorLifecycle::Creating);
    LogInfo(
        "Anchor creation started request=%llu at (%.3f %.3f %.3f)",
        static_cast<unsigned long long>(requestId_),
        poseInBaseSpace.position.x,
        poseInBaseSpace.position.y,
        poseInBaseSpace.position.z);
    return true;
}

bool MetaSpatialAnchorManager::ContinueComponents() {
    uint32_t count = 0;
    XrResult result = enumerateComponents_(space_, 0, &count, nullptr);
    if (XR_FAILED(result)) {
        return Fail(result, "xrEnumerateSpaceSupportedComponentsFB(count)");
    }
    std::vector<XrSpaceComponentTypeFB> components(count);
    result = enumerateComponents_(
        space_,
        static_cast<uint32_t>(components.size()),
        &count,
        components.data());
    if (XR_FAILED(result)) {
        return Fail(result, "xrEnumerateSpaceSupportedComponentsFB(data)");
    }
    components.resize(count);
    if (!ContainsComponent(
            components,
            XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB)) {
        return Fail(
            XR_ERROR_FEATURE_UNSUPPORTED,
            "anchor lacks LOCATABLE component");
    }
    if (!restoreFlow_ &&
        !ContainsComponent(
            components,
            XR_SPACE_COMPONENT_TYPE_STORABLE_FB)) {
        return Fail(
            XR_ERROR_FEATURE_UNSUPPORTED,
            "anchor lacks STORABLE component");
    }

    XrSpaceComponentStatusFB status{
        XR_TYPE_SPACE_COMPONENT_STATUS_FB};
    result = getComponentStatus_(
        space_,
        XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB,
        &status);
    if (XR_FAILED(result)) {
        return Fail(result, "xrGetSpaceComponentStatusFB(LOCATABLE)");
    }
    if (status.enabled != XR_TRUE) {
        return RequestComponent(XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB);
    }
    if (restoreFlow_) {
        pending_ = PendingOperation::None;
        state_.persisted = true;
        SetLifecycle(AnchorLifecycle::Ready);
        LogInfo("Saved spatial anchor restored and locatable");
        return true;
    }

    status = {XR_TYPE_SPACE_COMPONENT_STATUS_FB};
    result = getComponentStatus_(
        space_,
        XR_SPACE_COMPONENT_TYPE_STORABLE_FB,
        &status);
    if (XR_FAILED(result)) {
        return Fail(result, "xrGetSpaceComponentStatusFB(STORABLE)");
    }
    if (status.enabled != XR_TRUE) {
        return RequestComponent(XR_SPACE_COMPONENT_TYPE_STORABLE_FB);
    }
    return BeginSave();
}

bool MetaSpatialAnchorManager::RequestComponent(
    XrSpaceComponentTypeFB component) {
    XrSpaceComponentStatusSetInfoFB setInfo{
        XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB};
    setInfo.componentType = component;
    setInfo.enabled = XR_TRUE;
    setInfo.timeout = kComponentTimeout;
    XrAsyncRequestIdFB requestId = 0;
    const XrResult result =
        setComponentStatus_(space_, &setInfo, &requestId);
    if (XR_FAILED(result)) {
        return Fail(result, "xrSetSpaceComponentStatusFB");
    }
    requestId_ = requestId;
    pending_ =
        component == XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB
            ? PendingOperation::EnableLocatable
            : PendingOperation::EnableStorable;
    SetLifecycle(AnchorLifecycle::EnablingStorage);
    LogInfo(
        "Enabling anchor component %s request=%llu",
        component == XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB
            ? "LOCATABLE"
            : "STORABLE",
        static_cast<unsigned long long>(requestId_));
    return true;
}

bool MetaSpatialAnchorManager::BeginSave() {
    XrSpace space = space_;
    XrSpacesSaveInfoMETA saveInfo{XR_TYPE_SPACES_SAVE_INFO_META};
    saveInfo.spaceCount = 1;
    saveInfo.spaces = &space;
    XrAsyncRequestIdFB requestId = 0;
    const XrResult result = saveSpaces_(session_, &saveInfo, &requestId);
    if (XR_FAILED(result)) {
        return Fail(result, "xrSaveSpacesMETA");
    }
    requestId_ = requestId;
    pending_ = PendingOperation::Save;
    SetLifecycle(AnchorLifecycle::Saving);
    LogInfo(
        "Anchor save started request=%llu",
        static_cast<unsigned long long>(requestId_));
    return true;
}

bool MetaSpatialAnchorManager::Erase() {
    if (pending_ != PendingOperation::None) {
        return false;
    }
    if (state_.lifecycle == AnchorLifecycle::Error &&
        !state_.persisted) {
        ClearLiveSpace();
        RemoveRecord();
        ResetEmpty();
        LogInfo("Discarded failed ephemeral anchor state");
        return true;
    }
    if (!state_.hasUuid && space_ == XR_NULL_HANDLE) {
        return false;
    }
    XrSpace space = space_;
    XrUuidEXT uuid = NativeUuid();
    XrSpacesEraseInfoMETA eraseInfo{XR_TYPE_SPACES_ERASE_INFO_META};
    if (space_ != XR_NULL_HANDLE) {
        eraseInfo.spaceCount = 1;
        eraseInfo.spaces = &space;
    } else {
        eraseInfo.uuidCount = 1;
        eraseInfo.uuids = &uuid;
    }
    XrAsyncRequestIdFB requestId = 0;
    const XrResult result =
        eraseSpaces_(session_, &eraseInfo, &requestId);
    if (XR_FAILED(result)) {
        return Fail(result, "xrEraseSpacesMETA");
    }
    requestId_ = requestId;
    pending_ = PendingOperation::Erase;
    SetLifecycle(AnchorLifecycle::Erasing);
    LogInfo(
        "Anchor erase started request=%llu",
        static_cast<unsigned long long>(requestId_));
    return true;
}

bool MetaSpatialAnchorManager::HandleEvent(
    const XrEventDataBuffer& event) {
    switch (event.type) {
        case XR_TYPE_EVENT_DATA_SPATIAL_ANCHOR_CREATE_COMPLETE_FB: {
            const auto& complete =
                *reinterpret_cast<
                    const XrEventDataSpatialAnchorCreateCompleteFB*>(&event);
            if (!Matches(PendingOperation::Create, complete.requestId)) {
                return true;
            }
            pending_ = PendingOperation::None;
            if (XR_FAILED(complete.result) ||
                complete.space == XR_NULL_HANDLE) {
                return Fail(
                    XR_FAILED(complete.result)
                        ? complete.result
                        : XR_ERROR_RUNTIME_FAILURE,
                    "anchor create completion");
            }
            space_ = complete.space;
            XrUuidEXT runtimeUuid{};
            const XrResult uuidResult = getSpaceUuid_(space_, &runtimeUuid);
            if (XR_FAILED(uuidResult)) {
                return Fail(uuidResult, "xrGetSpaceUuidFB");
            }
            if (!(PortableUuid(runtimeUuid) ==
                  PortableUuid(complete.uuid))) {
                return Fail(
                    XR_ERROR_RUNTIME_FAILURE,
                    "created anchor UUID mismatch");
            }
            state_.uuid = PortableUuid(runtimeUuid);
            state_.hasUuid = true;
            LogInfo(
                "Anchor created uuid=%s",
                FormatAnchorUuid(state_.uuid).c_str());
            return ContinueComponents();
        }
        case XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB: {
            const auto& complete =
                *reinterpret_cast<
                    const XrEventDataSpaceSetStatusCompleteFB*>(&event);
            const PendingOperation expected =
                complete.componentType ==
                        XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB
                    ? PendingOperation::EnableLocatable
                    : PendingOperation::EnableStorable;
            if (!Matches(expected, complete.requestId)) {
                return true;
            }
            pending_ = PendingOperation::None;
            if (XR_FAILED(complete.result) ||
                complete.enabled != XR_TRUE) {
                return Fail(
                    XR_FAILED(complete.result)
                        ? complete.result
                        : XR_ERROR_RUNTIME_FAILURE,
                    "component enable completion");
            }
            return ContinueComponents();
        }
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB: {
            const auto& available =
                *reinterpret_cast<
                    const XrEventDataSpaceQueryResultsAvailableFB*>(&event);
            if (!Matches(PendingOperation::Query, available.requestId)) {
                return true;
            }
            return RetrieveQueryResults(available.requestId);
        }
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB: {
            const auto& complete =
                *reinterpret_cast<
                    const XrEventDataSpaceQueryCompleteFB*>(&event);
            if (!Matches(PendingOperation::Query, complete.requestId)) {
                return true;
            }
            if (XR_FAILED(complete.result)) {
                return Fail(complete.result, "anchor restore completion");
            }
            queryComplete_ = true;
            if (!queryResultsReceived_) {
                return true;
            }
            pending_ = PendingOperation::None;
            if (space_ == XR_NULL_HANDLE) {
                LogInfo("Saved anchor UUID no longer exists; clearing record");
                RemoveRecord();
                ResetEmpty();
                return true;
            }
            return ContinueComponents();
        }
        case XR_TYPE_EVENT_DATA_SPACES_SAVE_RESULT_META: {
            const auto& complete =
                *reinterpret_cast<
                    const XrEventDataSpacesSaveResultMETA*>(&event);
            if (!Matches(PendingOperation::Save, complete.requestId)) {
                return true;
            }
            pending_ = PendingOperation::None;
            if (XR_FAILED(complete.result)) {
                return Fail(complete.result, "anchor save completion");
            }
            state_.persisted = true;
            if (!WriteRecord()) {
                LogError(
                    "Anchor saved by runtime but UUID record failed; "
                    "issuing compensating erase");
                compensatingErase_ = true;
                return Erase();
            }
            SetLifecycle(AnchorLifecycle::Ready);
            LogInfo(
                "Anchor persisted uuid=%s",
                FormatAnchorUuid(state_.uuid).c_str());
            return true;
        }
        case XR_TYPE_EVENT_DATA_SPACES_ERASE_RESULT_META: {
            const auto& complete =
                *reinterpret_cast<
                    const XrEventDataSpacesEraseResultMETA*>(&event);
            if (!Matches(PendingOperation::Erase, complete.requestId)) {
                return true;
            }
            pending_ = PendingOperation::None;
            if (XR_FAILED(complete.result)) {
                return Fail(complete.result, "anchor erase completion");
            }
            RemoveRecord();
            ClearLiveSpace();
            const bool compensation = compensatingErase_;
            compensatingErase_ = false;
            ResetEmpty();
            if (compensation) {
                state_.lastResult = XR_ERROR_RUNTIME_FAILURE;
                SetLifecycle(AnchorLifecycle::Error);
                LogError("Compensating anchor erase completed");
            } else {
                LogInfo("Persistent anchor erased");
            }
            return true;
        }
        default:
            return true;
    }
}

bool MetaSpatialAnchorManager::RetrieveQueryResults(
    XrAsyncRequestIdFB requestId) {
    XrSpaceQueryResultsFB results{XR_TYPE_SPACE_QUERY_RESULTS_FB};
    XrResult result =
        retrieveQueryResults_(session_, requestId, &results);
    if (XR_FAILED(result)) {
        return Fail(result, "xrRetrieveSpaceQueryResultsFB(count)");
    }
    std::vector<XrSpaceQueryResultFB> entries(results.resultCountOutput);
    results.resultCapacityInput = static_cast<uint32_t>(entries.size());
    results.results = entries.data();
    result = retrieveQueryResults_(session_, requestId, &results);
    if (XR_FAILED(result)) {
        return Fail(result, "xrRetrieveSpaceQueryResultsFB(data)");
    }
    entries.resize(results.resultCountOutput);
    queryResultsReceived_ = true;
    if (entries.size() != 1 ||
        !(PortableUuid(entries[0].uuid) == state_.uuid)) {
        for (const XrSpaceQueryResultFB& entry : entries) {
            if (entry.space != XR_NULL_HANDLE) {
                xrDestroySpace(entry.space);
            }
        }
        if (!entries.empty()) {
            return Fail(
                XR_ERROR_RUNTIME_FAILURE,
                "restore query returned unexpected anchors");
        }
    } else {
        space_ = entries[0].space;
        LogInfo("Restore query returned matching anchor");
    }
    if (queryComplete_) {
        pending_ = PendingOperation::None;
        if (space_ == XR_NULL_HANDLE) {
            RemoveRecord();
            ResetEmpty();
            return true;
        }
        return ContinueComponents();
    }
    return true;
}

bool MetaSpatialAnchorManager::WriteRecord() {
    const std::string temporaryPath = recordPath_ + ".tmp";
    const std::string contents = SerializeAnchorRecord(state_.uuid);
    FILE* file = std::fopen(temporaryPath.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool written =
        std::fwrite(contents.data(), 1, contents.size(), file) ==
            contents.size() &&
        std::fflush(file) == 0 &&
        fsync(fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed ||
        std::rename(temporaryPath.c_str(), recordPath_.c_str()) != 0) {
        std::remove(temporaryPath.c_str());
        return false;
    }
    return true;
}

void MetaSpatialAnchorManager::RemoveRecord() {
    if (recordPath_.empty()) {
        return;
    }
    if (std::remove(recordPath_.c_str()) != 0 && errno != ENOENT) {
        LogError(
            "Could not remove anchor record (errno=%d)",
            errno);
    }
}

void MetaSpatialAnchorManager::ClearLiveSpace() {
    if (space_ != XR_NULL_HANDLE) {
        CheckXr(instance_, xrDestroySpace(space_), "xrDestroySpace(anchor)");
        space_ = XR_NULL_HANDLE;
    }
    lastLocationValid_ = false;
}

void MetaSpatialAnchorManager::ResetEmpty() {
    state_ = {};
    pending_ = PendingOperation::None;
    requestId_ = 0;
    restoreFlow_ = false;
    queryResultsReceived_ = false;
    queryComplete_ = false;
    SetLifecycle(AnchorLifecycle::Empty);
}

bool MetaSpatialAnchorManager::Fail(
    XrResult result,
    const char* operation) {
    LogError("%s failed (result=%d)", operation, result);
    state_.lastResult = result;
    pending_ = PendingOperation::None;
    requestId_ = 0;
    SetLifecycle(AnchorLifecycle::Error);
    return true;
}

bool MetaSpatialAnchorManager::Matches(
    PendingOperation operation,
    XrAsyncRequestIdFB requestId) {
    if (pending_ == operation && requestId_ == requestId) {
        return true;
    }
    LogInfo(
        "Ignoring unrelated/stale anchor event request=%llu",
        static_cast<unsigned long long>(requestId));
    return false;
}

void MetaSpatialAnchorManager::SetLifecycle(
    AnchorLifecycle lifecycle) {
    if (state_.lifecycle == lifecycle) {
        return;
    }
    LogInfo(
        "Anchor lifecycle: %s -> %s",
        AnchorLifecycleName(state_.lifecycle),
        AnchorLifecycleName(lifecycle));
    state_.lifecycle = lifecycle;
}

XrUuidEXT MetaSpatialAnchorManager::NativeUuid() const {
    XrUuidEXT uuid{};
    std::copy(
        state_.uuid.bytes.begin(),
        state_.uuid.bytes.end(),
        uuid.data);
    return uuid;
}

AnchorUuid MetaSpatialAnchorManager::PortableUuid(
    const XrUuidEXT& uuid) {
    AnchorUuid portable;
    std::copy(
        std::begin(uuid.data),
        std::end(uuid.data),
        portable.bytes.begin());
    return portable;
}

void MetaSpatialAnchorManager::Shutdown() {
    ClearLiveSpace();
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
    requestId_ = 0;
    pending_ = PendingOperation::None;
    recordPath_.clear();
    restoreRequested_ = false;
    restoreFlow_ = false;
    queryResultsReceived_ = false;
    queryComplete_ = false;
    compensatingErase_ = false;
    state_ = {};
    createSpatialAnchor_ = nullptr;
    getSpaceUuid_ = nullptr;
    enumerateComponents_ = nullptr;
    getComponentStatus_ = nullptr;
    setComponentStatus_ = nullptr;
    saveSpaces_ = nullptr;
    eraseSpaces_ = nullptr;
    querySpaces_ = nullptr;
    retrieveQueryResults_ = nullptr;
}

}  // namespace questlab
