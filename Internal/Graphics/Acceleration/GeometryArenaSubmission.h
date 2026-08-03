/*==============================================================================================================================================
                                                        GEOMETRYARENASUBMISSION.H
==============================================================================================================================================*/
// 🧩 Gets the bottom-level trees BuildGeometryTree produces onto the GPU. Every mesh's node blob is concatenated into ONE device buffer (the
//    "arena") and described by a table of per-mesh slices, so the trace binds a fixed number of buffers no matter how many distinct meshes the
//    world holds — which is the whole point, since the open-world target is "many different meshes" and a buffer-per-mesh design would need either
//    a descriptor rebind per mesh or the descriptor-indexing feature this device has available but NOT enabled (VulkanHost.cpp:144). An arena needs
//    neither. This owns buffers only: no pipeline, no dispatch, nothing recorded into a frame.
//
//    📝 The instance side of the two-level structure does NOT live here. An instance names its mesh through SuzanneSceneInstance::MeshOrdinal,
//       which indexes the slice table below, and carries its own InverseModel to enter that mesh's local space. This file only answers "where is
//       mesh N's tree".
//
//    🔴 APPEND AND UPLOAD ARE TWO PHASES AND THAT SPLIT IS LOAD-BEARING. Sizing the arena requires knowing every mesh, so appending must not touch
//       the device — AppendGeometryTreeToArena only accumulates host bytes and computes offsets. One allocation and one staged copy happen in
//       UploadGeometryArena, after the last append. Calling Retrieve before Upload hands back null buffers; the trace's descriptor write then
//       silently binds nothing and every ray misses, so UploadedCondition is the flag that must be checked, not ReadyCondition.
//
//    ⚠️ THE SLICE OFFSETS ARE IN ELEMENTS, NOT BYTES, AND NodeOffset IS IN WORDS WHILE NodeCount IS IN NODES. That asymmetry is inherited from the
//       packed node layout (GeometryTreeWordsPerNode words per node) and is kept rather than normalised, because the traversal shader indexes the
//       node array by WORD and would otherwise multiply on every step. A consumer that treats NodeOffset as a node index reads the fourth node's
//       tail as the root box: well-formed, plausible, and wrong.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_GEOMETRYARENASUBMISSION_H
#define FRONTIER_GRAPHICS_ACCELERATION_GEOMETRYARENASUBMISSION_H

#include "Graphics/Acceleration/GeometryTreeBuild.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One mesh's slice of the arena. The trace resolves an instance's MeshOrdinal to one of these, then walks the node array from NodeOffset. The
//    vertex and index offsets are the mesh's base within the SHARED geometry streams the raster already binds, so a leaf's primitive indices
//    resolve to the right triangles without a second geometry binding.
//
//    🔴 ParentOffset IS GeometryTreeNoParent WHEN THE MESH IS STATIC, not 0. Zero is a legitimate offset (the first dynamic mesh appended owns it),
//       so a consumer testing `ParentOffset == 0` to mean "no parents" would skip refitting exactly the one mesh that needs it. The sentinel is
//       reused from GeometryTreeBuild.h rather than inventing a second spelling of the same idea.
struct GeometryArenaSlice
{
    uint32_t NodeOffset      = 0;   // [-] - first WORD of this mesh's node blob within the arena (not a node index)
    uint32_t NodeCount       = 0;   // [-] - nodes in this mesh's tree
    uint32_t PrimitiveOffset = 0;   // [-] - first entry of this mesh's slice of the primitive-order table
    uint32_t PrimitiveCount  = 0;   // [-] - entries in that slice
    uint32_t VertexOffset    = 0;   // [-] - first RenderVertex of this mesh in the shared vertex stream
    uint32_t IndexOffset     = 0;   // [-] - first index of this mesh in the shared index stream
    uint32_t ParentOffset    = 0;   // [-] - first parent-table entry, or GeometryTreeNoParent when the mesh is static
    uint32_t Padding         = 0;   // [-] - std430 tail pad, holding the record at 32 B
};

// 🔴 Mirrored by the traversal shader as a hand-written struct, exactly like SuzanneSceneInstance's four copies. Pinned so a resize has to be
//    acknowledged rather than silently mis-striding the slice table.
static_assert(sizeof(GeometryArenaSlice) == 32, "GeometryArenaSlice changed size: update the matching struct in the traversal shader.");

// 📝 The arena's owned device resources plus the host-side accumulation the append phase fills. Nothing here is per-frame: it is written once at
//    load and then read by the trace for the lifetime of the scene.
//
//    📝 ParentBuffer is allocated only when at least one appended mesh was built with DynamicCondition. A wholly static scene — which is every
//       scene today, since no skinned mesh exists yet — pays no allocation for it, and the buffer stays null.
struct GeometryArenaSubmission
{
    VulkanHost*    Host                = nullptr;          // [-] - not owned; supplies device / physical device / allocator

