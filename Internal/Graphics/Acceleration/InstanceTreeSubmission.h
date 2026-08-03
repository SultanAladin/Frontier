/*==============================================================================================================================================
                                                         INSTANCETREESUBMISSION.H
==============================================================================================================================================*/
// 🧩 The TOP level (TLAS) build's last two dispatches: turn the sorted Morton codes into a binary radix tree over the instances
//    (InstanceTreeBuild.comp), then give every node its world-space box with one bottom-up pass (InstanceTreeRefit.comp). Two compute pipelines,
//    three owned device buffers, and no geometry of its own — the caller supplies the sorted key/payload pair the radix sort produced and the same
//    instance + arena buffers the earlier passes read. POD + free functions, mirroring InstanceBoundsSubmission.
//
//    🔴 THIS COMPLETES A FOUR-DISPATCH CHAIN AND THE ORDER IS NOT NEGOTIABLE: reduce (scene box) -> Morton (codes) -> radix sort (order) -> THIS
//       (tree + boxes). Each pass consumes what the previous one wrote, and every one of those hand-offs is a device buffer with a barrier, not a
//       host round trip. Recording this before the sort has completed does not fail — it builds a correct tree over the PREVIOUS frame's ordering,
//       which is a well-formed structure describing a scene that has moved.
//
//    📝 SIZING IS BY INSTANCE COUNT AND NOTHING ELSE, WHICH IS THE WHOLE POINT OF THE TWO-LEVEL SPLIT. A tree over N leaves has exactly N-1 internal
//       nodes, so the node buffer is (2N-1) nodes whatever the meshes contain: 100k instances sharing 12 meshes costs 100k leaves, and the 12
//       meshes' triangle trees are built once at load and never re-touched here. Buffers are allocated once for a capacity and reused every frame —
//       a per-frame rebuild must not allocate.
//
//    ⚠️ THE REFIT'S ARRIVAL COUNTERS MUST BE ZEROED EVERY REBUILD AND THAT RESEED IS PART OF RecordInstanceTreeRefit. A counter left at its previous
//       value makes the first lane to reach a node believe it is the second, so it climbs on a box whose sibling subtree has not landed yet. The
//       result is a box that is correct in most frames and short by one subtree in the frames where the timing lands badly — geometry that flickers
//       out of the trace intermittently, under load, which is close to the worst failure signature a structure like this can have.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_INSTANCETREESUBMISSION_H
#define FRONTIER_GRAPHICS_ACCELERATION_INSTANCETREESUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The workgroup width of both shaders (local_size_x). Must match LanesPerGroup in each.
constexpr uint32_t InstanceTreeLanesPerGroup = 256;

// 📝 Words per node, matching TreeWordsPerNode in both shaders and GeometryTreeWordsPerNode in GeometryTreeBuild.h. The top level writes the same
//    8-word node the bottom level uses so the trace carries ONE node decoder rather than two; see the word-meaning note in InstanceTreeBuild.comp,
//    which documents where the two levels' interpretations diverge.
constexpr uint32_t InstanceTreeWordsPerNode = 8;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block InstanceTreeBuild.comp reads, matching its InstanceTreeConstants byte for byte.
struct InstanceTreeConstants
{
    uint32_t InstanceCount = 0;   // [-] - leaves in the tree; internal nodes are one fewer
};

// 📝 The push block InstanceTreeRefit.comp reads, matching its InstanceTreeRefitConstants byte for byte.
struct InstanceTreeRefitConstants
{
    uint32_t InstanceCount     = 0;            // [-] - leaves in the tree
    uint32_t SliceCount        = 0;            // [-] - entries in the arena's slice table; bounds a valid MeshOrdinal
    uint32_t NoParentSentinel  = 0xFFFFFFFFu;  // [-] - GeometryTreeNoParent; marks the root and terminates the climb
};

// 📝 The submission's owned device resources. Sized once at Initialize for an instance capacity and reused every frame — see the sizing note in the
//    file header.
//
//    🔴 THE DESCRIPTOR SETS ARE WRITTEN AT BIND TIME, NOT RECORD TIME. vkUpdateDescriptorSets on a set already recorded into a live command buffer
//       is undefined, so every bind must happen before recording and not again until the submission has completed.
struct InstanceTreeSubmission
{
    VulkanHost*           Host             = nullptr;          // [-] - not owned; supplies device / physical device / allocator

    VkPipeline            BuildPipeline    = VK_NULL_HANDLE;   // [-] - InstanceTreeBuild.comp
    VkDescriptorSetLayout BuildSetLayout   = VK_NULL_HANDLE;   // [-] - { sorted keys, sorted payloads, tree nodes, tree parents }
    VkPipelineLayout      BuildLayout      = VK_NULL_HANDLE;   // [-] - BuildSetLayout + InstanceTreeConstants push range
    VkDescriptorSet       BuildSet         = VK_NULL_HANDLE;   // [-] - written by BindInstanceTreeSorted

    VkPipeline            RefitPipeline    = VK_NULL_HANDLE;   // [-] - InstanceTreeRefit.comp
    VkDescriptorSetLayout RefitSetLayout   = VK_NULL_HANDLE;   // [-] - { instances, slices, arena nodes, payloads, tree nodes, parents, counters }
    VkPipelineLayout      RefitLayout      = VK_NULL_HANDLE;   // [-] - RefitSetLayout + InstanceTreeRefitConstants push range
    VkDescriptorSet       RefitSet         = VK_NULL_HANDLE;   // [-] - written by BindInstanceTreeScene

    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE;   // [-] - sized for the two sets above

