/*==============================================================================================================================================
                                                      PARAMETRICSKETCHMATCAPTEXTURE.CPP
==============================================================================================================================================*/
// 🧩 Chrome matcap image for the parametric-sketch solid-preview inscription: decode a PNG once with stb_image (CPU, forced RGBA8), stage the
//    pixels into a device-local sampled image through a transient one-shot transfer (UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY), and build a
//    linear/clamp sampler. Raw Vulkan, no VMA — the same image-staging idiom BufferAllocation stages geometry with, minus the ImGui descriptor
//    (a custom pipeline samples this, not ImGui). Wired onto the shared VulkanHost.

#include "Graphics/Render/Surface/ParametricSketchMatcapTexture.h"

// 📝 This translation unit is the WHOLE destination's sole STB_IMAGE_IMPLEMENTATION owner (verified: no other TU in Internal/ defines it, and
//    stb is otherwise unused here). Defining the decoder here is therefore free of the LNK2005 collision the retired dual-build guarded against —
//    so the old FRONTIER_DRAUGHTING_ONLY gate is gone. Any future TU that includes stb_image.h must include only the declarations (no macro).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ReportMatcap(const char* MessageText)
    {
        std::fprintf(stderr, "[ParametricSketchMatcapTexture] %s\n", MessageText);
    }

    // 📝 First memory type allowed by the requirement bitmask that carries every required property bit — mirrors BufferAllocation so the whole
    //    engine selects memory identically.
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

    // 📝 Host-visible staging buffer (TRANSFER_SRC) of ByteSize backed by host-coherent memory. On any failure both out-handles are null + false.
    bool AllocateStagingBuffer(VkPhysicalDevice PhysicalDevice,
                               VkDevice         Device,
                               VkDeviceSize     ByteSize,
                               VkBuffer&        OutBuffer,
                               VkDeviceMemory&  OutMemory)
    {
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;

        VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        BufferInformation.size        = ByteSize;
        BufferInformation.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &BufferInformation, nullptr, &OutBuffer) != VK_SUCCESS)
        {
            OutBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryRequirements MemoryRequirements = {};
        vkGetBufferMemoryRequirements(Device, OutBuffer, &MemoryRequirements);

        bool MemoryTypeFound = false;
        const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(PhysicalDevice,
                                                               MemoryRequirements.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                               MemoryTypeFound);
        if (!MemoryTypeFound)
        {
            vkDestroyBuffer(Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocateInformation.allocationSize  = MemoryRequirements.size;
        AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
        if (vkAllocateMemory(Device, &AllocateInformation, nullptr, &OutMemory) != VK_SUCCESS ||
            vkBindBufferMemory(Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
        {
            if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Device, OutMemory, nullptr);
            vkDestroyBuffer(Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            OutMemory = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Create the device-local sampled image (R8G8B8A8_UNORM, SAMPLED | TRANSFER_DST) + its colour view. On failure every claimed handle is
    //    released and the texture image handles are left null (returns false).
    bool AllocateMatcapImage(ParametricSketchMatcapTexture& Texture, uint32_t Width, uint32_t Height)
    {
        VulkanHost& Host = *Texture.Host;

        VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
        ImageInformation.format        = VK_FORMAT_R8G8B8A8_UNORM;
        ImageInformation.extent        = { Width, Height, 1 };
        ImageInformation.mipLevels     = 1;
        ImageInformation.arrayLayers   = 1;
        ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
        ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ImageInformation.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(Host.Device, &ImageInformation, nullptr, &Texture.Image) != VK_SUCCESS)
        {
            Texture.Image = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryRequirements MemoryRequirements = {};
        vkGetImageMemoryRequirements(Host.Device, Texture.Image, &MemoryRequirements);

        bool MemoryTypeFound = false;
        const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                               MemoryRequirements.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                               MemoryTypeFound);
        if (!MemoryTypeFound)
        {
            vkDestroyImage(Host.Device, Texture.Image, nullptr);
            Texture.Image = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocateInformation.allocationSize  = MemoryRequirements.size;
        AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
        if (vkAllocateMemory(Host.Device, &AllocateInformation, nullptr, &Texture.Memory) != VK_SUCCESS ||
            vkBindImageMemory(Host.Device, Texture.Image, Texture.Memory, 0) != VK_SUCCESS)
        {
            if (Texture.Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Texture.Memory, nullptr);
            vkDestroyImage(Host.Device, Texture.Image, nullptr);
            Texture.Image  = VK_NULL_HANDLE;
            Texture.Memory = VK_NULL_HANDLE;
            return false;
        }

        VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ViewInformation.image                       = Texture.Image;
        ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        ViewInformation.format                      = VK_FORMAT_R8G8B8A8_UNORM;
        ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ViewInformation.subresourceRange.levelCount = 1;
        ViewInformation.subresourceRange.layerCount = 1;
        if (vkCreateImageView(Host.Device, &ViewInformation, nullptr, &Texture.View) != VK_SUCCESS)
        {
            vkFreeMemory(Host.Device, Texture.Memory, nullptr);
            vkDestroyImage(Host.Device, Texture.Image, nullptr);
            Texture.Image  = VK_NULL_HANDLE;
            Texture.Memory = VK_NULL_HANDLE;
            Texture.View   = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Stage SourceBytes into DestinationImage through one transient command buffer: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST ->
    //    SHADER_READ_ONLY, submitted on the graphics queue under a fence this waits on. Staging buffer + command buffer are always torn down.
    bool StageBytesIntoImage(VulkanHost&   Host,
                             VkCommandPool CommandPool,
                             VkImage       DestinationImage,
                             uint32_t      Width,
                             uint32_t      Height,
                             const void*   SourceBytes,
                             VkDeviceSize  ByteSize)
    {
        VkDevice LogicalDevice = Host.Device;

        VkBuffer       StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
        if (!AllocateStagingBuffer(Host.PhysicalDevice, LogicalDevice, ByteSize, StagingBuffer, StagingMemory))
        {
            ReportMatcap("failed to allocate staging buffer");
            return false;
        }

        void* MappedPointer = nullptr;
        if (vkMapMemory(LogicalDevice, StagingMemory, 0, ByteSize, 0, &MappedPointer) != VK_SUCCESS)
        {
            ReportMatcap("failed to map staging memory");
            vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
            vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
            return false;
        }
        std::memcpy(MappedPointer, SourceBytes, (size_t)ByteSize);
        vkUnmapMemory(LogicalDevice, StagingMemory);

        VkCommandBufferAllocateInfo CommandInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandInformation.commandPool        = CommandPool;
        CommandInformation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandInformation.commandBufferCount = 1;
        VkCommandBuffer CopyCommand = VK_NULL_HANDLE;
        bool TransferSucceeded = true;
        if (vkAllocateCommandBuffers(LogicalDevice, &CommandInformation, &CopyCommand) != VK_SUCCESS)
        {
            ReportMatcap("failed to allocate transfer command buffer");
            vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
            vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
            return false;
        }

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(CopyCommand, &BeginInformation);

        // ─── UNDEFINED → TRANSFER_DST_OPTIMAL ───
        VkImageMemoryBarrier ToTransfer = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        ToTransfer.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        ToTransfer.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ToTransfer.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        ToTransfer.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        ToTransfer.image                       = DestinationImage;
        ToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ToTransfer.subresourceRange.levelCount = 1;
        ToTransfer.subresourceRange.layerCount = 1;
        ToTransfer.srcAccessMask               = 0;
        ToTransfer.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(CopyCommand,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &ToTransfer);

        VkBufferImageCopy CopyRegion = {};
        CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        CopyRegion.imageSubresource.layerCount = 1;
        CopyRegion.imageExtent                 = { Width, Height, 1 };
        vkCmdCopyBufferToImage(CopyCommand, StagingBuffer, DestinationImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &CopyRegion);

        // ─── TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL ───
        VkImageMemoryBarrier ToShader = ToTransfer;
        ToShader.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ToShader.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(CopyCommand,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &ToShader);

        vkEndCommandBuffer(CopyCommand);

        VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence TransferFence = VK_NULL_HANDLE;
        vkCreateFence(LogicalDevice, &FenceInformation, nullptr, &TransferFence);

        VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        SubmitInformation.commandBufferCount = 1;
        SubmitInformation.pCommandBuffers    = &CopyCommand;
        if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, TransferFence) != VK_SUCCESS)
        {
            ReportMatcap("failed to submit image upload");
            TransferSucceeded = false;
        }
        else
        {
            vkWaitForFences(LogicalDevice, 1, &TransferFence, VK_TRUE, UINT64_MAX);
        }

        vkDestroyFence(LogicalDevice, TransferFence, nullptr);
        vkFreeCommandBuffers(LogicalDevice, CommandPool, 1, &CopyCommand);
        vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
        vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
        return TransferSucceeded;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeParametricSketchMatcapTexture(ParametricSketchMatcapTexture& Texture, VulkanHost& Host, const char* PngPath)
{
    Texture = ParametricSketchMatcapTexture{};
    Texture.Host        = &Host;
    Texture.ReadyStatus = false;

    if (PngPath == nullptr)
    {
        ReportMatcap("null PNG path — matcap disabled");
        return false;
    }

    // ─── decode the PNG (forced RGBA8) ───
    int DecodedWidth = 0, DecodedHeight = 0, DecodedChannels = 0;
    stbi_uc* Pixels = stbi_load(PngPath, &DecodedWidth, &DecodedHeight, &DecodedChannels, 4);
    if (Pixels == nullptr || DecodedWidth <= 0 || DecodedHeight <= 0)
    {
        ReportMatcap("PNG decode failed — matcap disabled");
        if (Pixels != nullptr) stbi_image_free(Pixels);
        return false;
    }
    Texture.Width  = (uint32_t)DecodedWidth;
    Texture.Height = (uint32_t)DecodedHeight;

    // ─── transient transfer pool (one-shot upload) ───
    VkCommandPool TransferPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    if (vkCreateCommandPool(Host.Device, &PoolInformation, nullptr, &TransferPool) != VK_SUCCESS)
    {
        ReportMatcap("transfer pool creation failed — matcap disabled");
        stbi_image_free(Pixels);
        return false;
    }

    // ─── device-local image + upload ───
    bool Constructed = AllocateMatcapImage(Texture, Texture.Width, Texture.Height);
    if (Constructed)
    {
        const VkDeviceSize ByteSize = (VkDeviceSize)Texture.Width * (VkDeviceSize)Texture.Height * 4u;
        Constructed = StageBytesIntoImage(Host, TransferPool, Texture.Image, Texture.Width, Texture.Height, Pixels, ByteSize);
    }

    stbi_image_free(Pixels);
    vkDestroyCommandPool(Host.Device, TransferPool, nullptr);

    if (!Constructed)
    {
        ReportMatcap("image upload failed — matcap disabled");
        FinalizeParametricSketchMatcapTexture(Texture);
        return false;
    }

    // 📝 Linear filter + clamp-to-edge so the disc lookup stays smooth and never wraps across the sphere seam at the image border.
    VkSamplerCreateInfo SamplerInformation = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    SamplerInformation.magFilter    = VK_FILTER_LINEAR;
    SamplerInformation.minFilter    = VK_FILTER_LINEAR;
    SamplerInformation.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerInformation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.minLod       = 0.0f;
    SamplerInformation.maxLod       = 1.0f;
    if (vkCreateSampler(Host.Device, &SamplerInformation, nullptr, &Texture.Sampler) != VK_SUCCESS)
    {
        ReportMatcap("sampler creation failed — matcap disabled");
        FinalizeParametricSketchMatcapTexture(Texture);
        return false;
    }

    Texture.ReadyStatus = true;
    return true;
}

void FinalizeParametricSketchMatcapTexture(ParametricSketchMatcapTexture& Texture)
{
    if (Texture.Host != nullptr)
    {
        VkDevice Device = Texture.Host->Device;
        if (Texture.Sampler != VK_NULL_HANDLE) vkDestroySampler(Device, Texture.Sampler, nullptr);
        if (Texture.View    != VK_NULL_HANDLE) vkDestroyImageView(Device, Texture.View, nullptr);
        if (Texture.Image   != VK_NULL_HANDLE) vkDestroyImage(Device, Texture.Image, nullptr);
        if (Texture.Memory  != VK_NULL_HANDLE) vkFreeMemory(Device, Texture.Memory, nullptr);
    }
    Texture = ParametricSketchMatcapTexture{};
}

} // namespace Frontier
