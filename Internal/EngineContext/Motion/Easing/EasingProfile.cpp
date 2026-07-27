/*==============================================================================================================================================
                                                              EASINGPROFILE.CPP
==============================================================================================================================================*/
// 🧩 Cubic-bezier easing curve — the prototype's --ease:(.22,.61,.36,1) and friends, evaluated on a normalized progress

#include "EasingProfile.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Cubic bezier with fixed endpoints (0,0) and (1,1); evaluates a coordinate channel at parameter T.
constexpr float EvaluateBezierChannel(float HandleA, float HandleB, float T) noexcept
{
    float Inverse = 1.0f - T;
    // 📝 Endpoints are 0 and 1, so the first and last Bernstein terms collapse to a single 'T*T*T'.
    return 3.0f * Inverse * Inverse * T * HandleA + 3.0f * Inverse * T * T * HandleB + T * T * T;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float EvaluateEasing(const EasingProfile& Profile, float Progress) noexcept
{
    if (Progress <= 0.0f)
        return 0.0f;
    if (Progress >= 1.0f)
        return 1.0f;

    // 💡 Solve X(T) = Progress for T by Newton iteration, then read Y(T). 8 steps is ample for UI timing.
    float Parameter = Progress;
    for (int Iteration = 0; Iteration < 8; ++Iteration)
    {
        float CurrentX  = EvaluateBezierChannel(Profile.FirstHandleX, Profile.SecondHandleX, Parameter) - Progress;
        float Inverse   = 1.0f - Parameter;
        float Slope     = 3.0f * Inverse * Inverse * Profile.FirstHandleX
                        + 6.0f * Inverse * Parameter * (Profile.SecondHandleX - Profile.FirstHandleX)
                        + 3.0f * Parameter * Parameter * (1.0f - Profile.SecondHandleX);
        if (Slope < 1e-5f && Slope > -1e-5f)
            break;
        Parameter -= CurrentX / Slope;
        if (Parameter < 0.0f) Parameter = 0.0f;
        if (Parameter > 1.0f) Parameter = 1.0f;
    }
    return EvaluateBezierChannel(Profile.FirstHandleY, Profile.SecondHandleY, Parameter);
}

} // namespace Frontier
