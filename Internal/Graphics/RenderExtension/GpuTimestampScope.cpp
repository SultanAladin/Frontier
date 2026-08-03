/*==============================================================================================================================================
                                                          GPUTIMESTAMPSCOPE.CPP
==============================================================================================================================================*/
// See GpuTimestampScope.h for the design. In short: one timestamp query pool, ringed by frames-in-flight, read one frame late with no wait bit so
// the CPU never stalls on the GPU, and a hard best-effort no-op when the device cannot timestamp the graphics queue.

#include "Graphics/RenderExtension/GpuTimestampScope.h"
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Total queries in the pool: two (begin+end) per slot, per ring.
    constexpr uint32_t TimestampQueryCount = GpuTimestampMaxSlots * 2u * GpuTimestampRingDepth;

    // The pool index of a slot's BEGIN query for a given ring. The END query is the next index. Laid out ring-major so one ring's queries are
    // contiguous — which lets a per-ring reset touch a single [base, base+span) range rather than a strided set.
    inline uint32_t RingBase(uint32_t Ring) { return Ring * (GpuTimestampMaxSlots * 2u); }
    inline uint32_t BeginQuery(uint32_t Ring, uint32_t Slot) { return RingBase(Ring) + Slot * 2u; }
    inline uint32_t EndQuery(uint32_t Ring, uint32_t Slot)   { return RingBase(Ring) + Slot * 2u + 1u; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeGpuTimestampScope(GpuTimestampScope& Scope, VulkanHost& Host)
{
    Scope = GpuTimestampScope{};
    Scope.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE || Host.PhysicalDevice == VK_NULL_HANDLE)
        return false;

    // ⚠️ Two independent gates, both of which must pass or the timestamps are meaningless. timestampComputeAndGraphics being VK_FALSE means SOME
    //    queue families lack timestamp support and the caller would have to check each; rather than special-case that, treat it as "not supported
    //    here" — every device this engine targets sets it true, so the fallback is only ever hit on a device we could not measure honestly anyway.
    VkPhysicalDeviceProperties DeviceProperties = {};
    vkGetPhysicalDeviceProperties(Host.PhysicalDevice, &DeviceProperties);
    if (DeviceProperties.limits.timestampComputeAndGraphics == VK_FALSE)
    {
        ISSUE_NOTICE("gpu-timestamp", "device reports no timestampComputeAndGraphics; GPU timing disabled");
        return false;
    }
    if (DeviceProperties.limits.timestampPeriod <= 0.0f)
    {
        ISSUE_NOTICE("gpu-timestamp", "device reports a zero timestampPeriod; GPU timing disabled");
        return false;
    }

    // 🔴 The graphics queue family's own timestampValidBits must be non-zero: timestampComputeAndGraphics can be true while a SPECIFIC family still
    //    writes no valid bits. The renderer records every bracketed pass on the graphics queue, so THAT family's support is the one that matters.
    uint32_t FamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(Host.PhysicalDevice, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(Host.PhysicalDevice, &FamilyCount, Families.data());
    if (Host.GraphicsQueueFamily >= FamilyCount || Families[Host.GraphicsQueueFamily].timestampValidBits == 0)
    {
        ISSUE_NOTICE("gpu-timestamp", "graphics queue family reports 0 timestampValidBits; GPU timing disabled");
        return false;
    }

    VkQueryPoolCreateInfo PoolInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    PoolInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    PoolInfo.queryCount = TimestampQueryCount;
    if (vkCreateQueryPool(Host.Device, &PoolInfo, Host.Allocator, &Scope.QueryPool) != VK_SUCCESS)
    {
        ISSUE_NOTICE("gpu-timestamp", "vkCreateQueryPool failed; GPU timing disabled");
        Scope.QueryPool = VK_NULL_HANDLE;
        return false;
    }

    Scope.NanosecondsPerTick = (double)DeviceProperties.limits.timestampPeriod;
    Scope.RecordRing  = 0;
    Scope.CollectRing = 0;
    Scope.ReadyCondition = true;
    ISSUE_NOTICE("gpu-timestamp", "GPU timing ready (%.2f ns/tick, %u rings)", Scope.NanosecondsPerTick, GpuTimestampRingDepth);
    return true;
}

void BeginGpuTimestampFrame(GpuTimestampScope& Scope, VkCommandBuffer CommandBuffer)
{
    if (!Scope.ReadyCondition)
        return;

    // Advance to the next ring for THIS frame's writes. The just-vacated ring stays untouched until CollectResults reads it (it trails by the depth).
    Scope.RecordRing = (Scope.RecordRing + 1u) % GpuTimestampRingDepth;

    // 🔴 A timestamp query MUST be reset before it is written, or vkCmdWriteTimestamp records into a query still holding the last cycle's result and
    //    the readback is undefined. Reset this ring's whole contiguous span in one call — cheaper than a per-slot reset and safe because nothing has
    //    written this ring yet this frame.
    vkCmdResetQueryPool(CommandBuffer, Scope.QueryPool, RingBase(Scope.RecordRing), GpuTimestampMaxSlots * 2u);

    for (uint32_t Slot = 0; Slot < GpuTimestampMaxSlots; ++Slot)
        Scope.SlotUsed[Slot] = false;
}

void BeginGpuTimestampScope(GpuTimestampScope& Scope, VkCommandBuffer CommandBuffer, uint32_t Slot)
{
    if (!Scope.ReadyCondition || Slot >= GpuTimestampMaxSlots)
        return;

    // TOP_OF_PIPE for the begin stamp: it marks when the GPU REACHES this point, so the pair delta measures the work between here and the end stamp.
    vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Scope.QueryPool, BeginQuery(Scope.RecordRing, Slot));
    Scope.SlotUsed[Slot] = true;
}

