/*==============================================================================================================================================
                                                          SURFELGRIDSLOTTING.H
==============================================================================================================================================*/
// 🧩 The per-frame grid build: bucket every live surfel into the camera-relative cascaded hash grid so any later stage can gather "the surfels near
//    this cell" in O(1). It owns the grid's two SSBOs and drives the five-stage pipeline — clear → count → SCAN → slot — the scan being the segmented
//    prefix sum (delegated to an owned SurfelPrefixSum). Given the surfel pool's records + high-water count and the frame's camera, on return the
//    Offsets buffer holds each cell's list-slice start and the List buffer holds the surfel indices of every cell, packed slice by slice.
//
//    🔴 THE GRID IS TWO SSBOs, NOT webgiya's ONE (PLAN §8 F6). webgiya merges the offsets header and the surfel list into a single buffer purely to
//       dodge WebGPU's 10-SSBO bind limit — a limit Vulkan does not have. Merging is the direct cause of F2 (the list index needs an OFFSETS_AND_LIST_START
//       bias and the whole thing shares one atomic). Splitting into a standalone Offsets buffer (header, TOTAL_CELLS+1 ints) and a standalone List buffer
//       ((TOTAL_CELLS+1) * MAX_SURFELS_PER_CELL ints) removes the bias and the shared-atomic hazard: the slot pass writes List[writeIdx] directly.
//
//    🔴 THE INTER-STAGE BARRIERS ARE MANDATORY (PLAN §8 F3). WebGPU fences between compute nodes implicitly; Vulkan does not. Every stage RMWs the
//       Offsets buffer the next stage reads — clear writes it, count atomic-increments it, the scan reads-then-writes it four times, slot atomic-decrements
//       it. Miss one barrier and a stage reads a half-written Offsets array: a torn prefix sum, surfels dropped or double-counted, silent under light load.
//
//    📝 The count/slot passes read the pool's SurfelBuffer + PoolMaxBuffer (BORROWED — the pool owns them). The scan is an owned SurfelPrefixSum operating
//       on this unit's Offsets buffer. POD struct + free functions, mirroring InstanceCullSubmission.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELGRIDSLOTTING_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELGRIDSLOTTING_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelPool.h"
#include "Graphics/Surfel/SurfelPrefixSum.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Grid shape — MUST match SurfelGrid.glsl (SURFEL_CS, SURFEL_CASCADES, MAX_SURFELS_PER_CELL). These fix the two SSBO sizes. TotalCells = CS^3 x
//    cascades; the Offsets header is TotalCells + 1 (an extra terminal offset); the List is (TotalCells + 1) x per-cell cap.
constexpr uint32_t SurfelGridCellEdge     = 32;    // [-] - SURFEL_CS
constexpr uint32_t SurfelGridCascades     = 8;     // [-] - SURFEL_CASCADES
constexpr uint32_t SurfelMaxPerCell       = 64;    // [-] - MAX_SURFELS_PER_CELL
constexpr uint32_t SurfelGridTotalCells   = SurfelGridCellEdge * SurfelGridCellEdge * SurfelGridCellEdge * SurfelGridCascades;   // 262144
constexpr uint32_t SurfelGridOffsetsCount = SurfelGridTotalCells + 1;                                   // header entries (scan element count)
constexpr uint32_t SurfelGridListCount    = (SurfelGridTotalCells + 1) * SurfelMaxPerCell;              // surfel-index list capacity

// The count/slot workgroup edge — must match local_size_x in SurfelGridCount.comp / SurfelGridSlot.comp. One lane per pool slot.
constexpr uint32_t SurfelSlottingWorkgroupEdge = 64;
// The clear workgroup edge — must match local_size_x in SurfelGridClear.comp. One lane per Offsets entry.
constexpr uint32_t SurfelClearWorkgroupEdge    = 256;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the count/slot passes read. Camera position drives the surfel radius (raw eye distance); GridOrigin (the SNAPPED grid origin) drives
//    the cell indices and centres. ListCount is the slot pass's bounds guard. std430, vec4-aligned; the slot pass reads the whole thing, clear/count read
//    the leading fields — one shared layout keeps a single push range.
struct SurfelSlottingConstants
{
    float    CameraPosition[4] = { 0, 0, 0, 0 };   // [m] - raw camera world position (radius distance); w unused
    float    GridOrigin[4]     = { 0, 0, 0, 0 };   // [m] - snapped grid origin (cell indices + centres); w unused
    int32_t  ListCount         = 0;                // [-] - List SSBO capacity (slot bounds guard)
    int32_t  Padding0          = 0;
    int32_t  Padding1          = 0;
    int32_t  Padding2          = 0;
};

