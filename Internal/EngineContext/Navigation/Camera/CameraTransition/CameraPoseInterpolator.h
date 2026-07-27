/*==============================================================================================================================================
                                                          CAMERAPOSEINTERPOLATOR.H
==============================================================================================================================================*/
// 🧩 A discrete, timed camera move from one orbit pose to another — "frame the selection", "snap to front view", "return home". A CameraTransition
//    holds a start + end pose and a Duration; advancing it by a frame delta samples the EaseTable for a blend fraction and interpolates the orbit
//    fields (Target / Distance / Yaw / Pitch) between the two, writing the result into the live camera. This is the keyed-transition path, distinct
//    from the direct navigation verbs; when no transition is active the camera is driven purely by CameraNavigation. Angles are interpolated on the
//    shortest arc so a yaw wrap does not spin the long way around.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERAPOSEINTERPOLATOR_H
#define FRONTIER_ENGINECONTEXT_NAVIGATION_CAMERA_CAMERAPOSEINTERPOLATOR_H

#include "CameraEasingProfile.h"
#include "../CameraConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The orbit fields that define a pose for a transition — a lightweight snapshot of the spec's navigable state (the lens
//    is not animated). InterpolateCameraPose blends two of these.
struct CameraPose
{
    Vector3f Target;             // [m]   - Orbit target
    float    Distance = 6.0f;    // [m]   - Eye distance
    float    Yaw      = 0.0f;    // [rad] - Orbit yaw
    float    Pitch    = 0.0f;    // [rad] - Orbit pitch
};

// 📝 An in-flight timed move. StartPose → EndPose over Duration seconds, shaped by Curve. Elapsed accumulates the frame
//    delta; EngagedCondition is false once Elapsed reaches Duration (or before one is armed).
struct CameraTransition
{
    CameraPose StartPose;                 // [-]  - Pose at Elapsed = 0
    CameraPose EndPose;                   // [-]  - Pose at Elapsed = Duration
    EaseTable  Curve;                     // [-]  - Timing shape (SmoothStep default)
    float      Duration         = 0.6f;   // [s]  - Total move time
    float      Elapsed          = 0.0f;   // [s]  - Time accumulated so far
    bool       EngagedCondition = false;  // [-]  - True while the move is running
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Snapshot the camera's current navigable fields into a CameraPose (the usual StartPose source).
[[nodiscard]] CameraPose CaptureCameraPose(const ViewportCamera& Camera) noexcept;

// Blend two poses at an already-eased fraction 0..1 (Fraction = EvaluateKeyedEase(...)). Angles take the shortest arc.
[[nodiscard]] CameraPose InterpolateCameraPose(const CameraPose& StartPose, const CameraPose& EndPose, float Fraction) noexcept;

// Arm a transition from the camera's current pose to Destination over Duration, shaped by Curve. Resets Elapsed + engages.
void InitializeCameraTransition(CameraTransition&     Transition,
                                const ViewportCamera& Camera,
                                const CameraPose&     Destination,
                                float                 Duration,
                                const EaseTable&      Curve) noexcept;

// Advance an engaged transition by DeltaSeconds and write the interpolated pose into Camera. Disengages when complete. A
// no-op when not engaged. Returns true while still running.
[[nodiscard]] bool EvaluateCameraTransition(CameraTransition& Transition, ViewportCamera& Camera, float DeltaSeconds) noexcept;

} // namespace Frontier

#endif
