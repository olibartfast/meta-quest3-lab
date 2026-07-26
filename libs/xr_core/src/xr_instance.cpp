#include "xr_core/xr_instance.h"

#include "xr_core/xr_error.h"

#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

namespace questlab {
namespace {

bool HasExtension(
    const std::vector<XrExtensionProperties>& extensions,
    const char* requiredName) {
    return std::any_of(
        extensions.begin(),
        extensions.end(),
        [requiredName](const XrExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, requiredName) == 0;
        });
}

}  // namespace

XrInstanceContext::~XrInstanceContext() {
    Shutdown();
}

bool XrInstanceContext::Initialize(
    void* applicationVm,
    void* applicationActivity,
    const XrInstanceOptions& options) {
    if (instance_ != XR_NULL_HANDLE) {
        return true;
    }

    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    if (!CheckXr(
            XR_NULL_HANDLE,
            xrGetInstanceProcAddr(
                XR_NULL_HANDLE,
                "xrInitializeLoaderKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader)),
            "xrGetInstanceProcAddr(xrInitializeLoaderKHR)")) {
        return false;
    }

    XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInfo.applicationVM = applicationVm;
    loaderInfo.applicationContext = applicationActivity;
    if (!CheckXr(
            XR_NULL_HANDLE,
            initializeLoader(
                reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)),
            "xrInitializeLoaderKHR")) {
        return false;
    }

    uint32_t extensionCount = 0;
    if (!CheckXr(
            XR_NULL_HANDLE,
            xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr),
            "xrEnumerateInstanceExtensionProperties(count)")) {
        return false;
    }
    std::vector<XrExtensionProperties> extensions(
        extensionCount,
        XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
    if (!CheckXr(
            XR_NULL_HANDLE,
            xrEnumerateInstanceExtensionProperties(
                nullptr,
                extensionCount,
                &extensionCount,
                extensions.data()),
            "xrEnumerateInstanceExtensionProperties(list)")) {
        return false;
    }
    LogInfo("Runtime exposes %u OpenXR extensions", extensionCount);
    for (const XrExtensionProperties& extension : extensions) {
        LogInfo("Extension: %s v%u", extension.extensionName, extension.extensionVersion);
    }

    constexpr const char* kRequiredExtensions[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
    };
    for (const char* requiredExtension : kRequiredExtensions) {
        if (!HasExtension(extensions, requiredExtension)) {
            LogError("Required OpenXR extension is unavailable: %s", requiredExtension);
            return false;
        }
    }
    std::vector<const char*> enabledExtensions(
        std::begin(kRequiredExtensions),
        std::end(kRequiredExtensions));
    for (const char* additionalExtension : options.additionalExtensions) {
        if (additionalExtension == nullptr ||
            additionalExtension[0] == '\0') {
            LogError("An additional OpenXR extension name is empty");
            return false;
        }
        if (!HasExtension(extensions, additionalExtension)) {
            LogError(
                "Required OpenXR extension is unavailable: %s",
                additionalExtension);
            return false;
        }
        const auto alreadyEnabled = std::find_if(
            enabledExtensions.begin(),
            enabledExtensions.end(),
            [additionalExtension](const char* enabledExtension) {
                return std::strcmp(
                    enabledExtension, additionalExtension) == 0;
            });
        if (alreadyEnabled == enabledExtensions.end()) {
            enabledExtensions.push_back(additionalExtension);
        }
    }

    XrInstanceCreateInfoAndroidKHR androidInfo{
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = applicationVm;
    androidInfo.applicationActivity = applicationActivity;

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = &androidInfo;
    const char* applicationName =
        options.applicationName != nullptr && options.applicationName[0] != '\0'
            ? options.applicationName
            : "QuestLab";
    std::strncpy(
        createInfo.applicationInfo.applicationName,
        applicationName,
        XR_MAX_APPLICATION_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = options.applicationVersion;
    std::strncpy(
        createInfo.applicationInfo.engineName,
        "questlab",
        XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(enabledExtensions.size());
    createInfo.enabledExtensionNames = enabledExtensions.data();
    if (!CheckXr(
            XR_NULL_HANDLE,
            xrCreateInstance(&createInfo, &instance_),
            "xrCreateInstance")) {
        return false;
    }

    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    if (!CheckXr(
            instance_,
            xrGetInstanceProperties(instance_, &instanceProperties),
            "xrGetInstanceProperties")) {
        Shutdown();
        return false;
    }
    LogInfo(
        "OpenXR runtime: %s %u.%u.%u",
        instanceProperties.runtimeName,
        XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
        XR_VERSION_MINOR(instanceProperties.runtimeVersion),
        XR_VERSION_PATCH(instanceProperties.runtimeVersion));

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!CheckXr(
            instance_,
            xrGetSystem(instance_, &systemInfo, &systemId_),
            "xrGetSystem(HMD)")) {
        Shutdown();
        return false;
    }

    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    if (!CheckXr(
            instance_,
            xrGetSystemProperties(instance_, systemId_, &systemProperties),
            "xrGetSystemProperties")) {
        Shutdown();
        return false;
    }
    LogInfo(
        "OpenXR system: %s (vendor=%u, id=%llu)",
        systemProperties.systemName,
        systemProperties.vendorId,
        static_cast<unsigned long long>(systemId_));
    return true;
}

void XrInstanceContext::Shutdown() {
    systemId_ = XR_NULL_SYSTEM_ID;
    if (instance_ != XR_NULL_HANDLE) {
        const XrInstance instance = instance_;
        instance_ = XR_NULL_HANDLE;
        CheckXr(instance, xrDestroyInstance(instance), "xrDestroyInstance");
        LogInfo("OpenXR instance destroyed");
    }
}

}  // namespace questlab
