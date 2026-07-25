#include "xr_core/vulkan_session_binding.h"

#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"

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

}  // namespace

VulkanSessionBinding::~VulkanSessionBinding() {
    Shutdown();
}

bool VulkanSessionBinding::Initialize(const XrInstanceContext& xrContext) {
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

    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "OpenXR Bootstrap";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "questlab";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = requestedApiVersion;

    VkInstanceCreateInfo instanceCreateInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceCreateInfo.pApplicationInfo = &applicationInfo;
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
    LogInfo("Minimal Vulkan device and graphics queue created");
    return true;
}

void VulkanSessionBinding::Shutdown() {
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
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
        LogInfo("Vulkan instance destroyed");
    }
}

}  // namespace questlab
