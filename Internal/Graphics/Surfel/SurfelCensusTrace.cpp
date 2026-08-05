/*==============================================================================================================================================
                                                         SURFELCENSUSTRACE.CPP
==============================================================================================================================================*/
// 🧩 See SurfelCensusTrace.h for what this measures and why the flows have to be counted on the GPU rather than differenced on the host.

#include "Graphics/Surfel/SurfelCensusTrace.h"
#include "Graphics/Surfel/SurfelPool.h"
#include "Graphics/RenderExtension/SurfelTuningWindow.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace Frontier
{

namespace
{

void ReportCensus(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelCensus] %s\n", MessageText);
}

// 📝 The staging slot holds the SurfelCensusSlotCount counters followed by the 3 pool atomics (alive, poolAlloc, poolMax). Two separate copy regions
//    land in one buffer, so the offsets are fixed here rather than spelled at each copy site — and every one derives from SurfelCensusSlotCount, so
//    adding a counter slot re-sizes the buffer and re-bases the atomics automatically.
constexpr uint32_t CensusStagingIntCount = SurfelCensusSlotCount + 3;
constexpr VkDeviceSize CensusCountersBytes = (VkDeviceSize)SurfelCensusSlotCount * sizeof(int32_t);
constexpr VkDeviceSize CensusStagingBytes  = (VkDeviceSize)CensusStagingIntCount * sizeof(int32_t);
constexpr VkDeviceSize CensusAliveOffset   = CensusCountersBytes;                       // + 0 ints
constexpr VkDeviceSize CensusAllocOffset   = CensusCountersBytes + sizeof(int32_t);     // + 1 int
constexpr VkDeviceSize CensusMaxOffset     = CensusCountersBytes + 2 * sizeof(int32_t); // + 2 ints

// First memory type allowed by the requirement bitmask carrying every required property bit — mirrors the SurfelPool helper.
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

bool AllocateCensusBuffer(VulkanHost&           Host,
                          VkDeviceSize          ByteSize,
                          VkBufferUsageFlags    Usage,
                          VkMemoryPropertyFlags Properties,
                          VkBuffer&             OutBuffer,
                          VkDeviceMemory&       OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (ByteSize == 0) ByteSize = 4;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = Usage;
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
                                                           Properties, MemoryTypeFound);
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

} // namespace

SurfelCensusRunLabel ComposeSurfelCensusRunLabel(const SurfelTuningState& Tuning)
{
    SurfelCensusRunLabel Label;
    Label.CellDiameter    = Tuning.CellDiameter;
    Label.BaseRadius      = Tuning.BaseRadius;
    Label.PerCellCap      = Tuning.PerCellCapApplied;
    Label.NearFieldBias   = Tuning.NearFieldBias;
    return Label;
}

bool InitializeSurfelCensusTrace(SurfelCensusTrace& Trace, VulkanHost& Host)
{
    Trace = SurfelCensusTrace{};
    Trace.Host = &Host;
    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportCensus("no device — census trace disabled");
        return false;
    }

    // The counters buffer is a storage buffer the shaders atomicAdd into, cleared by a fill and read by a copy.
    if (!AllocateCensusBuffer(Host, CensusCountersBytes,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Trace.CountersBuffer, Trace.CountersMemory))
    {
        ReportCensus("counters buffer allocation failed — census trace disabled");
        FinalizeSurfelCensusTrace(Trace);
        return false;
    }

    // 🔴 HOST_COHERENT as well as HOST_VISIBLE, deliberately: the read path never calls vkInvalidateMappedMemoryRanges, so without coherence the
    //    host could read a stale cache line and the CSV would silently repeat the previous frame's counts.
    for (uint32_t Ring = 0; Ring < SurfelCensusRingDepth; ++Ring)
    {
        if (!AllocateCensusBuffer(Host, CensusStagingBytes,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  Trace.StagingBuffer[Ring], Trace.StagingMemory[Ring]) ||
            vkMapMemory(Host.Device, Trace.StagingMemory[Ring], 0, CensusStagingBytes, 0, &Trace.StagingMapped[Ring]) != VK_SUCCESS)
        {
            ReportCensus("staging ring allocation/map failed — census trace disabled");
            FinalizeSurfelCensusTrace(Trace);
            return false;
        }
        std::memset(Trace.StagingMapped[Ring], 0, (size_t)CensusStagingBytes);
    }

    Trace.ReadyCondition = true;
    return true;
}

