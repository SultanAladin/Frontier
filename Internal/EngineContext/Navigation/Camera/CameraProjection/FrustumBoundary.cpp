/*==============================================================================================================================================
                                                             FRUSTUMBOUNDARY.CPP
==============================================================================================================================================*/
// 🧩 Gribb–Hartmann plane extraction. The six planes come from adding / subtracting the view-projection's rows; each is then normalized so the
//    stored constant is a true world distance and SphereInsideFrustum can compare against a radius directly. The matrix is column-major (Column[c][r]),
//    so row r is (Column[0][r], Column[1][r], Column[2][r], Column[3][r]).

#include "FrustumBoundary.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// One row of the column-major matrix as a 4-tuple (A,B,C,D) → the plane A·x + B·y + C·z + D = 0 before normalization. A
// degenerate row (zero-length normal) yields a zero plane rather than dividing by zero.
[[nodiscard]] FrustumPlane NormalizePlane(float A, float B, float C, float D) noexcept
{
    const float Length = VectorLength(Vector3f{ A, B, C });
    FrustumPlane Plane;
    if (Length <= 1e-8f)
    {
        Plane.Normal = Vector3f{ 0.0f, 0.0f, 0.0f };
        Plane.Offset = 0.0f;
        return Plane;
    }
    const float InverseLength = 1.0f / Length;
    Plane.Normal = Vector3f{ A * InverseLength, B * InverseLength, C * InverseLength };
    Plane.Offset = D * InverseLength;
    return Plane;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

FrustumBoundary ExtractFrustumBoundary(const Matrix4f& ViewProjection) noexcept
{
    // Rows of the column-major view-projection.
    const float R0[4] = { ViewProjection.Column[0][0], ViewProjection.Column[1][0], ViewProjection.Column[2][0], ViewProjection.Column[3][0] };
    const float R1[4] = { ViewProjection.Column[0][1], ViewProjection.Column[1][1], ViewProjection.Column[2][1], ViewProjection.Column[3][1] };
    const float R2[4] = { ViewProjection.Column[0][2], ViewProjection.Column[1][2], ViewProjection.Column[2][2], ViewProjection.Column[3][2] };
    const float R3[4] = { ViewProjection.Column[0][3], ViewProjection.Column[1][3], ViewProjection.Column[2][3], ViewProjection.Column[3][3] };

    FrustumBoundary Boundary;
    Boundary.Left   = NormalizePlane(R3[0] + R0[0], R3[1] + R0[1], R3[2] + R0[2], R3[3] + R0[3]);
    Boundary.Right  = NormalizePlane(R3[0] - R0[0], R3[1] - R0[1], R3[2] - R0[2], R3[3] - R0[3]);
    Boundary.Bottom = NormalizePlane(R3[0] + R1[0], R3[1] + R1[1], R3[2] + R1[2], R3[3] + R1[3]);
    Boundary.Top    = NormalizePlane(R3[0] - R1[0], R3[1] - R1[1], R3[2] - R1[2], R3[3] - R1[3]);
    Boundary.Near   = NormalizePlane(R2[0],         R2[1],         R2[2],         R2[3]);          // depth 0..1 near
    Boundary.Far    = NormalizePlane(R3[0] - R2[0], R3[1] - R2[1], R3[2] - R2[2], R3[3] - R2[3]);
    return Boundary;
}

float SignedDistanceToPlane(const FrustumPlane& Plane, const Vector3f& WorldPoint) noexcept
{
    return DotVector(Plane.Normal, WorldPoint) + Plane.Offset;
}

bool SphereInsideFrustum(const FrustumBoundary& Boundary, const Vector3f& Centre, float Radius) noexcept
{
    const FrustumPlane* Planes = &Boundary.Left;
    for (int Side = 0; Side < 6; ++Side)
    {
        if (SignedDistanceToPlane(Planes[Side], Centre) < -Radius)
            return false;   // wholly outside this plane
    }
    return true;
}

} // namespace Frontier
