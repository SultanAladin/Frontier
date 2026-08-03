/*==============================================================================================================================================
                                                          RADIXSORTSUBMISSION.H
==============================================================================================================================================*/
// 🧩 The GPU least-significant-digit radix sort that orders Morton-coded primitives ahead of the radix-tree build: four 8-bit passes over 32-bit
//    keys, each pass a tally → scan → scatter chain, twenty compute dispatches in total. It owns four compute pipelines (RadixBinTally,
//    RadixBlockScan, RadixBlockBaseAdd, RadixKeyScatter), the ping-pong key/payload buffer pair the passes alternate between, and the bin/offset/
//    block-total scratch the scan walks. The payload is the primitive index, so the sorted payload IS the reordered primitive list the tree build
//    consumes. Built once for a key capacity, recorded once per rebuild. POD + free functions; borrows the VulkanHost, owns its own buffers.
//
//    🔴 THE SORT MUST BE STABLE AND THAT IS A CORRECTNESS REQUIREMENT, NOT A QUALITY ONE. The Karras radix-tree build resolves duplicate Morton
//       codes by comparing primitive index; if equal keys come out in arbitrary order that tiebreak breaks and the tree build does not produce a
//       wrong tree — it HANGS, i.e. a TDR / device loss. Duplicates are ordinary, not pathological: the CPU model measured 19,772 of them at 1M
//       triangles. RadixKeyScatter.comp carries the in-tile guarantee; this file's job is to not disturb it by reordering or fusing dispatches.
//
//    ⚠️ THE SCAN'S MIDDLE STEP IS A SINGLE WORKGROUP AND THEREFORE HAS A HARD CEILING. Step 2b scans the per-block totals with one workgroup of
//       1024 entries, so it can only cover 1024 blocks = 1024 * 1024 = 1,048,576 bin-table entries. The bin table is 256 * GroupCount entries, so
//       the ceiling is a key count of RadixSortKeyCeiling. PAST IT THE SORT SILENTLY CORRUPTS rather than failing — the totals beyond block 1024
//       are never scanned, so a slice of the offsets stays block-local and keys land on top of each other. Initialize REFUSES a capacity above the
//       ceiling instead of trusting the caller; see RadixSortKeyCeiling.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_RADIXSORTSUBMISSION_H
#define FRONTIER_GRAPHICS_ACCELERATION_RADIXSORTSUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The radix: 256 bins = one 8-bit digit per pass. Must match RadixBinCount in RadixBinTally.comp / RadixKeyScatter.comp. Also equals the
//    workgroup width, which is what lets the tally clear and write out one bin per lane with no loop.
constexpr uint32_t RadixBinCount = 256;

// 📝 The workgroup width of all four shaders (local_size_x). Fixed at the bin count on purpose — see above.
constexpr uint32_t RadixLanesPerGroup = 256;

// 📝 Keys handled per workgroup: each lane walks 4 keys at a stride of the workgroup width, so a tile is 1024 keys. Must match KeysPerGroup in
//    RadixBinTally.comp / RadixKeyScatter.comp and EntriesPerGroup in RadixBlockScan.comp / RadixBlockBaseAdd.comp.
constexpr uint32_t RadixKeysPerGroup = 1024;

// 📝 Passes over the key: four 8-bit digits cover a 32-bit Morton code. Each pass is 5 dispatches, so a full sort is 20.
constexpr uint32_t RadixPassCount = 4;

// 🔴 The largest key count the scan's single-workgroup middle step can serve, derived rather than guessed: step 2b covers RadixKeysPerGroup
//    blocks, each block covers RadixKeysPerGroup bin-table entries, and the bin table holds RadixBinCount entries per group of RadixKeysPerGroup
//    keys. So the bound is (1024 * 1024 / 256) * 1024 = 4,194,304 keys. At 1M triangles the block count is 245 against a limit of 1024 — a 4.2x
//    margin, so this ceiling is a real guard rail rather than a formality, and crossing it corrupts silently. Initialize refuses it.
constexpr uint32_t RadixSortKeyCeiling =
    (RadixKeysPerGroup * RadixKeysPerGroup / RadixBinCount) * RadixKeysPerGroup;


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block RadixBinTally.comp and RadixKeyScatter.comp read, matching their RadixTallyConstants / RadixScatterConstants byte-for-byte.
//    Refilled per pass because ShiftBits advances 0 → 8 → 16 → 24.
struct RadixDigitConstants
{
    uint32_t KeyCount   = 0;   // [-] - live keys; the final tile is partial whenever this is not a multiple of RadixKeysPerGroup
    uint32_t GroupCount = 0;   // [-] - tiles in the dispatch; also the bin-major bin-table stride
    uint32_t ShiftBits  = 0;   // [-] - which 8-bit digit this pass sorts (0 / 8 / 16 / 24)
};

