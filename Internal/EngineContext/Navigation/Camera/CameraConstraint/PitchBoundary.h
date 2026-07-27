/*==============================================================================================================================================
                                                              PITCHBOUNDARY.H
==============================================================================================================================================*/
// 🧩 Clamps the orbit pitch away from the poles. Without this the eye can swing directly over / under the target, where the view-right axis
//    degenerates (forward becomes parallel to world up) and the orbit flips. ConstrainPitch clamps to a limit just short of ±90°. A pure scalar
//    rule the navigation unit applies after every orbit edit; kept standalone so the limit is defined in one place.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_PITCHBOUNDARY_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_PITCHBOUNDARY_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// The pitch limit in radians, ~89° — just short of the pole so the view basis never degenerates.
constexpr float OrbitPitchLimit = 1.55334f;

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clamp a pitch angle (radians) to [-OrbitPitchLimit, +OrbitPitchLimit]. Returns the constrained value.
[[nodiscard]] float ConstrainPitch(float Pitch) noexcept;

} // namespace Frontier

#endif
