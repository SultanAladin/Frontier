/*==============================================================================================================================================
                                                              RENDERSCHEDULE.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the ordered render spine. All four operations are container work over the step list — no Vulkan calls, no allocation
//    beyond the vector's own growth. RecordRenderSchedule is the hot path: a straight front-to-back walk that forwards the command buffer and
//    extent into each step's callback. Kept deliberately trivial so the enabled path is provably equivalent to the legacy inline recorder when
//    the same steps are appended in the same order.

#include "Graphics/RenderSchedule/RenderSchedule.h"

#include <utility>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeRenderSchedule(RenderSchedule& Schedule)
{
    Schedule.Steps.clear();
    Schedule.EnabledCondition = false;
}

size_t AppendRenderScheduleStep(RenderSchedule& Schedule, const char* Label, std::function<void(VkCommandBuffer, VkExtent2D)> Record)
{
    // A step with no work is not a step — reject an empty callback so the walk never dereferences a null std::function.
    if (Record)
        Schedule.Steps.push_back(RenderScheduleStep{ Label, std::move(Record) });
    return Schedule.Steps.size();
}

void RecordRenderSchedule(const RenderSchedule& Schedule, VkCommandBuffer CommandBuffer, VkExtent2D Extent)
{
    for (const RenderScheduleStep& Step : Schedule.Steps)
        Step.Record(CommandBuffer, Extent);
}

void FinalizeRenderSchedule(RenderSchedule& Schedule)
{
    Schedule.Steps.clear();
    Schedule.EnabledCondition = false;
}

} // namespace Frontier