// 📝 The push block RadixBlockScan.comp reads, matching its RadixScanConstants byte-for-byte. One shader serves scan steps 2a and 2b — the same
//    operation over different buffers — and TotalWriteEnabled is what selects between them.
struct RadixScanConstants
{
    uint32_t EntryCount        = 0;   // [-] - live entries to scan; the final block is partial whenever this is not a multiple of RadixKeysPerGroup
    uint32_t TotalWriteEnabled = 0;   // [-] - 1 in step 2a (publish each block's total), 0 in step 2b (nothing downstream reads it)
};

// 📝 The push block RadixBlockBaseAdd.comp reads (step 2c), matching its RadixBaseAddConstants byte-for-byte.
struct RadixBaseAddConstants
{
    uint32_t EntryCount = 0;   // [-] - live entries; the final block is partial whenever this is not a multiple of RadixKeysPerGroup
};

// 📝 The sort's owned device resources. The four pipelines and every descriptor set are size-independent once the capacity is fixed, so the record
//    path only pushes constants and dispatches.
//
//    🔴 THE DESCRIPTOR SETS ARE PRE-BAKED PER PING-PONG PARITY, NOT REWRITTEN PER PASS. Four passes alternate which key buffer is source and which
//       is target, so the tally and scatter bindings differ between even and odd passes. Rewriting one set between dispatches would be a
//       use-after-free hazard on a set already recorded into the live command buffer (vkUpdateDescriptorSets on a set bound by a pending command is
//       undefined), so each of those two stages gets TWO sets and the record path selects by parity. Buffers named "Primary" hold the caller's
//       uploaded keys and, because RadixPassCount is EVEN, also hold the sorted result — four swaps return to the start. An odd pass count would
//       leave the answer in Scratch; RetrieveRadixSortedBuffers exists so callers never have to know which.
struct RadixSortSubmission
{
    VulkanHost*           Host              = nullptr;          // [-] - not owned; supplies device / physical device / allocator

    VkPipeline            TallyPipeline     = VK_NULL_HANDLE;   // [-] - RadixBinTally.comp
    VkPipeline            ScanPipeline      = VK_NULL_HANDLE;   // [-] - RadixBlockScan.comp (serves scan steps 2a and 2b)
    VkPipeline            BaseAddPipeline   = VK_NULL_HANDLE;   // [-] - RadixBlockBaseAdd.comp (scan step 2c)
    VkPipeline            ScatterPipeline   = VK_NULL_HANDLE;   // [-] - RadixKeyScatter.comp

    VkDescriptorSetLayout TallySetLayout    = VK_NULL_HANDLE;   // [-] - { key source, bin table }
    VkDescriptorSetLayout ScanSetLayout     = VK_NULL_HANDLE;   // [-] - { scan source, scan target, block totals }
    VkDescriptorSetLayout BaseAddSetLayout  = VK_NULL_HANDLE;   // [-] - { offset table (read-modify-write), block bases }
    VkDescriptorSetLayout ScatterSetLayout  = VK_NULL_HANDLE;   // [-] - { key src, payload src, key dst, payload dst, offset table }

    VkPipelineLayout      TallyLayout       = VK_NULL_HANDLE;   // [-] - TallySetLayout + RadixDigitConstants push range
    VkPipelineLayout      ScanLayout        = VK_NULL_HANDLE;   // [-] - ScanSetLayout + RadixScanConstants push range
    VkPipelineLayout      BaseAddLayout     = VK_NULL_HANDLE;   // [-] - BaseAddSetLayout + RadixBaseAddConstants push range
    VkPipelineLayout      ScatterLayout     = VK_NULL_HANDLE;   // [-] - ScatterSetLayout + RadixDigitConstants push range

