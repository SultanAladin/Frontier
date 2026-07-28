/*==============================================================================================================================================
                                                                SVGICONREGISTRY.CPP
==============================================================================================================================================*/
// 🧩 Uploads rasterized SVG icons into Vulkan textures ImGui can sample, deduplicating by content hash. The staging idiom mirrors
//    ParametricSketchMatcapTexture (UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY through a transient one-shot transfer), then adds the piece the
//    matcap did not need: ImGui_ImplVulkan_AddTexture binds the view+sampler to a VkDescriptorSet that becomes the ImTextureID. A key resolves to
//    a texture through a content hash, so the global (g-) and local (cad-) tiers naturally share a glyph when their bytes match.

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include "EngineContext/Interface/Icons/SvgRasterizer.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include "backends/imgui_impl_vulkan.h"

#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 FNV-1a over the raster bytes plus the requested edge, so a key is deduplicated only against pixels that would upload
//    identically. Two SVG sources that rasterize to the same texels at the same size collapse onto one texture.
[[nodiscard]] uint64_t ContentHash(const char* SvgByteSource, uint32_t SvgByteCount, uint32_t EdgePixels) noexcept
{
    uint64_t Accumulator = 1469598103934665603ull;
    const auto Fold = [&Accumulator](uint8_t Byte) noexcept
    {
        Accumulator ^= static_cast<uint64_t>(Byte);
        Accumulator *= 1099511628211ull;
    };

    for (uint32_t ByteIndex = 0u; ByteIndex < SvgByteCount; ++ByteIndex)
    {
        Fold(static_cast<uint8_t>(SvgByteSource[ByteIndex]));
    }
    for (uint32_t Shift = 0u; Shift < 32u; Shift += 8u)
    {
        Fold(static_cast<uint8_t>((EdgePixels >> Shift) & 0xFFu));
    }
    return Accumulator;
}

// First memory type allowed by the requirement bitmask that carries every required property bit — mirrors the matcap/geometry
// selection so the whole engine picks memory identically.
[[nodiscard]] uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
                                             uint32_t              CompatibleTypesBitmask,
                                             VkMemoryPropertyFlags RequiredProperties,
                                             bool&                 FoundEnabled) noexcept
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    for (uint32_t IndexIterator = 0u; IndexIterator < MemoryProperties.memoryTypeCount; ++IndexIterator)
    {
        const bool TypeCompatible = (CompatibleTypesBitmask & (1u << IndexIterator)) != 0u;
        const bool PropertyMatch  = (MemoryProperties.memoryTypes[IndexIterator].propertyFlags & RequiredProperties) == RequiredProperties;
        if (TypeCompatible && PropertyMatch)
        {
            FoundEnabled = true;
            return IndexIterator;
        }
    }
    FoundEnabled = false;
    return 0u;
}

// Host-visible staging buffer (TRANSFER_SRC) of ByteSize backed by host-coherent memory. On any failure both out-handles are null + false.
[[nodiscard]] bool AllocateStagingBuffer(VkPhysicalDevice PhysicalDevice,
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
        if (OutMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Device, OutMemory, nullptr);
        }
        vkDestroyBuffer(Device, OutBuffer, nullptr);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Create the device-local sampled image (R8G8B8A8_UNORM, SAMPLED | TRANSFER_DST) + its colour view into the texture. On failure
