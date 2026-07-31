/*==============================================================================================================================================
                                                           PAINTICONSTORE.CPP
==============================================================================================================================================*/
// 🧩 Rasterizes instrument strips at their authored aspect and uploads them as Vulkan textures. The staging idiom is deliberately the same one
//    SvgIconRegistry uses (UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY through a transient one-shot transfer, then
//    ImGui_ImplVulkan_AddTexture binds view+sampler into the descriptor set that becomes the ImTextureID), so a reader who knows one knows both.
//
//    🔴 The single genuine difference from the shared registry, and the reason this file exists: the raster is RECTANGULAR. thorvg is asked for
//       Picture::size(Width, Height) at the document's own aspect instead of size(Edge, Edge), and the image extent and copy region follow. Every
//       other rectangle-hostile assumption in the registry (extent {Edge,Edge,1}, imageExtent {Edge,Edge,1}, EdgePixels = PixelWidth) is replaced
//       by a width/height pair. thorvg itself was never the obstacle — SwCanvas::target already takes a width and a height, and SvgRasterOutput
//       already carries PixelWidth and PixelHeight separately; only that one size() call forced the square.

#include "PaintIconStore.h"

#include "PaintCatalogue.h"

#include "EngineContext/Interface/Icons/SvgRasterizer.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include "backends/imgui_impl_vulkan.h"

#include "thorvg.h"

#include <cstring>
#include <memory>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// [px] - bound on a strip's long axis, mirroring the rasterizer's own MaximumIconEdgePixels guard. A 5:1 strip at a 64 px short
//        edge is 320 px long; this only stops a nonsense short-edge request allocating unboundedly.
constexpr uint32_t MaximumStripLongEdge = 2048u;

// 📝 The authored strip geometry, stated rather than parsed. Every instrument document is a 300x60 box (asserted by the emitter
//    across all 102), so the aspect is a constant of the catalogue and not something to re-derive per document.
constexpr float StripAuthoredWidth  = 300.0f;
constexpr float StripAuthoredHeight = 60.0f;


// A rectangular raster. Same shape as RasterizeSvg but scales the picture to a width AND a height, which is the one thing the
// shared rasterizer cannot be asked to do. Pixels come back straight-alpha ABGR8888S, mapping 1:1 onto R8G8B8A8_UNORM.
[[nodiscard]] bool RasterizeStrip(const char*            SvgByteSource,
                                  uint32_t               SvgByteCount,
                                  uint32_t               PixelWidth,
                                  uint32_t               PixelHeight,
                                  std::vector<uint32_t>& OutPixels)
{
    OutPixels.clear();

    if (SvgByteSource == nullptr || SvgByteCount == 0u || PixelWidth == 0u || PixelHeight == 0u)
    {
        return false;
    }

    // copy == true so thorvg owns its own copy of the bytes; our documents are constexpr literals, but the picture outliving the
    // call is thorvg's business rather than ours to reason about.
    tvg::Picture* StripPicture = tvg::Picture::gen();
    if (StripPicture == nullptr)
    {
        return false;
    }

    if (StripPicture->load(SvgByteSource, SvgByteCount, "svg", nullptr, true) != tvg::Result::Success)
    {
        tvg::Paint::rel(StripPicture);
        return false;
    }

    // 🔴 The whole point: a width and a height, at the document's own aspect, so the barrel is not squashed.
    StripPicture->size(static_cast<float>(PixelWidth), static_cast<float>(PixelHeight));

    OutPixels.assign(static_cast<std::size_t>(PixelWidth) * static_cast<std::size_t>(PixelHeight), 0u);

    // SwCanvas has a public destructor (unlike Paint), so a unique_ptr frees it on every path below.
    std::unique_ptr<tvg::SwCanvas> RasterCanvas(tvg::SwCanvas::gen());
    if (RasterCanvas == nullptr)
    {
        tvg::Paint::rel(StripPicture);
        OutPixels.clear();
        return false;
    }

    if (RasterCanvas->target(OutPixels.data(), PixelWidth, PixelWidth, PixelHeight,
                             tvg::ColorSpace::ABGR8888S) != tvg::Result::Success)
    {
        tvg::Paint::rel(StripPicture);
        OutPixels.clear();
        return false;
    }

    // add transfers ownership to the canvas only on success; a failed add leaves the picture ours to release.
    if (RasterCanvas->add(StripPicture) != tvg::Result::Success)
    {
        tvg::Paint::rel(StripPicture);
        OutPixels.clear();
        return false;
    }

    // From here the canvas owns the picture and releases it; a draw/sync failure leaves nothing for us to free.
    if (RasterCanvas->draw(true) != tvg::Result::Success ||
        RasterCanvas->sync() != tvg::Result::Success)
    {
        OutPixels.clear();
        return false;
    }

    return true;
}


