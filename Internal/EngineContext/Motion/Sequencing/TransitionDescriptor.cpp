/*==============================================================================================================================================
                                                           TRANSITIONDESCRIPTOR.CPP
==============================================================================================================================================*/
// 🧩 Finite timed transition {Start, Duration, Delay, EasingProfile} — settings slides and tray/dock settles

#include "TransitionDescriptor.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Linear, delay-aware progress in [0,1] for a timestamp; 1 once the ramp has fully elapsed.
float LinearProgress(const TransitionDescriptor& Transition, double Timestamp) noexcept
{
    double Elapsed = Timestamp - Transition.StartTimestamp - (double)Transition.Delay;
    if (Elapsed <= 0.0)
        return 0.0f;
    if (Transition.Duration <= 0.0f)
        return 1.0f;
    float Progress = (float)(Elapsed / (double)Transition.Duration);
    return Progress >= 1.0f ? 1.0f : Progress;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void EnableTransition(TransitionDescriptor& Transition, double Timestamp, float Duration, float Delay) noexcept
{
    Transition.StartTimestamp = Timestamp;
    Transition.Duration       = Duration;
    Transition.Delay          = Delay;
    Transition.ActiveStatus   = true;
}

float EvaluateTransition(TransitionDescriptor& Transition, double Timestamp) noexcept
{
    float Progress = LinearProgress(Transition, Timestamp);
    if (Progress >= 1.0f)
        Transition.ActiveStatus = false;
    return EvaluateEasing(Transition.Profile, Progress);
}

float SampleTransition(const TransitionDescriptor& Transition, double Timestamp) noexcept
{
    return EvaluateEasing(Transition.Profile, LinearProgress(Transition, Timestamp));
}

} // namespace Frontier
