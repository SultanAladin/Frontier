/*==============================================================================================================================================
                                                         LINEARALGEBRA_FLOAT64.CPP
==============================================================================================================================================*/
// 🧩 Double-precision vector algebra. Normalize returns the zero vector below a 1e-12 magnitude rather than dividing by it.

#include "LinearAlgebra_Float64.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCALAR HELPERS
//------------------------------------------------------------------------------------------------------------------------

double ClampScalar(double Target, double MinimumBoundary, double MaximumBoundary) noexcept
{
    if (Target < MinimumBoundary) return MinimumBoundary;
    if (Target > MaximumBoundary) return MaximumBoundary;
    return Target;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       VECTOR2 ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

Vector2d AddVector(const Vector2d& Alpha, const Vector2d& Beta) noexcept
{
    return Vector2d{ Alpha.XCoord + Beta.XCoord, Alpha.YCoord + Beta.YCoord };
}

Vector2d SubtractVector(const Vector2d& Alpha, const Vector2d& Beta) noexcept
{
    return Vector2d{ Alpha.XCoord - Beta.XCoord, Alpha.YCoord - Beta.YCoord };
}

Vector2d ScaleVector(const Vector2d& Target, double ScaleFactor) noexcept
{
    return Vector2d{ Target.XCoord * ScaleFactor, Target.YCoord * ScaleFactor };
}

double DotProduct(const Vector2d& Alpha, const Vector2d& Beta) noexcept
{
    return Alpha.XCoord * Beta.XCoord + Alpha.YCoord * Beta.YCoord;
}

double EvaluateVectorLength(const Vector2d& Target) noexcept
{
    return std::sqrt(Target.XCoord * Target.XCoord + Target.YCoord * Target.YCoord);
}

Vector2d NormalizeVector(const Vector2d& Target) noexcept
{
    const double Magnitude = EvaluateVectorLength(Target);
    if (Magnitude < 1.0e-12) return Vector2d{ 0.0, 0.0 };
    const double Inverse = 1.0 / Magnitude;
    return Vector2d{ Target.XCoord * Inverse, Target.YCoord * Inverse };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       VECTOR3 ALGEBRA
//------------------------------------------------------------------------------------------------------------------------

Vector3d AddVector(const Vector3d& Alpha, const Vector3d& Beta) noexcept
{
    return Vector3d{ Alpha.XCoord + Beta.XCoord, Alpha.YCoord + Beta.YCoord, Alpha.ZCoord + Beta.ZCoord };
}

Vector3d SubtractVector(const Vector3d& Alpha, const Vector3d& Beta) noexcept
{
    return Vector3d{ Alpha.XCoord - Beta.XCoord, Alpha.YCoord - Beta.YCoord, Alpha.ZCoord - Beta.ZCoord };
}

Vector3d ScaleVector(const Vector3d& Target, double ScaleFactor) noexcept
{
    return Vector3d{ Target.XCoord * ScaleFactor, Target.YCoord * ScaleFactor, Target.ZCoord * ScaleFactor };
}

Vector3d CrossProduct(const Vector3d& Alpha, const Vector3d& Beta) noexcept
{
    return Vector3d{ Alpha.YCoord * Beta.ZCoord - Alpha.ZCoord * Beta.YCoord,
                     Alpha.ZCoord * Beta.XCoord - Alpha.XCoord * Beta.ZCoord,
                     Alpha.XCoord * Beta.YCoord - Alpha.YCoord * Beta.XCoord };
}

double DotProduct(const Vector3d& Alpha, const Vector3d& Beta) noexcept
{
    return Alpha.XCoord * Beta.XCoord + Alpha.YCoord * Beta.YCoord + Alpha.ZCoord * Beta.ZCoord;
}

double EvaluateVectorLengthSquared(const Vector3d& Target) noexcept
{
    return Target.XCoord * Target.XCoord + Target.YCoord * Target.YCoord + Target.ZCoord * Target.ZCoord;
}

double EvaluateVectorLength(const Vector3d& Target) noexcept
{
    return std::sqrt(EvaluateVectorLengthSquared(Target));
}

Vector3d NormalizeVector(const Vector3d& Target) noexcept
{
    const double Magnitude = EvaluateVectorLength(Target);
    if (Magnitude < 1.0e-12) return Vector3d{ 0.0, 0.0, 0.0 };
    const double Inverse = 1.0 / Magnitude;
    return Vector3d{ Target.XCoord * Inverse, Target.YCoord * Inverse, Target.ZCoord * Inverse };
}

} // namespace Frontier
