#pragma once

#include <jni.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace questlab {

class XrInstanceContext;

struct VulkanBindingOptions {
    bool enableValidation = false;
};

struct VulkanDeviceContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
};

class VulkanSessionBinding {
public:
    VulkanSessionBinding() = default;
    ~VulkanSessionBinding();

    VulkanSessionBinding(const VulkanSessionBinding&) = delete;
    VulkanSessionBinding& operator=(const VulkanSessionBinding&) = delete;

    bool Initialize(
        const XrInstanceContext& xrContext,
        VulkanBindingOptions options = {});
    void Shutdown();

    const XrGraphicsBindingVulkan2KHR* GraphicsBinding() const { return &binding_; }
    const VulkanDeviceContext& DeviceContext() const { return context_; }

private:
    XrGraphicsBindingVulkan2KHR binding_{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    VulkanDeviceContext context_{};
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex_ = 0;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
};

}  // namespace questlab
