/*==============================================================================================================================================
                                                          TRANSFORMALIGNMENT.H
==============================================================================================================================================*/
// 🧩 Derives the target camera orbit pose from an AlignmentPreset and drives the eased snap toward it — the C++ side of the mockup's `snapTo(name)`
//    plus the CSS `.rig transition .42s`. A snap captures the live Yaw/Pitch as the start, resolves the preset's framed Yaw/Pitch as the end, and
//    interpolates on the shortest arc over the configured duration with a smooth-step ease (mirrors CameraEaseTable's SmoothStep). Operates on the
//    Interface-layer ViewportCamera the viewport actually holds. Header-only; the .cpp owns one OrientationTransition and advances it each cycle.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_TRANSFORMALIGNMENT_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_TRANSFORMALIGNMENT_H

#include "../Configuration/SphericalCoordinateTable.h"

#include <math.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 An in-flight eased snap of the orbit angles. StartYaw/Pitch → EndYaw/Pitch over Duration seconds, smooth-stepped. Engaged
//    is false once complete or before one is armed. Only Yaw + Pitch animate (the mockup snaps orientation only, not distance).
struct OrientationTransition
{
    float StartYaw   = 0.0f;    // [rad] - Yaw at Elapsed 0
    float StartPitch = 0.0f;    // [rad] - Pitch at Elapsed 0
    float EndYaw     = 0.0f;    // [rad] - Yaw at Elapsed Duration
    float EndPitch   = 0.0f;    // [rad] - Pitch at Elapsed Duration
    float Duration   = 0.42f;   // [s]   - Total snap time (mockup .42s)
    float Elapsed    = 0.0f;    // [s]   - Time accumulated
    bool  Engaged    = false;   // [-]   - True while the snap is running
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Smooth-step ease-in-ease-out on a normalized 0..1 time — the SmoothStep law from CameraEaseTable, reproduced here so the
//    overlay animates identically without depending on the Navigation-layer camera type.
inline float EvaluateSmoothStep(float NormalizedTime)
{
    if (NormalizedTime < 0.0f) { NormalizedTime = 0.0f; }
    if (NormalizedTime > 1.0f) { NormalizedTime = 1.0f; }
    return NormalizedTime * NormalizedTime * (3.0f - 2.0f * NormalizedTime);
}

// 📝 Shortest-arc blend of two angles (radians) at a fraction 0..1 — so a snap never spins the long way around a wrap.
inline float InterpolateShortestArc(float StartAngle, float EndAngle, float Fraction)
{
    const float Pi     = 3.14159265358979f;
    const float TwoPi  = 6.28318530717959f;
    float Delta = fmodf(EndAngle - StartAngle, TwoPi);
    if (Delta >  Pi) { Delta -= TwoPi; }
    if (Delta < -Pi) { Delta += TwoPi; }
    return StartAngle + Delta * Fraction;
}

// 📝 Arm an eased snap from the current orbit angles toward the preset's framed pose. Resolves the end Yaw/Pitch from the
//    orientation table (Yaw = -Azimuth, Pitch = -Elevation) and engages the transition. The mockup's `snapTo`.
inline void ArmOrientationSnap(OrientationTransition& Transition,
                               float CurrentYaw,
                               float CurrentPitch,
                               AlignmentPreset Preset,
                               float Duration)
{
    float EndYaw = 0.0f, EndPitch = 0.0f;
    ResolvePresetCameraPose(Preset, EndYaw, EndPitch);

    Transition.StartYaw   = CurrentYaw;
    Transition.StartPitch = CurrentPitch;
    Transition.EndYaw     = EndYaw;
    Transition.EndPitch   = EndPitch;
    Transition.Duration   = (Duration > 0.0001f) ? Duration : 0.0001f;
    Transition.Elapsed    = 0.0f;
    Transition.Engaged    = true;
}

// 📝 Advance an engaged snap by DeltaSeconds and write the eased Yaw/Pitch into the two outputs. Disengages when complete. A
//    no-op (returns false) when not engaged. Returns true while still running so the caller keeps requesting redraws.
inline bool EvaluateOrientationSnap(OrientationTransition& Transition,
                                    float DeltaSeconds,
                                    float& OutYaw,
                                    float& OutPitch)
{
    if (!Transition.Engaged)
    {
        return false;
    }

    Transition.Elapsed += DeltaSeconds;
    float Normalized = Transition.Elapsed / Transition.Duration;
    bool StillRunning = true;
    if (Normalized >= 1.0f)
    {
        Normalized   = 1.0f;
        StillRunning = false;
        Transition.Engaged = false;
    }

    const float Fraction = EvaluateSmoothStep(Normalized);
    OutYaw   = InterpolateShortestArc(Transition.StartYaw,   Transition.EndYaw,   Fraction);
    OutPitch = InterpolateShortestArc(Transition.StartPitch, Transition.EndPitch, Fraction);
    return StillRunning;
}

}   // namespace Frontier

#endif
