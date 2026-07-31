/*==============================================================================================================================================
                                                            OBJECTPICKREADBACK.H
==============================================================================================================================================*/
// 🧩 The one-pixel identity copy-back that turns a cursor position into a partition (object) ordinal. Owns a tiny host-visible ring of staging
//    buffers — one slot per in-flight frame — and records a single vkCmdCopyImageToBuffer of ONE texel from the R32_UINT visibility buffer at the
//    cursor. The slot written on frame N is read on frame N + FramesInFlight, by which point its fence has been waited on by the substrate, so the
//    read never races the GPU and never stalls the pipeline with a vkQueueWaitIdle.
//
// 💡 WHY A COPY-BACK AND NOT A RAYCAST. A CPU ray test costs O(triangles) per query and must rebuild or walk an acceleration structure as geometry
//    changes. The visibility raster ALREADY resolved, per pixel, which surface is nearest — that work is paid for whether or not anything is picked.
//    Reading one texel is O(1) in the polygon count: picking a 10M-triangle model costs exactly what picking a cube costs. It is also automatically
//    consistent with what the user sees, because it samples the very buffer the frame was derived from — a raycast can disagree with the rendered
//    image wherever the two use different tolerances.
//
// ⚠️ ONE FRAME OF LATENCY is inherent: the copy is recorded during frame N and the bytes are host-visible only after frame N completes. For hover
//    and click this is imperceptible (the cursor moves a few pixels at most). It does mean a click is resolved against the frame the user was LOOKING
//    at when they clicked, which is the correct semantic — resolving against the frame in flight would use a camera the user never saw.
//
// 📝 Object AND primitive. The copy always captured the whole 32-bit word — the primitive half was simply masked off and discarded, so surfacing it
//    for the component (vertex / edge / face) modes costs one extra field and no extra GPU work. The component modes additionally need the hit
//    POSITION to decide WHICH corner or edge is nearest the cursor; that is resolved on the CPU against the one triangle the primitive ordinal names
//    (see ResolveComponentPick), which is O(1) rather than the O(triangles) a raycast would cost.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_OBJECTPICKREADBACK_H
#define FRONTIER_GRAPHICS_VISIBILITY_OBJECTPICKREADBACK_H

// 📝 Authoring-only unit. Selection exists to serve the polygon-authoring (modelling) tools, so the whole body compiles out unless
//    FRONTIER_POLYGON_AUTHORING is defined — a runtime-only renderer carries none of the staging ring, and the RenderExtension call
//    sites are gated on the same flag. Mirrors how ClipmapFieldInspection gates itself on FRONTIER_DEVELOPMENT_PROFILE.
#ifdef FRONTIER_POLYGON_AUTHORING

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityImage.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The "no object" ordinal. The raster packs the partition into 12 bits, so 0xFFFFFFFF is unreachable and unambiguous as an empty marker.
constexpr uint32_t NoSelectionSentinel = 0xFFFFFFFFu;

// 📝 Primitive occupies the low 20 bits (VisibilityRaster.frag); the partition ordinal is what remains above it.
constexpr uint32_t PickPrimitiveBits = 20u;

// 📝 Slots in the staging ring. MUST be >= WindowSubstrate::FramesInFlight, or a slot could be read while its copy is still outstanding.
constexpr uint32_t PickReadbackSlots = 3;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One staging slot: a 4-byte host-visible buffer plus the cursor it was recorded for and whether a copy is outstanding. PendingCondition
//    distinguishes "not yet written" from "holds a stale id", so the first frames before any copy has landed report no selection rather than garbage.
struct PickReadbackSlot
{
    VkBuffer       StagingBuffer    = VK_NULL_HANDLE;   // [-]  - 4-byte TRANSFER_DST, host-visible + coherent
    VkDeviceMemory StagingMemory    = VK_NULL_HANDLE;   // [-]  - backing allocation
    uint32_t*      MappedIdentity   = nullptr;          // [-]  - persistently mapped 4-byte window into StagingBuffer
    int32_t        RecordedCursorX  = -1;               // [px] - cursor the copy was recorded at (diagnostics)
    int32_t        RecordedCursorY  = -1;               // [px] - cursor the copy was recorded at (diagnostics)
    bool           PendingCondition = false;            // [-]  - true once a copy has been recorded into this slot
};

// 📝 The readback ring. Host borrowed. WriteCursor round-robins the slots; the slot it is about to overwrite is the OLDEST, hence the one whose copy
//    has certainly completed — that is the slot read this frame. ResolvedPartition carries the most recent successfully read ordinal.
struct ObjectPickReadback
{
    VulkanHost*      Host              = nullptr;                 // [-] - not owned; supplies device / physical device / allocator
    PickReadbackSlot Slots[PickReadbackSlots];                    // [-] - the staging ring
    uint32_t         WriteCursor       = 0;                       // [-] - next slot to record into
    uint32_t         ResolvedPartition = NoSelectionSentinel;     // [-] - newest read partition ordinal (NoSelectionSentinel = empty pixel)
    uint32_t         ResolvedPrimitive = NoSelectionSentinel;     // [-] - newest read primitive (triangle) ordinal, for the component modes
    bool             ReadyCondition    = false;                   // [-] - true once every slot is live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate the staging ring and map every slot. Returns false (ReadyCondition stays false, handles null) on any Vulkan failure. Host must be
// provisioned. Pair with FinalizeObjectPickReadback.
bool InitializeObjectPickReadback(ObjectPickReadback& Readback, VulkanHost& Host);

// Record a one-texel copy from Image at (CursorX, CursorY) into the current write slot, then advance the cursor. The image must be in
// TRANSFER_SRC_OPTIMAL — call TransitionVisibilityImageForPickCopy first. A no-op when not ready, when the image is not ready, or when the cursor
// lies outside the image. CommandBuffer must be recording and OUTSIDE any dynamic-rendering scope (a copy is not a draw).
void RecordObjectPickCopy(ObjectPickReadback& Readback,
                          VisibilityImage&    Image,
                          int32_t             CursorX,
                          int32_t             CursorY,
                          VkCommandBuffer     CommandBuffer);

// Read the OLDEST slot — the one whose copy has completed — and update ResolvedPartition. Returns the partition ordinal, or NoSelectionSentinel when
// the pixel was empty or no copy has landed yet. Call once per frame BEFORE RecordObjectPickCopy overwrites that slot.
uint32_t ResolveObjectPickIdentity(ObjectPickReadback& Readback);

// Barrier Image from COLOR_ATTACHMENT (where the raster leaves it) to TRANSFER_SRC_OPTIMAL so the one-texel copy can read it, updating
// CurrentLayout. A no-op when the image is not ready. Records into CommandBuffer, outside any rendering scope.
void TransitionVisibilityImageForPickCopy(VisibilityImage& Image, VkCommandBuffer CommandBuffer);

// Unmap + destroy every slot and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeObjectPickReadback(ObjectPickReadback& Readback);

} // namespace Frontier

#endif // FRONTIER_POLYGON_AUTHORING

#endif
