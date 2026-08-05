/*==============================================================================================================================================
                                                      SURFELLIFECYCLESUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the four-dispatch per-frame lifecycle. Initialize builds one twelve-binding set layout (the ordinals mirror
//    Shaders/SurfelStoreBindings.glsl), a pipeline layout carrying the 48-byte push range, one descriptor set, and the four compute pipelines.
//    Record binds the set and pushes the constants once, then runs reset -> copy -> census -> barrier -> scan -> barrier -> scatter. Raw Vulkan,
//    no VMA, mirroring the InstanceCullSubmission / SurfelStore idiom.
//
// 🔴 THE DISPATCHES ARE SIZED FROM CONSTANTS, NEVER FROM A DEVICE COUNTER. Every count that matters this frame — pending, live, occupied cells —
//    lives on the GPU and is not knowable here without a stall, so the census and the scatter are dispatched over the FULL capacity and early-out
//    against the counter they read themselves. Upstream does the same. 💡 That is not waste: 1024 groups of 64 that return on their first
//    instruction cost less than the readback needed to size them exactly, and it means the dispatch can never disagree with the counter.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelLifecycleSubmission.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Must match `layout(local_size_x = 64)` in the census, the offset scan and the scatter. The counter reset is a single invocation and is
//    dispatched as one group regardless.
constexpr uint32_t LifecycleGroupWidth = 64u;

// 🔴 THE BINDING ORDINALS OF SET 0, IN THE ORDER Shaders/SurfelStoreBindings.glsl DECLARES THEM. This table and that file are one contract: a
//    mismatch does NOT fail validation, because both sides declare a storage buffer at every index — it hands a pass the wrong buffer, and a pass
//    reading cell spans as surfel records produces a plausible-looking field of nonsense. ⚠️ Adding a binding means editing both in one edit.
enum LifecycleBinding : uint32_t
{
    BindingSurfelRecords  =  0u,
    BindingLiveIndex      =  1u,
    BindingPendingIndex   =  2u,
    BindingVacancyTable   =  3u,
    BindingCellSpan       =  4u,
    BindingCellList       =  5u,
    BindingReservation    =  6u,
    BindingReferenceTally =  7u,
    BindingRecycleRecords =  8u,
    BindingRayOutcomes    =  9u,
    BindingCounter        = 10u,
    BindingHitLocator     = 11u,
};

constexpr uint32_t LifecycleBindingCount = 12u;

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ReportLifecycle(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelLifecycleSubmission] %s\n", MessageText);
}

std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
        return Bytes;
    std::fseek(Handle, 0, SEEK_END);
    const long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Bytes.resize((size_t)Size);
        if (std::fread(Bytes.data(), 1, (size_t)Size, Handle) != (size_t)Size)
            Bytes.clear();
    }
    std::fclose(Handle);
    return Bytes;
}

VkPipeline ConstructComputePipeline(VulkanHost& Host, VkPipelineLayout Layout, const std::string& Directory, const char* FileName)
{
    const std::vector<char> Bytes = RetrieveShaderBytes(Directory + "/" + FileName);
    if (Bytes.empty() || (Bytes.size() % 4) != 0)
    {
        ReportLifecycle("shader module unavailable or not word-aligned");
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ModuleInformation.codeSize = Bytes.size();
    ModuleInformation.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());
    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInformation, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;

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

// Build set 0 — twelve compute storage buffers at the ordinals above — plus the pipeline layout carrying the push range.
bool ConstructLifecycleLayout(SurfelLifecycleSubmission& Submission)
{
    VulkanHost& Host = *Submission.Host;

    VkDescriptorSetLayoutBinding Bindings[LifecycleBindingCount] = {};
    for (uint32_t Index = 0; Index < LifecycleBindingCount; ++Index)
    {
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = LifecycleBindingCount;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Submission.SetLayout) != VK_SUCCESS)
    {
        Submission.SetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SurfelLifecycleConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Submission.SetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Submission.PipelineLayout) != VK_SUCCESS)
    {
        Submission.PipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool ConstructLifecycleDescriptors(SurfelLifecycleSubmission& Submission)
{
    VulkanHost& Host = *Submission.Host;

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = LifecycleBindingCount;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Submission.DescriptorPool) != VK_SUCCESS)
    {
        Submission.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Submission.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Submission.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Submission.StoreSet) != VK_SUCCESS)
    {
        Submission.StoreSet = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Point all twelve bindings at the store's buffers. 🔴 The order of this array IS the binding order — index i is written to binding i — so it must
// stay in step with the LifecycleBinding table and with SurfelStoreBindings.glsl. Every descriptor is written in one call: a set with even one
// binding left unwritten is undefined to dispatch against, and the census touches nine of the twelve on its own.
bool WriteLifecycleDescriptors(SurfelLifecycleSubmission& Submission, const SurfelStore& Store)
{
    const VkBuffer OrderedBuffers[LifecycleBindingCount] =
    {
        Store.SurfelRecords.Buffer,      // 0
        Store.LiveIndex.Buffer,          // 1
        Store.PendingIndex.Buffer,       // 2
        Store.VacancyTable.Buffer,       // 3
        Store.CellSpan.Buffer,           // 4
        Store.CellList.Buffer,           // 5
        Store.Reservation.Buffer,        // 6
        Store.ReferenceTally.Buffer,     // 7
        Store.RecycleRecords.Buffer,     // 8
        Store.RayOutcomes.Buffer,        // 9
        Store.Counter.Buffer,            // 10
        Store.HitLocator.Buffer,         // 11
    };

    VkDescriptorBufferInfo BufferSpans[LifecycleBindingCount] = {};
    VkWriteDescriptorSet   Writes[LifecycleBindingCount]      = {};
    for (uint32_t Index = 0; Index < LifecycleBindingCount; ++Index)
    {
        if (OrderedBuffers[Index] == VK_NULL_HANDLE)
        {
            ReportLifecycle("store handed a null buffer; descriptors not written");
            return false;
        }

        BufferSpans[Index].buffer = OrderedBuffers[Index];
        BufferSpans[Index].offset = 0;
        BufferSpans[Index].range  = VK_WHOLE_SIZE;

        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Submission.StoreSet;
        Writes[Index].dstBinding      = Index;
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = &BufferSpans[Index];
    }

    vkUpdateDescriptorSets(Submission.Host->Device, LifecycleBindingCount, Writes, 0, nullptr);
    Submission.BoundRecordBuffer = Store.SurfelRecords.Buffer;
    return true;
}

// A compute->compute all-storage barrier: the previous dispatch's writes are visible to the next dispatch's reads AND writes. The next stage
// re-modifies the same fields (the cell span's count, the counters), so the destination mask must carry WRITE as well as READ.
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

// The transfer half of the Live -> Pending handover: the shader wrote the counter, this moves the list, and the two must be ordered against the
// dispatches on either side. Compute-write -> transfer-read on the source, then transfer-write -> compute-read on the destination.
void LifecycleTransferBarrier(VkCommandBuffer     CommandBuffer,
                              VkPipelineStageFlags SourceStage,
                              VkAccessFlags        SourceAccess,
                              VkPipelineStageFlags DestinationStage,
                              VkAccessFlags        DestinationAccess)
{
    VkMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    Barrier.srcAccessMask = SourceAccess;
    Barrier.dstAccessMask = DestinationAccess;
    vkCmdPipelineBarrier(CommandBuffer, SourceStage, DestinationStage, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Submission,
                                         VulkanHost&                Host,
                                         const SurfelStore&         Store,
                                         const char*                ShaderDirectory)
{
    Submission = SurfelLifecycleSubmission{};
    Submission.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportLifecycle("no device — lifecycle not built");
        Submission = SurfelLifecycleSubmission{};
        return false;
    }
    if (!Store.ReadyCondition)
    {
        ReportLifecycle("store not ready — lifecycle not built");
        Submission = SurfelLifecycleSubmission{};
        return false;
    }
    if (ShaderDirectory == nullptr)
    {
        ReportLifecycle("no shader directory — lifecycle not built");
        Submission = SurfelLifecycleSubmission{};
        return false;
    }

    if (!ConstructLifecycleLayout(Submission))
    {
        ReportLifecycle("set / pipeline layout creation failed");
        FinalizeSurfelLifecycleSubmission(Submission);
        return false;
    }
    if (!ConstructLifecycleDescriptors(Submission))
    {
        ReportLifecycle("descriptor pool / set creation failed");
        FinalizeSurfelLifecycleSubmission(Submission);
        return false;
    }
    if (!WriteLifecycleDescriptors(Submission, Store))
    {
        ReportLifecycle("descriptor write failed");
        FinalizeSurfelLifecycleSubmission(Submission);
        return false;
    }

    const std::string Directory = ShaderDirectory;
    Submission.CounterResetPipeline   = ConstructComputePipeline(Host, Submission.PipelineLayout, Directory, "SurfelCounterReset.comp.spv");
    Submission.CellCensusPipeline     = ConstructComputePipeline(Host, Submission.PipelineLayout, Directory, "SurfelCellCensus.comp.spv");
    Submission.CellOffsetScanPipeline = ConstructComputePipeline(Host, Submission.PipelineLayout, Directory, "SurfelCellOffsetScan.comp.spv");
    Submission.CellScatterPipeline    = ConstructComputePipeline(Host, Submission.PipelineLayout, Directory, "SurfelCellScatter.comp.spv");
    if (Submission.CounterResetPipeline   == VK_NULL_HANDLE || Submission.CellCensusPipeline  == VK_NULL_HANDLE ||
        Submission.CellOffsetScanPipeline == VK_NULL_HANDLE || Submission.CellScatterPipeline == VK_NULL_HANDLE)
    {
        ReportLifecycle("one or more lifecycle pipelines failed to build");
        FinalizeSurfelLifecycleSubmission(Submission);
        return false;
    }

    Submission.Capacity  = Store.Capacity;
    Submission.CellCount = Store.CellCount;
    Submission.ReadyCondition = true;
    return true;
}

void RecordSurfelLifecycle(SurfelLifecycleSubmission&      Submission,
                           const SurfelStore&              Store,
                           const SurfelLifecycleConstants& Constants,
                           VkCommandBuffer                 CommandBuffer)
{
    if (!Submission.ReadyCondition || Submission.Host == nullptr || CommandBuffer == VK_NULL_HANDLE)
        return;
    if (!Store.ReadyCondition)
        return;

    // A rebuilt store hands out fresh handles; re-point the whole set rather than dispatching against freed memory. In the steady state this never
    // fires, because the store allocates once and stays resident.
    if (Store.SurfelRecords.Buffer != Submission.BoundRecordBuffer)
    {
        if (!WriteLifecycleDescriptors(Submission, Store))
            return;
        Submission.Capacity  = Store.Capacity;
        Submission.CellCount = Store.CellCount;
    }

    // 🔴 CellCount is taken from the STORE, not from the caller's constants. CellSpan and Reservation were sized against the store's cell count, and
    //    an offset scan dispatched past that count writes past both buffers. The caller's field is honoured only in that it is overwritten with the
    //    truth before being pushed.
    SurfelLifecycleConstants Push = Constants;
    Push.CellCount = Submission.CellCount;

    //-- Clear the cell spans BEFORE the census ---------------------------------------------------------------------------
    // 🔴 A DELIBERATE DEVIATION FROM UPSTREAM, AND IT FIXES A DEFECT RATHER THAN AVOIDING WORK. Upstream never clears CellInfo: the offset scan zeroes
    //    surfelCount mid-frame, and the scatter then re-increments it back to the census's count as its write cursor. So the count is NON-ZERO when the
    //    NEXT frame's census starts adding to it, and that frame's scan banks the leftover PLUS the new count. The Phase-5 gate measured it exactly —
    //    154 pairs claimed on frame 1, then 293 (= 154 leftover + 139 new), settling at a steady 2x:
    //       frame 1: leftover 0   + census 154 -> claimed 154, written 154
    //       frame 2: leftover 154 + census 139 -> claimed 293, written 139
    //       frame 3: leftover 139 + census 139 -> claimed 278, written 139
    //    ⚠️ WHY IT LOOKS HARMLESS AND IS NOT. It converges at 2x instead of diverging, because the scatter always re-establishes exactly the census's
    //    count — and consumers read TableOffset..+SurfelCount, which stays correct. So nothing renders wrong. What it costs is HALF THE CELL TABLE: every
    //    cell's claimed run is twice the length it uses, so the scan's overflow guard trips at half the population the buffer was sized for, and the
    //    surfels it parks as empty simply stop lighting. That failure arrives only under load, which is where it is hardest to attribute.
    // 📝 A fill, not a fifth compute dispatch: the whole buffer goes to zero, the driver's fill beats a 32768-group clear that writes one uint each, and
    //    zeroing TableOffset too is free — the scan rewrites it for every occupied cell and an empty cell's offset is never read.
    vkCmdFillBuffer(CommandBuffer, Store.CellSpan.Buffer, 0, Store.CellSpan.ByteLength, 0u);

    // The census reads the spans it just zeroed through a storage binding, so the fill must be visible to a shader read before pass 2 runs. Recorded here
    // rather than after the census's own barrier because the fill is a TRANSFER write and the barrier below covers only compute.
    LifecycleTransferBarrier(CommandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,       VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    // Bound and pushed ONCE. All four pipelines were compiled against this one layout (they all include SurfelStoreBindings.glsl), so nothing between
    // the dispatches below disturbs either — no other unit's pipeline layout is bound inside this sequence.
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.PipelineLayout, 0, 1, &Submission.StoreSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Submission.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelLifecycleConstants), &Push);

    //-- Pass 1 — roll the counters over -----------------------------------------------------------------------------------
    // One invocation: last frame's ValidSurfel becomes this frame's DirtySurfel, ValidSurfel restarts at zero, the per-frame accumulators clear.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.CounterResetPipeline);
    vkCmdDispatch(CommandBuffer, 1, 1, 1);

    //-- The Live -> Pending list handover ---------------------------------------------------------------------------------
    // 🔴 THE DIRECTION IS THE WHOLE MECHANISM, AND IT IS SRC=LiveIndex, DST=PendingIndex (upstream SurfelGI.cpp:100, a copyResource straight after
    //    the prepare dispatch). Pass 1 moved the COUNT; this moves the LIST. The census then reads Pending — last frame's survivors — and rebuilds
    //    Live from whichever of them are still worth keeping. ⚠️ Copy it the other way and the census re-processes the set it is writing: surfels are
    //    counted twice, dropped, or read half-written, and the population wanders instead of settling. Nothing reports it.
    // 📝 The counter reset does not touch either list, so the barrier before the copy is not strictly owed for THIS copy — it is owed because the
    //    previous frame's census wrote LiveIndex through the shader and this reads it as a transfer. Cheap, and it removes a cross-frame hazard that
    //    only shows up under load.
    LifecycleTransferBarrier(CommandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,       VK_ACCESS_TRANSFER_READ_BIT);

    VkBufferCopy ListSpan = {};
    ListSpan.srcOffset = 0;
    ListSpan.dstOffset = 0;
    // Both lists are SurfelTotalLimit uints and were allocated by the same call, so either length is the same number; taking the smaller keeps this
    // correct if a future store ever sizes them apart.
    ListSpan.size = Store.LiveIndex.ByteLength < Store.PendingIndex.ByteLength ? Store.LiveIndex.ByteLength : Store.PendingIndex.ByteLength;
    vkCmdCopyBuffer(CommandBuffer, Store.LiveIndex.Buffer, Store.PendingIndex.Buffer, 1, &ListSpan);

    // The census reads PendingIndex through a storage binding, so the copy's write must be visible to a shader read — and it reads the counter pass 1
    // wrote, which this same barrier covers.
    LifecycleTransferBarrier(CommandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT   | VK_ACCESS_SHADER_WRITE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    //-- Pass 2 — census the pending set ----------------------------------------------------------------------------------
    // Dispatched over the full capacity and early-outs against DirtySurfel, because that count lives on the device (see the file header).
    const uint32_t SurfelGroups = (Submission.Capacity + LifecycleGroupWidth - 1u) / LifecycleGroupWidth;
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.CellCensusPipeline);
    vkCmdDispatch(CommandBuffer, SurfelGroups, 1, 1);

    // 🔴 MANDATORY. The offset scan reads the counts this pass is still writing; without this, cells claim runs too short for what the scatter writes
    //    and entries land inside the neighbouring cell's run.
    LifecycleStorageBarrier(CommandBuffer);

    //-- Pass 3 — turn counts into base offsets ---------------------------------------------------------------------------
    // One invocation per cell. Claims each occupied cell's run atomically, then zeroes the count for the scatter to use as a write cursor.
    const uint32_t CellGroups = (Submission.CellCount + LifecycleGroupWidth - 1u) / LifecycleGroupWidth;
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.CellOffsetScanPipeline);
    vkCmdDispatch(CommandBuffer, CellGroups, 1, 1);

    // 🔴 MANDATORY, AND FOR A DIFFERENT REASON THAN THE ONE ABOVE. The scatter's cursor must start from the ZERO this pass wrote, not from the
    //    census's count. Without this barrier every cell's entries land one full run too far along the table — silent, and it looks like mildly
    //    wrong lighting.
    LifecycleStorageBarrier(CommandBuffer);

    //-- Pass 4 — write the cell-list entries -----------------------------------------------------------------------------
    // One invocation per live surfel, walking the same 125-cell neighbourhood the census walked.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.CellScatterPipeline);
    vkCmdDispatch(CommandBuffer, SurfelGroups, 1, 1);

    // ⚠️ No trailing barrier. The trace, the integrate and the shade read what this wrote, and the fence that makes it visible belongs to whichever
    //    of them reads it next — the consumer knows its own destination stage, this unit does not.
}

void FinalizeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Submission)
{
    if (Submission.Host == nullptr || Submission.Host->Device == VK_NULL_HANDLE)
    {
        Submission = SurfelLifecycleSubmission{};
        return;
    }

    VkDevice                     Device    = Submission.Host->Device;
    const VkAllocationCallbacks* Allocator = Submission.Host->Allocator;

    if (Submission.CounterResetPipeline   != VK_NULL_HANDLE) vkDestroyPipeline(Device, Submission.CounterResetPipeline, Allocator);
    if (Submission.CellCensusPipeline     != VK_NULL_HANDLE) vkDestroyPipeline(Device, Submission.CellCensusPipeline, Allocator);
    if (Submission.CellOffsetScanPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Submission.CellOffsetScanPipeline, Allocator);
    if (Submission.CellScatterPipeline    != VK_NULL_HANDLE) vkDestroyPipeline(Device, Submission.CellScatterPipeline, Allocator);

    if (Submission.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Submission.PipelineLayout, Allocator);
    if (Submission.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Submission.DescriptorPool, Allocator);   // frees StoreSet
    if (Submission.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Submission.SetLayout, Allocator);

    Submission = SurfelLifecycleSubmission{};
}

} // namespace Frontier