// 📝 The slotting unit's owned resources. Two SSBOs (Offsets header + surfel-index List), three pipelines (clear/count/slot) sharing one set layout, an
//    owned SurfelPrefixSum for the scan stage, and the descriptor set the passes bind. BoundSurfelBuffer / BoundPoolMaxBuffer cache the borrowed pool
//    handles so the descriptor is re-pointed only when the pool changes. ReadyCondition gates recording.
struct SurfelGridSlotting
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - Offsets @0, Surfel @1, PoolMax @2, List @3 (all compute storage)
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - SetLayout + SurfelSlottingConstants push range
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - one set
    VkDescriptorSet       SlottingSet    = VK_NULL_HANDLE;   // [-] - the bound set

    VkPipeline            ClearPipeline  = VK_NULL_HANDLE;   // [-] - SurfelGridClear.comp
    VkPipeline            CountPipeline  = VK_NULL_HANDLE;   // [-] - SurfelGridCount.comp
    VkPipeline            SlotPipeline   = VK_NULL_HANDLE;   // [-] - SurfelGridSlot.comp

    VkBuffer              OffsetsBuffer  = VK_NULL_HANDLE;   // [-] - per-cell header (TotalCells+1 ints); scanned in place
    VkDeviceMemory        OffsetsMemory  = VK_NULL_HANDLE;
    VkBuffer              ListBuffer     = VK_NULL_HANDLE;   // [-] - surfel-index list ((TotalCells+1)*cap ints); init -1 once
    VkDeviceMemory        ListMemory     = VK_NULL_HANDLE;

    SurfelPrefixSum       Prefix;                            // [-] - owned segmented scan over OffsetsBuffer

    VkBuffer              BoundSurfelBuffer  = VK_NULL_HANDLE;   // [-] - last pool SurfelBuffer bound at @1
    VkBuffer              BoundPoolMaxBuffer = VK_NULL_HANDLE;   // [-] - last pool PoolMaxBuffer bound at @2

    bool                  ReadyCondition = false;           // [-] - true once pipelines + layout + descriptors + buffers + prefix are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the clear/count/slot pipelines, the shared four-binding set layout + push range, the descriptor pool + one set, the owned Offsets + List SSBOs
// (List cleared to -1 on a one-shot command buffer), and the owned SurfelPrefixSum sized for the Offsets header. ShaderDirectory locates the three
// SurfelGrid*.comp.spv and the four SurfelPrefix*.comp.spv. CommandPool submits the initial List clear and the function waits. Returns false
// (ReadyCondition stays false, handles null) on any failure. Pair with FinalizeSurfelGridSlotting.
bool InitializeSurfelGridSlotting(SurfelGridSlotting& Slotting,
                                  VulkanHost&         Host,
                                  VkCommandPool       CommandPool,
                                  const char*         ShaderDirectory);

// Record the full grid build against Pool's SurfelBuffer + PoolMaxBuffer: clear -> barrier -> count -> barrier -> scan (4 passes, own barriers) ->
// barrier -> slot, with every mandatory F3 barrier. Re-points the pool bindings if the pool's handles changed. Constants supplies the camera + snapped
// grid origin. On return Offsets holds per-cell END offsets consumed and List holds the packed per-cell surfel indices; the caller barriers them before a
// downstream read. A no-op when either side is not ready. CommandBuffer must be recording, OUTSIDE any rendering scope.
void RecordSurfelGridSlotting(SurfelGridSlotting&            Slotting,
                              const SurfelPool&               Pool,
                              const SurfelSlottingConstants&  Constants,
                              VkCommandBuffer                 CommandBuffer);

// Destroy the pipelines / layout / descriptors / both SSBOs / the owned prefix sum, then reset to empty. The device must be idle. Safe on a
// never-initialized value.
void FinalizeSurfelGridSlotting(SurfelGridSlotting& Slotting);

} // namespace Frontier

#endif
