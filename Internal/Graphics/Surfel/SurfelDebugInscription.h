/*==============================================================================================================================================
                                                          SURFELDEBUGINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The surfel DEBUG view — the eyeballable half of the Phase-1 definition of done. It splats every LIVE surfel in the pool as a screen-space disc,
//    coloured by a number-key-selected mode, composited over the shaded scene inside the radiance scope (like GroundGridPass). With it on you can see
//    at a glance whether surfels cover every head mesh AND the floor slab, whether they cluster where the camera looks, and whether they age and recycle
//    — none of which the trace or the shade will reveal until Phase 3, and all of which Phase 1 must prove.
//
//    🔴 DEBUG-ONLY, OFF BY DEFAULT (DebugMode 0). Reads the pool's surfel SSBO and the slotting's Offsets header; writes nothing back. It is not a
//       rendering path — Phase 2/3 never consume it. The draw is a point list of exactly SurfelMaxCount vertices with no vertex buffer: the vertex stage
//       reads Surfels[gl_VertexIndex], culls the dead / never-spawned slots off screen, and sizes each point from the surfel's world radius. See the .vert.
//
//    🔴 THE MODE IS A 0-9 NUMBER KEY (user-requested). The host maps a key press to DebugMode: 0 off · 1 age (cold->hot toward TTL) · 2 cascade (one hue
//       per level) · 3 identity (one hue per slot, so coverage density is legible) · 4 cell-occupancy heatmap (end-prevEnd on the scanned Offsets). 5-9
//       are reserved for the later phases' radiance / trace views and draw nothing here. RenderExtension owns the key latch and passes the resolved mode in.
//
//    📝 …Inscription because it composites onto a target and records no dispatch of its own (it is a graphics draw into an open scope). POD struct + free
//       functions, mirroring GroundGridPass / SurfaceShadeInscription. The pool + slotting buffers are BORROWED; this unit owns only its pipeline plumbing.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELDEBUGINSCRIPTION_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELDEBUGINSCRIPTION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelPool.h"
#include "Graphics/Surfel/SurfelGridSlotting.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The number-key debug modes, matching the branch in SurfelDebugSplat.vert. 0 draws nothing (the inscription early-outs), 1-4 are the Phase-1
//    coverage views, 5-9 are reserved for the trace / radiance views later phases add. Kept as an enum so the host key handler and the shader agree.
enum SurfelDebugMode : uint32_t
{
    SurfelDebugModeOff        = 0,   // draw nothing
    SurfelDebugModeAge        = 1,   // cold -> hot toward TTL
    SurfelDebugModeCascade    = 2,   // one hue per cascade level
    SurfelDebugModeIdentity   = 3,   // one hue per slot (coverage density)
    SurfelDebugModeOccupancy  = 4,   // cell fill heatmap (scanned Offsets)
    SurfelDebugModeCount      = 5,   // first reserved slot
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the splat reads, byte-compatible with SurfelDebugConstants in SurfelDebugSplat.vert. ViewProjection projects each surfel's world
//    position to clip; CameraPosition drives the disc radius (the grid's own eye-distance radius); GridOrigin is the SNAPPED grid origin the slotting
//    used this frame, so the cascade / occupancy modes recover the same eye-relative cell the build did. ScreenAndRadius packs (width, height, radius
//    scale, unused); DebugMode is the selected number key; Capacity is the pool capacity == the draw's vertex count.
struct SurfelDebugConstants
{
    Matrix4f ViewProjection;                                   // [-]  - world -> clip (column-major)
    float    CameraPosition[4]  = { 0, 0, 0, 0 };              // [m]  - raw eye; w unused
    float    GridOrigin[4]      = { 0, 0, 0, 0 };              // [m]  - snapped grid origin; w unused
    float    ScreenAndRadius[4] = { 1.0f, 1.0f, 1.0f, 0.0f };  // [-]  - x width, y height, z radius scale, w unused
    uint32_t DebugMode          = SurfelDebugModeOff;          // [-]  - selected number key (0 off)
    uint32_t Capacity           = 0;                           // [-]  - pool capacity == vertex count
    uint32_t Pad0               = 0;
    uint32_t Pad1               = 0;
};

// 📝 The debug view's owned device resources. One alpha-blended point-list graphics pipeline (dynamic rendering into the radiance colour format), a
//    two-binding storage set (b0 surfel records, b1 the slotting Offsets header for the occupancy mode), and the set itself. Bound* cache the borrowed
//    pool / slotting handles so the descriptor is re-pointed only on change. Owns no buffers — everything it reads is borrowed.
struct SurfelDebugInscription
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned
    VkPipeline            Pipeline       = VK_NULL_HANDLE;   // [-] - point-list splat pipeline (alpha-over, no depth)
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - the storage set + SurfelDebugConstants push range
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: b0 surfels, b1 offsets
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - one set
    VkDescriptorSet       SplatSet       = VK_NULL_HANDLE;   // [-] - the bound set

    VkBuffer              BoundSurfelBuffer  = VK_NULL_HANDLE;  // [-] - the borrowed pool SurfelBuffer at b0
    VkBuffer              BoundOffsetsBuffer = VK_NULL_HANDLE;  // [-] - the borrowed slotting OffsetsBuffer at b1

    uint32_t              Capacity       = 0;                  // [-] - pool capacity; the draw's vertex count
    bool                  ReadyCondition = false;              // [-] - true once pipeline + layout + descriptors are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the point-list splat pipeline for the given radiance colour format, the two-binding storage set layout + pool + set (left unpointed until
// Refresh), and the pipeline layout carrying the push range. Loads SurfelDebugSplat.vert.spv / .frag.spv from ShaderDirectory. Returns false
// (ReadyCondition stays false, handles null) if dynamic rendering is unavailable, a module is missing, or any Vulkan step fails. Pair with Finalize.
bool InitializeSurfelDebugInscription(SurfelDebugInscription& Debug,
                                      VulkanHost&             Host,
                                      VkFormat                ColourFormat,
                                      const char*             ShaderDirectory);

// Point the storage set at the borrowed pool SurfelBuffer (b0) and the slotting OffsetsBuffer (b1), and latch the pool capacity as the draw's vertex
// count. Idempotent — a no-op when both handles already match, so it is safe to call every frame; it writes the descriptor only after a rebuild changed
// a handle. The device must be idle when a handle actually changes. Must be called at least once before recording.
void RefreshSurfelDebugInscription(SurfelDebugInscription&   Debug,
                                   const SurfelPool&         Pool,
                                   const SurfelGridSlotting& Slotting);

// Record one debug splat into an already-open dynamic-rendering colour scope: set viewport + scissor, bind the pipeline + set, push the constants, and
// draw Capacity points. A no-op when not ready OR when Constants.DebugMode is 0 (the off state costs nothing). The surfel + offsets buffers must already
// be readable (the lifecycle + slotting for this frame have run and been barriered). CommandBuffer must be INSIDE the radiance scope, AFTER the shade.
void RecordSurfelDebugInscription(const SurfelDebugInscription& Debug,
                                  VkExtent2D                    Extent,
                                  const SurfelDebugConstants&   Constants,
                                  VkCommandBuffer               CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeSurfelDebugInscription(SurfelDebugInscription& Debug);

} // namespace Frontier

#endif