void BeginSurfelCensusFrame(SurfelCensusTrace& Trace, VkCommandBuffer CommandBuffer)
{
    if (!Trace.ReadyCondition || CommandBuffer == VK_NULL_HANDLE)
        return;

    vkCmdFillBuffer(CommandBuffer, Trace.CountersBuffer, 0, CensusCountersBytes, 0u);

    // 🔴 The fill must be visible to the first dispatch that tallies, or that dispatch's atomicAdds race the clear and the row reads low.
    VkBufferMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    Barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    Barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.buffer              = Trace.CountersBuffer;
    Barrier.offset              = 0;
    Barrier.size                = CensusCountersBytes;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &Barrier, 0, nullptr);
}

void RecordSurfelCensusCopy(SurfelCensusTrace& Trace,
                            const SurfelPool&  Pool,
                            VkCommandBuffer    CommandBuffer,
                            uint32_t           FrameIndex)
{
    if (!Trace.ReadyCondition || !Trace.Recording || CommandBuffer == VK_NULL_HANDLE)
        return;
    if (!Pool.ReadyCondition)
        return;

    const uint32_t Ring = Trace.RecordRing;

    // 🔴 The tallying dispatches wrote the counters and the pool atomics; make those writes visible to the transfer below. Without this the copy
    //    can read the counters before the Age dispatch's atomicAdds land, which shows up as a row of plausible-but-low flows — the worst kind of
    //    wrong, because it looks like a measurement rather than a race.
    VkBufferMemoryBarrier Barriers[4] = {};
    const VkBuffer Sources[4] = { Trace.CountersBuffer, Pool.AliveCountBuffer, Pool.PoolAllocBuffer, Pool.PoolMaxBuffer };
    uint32_t BarrierCount = 0;
    for (uint32_t Index = 0; Index < 4; ++Index)
    {
        if (Sources[Index] == VK_NULL_HANDLE) continue;
        Barriers[BarrierCount] = VkBufferMemoryBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        Barriers[BarrierCount].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        Barriers[BarrierCount].dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        Barriers[BarrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barriers[BarrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barriers[BarrierCount].buffer              = Sources[Index];
        Barriers[BarrierCount].offset              = 0;
        Barriers[BarrierCount].size                = VK_WHOLE_SIZE;
        BarrierCount++;
    }
    if (BarrierCount > 0)
        vkCmdPipelineBarrier(CommandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, BarrierCount, Barriers, 0, nullptr);

    VkBufferCopy CountersRegion = { 0, 0, CensusCountersBytes };
    vkCmdCopyBuffer(CommandBuffer, Trace.CountersBuffer, Trace.StagingBuffer[Ring], 1, &CountersRegion);

    if (Pool.AliveCountBuffer != VK_NULL_HANDLE)
    {
        VkBufferCopy Region = { 0, CensusAliveOffset, sizeof(int32_t) };
        vkCmdCopyBuffer(CommandBuffer, Pool.AliveCountBuffer, Trace.StagingBuffer[Ring], 1, &Region);
    }
    if (Pool.PoolAllocBuffer != VK_NULL_HANDLE)
    {
        VkBufferCopy Region = { 0, CensusAllocOffset, sizeof(int32_t) };
        vkCmdCopyBuffer(CommandBuffer, Pool.PoolAllocBuffer, Trace.StagingBuffer[Ring], 1, &Region);
    }
    if (Pool.PoolMaxBuffer != VK_NULL_HANDLE)
    {
        VkBufferCopy Region = { 0, CensusMaxOffset, sizeof(int32_t) };
        vkCmdCopyBuffer(CommandBuffer, Pool.PoolMaxBuffer, Trace.StagingBuffer[Ring], 1, &Region);
    }

    Trace.StagingFrame[Ring] = FrameIndex;
    Trace.RingPrimed[Ring]   = true;
    Trace.RecordRing         = (Ring + 1) % SurfelCensusRingDepth;
}

void SupplySurfelCensusTimings(SurfelCensusTrace& Trace, const float* Millis, uint32_t MillisCount)
{
    if (Millis == nullptr)
        return;
    const uint32_t Count = (MillisCount < SurfelCensusTimingSlotCount) ? MillisCount : SurfelCensusTimingSlotCount;
    for (uint32_t Index = 0; Index < Count; ++Index)
        Trace.PassMillis[Index] = Millis[Index];
}

void CollectSurfelCensusRow(SurfelCensusTrace& Trace)
{
    if (!Trace.ReadyCondition || !Trace.Recording || Trace.OutputFile == nullptr)
        return;

    const uint32_t Ring = Trace.CollectRing;
    if (!Trace.RingPrimed[Ring] || Trace.StagingMapped[Ring] == nullptr)
        return;   // never written yet — no sample, not a fault

    // 🔴 Reading the slot that TRAILS RecordRing by the full ring depth is what makes this non-blocking: RecordRing has already advanced past the
    //    frame just recorded, so this slot is at least SurfelCensusRingDepth-1 frames old and the GPU has certainly retired its copy.
    int32_t Values[CensusStagingIntCount] = {};
    std::memcpy(Values, Trace.StagingMapped[Ring], (size_t)CensusStagingBytes);

    SurfelCensusSample Sample = {};
    Sample.FrameIndex  = Trace.StagingFrame[Ring];
    Sample.Spawned     = Values[SurfelCensusSpawned];
    Sample.SpawnFailed = Values[SurfelCensusSpawnFailed];
    Sample.DiedTtl     = Values[SurfelCensusDiedTtl];
    Sample.DiedPolice  = Values[SurfelCensusDiedPolice];
    Sample.KeepAlive   = Values[SurfelCensusKeepAlive];
    Sample.AliveCount  = Values[SurfelCensusSlotCount + 0];
    Sample.PoolAlloc   = Values[SurfelCensusSlotCount + 1];
    Sample.PoolMax     = Values[SurfelCensusSlotCount + 2];

    // 📝 The derived columns are what the question actually needs. Churn = births + deaths (the gross flow a snapshot hides); NetChange = births -
    //    deaths (what differencing AliveCount would have shown). A settled field has churn ~0; a field in violent equilibrium has large churn and
    //    NetChange ~0 — the two cases this trace exists to tell apart.
    // 🔴 EVERY DEATH CAUSE MUST BE IN THIS SUM. Deaths feeds churn, netChange AND turnoverPercent, so a cause left out does not merely miss a column —
    //    it makes the field look calmer than it is on exactly the runs that added the new cause. Adding a death path without extending this sum reads
    //    as "the new cause is free" rather than "the new cause is uncounted".
    const int32_t Deaths     = Sample.DiedTtl + Sample.DiedPolice;
    const int32_t Churn      = Sample.Spawned + Deaths;
    const int32_t NetChange  = Sample.Spawned - Deaths;
    const double  TurnoverPercent = (Sample.AliveCount > 0) ? (100.0 * (double)Deaths / (double)Sample.AliveCount) : 0.0;

    // 📝 The surfel pipeline's own GPU cost, excluding the debug splat (a diagnostic overlay, not part of the technique). This is the number an A/B
    //    between spawn front-ends is actually about.
    const double SurfelMillis = (double)Trace.PassMillis[0] + (double)Trace.PassMillis[1] + (double)Trace.PassMillis[2]
                              + (double)Trace.PassMillis[3] + (double)Trace.PassMillis[4];

    // 🔴 COST PER SURFEL IS THE ONLY HONEST WAY TO COMPARE TWO RUNS THAT PLACED DIFFERENT POPULATIONS. A configuration that places half as many probes
    //    shows a lower total ms while looking worse; dividing by the live count is what separates "cheaper" from "doing less". Zero when the field is
    //    empty rather than a divide-by-zero infinity.
    const double MicrosPerSurfel = (Sample.AliveCount > 0) ? (SurfelMillis * 1000.0 / (double)Sample.AliveCount) : 0.0;

    std::fprintf((FILE*)Trace.OutputFile,
                 "%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,"
                 "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                 Sample.FrameIndex, Sample.AliveCount, Sample.PoolAlloc, Sample.PoolMax,
                 Sample.Spawned, Sample.SpawnFailed, Sample.DiedTtl, Sample.DiedPolice, Sample.KeepAlive,
                 Deaths, Churn, NetChange, TurnoverPercent,
                 Trace.PassMillis[0], Trace.PassMillis[1], Trace.PassMillis[2], Trace.PassMillis[3],
                 Trace.PassMillis[4], Trace.PassMillis[5], SurfelMillis, MicrosPerSurfel);
    Trace.RowsWritten++;

    Trace.CollectRing = (Ring + 1) % SurfelCensusRingDepth;
}

bool BeginSurfelCensusRecording(SurfelCensusTrace& Trace, const char* Directory, const SurfelCensusRunLabel* Label)
{
    if (!Trace.ReadyCondition || Trace.Recording)
        return false;

    const char* Folder = (Directory && Directory[0]) ? Directory : "SurfelDumps";
    {
        std::error_code DirError;
        std::filesystem::create_directories(Folder, DirError);
    }

    char Suffix[16];
    std::snprintf(Suffix, sizeof(Suffix), "-%04u", Trace.TraceSequence);
    const std::string Path = std::string(Folder) + "/surfel-census" + Suffix + ".csv";

    FILE* File = std::fopen(Path.c_str(), "wb");
    if (File == nullptr)
    {
        ReportCensus("could not open surfel-census csv for writing");
        return false;
    }

    std::fprintf(File, "# surfel-census  per-frame population time series (levels + GPU-counted flows)\n");
    std::fprintf(File, "# churn = spawned + deaths (gross); netChange = spawned - deaths (what differencing alive would show)\n");
    std::fprintf(File, "# turnoverPercent = deaths / alive * 100 — the per-frame fraction of the field replaced\n");
    std::fprintf(File, "# ms* = per-pass GPU milliseconds, read one frame late; surfelMs = slotting+spawn+age+integrate+shade (splat excluded, it is a diagnostic)\n");
    std::fprintf(File, "# usPerSurfel = surfelMs * 1000 / alive — the per-surfel cost, which is what survives a change in population size\n");

    // 🔴 STAMP THE SETTINGS OR THE FILE IS UNIDENTIFIABLE. See SurfelCensusRunLabel — two runs otherwise differ only by sequence number, and getting
    //    them the wrong way round inverts the conclusion while looking entirely reasonable.
    if (Label != nullptr)
    {
        std::fprintf(File, "# run: cellDiameter=%.3f baseRadius=%.3f perCellCap=%d nearFieldBias=%.2f\n",
                     Label->CellDiameter, Label->BaseRadius, Label->PerCellCap, Label->NearFieldBias);
    }
    else
    {
        std::fprintf(File, "# run: settings NOT recorded (no label supplied) — do not use this file for an A/B comparison\n");
    }

    std::fprintf(File, "frame,alive,poolAlloc,poolMax,spawned,spawnFailed,diedTtl,diedPolice,keepAlive,deaths,churn,netChange,turnoverPercent,"
                       "msSlotting,msSpawn,msAge,msIntegrate,msShade,msDebugSplat,surfelMs,usPerSurfel\n");
    std::fflush(File);

    Trace.OutputFile  = File;
    Trace.OutputPath  = Path;
    Trace.RowsWritten = 0;
    Trace.Recording   = true;
    Trace.TraceSequence++;

    // 🔴 Drop every primed ring slot. Slots still hold copies from BEFORE this trace opened (or from a previous trace), and replaying them would
    //    put pre-trace frames at the top of a fresh CSV.
    for (uint32_t Ring = 0; Ring < SurfelCensusRingDepth; ++Ring)
        Trace.RingPrimed[Ring] = false;
    Trace.CollectRing = Trace.RecordRing;

    std::fprintf(stderr, "[SurfelCensus] recording -> %s\n", Path.c_str());
    return true;
}

uint32_t EndSurfelCensusRecording(SurfelCensusTrace& Trace)
{
    if (!Trace.Recording)
        return 0;

    const uint32_t Rows = Trace.RowsWritten;
    if (Trace.OutputFile != nullptr)
    {
        std::fclose((FILE*)Trace.OutputFile);
        Trace.OutputFile = nullptr;
    }
    Trace.Recording = false;
    std::fprintf(stderr, "[SurfelCensus] stopped — %u rows -> %s\n", Rows, Trace.OutputPath.c_str());
    Trace.OutputPath.clear();
    return Rows;
}

void FinalizeSurfelCensusTrace(SurfelCensusTrace& Trace)
{
    if (Trace.Recording)
        EndSurfelCensusRecording(Trace);

    if (Trace.Host != nullptr && Trace.Host->Device != VK_NULL_HANDLE)
    {
        VkDevice                     Device    = Trace.Host->Device;
        const VkAllocationCallbacks* Allocator = Trace.Host->Allocator;

        for (uint32_t Ring = 0; Ring < SurfelCensusRingDepth; ++Ring)
        {
            if (Trace.StagingMapped[Ring] != nullptr && Trace.StagingMemory[Ring] != VK_NULL_HANDLE)
                vkUnmapMemory(Device, Trace.StagingMemory[Ring]);
            if (Trace.StagingBuffer[Ring] != VK_NULL_HANDLE) vkDestroyBuffer(Device, Trace.StagingBuffer[Ring], Allocator);
            if (Trace.StagingMemory[Ring] != VK_NULL_HANDLE) vkFreeMemory(Device, Trace.StagingMemory[Ring], Allocator);
        }
        if (Trace.CountersBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Trace.CountersBuffer, Allocator);
        if (Trace.CountersMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Trace.CountersMemory, Allocator);
    }

    Trace = SurfelCensusTrace{};
}

} // namespace Frontier
