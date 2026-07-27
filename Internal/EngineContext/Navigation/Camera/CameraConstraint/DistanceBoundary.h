/*==============================================================================================================================================
                                                            DISTANCEBOUNDARY.H
==============================================================================================================================================*/
// 🧩 Clamps the orbit distance to a positive range. Dolly must never pull the eye through the target (Distance <= 0 flips the view) nor push it
//    past the far clip. ConstrainDistance holds it within [MinimumOrbitDistance, MaximumOrbitDistance]. Pure scalar rule applied after every dolly
//    edit; standalone so the range is defined once.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_DISTANCEBOUNDARY_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_DISTANCEBOUNDARY_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// The closest the eye may sit to the target — always positive so the view never flips through the pivot.
constexpr float MinimumOrbitDistance = 0.05f;   // [m]

// The farthest the eye may pull back — kept inside a sane fraction of the default far clip.
constexpr float MaximumOrbitDistance = 500.0f;  // [m]

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clamp an orbit distance (m) to [MinimumOrbitDistance, MaximumOrbitDistance]. Returns the constrained value.
[[nodiscard]] float ConstrainDistance(float Distance) noexcept;

} // namespace Frontier

#endif