    VkDescriptorPool      DescriptorPool    = VK_NULL_HANDLE;   // [-] - sized for the six sets below
    VkDescriptorSet       TallySet[2]       = { VK_NULL_HANDLE, VK_NULL_HANDLE };   // [-] - [parity] tally reading Primary (0) / Scratch (1)
    VkDescriptorSet       BlockScanSet      = VK_NULL_HANDLE;   // [-] - step 2a: bin table -> offset table, publishing block totals
    VkDescriptorSet       TotalScanSet      = VK_NULL_HANDLE;   // [-] - step 2b: block totals -> block bases
    VkDescriptorSet       BaseAddSet        = VK_NULL_HANDLE;   // [-] - step 2c: offset table += block base
    VkDescriptorSet       ScatterSet[2]     = { VK_NULL_HANDLE, VK_NULL_HANDLE };   // [-] - [parity] Primary -> Scratch (0) / Scratch -> Primary (1)

    VkBuffer              PrimaryKeyBuffer     = VK_NULL_HANDLE;   // [-] - device-local keys; the upload target AND (even pass count) the result
    VkDeviceMemory        PrimaryKeyMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for PrimaryKeyBuffer
    VkBuffer              PrimaryPayloadBuffer = VK_NULL_HANDLE;   // [-] - device-local payloads, ping-pong partner of PrimaryKeyBuffer
    VkDeviceMemory        PrimaryPayloadMemory = VK_NULL_HANDLE;   // [-] - backing allocation for PrimaryPayloadBuffer
    VkBuffer              ScratchKeyBuffer     = VK_NULL_HANDLE;   // [-] - device-local keys, the other ping-pong side
    VkDeviceMemory        ScratchKeyMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for ScratchKeyBuffer
    VkBuffer              ScratchPayloadBuffer = VK_NULL_HANDLE;   // [-] - device-local payloads, the other ping-pong side
    VkDeviceMemory        ScratchPayloadMemory = VK_NULL_HANDLE;   // [-] - backing allocation for ScratchPayloadBuffer

    VkBuffer              BinTallyBuffer    = VK_NULL_HANDLE;   // [-] - device-local bin-major bin table (RadixBinCount * GroupCapacity uints)
    VkDeviceMemory        BinTallyMemory    = VK_NULL_HANDLE;   // [-] - backing allocation for BinTallyBuffer
    VkBuffer              BinOffsetBuffer   = VK_NULL_HANDLE;   // [-] - device-local scanned offsets, same shape as the bin table
    VkDeviceMemory        BinOffsetMemory   = VK_NULL_HANDLE;   // [-] - backing allocation for BinOffsetBuffer
    VkBuffer              BlockTotalBuffer  = VK_NULL_HANDLE;   // [-] - device-local per-block totals published by step 2a
    VkDeviceMemory        BlockTotalMemory  = VK_NULL_HANDLE;   // [-] - backing allocation for BlockTotalBuffer
    VkBuffer              BlockBaseBuffer   = VK_NULL_HANDLE;   // [-] - device-local per-block bases produced by step 2b
    VkDeviceMemory        BlockBaseMemory   = VK_NULL_HANDLE;   // [-] - backing allocation for BlockBaseBuffer

