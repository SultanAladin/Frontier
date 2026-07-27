/*==============================================================================================================================================
                                                               DISTANCEMETRIC.H
==============================================================================================================================================*/
// 🧩 The engine's distance standard: every length the renderer stores, compares, or hands to a shader is in METRES. Older code carried lengths
//    in centimetres; this header is the one place that names the standard and the conversions across it, so a centimetre literal is converted at
//    exactly one boundary and never leaks into the metric world. Metres in, metres out — the helpers below only exist for the edges that still
//    speak centimetres. Header-only, POD, no link unit, no external dependency (our own Vector3f from LinearAlgebra, never glm).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_METRICSPACE_DISTANCEMETRIC_H
#define FRONTIER_ENGINECONTEXT_METRICSPACE_DISTANCEMETRIC_H

#include "../Math/LinearAlgebra_Float32.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE METRE STANDARD
//------------------------------------------------------------------------------------------------------------------------

// One metre expressed in the legacy centimetre unit. The only numeric bridge between the two; every conversion below is
// phrased through it so there is a single constant to reason about.
[[nodiscard]] inline float CentimetresPerMetre() noexcept
{
    return 100.0f;
}

// Legacy centimetres → the engine standard (metres). Use at any boundary that still receives a centimetre length.
[[nodiscard]] inline float MetresFromCentimetres(float Centimetres) noexcept
{
    return Centimetres / CentimetresPerMetre();
}

// Metres → legacy centimetres. Use only when handing a length back to code that has not yet moved to the standard.
[[nodiscard]] inline float CentimetresFromMetres(float Metres) noexcept
{
    return Metres * CentimetresPerMetre();
}

// A whole position in centimetres → metres. Component-wise; the reference frame is unchanged, only the unit.
[[nodiscard]] inline Vector3f MetresFromCentimetres(const Vector3f& Centimetres) noexcept
{
    return Vector3f{ MetresFromCentimetres(Centimetres.XCoord),
                     MetresFromCentimetres(Centimetres.YCoord),
                     MetresFromCentimetres(Centimetres.ZCoord) };
}

// A whole position in metres → centimetres.
[[nodiscard]] inline Vector3f CentimetresFromMetres(const Vector3f& Metres) noexcept
{
    return Vector3f{ CentimetresFromMetres(Metres.XCoord),
                     CentimetresFromMetres(Metres.YCoord),
                     CentimetresFromMetres(Metres.ZCoord) };
}

} // namespace Frontier

#endif