// every claimed handle is released and the texture image handles are left null (returns false).
[[nodiscard]] bool AllocateIconImage(VulkanHost& Host, SvgIconTexture& Texture, uint32_t EdgePixels)
{
    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ImageInformation.extent        = { EdgePixels, EdgePixels, 1u };
    ImageInformation.mipLevels     = 1u;
    ImageInformation.arrayLayers   = 1u;
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
    if (vkAllocateMemory(Host.Device, &AllocateInformation, nullptr, &Texture.ImageMemory) != VK_SUCCESS ||
        vkBindImageMemory(Host.Device, Texture.Image, Texture.ImageMemory, 0) != VK_SUCCESS)
    {
        if (Texture.ImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Host.Device, Texture.ImageMemory, nullptr);
        }
        vkDestroyImage(Host.Device, Texture.Image, nullptr);
        Texture.Image       = VK_NULL_HANDLE;
        Texture.ImageMemory = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInformation.image                       = Texture.Image;
    ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    ViewInformation.format                      = VK_FORMAT_R8G8B8A8_UNORM;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInformation.subresourceRange.levelCount = 1u;
    ViewInformation.subresourceRange.layerCount = 1u;
    if (vkCreateImageView(Host.Device, &ViewInformation, nullptr, &Texture.ImageView) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, Texture.ImageMemory, nullptr);
        vkDestroyImage(Host.Device, Texture.Image, nullptr);
        Texture.Image       = VK_NULL_HANDLE;
        Texture.ImageMemory = VK_NULL_HANDLE;
        Texture.ImageView   = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Stage SourceBytes into DestinationImage through one transient command buffer: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST ->
// SHADER_READ_ONLY, submitted on the graphics queue under a fence this waits on. Staging buffer + command buffer are always torn down.
[[nodiscard]] bool StageBytesIntoImage(VulkanHost&   Host,
                                       VkCommandPool CommandPool,
                                       VkImage       DestinationImage,
                                       uint32_t      EdgePixels,
                                       const void*   SourceBytes,
                                       VkDeviceSize  ByteSize)
{
    VkDevice LogicalDevice = Host.Device;

    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateStagingBuffer(Host.PhysicalDevice, LogicalDevice, ByteSize, StagingBuffer, StagingMemory))
    {
        return false;
    }

    void* MappedPointer = nullptr;
    if (vkMapMemory(LogicalDevice, StagingMemory, 0, ByteSize, 0, &MappedPointer) != VK_SUCCESS)
    {
        vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
        vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
        return false;
    }
    std::memcpy(MappedPointer, SourceBytes, static_cast<size_t>(ByteSize));
    vkUnmapMemory(LogicalDevice, StagingMemory);

    VkCommandBufferAllocateInfo CommandInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandInformation.commandPool        = CommandPool;
    CommandInformation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandInformation.commandBufferCount = 1u;
    VkCommandBuffer CopyCommand = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(LogicalDevice, &CommandInformation, &CopyCommand) != VK_SUCCESS)
    {
        vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
        vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(CopyCommand, &BeginInformation);

    // UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier ToTransfer = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    ToTransfer.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
    ToTransfer.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToTransfer.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.image                       = DestinationImage;
    ToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ToTransfer.subresourceRange.levelCount = 1u;
    ToTransfer.subresourceRange.layerCount = 1u;
    ToTransfer.srcAccessMask               = 0;
    ToTransfer.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(CopyCommand,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToTransfer);

    VkBufferImageCopy CopyRegion = {};
    CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.layerCount = 1u;
    CopyRegion.imageExtent                 = { EdgePixels, EdgePixels, 1u };
    vkCmdCopyBufferToImage(CopyCommand, StagingBuffer, DestinationImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &CopyRegion);

    // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
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
    SubmitInformation.commandBufferCount = 1u;
    SubmitInformation.pCommandBuffers    = &CopyCommand;
    bool TransferSucceeded = true;
    if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, TransferFence) != VK_SUCCESS)
    {
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

// Build the linear/clamp sampler + bind the view into an ImGui descriptor set. On failure the sampler (if any) is released and
// the descriptor id is left null.
[[nodiscard]] bool BindIconDescriptor(VulkanHost& Host, SvgIconTexture& Texture)
{
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
        Texture.Sampler = VK_NULL_HANDLE;
        return false;
    }

    Texture.DescriptorSet = ImGui_ImplVulkan_AddTexture(Texture.Sampler, Texture.ImageView,
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (Texture.DescriptorSet == VK_NULL_HANDLE)
    {
        vkDestroySampler(Host.Device, Texture.Sampler, nullptr);
        Texture.Sampler = VK_NULL_HANDLE;
        return false;
    }

    Texture.IconTextureId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(Texture.DescriptorSet));
    return true;
}

// Tear down one texture's Vulkan chain + ImGui descriptor. Assumes the device is idle.
void DestroyIconTexture(VulkanHost& Host, SvgIconTexture& Texture) noexcept
{
    if (Texture.DescriptorSet != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(Texture.DescriptorSet);
    }
    if (Texture.Sampler   != VK_NULL_HANDLE) vkDestroySampler(Host.Device, Texture.Sampler, nullptr);
    if (Texture.ImageView != VK_NULL_HANDLE) vkDestroyImageView(Host.Device, Texture.ImageView, nullptr);
    if (Texture.Image     != VK_NULL_HANDLE) vkDestroyImage(Host.Device, Texture.Image, nullptr);
    if (Texture.ImageMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Texture.ImageMemory, nullptr);
    Texture = SvgIconTexture{};
}

