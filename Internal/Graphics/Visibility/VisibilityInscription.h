/*==============================================================================================================================================
                                                          VISIBILITYINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The visibility inscription: a fullscreen-triangle composite that reads the R32_UINT visibility buffer (VisibilityImage) and writes a debug
//    colour over the already-drawn swapchain colour (sky + grid). It is the on-screen A/B for the hardware visibility raster — with it OFF the
//    presented image is the plain forward view, with it ON the Suzanne heads read as flat coloured silhouettes derived purely from the packed
//    (partition, primitive) ids, so a correct raster is visible at a glance. It owns the graphics pipeline (no vertex buffer, dynamic rendering
//    against the swapchain colour format, alpha-over blend, no depth) plus a one-binding descriptor set that samples the visibility image as a
//    usampler2D. The image is BORROWED — its view is bound into the set at Refresh time (and re-pointed after each resize, since the reconfigure
//    rebuilds the view). Records INSIDE the substrate's already-open colour scope (the GroundGridPass model), unlike the raster which owns its own
//    offscreen scope in the preamble. Built once, host + image borrowed. Named for its mechanism (…Inscription = composites onto a target).

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYINSCRIPTION_H
#define FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYINSCRIPTION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityImage.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the fragment stage reads, byte-compatible with the InscriptionConstants block in VisibilityInscription.frag. One live knob
//    (colour by per-head partition vs per-triangle primitive) plus padding to a 16-byte multiple for std430.
struct VisibilityInscriptionConstants
{
    uint32_t ColourByPrimitive = 0;   // [-] - 0 → colour by partition (per-head), 1 → colour by primitive (per-triangle)
    uint32_t WireframeMode     = 0;    // [-] - 0 → per-triangle wireframe, 1 → per-source-face wireframe (authored ngon/quad/tri topology)
    uint32_t Pad1              = 0;    // [-] - padding
    uint32_t Pad2              = 0;    // [-] - padding
};

// 📝 The inscription's owned device resources. Pipeline + layout, the single-binding descriptor plumbing for the sampled visibility image, and a
//    point sampler (the id must not be filtered). ReadyCondition gates recording. The descriptor set is (re)pointed at the borrowed image's view by
//    RefreshVisibilityInscription — call it once after the image is first ready and again after every resize, because ReconfigureVisibilityImage
//    rebuilds the view and the old handle goes stale.
struct VisibilityInscription
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned; supplies device / allocator
    VkPipeline            Pipeline       = VK_NULL_HANDLE;   // [-] - fullscreen-triangle composite pipeline (dynamic rendering)
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - one sampler set + the InscriptionConstants push range
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: binding 0 = combined image sampler (fragment stage)
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - pool sized for one image-sampler set
    VkDescriptorSet       ImageSet       = VK_NULL_HANDLE;   // [-] - the bound set: binding 0 = image sampler, binding 1 = source-face table
    VkSampler             PointSampler   = VK_NULL_HANDLE;   // [-] - nearest / clamp; the id must not be filtered
    VkImageView           BoundIdView    = VK_NULL_HANDLE;   // [-] - the view ImageSet binding 0 currently points at; Refresh writes only on change
    VkBuffer              SourceFaceBuffer = VK_NULL_HANDLE; // [-] - host-visible storage buffer: primitive ordinal -> authored source-face ordinal
    VkDeviceMemory        SourceFaceMemory = VK_NULL_HANDLE; // [-] - backing allocation for SourceFaceBuffer (host-visible, mapped once at upload)
    VkDeviceSize          SourceFaceCapacity = 0;            // [B] - allocated source-face-buffer size
    uint32_t              SourceFaceCount  = 0;              // [-] - live entries in the source-face table (one per emitted triangle)
    bool                  ReadyCondition = false;            // [-] - true once pipeline + layout + descriptors are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the pipeline, layout, descriptor set layout, pool, sampler, and the one descriptor set (left unpointed until Refresh). ColourFormat is the
// swapchain colour format the dynamic-rendering pipeline composites into. Reads VisibilityInscription.vert.spv / .frag.spv from ShaderDirectory.
// Returns false (ReadyCondition stays false) if dynamic rendering is unavailable, a shader is missing, or any Vulkan step fails. Pair with Finalize.
bool InitializeVisibilityInscription(VisibilityInscription& Inscription,
                                     VulkanHost&            Host,
                                     VkFormat               ColourFormat,
                                     const char*            ShaderDirectory);

// Point the descriptor set at the borrowed visibility image's current view. Call once after the image is first ready and again after every resize
// (ReconfigureVisibilityImage rebuilds the view). A no-op when either side is not ready. The device must be idle (an in-flight frame may still read
// the set). Cheap — one vkUpdateDescriptorSets.
void RefreshVisibilityInscription(VisibilityInscription& Inscription, const VisibilityImage& Image);

// Upload the primitive-ordinal → source-face-ordinal table the topology wireframe reads (SourceFace[t] is the authored editable face display
// triangle t was fan-triangulated from). One entry per emitted triangle, indexed by the primitive ordinal the raster packs into the visibility
// buffer. Written once per scene into a host-visible storage buffer (binding 1) and bound to the set. A no-op when not ready or the table is empty
// (the topology mode then falls back to the per-triangle wireframe, since binding 1 carries nothing). Call once after the scene loads.
void UploadInscriptionSourceFaces(VisibilityInscription& Inscription, const std::vector<uint32_t>& TriangleSourceFace);

// Record one composite into an already-open dynamic-rendering colour scope: set viewport + scissor, bind the pipeline + image set, push the
// constants, and draw the three-vertex fullscreen triangle. The visibility image must already be in SHADER_READ_ONLY (see
// TransitionVisibilityImageForSampling). A no-op when not ready. CommandBuffer must be INSIDE the colour scope (records over sky + grid).
void RecordVisibilityInscription(const VisibilityInscription&          Inscription,
                                 VkExtent2D                            Extent,
                                 const VisibilityInscriptionConstants& Constants,
                                 VkCommandBuffer                       CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, and sampler, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeVisibilityInscription(VisibilityInscription& Inscription);

} // namespace Frontier

#endif
