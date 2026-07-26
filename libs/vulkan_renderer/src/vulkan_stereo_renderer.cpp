#include "vulkan_renderer/vulkan_stereo_renderer.h"

#include "xr_core/xr_error.h"

#include <openxr/openxr_platform.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace questlab {
namespace {

struct Matrix4 {
    std::array<float, 16> values{};
};

Matrix4 Identity() {
    Matrix4 matrix;
    matrix.values[0] = 1.0F;
    matrix.values[5] = 1.0F;
    matrix.values[10] = 1.0F;
    matrix.values[15] = 1.0F;
    return matrix;
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) {
    Matrix4 result;
    for (uint32_t column = 0; column < 4; ++column) {
        for (uint32_t row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (uint32_t element = 0; element < 4; ++element) {
                value +=
                    left.values[element * 4 + row] *
                    right.values[column * 4 + element];
            }
            result.values[column * 4 + row] = value;
        }
    }
    return result;
}

Matrix4 Translation(float x, float y, float z) {
    Matrix4 matrix = Identity();
    matrix.values[12] = x;
    matrix.values[13] = y;
    matrix.values[14] = z;
    return matrix;
}

Matrix4 Rotation(const XrQuaternionf& quaternion) {
    const float xx = quaternion.x * quaternion.x;
    const float yy = quaternion.y * quaternion.y;
    const float zz = quaternion.z * quaternion.z;
    const float xy = quaternion.x * quaternion.y;
    const float xz = quaternion.x * quaternion.z;
    const float yz = quaternion.y * quaternion.z;
    const float wx = quaternion.w * quaternion.x;
    const float wy = quaternion.w * quaternion.y;
    const float wz = quaternion.w * quaternion.z;

    Matrix4 matrix = Identity();
    matrix.values[0] = 1.0F - 2.0F * (yy + zz);
    matrix.values[1] = 2.0F * (xy + wz);
    matrix.values[2] = 2.0F * (xz - wy);
    matrix.values[4] = 2.0F * (xy - wz);
    matrix.values[5] = 1.0F - 2.0F * (xx + zz);
    matrix.values[6] = 2.0F * (yz + wx);
    matrix.values[8] = 2.0F * (xz + wy);
    matrix.values[9] = 2.0F * (yz - wx);
    matrix.values[10] = 1.0F - 2.0F * (xx + yy);
    return matrix;
}

Matrix4 ViewFromPose(const XrPosef& pose) {
    XrQuaternionf inverseOrientation{
        -pose.orientation.x,
        -pose.orientation.y,
        -pose.orientation.z,
        pose.orientation.w,
    };
    return Multiply(
        Rotation(inverseOrientation),
        Translation(-pose.position.x, -pose.position.y, -pose.position.z));
}

Matrix4 ProjectionFromFov(
    const XrFovf& fov,
    float nearDistance,
    float farDistance) {
    const float tanLeft = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float tanDown = std::tan(fov.angleDown);
    const float tanUp = std::tan(fov.angleUp);
    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;

    Matrix4 matrix;
    matrix.values[0] = 2.0F / tanWidth;
    matrix.values[5] = 2.0F / tanHeight;
    matrix.values[8] = (tanRight + tanLeft) / tanWidth;
    matrix.values[9] = (tanUp + tanDown) / tanHeight;
    matrix.values[10] = -farDistance / (farDistance - nearDistance);
    matrix.values[11] = -1.0F;
    matrix.values[14] =
        -(farDistance * nearDistance) / (farDistance - nearDistance);
    return matrix;
}

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

constexpr uint32_t kVertexShader[] =
#include "triangle.vert.inc"
;

constexpr uint32_t kFragmentShader[] =
#include "triangle.frag.inc"
;

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
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::array<EyeSwapchain, 2> eyes;
    std::array<XrCompositionLayerProjectionView, 2> projectionViews = {
        XrCompositionLayerProjectionView{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
        XrCompositionLayerProjectionView{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
    };
    XrCompositionLayerProjection projectionLayer{
        XR_TYPE_COMPOSITION_LAYER_PROJECTION};
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
        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;
        if (!CreateShaderModule(
                kVertexShader,
                sizeof(kVertexShader),
                &vertexModule) ||
            !CreateShaderModule(
                kFragmentShader,
                sizeof(kFragmentShader),
                &fragmentModule)) {
            if (vertexModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(context.device, vertexModule, nullptr);
            }
            if (fragmentModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(context.device, fragmentModule, nullptr);
            }
            return false;
        }

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {{
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                nullptr,
                0,
                VK_SHADER_STAGE_VERTEX_BIT,
                vertexModule,
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
        pushConstantRange.size = sizeof(Matrix4);
        VkPipelineLayoutCreateInfo layoutCreateInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutCreateInfo.pushConstantRangeCount = 1;
        layoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        bool succeeded = CheckVk(
            vkCreatePipelineLayout(
                context.device,
                &layoutCreateInfo,
                nullptr,
                &pipelineLayout),
            "vkCreatePipelineLayout");

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
                    &pipeline),
                "vkCreateGraphicsPipelines");
        }

        vkDestroyShaderModule(context.device, fragmentModule, nullptr);
        vkDestroyShaderModule(context.device, vertexModule, nullptr);
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

    bool Initialize(
        XrInstance instance,
        const XrSessionContext& session,
        const VulkanDeviceContext& deviceContext) {
        xrInstance = instance;
        xrSession = session.Session();
        context = deviceContext;
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
        LogInfo("Vulkan stereo renderer initialized");
        return true;
    }

    bool RecordAndSubmit(
        uint32_t eyeIndex,
        uint32_t imageIndex,
        const Matrix4& modelViewProjection) {
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
        clearValue.color = {{0.015F, 0.025F, 0.055F, 1.0F}};
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
        vkCmdBindPipeline(
            resource.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline);

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
        vkCmdPushConstants(
            resource.commandBuffer,
            pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(Matrix4),
            modelViewProjection.values.data());
        vkCmdDraw(resource.commandBuffer, 3, 1, 0, 0);
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
            const Matrix4 model = Translation(0.0F, 0.0F, -2.0F);
            const Matrix4 viewMatrix = ViewFromPose(view.pose);
            const Matrix4 projection = ProjectionFromFov(
                view.fov, kNearDistance, kFarDistance);
            const Matrix4 modelViewProjection =
                Multiply(projection, Multiply(viewMatrix, model));
            rendered = RecordAndSubmit(
                eyeIndex, imageIndex, modelViewProjection);
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
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(context.device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
            if (pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(
                    context.device, pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
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
    const VulkanDeviceContext& deviceContext) {
    if (impl_->initialized) {
        return true;
    }
    if (!impl_->Initialize(xrInstance, xrSession, deviceContext)) {
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
