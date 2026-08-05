/*==============================================================================================================================================
                                                      SURFELLIFECYCLESUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the surfel lifecycle. Initialize builds two descriptor set layouts — the SPAWN layout (visibility set 0 mirroring
//    SurfaceShade's bindings + a grid/pool/tile set 1) and the POOL layout (7 storage bindings shared by Prepare / Age / Allocate) — their pipeline
//    layouts, the four compute pipelines, one descriptor pool with three sets, a point sampler for the id image, and the two owned per-tile request
//    buffers. Prepare is recorded once (Prepared latches). Spawn records SpawnRequest -> barrier -> Allocate(alloc) -> barrier -> Allocate(sync).
//    Age records the +1/TTL pass. Raw Vulkan, no VMA, mirroring InstanceCullSubmission / SurfelGridSlotting.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelLifecycleSubmission.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportLifecycle(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelLifecycle] %s\n", MessageText);
}

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

bool AllocateStorageBuffer(VulkanHost& Host, VkDeviceSize ByteSize, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (ByteSize == 0) ByteSize = 4;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    // TRANSFER_SRC lets the diagnostic dump (DumpSurfelStateToDisk / the L key) copy these buffers — notably TileAlloc and
    // TileCandidate, this frame's spawn requests — back to host staging. Harmless on the buffers the dump never reads.
    BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);
    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
        return Bytes;
    std::fseek(Handle, 0, SEEK_END);
    long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Bytes.resize((size_t)Size);
        size_t Read = std::fread(Bytes.data(), 1, (size_t)Size, Handle);
        if (Read != (size_t)Size)
            Bytes.clear();
    }
    std::fclose(Handle);
    return Bytes;
}

VkShaderModule ConstructShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
{
    if (Bytes.empty() || (Bytes.size() % 4) != 0)
        return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ModuleInfo.codeSize = Bytes.size();
    ModuleInfo.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());
    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInfo, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Module;
}

VkPipeline ConstructComputePipeline(VulkanHost& Host, VkPipelineLayout Layout, const std::string& Directory, const char* FileName)
{
    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/" + FileName));
    if (Module == VK_NULL_HANDLE)
    {
        ReportLifecycle("lifecycle shader module unavailable");
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Layout;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    const VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &Pipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (Outcome != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Pipeline;
}

// A compute->compute all-storage barrier between two lifecycle stages: the previous stage's writes are visible to the next stage's reads/writes.
void LifecycleStorageBarrier(VkCommandBuffer CommandBuffer)
{
    VkMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &Barrier, 0, nullptr, 0, nullptr);
}

// Build a storage-only set layout of N compute bindings starting at binding 0. Used for the spawn grid set (6) and the pool set (10).
VkDescriptorSetLayout ConstructStorageSetLayout(VulkanHost& Host, uint32_t BindingCount)
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingCount);
    for (uint32_t Index = 0; Index < BindingCount; ++Index)
    {
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = BindingCount;
    LayoutInformation.pBindings    = Bindings.data();
    VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Layout;
}

// The spawn visibility set 0: b0 combined image sampler (the id image), b1/b2/b3 head mesh SSBOs, b5/b6/b7 floor mesh SSBOs. Binding 4 is unused (the
// SurfaceShade material UBO lives there; the spawn pass does not shade), so the layout leaves it absent — GLSL never references set 0 binding 4.
VkDescriptorSetLayout ConstructSpawnVisibilityLayout(VulkanHost& Host)
{
    VkDescriptorSetLayoutBinding Bindings[7] = {};
    const uint32_t   BindingIds[7]   = { 0u, 1u, 2u, 3u, 5u, 6u, 7u };
    const VkDescriptorType Types[7]  = {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };
    for (int Index = 0; Index < 7; ++Index)
    {
        Bindings[Index].binding         = BindingIds[Index];
        Bindings[Index].descriptorType  = Types[Index];
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 7;
    LayoutInformation.pBindings    = Bindings;
    VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Layout;
}

VkPipelineLayout ConstructPipelineLayout(VulkanHost& Host, const VkDescriptorSetLayout* SetLayouts, uint32_t SetCount, uint32_t PushSize)
{
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = PushSize;

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = SetCount;
    PipelineLayoutInformation.pSetLayouts            = SetLayouts;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Layout;
}

// 🔴 THE ONE DEFINITION OF THE POOL PUSH BLOCK, byte-mirroring SurfelAllocate.comp / SurfelAge.comp / SurfelPrepare.comp's PushBlock. It was previously
//    re-declared locally at each of the four record sites; that is four chances for one copy to drift from the shader with no compile error and no
//    validation error — the shader would simply read one field's bytes as another's.
//    📝 THE TAIL GROWS, THE PREFIX DOES NOT MOVE. The three shaders declare PREFIXES of this struct — Prepare stops after Capacity, Allocate stops after
//       Pad0, only Age reads GridOrigin and beyond. A push block is a block-layout interface, so a shader that declares fewer TRAILING fields than the
//       host pushes is legal and reads the ones it named at the offsets it named. That is ONLY true of the tail: inserting a field anywhere above
//       GridOrigin would silently re-offset both other shaders.
struct PoolPushBlock
{
    int32_t Capacity;
    int32_t TileCount;
    int32_t SyncPass;       // 0 = allocate lanes, 1 = alive-sync lane
    int32_t Pad0;           // [-] - keeps GridOrigin below vec4-aligned. Every record site zero-inits with `= {}`.
    float   GridOrigin[4];
    // 🔴 THE LIVE WORLD SCALE THE AGE PASS WAS MISSING. SurfelHashOfPosition divides by the cell diameter, so without these the pass hashed with the
    //    BAKED default while slotting hashed with the F10 value — the crowding rent has been reading a different cell's occupancy than the surfel's own
    //    whenever the slider moved off default. See the matching note in SurfelAge.comp. CameraPosition is the RAW eye, not the snapped GridOrigin above.
    float   CameraPosition[4];
    float   TuneCellDiameter;  // [m] - MUST be the same value this frame's slotting hashed with
    float   TuneBaseRadius;    // [m] - live cascade-0 disc radius
};

// Write a single storage-buffer descriptor.
void WriteStorageDescriptor(VulkanHost& Host, VkDescriptorSet Set, uint32_t Binding, VkBuffer Buffer)
{
    VkDescriptorBufferInfo BufferInfo = { Buffer, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Set;
    Write.dstBinding      = Binding;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Write.pBufferInfo     = &BufferInfo;
    vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Lifecycle,
                                         VulkanHost&                Host,
                                         uint32_t                   MaxTileCount,
                                         const char*                ShaderDirectory)
{
    Lifecycle = SurfelLifecycleSubmission{};
    Lifecycle.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportLifecycle("no device — lifecycle not built");
        return false;
    }

    if (MaxTileCount == 0) MaxTileCount = 1;
    Lifecycle.TileCapacity = MaxTileCount;

    // --- owned per-tile request buffers ---
    const VkDeviceSize TileAllocBytes     = (VkDeviceSize)MaxTileCount * sizeof(int32_t);
    const VkDeviceSize TileCandidateBytes = (VkDeviceSize)MaxTileCount * 2 * 4 * sizeof(float);   // 2 vec4 per tile
    if (!AllocateStorageBuffer(Host, TileAllocBytes,     Lifecycle.TileAllocBuffer,     Lifecycle.TileAllocMemory) ||
        !AllocateStorageBuffer(Host, TileCandidateBytes, Lifecycle.TileCandidateBuffer, Lifecycle.TileCandidateMemory))
    {
        ReportLifecycle("tile request buffer allocation failed");
        FinalizeSurfelLifecycleSubmission(Lifecycle);
        return false;
    }

    // --- point sampler for the id image (nearest / clamp; a filtered id is a wrong id) ---
    VkSamplerCreateInfo SamplerInformation = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    SamplerInformation.magFilter    = VK_FILTER_NEAREST;
    SamplerInformation.minFilter    = VK_FILTER_NEAREST;
    SamplerInformation.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInformation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Host.Device, &SamplerInformation, Host.Allocator, &Lifecycle.PointSampler) != VK_SUCCESS)
    {
        ReportLifecycle("point sampler creation failed");
        FinalizeSurfelLifecycleSubmission(Lifecycle);
        return false;
    }

    // --- set layouts ---
    Lifecycle.SpawnVisibilityLayout = ConstructSpawnVisibilityLayout(Host);
    Lifecycle.SpawnGridLayout       = ConstructStorageSetLayout(Host, 6);   // offsets, list, surfels, tileAlloc, tileCandidate, touched (b5, economy)
    // 🔴 A binding a shader DECLARES but the layout omits is not a no-op: pipeline creation may still succeed and the dispatch then reads an undefined
    //    descriptor. Every binding below must exist here AND be written before the first Allocate/Age.
    Lifecycle.PoolLayout            = ConstructStorageSetLayout(Host, 10);  // surfels, pool, poolAlloc, poolMax, alive, tileAlloc, tileCandidate, offsets (b7), touched (b8), census (b9)
    if (Lifecycle.SpawnVisibilityLayout == VK_NULL_HANDLE || Lifecycle.SpawnGridLayout == VK_NULL_HANDLE || Lifecycle.PoolLayout == VK_NULL_HANDLE)
    {
        ReportLifecycle("set layout creation failed");
        FinalizeSurfelLifecycleSubmission(Lifecycle);
        return false;
    }

    // --- pipeline layouts ---
    const VkDescriptorSetLayout SpawnSets[2] = { Lifecycle.SpawnVisibilityLayout, Lifecycle.SpawnGridLayout };
    Lifecycle.SpawnPipelineLayout = ConstructPipelineLayout(Host, SpawnSets, 2, sizeof(SurfelSpawnConstants));
    // The pool layout drives Prepare (push { int Capacity }), Allocate (push { int Capacity, TileCount, SyncPass, Pad }), and Age (which additionally
    // reads GridOrigin to hash each surfel's cell for the crowding rent). One shared push range sized to the whole block (32 bytes: 16 header + a padded
    // vec4 GridOrigin) keeps the pipeline layout single — the passes that don't use GridOrigin simply don't read it.
    Lifecycle.PoolPipelineLayout = ConstructPipelineLayout(Host, &Lifecycle.PoolLayout, 1, sizeof(PoolPushBlock));
    if (Lifecycle.SpawnPipelineLayout == VK_NULL_HANDLE || Lifecycle.PoolPipelineLayout == VK_NULL_HANDLE)
    {
        ReportLifecycle("pipeline layout creation failed");
        FinalizeSurfelLifecycleSubmission(Lifecycle);
        return false;
    }

    // --- pipelines ---
    const std::string Directory = ShaderDirectory;
    Lifecycle.SpawnPipeline    = ConstructComputePipeline(Host, Lifecycle.SpawnPipelineLayout, Directory, "SurfelSpawnRequest.comp.spv");
    Lifecycle.PreparePipeline  = ConstructComputePipeline(Host, Lifecycle.PoolPipelineLayout,  Directory, "SurfelPrepare.comp.spv");
    Lifecycle.AgePipeline      = ConstructComputePipeline(Host, Lifecycle.PoolPipelineLayout,  Directory, "SurfelAge.comp.spv");
    Lifecycle.AllocatePipeline = ConstructComputePipeline(Host, Lifecycle.PoolPipelineLayout,  Directory, "SurfelAllocate.comp.spv");
    if (Lifecycle.SpawnPipeline == VK_NULL_HANDLE || Lifecycle.PreparePipeline == VK_NULL_HANDLE ||
        Lifecycle.AgePipeline == VK_NULL_HANDLE || Lifecycle.AllocatePipeline == VK_NULL_HANDLE)
    {
        ReportLifecycle("one or more lifecycle pipelines failed to build");
        FinalizeSurfelLifecycleSubmission(Lifecycle);
        return false;
    }

    // --- descriptor pool + three sets ---
    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[0].descriptorCount = 6 + 6 + 12;  // spawn visibility (6 SSBOs) + spawn grid (6: +touched) + pool (12: +offsets +touched +census +requests +requestCount)
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[1].descriptorCount = 1;           // the id image

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 3;
    PoolInformation.poolSizeCount = 2;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Lifecycle.DescriptorPool) != VK_SUCCESS)
    {
        ReportLifecycle("descriptor pool creation failed");
        FinalizeSurfelLifecycleSubmission(Lifecycle);
        return false;
    }

    const VkDescriptorSetLayout AllocLayouts[3] = { Lifecycle.SpawnVisibilityLayout, Lifecycle.SpawnGridLayout, Lifecycle.PoolLayout };
    VkDescriptorSet AllocSets[3] = {};
    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Lifecycle.DescriptorPool;
    SetAllocation.descriptorSetCount = 3;
    SetAllocation.pSetLayouts        = AllocLayouts;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, AllocSets) != VK_SUCCESS)
    {
        ReportLifecycle("descriptor set allocation failed");
        FinalizeSurfelLifecycleSubmission(Lifecycle);
        return false;
    }
    Lifecycle.SpawnVisibilitySet = AllocSets[0];
    Lifecycle.SpawnGridSet       = AllocSets[1];
    Lifecycle.PoolSet            = AllocSets[2];

    // Point set 1's OWNED bindings now (tileAlloc @3, tileCandidate @4); the borrowed offsets/list/surfels are pointed at Refresh.
    WriteStorageDescriptor(Host, Lifecycle.SpawnGridSet, 3u, Lifecycle.TileAllocBuffer);
    WriteStorageDescriptor(Host, Lifecycle.SpawnGridSet, 4u, Lifecycle.TileCandidateBuffer);
    // Point the pool set's OWNED tile bindings (tileAlloc @5, tileCandidate @6); the borrowed pool buffers are pointed at Refresh.
    WriteStorageDescriptor(Host, Lifecycle.PoolSet, 5u, Lifecycle.TileAllocBuffer);
    WriteStorageDescriptor(Host, Lifecycle.PoolSet, 6u, Lifecycle.TileCandidateBuffer);

    Lifecycle.ReadyCondition = true;
    return true;
}

void RefreshSurfelLifecycleVisibility(SurfelLifecycleSubmission& Lifecycle,
                                      VkImageView                IdView,
                                      const SurfelPool&          Pool,
                                      const SurfelGridSlotting&  Slotting,
                                      VkBuffer                   VertexBuffer,
                                      VkBuffer                   IndexBuffer,
                                      VkBuffer                   InstanceBuffer,
                                      VkBuffer                   FloorVertexBuffer,
                                      VkBuffer                   FloorIndexBuffer,
                                      VkBuffer                   FloorInstanceBuffer,
                                      VkBuffer                   CensusCountersBuffer)
{
    if (!Lifecycle.ReadyCondition || Lifecycle.Host == nullptr)
        return;
    VulkanHost& Host = *Lifecycle.Host;

    // When no floor loaded, alias the floor bindings onto the head buffers so no set-0 descriptor is left undefined (mirrors SurfaceShade). The caller
    // must then leave FloorShadeEnabled at 0 so the spawn shader discards floor pixels.
    if (FloorVertexBuffer   == VK_NULL_HANDLE) FloorVertexBuffer   = VertexBuffer;
    if (FloorIndexBuffer    == VK_NULL_HANDLE) FloorIndexBuffer    = IndexBuffer;
    if (FloorInstanceBuffer == VK_NULL_HANDLE) FloorInstanceBuffer = InstanceBuffer;

    // --- set 0: the id image (b0) ---
    if (IdView != Lifecycle.BoundIdView && IdView != VK_NULL_HANDLE)
    {
        VkDescriptorImageInfo ImageInfo = {};
        ImageInfo.sampler     = Lifecycle.PointSampler;
        ImageInfo.imageView   = IdView;
        ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        Write.dstSet          = Lifecycle.SpawnVisibilitySet;
        Write.dstBinding      = 0u;
        Write.descriptorCount = 1;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Write.pImageInfo      = &ImageInfo;
        vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
        Lifecycle.BoundIdView = IdView;
    }

    // --- set 0: the six mesh/instance SSBOs (b1/b2/b3 head, b5/b6/b7 floor) ---
    struct MeshBind { uint32_t Binding; VkBuffer Buffer; VkBuffer* Cache; };
    MeshBind MeshBinds[6] = {
        { 1u, VertexBuffer,        &Lifecycle.BoundVertexBuffer },
        { 2u, IndexBuffer,         &Lifecycle.BoundIndexBuffer },
        { 3u, InstanceBuffer,      &Lifecycle.BoundInstanceBuffer },
        { 5u, FloorVertexBuffer,   &Lifecycle.BoundFloorVertexBuffer },
        { 6u, FloorIndexBuffer,    &Lifecycle.BoundFloorIndexBuffer },
        { 7u, FloorInstanceBuffer, &Lifecycle.BoundFloorInstanceBuffer },
    };
    for (int Index = 0; Index < 6; ++Index)
    {
        if (MeshBinds[Index].Buffer != VK_NULL_HANDLE && MeshBinds[Index].Buffer != *MeshBinds[Index].Cache)
        {
            WriteStorageDescriptor(Host, Lifecycle.SpawnVisibilitySet, MeshBinds[Index].Binding, MeshBinds[Index].Buffer);
            *MeshBinds[Index].Cache = MeshBinds[Index].Buffer;
        }
    }

    // --- set 1: the borrowed grid + pool surfel buffers (offsets @0, list @1, surfels @2) ---
    if (Slotting.OffsetsBuffer != VK_NULL_HANDLE && Slotting.OffsetsBuffer != Lifecycle.BoundOffsetsBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.SpawnGridSet, 0u, Slotting.OffsetsBuffer);
        Lifecycle.BoundOffsetsBuffer = Slotting.OffsetsBuffer;
    }
    if (Slotting.ListBuffer != VK_NULL_HANDLE && Slotting.ListBuffer != Lifecycle.BoundListBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.SpawnGridSet, 1u, Slotting.ListBuffer);
        Lifecycle.BoundListBuffer = Slotting.ListBuffer;
    }
    if (Pool.SurfelBuffer != VK_NULL_HANDLE && Pool.SurfelBuffer != Lifecycle.BoundSpawnSurfelBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.SpawnGridSet, 2u, Pool.SurfelBuffer);
        Lifecycle.BoundSpawnSurfelBuffer = Pool.SurfelBuffer;
    }

    // --- set 1: the touched income mailbox (b5, economy) — the spawn pass's keep-alive/despawn writes ---
    if (Pool.TouchedBuffer != VK_NULL_HANDLE && Pool.TouchedBuffer != Lifecycle.BoundSpawnTouchedBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.SpawnGridSet, 5u, Pool.TouchedBuffer);
        Lifecycle.BoundSpawnTouchedBuffer = Pool.TouchedBuffer;
    }

    // --- pool set: the borrowed pool buffers (surfels @0, pool @1, poolAlloc @2, poolMax @3, alive @4) ---
    if (Pool.SurfelBuffer != VK_NULL_HANDLE && Pool.SurfelBuffer != Lifecycle.BoundPoolSurfelBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 0u, Pool.SurfelBuffer);
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 1u, Pool.PoolBuffer);
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 2u, Pool.PoolAllocBuffer);
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 3u, Pool.PoolMaxBuffer);
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 4u, Pool.AliveCountBuffer);
        Lifecycle.BoundPoolSurfelBuffer = Pool.SurfelBuffer;
    }

    // --- pool set: the economy-only bindings — the grid Offsets slice (b7, the rent-crowding count) + the touched mailbox (b8, the income drain) ---
    if (Slotting.OffsetsBuffer != VK_NULL_HANDLE && Slotting.OffsetsBuffer != Lifecycle.BoundPoolOffsetsBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 7u, Slotting.OffsetsBuffer);
        Lifecycle.BoundPoolOffsetsBuffer = Slotting.OffsetsBuffer;
    }
    if (Pool.TouchedBuffer != VK_NULL_HANDLE && Pool.TouchedBuffer != Lifecycle.BoundPoolTouchedBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 8u, Pool.TouchedBuffer);
        Lifecycle.BoundPoolTouchedBuffer = Pool.TouchedBuffer;
    }

    // --- pool set: the 🩺 census tally buffer (b9, diagnostic) ---
    // 🔴 Age/Allocate declare this binding UNCONDITIONALLY and index it up to SURFEL_CENSUS_SLOT_COUNT-1, so the bound buffer must be AT LEAST that
    //    many ints. There is deliberately NO alias fallback onto a pool atomic here: every one of those (alive / poolAlloc / poolMax) is a SINGLE int,
    //    so aliasing would turn the tallies into out-of-bounds writes past a 4-byte allocation — silent memory corruption dressed up as a diagnostic.
    //    The census buffer is therefore owned by the trace and allocated unconditionally at init (whether or not a recording is ever started), which
    //    costs 20 bytes and keeps this binding always valid.
    if (CensusCountersBuffer != VK_NULL_HANDLE && CensusCountersBuffer != Lifecycle.BoundPoolCensusBuffer)
    {
        WriteStorageDescriptor(Host, Lifecycle.PoolSet, 9u, CensusCountersBuffer);
        Lifecycle.BoundPoolCensusBuffer = CensusCountersBuffer;
    }
}

void RecordSurfelLifecyclePrepare(SurfelLifecycleSubmission& Lifecycle,
                                  const SurfelPool&           Pool,
                                  VkCommandBuffer             CommandBuffer)
{
    if (!Lifecycle.ReadyCondition || Lifecycle.Prepared)
        return;
    if (!Pool.ReadyCondition || Pool.SurfelBuffer == VK_NULL_HANDLE)
        return;

    PoolPushBlock Push = {};
    Push.Capacity = (int32_t)Pool.Capacity;

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.PoolPipelineLayout, 0, 1, &Lifecycle.PoolSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Lifecycle.PoolPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PoolPushBlock), &Push);
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.PreparePipeline);
    const uint32_t Groups = (Pool.Capacity + SurfelPrepareWorkgroupEdge - 1) / SurfelPrepareWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, Groups, 1, 1);
    // The caller barriers the pool before the first slotting reads it.

    Lifecycle.Prepared = true;
}

void RecordSurfelLifecycleSpawn(SurfelLifecycleSubmission&   Lifecycle,
                                const SurfelPool&             Pool,
                                const SurfelSpawnConstants&   Constants,
                                VkExtent2D                    Extent,
                                VkCommandBuffer               CommandBuffer)
{
    if (!Lifecycle.ReadyCondition || Lifecycle.Host == nullptr)
        return;
    if (!Pool.ReadyCondition || Pool.SurfelBuffer == VK_NULL_HANDLE)
        return;

    const uint32_t TilesX = SurfelSpawnTilesAcross(Extent.width);
    const uint32_t TilesY = SurfelSpawnTilesAcross(Extent.height);
    const uint32_t TileCount = TilesX * TilesY;
    if (TileCount > Lifecycle.TileCapacity)
    {
        ReportLifecycle("visibility extent exceeds the tile buffer capacity — spawn skipped");
        return;
    }

    // --- Stage 1: SurfelSpawnRequest over the visibility tiles (writes the per-tile requests) ---
    SurfelSpawnConstants Push = Constants;
    Push.ScreenAndTiles[0] = (int32_t)Extent.width;
    Push.ScreenAndTiles[1] = (int32_t)Extent.height;
    Push.ScreenAndTiles[2] = (int32_t)TilesX;
    // ScreenAndTiles[3] (frame) supplied by the caller.

    const VkDescriptorSet SpawnSets[2] = { Lifecycle.SpawnVisibilitySet, Lifecycle.SpawnGridSet };
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.SpawnPipelineLayout, 0, 2, SpawnSets, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Lifecycle.SpawnPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelSpawnConstants), &Push);
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.SpawnPipeline);
    vkCmdDispatch(CommandBuffer, TilesX, TilesY, 1);
    LifecycleStorageBarrier(CommandBuffer);   // the requests must be visible to Allocate

    // --- Stage 2: SurfelAllocate over the tiles (pops pool slots, commits surfels) ---
    PoolPushBlock AllocPush = {};
    AllocPush.Capacity  = (int32_t)Pool.Capacity;
    AllocPush.TileCount = (int32_t)TileCount;
    AllocPush.SyncPass  = 0;

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.PoolPipelineLayout, 0, 1, &Lifecycle.PoolSet, 0, nullptr);
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.AllocatePipeline);
    vkCmdPushConstants(CommandBuffer, Lifecycle.PoolPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PoolPushBlock), &AllocPush);
    const uint32_t AllocGroups = (TileCount + SurfelLifecycleWorkgroupEdge - 1) / SurfelLifecycleWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, AllocGroups, 1, 1);
    LifecycleStorageBarrier(CommandBuffer);   // the allocation must be visible to the alive sync

    // --- Stage 3: single-lane alive sync ---
    AllocPush.SyncPass = 1;
    vkCmdPushConstants(CommandBuffer, Lifecycle.PoolPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PoolPushBlock), &AllocPush);
    vkCmdDispatch(CommandBuffer, 1, 1, 1);
    // The caller barriers the pool before a downstream read.
}

void RecordSurfelLifecycleAge(SurfelLifecycleSubmission& Lifecycle,
                              const SurfelPool&           Pool,
                              const float                 GridOrigin[3],
                              const SurfelSpawnConstants& Constants,
                              VkCommandBuffer             CommandBuffer)
{
    if (!Lifecycle.ReadyCondition)
        return;
    if (!Pool.ReadyCondition || Pool.SurfelBuffer == VK_NULL_HANDLE)
        return;

    PoolPushBlock Push = {};
    Push.Capacity = (int32_t)Pool.Capacity;
    // The snapped grid origin the slotting/spawn used this frame — hashes each surfel's cell for the crowding-rent count.
    Push.GridOrigin[0] = GridOrigin[0];
    Push.GridOrigin[1] = GridOrigin[1];
    Push.GridOrigin[2] = GridOrigin[2];
    Push.GridOrigin[3] = 0.0f;
    // The live world scale — see the header note. These MUST be the frame's spawn/slotting values or this pass hashes onto a different lattice than the
    // Offsets/List arrays it reads, which is a wrong-cell lookup with an entirely plausible-looking result.
    Push.CameraPosition[0] = Constants.CameraPosition[0];
    Push.CameraPosition[1] = Constants.CameraPosition[1];
    Push.CameraPosition[2] = Constants.CameraPosition[2];
    Push.CameraPosition[3] = 0.0f;
    Push.TuneCellDiameter  = Constants.TuneCellDiameter;
    Push.TuneBaseRadius    = Constants.TuneBaseRadius;

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.PoolPipelineLayout, 0, 1, &Lifecycle.PoolSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Lifecycle.PoolPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PoolPushBlock), &Push);
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Lifecycle.AgePipeline);
    const uint32_t Groups = (Pool.Capacity + SurfelLifecycleWorkgroupEdge - 1) / SurfelLifecycleWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, Groups, 1, 1);
    // The caller barriers the pool before a downstream read.
}

void FinalizeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Lifecycle)
{
    if (Lifecycle.Host == nullptr || Lifecycle.Host->Device == VK_NULL_HANDLE)
    {
        Lifecycle = SurfelLifecycleSubmission{};
        return;
    }
    VkDevice Device = Lifecycle.Host->Device;
    const VkAllocationCallbacks* Allocator = Lifecycle.Host->Allocator;

    if (Lifecycle.SpawnPipeline    != VK_NULL_HANDLE) vkDestroyPipeline(Device, Lifecycle.SpawnPipeline, Allocator);
    if (Lifecycle.PreparePipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Device, Lifecycle.PreparePipeline, Allocator);
    if (Lifecycle.AgePipeline      != VK_NULL_HANDLE) vkDestroyPipeline(Device, Lifecycle.AgePipeline, Allocator);
    if (Lifecycle.AllocatePipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Lifecycle.AllocatePipeline, Allocator);

    if (Lifecycle.SpawnPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Lifecycle.SpawnPipelineLayout, Allocator);
    if (Lifecycle.PoolPipelineLayout  != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Lifecycle.PoolPipelineLayout, Allocator);

    if (Lifecycle.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Lifecycle.DescriptorPool, Allocator);   // frees all three sets

    if (Lifecycle.SpawnVisibilityLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Lifecycle.SpawnVisibilityLayout, Allocator);
    if (Lifecycle.SpawnGridLayout       != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Lifecycle.SpawnGridLayout, Allocator);
    if (Lifecycle.PoolLayout            != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Lifecycle.PoolLayout, Allocator);

    if (Lifecycle.PointSampler != VK_NULL_HANDLE) vkDestroySampler(Device, Lifecycle.PointSampler, Allocator);

    if (Lifecycle.TileAllocBuffer     != VK_NULL_HANDLE) vkDestroyBuffer(Device, Lifecycle.TileAllocBuffer, Allocator);
    if (Lifecycle.TileAllocMemory     != VK_NULL_HANDLE) vkFreeMemory(Device, Lifecycle.TileAllocMemory, Allocator);
    if (Lifecycle.TileCandidateBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Lifecycle.TileCandidateBuffer, Allocator);
    if (Lifecycle.TileCandidateMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Lifecycle.TileCandidateMemory, Allocator);

    Lifecycle = SurfelLifecycleSubmission{};
}

} // namespace Frontier
