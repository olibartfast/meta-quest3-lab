#include "xr_core/vulkan_session_binding.h"

#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace questlab {
namespace {

template <typename FunctionType>
bool LoadXrFunction(
    XrInstance instance,
    const char* name,
    FunctionType* function) {
    return CheckXr(
        instance,
        xrGetInstanceProcAddr(
            instance,
            name,
            reinterpret_cast<PFN_xrVoidFunction*>(function)),
        name);
}

bool HasLayer(
    const std::vector<VkLayerProperties>& layers,
    const char* name) {
    return std::any_of(
        layers.begin(),
        layers.end(),
        [name](const VkLayerProperties& layer) {
            return std::strcmp(layer.layerName, name) == 0;
        });
}

bool HasExtension(
    const std::vector<VkExtensionProperties>& extensions,
    const char* name) {
    return std::any_of(
        extensions.begin(),
        extensions.end(),
        [name](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*) {
    const char* message =
        callbackData != nullptr && callbackData->pMessage != nullptr
        ? callbackData->pMessage
        : "Vulkan validation message without text";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        LogError("Vulkan validation: %s", message);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        LogWarning("Vulkan validation: %s", message);
    } else {
        LogInfo("Vulkan validation: %s", message);
    }
    return VK_FALSE;
}

}  // namespace

VulkanSessionBinding::~VulkanSessionBinding() {
    Shutdown();
}

bool VulkanSessionBinding::Initialize(
    const XrInstanceContext& xrContext,
    VulkanBindingOptions options) {
    if (device_ != VK_NULL_HANDLE) {
        return true;
    }

    const XrInstance xrInstance = xrContext.Instance();
    PFN_xrGetVulkanGraphicsRequirements2KHR getRequirements = nullptr;
    PFN_xrCreateVulkanInstanceKHR createVulkanInstance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR getGraphicsDevice = nullptr;
    PFN_xrCreateVulkanDeviceKHR createVulkanDevice = nullptr;
    if (!LoadXrFunction(
            xrInstance,
            "xrGetVulkanGraphicsRequirements2KHR",
            &getRequirements) ||
        !LoadXrFunction(
            xrInstance,
            "xrCreateVulkanInstanceKHR",
            &createVulkanInstance) ||
        !LoadXrFunction(
            xrInstance,
            "xrGetVulkanGraphicsDevice2KHR",
            &getGraphicsDevice) ||
        !LoadXrFunction(
            xrInstance,
            "xrCreateVulkanDeviceKHR",
            &createVulkanDevice)) {
        return false;
    }

    XrGraphicsRequirementsVulkan2KHR requirements{
        XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (!CheckXr(
            xrInstance,
            getRequirements(xrInstance, xrContext.SystemId(), &requirements),
            "xrGetVulkanGraphicsRequirements2KHR")) {
        return false;
    }
    LogInfo(
        "Vulkan API supported by runtime: min %u.%u.%u, max %u.%u.%u",
        XR_VERSION_MAJOR(requirements.minApiVersionSupported),
        XR_VERSION_MINOR(requirements.minApiVersionSupported),
        XR_VERSION_PATCH(requirements.minApiVersionSupported),
        XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
        XR_VERSION_MINOR(requirements.maxApiVersionSupported),
        XR_VERSION_PATCH(requirements.maxApiVersionSupported));

    const uint32_t requestedApiVersion = VK_API_VERSION_1_1;
    const XrVersion requestedXrVersion = XR_MAKE_VERSION(1, 1, 0);
    if (requestedXrVersion < requirements.minApiVersionSupported ||
        requestedXrVersion > requirements.maxApiVersionSupported) {
        LogError("Runtime does not support the requested Vulkan 1.1 API version");
        return false;
    }

    std::vector<const char*> enabledLayers;
    std::vector<const char*> enabledExtensions;
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    if (options.enableValidation) {
        uint32_t layerCount = 0;
        if (!CheckVk(
                vkEnumerateInstanceLayerProperties(&layerCount, nullptr),
                "vkEnumerateInstanceLayerProperties(count)")) {
            return false;
        }
        std::vector<VkLayerProperties> layers(layerCount);
        if (!CheckVk(
                vkEnumerateInstanceLayerProperties(&layerCount, layers.data()),
                "vkEnumerateInstanceLayerProperties(list)")) {
            return false;
        }

        uint32_t extensionCount = 0;
        if (!CheckVk(
                vkEnumerateInstanceExtensionProperties(
                    nullptr, &extensionCount, nullptr),
                "vkEnumerateInstanceExtensionProperties(count)")) {
            return false;
        }
        std::vector<VkExtensionProperties> extensions(extensionCount);
        if (!CheckVk(
                vkEnumerateInstanceExtensionProperties(
                    nullptr, &extensionCount, extensions.data()),
                "vkEnumerateInstanceExtensionProperties(list)")) {
            return false;
        }

        constexpr const char* kValidationLayer =
            "VK_LAYER_KHRONOS_validation";
        if (HasLayer(layers, kValidationLayer) &&
            HasExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            enabledLayers.push_back(kValidationLayer);
            enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debugCreateInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = DebugCallback;
            LogInfo("Vulkan validation requested and available");
        } else {
            LogWarning(
                "Vulkan validation requested but the layer or debug-utils "
                "extension is unavailable; continuing without validation");
        }
    }

    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "QuestLab Native XR";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "questlab";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = requestedApiVersion;

    VkInstanceCreateInfo instanceCreateInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceCreateInfo.pApplicationInfo = &applicationInfo;
    instanceCreateInfo.enabledLayerCount =
        static_cast<uint32_t>(enabledLayers.size());
    instanceCreateInfo.ppEnabledLayerNames = enabledLayers.data();
    instanceCreateInfo.enabledExtensionCount =
        static_cast<uint32_t>(enabledExtensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
    if (!enabledLayers.empty()) {
        instanceCreateInfo.pNext = &debugCreateInfo;
    }
    XrVulkanInstanceCreateInfoKHR xrInstanceCreateInfo{
        XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrInstanceCreateInfo.systemId = xrContext.SystemId();
    xrInstanceCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrInstanceCreateInfo.vulkanCreateInfo = &instanceCreateInfo;

    VkResult vkResult = VK_SUCCESS;
    if (!CheckXr(
            xrInstance,
            createVulkanInstance(
                xrInstance,
                &xrInstanceCreateInfo,
                &instance_,
                &vkResult),
            "xrCreateVulkanInstanceKHR") ||
        !CheckVk(vkResult, "vkCreateInstance (via OpenXR)")) {
        Shutdown();
        return false;
    }

    if (!enabledLayers.empty()) {
        const auto createDebugMessenger =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(
                    instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (createDebugMessenger == nullptr ||
            !CheckVk(
                createDebugMessenger(
                    instance_,
                    &debugCreateInfo,
                    nullptr,
                    &debugMessenger_),
                "vkCreateDebugUtilsMessengerEXT")) {
            Shutdown();
            return false;
        }
    }

    XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{
        XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    deviceGetInfo.systemId = xrContext.SystemId();
    deviceGetInfo.vulkanInstance = instance_;
    if (!CheckXr(
            xrInstance,
            getGraphicsDevice(xrInstance, &deviceGetInfo, &physicalDevice_),
            "xrGetVulkanGraphicsDevice2KHR")) {
        Shutdown();
        return false;
    }

    VkPhysicalDeviceProperties physicalDeviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &physicalDeviceProperties);
    LogInfo("Vulkan physical device: %s", physicalDeviceProperties.deviceName);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice_,
        &queueFamilyCount,
        nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice_,
        &queueFamilyCount,
        queueFamilies.data());
    queueFamilyIndex_ = std::numeric_limits<uint32_t>::max();
    for (uint32_t index = 0; index < queueFamilyCount; ++index) {
        if (queueFamilies[index].queueCount > 0 &&
            (queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            queueFamilyIndex_ = index;
            break;
        }
    }
    if (queueFamilyIndex_ == std::numeric_limits<uint32_t>::max()) {
        LogError("No Vulkan graphics queue family is available");
        Shutdown();
        return false;
    }

    constexpr float kQueuePriority = 1.0F;
    VkDeviceQueueCreateInfo queueCreateInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex_;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &kQueuePriority;

    VkDeviceCreateInfo deviceCreateInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    XrVulkanDeviceCreateInfoKHR xrDeviceCreateInfo{
        XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrDeviceCreateInfo.systemId = xrContext.SystemId();
    xrDeviceCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrDeviceCreateInfo.vulkanPhysicalDevice = physicalDevice_;
    xrDeviceCreateInfo.vulkanCreateInfo = &deviceCreateInfo;

    vkResult = VK_SUCCESS;
    if (!CheckXr(
            xrInstance,
            createVulkanDevice(
                xrInstance,
                &xrDeviceCreateInfo,
                &device_,
                &vkResult),
            "xrCreateVulkanDeviceKHR") ||
        !CheckVk(vkResult, "vkCreateDevice (via OpenXR)")) {
        Shutdown();
        return false;
    }
    vkGetDeviceQueue(device_, queueFamilyIndex_, 0, &queue_);
    if (queue_ == VK_NULL_HANDLE) {
        LogError("vkGetDeviceQueue returned a null graphics queue");
        Shutdown();
        return false;
    }

    binding_.instance = instance_;
    binding_.physicalDevice = physicalDevice_;
    binding_.device = device_;
    binding_.queueFamilyIndex = queueFamilyIndex_;
    binding_.queueIndex = 0;
    context_.instance = instance_;
    context_.physicalDevice = physicalDevice_;
    context_.device = device_;
    context_.queue = queue_;
    context_.queueFamilyIndex = queueFamilyIndex_;
    LogInfo("Minimal Vulkan device and graphics queue created");
    return true;
}

void VulkanSessionBinding::Shutdown() {
    context_ = {};
    binding_.instance = VK_NULL_HANDLE;
    binding_.physicalDevice = VK_NULL_HANDLE;
    binding_.device = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        LogInfo("Vulkan device destroyed");
    }
    physicalDevice_ = VK_NULL_HANDLE;
    if (debugMessenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        const auto destroyDebugMessenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(
                    instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessenger != nullptr) {
            destroyDebugMessenger(instance_, debugMessenger_, nullptr);
        }
        debugMessenger_ = VK_NULL_HANDLE;
        LogInfo("Vulkan debug messenger destroyed");
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
        LogInfo("Vulkan instance destroyed");
    }
}

}  // namespace questlab