    uint32_t              KeyCapacity       = 0;                // [-] - max keys every key/payload buffer is sized for (<= RadixSortKeyCeiling)
    uint32_t              GroupCapacity     = 0;                // [-] - tiles at capacity; the bin table's stride at capacity
    uint32_t              KeyCount          = 0;                // [-] - live key count set by the last upload; 0 makes the record path a no-op
    bool                  ReadyCondition    = false;            // [-] - true once pipelines + layouts + descriptors + buffers are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the four compute pipelines, their set/pipeline layouts, the descriptor pool + six sets, and every device-local buffer sized for MaxKeys.
// ShaderDirectory locates the four .comp.spv files. Returns false (ReadyCondition stays false, handles null) when the host has no device, a shader
// is missing, an allocation fails, or MaxKeys exceeds RadixSortKeyCeiling — that last case is a REFUSAL, not a clamp, because a silently clamped
// capacity would corrupt the sort rather than fail it. Host must be provisioned. Pair with FinalizeRadixSortSubmission.
bool InitializeRadixSortSubmission(RadixSortSubmission& Sort,
                                   VulkanHost&          Host,
                                   uint32_t             MaxKeys,
                                   const char*          ShaderDirectory);

// Stage KeyCount keys and their payloads into the primary buffers through a host-visible scratch buffer, waiting on a fence so both are resident on
// return. Keys are the 32-bit Morton codes; Payloads are the matching primitive indices (Payloads may be null, in which case the identity 0..N-1 is
// generated). Truncates to KeyCapacity with a report. A no-op when ReadyCondition is false. Sets Sort.KeyCount.
bool UploadRadixSortKeys(RadixSortSubmission& Sort,
                         VkCommandPool        CommandPool,
                         const uint32_t*      Keys,
                         const uint32_t*      Payloads,
                         uint32_t             KeyCount);

// Declare KeyCount live keys WITHOUT staging anything, for a producer that has already written the key and payload buffers on the DEVICE — which
// is what the TLAS does: InstanceMortonCode.comp writes its codes straight into PrimaryKeyBuffer / PrimaryPayloadBuffer, so routing them through
// UploadRadixSortKeys would mean device → host → device every frame for data the GPU just produced. Returns false when not ready; refuses (rather
// than truncates) a count past KeyCapacity, matching InitializeRadixSortSubmission's stance on the same overflow.
//
// 🔴 THIS SETS A COUNT, IT DOES NOT MOVE ANY DATA. Calling it without having filled the buffers leaves whatever they last held — for a per-frame
//    rebuild that is the PREVIOUS frame's codes, which sort perfectly and describe a scene that no longer exists. The pairing with a device-side
//    producer is the caller's to guarantee; nothing here can check it.
bool SetRadixSortKeyCount(RadixSortSubmission& Sort, uint32_t KeyCount);

// Report the device buffers a device-side producer must write to have its keys sorted: the primary pair, which is what the first tally reads at
// parity 0. Distinct from RetrieveRadixSortedBuffers, which reports where the RESULT lands after the passes have run.
void RetrieveRadixSortInputBuffers(const RadixSortSubmission& Sort, VkBuffer& OutKeyBuffer, VkBuffer& OutPayloadBuffer);

// Record the whole sort: RadixPassCount passes of tally → scan 2a → scan 2b → scan 2c → scatter, with a compute→compute barrier between every
// dispatch. A no-op when not ready or KeyCount is 0. CommandBuffer must be recording, OUTSIDE any rendering scope.
//
// 🔴 EVERY BARRIER HERE IS LOAD-BEARING AND THE DISPATCH COUNT IS NOT NEGOTIABLE. Each dispatch consumes what the previous one wrote, and Vulkan
//    offers no ordering between workgroups inside one dispatch — so the scan's three steps cannot be fused into one, and dropping a barrier turns
//    the sort into a race that on this hardware would usually still give the right answer. Both failure modes are intermittent, not obvious.
void RecordRadixSort(RadixSortSubmission& Sort, VkCommandBuffer CommandBuffer);

// Report which buffer pair holds the sorted result after RecordRadixSort. With RadixPassCount even this is always the primary pair, but the parity
// is derived rather than assumed so a change to the pass count cannot silently hand back the unsorted side.
void RetrieveRadixSortedBuffers(const RadixSortSubmission& Sort, VkBuffer& OutKeyBuffer, VkBuffer& OutPayloadBuffer);

// Copy SampleCount sorted keys and payloads back to host memory through a staging buffer, blocking until the copy completes. Intended for the
// validation gate that judges the GPU result against std::stable_sort, not for per-frame use. Returns false when not ready or the readback fails.
bool RetrieveRadixSortReadback(RadixSortSubmission& Sort,
                               VkCommandPool        CommandPool,
                               uint32_t             SampleCount,
                               uint32_t*            OutKeys,
                               uint32_t*            OutPayloads);

// Destroy the pipelines / layouts / descriptors / all buffers, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeRadixSortSubmission(RadixSortSubmission& Sort);

} // namespace Frontier

#endif
