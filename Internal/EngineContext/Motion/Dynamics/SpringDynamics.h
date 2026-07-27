/*==============================================================================================================================================
                                                               SPRINGDYNAMICS.H
==============================================================================================================================================*/
// 🧩 Velocity spring {Offset, Velocity, RestTarget} for the notch/dock settle; active only while unsettled. POD struct integrated by
//    elapsed time with zero allocation, so it is safe to advance every cycle on a hot path.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MOTION_DYNAMICS_SPRINGDYNAMICS_H
#define FRONTIER_ENGINECONTEXT_MOTION_DYNAMICS_SPRINGDYNAMICS_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Critically-ish damped spring integrated by elapsed time. SettledStatus latches once the offset reaches RestTarget
//    with negligible velocity, at which point MotionEvaluator removes it from the active set — zero idle cost.
struct SpringDynamics
{
    float Offset        = 0.0f;     // [px]   - Current displacement, the value the chrome reads
    float Velocity      = 0.0f;     // [px/s] - Current velocity, seeded by a drag flick on release
    float RestTarget    = 0.0f;     // [px]   - Displacement the spring settles toward
    float Stiffness     = 320.0f;   // [-]    - Spring constant (prototype SPRING_STIFF)
    float Damping       = 34.0f;    // [-]    - Damping coefficient (prototype SPRING_DAMP)
    bool  SettledStatus = true;     // [-]    - True once at rest; false re-arms the spring
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Aim the spring at a rest target and wake it; optionally seed the release velocity from a drag flick.
void EnableSpring(SpringDynamics& Spring, float RestTarget, float ReleaseVelocity) noexcept;

// Integrate one timestep; updates Offset/Velocity and latches SettledStatus when it comes to rest.
void EvaluateSpring(SpringDynamics& Spring, float DeltaSeconds) noexcept;

// Snap the spring directly to an offset and mark it settled (used while dragging, before release).
void AlignSpring(SpringDynamics& Spring, float Offset) noexcept;

} // namespace Frontier

#endif