    VkBuffer       NodeBuffer          = VK_NULL_HANDLE;   // [-] - device-local concatenated node words for every mesh
    VkDeviceMemory NodeMemory          = VK_NULL_HANDLE;   // [-] - backing allocation for NodeBuffer
    VkBuffer       PrimitiveBuffer     = VK_NULL_HANDLE;   // [-] - device-local concatenated primitive-order entries
    VkDeviceMemory PrimitiveMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for PrimitiveBuffer
    VkBuffer       SliceBuffer         = VK_NULL_HANDLE;   // [-] - device-local GeometryArenaSlice table, indexed by mesh ordinal
    VkDeviceMemory SliceMemory         = VK_NULL_HANDLE;   // [-] - backing allocation for SliceBuffer
    VkBuffer       ParentBuffer        = VK_NULL_HANDLE;   // [-] - device-local child->parent table; null when no mesh is dynamic
    VkDeviceMemory ParentMemory        = VK_NULL_HANDLE;   // [-] - backing allocation for ParentBuffer

    std::vector<uint32_t>           PendingNodeWords;      // [-] - host accumulation, uploaded and then released by UploadGeometryArena
    std::vector<uint32_t>           PendingPrimitiveOrder; // [-] - host accumulation of the permuted primitive indices
    std::vector<uint32_t>           PendingParentTable;    // [-] - host accumulation of parent entries; empty for a fully static scene
    std::vector<GeometryArenaSlice> Slices;                // [-] - one per appended mesh, indexed by the ordinal Append hands back

    bool ReadyCondition    = false;   // [-] - true once the host has a device and the arena can accept appends
    bool UploadedCondition = false;   // [-] - true once the device buffers hold the accumulated bytes; the flag the trace must gate on
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Attach the arena to a host and clear it for accumulation. No device object is created here — allocation is deferred to UploadGeometryArena,
// which is the only point the total size is known. Returns false (ReadyCondition stays false) when the host has no device. Host must be
// provisioned. Pair with FinalizeGeometryArenaSubmission.
bool InitializeGeometryArenaSubmission(GeometryArenaSubmission& Arena, VulkanHost& Host);

// Accumulate one built tree, recording its slice and handing back the mesh ordinal that names it. VertexOffset and IndexOffset are the mesh's base
// within the shared geometry streams. Host-only: allocates nothing on the device and may be called for every mesh before a single upload.
// Returns false when the arena is not ready, the tree is not ReadyCondition, or the tree is internally inconsistent.
//
// 🔴 CALLING THIS AFTER UploadGeometryArena DOES NOT EXTEND THE ARENA. The append succeeds on the host and the device keeps the old bytes, so the
//    new mesh's slice points past the end of a buffer that was sized without it. Refused outright rather than half-honoured.
bool AppendGeometryTreeToArena(GeometryArenaSubmission& Arena,
                               const GeometryTree&      Tree,
                               uint32_t                 VertexOffset,
                               uint32_t                 IndexOffset,
                               uint32_t&                OutMeshOrdinal);

// Allocate the device buffers for everything accumulated and stage it across in one blocking transfer, then release the host copies. A no-op
// returning false when nothing has been appended. CommandPool must be able to allocate a primary command buffer for the graphics queue.
bool UploadGeometryArena(GeometryArenaSubmission& Arena, VkCommandPool CommandPool);

// Report the four device buffers for the trace's descriptor write. ParentBuffer is null for a fully static scene, which the caller must handle —
// a descriptor write of VK_NULL_HANDLE is invalid, so bind a placeholder or omit the binding rather than passing it through.
void RetrieveGeometryArenaBuffers(const GeometryArenaSubmission& Arena,
                                  VkBuffer&                      OutNodeBuffer,
                                  VkBuffer&                      OutPrimitiveBuffer,
                                  VkBuffer&                      OutSliceBuffer,
                                  VkBuffer&                      OutParentBuffer);

// The slice recorded for a mesh ordinal, or a default-constructed slice when the ordinal is out of range. Host-side mirror of what the trace reads
// from SliceBuffer, for validation and for callers that need a mesh's extents without a readback.
GeometryArenaSlice RetrieveGeometryArenaSlice(const GeometryArenaSubmission& Arena, uint32_t MeshOrdinal);

// Destroy the buffers and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeGeometryArenaSubmission(GeometryArenaSubmission& Arena);

} // namespace Frontier

#endif
