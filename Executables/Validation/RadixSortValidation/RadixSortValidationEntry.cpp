/*==============================================================================================================================================
                                                      RADIXSORTVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for the GPU radix sort: brings up a HEADLESS Vulkan device, runs RadixSortSubmission over generated Morton keys, reads the result
//    back, and judges it against std::stable_sort. Nothing before this proved the sort works — RadixShaderTranscriptionProbe proved the shaders'
//    ALGORITHMS on the CPU and RadixHostArithmeticProbe proved the host's ARITHMETIC, but neither executes a single Vulkan command, so descriptor
//    layout, barrier sufficiency and shared-memory atomics are all untested until this runs.
//
//    🔴 THE JUDGEMENT IS ORDER AND STABILITY AND PAYLOAD, NOT JUST ORDER. A sort that emits ascending keys but permutes equal keys arbitrarily passes
//       an ordering check and then HANGS the Karras tree build downstream (the duplicate-Morton tiebreak resolves by primitive index). So every case
//       compares the payload sequence element-for-element against std::stable_sort's, which is the only check that can see instability.
//
//    📝 Headless on purpose: InitializeVulkanHost never creates or queries a surface — it only enables the swapchain DEVICE extension and picks a
//       queue family on the graphics bit — so passing a zero extension count yields a compute-capable device with no window. That keeps this gate
//       runnable unattended, which the stress mode depends on.
//
//    Modes:  (default)  the correctness suite — fixed scales, adversarial distributions, boundary key counts.
//            --stress   the endurance suite — large scales, repeated runs, randomized counts. Slow; run deliberately.

#define _CRT_SECURE_NO_WARNINGS

#include "Graphics/Acceleration/RadixSortSubmission.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Where the compiled .comp.spv files live, relative to the repo root this exe is launched from.
const char* ShaderDirectoryPath = "Internal/Graphics/Acceleration/Shaders";

//------------------------------------------------------------------------------------------------------------------------
//                                                        KEY DISTRIBUTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The distributions worth testing, each chosen for a specific failure it can expose rather than for variety's sake.
enum class KeyDistribution
{
    UniformRandom,      // the ordinary case: every digit roughly balanced across bins
    AllIdentical,       // 🔴 every key duplicates: maximum stability pressure, every key lands in ONE bin per pass
    TwoValues,          // near-degenerate: two huge equal runs, so ranks within a bin are long
    AlreadySorted,      // the scatter becomes near-identity; a fencepost error shows as an off-by-one shift
    ReverseSorted,      // maximum movement: every key crosses the buffer
    SingleBitVarying,   // only bit 0 varies: passes 1-3 see a single bin, exercising the empty-bin path
    HighBitsOnly,       // only the top digit varies: passes 0-2 see a single bin
    ClusteredMorton,    // realistic: Morton codes of points clustered in a few octree cells
    SparseDuplicates    // uniform keys with a deliberate ~2% duplicate injection, mirroring real mesh data
};

const char* DescribeDistribution(KeyDistribution Distribution)
{
    switch (Distribution)
    {
        case KeyDistribution::UniformRandom:    return "uniform-random";
        case KeyDistribution::AllIdentical:     return "all-identical";
        case KeyDistribution::TwoValues:        return "two-values";
        case KeyDistribution::AlreadySorted:    return "already-sorted";
        case KeyDistribution::ReverseSorted:    return "reverse-sorted";
        case KeyDistribution::SingleBitVarying: return "single-bit";
        case KeyDistribution::HighBitsOnly:     return "high-bits-only";
        case KeyDistribution::ClusteredMorton:  return "clustered-morton";
        case KeyDistribution::SparseDuplicates: return "sparse-duplicates";
    }
    return "unknown";
}

// Interleave the low 10 bits of three coordinates into a 30-bit Morton code — the same encoding the tree build will consume, so the clustered case
// carries realistic bit structure rather than arbitrary integers.
uint32_t EncodeMortonCode(uint32_t CoordinateX, uint32_t CoordinateY, uint32_t CoordinateZ)
{
    auto SpreadBits = [](uint32_t Value) -> uint32_t
    {
        Value &= 0x000003FFu;
        Value = (Value | (Value << 16)) & 0x030000FFu;
        Value = (Value | (Value <<  8)) & 0x0300F00Fu;
        Value = (Value | (Value <<  4)) & 0x030C30C3u;
        Value = (Value | (Value <<  2)) & 0x09249249u;
        return Value;
    };
    return (SpreadBits(CoordinateZ) << 2) | (SpreadBits(CoordinateY) << 1) | SpreadBits(CoordinateX);
}

std::vector<uint32_t> GenerateKeys(KeyDistribution Distribution, uint32_t KeyCount, uint32_t SeedValue)
{
    std::mt19937 Generator(SeedValue);
    std::vector<uint32_t> Keys(KeyCount);

    switch (Distribution)
    {
        case KeyDistribution::UniformRandom:
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = Generator();
            break;

        case KeyDistribution::AllIdentical:
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = 0xA5A5A5A5u;
            break;

        case KeyDistribution::TwoValues:
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = (Generator() & 1u) ? 0x11111111u : 0xEEEEEEEEu;
            break;

        case KeyDistribution::AlreadySorted:
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = Index;
            break;

        case KeyDistribution::ReverseSorted:
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = KeyCount - 1u - Index;
            break;

        case KeyDistribution::SingleBitVarying:
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = Generator() & 1u;
            break;

        case KeyDistribution::HighBitsOnly:
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = (Generator() & 0xFFu) << 24;
            break;

        case KeyDistribution::ClusteredMorton:
        {
            // A handful of octree cells, each holding many points — what a real mesh's triangle centroids look like.
            const uint32_t ClusterCount = 8;
            std::uniform_int_distribution<uint32_t> ClusterPick(0, ClusterCount - 1);
            std::uniform_int_distribution<uint32_t> LocalJitter(0, 15);
            uint32_t ClusterOriginX[ClusterCount], ClusterOriginY[ClusterCount], ClusterOriginZ[ClusterCount];
            for (uint32_t Cluster = 0; Cluster < ClusterCount; ++Cluster)
            {
                ClusterOriginX[Cluster] = Generator() % 1000u;
                ClusterOriginY[Cluster] = Generator() % 1000u;
                ClusterOriginZ[Cluster] = Generator() % 1000u;
            }
            for (uint32_t Index = 0; Index < KeyCount; ++Index)
            {
                const uint32_t Cluster = ClusterPick(Generator);
                Keys[Index] = EncodeMortonCode((ClusterOriginX[Cluster] + LocalJitter(Generator)) & 0x3FFu,
                                               (ClusterOriginY[Cluster] + LocalJitter(Generator)) & 0x3FFu,
                                               (ClusterOriginZ[Cluster] + LocalJitter(Generator)) & 0x3FFu);
            }
            break;
        }

        case KeyDistribution::SparseDuplicates:
        {
            for (uint32_t Index = 0; Index < KeyCount; ++Index) Keys[Index] = Generator();
            // Overwrite ~2% of slots with an earlier key, producing duplicates scattered across the whole range.
            const uint32_t DuplicateCount = KeyCount / 50u;
            for (uint32_t Step = 0; Step < DuplicateCount && KeyCount > 1; ++Step)
            {
                const uint32_t Destination = Generator() % KeyCount;
                const uint32_t Source      = Generator() % KeyCount;
                Keys[Destination] = Keys[Source];
            }
            break;
        }
    }
    return Keys;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE JUDGEMENT
//------------------------------------------------------------------------------------------------------------------------

struct CaseOutcome
{
    bool     Passed            = false;
    bool     OrderedCondition  = false;
    bool     StableCondition   = false;
    bool     KeysMatched       = false;
    bool     PayloadsMatched   = false;
    uint32_t DuplicateTally    = 0;
    uint32_t FirstFaultIndex   = 0xFFFFFFFFu;
    char     FaultText[192]    = {};
};

// Compare the GPU's (key, payload) sequence against std::stable_sort's over the same input. The payload comparison is what detects instability: a
// stable sort's payload sequence is uniquely determined, so any deviation among equal keys is a real fault, not an allowed alternative.
CaseOutcome JudgeSortedResult(const std::vector<uint32_t>& SourceKeys,
                              const std::vector<uint32_t>& DeviceKeys,
                              const std::vector<uint32_t>& DevicePayloads)
{
    CaseOutcome Outcome;
    const uint32_t KeyCount = (uint32_t)SourceKeys.size();

    // The reference: stable_sort over (key, original index) pairs.
    std::vector<std::pair<uint32_t, uint32_t>> Reference(KeyCount);
    for (uint32_t Index = 0; Index < KeyCount; ++Index)
        Reference[Index] = { SourceKeys[Index], Index };
    std::stable_sort(Reference.begin(), Reference.end(),
                     [](const std::pair<uint32_t, uint32_t>& Left, const std::pair<uint32_t, uint32_t>& Right)
                     { return Left.first < Right.first; });

    for (uint32_t Index = 1; Index < KeyCount; ++Index)
        if (Reference[Index].first == Reference[Index - 1].first)
            ++Outcome.DuplicateTally;

    // -- ordering ------------------------------------------------------------------------------------------------------
    Outcome.OrderedCondition = true;
    for (uint32_t Index = 1; Index < KeyCount; ++Index)
    {
        if (DeviceKeys[Index] < DeviceKeys[Index - 1])
        {
            Outcome.OrderedCondition = false;
            Outcome.FirstFaultIndex  = Index;
            std::snprintf(Outcome.FaultText, sizeof(Outcome.FaultText),
                          "descending pair at %u: key[%u]=0x%08X > key[%u]=0x%08X",
                          Index, Index - 1, DeviceKeys[Index - 1], Index, DeviceKeys[Index]);
            break;
        }
    }

    // -- keys against the reference -------------------------------------------------------------------------------------
    Outcome.KeysMatched = true;
    for (uint32_t Index = 0; Index < KeyCount; ++Index)
    {
        if (DeviceKeys[Index] != Reference[Index].first)
        {
            Outcome.KeysMatched = false;
            if (Outcome.FirstFaultIndex == 0xFFFFFFFFu)
            {
                Outcome.FirstFaultIndex = Index;
                std::snprintf(Outcome.FaultText, sizeof(Outcome.FaultText),
                              "key mismatch at %u: device 0x%08X, reference 0x%08X",
                              Index, DeviceKeys[Index], Reference[Index].first);
            }
            break;
        }
    }

    // -- payloads against the reference : THE STABILITY CHECK ------------------------------------------------------------
    Outcome.PayloadsMatched = true;
    for (uint32_t Index = 0; Index < KeyCount; ++Index)
    {
        if (DevicePayloads[Index] != Reference[Index].second)
        {
            Outcome.PayloadsMatched = false;
            if (Outcome.FirstFaultIndex == 0xFFFFFFFFu)
            {
                Outcome.FirstFaultIndex = Index;
                std::snprintf(Outcome.FaultText, sizeof(Outcome.FaultText),
                              "payload mismatch at %u (key 0x%08X): device %u, reference %u — INSTABILITY",
                              Index, DeviceKeys[Index], DevicePayloads[Index], Reference[Index].second);
            }
            break;
        }
    }

    // Stability restated directly: among equal keys the payloads must ascend. Redundant with the payload comparison above but it names the fault
    // precisely when both trip, which is the difference between "the sort is wrong" and "the sort is unstable".
    Outcome.StableCondition = true;
    for (uint32_t Index = 1; Index < KeyCount; ++Index)
    {
        if (DeviceKeys[Index] == DeviceKeys[Index - 1] && DevicePayloads[Index] < DevicePayloads[Index - 1])
        {
            Outcome.StableCondition = false;
            break;
        }
    }

    Outcome.Passed = Outcome.OrderedCondition && Outcome.StableCondition && Outcome.KeysMatched && Outcome.PayloadsMatched;
    return Outcome;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE DEVICE HARNESS
//------------------------------------------------------------------------------------------------------------------------

// One sort on the device: upload, record the twenty dispatches into a one-shot command buffer, submit, wait, read back, judge.
bool ExecuteDeviceSort(RadixSortSubmission&         Sort,
                       VulkanHost&                  Host,
                       VkCommandPool                CommandPool,
                       const std::vector<uint32_t>& SourceKeys,
                       std::vector<uint32_t>&       OutKeys,
                       std::vector<uint32_t>&       OutPayloads)
{
    const uint32_t KeyCount = (uint32_t)SourceKeys.size();

    if (!UploadRadixSortKeys(Sort, CommandPool, SourceKeys.data(), nullptr, KeyCount))
    {
        std::printf("      upload failed\n");
        return false;
    }

    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer SortCommand = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &SortCommand) != VK_SUCCESS)
    {
        std::printf("      command buffer allocation failed\n");
        return false;
    }

    bool Submitted = false;
    VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(SortCommand, &BeginInformation) == VK_SUCCESS)
    {
        RecordRadixSort(Sort, SortCommand);
        if (vkEndCommandBuffer(SortCommand) == VK_SUCCESS)
        {
            VkFence Fence = VK_NULL_HANDLE;
            VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            if (vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence) == VK_SUCCESS)
            {
                VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                SubmitInformation.commandBufferCount = 1;
                SubmitInformation.pCommandBuffers    = &SortCommand;
                if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence) == VK_SUCCESS)
                {
                    // A generous but finite wait: a hang here is a real outcome worth reporting, not something to block on forever.
                    const uint64_t WaitNanoseconds = 30ull * 1000ull * 1000ull * 1000ull;
                    const VkResult WaitOutcome = vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, WaitNanoseconds);
                    if (WaitOutcome == VK_SUCCESS)
                        Submitted = true;
                    else
                        std::printf("      fence wait returned %d (timeout / device lost)\n", (int)WaitOutcome);
                }
                else
                {
                    std::printf("      queue submit failed\n");
                }
                vkDestroyFence(Host.Device, Fence, Host.Allocator);
            }
        }
    }
    vkFreeCommandBuffers(Host.Device, CommandPool, 1, &SortCommand);
    if (!Submitted)
        return false;

    OutKeys.assign(KeyCount, 0u);
    OutPayloads.assign(KeyCount, 0u);
    if (!RetrieveRadixSortReadback(Sort, CommandPool, KeyCount, OutKeys.data(), OutPayloads.data()))
    {
        std::printf("      readback failed\n");
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE SUITES
//------------------------------------------------------------------------------------------------------------------------

struct SuiteTally
{
    uint32_t CaseTally    = 0;
    uint32_t FailureTally = 0;
};

void ExecuteCase(RadixSortSubmission& Sort,
                 VulkanHost&          Host,
                 VkCommandPool        CommandPool,
                 KeyDistribution      Distribution,
                 uint32_t             KeyCount,
                 uint32_t             SeedValue,
                 SuiteTally&          Tally)
{
    ++Tally.CaseTally;

    const std::vector<uint32_t> SourceKeys = GenerateKeys(Distribution, KeyCount, SeedValue);
    std::vector<uint32_t> DeviceKeys, DevicePayloads;

    if (!ExecuteDeviceSort(Sort, Host, CommandPool, SourceKeys, DeviceKeys, DevicePayloads))
    {
        std::printf("  %-18s %9u  DEVICE FAILURE\n", DescribeDistribution(Distribution), KeyCount);
        ++Tally.FailureTally;
        return;
    }

    const CaseOutcome Outcome = JudgeSortedResult(SourceKeys, DeviceKeys, DevicePayloads);
    std::printf("  %-18s %9u  dupes %8u  ord %s  stab %s  keys %s  pay %s  %s\n",
                DescribeDistribution(Distribution), KeyCount, Outcome.DuplicateTally,
                Outcome.OrderedCondition ? "y" : "N",
                Outcome.StableCondition  ? "y" : "N",
                Outcome.KeysMatched      ? "y" : "N",
                Outcome.PayloadsMatched  ? "y" : "N",
                Outcome.Passed ? "PASS" : "FAIL");
    if (!Outcome.Passed)
    {
        ++Tally.FailureTally;
        if (Outcome.FaultText[0] != '\0')
            std::printf("      %s\n", Outcome.FaultText);
    }
}

// The correctness suite: every distribution at several scales, plus the boundary key counts where a fencepost error would live.
void ExecuteCorrectnessSuite(RadixSortSubmission& Sort, VulkanHost& Host, VkCommandPool CommandPool, SuiteTally& Tally)
{
    std::printf("\n---- correctness suite ------------------------------------------------------------------------------\n");

    const KeyDistribution Distributions[] = {
        KeyDistribution::UniformRandom,   KeyDistribution::AllIdentical,    KeyDistribution::TwoValues,
        KeyDistribution::AlreadySorted,   KeyDistribution::ReverseSorted,   KeyDistribution::SingleBitVarying,
        KeyDistribution::HighBitsOnly,    KeyDistribution::ClusteredMorton, KeyDistribution::SparseDuplicates
    };

    // 🔴 The boundary counts matter more than the round ones. 1023/1024/1025 straddle a single tile; 262143/262144/262145 straddle the point where
    //    the bin table needs a second scan BLOCK (256 groups * 256 bins = 65536 entries... the table crosses 1024-entry blocks constantly, but
    //    262144 keys = 256 groups is where the block count itself steps). A partial final tile is the most likely place for an off-by-one.
    const uint32_t KeyCounts[] = { 1u, 2u, 255u, 256u, 257u, 1023u, 1024u, 1025u, 4095u, 4096u, 4097u,
                                   65535u, 65536u, 65537u, 262144u, 262145u, 1000000u };

    for (KeyDistribution Distribution : Distributions)
        for (uint32_t KeyCount : KeyCounts)
            ExecuteCase(Sort, Host, CommandPool, Distribution, KeyCount, 1234u + KeyCount, Tally);
}

// The stress suite: sustained large-scale work with randomized counts, to expose anything that only appears under repetition, near capacity, or at
// key counts nobody chose deliberately.
void ExecuteStressSuite(RadixSortSubmission& Sort, VulkanHost& Host, VkCommandPool CommandPool, uint32_t Capacity, SuiteTally& Tally)
{
    std::printf("\n---- stress suite -----------------------------------------------------------------------------------\n");

    // Pass 1: repeated runs at a large fixed scale. A barrier that is ALMOST sufficient tends to pass once and fail on the tenth run, so a single
    // large case proves much less than the same case repeated.
    std::printf("  [repetition] 24 runs, uniform-random, 1,000,000 keys — a marginal barrier fails intermittently\n");
    for (uint32_t Repetition = 0; Repetition < 24u; ++Repetition)
        ExecuteCase(Sort, Host, CommandPool, KeyDistribution::UniformRandom, 1000000u, 9000u + Repetition, Tally);

    // Pass 2: the same at maximum duplicate pressure, where every key shares a bin and the ranking sweep is fully serialized per bin.
    std::printf("  [duplicates] 12 runs, all-identical, 1,000,000 keys — maximum stability pressure\n");
    for (uint32_t Repetition = 0; Repetition < 12u; ++Repetition)
        ExecuteCase(Sort, Host, CommandPool, KeyDistribution::AllIdentical, 1000000u, 4000u + Repetition, Tally);

    // Pass 3: randomized key counts, so the partial-final-tile arithmetic is exercised at counts nobody picked.
    std::printf("  [randomized] 32 runs, random distribution and count up to capacity\n");
    std::mt19937 Generator(20260802u);
    std::uniform_int_distribution<uint32_t> CountPick(1u, Capacity);
    const KeyDistribution Rotation[] = { KeyDistribution::UniformRandom, KeyDistribution::ClusteredMorton,
                                         KeyDistribution::SparseDuplicates, KeyDistribution::TwoValues };
    for (uint32_t Step = 0; Step < 32u; ++Step)
        ExecuteCase(Sort, Host, CommandPool, Rotation[Step % 4], CountPick(Generator), 7000u + Step, Tally);

    // Pass 4: right at capacity, the largest thing this build can sort.
    std::printf("  [at capacity] the largest count this instance can hold\n");
    ExecuteCase(Sort, Host, CommandPool, KeyDistribution::UniformRandom,   Capacity, 31337u, Tally);
    ExecuteCase(Sort, Host, CommandPool, KeyDistribution::ClusteredMorton, Capacity, 31338u, Tally);
    ExecuteCase(Sort, Host, CommandPool, KeyDistribution::AllIdentical,    Capacity, 31339u, Tally);
}

// The refusal check: Initialize must REFUSE a capacity past the single-workgroup scan ceiling rather than clamp it. Clamping would corrupt silently,
// so this asserts the failure path actually fails.
void ExecuteRefusalCheck(VulkanHost& Host, SuiteTally& Tally)
{
    std::printf("\n---- ceiling refusal --------------------------------------------------------------------------------\n");
    ++Tally.CaseTally;

    RadixSortSubmission OverCapacitySort;
    const bool Built = InitializeRadixSortSubmission(OverCapacitySort, Host, RadixSortKeyCeiling + 1u, ShaderDirectoryPath);
    if (Built)
    {
        std::printf("  %u keys (ceiling+1) accepted  FAIL — must be refused, not clamped\n", RadixSortKeyCeiling + 1u);
        ++Tally.FailureTally;
        FinalizeRadixSortSubmission(OverCapacitySort);
    }
    else
    {
        std::printf("  %u keys (ceiling+1) refused   PASS\n", RadixSortKeyCeiling + 1u);
    }
    FinalizeRadixSortSubmission(OverCapacitySort);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                             ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    bool StressEnabled = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
        if (std::strcmp(ArgumentValues[Index], "--stress") == 0)
            StressEnabled = true;

    std::printf("==== RadixSortValidation : GPU radix sort against std::stable_sort ====\n");
    std::printf("mode: %s\n", StressEnabled ? "correctness + STRESS" : "correctness");

    // -- headless device ---------------------------------------------------------------------------------------------
    // No window, no surface: InitializeVulkanHost never queries presentation, so a zero extension count is enough.
    VulkanHost Host;
    if (!InitializeVulkanHost(Host, nullptr, 0))
    {
        std::printf("FAIL: no Vulkan device\n");
        return 1;
    }

    VkPhysicalDeviceProperties DeviceProperties = {};
    vkGetPhysicalDeviceProperties(Host.PhysicalDevice, &DeviceProperties);
    std::printf("device: %s\n", DeviceProperties.deviceName);

    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(Host.Device, &PoolInformation, Host.Allocator, &CommandPool) != VK_SUCCESS)
    {
        std::printf("FAIL: command pool creation failed\n");
        FinalizeVulkanHost(Host);
        return 1;
    }

    // Capacity: enough for the correctness suite's largest case, raised for stress so the randomized counts have room to roam.
    const uint32_t Capacity = StressEnabled ? 2000000u : 1000000u;

    RadixSortSubmission Sort;
    if (!InitializeRadixSortSubmission(Sort, Host, Capacity, ShaderDirectoryPath))
    {
        std::printf("FAIL: sort initialization failed (are the .comp.spv files built? expected in %s)\n", ShaderDirectoryPath);
        vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
        FinalizeVulkanHost(Host);
        return 1;
    }
    std::printf("capacity: %u keys (ceiling %u)\n", Sort.KeyCapacity, RadixSortKeyCeiling);

    SuiteTally Tally;
    ExecuteCorrectnessSuite(Sort, Host, CommandPool, Tally);
    if (StressEnabled)
        ExecuteStressSuite(Sort, Host, CommandPool, Capacity, Tally);

    // -- teardown ----------------------------------------------------------------------------------------------------
    vkDeviceWaitIdle(Host.Device);
    FinalizeRadixSortSubmission(Sort);

    ExecuteRefusalCheck(Host, Tally);

    vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
    FinalizeVulkanHost(Host);

    std::printf("\n==== %s : %u cases, %u failure%s ====\n",
                Tally.FailureTally == 0 ? "PASS" : "FAIL",
                Tally.CaseTally, Tally.FailureTally, Tally.FailureTally == 1 ? "" : "s");
    return Tally.FailureTally == 0 ? 0 : 1;
}