// First memory type allowed by the requirement bitmask that carries every required property bit. Mirrors the registry's selection
// so the whole engine picks memory identically.
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


// Create the device-local sampled image (R8G8B8A8_UNORM, SAMPLED | TRANSFER_DST) + its colour view. Takes a WIDTH and a HEIGHT
// where the registry takes one edge. On failure every claimed handle is released and the handles are left null (returns false).
[[nodiscard]] bool AllocateStripImage(VulkanHost& Host, PaintStripTexture& Texture, uint32_t PixelWidth, uint32_t PixelHeight)
{
    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ImageInformation.extent        = { PixelWidth, PixelHeight, 1u };
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


// Stage SourceBytes into DestinationImage through one transient command buffer, submitted on the graphics queue under a fence this
// waits on. Staging buffer + command buffer are always torn down. Takes a width and a height for the copy extent.
[[nodiscard]] bool StageBytesIntoStrip(VulkanHost&   Host,
                                       VkCommandPool CommandPool,
                                       VkImage       DestinationImage,
                                       uint32_t      PixelWidth,
                                       uint32_t      PixelHeight,
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
    CopyRegion.imageExtent                 = { PixelWidth, PixelHeight, 1u };
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


// Build the linear/clamp sampler + bind the view into an ImGui descriptor set. On failure the sampler (if any) is released and the
// descriptor id is left null.
[[nodiscard]] bool BindStripDescriptor(VulkanHost& Host, PaintStripTexture& Texture)
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

    Texture.StripTextureId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(Texture.DescriptorSet));
    return true;
}


// Tear down one strip's Vulkan chain + ImGui descriptor. Assumes the device is idle.
void DestroyStripTexture(VulkanHost& Host, PaintStripTexture& Texture) noexcept
{
    if (Texture.DescriptorSet != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(Texture.DescriptorSet);
    }
    if (Texture.Sampler     != VK_NULL_HANDLE) vkDestroySampler(Host.Device, Texture.Sampler, nullptr);
    if (Texture.ImageView   != VK_NULL_HANDLE) vkDestroyImageView(Host.Device, Texture.ImageView, nullptr);
    if (Texture.Image       != VK_NULL_HANDLE) vkDestroyImage(Host.Device, Texture.Image, nullptr);
    if (Texture.ImageMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Texture.ImageMemory, nullptr);
    Texture = PaintStripTexture{};
}