    VkBuffer              NodeBuffer       = VK_NULL_HANDLE;   // [-] - device-local (2*capacity - 1) nodes of InstanceTreeWordsPerNode words
    VkDeviceMemory        NodeMemory       = VK_NULL_HANDLE;   // [-] - backing allocation for NodeBuffer
    VkBuffer              ParentBuffer     = VK_NULL_HANDLE;   // [-] - device-local one parent index per node
    VkDeviceMemory        ParentMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for ParentBuffer
    VkBuffer              CounterBuffer    = VK_NULL_HANDLE;   // [-] - device-local one arrival counter per internal node; reseeded every rebuild
    VkDeviceMemory        CounterMemory    = VK_NULL_HANDLE;   // [-] - backing allocation for CounterBuffer

    uint32_t              InstanceCapacity = 0;                // [-] - max leaves every buffer above is sized for
    uint32_t              InstanceCount    = 0;                // [-] - live leaves set by the last bind; 0 makes the record path a no-op
    uint32_t              SliceCount       = 0;                // [-] - slice-table entries set by the last scene bind
    bool                  SortedBound      = false;            // [-] - true once BindInstanceTreeSorted has written the build set
    bool                  SceneBound       = false;            // [-] - true once BindInstanceTreeScene has written the refit set
    bool                  ReadyCondition   = false;            // [-] - true once pipelines + layouts + descriptors + buffers are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build both compute pipelines, their set/pipeline layouts, the descriptor pool + two sets, and the node / parent / counter buffers sized for
// MaxInstances leaves. ShaderDirectory locates InstanceTreeBuild.comp.spv and InstanceTreeRefit.comp.spv. Returns false (ReadyCondition stays false,
// handles null) when the host has no device, either shader is missing, or an allocation fails. Pair with FinalizeInstanceTreeSubmission.
bool InitializeInstanceTreeSubmission(InstanceTreeSubmission& Tree,
                                      VulkanHost&             Host,
                                      uint32_t                MaxInstances,
                                      const char*             ShaderDirectory);

// Point the build pass at the radix sort's RESULT pair. Records InstanceCount. Must be called before recording, and NOT while a command buffer that
// already bound this set is in flight. A no-op when not ready.
//
// 🔴 PASS THE BUFFERS RetrieveRadixSortedBuffers REPORTS, NOT THE PRIMARY PAIR BY NAME. The sort ping-pongs, and while an even pass count currently
//    lands the answer back in the primary buffers, that is a derived fact rather than a guarantee — RetrieveRadixSortedBuffers exists precisely so a
//    caller never has to know which side won. Binding the wrong side builds a tree over UNSORTED keys, which is well-formed and useless.
bool BindInstanceTreeSorted(InstanceTreeSubmission& Tree,
                            VkBuffer                SortedKeyBuffer,
                            VkDeviceSize            SortedKeyByteSize,
                            VkBuffer                SortedPayloadBuffer,
                            VkDeviceSize            SortedPayloadByteSize,
                            uint32_t                InstanceCount);

// Point the refit pass at the scene: the instance array, the arena's slice and node buffers, and the sorted payloads it resolves leaves through.
// Records SliceCount. Must be called after BindInstanceTreeSorted and before recording. A no-op when not ready.
bool BindInstanceTreeScene(InstanceTreeSubmission& Tree,
                           VkBuffer                InstanceBuffer,
                           VkDeviceSize            InstanceByteSize,
                           VkBuffer                SliceBuffer,
                           VkDeviceSize            SliceByteSize,
                           VkBuffer                ArenaNodeBuffer,
                           VkDeviceSize            ArenaNodeByteSize,
                           VkBuffer                SortedPayloadBuffer,
                           VkDeviceSize            SortedPayloadByteSize,
                           uint32_t                SliceCount);

// Record the tree build: seed the parent table to the no-parent sentinel, a transfer->compute barrier, then one dispatch of
// ceil((InstanceCount-1) / InstanceTreeLanesPerGroup) workgroups, then a compute barrier so the refit sees the shape. A no-op when not ready, the
// sorted pair is unbound, or InstanceCount is below 2. CommandBuffer must be recording, OUTSIDE any rendering scope.
void RecordInstanceTreeBuild(InstanceTreeSubmission& Tree, VkCommandBuffer CommandBuffer);

// Record the bounds refit: zero the arrival counters, a transfer->compute barrier, then one dispatch of
// ceil(InstanceCount / InstanceTreeLanesPerGroup) workgroups walking leaves to root, then a compute barrier so a consumer sees the boxes. A no-op
// when not ready, either bind is missing, or InstanceCount is 0. Must be recorded AFTER RecordInstanceTreeBuild in the same command buffer.
//
// 🔴 THE COUNTER RESEED IS PART OF THIS RECORDING AND MUST NOT BE HOISTED OUT — see the file header for what a stale counter does.
void RecordInstanceTreeRefit(InstanceTreeSubmission& Tree, VkCommandBuffer CommandBuffer);

// Report the device buffers the trace descends: the node blob and the parent table. Valid after a recorded build+refit has completed.
void RetrieveInstanceTreeBuffers(const InstanceTreeSubmission& Tree, VkBuffer& OutNodeBuffer, VkBuffer& OutParentBuffer);

// Copy the tree's nodes and parents back to host memory through a staging buffer, blocking until the copy completes. Intended for the validation
// gate, not for per-frame use. Either output pointer may be null. NodeWordCount is (2*InstanceCount - 1) * InstanceTreeWordsPerNode.
bool RetrieveInstanceTreeReadback(InstanceTreeSubmission& Tree,
                                  VkCommandPool           CommandPool,
                                  uint32_t                InstanceCount,
                                  uint32_t*               OutNodeWords,
                                  uint32_t*               OutParents);

// Destroy the pipelines / layouts / descriptors / buffers, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeInstanceTreeSubmission(InstanceTreeSubmission& Tree);

} // namespace Frontier

#endif
