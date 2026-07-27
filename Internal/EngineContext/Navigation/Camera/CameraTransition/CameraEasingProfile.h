/*==============================================================================================================================================
                                                            CAMERAEASINGPROFILE.H
==============================================================================================================================================*/
// 🧩 The timing curve for a keyed camera transition. An EaseTable maps normalized time 0..1 to an eased 0..1 through a chosen easing law
//    (EvaluateKeyedEase) — this is the transition vocabulary from SKILL-Naming, NOT a per-frame smoother. A CameraTransition (see
//    CameraPoseInterpolator) drives a discrete move from one orbit pose to another over a fixed Duration, sampling this table for the blend
//    fraction each frame. Standalone so the curve set is defined in one place and any timed camera animation reuses it.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERAEASINGPROFILE_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERAEASINGPROFILE_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The easing law an EaseTable applies to normalized time. SmoothStep is the default for a natural ease-in-ease-out camera
//    move; the others cover linear moves and asymmetric accents.
enum class EaseLaw
{
    Linear,        // [-] - Constant rate (t → t)
    SmoothStep,    // [-] - Ease-in-ease-out cubic (DEFAULT)
    EaseIn,        // [-] - Slow start, quadratic
    EaseOut        // [-] - Fast start, quadratic
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The curve definition. Law selects the shape; the table is evaluated at a normalized time to yield the eased fraction.
struct EaseTable
{
    EaseLaw Law = EaseLaw::SmoothStep;   // [-] - The easing shape applied to normalized time
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Sample the table: map a normalized time (clamped to 0..1) to an eased 0..1 blend fraction under the table's law.
[[nodiscard]] float EvaluateKeyedEase(const EaseTable& Table, float NormalizedTime) noexcept;

} // namespace Frontier

#endif
