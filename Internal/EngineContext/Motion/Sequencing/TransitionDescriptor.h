/*==============================================================================================================================================
                                                            TRANSITIONDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 Finite timed transition {Start, Duration, Delay, EasingProfile} — settings slides and tray/dock settles. A one-shot eased ramp from
//    0 to 1 over Duration seconds; POD, zero allocation, retired from the active set by MotionEvaluator on completion.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MOTION_SEQUENCING_TRANSITIONDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_MOTION_SEQUENCING_TRANSITIONDESCRIPTOR_H

#include "../Easing/EasingProfile.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A one-shot eased ramp from 0 to 1 over Duration seconds, started at a timestamp. Retired from the active set
//    by MotionEvaluator the cycle EvaluateTransition reports completion, so an idle transition costs nothing.
struct TransitionDescriptor
{
    double        StartTimestamp = 0.0;     // [s]   - Wall time the transition began
    float         Duration       = 0.28f;   // [s]   - Ramp length once the delay has elapsed
    float         Delay          = 0.0f;    // [s]   - Lead time before the ramp starts
    bool          ActiveStatus   = false;   // [-]   - True while the ramp is running
    EasingProfile Profile;                  // [-]   - Timing curve applied to the linear progress
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Begin the transition at the supplied timestamp; marks it active so the evaluator picks it up.
void EnableTransition(TransitionDescriptor& Transition, double Timestamp, float Duration, float Delay) noexcept;

// Advance to the supplied timestamp and write the eased [0,1] output; clears ActiveStatus when complete.
[[nodiscard]] float EvaluateTransition(TransitionDescriptor& Transition, double Timestamp) noexcept;

// Retrieve the eased output for the supplied timestamp without mutating the transition.
[[nodiscard]] float SampleTransition(const TransitionDescriptor& Transition, double Timestamp) noexcept;

} // namespace Frontier

#endif
