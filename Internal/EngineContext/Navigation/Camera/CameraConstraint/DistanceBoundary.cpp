/*==============================================================================================================================================
                                                            DISTANCEBOUNDARY.CPP
==============================================================================================================================================*/
// 🧩 The distance clamp. Owns the positive orbit-distance range so dolly can never invert or escape the view.

#include "DistanceBoundary.h"

#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ConstrainDistance(float Distance) noexcept
{
    return std::max(MinimumOrbitDistance, std::min(MaximumOrbitDistance, Distance));
}

} // namespace Frontier
