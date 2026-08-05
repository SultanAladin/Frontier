/*==============================================================================================================================================
                                                          INSTANCECULLSUBMISSION.H
==============================================================================================================================================*/
// 🧩 The GPU-driven two-pass visibility cull at per-instance granularity: the compute pass that decides which SuzanneSceneInstances the hardware
//    raster actually draws. It owns one InstanceCullSubmission.comp pipeline, an uploaded per-instance PartitionCullRecord SSBO (one record per
//    instance — the mesh-local bounding sphere transformed by the instance's Model, plus the rotated normal cone), and the device-local output
//    buffers the raster consumes: a survivor instance-index list, a VkDrawIndexedIndirectCommand argument whose instanceCount the cull grows, an
//    occlusion re-test list, and an atomic-counter buffer. The pass runs twice a frame — the EARLY pass tests every instance against last frame's
//    HierarchicalDepthPyramid and defers its occlusion-rejects to the re-test list; the LATE pass re-tests only that list against THIS frame's
//    rebuilt pyramid, recovering instances stale depth wrongly hid. Built once (pipeline + layout size-independent), the record SSBO re-uploaded
//    per scene, the HiZ binding re-pointed on resize. POD + free functions; borrows the VulkanHost and the pyramid, owns its own buffers.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_INSTANCECULLSUBMISSION_H
#define FRONTIER_GRAPHICS_VISIBILITY_INSTANCECULLSUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/HierarchicalDepth/HierarchicalDepthPyramid.h"
#include "Graphics/Visibility/SurfacePartition.h"
#include "Graphics/Scene/SuzanneScene.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The cull compute's local workgroup edge — must match local_size_x in InstanceCullSubmission.comp. One lane per cull record; the dispatch
//    rounds the record count up to this multiple.
constexpr uint32_t InstanceCullWorkgroupEdge = 64;


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the cull shader reads, matching InstanceCullSubmission.comp's CullConstants byte-for-byte (std140 push layout): the world ->
//    clip matrix, the six inward frustum planes, the world-space camera origin, the level-0 pyramid extent, the pyramid level count, the record /
//    re-test bound, and the early/late selector. Filled each pass from the camera + pyramid before recording.
struct InstanceCullConstants
{
    float    ViewProjection[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // [-]  - column-major world -> clip
    float    FrustumPlanes[24]  = {};                                       // [-]  - six vec4 planes (xyz inward normal, w distance)
    float    CameraOrigin[4]    = { 0,0,0,0 };                              // [-]  - xyz world camera position (cone view direction); w unused
    float    ViewportExtentX    = 0.0f;                                     // [px] - level-0 pyramid width
    float    ViewportExtentY    = 0.0f;                                     // [px] - level-0 pyramid height
    int32_t  PyramidLevelCount  = 1;                                        // [-]  - mip count (clamps the HiZ lookup level)
    uint32_t RecordCount        = 0;                                        // [-]  - lane bound: record count (early) / re-test count (late)
    uint32_t LatePassEnabled    = 0;                                        // [-]  - 0 = early pass, 1 = late pass
    uint32_t Padding0           = 0;                                        // [-]  - std140 tail pad
    uint32_t Padding1           = 0;                                        // [-]  - std140 tail pad
    uint32_t Padding2           = 0;                                        // [-]  - std140 tail pad
};

// 📝 The device-side draw argument the raster consumes with vkCmdDrawIndexedIndirect — laid out exactly as VkDrawIndexedIndirectCommand. The host
//    seeds IndexCount / FirstIndex / VertexOffset / FirstInstance and zeroes InstanceCount before the early pass; the cull grows InstanceCount by
//    one atomic per survivor. Retained on the host only as the upload template; the live copy lives in ArgumentBuffer.
struct InstanceDrawArgument
{
    uint32_t IndexCount    = 0;   // [-] - indices per instance (the mesh's IndexCount)
    uint32_t InstanceCount = 0;   // [-] - grown by the cull; seeded to 0
    uint32_t FirstIndex    = 0;   // [-] - 0 (whole mesh)
    int32_t  VertexOffset  = 0;   // [-] - 0
    uint32_t FirstInstance = 0;   // [-] - 0
};

// 📝 The cull pass's owned device resources. The pipeline + layout + descriptor set are size-independent (built once). RecordBuffer holds the
//    uploaded per-instance cull records (device-local, staged on scene upload); SurvivorBuffer / RetestBuffer / CounterBuffer / ArgumentBuffer are
//    the compute outputs the raster then reads. RecordCapacity bounds every per-instance buffer. BoundPyramidView caches the HiZ sampled view so the
//    descriptor is re-pointed only when the pyramid rebuilds. ReadyCondition gates recording: false leaves the record path a no-op.
struct InstanceCullSubmission
{
    VulkanHost*           Host             = nullptr;          // [-] - not owned; supplies device / physical device / allocator
    VkPipeline            Pipeline         = VK_NULL_HANDLE;   // [-] - InstanceCullSubmission.comp compute pipeline
    VkPipelineLayout      PipelineLayout   = VK_NULL_HANDLE;   // [-] - descriptor layout + InstanceCullConstants push range
    VkDescriptorSetLayout SetLayout        = VK_NULL_HANDLE;   // [-] - the six SSBO bindings + the pyramid sampler
    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE;   // [-] - sized for one set
    VkDescriptorSet       CullSet          = VK_NULL_HANDLE;   // [-] - the bound cull set

