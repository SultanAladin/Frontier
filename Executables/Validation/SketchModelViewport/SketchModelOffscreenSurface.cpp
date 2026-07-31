/*==============================================================================================================================================
                                                       SKETCHMODELOFFSCREENSURFACE.CPP
==============================================================================================================================================*/
// 🧩 Device-side body for the offscreen colour target the GPU grid draws into and ImGui then samples. Builds one VkImage (COLOR_ATTACHMENT |
//    SAMPLED) with a single-mip view + linear-clamp sampler, registers the pair once with the ImGui Vulkan backend, and hands back the resulting
//    descriptor set as the panel's ImTextureID. Memory selection mirrors ParametricSketchMatcapTexture / BufferAllocation so the whole engine
//    picks device memory identically — raw Vulkan, no VMA (VulkanHost carries no allocator helper). Recording wraps the caller's pass draw in the
//    two layout transitions (UNDEFINED-or-SHADER_READ_ONLY → COLOR_ATTACHMENT while drawing, → SHADER_READ_ONLY while ImGui samples) and one
//    dynamic-rendering scope opened through the host's loaded CmdBeginRendering / CmdEndRendering entry points. Rebuild-in-place on resize keeps
//    the grid pixel-exact.

#include "SketchModelOffscreenSurface.h"

#include "backends/imgui_impl_vulkan.h"

#include <cstdio>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ReportSurface(const char* MessageText)
    {
        std::fprintf(stderr, "[SketchModelOffscreenSurface] %s\n", MessageText);
    }

    // 📝 First memory type allowed by the requirement bitmask that carries every required property bit — same selection the rest of the
    //    engine uses so device memory is picked identically everywhere.
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
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSketchModelOffscreenSurface(SketchModelOffscreenSurface& Surface,
                                           const Frontier::VulkanHost&  Host,
                                           uint32_t                     Width,
                                           uint32_t                     Height,
                                           VkFormat                     Format)
{
    // 📝 A rebuild reuses the same struct: tear down any prior image/view/sampler/descriptor first so resize is in place.
    FinalizeSketchModelOffscreenSurface(Surface, Host);

    if (Width == 0 || Height == 0)
    {
        ReportSurface("zero extent requested — surface not built");
        return false;
    }

    Surface.Format = Format;
    Surface.Width  = Width;
    Surface.Height = Height;

    // ─── device-local colour target (COLOR_ATTACHMENT | SAMPLED) ───
    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = Format;
    ImageInformation.extent        = { Width, Height, 1 };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Host.Device, &ImageInformation, Host.Allocator, &Surface.Image) != VK_SUCCESS)
    {
        Surface.Image = VK_NULL_HANDLE;
        ReportSurface("image creation failed");
        FinalizeSketchModelOffscreenSurface(Surface, Host);
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetImageMemoryRequirements(Host.Device, Surface.Image, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        ReportSurface("no device-local memory type — surface not built");
        FinalizeSketchModelOffscreenSurface(Surface, Host);
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &Surface.Memory) != VK_SUCCESS ||
        vkBindImageMemory(Host.Device, Surface.Image, Surface.Memory, 0) != VK_SUCCESS)
    {
        ReportSurface("memory allocation / bind failed");
        FinalizeSketchModelOffscreenSurface(Surface, Host);
        return false;
    }

    // ─── single-mip colour view (the attachment + the sampler both bind this) ───
    VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInformation.image                       = Surface.Image;
    ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    ViewInformation.format                      = Format;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInformation.subresourceRange.levelCount = 1;
    ViewInformation.subresourceRange.layerCount = 1;
    if (vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &Surface.View) != VK_SUCCESS)
    {
        Surface.View = VK_NULL_HANDLE;
        ReportSurface("image view creation failed");
        FinalizeSketchModelOffscreenSurface(Surface, Host);
        return false;
    }

    // 📝 Linear filter + clamp so a panel drawn at a non-integer scale stays smooth and never wraps at the border.
    VkSamplerCreateInfo SamplerInformation = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    SamplerInformation.magFilter    = VK_FILTER_LINEAR;
    SamplerInformation.minFilter    = VK_FILTER_LINEAR;
    SamplerInformation.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerInformation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.minLod       = 0.0f;
    SamplerInformation.maxLod       = 1.0f;
    if (vkCreateSampler(Host.Device, &SamplerInformation, Host.Allocator, &Surface.Sampler) != VK_SUCCESS)
    {
        Surface.Sampler = VK_NULL_HANDLE;
        ReportSurface("sampler creation failed");
        FinalizeSketchModelOffscreenSurface(Surface, Host);
        return false;
    }

    // 📝 Register the (sampler, view) pair once with the ImGui Vulkan backend. The returned descriptor set IS the ImTextureID the panel
    //    draws — it is declared valid at SHADER_READ_ONLY, which is exactly the layout every record leaves the image in.
    Surface.DescriptorSet = ImGui_ImplVulkan_AddTexture(Surface.Sampler, Surface.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (Surface.DescriptorSet == VK_NULL_HANDLE)
    {
        ReportSurface("ImGui texture registration failed");
        FinalizeSketchModelOffscreenSurface(Surface, Host);
        return false;
    }

    Surface.ShaderReadable = false;   // first record performs the initial UNDEFINED → COLOR_ATTACHMENT transition
    return true;
}


