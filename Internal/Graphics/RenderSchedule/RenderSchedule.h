/*==============================================================================================================================================
                                                               RENDERSCHEDULE.H
==============================================================================================================================================*/
// 🧩 The ordered spine the visibility renderer records through. A RenderSchedule owns a fixed, in-order list of named steps; each step is a
//    label plus a record callback with the exact substrate seam signature (VkCommandBuffer, VkExtent2D). RecordRenderSchedule walks the list
//    front-to-back and invokes each callback into the open dynamic-rendering scope. At Phase 0 the schedule carries only the two forward steps
//    that exist today (sky, then grid); every later phase (visibility raster, shade, shadow, GI) appends its own step here rather than growing
//    the recorder lambda. The schedule is a passive container — it holds no GPU resources and allocates nothing per frame once assembled, so a
//    caller can keep it enabled permanently or route around it (the default-OFF legacy path) for an A/B pixel-identity check.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDERSCHEDULE_RENDERSCHEDULE_H
#define FRONTIER_GRAPHICS_RENDERSCHEDULE_RENDERSCHEDULE_H

#include <vulkan/vulkan.h>

#include <functional>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One in-order unit of the frame. Label is a short human tag for diagnostics / capture-tool markers (not shown to a user); Record is the
//    callback that records this unit's commands into the already-open dynamic-rendering scope. The signature matches the substrate's
//    RecordSequence seam exactly, so an existing per-frame record body drops in unchanged. Record must be non-empty for an appended step.
struct RenderScheduleStep
{
    const char*                                              Label = nullptr;   // [-] - Short diagnostic tag (e.g. "sky", "grid", "visibility-raster")
    std::function<void(VkCommandBuffer, VkExtent2D)>         Record;            // [-] - Records this step into the open rendering scope
};

// 📝 The ordered spine. Steps run front-to-back exactly as appended; there is no reordering, priority, or dependency solve at Phase 0 — the
//    order IS the append order, which is the contract the visibility pipeline's phase order depends on. Holds no Vulkan objects, so it needs
//    no device to construct or destroy; FinalizeRenderSchedule just clears the list. EnabledCondition is the default-OFF gate the coordinator
//    reads to choose the schedule path over the legacy inline recorder during the pixel-identity A/B.
struct RenderSchedule
{
    std::vector<RenderScheduleStep> Steps;                     // [-] - The in-order units, walked front-to-back each frame
    bool                            EnabledCondition = false;  // [-] - Default-OFF; when true the coordinator records through this schedule
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Reset the schedule to empty and default-OFF. Cheap; safe to call repeatedly. Does not touch the GPU (the schedule owns no device resources).
void InitializeRenderSchedule(RenderSchedule& Schedule);

// Append one step to the tail of the order. A step with an empty Record callback is rejected (ignored) so a null unit can never be walked.
// Returns the resulting step count so a caller can assert on the assembled length.
size_t AppendRenderScheduleStep(RenderSchedule& Schedule, const char* Label, std::function<void(VkCommandBuffer, VkExtent2D)> Record);

// Walk the steps front-to-back, invoking each Record into the open rendering scope. Empty schedule records nothing. This is the whole of the
// enabled path — the coordinator calls it inside the substrate's RecordSequence once EnabledCondition is set.
void RecordRenderSchedule(const RenderSchedule& Schedule, VkCommandBuffer CommandBuffer, VkExtent2D Extent);

// Drop every step and return to default-OFF. The stored callbacks release their captures here, so it must run before anything they capture by
// reference (e.g. the render coordinator) is torn down.
void FinalizeRenderSchedule(RenderSchedule& Schedule);

} // namespace Frontier

#endif