    VkBuffer              RecordBuffer     = VK_NULL_HANDLE;   // [-] - per-instance cull records; HOST_VISIBLE | HOST_COHERENT, so a writer needs no staging copy and no flush
    VkDeviceMemory        RecordMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for RecordBuffer
    VkBuffer              SurvivorBuffer   = VK_NULL_HANDLE;   // [-] - device-local survivor instance-index list (cull writes, raster reads)
    VkDeviceMemory        SurvivorMemory   = VK_NULL_HANDLE;   // [-] - backing allocation for SurvivorBuffer
    VkBuffer              RetestBuffer     = VK_NULL_HANDLE;   // [-] - device-local occlusion re-test list (early writes, late reads)
    VkDeviceMemory        RetestMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for RetestBuffer
    VkBuffer              CounterBuffer    = VK_NULL_HANDLE;   // [-] - device-local atomic counters [survivor cursor, re-test cursor]
    VkDeviceMemory        CounterMemory    = VK_NULL_HANDLE;   // [-] - backing allocation for CounterBuffer
    VkBuffer              ArgumentBuffer   = VK_NULL_HANDLE;   // [-] - device-local VkDrawIndexedIndirectCommand (raster consumes)
    VkDeviceMemory        ArgumentMemory   = VK_NULL_HANDLE;   // [-] - backing allocation for ArgumentBuffer

    VkImageView           BoundPyramidView = VK_NULL_HANDLE;   // [-] - HiZ sampled view the cull set binding 6 points at (re-pointed on change)

    uint32_t              RecordCapacity   = 0;                // [-] - max instances every per-instance buffer is sized for
    uint32_t              RecordCount      = 0;                // [-] - live uploaded record count (early-pass lane bound)
    uint32_t              MeshIndexCount   = 0;                // [-] - seeded into the argument's IndexCount each frame
    bool                  ReadyCondition   = false;            // [-] - true once pipeline + layout + descriptors + buffers are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the compute pipeline, layout, descriptor set layout, pool + one set, and the device-local record / survivor / re-test / counter / argument
// buffers sized for MaxInstances. ShaderDirectory locates InstanceCullSubmission.comp.spv. Returns false (ReadyCondition stays false, handles null)
// on any failure. Host must be provisioned. Pair with FinalizeInstanceCullSubmission.
bool InitializeInstanceCullSubmission(InstanceCullSubmission& Cull,
                                      VulkanHost&             Host,
                                      uint32_t                MaxInstances,
                                      const char*             ShaderDirectory);

// Fit one PartitionCullRecord per instance (mesh-local sphere/cone transformed by each instance's Model) and stage the records into RecordBuffer.
// LocalSphere is the mesh's local bounding sphere as { centre.x, centre.y, centre.z, radius }; LocalCone is its local normal cone as { axis.x,
// axis.y, axis.z, cosine } (cosine -1 = non-coneable). MeshIndexCount is the mesh's index count (seeded into the draw argument). Truncates to
// RecordCapacity. A no-op when ReadyCondition is false. CommandPool submits the staging copy and the function waits, so records are resident on return.
void UploadInstanceCullRecords(InstanceCullSubmission&                  Cull,
                               VkCommandPool                            CommandPool,
                               const std::vector<SuzanneSceneInstance>& Instances,
                               const float                              LocalSphere[4],
                               const float                              LocalCone[4],
                               uint32_t                                 MeshIndexCount);

// Reset the counters + argument (InstanceCount 0, IndexCount seeded) for a fresh frame: records a fill on the device-local counter / argument
// buffers. Must precede the early pass each frame. A no-op when not ready. CommandBuffer must be recording, OUTSIDE any rendering scope.
void ResetInstanceCullFrame(InstanceCullSubmission& Cull, VkCommandBuffer CommandBuffer);

// Record one cull pass. Constants selects early (LatePassEnabled 0) vs late (1) and supplies the camera + frustum + pyramid extent. Pyramid must be
// in SHADER_READ_ONLY. Rebinds the HiZ source when its view changed. Dispatches ceil(Constants.RecordCount / edge) workgroups and barriers the
// output buffers for the downstream raster (indirect + vertex read). A no-op when either side is not ready. CommandBuffer must be recording, OUTSIDE
// any rendering scope.
void RecordInstanceCullPass(InstanceCullSubmission&         Cull,
                            const HierarchicalDepthPyramid& Pyramid,
                            const InstanceCullConstants&    Constants,
                            VkCommandBuffer                 CommandBuffer);

// Destroy the pipeline / layout / descriptors / all buffers, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeInstanceCullSubmission(InstanceCullSubmission& Cull);

} // namespace Frontier

#endif