// Rasterize + upload a fresh texture for a content hash. On any failure returns a texture with a null IconTextureId and no live handles.
[[nodiscard]] SvgIconTexture UploadIconTexture(VulkanHost& Host,
                                               const char* SvgByteSource,
                                               uint32_t    SvgByteCount,
                                               uint32_t    EdgePixels)
{
    SvgIconTexture Texture;

    const SvgRasterOutput Raster = RasterizeSvg(SvgByteSource, SvgByteCount, EdgePixels);
    if (!Raster.RasterSuccess)
    {
        return Texture;
    }

    VkCommandPool TransferPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    if (vkCreateCommandPool(Host.Device, &PoolInformation, nullptr, &TransferPool) != VK_SUCCESS)
    {
        return Texture;
    }

    bool Constructed = AllocateIconImage(Host, Texture, Raster.PixelWidth);
    if (Constructed)
    {
        const VkDeviceSize ByteSize = static_cast<VkDeviceSize>(Raster.PixelWidth) *
                                      static_cast<VkDeviceSize>(Raster.PixelHeight) * 4u;
        Constructed = StageBytesIntoImage(Host, TransferPool, Texture.Image, Raster.PixelWidth,
                                          Raster.Pixels.data(), ByteSize);
    }

    vkDestroyCommandPool(Host.Device, TransferPool, nullptr);

    if (!Constructed || !BindIconDescriptor(Host, Texture))
    {
        DestroyIconTexture(Host, Texture);
        return SvgIconTexture{};
    }

    Texture.EdgePixels = Raster.PixelWidth;
    return Texture;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSvgIconRegistry(SvgIconRegistry& Registry, VulkanHost& Host)
{
    Registry.Host = &Host;
    Registry.KeyToHash.clear();
    Registry.HashToTexture.clear();
    return InitializeSvgEngine();
}

bool RegisterSvgIcon(SvgIconRegistry&   Registry,
                     const std::string& IconKey,
                     const char*        SvgByteSource,
                     uint32_t           SvgByteCount,
                     uint32_t           EdgePixels)
{
    if (Registry.Host == nullptr || SvgByteSource == nullptr || SvgByteCount == 0u || IconKey.empty())
    {
        return false;
    }

    const uint32_t ResolvedEdge = EdgePixels != 0u ? EdgePixels : Registry.DefaultEdgePixels;
    const uint64_t Hash         = ContentHash(SvgByteSource, SvgByteCount, ResolvedEdge);

    // Rebinding an existing key that already points elsewhere drops its old reference first.
    const auto PriorBinding = Registry.KeyToHash.find(IconKey);
    if (PriorBinding != Registry.KeyToHash.end())
    {
        if (PriorBinding->second == Hash)
        {
            return true;   // already bound to this exact content
        }

        const auto PriorTexture = Registry.HashToTexture.find(PriorBinding->second);
        if (PriorTexture != Registry.HashToTexture.end() && --PriorTexture->second.ReferenceCount == 0u)
        {
            DestroyIconTexture(*Registry.Host, PriorTexture->second);
            Registry.HashToTexture.erase(PriorTexture);
        }
    }

    // Reuse an existing texture for this content, or upload a fresh one.
    auto ExistingTexture = Registry.HashToTexture.find(Hash);
    if (ExistingTexture == Registry.HashToTexture.end())
    {
        SvgIconTexture Uploaded = UploadIconTexture(*Registry.Host, SvgByteSource, SvgByteCount, ResolvedEdge);
        if (Uploaded.IconTextureId == 0)
        {
            Registry.KeyToHash.erase(IconKey);
            return false;
        }

        Uploaded.ReferenceCount = 0u;
        ExistingTexture = Registry.HashToTexture.emplace(Hash, Uploaded).first;
    }

    ExistingTexture->second.ReferenceCount += 1u;
    Registry.KeyToHash[IconKey] = Hash;
    return true;
}

ImTextureID ResolveIconTexture(const SvgIconRegistry& Registry, const std::string& IconKey)
{
    const auto Binding = Registry.KeyToHash.find(IconKey);
    if (Binding == Registry.KeyToHash.end())
    {
        return 0;
    }

    const auto Texture = Registry.HashToTexture.find(Binding->second);
    if (Texture == Registry.HashToTexture.end())
    {
        return 0;
    }

    return Texture->second.IconTextureId;
}

bool IconRegistered(const SvgIconRegistry& Registry, const std::string& IconKey)
{
    return ResolveIconTexture(Registry, IconKey) != 0;
}

void FinalizeSvgIconRegistry(SvgIconRegistry& Registry)
{
    if (Registry.Host != nullptr)
    {
        // No in-flight frame may reference these descriptor sets when they are removed.
        vkDeviceWaitIdle(Registry.Host->Device);
        for (auto& HashedTexture : Registry.HashToTexture)
        {
            DestroyIconTexture(*Registry.Host, HashedTexture.second);
        }
    }

    Registry.HashToTexture.clear();
    Registry.KeyToHash.clear();
    Registry.Host = nullptr;
    FinalizeSvgEngine();
}

} // namespace Frontier
