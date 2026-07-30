#include "vulkan_renderer/vulkan_stereo_renderer.h"

#include "xr_core/xr_error.h"
#include "xr_math/openxr_conversions.h"

#include <openxr/openxr_platform.h>

#include <array>
#include <cstring>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace questlab {
namespace {

VkFormat SelectSwapchainFormat(const std::vector<int64_t>& formats) {
    constexpr std::array<VkFormat, 4> kPreferences = {
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
    };
    for (VkFormat preference : kPreferences) {
        for (int64_t format : formats) {
            if (format == static_cast<int64_t>(preference)) {
                return preference;
            }
        }
    }
    return VK_FORMAT_UNDEFINED;
}

constexpr uint32_t kTriangleVertexShader[] =
#include "triangle.vert.inc"
;

constexpr uint32_t kFragmentShader[] =
#include "triangle.frag.inc"
;

constexpr uint32_t kDebugLineVertexShader[] =
#include "debug_lines.vert.inc"
;

constexpr uint32_t kImageQuadVertexShader[] =
#include "image_quad.vert.inc"
;

constexpr uint32_t kImageQuadFragmentShader[] =
#include "image_quad.frag.inc"
;

struct DebugPushConstants {
    math::Mat4 modelViewProjection;
    std::array<float, 4> color{};
    int32_t shape = 0;
    std::array<int32_t, 3> padding{};
};

uint32_t DebugVertexCount(DebugLineShape shape) {
    switch (shape) {
        case DebugLineShape::Axes: return 6U;
        case DebugLineShape::Rectangle: return 8U;
        case DebugLineShape::Ray: return 2U;
        case DebugLineShape::Box: return 24U;
        case DebugLineShape::ScreenRectangle: return 8U;
    }
    return 0U;
}

}  // namespace

struct VulkanStereoRenderer::Impl {
    struct ImageResources {
        VkImageView imageView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    struct EyeSwapchain {
        XrSwapchain handle = XR_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<XrSwapchainImageVulkan2KHR> images;
        std::vector<ImageResources> resources;
    };

