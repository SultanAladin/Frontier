/*==============================================================================================================================================
                                                       PARAMETRICSKETCHVIEWTARGET.CPP
==============================================================================================================================================*/
// 🧩 Lean offscreen colour+depth target for the parametric-sketch solid-preview inscription. Builds a single-subpass render pass (colour
//    B8G8R8A8_UNORM finishing in SHADER_READ_ONLY + transient depth D32_SFLOAT), the device-local images backing it, a framebuffer, and an ImGui
//    backend descriptor so ImGui::Image can sample the colour result the same frame it is drawn. Raw Vulkan, no VMA. The render pass + sampler are
//    size-independent (built once); the images / framebuffer / descriptor rebuild on resize. Wired onto the shared VulkanHost.

#include "Graphics/Render/Surface/ParametricSketchViewTarget.h"

#include "imgui_impl_vulkan.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr VkFormat ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;   // [-] - matches the swapchain colour format
    constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;       // [-] - transient depth, cleared per frame

    void ReportViewTarget(const char* MessageText)
    {
        std::fprintf(stderr, "[ParametricSketchViewTarget] %s\n", MessageText);
    }

    // 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors BufferAllocation.
    uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
                                   uint32_t              CompatibleTypesBitmask,
                                   VkMemoryPropertyFlags RequiredProperties,
                                   bool&                 FoundEnabled)
    {
        VkPhysicalDeviceMemoryProperties MemoryProperties = {};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
        for (uint32_t IndexIterator = 0; IndexIterator < MemoryProperties.memoryTypeCount; ++IndexIterator)
        {
            const bool TypeCompatible = (CompatibleTypesBitmask & (1u << IndexIterator)) != 0;
            const bool PropertyMatch  = (MemoryProperties.memoryTypes[IndexIterator].propertyFlags & RequiredProperties) == RequiredProperties;
            if (TypeCompatible && PropertyMatch) { FoundEnabled = true; return IndexIterator; }
        }
        FoundEnabled = false;
        return 0;
    }

    // 📝 Create a device-local image + view for one attachment. Usage + aspect distinguish colour (sampled attachment) from depth (transient
    //    attachment). On any failure every claimed handle is released and false is returned with the out-handles null.
    bool AllocateAttachment(VulkanHost&        Host,
                            uint32_t           Width,
                            uint32_t           Height,
                            VkFormat           Format,
                            VkImageUsageFlags  Usage,
                            VkImageAspectFlags Aspect,
                            VkImage&           OutImage,
                            VkDeviceMemory&    OutMemory,
                            VkImageView&       OutView)
    {
        OutImage  = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        OutView   = VK_NULL_HANDLE;

        VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
        ImageInformation.format        = Format;
        ImageInformation.extent        = { Width, Height, 1 };
        ImageInformation.mipLevels     = 1;
        ImageInformation.arrayLayers   = 1;
        ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
        ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ImageInformation.usage         = Usage;
        ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(Host.Device, &ImageInformation, nullptr, &OutImage) != VK_SUCCESS)
        {
            OutImage = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryRequirements MemoryRequirements = {};
        vkGetImageMemoryRequirements(Host.Device, OutImage, &MemoryRequirements);

        bool MemoryTypeFound = false;
        const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                               MemoryRequirements.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                               MemoryTypeFound);
        if (!MemoryTypeFound)
        {
            vkDestroyImage(Host.Device, OutImage, nullptr);
            OutImage = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocateInformation.allocationSize  = MemoryRequirements.size;
        AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
        if (vkAllocateMemory(Host.Device, &AllocateInformation, nullptr, &OutMemory) != VK_SUCCESS ||
            vkBindImageMemory(Host.Device, OutImage, OutMemory, 0) != VK_SUCCESS)
        {
            if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, nullptr);
            vkDestroyImage(Host.Device, OutImage, nullptr);
            OutImage  = VK_NULL_HANDLE;
            OutMemory = VK_NULL_HANDLE;
            return false;
        }

        VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ViewInformation.image                       = OutImage;
        ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        ViewInformation.format                      = Format;
        ViewInformation.subresourceRange.aspectMask = Aspect;
        ViewInformation.subresourceRange.levelCount = 1;
        ViewInformation.subresourceRange.layerCount = 1;
        if (vkCreateImageView(Host.Device, &ViewInformation, nullptr, &OutView) != VK_SUCCESS)
        {
            vkFreeMemory(Host.Device, OutMemory, nullptr);
            vkDestroyImage(Host.Device, OutImage, nullptr);
            OutImage  = VK_NULL_HANDLE;
            OutMemory = VK_NULL_HANDLE;
            OutView   = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 The single-subpass render pass: colour attachment 0 (clear -> store, finalLayout SHADER_READ_ONLY so ImGui samples it) + depth
    //    attachment 1 (clear, don't-store, DEPTH_STENCIL_ATTACHMENT_OPTIMAL). One dependency each side orders the pass against the external
    //    sampling / earlier writes. Built once — extent-independent.
    bool BuildRenderPass(ParametricSketchViewTarget& Target)
    {
        VkAttachmentDescription Attachments[2] = {};
        Attachments[0].format         = ColorFormat;
        Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        Attachments[1].format         = DepthFormat;
        Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
        Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ColorReference = {};
        ColorReference.attachment = 0;
        ColorReference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference DepthReference = {};
        DepthReference.attachment = 1;
        DepthReference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription Subpass = {};
        Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subpass.colorAttachmentCount    = 1;
        Subpass.pColorAttachments       = &ColorReference;
        Subpass.pDepthStencilAttachment = &DepthReference;

        // Order the offscreen writes after any earlier sampling of the previous frame's colour, and before this frame's ImGui sample of it.
        VkSubpassDependency Dependencies[2] = {};
        Dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        Dependencies[0].dstSubpass      = 0;
        Dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        Dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Dependencies[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        Dependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        Dependencies[1].srcSubpass      = 0;
        Dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        Dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        Dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Dependencies[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        Dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo PassInformation = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        PassInformation.attachmentCount = 2;
        PassInformation.pAttachments    = Attachments;
        PassInformation.subpassCount    = 1;
        PassInformation.pSubpasses      = &Subpass;
        PassInformation.dependencyCount = 2;
        PassInformation.pDependencies   = Dependencies;
        if (vkCreateRenderPass(Target.Host->Device, &PassInformation, nullptr, &Target.RenderPass) != VK_SUCCESS)
        {
            Target.RenderPass = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Tear down only the size-dependent resources (images / views / framebuffer / ImGui descriptor), leaving the render pass + sampler intact
    //    so a resize rebuilds just these. Safe on partially-built values. The device must be idle.
    void ReleaseSizedResources(ParametricSketchViewTarget& Target)
    {
        VkDevice Device = Target.Host->Device;
        if (Target.Descriptor  != VK_NULL_HANDLE) { ImGui_ImplVulkan_RemoveTexture(Target.Descriptor); Target.Descriptor = VK_NULL_HANDLE; }
        if (Target.Framebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(Device, Target.Framebuffer, nullptr); Target.Framebuffer = VK_NULL_HANDLE; }
        if (Target.ColorView   != VK_NULL_HANDLE) { vkDestroyImageView(Device, Target.ColorView, nullptr); Target.ColorView = VK_NULL_HANDLE; }
        if (Target.ColorImage  != VK_NULL_HANDLE) { vkDestroyImage(Device, Target.ColorImage, nullptr); Target.ColorImage = VK_NULL_HANDLE; }
        if (Target.ColorMemory != VK_NULL_HANDLE) { vkFreeMemory(Device, Target.ColorMemory, nullptr); Target.ColorMemory = VK_NULL_HANDLE; }
        if (Target.DepthView   != VK_NULL_HANDLE) { vkDestroyImageView(Device, Target.DepthView, nullptr); Target.DepthView = VK_NULL_HANDLE; }
        if (Target.DepthImage  != VK_NULL_HANDLE) { vkDestroyImage(Device, Target.DepthImage, nullptr); Target.DepthImage = VK_NULL_HANDLE; }
        if (Target.DepthMemory != VK_NULL_HANDLE) { vkFreeMemory(Device, Target.DepthMemory, nullptr); Target.DepthMemory = VK_NULL_HANDLE; }
    }

    // 📝 Build the colour + depth images, a framebuffer against the (already-built) render pass, and register the colour view with the ImGui
    //    backend so ImGui::Image can sample it. On any failure every sized resource is released and false is returned.
    bool BuildSizedResources(ParametricSketchViewTarget& Target, uint32_t Width, uint32_t Height)
    {
        VulkanHost& Host = *Target.Host;

        if (!AllocateAttachment(Host, Width, Height, ColorFormat,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT,
                                Target.ColorImage, Target.ColorMemory, Target.ColorView))
        {
            ReportViewTarget("colour attachment allocation failed");
            return false;
        }
        if (!AllocateAttachment(Host, Width, Height, DepthFormat,
                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT,
                                Target.DepthImage, Target.DepthMemory, Target.DepthView))
        {
            ReportViewTarget("depth attachment allocation failed");
            ReleaseSizedResources(Target);
            return false;
        }

        VkImageView FramebufferViews[2] = { Target.ColorView, Target.DepthView };
        VkFramebufferCreateInfo FramebufferInformation = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        FramebufferInformation.renderPass      = Target.RenderPass;
        FramebufferInformation.attachmentCount = 2;
        FramebufferInformation.pAttachments    = FramebufferViews;
        FramebufferInformation.width           = Width;
        FramebufferInformation.height          = Height;
        FramebufferInformation.layers          = 1;
        if (vkCreateFramebuffer(Host.Device, &FramebufferInformation, nullptr, &Target.Framebuffer) != VK_SUCCESS)
        {
            ReportViewTarget("framebuffer creation failed");
            Target.Framebuffer = VK_NULL_HANDLE;
            ReleaseSizedResources(Target);
            return false;
        }

        Target.Descriptor = ImGui_ImplVulkan_AddTexture(Target.Sampler, Target.ColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (Target.Descriptor == VK_NULL_HANDLE)
        {
            ReportViewTarget("ImGui texture registration failed");
            ReleaseSizedResources(Target);
            return false;
        }

        Target.Width  = Width;
        Target.Height = Height;
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeParametricSketchViewTarget(ParametricSketchViewTarget& Target, VulkanHost& Host, uint32_t Width, uint32_t Height)
{
    Target = ParametricSketchViewTarget{};
    Target.Host        = &Host;
    Target.ReadyStatus = false;

    if (Width == 0 || Height == 0)
    {
        ReportViewTarget("zero extent at bring-up — preview disabled");
        return false;
    }

    if (!BuildRenderPass(Target))
    {
        ReportViewTarget("render pass creation failed — preview disabled");
        FinalizeParametricSketchViewTarget(Target);
        return false;
    }

    // 📝 Linear filter + clamp so ImGui scales the preview smoothly into whatever pane size the layout gives it.
    VkSamplerCreateInfo SamplerInformation = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    SamplerInformation.magFilter    = VK_FILTER_LINEAR;
    SamplerInformation.minFilter    = VK_FILTER_LINEAR;
    SamplerInformation.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerInformation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.minLod       = 0.0f;
    SamplerInformation.maxLod       = 1.0f;
    if (vkCreateSampler(Host.Device, &SamplerInformation, nullptr, &Target.Sampler) != VK_SUCCESS)
    {
        ReportViewTarget("sampler creation failed — preview disabled");
        FinalizeParametricSketchViewTarget(Target);
        return false;
    }

    if (!BuildSizedResources(Target, Width, Height))
    {
        FinalizeParametricSketchViewTarget(Target);
        return false;
    }

    Target.ReadyStatus = true;
    return true;
}

bool ReconfigureParametricSketchViewTarget(ParametricSketchViewTarget& Target, uint32_t Width, uint32_t Height)
{
    if (Target.Host == nullptr || Target.RenderPass == VK_NULL_HANDLE)
        return false;
    if (Width == 0 || Height == 0)
        return false;
    if (Target.ReadyStatus && Target.Width == Width && Target.Height == Height)
        return true;

    ReleaseSizedResources(Target);
    Target.ReadyStatus = false;
    if (!BuildSizedResources(Target, Width, Height))
        return false;

    Target.ReadyStatus = true;
    return true;
}

void FinalizeParametricSketchViewTarget(ParametricSketchViewTarget& Target)
{
    if (Target.Host != nullptr)
    {
        ReleaseSizedResources(Target);
        if (Target.Sampler    != VK_NULL_HANDLE) vkDestroySampler(Target.Host->Device, Target.Sampler, nullptr);
        if (Target.RenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(Target.Host->Device, Target.RenderPass, nullptr);
    }
    Target = ParametricSketchViewTarget{};
}

} // namespace Frontier
