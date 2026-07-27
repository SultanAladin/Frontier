/*==============================================================================================================================================
                                                              SPRINGDYNAMICS.CPP
==============================================================================================================================================*/
// 🧩 Velocity spring {Offset, Velocity, RestTarget} for the notch/dock settle; active only while unsettled

#include "SpringDynamics.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
constexpr float OffsetRestThreshold   = 0.5f;    // [px]   - Below this distance to target counts as arrived
constexpr float VelocityRestThreshold = 4.0f;    // [px/s] - Below this speed counts as stopped
constexpr float MaximumTimestep       = 0.032f;  // [s]    - Clamp to avoid instability after a stall
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void EnableSpring(SpringDynamics& Spring, float RestTarget, float ReleaseVelocity) noexcept
{
    Spring.RestTarget    = RestTarget;
    Spring.Velocity      = ReleaseVelocity;
    Spring.SettledStatus = false;
}

void EvaluateSpring(SpringDynamics& Spring, float DeltaSeconds) noexcept
{
    if (Spring.SettledStatus)
        return;

    float Step = DeltaSeconds > MaximumTimestep ? MaximumTimestep : DeltaSeconds;

    // 💡 Hooke's law with viscous damping, semi-implicit Euler: F = -k·x - c·v, integrated velocity-first for stability.
    float Displacement   = Spring.Offset - Spring.RestTarget;
    float Acceleration   = -Spring.Stiffness * Displacement - Spring.Damping * Spring.Velocity;
    Spring.Velocity     += Acceleration * Step;
    Spring.Offset       += Spring.Velocity * Step;

    float Remaining = Spring.Offset - Spring.RestTarget;
    if ((Remaining < OffsetRestThreshold && Remaining > -OffsetRestThreshold) &&
        (Spring.Velocity < VelocityRestThreshold && Spring.Velocity > -VelocityRestThreshold))
    {
        Spring.Offset        = Spring.RestTarget;
        Spring.Velocity      = 0.0f;
        Spring.SettledStatus = true;
    }
}

void AlignSpring(SpringDynamics& Spring, float Offset) noexcept
{
    Spring.Offset        = Offset;
    Spring.Velocity      = 0.0f;
    Spring.SettledStatus = true;
}

} // namespace Frontier