    XrInstance xrInstance = XR_NULL_HANDLE;
    XrSession xrSession = XR_NULL_HANDLE;
    VulkanDeviceContext context{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout imageDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool imageDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet imageDescriptorSet = VK_NULL_HANDLE;
    VkPipeline trianglePipeline = VK_NULL_HANDLE;
    VkPipeline debugLinePipeline = VK_NULL_HANDLE;
    VkPipeline imageQuadPipeline = VK_NULL_HANDLE;
    VkImage cameraImage = VK_NULL_HANDLE;
    VkDeviceMemory cameraImageMemory = VK_NULL_HANDLE;
    VkImageView cameraImageView = VK_NULL_HANDLE;
    VkSampler cameraSampler = VK_NULL_HANDLE;
    uint32_t cameraImageWidth = 0;
    uint32_t cameraImageHeight = 0;
    uint64_t uploadedFrameId = 0;
    RgbaImageQuad frameImage;
    bool frameHasImage = false;
    VulkanSceneProvider* sceneProvider = nullptr;
    std::vector<DebugLineDraw> frameDraws;
    std::array<EyeSwapchain, 2> eyes;
    std::array<XrCompositionLayerProjectionView, 2> projectionViews = {
        XrCompositionLayerProjectionView{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        XrCompositionLayerProjectionView{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
    };
    XrCompositionLayerProjection projectionLayer{
        XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    bool transparentBackground = false;
    bool initialized = false;

    bool CreateSwapchains(
        const std::vector<XrViewConfigurationView>& viewConfigurations) {
        uint32_t formatCount = 0;
        if (!CheckXr(
                xrInstance,
                xrEnumerateSwapchainFormats(
                    xrSession, 0, &formatCount, nullptr),
                "xrEnumerateSwapchainFormats(count)")) {
            return false;
        }
        std::vector<int64_t> formats(formatCount);
        if (!CheckXr(
                xrInstance,
                xrEnumerateSwapchainFormats(
                    xrSession,
                    formatCount,
                    &formatCount,
                    formats.data()),
                "xrEnumerateSwapchainFormats(list)")) {
            return false;
        }
        colorFormat = SelectSwapchainFormat(formats);
        if (colorFormat == VK_FORMAT_UNDEFINED) {
            LogError("No supported RGBA Vulkan swapchain format is available");
            return false;
        }
        if (colorFormat == VK_FORMAT_R8G8B8A8_UNORM ||
            colorFormat == VK_FORMAT_B8G8R8A8_UNORM) {
            LogWarning(
                "No sRGB swapchain format is available; using UNORM format %d",
                colorFormat);
        } else {
            LogInfo("Selected sRGB Vulkan swapchain format %d", colorFormat);
        }

        for (uint32_t eyeIndex = 0; eyeIndex < eyes.size(); ++eyeIndex) {
            const XrViewConfigurationView& view =
                viewConfigurations[eyeIndex];
            EyeSwapchain& eye = eyes[eyeIndex];
            eye.width = view.recommendedImageRectWidth;
            eye.height = view.recommendedImageRectHeight;
            if (eye.width == 0 || eye.height == 0) {
                LogError("Stereo view %u reported a zero-sized extent", eyeIndex);
                return false;
            }

            XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            createInfo.format = static_cast<int64_t>(colorFormat);
            createInfo.sampleCount = 1;
            createInfo.width = eye.width;
            createInfo.height = eye.height;
            createInfo.faceCount = 1;
            createInfo.arraySize = 1;
            createInfo.mipCount = 1;
            if (!CheckXr(
                    xrInstance,
                    xrCreateSwapchain(
                        xrSession, &createInfo, &eye.handle),
                    "xrCreateSwapchain(color)")) {
                return false;
            }

            uint32_t imageCount = 0;
            if (!CheckXr(
                    xrInstance,
                    xrEnumerateSwapchainImages(
                        eye.handle, 0, &imageCount, nullptr),
                    "xrEnumerateSwapchainImages(count)")) {
                return false;
            }
            eye.images.assign(
                imageCount,
                XrSwapchainImageVulkan2KHR{
                    XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
            if (!CheckXr(
                    xrInstance,
                    xrEnumerateSwapchainImages(
                        eye.handle,
                        imageCount,
                        &imageCount,
                        reinterpret_cast<XrSwapchainImageBaseHeader*>(
                            eye.images.data())),
                    "xrEnumerateSwapchainImages(list)")) {
                return false;
            }
            eye.resources.resize(imageCount);
            LogInfo(
                "Eye %u color swapchain: %ux%u, %u images",
                eyeIndex,
                eye.width,
                eye.height,
                imageCount);
        }
        return true;
    }

    bool CreateImageViews() {
        for (EyeSwapchain& eye : eyes) {
            for (uint32_t index = 0; index < eye.images.size(); ++index) {
                VkImageViewCreateInfo createInfo{
                    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                createInfo.image = eye.images[index].image;
                createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                createInfo.format = colorFormat;
                createInfo.components = {
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                };
                createInfo.subresourceRange.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                createInfo.subresourceRange.levelCount = 1;
                createInfo.subresourceRange.layerCount = 1;
                if (!CheckVk(
                        vkCreateImageView(
                            context.device,
                            &createInfo,
                            nullptr,
                            &eye.resources[index].imageView),
                        "vkCreateImageView")) {
                    return false;
                }
            }
        }
        return true;
    }

    bool CreateRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = colorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;
        return CheckVk(
            vkCreateRenderPass(
                context.device, &createInfo, nullptr, &renderPass),
            "vkCreateRenderPass");
    }

    bool CreateShaderModule(
        const uint32_t* code,
        size_t byteCount,
        VkShaderModule* module) {
        VkShaderModuleCreateInfo createInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = byteCount;
        createInfo.pCode = code;
        return CheckVk(
            vkCreateShaderModule(
                context.device, &createInfo, nullptr, module),
            "vkCreateShaderModule");
    }

    bool CreatePipeline() {
        VkShaderModule triangleVertexModule = VK_NULL_HANDLE;
        VkShaderModule debugLineVertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;
        VkShaderModule imageQuadVertexModule = VK_NULL_HANDLE;
        VkShaderModule imageQuadFragmentModule = VK_NULL_HANDLE;
        if (!CreateShaderModule(
                kTriangleVertexShader,
                sizeof(kTriangleVertexShader),
                &triangleVertexModule) ||
            !CreateShaderModule(
                kDebugLineVertexShader,
                sizeof(kDebugLineVertexShader),
                &debugLineVertexModule) ||
            !CreateShaderModule(
                kFragmentShader,
                sizeof(kFragmentShader),
                &fragmentModule) ||
            !CreateShaderModule(
                kImageQuadVertexShader,
                sizeof(kImageQuadVertexShader),
                &imageQuadVertexModule) ||
            !CreateShaderModule(
                kImageQuadFragmentShader,
                sizeof(kImageQuadFragmentShader),
                &imageQuadFragmentModule)) {
            if (triangleVertexModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    context.device, triangleVertexModule, nullptr);
            }
            if (debugLineVertexModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    context.device, debugLineVertexModule, nullptr);
            }
            if (fragmentModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(context.device, fragmentModule, nullptr);
            }
            if (imageQuadVertexModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    context.device, imageQuadVertexModule, nullptr);
            }
            if (imageQuadFragmentModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    context.device, imageQuadFragmentModule, nullptr);
            }
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {{
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_VERTEX_BIT,
                triangleVertexModule,
                "main",
                nullptr,
            },
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                fragmentModule,
                "main",
                nullptr,
            },
        }};

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.size = sizeof(DebugPushConstants);

        VkDescriptorSetLayoutBinding imageBinding{};
        imageBinding.binding = 0;
        imageBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageBinding.descriptorCount = 1;
        imageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo descriptorLayoutCreateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorLayoutCreateInfo.bindingCount = 1;
        descriptorLayoutCreateInfo.pBindings = &imageBinding;
        bool succeeded = CheckVk(
            vkCreateDescriptorSetLayout(
                context.device,
                &descriptorLayoutCreateInfo,
                nullptr,
                &imageDescriptorSetLayout),
            "vkCreateDescriptorSetLayout(camera image)");

        VkPipelineLayoutCreateInfo layoutCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        layoutCreateInfo.setLayoutCount = 1;
        layoutCreateInfo.pSetLayouts = &imageDescriptorSetLayout;
        if (succeeded) {
            succeeded = CheckVk(
                vkCreatePipelineLayout(
                    context.device,
                    &layoutCreateInfo,
                    nullptr,
                    &pipelineLayout),
                "vkCreatePipelineLayout");
        }

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorBlendAttachment;
        constexpr std::array<VkDynamicState, 2> kDynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount =
            static_cast<uint32_t>(kDynamicStates.size());
        dynamicState.pDynamicStates = kDynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineCreateInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineCreateInfo.stageCount =
            static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.pVertexInputState = &vertexInput;
        pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterization;
        pipelineCreateInfo.pMultisampleState = &multisampling;
        pipelineCreateInfo.pColorBlendState = &colorBlend;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.renderPass = renderPass;
        pipelineCreateInfo.subpass = 0;
        if (succeeded) {
            succeeded = CheckVk(
                vkCreateGraphicsPipelines(
                    context.device,
                    VK_NULL_HANDLE,
                    1,
                    &pipelineCreateInfo,
                    nullptr,
                    &trianglePipeline),
                "vkCreateGraphicsPipelines(triangle)");
        }
        if (succeeded) {
            shaderStages[0].module = debugLineVertexModule;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            succeeded = CheckVk(
                vkCreateGraphicsPipelines(
                    context.device,
                    VK_NULL_HANDLE,
                    1,
                    &pipelineCreateInfo,
                    nullptr,
                    &debugLinePipeline),
                "vkCreateGraphicsPipelines(debug lines)");
        }
        if (succeeded) {
            shaderStages[0].module = imageQuadVertexModule;
            shaderStages[1].module = imageQuadFragmentModule;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            succeeded = CheckVk(
                vkCreateGraphicsPipelines(
                    context.device,
                    VK_NULL_HANDLE,
                    1,
                    &pipelineCreateInfo,
                    nullptr,
                    &imageQuadPipeline),
                "vkCreateGraphicsPipelines(camera image)");
        }

        VkDescriptorPoolSize descriptorPoolSize{};
        descriptorPoolSize.type =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorPoolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        descriptorPoolCreateInfo.maxSets = 1;
        descriptorPoolCreateInfo.poolSizeCount = 1;
        descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
        if (succeeded) {
            succeeded = CheckVk(
                vkCreateDescriptorPool(
                    context.device,
                    &descriptorPoolCreateInfo,
                    nullptr,
                    &imageDescriptorPool),
                "vkCreateDescriptorPool(camera image)");
        }
        VkDescriptorSetAllocateInfo descriptorAllocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        descriptorAllocateInfo.descriptorPool = imageDescriptorPool;
        descriptorAllocateInfo.descriptorSetCount = 1;
        descriptorAllocateInfo.pSetLayouts = &imageDescriptorSetLayout;
        if (succeeded) {
            succeeded = CheckVk(
                vkAllocateDescriptorSets(
                    context.device,
                    &descriptorAllocateInfo,
                    &imageDescriptorSet),
                "vkAllocateDescriptorSets(camera image)");
        }
        VkSamplerCreateInfo samplerCreateInfo{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCreateInfo.addressModeU =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.addressModeV =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.maxLod = 1.0F;
        if (succeeded) {
            succeeded = CheckVk(
                vkCreateSampler(
                    context.device,
                    &samplerCreateInfo,
                    nullptr,
                    &cameraSampler),
                "vkCreateSampler(camera image)");
        }

        vkDestroyShaderModule(context.device, fragmentModule, nullptr);
        vkDestroyShaderModule(
            context.device, imageQuadFragmentModule, nullptr);
        vkDestroyShaderModule(
            context.device, imageQuadVertexModule, nullptr);
        vkDestroyShaderModule(
            context.device, debugLineVertexModule, nullptr);
        vkDestroyShaderModule(
            context.device, triangleVertexModule, nullptr);
        return succeeded;
    }

    bool CreateFramebuffers() {
        for (EyeSwapchain& eye : eyes) {
            for (ImageResources& resource : eye.resources) {
                VkFramebufferCreateInfo createInfo{
                    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                createInfo.renderPass = renderPass;
                createInfo.attachmentCount = 1;
                createInfo.pAttachments = &resource.imageView;
                createInfo.width = eye.width;
                createInfo.height = eye.height;
                createInfo.layers = 1;
                if (!CheckVk(
                        vkCreateFramebuffer(
                            context.device,
                            &createInfo,
                            nullptr,
                            &resource.framebuffer),
                        "vkCreateFramebuffer")) {
                    return false;
                }
            }
        }
        return true;
    }

    bool CreateCommandResources() {
        VkCommandPoolCreateInfo poolCreateInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolCreateInfo.queueFamilyIndex = context.queueFamilyIndex;
        if (!CheckVk(
                vkCreateCommandPool(
                    context.device,
                    &poolCreateInfo,
                    nullptr,
                    &commandPool),
                "vkCreateCommandPool")) {
            return false;
        }

        for (EyeSwapchain& eye : eyes) {
            std::vector<VkCommandBuffer> commandBuffers(eye.resources.size());
            VkCommandBufferAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocateInfo.commandPool = commandPool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount =
                static_cast<uint32_t>(commandBuffers.size());
            if (!CheckVk(
                    vkAllocateCommandBuffers(
                        context.device,
                        &allocateInfo,
                        commandBuffers.data()),
                    "vkAllocateCommandBuffers")) {
                return false;
            }
            for (uint32_t index = 0; index < eye.resources.size(); ++index) {
                eye.resources[index].commandBuffer = commandBuffers[index];
                VkFenceCreateInfo fenceCreateInfo{
                    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                if (!CheckVk(
                        vkCreateFence(
                            context.device,
                            &fenceCreateInfo,
                            nullptr,
                            &eye.resources[index].fence),
                        "vkCreateFence")) {
                    return false;
                }
            }
        }
        return true;
    }

    bool FindMemoryType(
        uint32_t typeBits,
        VkMemoryPropertyFlags properties,
        uint32_t* typeIndex) const {
        if (typeIndex == nullptr) {
            return false;
        }
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(
            context.physicalDevice, &memoryProperties);
        for (uint32_t index = 0;
             index < memoryProperties.memoryTypeCount;
             ++index) {
            if ((typeBits & (1U << index)) != 0U &&
                (memoryProperties.memoryTypes[index].propertyFlags &
                    properties) == properties) {
                *typeIndex = index;
                return true;
            }
        }
        LogError(
            "No Vulkan memory type satisfies flags 0x%x",
            properties);
        return false;
    }

    void DestroyCameraImage() {
        if (context.device == VK_NULL_HANDLE) {
            return;
        }
        if (cameraImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(context.device, cameraImageView, nullptr);
            cameraImageView = VK_NULL_HANDLE;
        }
        if (cameraImage != VK_NULL_HANDLE) {
            vkDestroyImage(context.device, cameraImage, nullptr);
            cameraImage = VK_NULL_HANDLE;
        }
        if (cameraImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context.device, cameraImageMemory, nullptr);
            cameraImageMemory = VK_NULL_HANDLE;
        }
        cameraImageWidth = 0;
        cameraImageHeight = 0;
        uploadedFrameId = 0;
    }

    bool CreateCameraImage(uint32_t width, uint32_t height) {
        DestroyCameraImage();
        VkImageCreateInfo imageCreateInfo{
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageCreateInfo.extent = {width, height, 1};
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!CheckVk(
                vkCreateImage(
                    context.device,
                    &imageCreateInfo,
                    nullptr,
                    &cameraImage),
                "vkCreateImage(camera image)")) {
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(
            context.device, cameraImage, &requirements);
        uint32_t memoryTypeIndex = 0;
        if (!FindMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &memoryTypeIndex)) {
            return false;
        }
        VkMemoryAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;
        if (!CheckVk(
                vkAllocateMemory(
                    context.device,
                    &allocateInfo,
                    nullptr,
                    &cameraImageMemory),
                "vkAllocateMemory(camera image)") ||
            !CheckVk(
                vkBindImageMemory(
                    context.device,
                    cameraImage,
                    cameraImageMemory,
                    0),
                "vkBindImageMemory(camera image)")) {
            return false;
        }
        VkImageViewCreateInfo viewCreateInfo{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCreateInfo.image = cameraImage;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewCreateInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.layerCount = 1;
        if (!CheckVk(
                vkCreateImageView(
                    context.device,
                    &viewCreateInfo,
                    nullptr,
                    &cameraImageView),
                "vkCreateImageView(camera image)")) {
            return false;
        }
        cameraImageWidth = width;
        cameraImageHeight = height;
        return true;
    }

    bool UploadCameraImage(const RgbaImageQuad& image) {
        if (image.pixels == nullptr || image.width == 0 ||
            image.height == 0) {
            return false;
        }
        const VkDeviceSize byteCount =
            static_cast<VkDeviceSize>(image.width) *
            static_cast<VkDeviceSize>(image.height) * 4U;
        if (image.pixels->size() != byteCount) {
            LogError(
                "Camera image byte count %zu does not match %ux%u RGBA",
                image.pixels->size(),
                image.width,
                image.height);
            return false;
        }
        if (cameraImage == VK_NULL_HANDLE ||
            cameraImageWidth != image.width ||
            cameraImageHeight != image.height) {
            if (!CheckVk(
                    vkDeviceWaitIdle(context.device),
                    "vkDeviceWaitIdle(camera resize)") ||
                !CreateCameraImage(image.width, image.height)) {
                return false;
            }
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VkBufferCreateInfo bufferCreateInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferCreateInfo.size = byteCount;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!CheckVk(
                vkCreateBuffer(
                    context.device,
                    &bufferCreateInfo,
                    nullptr,
                    &stagingBuffer),
                "vkCreateBuffer(camera staging)")) {
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(
            context.device, stagingBuffer, &requirements);
        uint32_t memoryTypeIndex = 0;
        bool succeeded = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memoryTypeIndex);
        VkMemoryAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;
        if (succeeded) {
            succeeded = CheckVk(
                vkAllocateMemory(
                    context.device,
                    &allocateInfo,
                    nullptr,
                    &stagingMemory),
                "vkAllocateMemory(camera staging)");
        }
        if (succeeded) {
            succeeded = CheckVk(
                vkBindBufferMemory(
                    context.device, stagingBuffer, stagingMemory, 0),
                "vkBindBufferMemory(camera staging)");
        }
        void* mapped = nullptr;
        if (succeeded) {
            succeeded = CheckVk(
                vkMapMemory(
                    context.device,
                    stagingMemory,
                    0,
                    byteCount,
                    0,
                    &mapped),
                "vkMapMemory(camera staging)");
        }
        if (succeeded) {
            std::memcpy(mapped, image.pixels->data(), image.pixels->size());
            vkUnmapMemory(context.device, stagingMemory);
        }

        VkCommandBuffer uploadCommand = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo commandAllocateInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocateInfo.commandPool = commandPool;
        commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocateInfo.commandBufferCount = 1;
        if (succeeded) {
            succeeded = CheckVk(
                vkAllocateCommandBuffers(
                    context.device,
                    &commandAllocateInfo,
                    &uploadCommand),
                "vkAllocateCommandBuffers(camera upload)");
        }
        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (succeeded) {
            succeeded = CheckVk(
                vkBeginCommandBuffer(uploadCommand, &beginInfo),
                "vkBeginCommandBuffer(camera upload)");
        }
        if (succeeded) {
            VkImageMemoryBarrier toTransfer{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toTransfer.srcAccessMask =
                uploadedFrameId == 0 ? 0 : VK_ACCESS_SHADER_READ_BIT;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.oldLayout =
                uploadedFrameId == 0
                    ? VK_IMAGE_LAYOUT_UNDEFINED
                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = cameraImage;
            toTransfer.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            toTransfer.subresourceRange.levelCount = 1;
            toTransfer.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(
                uploadCommand,
                uploadedFrameId == 0
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &toTransfer);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {image.width, image.height, 1};
            vkCmdCopyBufferToImage(
                uploadCommand,
                stagingBuffer,
                cameraImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy);
            VkImageMemoryBarrier toShader{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShader.newLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.image = cameraImage;
            toShader.subresourceRange = toTransfer.subresourceRange;
            vkCmdPipelineBarrier(
                uploadCommand,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &toShader);
            succeeded = CheckVk(
                vkEndCommandBuffer(uploadCommand),
                "vkEndCommandBuffer(camera upload)");
        }
        if (succeeded) {
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &uploadCommand;
            succeeded = CheckVk(
                vkQueueSubmit(
                    context.queue, 1, &submitInfo, VK_NULL_HANDLE),
                "vkQueueSubmit(camera upload)") &&
                CheckVk(
                    vkQueueWaitIdle(context.queue),
                    "vkQueueWaitIdle(camera upload)");
        }
        if (uploadCommand != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(
                context.device, commandPool, 1, &uploadCommand);
        }
        if (stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(context.device, stagingBuffer, nullptr);
        }
        if (stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context.device, stagingMemory, nullptr);
        }
        if (!succeeded) {
            return false;
        }

        VkDescriptorImageInfo descriptorImageInfo{};
        descriptorImageInfo.sampler = cameraSampler;
        descriptorImageInfo.imageView = cameraImageView;
        descriptorImageInfo.imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = imageDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descriptorImageInfo;
        vkUpdateDescriptorSets(
            context.device, 1, &write, 0, nullptr);
        uploadedFrameId = image.frameId;
        return true;
    }

    bool Initialize(
        XrInstance instance,
        const XrSessionContext& session,
        const VulkanDeviceContext& deviceContext,
        VulkanSceneProvider* provider,
        const VulkanRendererOptions& options) {
        xrInstance = instance;
        xrSession = session.Session();
        context = deviceContext;
        sceneProvider = provider;
        transparentBackground = options.transparentBackground;
        const std::vector<XrViewConfigurationView>& configurations =
            session.ViewConfigurationViews();
        if (xrInstance == XR_NULL_HANDLE ||
            xrSession == XR_NULL_HANDLE ||
            context.device == VK_NULL_HANDLE ||
            context.queue == VK_NULL_HANDLE ||
            configurations.size() != eyes.size()) {
            LogError("Vulkan stereo renderer received an invalid context");
            return false;
        }
        if (!CreateSwapchains(configurations) ||
            !CreateImageViews() ||
            !CreateRenderPass() ||
            !CreatePipeline() ||
            !CreateFramebuffers() ||
            !CreateCommandResources()) {
            return false;
        }
        initialized = true;
        LogInfo(
            "Vulkan stereo renderer initialized (%s background)",
            transparentBackground ? "transparent" : "opaque");
        return true;
    }

    bool RecordAndSubmit(
        uint32_t eyeIndex,
        uint32_t imageIndex,
        const math::Mat4& viewProjection) {
        EyeSwapchain& eye = eyes[eyeIndex];
        ImageResources& resource = eye.resources[imageIndex];
        if (!CheckVk(
                vkWaitForFences(
                    context.device,
                    1,
                    &resource.fence,
                    VK_TRUE,
                    std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences") ||
            !CheckVk(
                vkResetFences(context.device, 1, &resource.fence),
                "vkResetFences") ||
            !CheckVk(
                vkResetCommandBuffer(resource.commandBuffer, 0),
                "vkResetCommandBuffer")) {
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!CheckVk(
                vkBeginCommandBuffer(resource.commandBuffer, &beginInfo),
                "vkBeginCommandBuffer")) {
            return false;
        }

        VkClearValue clearValue{};
        clearValue.color = transparentBackground
            ? VkClearColorValue{{0.0F, 0.0F, 0.0F, 0.0F}}
            : VkClearColorValue{{0.015F, 0.025F, 0.055F, 1.0F}};
        VkRenderPassBeginInfo renderPassBegin{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassBegin.renderPass = renderPass;
        renderPassBegin.framebuffer = resource.framebuffer;
        renderPassBegin.renderArea.extent = {eye.width, eye.height};
        renderPassBegin.clearValueCount = 1;
        renderPassBegin.pClearValues = &clearValue;
        vkCmdBeginRenderPass(
            resource.commandBuffer,
            &renderPassBegin,
            VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{};
        viewport.x = 0.0F;
        viewport.y = static_cast<float>(eye.height);
        viewport.width = static_cast<float>(eye.width);
        viewport.height = -static_cast<float>(eye.height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        vkCmdSetViewport(resource.commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {eye.width, eye.height};
        vkCmdSetScissor(resource.commandBuffer, 0, 1, &scissor);
        if (sceneProvider == nullptr) {
            const math::Mat4 model = math::TranslationMatrix(
                {0.0F, 0.0F, -2.0F});
            const math::Mat4 modelViewProjection =
                math::Multiply(viewProjection, model);
            vkCmdBindPipeline(
                resource.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                trianglePipeline);
            vkCmdPushConstants(
                resource.commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(math::Mat4),
                modelViewProjection.values.data());
            vkCmdDraw(resource.commandBuffer, 3, 1, 0, 0);
        } else {
            if (frameHasImage && cameraImage != VK_NULL_HANDLE) {
                const math::Mat4 modelViewProjection =
                    math::Multiply(viewProjection, frameImage.model);
                vkCmdBindPipeline(
                    resource.commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    imageQuadPipeline);
                vkCmdBindDescriptorSets(
                    resource.commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout,
                    0,
                    1,
                    &imageDescriptorSet,
                    0,
                    nullptr);
                vkCmdPushConstants(
                    resource.commandBuffer,
                    pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0,
                    sizeof(math::Mat4),
                    modelViewProjection.values.data());
                vkCmdDraw(resource.commandBuffer, 6, 1, 0, 0);
            }
            vkCmdBindPipeline(
                resource.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                debugLinePipeline);
            for (const DebugLineDraw& draw : frameDraws) {
                DebugPushConstants pushConstants;
                pushConstants.modelViewProjection =
                    math::Multiply(viewProjection, draw.model);
                pushConstants.color = draw.color;
                pushConstants.shape = static_cast<int32_t>(draw.shape);
                vkCmdPushConstants(
                    resource.commandBuffer,
                    pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0,
                    sizeof(pushConstants),
                    &pushConstants);
                const uint32_t vertexCount =
                    DebugVertexCount(draw.shape);
                vkCmdDraw(
                    resource.commandBuffer,
                    vertexCount,
                    1,
                    0,
                    0);
            }
        }
        vkCmdEndRenderPass(resource.commandBuffer);
        if (!CheckVk(
                vkEndCommandBuffer(resource.commandBuffer),
                "vkEndCommandBuffer")) {
            return false;
        }

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &resource.commandBuffer;
        if (!CheckVk(
                vkQueueSubmit(
                    context.queue, 1, &submitInfo, resource.fence),
                "vkQueueSubmit")) {
            return false;
        }
        if (!CheckVk(
                vkWaitForFences(
                    context.device,
                    1,
                    &resource.fence,
                    VK_TRUE,
                    std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences(render)")) {
            CheckVk(vkQueueWaitIdle(context.queue), "vkQueueWaitIdle(recovery)");
            return false;
        }
        return true;
    }

    bool RenderEye(
        uint32_t eyeIndex,
        const XrView& view,
        XrCompositionLayerProjectionView* projectionView) {
        EyeSwapchain& eye = eyes[eyeIndex];
        XrSwapchainImageAcquireInfo acquireInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        uint32_t imageIndex = 0;
        if (!CheckXr(
                xrInstance,
                xrAcquireSwapchainImage(
                    eye.handle, &acquireInfo, &imageIndex),
                "xrAcquireSwapchainImage")) {
            return false;
        }

        XrSwapchainImageWaitInfo waitInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        if (!CheckXr(
                xrInstance,
                xrWaitSwapchainImage(eye.handle, &waitInfo),
                "xrWaitSwapchainImage")) {
            return false;
        }

        bool rendered = false;
        if (imageIndex >= eye.resources.size()) {
            LogError(
                "OpenXR returned swapchain image index %u outside %zu images",
                imageIndex,
                eye.resources.size());
        } else {
            constexpr float kNearDistance = 0.05F;
            constexpr float kFarDistance = 100.0F;
            const math::Mat4 viewMatrix = math::PoseMatrix(
                math::InverseRigid(math::FromXr(view.pose)));
            const math::Mat4 projection = math::VulkanProjectionFromFov(
                view.fov, kNearDistance, kFarDistance);
            const math::Mat4 viewProjection =
                math::Multiply(projection, viewMatrix);
            rendered = RecordAndSubmit(
                eyeIndex, imageIndex, viewProjection);
        }

        XrSwapchainImageReleaseInfo releaseInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const bool released = CheckXr(
            xrInstance,
            xrReleaseSwapchainImage(eye.handle, &releaseInfo),
            "xrReleaseSwapchainImage");
        if (!rendered || !released) {
            return false;
        }

        *projectionView =
            XrCompositionLayerProjectionView{
                XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projectionView->pose = view.pose;
        projectionView->fov = view.fov;
        projectionView->subImage.swapchain = eye.handle;
        projectionView->subImage.imageRect.extent = {
            static_cast<int32_t>(eye.width),
            static_cast<int32_t>(eye.height),
        };
        projectionView->subImage.imageArrayIndex = 0;
        return true;
    }

    bool Render(
        const XrFrameRenderInfo& frame,
        const XrCompositionLayerBaseHeader** layer) {
        if (!initialized || layer == nullptr || frame.viewCount != eyes.size()) {
            LogError("Invalid Vulkan stereo frame input");
            return false;
        }
        *layer = nullptr;
        frameDraws.clear();
        frameHasImage = false;
        if (sceneProvider != nullptr &&
            !sceneProvider->BuildScene(frame, &frameDraws)) {
            LogError("Vulkan scene provider failed");
            return false;
        }
        if (sceneProvider != nullptr &&
            sceneProvider->GetRgbaImageQuad(&frameImage)) {
            if (frameImage.frameId != uploadedFrameId &&
                !UploadCameraImage(frameImage)) {
                LogError("Camera image upload failed");
                return false;
            }
            frameHasImage = cameraImage != VK_NULL_HANDLE;
        }
        for (uint32_t eyeIndex = 0; eyeIndex < eyes.size(); ++eyeIndex) {
            if (!RenderEye(
                    eyeIndex,
                    frame.views[eyeIndex],
                    &projectionViews[eyeIndex])) {
                return false;
            }
        }

        projectionLayer =
            XrCompositionLayerProjection{
                XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        if (transparentBackground) {
            projectionLayer.layerFlags =
                XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        }
        projectionLayer.space = frame.space;
        projectionLayer.viewCount =
            static_cast<uint32_t>(projectionViews.size());
        projectionLayer.views = projectionViews.data();
        *layer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &projectionLayer);
        return true;
    }

    void Shutdown() {
        if (context.device != VK_NULL_HANDLE) {
            CheckVk(vkDeviceWaitIdle(context.device), "vkDeviceWaitIdle");
        }
        for (EyeSwapchain& eye : eyes) {
            if (context.device != VK_NULL_HANDLE) {
                for (ImageResources& resource : eye.resources) {
                    if (resource.fence != VK_NULL_HANDLE) {
                        vkDestroyFence(context.device, resource.fence, nullptr);
                        resource.fence = VK_NULL_HANDLE;
                    }
                    if (resource.framebuffer != VK_NULL_HANDLE) {
                        vkDestroyFramebuffer(
                            context.device,
                            resource.framebuffer,
                            nullptr);
                        resource.framebuffer = VK_NULL_HANDLE;
                    }
                    if (resource.imageView != VK_NULL_HANDLE) {
                        vkDestroyImageView(
                            context.device, resource.imageView, nullptr);
                        resource.imageView = VK_NULL_HANDLE;
                    }
                }
            }
            eye.resources.clear();
            eye.images.clear();
            if (eye.handle != XR_NULL_HANDLE) {
                CheckXr(
                    xrInstance,
                    xrDestroySwapchain(eye.handle),
                    "xrDestroySwapchain");
                eye.handle = XR_NULL_HANDLE;
            }
        }
        if (context.device != VK_NULL_HANDLE) {
            DestroyCameraImage();
            if (cameraSampler != VK_NULL_HANDLE) {
                vkDestroySampler(
                    context.device, cameraSampler, nullptr);
                cameraSampler = VK_NULL_HANDLE;
            }
            if (imageDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(
                    context.device, imageDescriptorPool, nullptr);
                imageDescriptorPool = VK_NULL_HANDLE;
                imageDescriptorSet = VK_NULL_HANDLE;
            }
            if (imageQuadPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(
                    context.device, imageQuadPipeline, nullptr);
                imageQuadPipeline = VK_NULL_HANDLE;
            }
            if (debugLinePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(
                    context.device, debugLinePipeline, nullptr);
                debugLinePipeline = VK_NULL_HANDLE;
            }
            if (trianglePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(
                    context.device, trianglePipeline, nullptr);
                trianglePipeline = VK_NULL_HANDLE;
            }
            if (pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(
                    context.device, pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
            }
            if (imageDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(
                    context.device,
                    imageDescriptorSetLayout,
                    nullptr);
                imageDescriptorSetLayout = VK_NULL_HANDLE;
            }
            if (renderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(context.device, renderPass, nullptr);
                renderPass = VK_NULL_HANDLE;
            }
            if (commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(
                    context.device, commandPool, nullptr);
                commandPool = VK_NULL_HANDLE;
            }
        }
        initialized = false;
        colorFormat = VK_FORMAT_UNDEFINED;
        frameDraws.clear();
        frameImage = {};
        frameHasImage = false;
        sceneProvider = nullptr;
        transparentBackground = false;
        context = {};
        xrSession = XR_NULL_HANDLE;
        xrInstance = XR_NULL_HANDLE;
    }
};

VulkanStereoRenderer::VulkanStereoRenderer()
    : impl_(std::make_unique<Impl>()) {}

VulkanStereoRenderer::~VulkanStereoRenderer() {
    Shutdown();
}

bool VulkanStereoRenderer::Initialize(
    XrInstance xrInstance,
    const XrSessionContext& xrSession,
    const VulkanDeviceContext& deviceContext,
    VulkanSceneProvider* sceneProvider,
    const VulkanRendererOptions& options) {
    if (impl_->initialized) {
        return true;
    }
    if (!impl_->Initialize(
            xrInstance,
            xrSession,
            deviceContext,
            sceneProvider,
            options)) {
        impl_->Shutdown();
        return false;
    }
    return true;
}

bool VulkanStereoRenderer::RenderFrame(
    const XrFrameRenderInfo& frame,
    const XrCompositionLayerBaseHeader** layer) {
    return impl_->Render(frame, layer);
}

void VulkanStereoRenderer::Shutdown() {
    if (impl_ != nullptr) {
        const bool wasInitialized = impl_->initialized;
        impl_->Shutdown();
        if (wasInitialized) {
            LogInfo("Vulkan stereo renderer destroyed");
        }
    }
}

}  // namespace questlab
