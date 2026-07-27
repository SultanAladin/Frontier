/*==============================================================================================================================================
                                                              MOTIONEVALUATOR.H
==============================================================================================================================================*/
// 🧩 Timer-driven motion: EvaluateActiveMotions(dt) advances only registered, unsettled springs and transitions. The active lists hold
//    borrowed pointers (no ownership, no per-cycle allocation); a settled interface performs zero motion work.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MOTION_MOTIONEVALUATOR_H
#define FRONTIER_ENGINECONTEXT_MOTION_MOTIONEVALUATOR_H

#include "Dynamics/SpringDynamics.h"
#include "Sequencing/TransitionDescriptor.h"

#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Replaces the prototype's perpetual requestAnimationFrame. Chrome registers a spring/transition the moment it
//    wakes; the evaluator advances only the registered set and drops each member the cycle it reaches rest. When
//    nothing is animating both lists are empty, so a settled interface performs zero motion work per cycle.
struct MotionEvaluator
{
    std::vector<SpringDynamics*>       ActiveSprings;       // [-] - Unsettled springs, pruned on settle
    std::vector<TransitionDescriptor*> ActiveTransitions;   // [-] - Running transitions, pruned on completion
    double                             Timestamp = 0.0;     // [s] - Accumulated wall time handed to transitions
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Register a spring as active (no-op if already tracked); call right after EnableSpring.
void RegisterSpring(MotionEvaluator& Motion, SpringDynamics* Spring);

// Register a transition as active (no-op if already tracked); call right after EnableTransition.
void RegisterTransition(MotionEvaluator& Motion, TransitionDescriptor* Transition);

// Advance every registered motion by DeltaSeconds and prune any that have come to rest this cycle.
void EvaluateActiveMotions(MotionEvaluator& Motion, float DeltaSeconds);

// Report whether anything is still animating (used only for diagnostics; idle is the normal case).
[[nodiscard]] bool QueryMotionActivity(const MotionEvaluator& Motion) noexcept;

} // namespace Frontier

#endif
