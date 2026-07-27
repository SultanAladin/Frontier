/*============================================================================================================================================
                                                           BUFFERALLOCATION.H
============================================================================================================================================*/
// 🧩 The explicit GPU memory claim for a display polygon: takes the throwaway RenderVertexStream the Geometry presentation
//    produces (interleaved float vertices + a triangle-list index buffer) and lands it in DEVICE-LOCAL Vulkan buffers ready to
//    bind and draw. The data is static between edits, so it is staged once — written to a host-visible scratch buffer, then
//    copied into device-local memory through a one-shot transfer — rather than left host-visible like a per-frame uniform block.
//    That keeps the hot draw path reading the fastest memory the GPU has, which is the long-term-performance choice this
//    subsystem is held to. Raw Vulkan, no VMA, against the shared VulkanHost.
// 📝 This brick is the upload boundary only: it never triangulates, dedups, or inspects topology — the presentation stage already
//    did that. It consumes RenderVertexStream (the engine's single polygon presentation contract, declared in PolygonCluster.h
//    and never redefined here) and exposes the bind handles + draw count a pipeline needs. One PolygonBufferAllocation owns one
//    polygon's geometry for its lifetime; reconstruct it when the stream changes.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_RESOURCES_BUFFERALLOCATION_H
#define FRONTIER_GRAPHICS_RENDER_RESOURCES_BUFFERALLOCATION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Authoring/Geometry/Modeling/PolygonCluster.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One display polygon's device-local geometry: a vertex buffer (interleaved RenderVertex, stride 32 — position @0, normal
//    @12, texcoord @24) and an index buffer (uint32 triangle list), each with its own backing allocation. IndexCount is the
//    draw count handed to vkCmdDrawIndexed; VertexCount is retained for validation and a future non-indexed path. Both
//    buffers carry TRANSFER_DST (they are copy targets) plus their VERTEX/INDEX usage. A default-constructed value is the
//    empty/unallocated case — every handle VK_NULL_HANDLE, both counts zero — which ConstructPolygonBufferAllocation produces
//    for an empty stream and which ReleasePolygonBufferAllocation restores.
struct PolygonBufferAllocation
{
    VkBuffer       VertexBuffer       = VK_NULL_HANDLE;   // [-]   - device-local interleaved RenderVertex buffer
    VkDeviceMemory VertexMemory       = VK_NULL_HANDLE;   // [-]   - backing allocation for VertexBuffer
    VkBuffer       IndexBuffer        = VK_NULL_HANDLE;   // [-]   - device-local uint32 triangle-list index buffer
    VkDeviceMemory IndexMemory        = VK_NULL_HANDLE;   // [-]   - backing allocation for IndexBuffer
    uint32_t       VertexCount        = 0;                // [-]   - RenderVertices uploaded (for validation / non-indexed)
    uint32_t       IndexCount         = 0;                // [-]   - indices uploaded; the vkCmdDrawIndexed draw count
    VkDeviceSize   VertexByteCapacity = 0;                // [B]   - allocated vertex-buffer size (>= VertexCount * stride)
    VkDeviceSize   IndexByteCapacity  = 0;                // [B]   - allocated index-buffer size (>= IndexCount * 4)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Claim device-local vertex + index buffers for Stream and stage its bytes into them, leaving Result ready to bind and draw.
// Host must already be provisioned (logical device + graphics queue); the transfer is submitted on Host.GraphicsQueue
// through CommandPool (a pool created against Host.GraphicsQueueFamily) and the function waits on a fence so the buffers are
// fully resident before it returns — no caller-side synchronization is required.
//
// Two-phase, no-partial-writes contract: Result is released to its empty value first, then populated only if every Vulkan
// step succeeds; any failure releases whatever was claimed and leaves Result empty (returns false). An EMPTY stream (no
// vertices or no indices) is a success: Result is left empty and the function returns true, so an empty polygon is a no-op draw
// rather than an error. Indices are not bounds-checked against VertexCount here — the presentation stage guarantees that.
bool ConstructPolygonBufferAllocation(VulkanHost&               Host,
                                      VkCommandPool             CommandPool,
                                      const RenderVertexStream& Stream,
                                      PolygonBufferAllocation&  Result);

// Destroy both buffers and free both allocations, then reset Allocation to its empty value. Safe on an already-empty value
// (every branch is null-guarded) and idempotent. The device must be idle, or the caller must otherwise guarantee no frame in
// flight still references these buffers.
void ReleasePolygonBufferAllocation(VulkanHost& Host, PolygonBufferAllocation& Allocation);

} // namespace Frontier

#endif
