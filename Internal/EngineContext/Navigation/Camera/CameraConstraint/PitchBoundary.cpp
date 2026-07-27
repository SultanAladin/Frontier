/*==============================================================================================================================================
                                                              PITCHBOUNDARY.CPP
==============================================================================================================================================*/
// 🧩 The pitch clamp. One line, but it owns the pole-avoidance limit so the value lives in exactly one place.

#include "PitchBoundary.h"

#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ConstrainPitch(float Pitch) noexcept
{
    return std::max(-OrbitPitchLimit, std::min(OrbitPitchLimit, Pitch));
}

} // namespace Frontier