// Rasterize + upload one instrument's strip. On any failure returns a texture with a null StripTextureId and no live handles.
[[nodiscard]] PaintStripTexture UploadStripTexture(VulkanHost& Host,
                                                   const char* SvgByteSource,
                                                   uint32_t    SvgByteCount,
                                                   uint32_t    ShortEdgePixels)
{
    PaintStripTexture Texture;

    // The long axis follows from the authored aspect, so the barrel keeps its proportions. Rounded rather than truncated: a 5:1
    // box at an odd short edge would otherwise lose a column and shear the art by a fraction of a texel.
    const float    AspectRatio = StripAuthoredWidth / StripAuthoredHeight;
    const uint32_t PixelHeight = ShortEdgePixels;
    uint32_t       PixelWidth  = static_cast<uint32_t>((static_cast<float>(ShortEdgePixels) * AspectRatio) + 0.5f);

    if (PixelWidth == 0u || PixelHeight == 0u || PixelWidth > MaximumStripLongEdge)
    {
        return Texture;
    }

    std::vector<uint32_t> Pixels;
    if (!RasterizeStrip(SvgByteSource, SvgByteCount, PixelWidth, PixelHeight, Pixels))
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

    bool Constructed = AllocateStripImage(Host, Texture, PixelWidth, PixelHeight);
    if (Constructed)
    {
        const VkDeviceSize ByteSize = static_cast<VkDeviceSize>(PixelWidth) *
                                      static_cast<VkDeviceSize>(PixelHeight) * 4u;
        Constructed = StageBytesIntoStrip(Host, TransferPool, Texture.Image, PixelWidth, PixelHeight,
                                          Pixels.data(), ByteSize);
    }

    vkDestroyCommandPool(Host.Device, TransferPool, nullptr);

    if (!Constructed || !BindStripDescriptor(Host, Texture))
    {
        DestroyStripTexture(Host, Texture);
        return PaintStripTexture{};
    }

    Texture.PixelWidth  = PixelWidth;
    Texture.PixelHeight = PixelHeight;
    return Texture;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializePaintIconStore(PaintIconStore& Store, VulkanHost& Host, uint32_t ShortEdgePixels)
{
    Store.Host           = &Host;
    Store.StripShortEdge = ShortEdgePixels;
    Store.IndexToStrip.clear();

    // 📝 A reference on the shared engine, not a second engine. thorvg's bring-up is process-wide and reference-counted, so the
    //    store and the icon registry can each hold one without either terminating the other's.
    return InitializeSvgEngine();
}


const PaintStripTexture* ResolvePaintStrip(PaintIconStore& Store, int InstrumentIndex)
{
    if (Store.Host == nullptr)
    {
        return nullptr;
    }

    const auto Existing = Store.IndexToStrip.find(InstrumentIndex);
    if (Existing != Store.IndexToStrip.end())
    {
        // 📝 A cached FAILURE is still cached, deliberately. A document that will not rasterize will not rasterize on the next
        //    frame either, and retrying every frame would stall the card at the fence forever.
        return (Existing->second.StripTextureId != 0) ? &Existing->second : nullptr;
    }

    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* const Instruments = ResolvePaintInstruments(InstrumentCount);
    if (InstrumentIndex < 0 || InstrumentIndex >= InstrumentCount)
    {
        return nullptr;
    }

    const char* const Document = Instruments[InstrumentIndex].FullDocument;
    if (Document == nullptr)
    {
        return nullptr;
    }

    const uint32_t ByteCount = static_cast<uint32_t>(std::strlen(Document));
    PaintStripTexture Uploaded = UploadStripTexture(*Store.Host, Document, ByteCount, Store.StripShortEdge);

    const auto Inserted = Store.IndexToStrip.emplace(InstrumentIndex, Uploaded);
    return (Inserted.first->second.StripTextureId != 0) ? &Inserted.first->second : nullptr;
}


void FinalizePaintIconStore(PaintIconStore& Store)
{
    if (Store.Host != nullptr)
    {
        // Waited idle before anything is destroyed, so no in-flight frame still samples a strip.
        vkDeviceWaitIdle(Store.Host->Device);

        for (auto& Entry : Store.IndexToStrip)
        {
            DestroyStripTexture(*Store.Host, Entry.second);
        }
        FinalizeSvgEngine();
    }

    Store.IndexToStrip.clear();
    Store.Host           = nullptr;
    Store.StripShortEdge = 0u;
}

} // namespace Frontier