void EndGpuTimestampScope(GpuTimestampScope& Scope, VkCommandBuffer CommandBuffer, uint32_t Slot)
{
    if (!Scope.ReadyCondition || Slot >= GpuTimestampMaxSlots || !Scope.SlotUsed[Slot])
        return;

    // BOTTOM_OF_PIPE for the end stamp: it marks when ALL prior work has completed, so begin(top)->end(bottom) brackets the pass's real GPU span.
    vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Scope.QueryPool, EndQuery(Scope.RecordRing, Slot));

    // Mark this ring primed once its first pair lands, so Collect knows the ring holds real data rather than a zeroed reset.
    Scope.RingPrimed[Scope.RecordRing] = true;
}

void CollectGpuTimestampResults(GpuTimestampScope& Scope)
{
    if (!Scope.ReadyCondition)
        return;

    // Read the ring that trails the one being recorded — the GPU has retired it by now, so a no-wait read succeeds without a CPU stall. On the very
    // first frames CollectRing points at an unprimed ring; the RingPrimed gate below leaves ResolvedMillis untouched (zero) until real data exists.
    Scope.CollectRing = (Scope.RecordRing + 1u) % GpuTimestampRingDepth;
    if (!Scope.RingPrimed[Scope.CollectRing])
        return;

    // 64-bit results + availability: WITH_AVAILABILITY writes an availability word after each query so a not-yet-ready pair is skipped rather than
    // read as garbage. No WAIT bit — this must never block the CPU on the GPU.
    const uint32_t Base = RingBase(Scope.CollectRing);
    uint64_t Raw[GpuTimestampMaxSlots * 2u * 2u] = {};   // (begin,end) x (value,availability) per slot
    const VkResult Status = vkGetQueryPoolResults(
        Scope.Host->Device, Scope.QueryPool,
        Base, GpuTimestampMaxSlots * 2u,
        sizeof(Raw), Raw,
        sizeof(uint64_t) * 2u,   // stride: value + availability per query
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    // VK_NOT_READY is expected and fine — some pairs simply are not back yet; keep last frame's numbers for those. Any hard error leaves the numbers
    // untouched too rather than writing noise.
    if (Status != VK_SUCCESS && Status != VK_NOT_READY)
        return;

    const double NanosecondsToMillis = 1.0 / 1.0e6;
    for (uint32_t Slot = 0; Slot < GpuTimestampMaxSlots; ++Slot)
    {
        const uint32_t BeginIndex = Slot * 2u;          // begin query within this ring's contiguous read
        const uint32_t EndIndex   = Slot * 2u + 1u;

        // Each query occupies two uint64s in Raw (value, availability). Availability != 0 means the value is valid.
        const uint64_t BeginValue = Raw[BeginIndex * 2u + 0u];
        const uint64_t BeginAvail = Raw[BeginIndex * 2u + 1u];
        const uint64_t EndValue   = Raw[EndIndex   * 2u + 0u];
        const uint64_t EndAvail   = Raw[EndIndex   * 2u + 1u];

        if (BeginAvail == 0u || EndAvail == 0u)
            continue;                                   // pair not back yet — leave the last good number in place
        if (EndValue < BeginValue)
            continue;                                   // wrapped / bogus tick delta — never write a negative span

        const double Ticks  = (double)(EndValue - BeginValue);
        Scope.ResolvedMillis[Slot] = (float)(Ticks * Scope.NanosecondsPerTick * NanosecondsToMillis);
    }
}

void FinalizeGpuTimestampScope(GpuTimestampScope& Scope)
{
    if (Scope.Host && Scope.Host->Device != VK_NULL_HANDLE && Scope.QueryPool != VK_NULL_HANDLE)
        vkDestroyQueryPool(Scope.Host->Device, Scope.QueryPool, Scope.Host->Allocator);
    Scope = GpuTimestampScope{};
}

} // namespace Frontier
