/*==============================================================================================================================================
                                                            CAMERAEASINGPROFILE.CPP
==============================================================================================================================================*/
// 🧩 The easing-law evaluation. Clamps normalized time to 0..1 then applies the selected curve. All shapes are closed-form (no stored samples),
//    so the "table" is a law selector — kept as a struct so a keyframed sample array can slot in later without changing callers.

#include "CameraEasingProfile.h"

#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float EvaluateKeyedEase(const EaseTable& Table, float NormalizedTime) noexcept
{
    const float Time = std::max(0.0f, std::min(1.0f, NormalizedTime));
    switch (Table.Law)
    {
        case EaseLaw::Linear:     return Time;
        case EaseLaw::SmoothStep: return Time * Time * (3.0f - 2.0f * Time);
        case EaseLaw::EaseIn:     return Time * Time;
        case EaseLaw::EaseOut:    return Time * (2.0f - Time);
    }
    return Time;
}

} // namespace Frontier
