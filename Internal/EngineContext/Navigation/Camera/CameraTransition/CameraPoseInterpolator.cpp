/*==============================================================================================================================================
                                                          CAMERAPOSEINTERPOLATOR.CPP
==============================================================================================================================================*/
// 🧩 The keyed pose blend. Capture snapshots the spec; Initialize arms a move to a destination; Evaluate advances it, sampling the ease table for
//    the blend fraction and writing the interpolated orbit fields back into the live camera. Target + distance blend linearly; yaw + pitch blend on
//    the shortest arc so a wrap does not spin the long way. When Elapsed reaches Duration the end pose is written exactly and the move disengages.

#include "CameraPoseInterpolator.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Shortest-arc scalar-angle blend: wrap the delta into (-π, π] before interpolating so yaw takes the short way around.
[[nodiscard]] float BlendAngleShortest(float From, float To, float Fraction) noexcept
{
    const float TwoPi = 6.2831853f;
    float Delta = To - From;
    while (Delta >  3.1415927f) Delta -= TwoPi;
    while (Delta < -3.1415927f) Delta += TwoPi;
    return From + Delta * Fraction;
}

// Linear scalar blend.
[[nodiscard]] float BlendScalar(float From, float To, float Fraction) noexcept
{
    return From + (To - From) * Fraction;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

CameraPose CaptureCameraPose(const ViewportCamera& Camera) noexcept
{
    CameraPose Pose;
    Pose.Target   = Camera.Target;
    Pose.Distance = Camera.Distance;
    Pose.Yaw      = Camera.Yaw;
    Pose.Pitch    = Camera.Pitch;
    return Pose;
}

CameraPose InterpolateCameraPose(const CameraPose& StartPose, const CameraPose& EndPose, float Fraction) noexcept
{
    CameraPose Pose;
    Pose.Target.XCoord = BlendScalar(StartPose.Target.XCoord, EndPose.Target.XCoord, Fraction);
    Pose.Target.YCoord = BlendScalar(StartPose.Target.YCoord, EndPose.Target.YCoord, Fraction);
    Pose.Target.ZCoord = BlendScalar(StartPose.Target.ZCoord, EndPose.Target.ZCoord, Fraction);
    Pose.Distance      = BlendScalar(StartPose.Distance, EndPose.Distance, Fraction);
    Pose.Yaw           = BlendAngleShortest(StartPose.Yaw,   EndPose.Yaw,   Fraction);
    Pose.Pitch         = BlendAngleShortest(StartPose.Pitch, EndPose.Pitch, Fraction);
    return Pose;
}

void InitializeCameraTransition(CameraTransition&     Transition,
                                const ViewportCamera& Camera,
                                const CameraPose&     Destination,
                                float                 Duration,
                                const EaseTable&      Curve) noexcept
{
    Transition.StartPose        = CaptureCameraPose(Camera);
    Transition.EndPose          = Destination;
    Transition.Curve            = Curve;
    Transition.Duration         = Duration > 1e-4f ? Duration : 1e-4f;
    Transition.Elapsed          = 0.0f;
    Transition.EngagedCondition = true;
}

bool EvaluateCameraTransition(CameraTransition& Transition, ViewportCamera& Camera, float DeltaSeconds) noexcept
{
    if (!Transition.EngagedCondition)
        return false;

    Transition.Elapsed += DeltaSeconds;
    const float NormalizedTime = Transition.Elapsed / Transition.Duration;
    const float Fraction       = EvaluateKeyedEase(Transition.Curve, NormalizedTime);

    const CameraPose Blended = InterpolateCameraPose(Transition.StartPose, Transition.EndPose, Fraction);
    Camera.Target   = Blended.Target;
    Camera.Distance = Blended.Distance;
    Camera.Yaw      = Blended.Yaw;
    Camera.Pitch    = Blended.Pitch;

    if (NormalizedTime >= 1.0f)
    {
        Transition.EngagedCondition = false;
        return false;
    }
    return true;
}

} // namespace Frontier