bool QuerySketchModelOffscreenRebuildRequired(const SketchModelOffscreenSurface& Surface,
                                              uint32_t                           Width,
                                              uint32_t                           Height)
{
    return Surface.Image == VK_NULL_HANDLE || Surface.Width != Width || Surface.Height != Height;
}


ImTextureID ResolveSketchModelOffscreenTexture(const SketchModelOffscreenSurface& Surface)
{
    if (Surface.DescriptorSet == VK_NULL_HANDLE)
    {
        return (ImTextureID)0;
    }
    return (ImTextureID)Surface.DescriptorSet;
}


void RecordSketchModelOffscreenSurface(SketchModelOffscreenSurface& Surface,
                                       const Frontier::VulkanHost&  Host,
                                       VkCommandBuffer              CommandBuffer,
                                       const float                  ClearColour[4],
                                       void                       (*Record)(VkCommandBuffer, VkExtent2D, void*),
                                       void*                        RecordContext)
{
    if (Surface.Image == VK_NULL_HANDLE || Host.CmdBeginRendering == nullptr || Host.CmdEndRendering == nullptr)
    {
        return;   // nothing built, or the host lacks dynamic rendering — the panel simply shows no texture
    }

    const VkExtent2D Extent = { Surface.Width, Surface.Height };

    // ─── (UNDEFINED first frame, else SHADER_READ_ONLY) → COLOR_ATTACHMENT_OPTIMAL ───
    VkImageMemoryBarrier ToColour = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    ToColour.oldLayout                   = Surface.ShaderReadable ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    ToColour.newLayout                   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ToColour.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    ToColour.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    ToColour.image                       = Surface.Image;
    ToColour.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ToColour.subresourceRange.levelCount = 1;
    ToColour.subresourceRange.layerCount = 1;
    ToColour.srcAccessMask               = Surface.ShaderReadable ? VK_ACCESS_SHADER_READ_BIT : 0;
    ToColour.dstAccessMask               = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToColour);

    // ─── open the dynamic-rendering scope, cleared to ClearColour ───
    VkRenderingAttachmentInfoKHR ColourAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
    ColourAttachment.imageView                  = Surface.View;
    ColourAttachment.imageLayout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColourAttachment.loadOp                     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColourAttachment.storeOp                    = VK_ATTACHMENT_STORE_OP_STORE;
    ColourAttachment.clearValue.color.float32[0] = ClearColour != nullptr ? ClearColour[0] : 0.0f;
    ColourAttachment.clearValue.color.float32[1] = ClearColour != nullptr ? ClearColour[1] : 0.0f;
    ColourAttachment.clearValue.color.float32[2] = ClearColour != nullptr ? ClearColour[2] : 0.0f;
    ColourAttachment.clearValue.color.float32[3] = ClearColour != nullptr ? ClearColour[3] : 1.0f;

    VkRenderingInfoKHR RenderingInformation = { VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
    RenderingInformation.renderArea           = { { 0, 0 }, Extent };
    RenderingInformation.layerCount           = 1;
    RenderingInformation.colorAttachmentCount = 1;
    RenderingInformation.pColorAttachments    = &ColourAttachment;
    Host.CmdBeginRendering(CommandBuffer, &RenderingInformation);

    if (Record != nullptr)
    {
        Record(CommandBuffer, Extent, RecordContext);
    }

    Host.CmdEndRendering(CommandBuffer);

    // ─── COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL (ImGui samples it this frame) ───
    VkImageMemoryBarrier ToShader = ToColour;
    ToShader.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ToShader.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToShader.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToShader);

    Surface.ShaderReadable = true;
}


void FinalizeSketchModelOffscreenSurface(SketchModelOffscreenSurface& Surface, const Frontier::VulkanHost& Host)
{
    if (Surface.DescriptorSet != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(Surface.DescriptorSet);
        Surface.DescriptorSet = VK_NULL_HANDLE;
    }
    if (Surface.Sampler != VK_NULL_HANDLE) { vkDestroySampler(Host.Device, Surface.Sampler, Host.Allocator);  Surface.Sampler = VK_NULL_HANDLE; }
    if (Surface.View    != VK_NULL_HANDLE) { vkDestroyImageView(Host.Device, Surface.View, Host.Allocator);   Surface.View    = VK_NULL_HANDLE; }
    if (Surface.Image   != VK_NULL_HANDLE) { vkDestroyImage(Host.Device, Surface.Image, Host.Allocator);      Surface.Image   = VK_NULL_HANDLE; }
    if (Surface.Memory  != VK_NULL_HANDLE) { vkFreeMemory(Host.Device, Surface.Memory, Host.Allocator);       Surface.Memory  = VK_NULL_HANDLE; }

    Surface.Width          = 0;
    Surface.Height         = 0;
    Surface.ShaderReadable = false;
}

}   // namespace SketchModelViewportValidation
