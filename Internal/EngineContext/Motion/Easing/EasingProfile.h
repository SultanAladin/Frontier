/*==============================================================================================================================================
                                                               EASINGPROFILE.H
==============================================================================================================================================*/
// 🧩 Cubic-bezier easing curve — the prototype's --ease:(.22,.61,.36,1) and friends, evaluated on a normalized progress. A 1-D timing
//    curve y = f(x) over x,y in [0,1] defined by the two control handles of a CSS cubic-bezier. Header-only, POD, zero allocation; the
//    solve is a fixed 8-step Newton inversion so it is safe on a hot per-cycle path.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MOTION_EASING_EASINGPROFILE_H
#define FRONTIER_ENGINECONTEXT_MOTION_EASING_EASINGPROFILE_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A 1-D timing curve y = f(x) over x,y in [0,1], defined by the two control handles of a CSS cubic-bezier.
struct EasingProfile
{
    float FirstHandleX  = 0.22f;   // [0-1] - X of the first bezier control point
    float FirstHandleY  = 0.61f;   // [0-1] - Y of the first bezier control point
    float SecondHandleX = 0.36f;   // [0-1] - X of the second bezier control point
    float SecondHandleY = 1.00f;   // [0-1] - Y of the second bezier control point
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Evaluate the eased output for a normalized progress in [0,1]; clamps outside that range.
[[nodiscard]] float EvaluateEasing(const EasingProfile& Profile, float Progress) noexcept;

// The default workspace ease — cubic-bezier(.22,.61,.36,1), matching the prototype --ease custom property.
[[nodiscard]] constexpr EasingProfile StandardEasingProfile() noexcept
{
    return EasingProfile{ 0.22f, 0.61f, 0.36f, 1.00f };
}

} // namespace Frontier

#endif
