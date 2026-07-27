/*==============================================================================================================================================
                                                          LINEARALGEBRA_FLOAT64.H
==============================================================================================================================================*/
// 🧩 Double-precision 2- and 3-component vectors plus the free-function algebra (add / scale / cross / dot / normalize) that every CPU-side
//    authoring subsystem shares. Convert to float only at the GPU-upload boundary; the authoring math stays double. The single-precision
//    render companion lives in LinearAlgebra_Float32.h; the two coexist in one translation unit because the type names carry the f/d suffix.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MATH_LINEARALGEBRA_FLOAT64_H
#define FRONTIER_ENGINECONTEXT_MATH_LINEARALGEBRA_FLOAT64_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct Vector2d
{
    double XCoord = 0.0;   // [-] - first component (UV U, screen X, ...)
    double YCoord = 0.0;   // [-] - second component (UV V, screen Y, ...)
};

struct Vector3d
{
    double XCoord = 0.0;   // [cm] - world X
    double YCoord = 0.0;   // [cm] - world Y
    double ZCoord = 0.0;   // [cm] - world Z
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCALAR HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] double ClampScalar(double Target, double MinimumBoundary, double MaximumBoundary) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                       VECTOR2 ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector2d AddVector(const Vector2d& Alpha, const Vector2d& Beta) noexcept;
[[nodiscard]] Vector2d SubtractVector(const Vector2d& Alpha, const Vector2d& Beta) noexcept;
[[nodiscard]] Vector2d ScaleVector(const Vector2d& Target, double ScaleFactor) noexcept;
[[nodiscard]] double   DotProduct(const Vector2d& Alpha, const Vector2d& Beta) noexcept;
[[nodiscard]] double   EvaluateVectorLength(const Vector2d& Target) noexcept;
[[nodiscard]] Vector2d NormalizeVector(const Vector2d& Target) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                       VECTOR3 ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3d AddVector(const Vector3d& Alpha, const Vector3d& Beta) noexcept;
[[nodiscard]] Vector3d SubtractVector(const Vector3d& Alpha, const Vector3d& Beta) noexcept;
[[nodiscard]] Vector3d ScaleVector(const Vector3d& Target, double ScaleFactor) noexcept;
[[nodiscard]] Vector3d CrossProduct(const Vector3d& Alpha, const Vector3d& Beta) noexcept;
[[nodiscard]] double   DotProduct(const Vector3d& Alpha, const Vector3d& Beta) noexcept;
[[nodiscard]] double   EvaluateVectorLength(const Vector3d& Target) noexcept;
[[nodiscard]] double   EvaluateVectorLengthSquared(const Vector3d& Target) noexcept;
[[nodiscard]] Vector3d NormalizeVector(const Vector3d& Target) noexcept;

} // namespace Frontier

#endif
