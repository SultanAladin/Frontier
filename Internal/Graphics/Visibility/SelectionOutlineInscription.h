/*==============================================================================================================================================
                                                       SELECTIONOUTLINEINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The object-selection outline: a fullscreen composite that reads the R32_UINT visibility buffer and draws a ring exactly where the SELECTED
//    partition (object) ordinal borders a pixel that is not that ordinal. Owns the graphics pipeline (no vertex buffer, dynamic rendering against the
//    swapchain colour format, alpha-over blend, no depth) plus a one-binding descriptor set that samples the visibility image as a usampler2D. The
//    image is BORROWED — its view is bound at Refresh time and re-pointed after every resize, since ReconfigureVisibilityImage rebuilds the view.
//    Records INSIDE the substrate's already-open colour scope, exactly like VisibilityInscription. Named for its mechanism (…Inscription =
//    composites onto an existing target).
//
// 💡 WHY THIS IS DEPTH-CORRECT, AND WHAT IT REPLACES. The conventional outline re-draws the selected object (front-face cull / scaled hull / jump-
//    flood over its silhouette) and composites the ring over the frame. That ring is derived from the object's OWN extent with no knowledge of what
//    occludes it, so an unselected object standing IN FRONT of the selected one gets the ring painted on top of it — the selection appears to float in
//    front of geometry that actually occludes it. Depth-sorting the draws cannot fix it (the ring is not the object), and depth-testing the ring only
//    substitutes a second artefact: the ring drops out wherever it meets the silhouette's own depth discontinuity.
//    This unit reads the ALREADY DEPTH-RESOLVED id buffer instead. The visibility raster wrote it under hardware depth test, so every pixel names the
//    partition nearest the camera there. A pixel therefore belongs to the selected object ONLY IF the selected object won the depth test at that pixel;
//    an occluder in front simply owns those pixels, and the ring is never painted on top of geometry that occludes it. No stencil is allocated and no
//    second geometry pass is recorded — the visibility information is already in the buffers the raster produced.
//
// ⚠️ THE ID BUFFER ALONE IS NOT SUFFICIENT — a second, distinct bug, and the reason this unit also samples DEPTH. "Neighbour has a different ordinal"
//    conflates two opposite situations: the neighbour may sit BEHIND me (a true silhouette edge) or IN FRONT of me (an occlusion contour). Selecting a
//    large background object exposed it plainly — picking the floor ringed every object standing on it, because the floor pixels hugging each silhouette
//    do satisfy "inside AND neighbour differs". Those are honestly the floor's own pixels, so the inner-ring rule was never violated; the outline was
//    simply tracing every hole punched in the floor instead of the floor's outer boundary. Depth is the only signal that separates the two, so the
//    neighbour test compares it and splits the boundary into a VISIBLE stretch (solid ring) and a HIDDEN stretch (the dashed X-ray hint below).
//
// 📝 The dashed X-ray hint. Once occlusion contours are correctly rejected, a selection hidden entirely behind another object draws nothing at all —
//    honest, and what Blender does, but it leaves the user with no cue that their selection still exists. So the hidden stretch of the silhouette is
//    drawn as a dimmed dashed ring instead of being dropped: distinguishable at a glance from the solid visible ring, and suppressible outright via
//    OccludedStyleEnabled = 0 for anyone who prefers the strict behaviour.
//
// 📝 Deliberately OBJECT-only. The shader reads the high partition bits and discards the low 20 primitive bits, so the ring follows whole-object
//    silhouettes. Component (face / edge / vertex) outlining would read the primitive half and is out of scope for this unit.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_SELECTIONOUTLINEINSCRIPTION_H
#define FRONTIER_GRAPHICS_VISIBILITY_SELECTIONOUTLINEINSCRIPTION_H

// 📝 Authoring-only unit, gated on the same flag as ObjectPickReadback (which supplies NoSelectionSentinel below, so the two must
//    gate identically or this header would reference an undeclared constant).
#ifdef FRONTIER_POLYGON_AUTHORING

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityImage.h"
#include "Graphics/Visibility/VisibilityDepth.h"
#include "Graphics/Visibility/ObjectPickReadback.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the fragment stage reads, byte-compatible with the OutlineConstants block in SelectionOutline.frag. The two ordinals carry
//    NoSelectionSentinel when nothing is selected / hovered, which the shader's border test rejects outright. Laid out as 4 uints then 2 vec4s so the
//    std430 rules place the vectors on their natural 16-byte boundaries with no implicit padding.
struct SelectionOutlineConstants
{
    uint32_t SelectedPartition    = NoSelectionSentinel;   // [-] - partition ordinal to outline (NoSelectionSentinel = none)
    uint32_t OutlineThickness     = 2;                     // [px]- ring half-width in pixels (1..4 sane)
    uint32_t HoveredPartition     = NoSelectionSentinel;   // [-] - partition ordinal under the cursor (NoSelectionSentinel = none)
    uint32_t OccludedStyleEnabled = 1;                     // [-] - 1 = dashed X-ray hint where the selection is hidden; 0 = hide it outright
    float    OutlineColour[4]  = { 1.0f, 0.60f, 0.10f, 1.0f };  // [-] - selected ring RGBA (warm amber, the DCC convention)
    float    HoverColour[4]    = { 1.0f, 1.0f,  1.0f,  0.45f }; // [-] - hovered ring RGBA (faint white, drawn under the selection)
    float    OccludedColour[4] = { 1.0f, 0.60f, 0.10f, 0.35f }; // [-] - dashed X-ray RGBA (the selection hue, dimmed — same object, hidden stretch)

    // ⚠️ The occlusion compare runs in LINEAR VIEW METRES, not window depth, and DepthNearPlane is what makes that possible — the shader unprojects
    //    each sampled window depth with z_view = near / (1 − z_win), so this MUST carry the same near plane the frame's projection matrix was built
    //    from or every distance is scaled wrong. A slack in window depth cannot work here: with near=0.01 / far=1000 the whole range beyond 5 m lives
    //    in the last 0.002 of the depth span, so any fraction-of-z_win tolerance dwarfs the occlusion signal it is meant to admit (measured: a 2 m
    //    occlusion moves z_win by 0.000167). That mistake made the depth test a silent no-op and every occlusion contour drew as a solid ring.
    float    DepthNearPlane    = 0.01f;                    // [m] - camera near plane, for the window->view unproject (keep in sync with the lens)
    float    DepthSlackMetres  = 0.05f;                    // [m] - fixed slack: depth quantization + id/depth sub-pixel disagreement at silhouettes
    float    DepthSlackRelative = 0.02f;                   // [-] - proportional slack: perspective foreshortening at distance (fraction of distance)
    float    DashPeriod        = 8.0f;                     // [px]- dash cycle length along the silhouette
    float    DashDutyCycle     = 0.55f;                    // [-] - lit fraction of each dash cycle (0..1)
    float    Pad0              = 0.0f;                     // [-] - std430 tail pad
};

// 📝 The outline's owned device resources. Pipeline + layout, the single-binding descriptor plumbing for the sampled visibility image, and a point
//    sampler (the id must not be filtered). ReadyCondition gates recording. The descriptor set is (re)pointed at the borrowed image's view by
//    RefreshSelectionOutlineInscription — call it once after the image is first ready and again after every resize, because
//    ReconfigureVisibilityImage rebuilds the view and the old handle goes stale.
struct SelectionOutlineInscription
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned; supplies device / allocator
    VkPipeline            Pipeline       = VK_NULL_HANDLE;   // [-] - fullscreen-triangle composite pipeline (dynamic rendering)
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - one sampler set + the OutlineConstants push range
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: binding 0 = combined image sampler (fragment stage)
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - pool sized for one image-sampler set
    VkDescriptorSet       ImageSet       = VK_NULL_HANDLE;   // [-] - the bound set (binding 0 = the visibility image sampler)
    VkSampler             PointSampler   = VK_NULL_HANDLE;   // [-] - nearest / clamp; neither the id nor the depth may be filtered
    VkImageView           BoundIdView    = VK_NULL_HANDLE;   // [-] - the id view ImageSet currently points at; Refresh writes only on change
    VkImageView           BoundDepthView = VK_NULL_HANDLE;   // [-] - the depth view ImageSet currently points at; paired with BoundIdView
    bool                  ReadyCondition = false;            // [-] - true once pipeline + layout + descriptors are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the pipeline, layout, descriptor plumbing, and point sampler. ColourFormat is the swapchain colour format the dynamic-rendering pipeline is
// created against. Reads SelectionOutline.vert.spv / .frag.spv from ShaderDirectory. Returns false (ReadyCondition stays false) if dynamic rendering
// is unavailable, a shader is missing, or any Vulkan step fails. Pair with FinalizeSelectionOutlineInscription.
bool InitializeSelectionOutlineInscription(SelectionOutlineInscription& Inscription,
                                           VulkanHost&                  Host,
                                           VkFormat                     ColourFormat,
                                           const char*                  ShaderDirectory);

// Point the descriptor set at the current id + depth views. A no-op when both already match (cheap to call every frame) or when any side is not
// ready. Must not run while a frame referencing ImageSet is in flight — call at build time and after a resize, with the device idle.
// 📝 Depth is required, not optional: without it the border test cannot tell a silhouette edge from an occlusion contour (see the header note), so a
//    not-ready depth target leaves the descriptors untouched rather than binding the id alone.
void RefreshSelectionOutlineInscription(SelectionOutlineInscription& Inscription,
                                        const VisibilityImage&       Image,
                                        const VisibilityDepth&       Depth);

// Record the outline composite into the caller's ALREADY-OPEN colour scope: bind the pipeline + set, push Constants, draw the fullscreen triangle.
// A no-op when not ready or when Constants selects and hovers nothing (both sentinels), so an idle frame records zero work. CommandBuffer must be
// inside an active dynamic-rendering scope whose colour format matches the one the pipeline was built against.
void RecordSelectionOutlineInscription(const SelectionOutlineInscription& Inscription,
                                       VkExtent2D                         Extent,
                                       const SelectionOutlineConstants&   Constants,
                                       VkCommandBuffer                    CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, and sampler, then reset to empty. The device must be idle. Safe on a never-initialized value.
// Does NOT touch the borrowed visibility image.
void FinalizeSelectionOutlineInscription(SelectionOutlineInscription& Inscription);

} // namespace Frontier

#endif // FRONTIER_POLYGON_AUTHORING

#endif
