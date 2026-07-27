/*==============================================================================================================================================
                                                             MOTIONEVALUATOR.CPP
==============================================================================================================================================*/
// 🧩 Timer-driven motion: EvaluateActiveMotions(dt) advances only registered, unsettled springs and transitions

#include "MotionEvaluator.h"

#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void RegisterSpring(MotionEvaluator& Motion, SpringDynamics* Spring)
{
    if (Spring == nullptr)
        return;
    if (std::find(Motion.ActiveSprings.begin(), Motion.ActiveSprings.end(), Spring) == Motion.ActiveSprings.end())
        Motion.ActiveSprings.push_back(Spring);
}

void RegisterTransition(MotionEvaluator& Motion, TransitionDescriptor* Transition)
{
    if (Transition == nullptr)
        return;
    if (std::find(Motion.ActiveTransitions.begin(), Motion.ActiveTransitions.end(), Transition) == Motion.ActiveTransitions.end())
        Motion.ActiveTransitions.push_back(Transition);
}

void EvaluateActiveMotions(MotionEvaluator& Motion, float DeltaSeconds)
{
    Motion.Timestamp += (double)DeltaSeconds;

    for (SpringDynamics* Spring : Motion.ActiveSprings)
        EvaluateSpring(*Spring, DeltaSeconds);
    Motion.ActiveSprings.erase(
        std::remove_if(Motion.ActiveSprings.begin(), Motion.ActiveSprings.end(),
                       [](const SpringDynamics* Spring) { return Spring->SettledStatus; }),
        Motion.ActiveSprings.end());

    for (TransitionDescriptor* Transition : Motion.ActiveTransitions)
        (void)EvaluateTransition(*Transition, Motion.Timestamp);   // advance for the side effect; eased output unused here
    Motion.ActiveTransitions.erase(
        std::remove_if(Motion.ActiveTransitions.begin(), Motion.ActiveTransitions.end(),
                       [](const TransitionDescriptor* Transition) { return !Transition->ActiveStatus; }),
        Motion.ActiveTransitions.end());
}

bool QueryMotionActivity(const MotionEvaluator& Motion) noexcept
{
    return !Motion.ActiveSprings.empty() || !Motion.ActiveTransitions.empty();
}

} // namespace Frontier
