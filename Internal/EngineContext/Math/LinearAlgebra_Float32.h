/*==============================================================================================================================================
                                                          LINEARALGEBRA_FLOAT32.H
==============================================================================================================================================*/
// 🧩 The single-precision (float) linear-algebra surface for the engine: a 3-vector, a quaternion, a column-major 4×4, and the operations
//    (add / scale / dot / cross / normalize / rotate / slerp / look-at / perspective / orthographic / multiply / invert) that build a
//    view-projection. Header-only, POD, no external dependency — this is the GPU-facing precision (Vulkan upload boundary). The double-precision
//    authoring companion lives in LinearAlgebra_Float64.h; the two coexist in one translation unit because the type names carry the f/d suffix.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MATH_LINEARALGEBRA_FLOAT32_H
#define FRONTIER_ENGINECONTEXT_MATH_LINEARALGEBRA_FLOAT32_H

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct Vector3f
{
    float XCoord = 0.0f;   // [-]
    float YCoord = 0.0f;   // [-]
    float ZCoord = 0.0f;   // [-]
};

struct Quaternionf
{
    float XCoord = 0.0f;   // [-]
    float YCoord = 0.0f;   // [-]
    float ZCoord = 0.0f;   // [-]
    float WCoord = 1.0f;   // [-] - Identity rotation
};

// Column-major 4×4 (Vulkan-friendly), stored Column[c][r].
struct Matrix4f
{
    float Column[4][4] =
    {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    };
};


//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Vector3f AddVector(const Vector3f& Left, const Vector3f& Right) noexcept
{
    return Vector3f{ Left.XCoord + Right.XCoord, Left.YCoord + Right.YCoord, Left.ZCoord + Right.ZCoord };
}

[[nodiscard]] inline Vector3f SubtractVector(const Vector3f& Left, const Vector3f& Right) noexcept
{
    return Vector3f{ Left.XCoord - Right.XCoord, Left.YCoord - Right.YCoord, Left.ZCoord - Right.ZCoord };
}

[[nodiscard]] inline Vector3f ScaleVector(const Vector3f& Value, float Factor) noexcept
{
    return Vector3f{ Value.XCoord * Factor, Value.YCoord * Factor, Value.ZCoord * Factor };
}

[[nodiscard]] inline float DotVector(const Vector3f& Left, const Vector3f& Right) noexcept
{
    return Left.XCoord * Right.XCoord + Left.YCoord * Right.YCoord + Left.ZCoord * Right.ZCoord;
}

[[nodiscard]] inline Vector3f CrossVector(const Vector3f& Left, const Vector3f& Right) noexcept
{
    return Vector3f{
        Left.YCoord * Right.ZCoord - Left.ZCoord * Right.YCoord,
        Left.ZCoord * Right.XCoord - Left.XCoord * Right.ZCoord,
        Left.XCoord * Right.YCoord - Left.YCoord * Right.XCoord };
}

[[nodiscard]] inline float VectorLength(const Vector3f& Value) noexcept
{
    return std::sqrt(DotVector(Value, Value));
}

[[nodiscard]] inline Vector3f NormalizeVector(const Vector3f& Value) noexcept
{
    const float Length = VectorLength(Value);
    if (Length <= 1e-8f)
        return Vector3f{ 0.0f, 0.0f, 0.0f };
    const float Inverse = 1.0f / Length;
    return Vector3f{ Value.XCoord * Inverse, Value.YCoord * Inverse, Value.ZCoord * Inverse };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                  QUATERNION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Quaternionf NormalizeQuaternion(const Quaternionf& Value) noexcept
{
    const float Length = std::sqrt(Value.XCoord * Value.XCoord + Value.YCoord * Value.YCoord + Value.ZCoord * Value.ZCoord + Value.WCoord * Value.WCoord);
    if (Length <= 1e-8f)
        return Quaternionf{ 0.0f, 0.0f, 0.0f, 1.0f };
    const float Inverse = 1.0f / Length;
    return Quaternionf{ Value.XCoord * Inverse, Value.YCoord * Inverse, Value.ZCoord * Inverse, Value.WCoord * Inverse };
}

[[nodiscard]] inline Quaternionf MultiplyQuaternion(const Quaternionf& Left, const Quaternionf& Right) noexcept
{
    return Quaternionf{
        Left.WCoord * Right.XCoord + Left.XCoord * Right.WCoord + Left.YCoord * Right.ZCoord - Left.ZCoord * Right.YCoord,
        Left.WCoord * Right.YCoord - Left.XCoord * Right.ZCoord + Left.YCoord * Right.WCoord + Left.ZCoord * Right.XCoord,
        Left.WCoord * Right.ZCoord + Left.XCoord * Right.YCoord - Left.YCoord * Right.XCoord + Left.ZCoord * Right.WCoord,
        Left.WCoord * Right.WCoord - Left.XCoord * Right.XCoord - Left.YCoord * Right.YCoord - Left.ZCoord * Right.ZCoord };
}

// Quaternion from an axis (assumed normalized) and an angle in radians.
[[nodiscard]] inline Quaternionf QuaternionFromAxisAngle(const Vector3f& Axis, float AngleRadians) noexcept
{
    const float Half = AngleRadians * 0.5f;
    const float SinHalf = std::sin(Half);
    return Quaternionf{ Axis.XCoord * SinHalf, Axis.YCoord * SinHalf, Axis.ZCoord * SinHalf, std::cos(Half) };
}

// Rotate a vector by a quaternion (v' = q * v * q⁻¹, expanded).
[[nodiscard]] inline Vector3f RotateVector(const Quaternionf& Rotation, const Vector3f& Value) noexcept
{
    const Vector3f Axis{ Rotation.XCoord, Rotation.YCoord, Rotation.ZCoord };
    const Vector3f CrossA = CrossVector(Axis, Value);
    const Vector3f CrossB = CrossVector(Axis, CrossA);
    return AddVector(Value, AddVector(ScaleVector(CrossA, 2.0f * Rotation.WCoord), ScaleVector(CrossB, 2.0f)));
}

// Spherical linear interpolation, shortest-arc. Amount 0 → From, 1 → To.
[[nodiscard]] inline Quaternionf SlerpQuaternion(const Quaternionf& From, const Quaternionf& To, float Amount) noexcept
{
    float CosTheta = From.XCoord * To.XCoord + From.YCoord * To.YCoord + From.ZCoord * To.ZCoord + From.WCoord * To.WCoord;
    Quaternionf Target = To;
    if (CosTheta < 0.0f)   // shortest arc
    {
        CosTheta = -CosTheta;
        Target = Quaternionf{ -To.XCoord, -To.YCoord, -To.ZCoord, -To.WCoord };
    }

    if (CosTheta > 0.9995f) // nearly parallel — linear blend then renormalize
    {
        return NormalizeQuaternion(Quaternionf{
            From.XCoord + (Target.XCoord - From.XCoord) * Amount,
            From.YCoord + (Target.YCoord - From.YCoord) * Amount,
            From.ZCoord + (Target.ZCoord - From.ZCoord) * Amount,
            From.WCoord + (Target.WCoord - From.WCoord) * Amount });
    }

    const float Theta    = std::acos(CosTheta);
    const float SinTheta = std::sin(Theta);
    const float WeightFrom = std::sin((1.0f - Amount) * Theta) / SinTheta;
    const float WeightTo   = std::sin(Amount * Theta) / SinTheta;
    return Quaternionf{
        From.XCoord * WeightFrom + Target.XCoord * WeightTo,
        From.YCoord * WeightFrom + Target.YCoord * WeightTo,
        From.ZCoord * WeightFrom + Target.ZCoord * WeightTo,
        From.WCoord * WeightFrom + Target.WCoord * WeightTo };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    MATRIX OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// Right-handed look-at view matrix (world → view). Up need not be orthogonal to the forward direction.
[[nodiscard]] inline Matrix4f ConstructLookAt(const Vector3f& Eye, const Vector3f& Target, const Vector3f& Up) noexcept
{
    const Vector3f Forward = NormalizeVector(SubtractVector(Target, Eye));
    const Vector3f Right   = NormalizeVector(CrossVector(Forward, Up));
    const Vector3f TrueUp  = CrossVector(Right, Forward);

    Matrix4f Result;
    Result.Column[0][0] = Right.XCoord;    Result.Column[1][0] = Right.YCoord;    Result.Column[2][0] = Right.ZCoord;    Result.Column[3][0] = -DotVector(Right, Eye);
    Result.Column[0][1] = TrueUp.XCoord;   Result.Column[1][1] = TrueUp.YCoord;   Result.Column[2][1] = TrueUp.ZCoord;   Result.Column[3][1] = -DotVector(TrueUp, Eye);
    Result.Column[0][2] = -Forward.XCoord; Result.Column[1][2] = -Forward.YCoord; Result.Column[2][2] = -Forward.ZCoord; Result.Column[3][2] = DotVector(Forward, Eye);
    Result.Column[0][3] = 0.0f;            Result.Column[1][3] = 0.0f;            Result.Column[2][3] = 0.0f;            Result.Column[3][3] = 1.0f;
    return Result;
}

// Vulkan-style perspective (right-handed, depth 0..1). Vulkan's clip-space +Y points DOWN (opposite the OpenGL convention this
// focal-length form assumes), so Column[1][1] is NEGATED here to flip Y once, at the projection, for every consumer. Without it
// the whole scene renders vertically mirrored — sky below the grid, horizon/sun band at the bottom — because the un-flipped
// matrix hands each pass a Y-inverted clip space (grid, sky, and all future geometry share this one matrix through the
// InverseViewProjection unproject, so the flip must live here and nowhere else — no negative-height viewport double-flip).
[[nodiscard]] inline Matrix4f ConstructPerspective(float VerticalFovRadians, float AspectRatio, float NearPlane, float FarPlane) noexcept
{
    const float FocalLength = 1.0f / std::tan(VerticalFovRadians * 0.5f);
    Matrix4f Result;
    Result.Column[0][0] = FocalLength / AspectRatio;
    Result.Column[1][1] = -FocalLength;   // Vulkan clip-space Y is down — negate to keep world +Z up on screen
    Result.Column[2][2] = FarPlane / (NearPlane - FarPlane);
    Result.Column[2][3] = -1.0f;
    Result.Column[3][2] = (NearPlane * FarPlane) / (NearPlane - FarPlane);
    Result.Column[3][3] = 0.0f;
    return Result;
}

// Orthographic projection (right-handed, depth 0..1) for CAD/ortho views. HalfHeight sets the vertical world extent.
[[nodiscard]] inline Matrix4f ConstructOrthographic(float HalfHeight, float AspectRatio, float NearPlane, float FarPlane) noexcept
{
    const float HalfWidth = HalfHeight * AspectRatio;
    Matrix4f Result;
    Result.Column[0][0] = 1.0f / HalfWidth;
    Result.Column[1][1] = -1.0f / HalfHeight;   // Vulkan clip-space Y is down — negate (matches ConstructPerspective)
    Result.Column[2][2] = 1.0f / (NearPlane - FarPlane);
    Result.Column[3][2] = NearPlane / (NearPlane - FarPlane);
    Result.Column[3][3] = 1.0f;
    return Result;
}

// Column-major product Left · Right (applies Right first when transforming a column vector).
[[nodiscard]] inline Matrix4f MultiplyMatrix(const Matrix4f& Left, const Matrix4f& Right) noexcept
{
    Matrix4f Result;
    for (int OutColumn = 0; OutColumn < 4; ++OutColumn)
    {
        for (int OutRow = 0; OutRow < 4; ++OutRow)
        {
            float Sum = 0.0f;
            for (int Inner = 0; Inner < 4; ++Inner)
                Sum += Left.Column[Inner][OutRow] * Right.Column[OutColumn][Inner];
            Result.Column[OutColumn][OutRow] = Sum;
        }
    }
    return Result;
}

// Full 4×4 inverse (cofactor method). Returns identity if the matrix is singular. Used to unproject clip → world.
[[nodiscard]] inline Matrix4f InvertMatrix(const Matrix4f& Source) noexcept
{
    const float* M = &Source.Column[0][0];   // Flat, column-major: M[c*4 + r]

    float Inverse[16];
    Inverse[0]  =  M[5]*M[10]*M[15] - M[5]*M[11]*M[14] - M[9]*M[6]*M[15] + M[9]*M[7]*M[14] + M[13]*M[6]*M[11] - M[13]*M[7]*M[10];
    Inverse[4]  = -M[4]*M[10]*M[15] + M[4]*M[11]*M[14] + M[8]*M[6]*M[15] - M[8]*M[7]*M[14] - M[12]*M[6]*M[11] + M[12]*M[7]*M[10];
    Inverse[8]  =  M[4]*M[9]*M[15]  - M[4]*M[11]*M[13] - M[8]*M[5]*M[15] + M[8]*M[7]*M[13] + M[12]*M[5]*M[11] - M[12]*M[7]*M[9];
    Inverse[12] = -M[4]*M[9]*M[14]  + M[4]*M[10]*M[13] + M[8]*M[5]*M[14] - M[8]*M[6]*M[13] - M[12]*M[5]*M[10] + M[12]*M[6]*M[9];
    Inverse[1]  = -M[1]*M[10]*M[15] + M[1]*M[11]*M[14] + M[9]*M[2]*M[15] - M[9]*M[3]*M[14] - M[13]*M[2]*M[11] + M[13]*M[3]*M[10];
    Inverse[5]  =  M[0]*M[10]*M[15] - M[0]*M[11]*M[14] - M[8]*M[2]*M[15] + M[8]*M[3]*M[14] + M[12]*M[2]*M[11] - M[12]*M[3]*M[10];
    Inverse[9]  = -M[0]*M[9]*M[15]  + M[0]*M[11]*M[13] + M[8]*M[1]*M[15] - M[8]*M[3]*M[13] - M[12]*M[1]*M[11] + M[12]*M[3]*M[9];
    Inverse[13] =  M[0]*M[9]*M[14]  - M[0]*M[10]*M[13] - M[8]*M[1]*M[14] + M[8]*M[2]*M[13] + M[12]*M[1]*M[10] - M[12]*M[2]*M[9];
    Inverse[2]  =  M[1]*M[6]*M[15]  - M[1]*M[7]*M[14]  - M[5]*M[2]*M[15] + M[5]*M[3]*M[14] + M[13]*M[2]*M[7]  - M[13]*M[3]*M[6];
    Inverse[6]  = -M[0]*M[6]*M[15]  + M[0]*M[7]*M[14]  + M[4]*M[2]*M[15] - M[4]*M[3]*M[14] - M[12]*M[2]*M[7]  + M[12]*M[3]*M[6];
    Inverse[10] =  M[0]*M[5]*M[15]  - M[0]*M[7]*M[13]  - M[4]*M[1]*M[15] + M[4]*M[3]*M[13] + M[12]*M[1]*M[7]  - M[12]*M[3]*M[5];
    Inverse[14] = -M[0]*M[5]*M[14]  + M[0]*M[6]*M[13]  + M[4]*M[1]*M[14] - M[4]*M[2]*M[13] - M[12]*M[1]*M[6]  + M[12]*M[2]*M[5];
    Inverse[3]  = -M[1]*M[6]*M[11]  + M[1]*M[7]*M[10]  + M[5]*M[2]*M[11] - M[5]*M[3]*M[10] - M[9]*M[2]*M[7]   + M[9]*M[3]*M[6];
    Inverse[7]  =  M[0]*M[6]*M[11]  - M[0]*M[7]*M[10]  - M[4]*M[2]*M[11] + M[4]*M[3]*M[10] + M[8]*M[2]*M[7]   - M[8]*M[3]*M[6];
    Inverse[11] = -M[0]*M[5]*M[11]  + M[0]*M[7]*M[9]   + M[4]*M[1]*M[11] - M[4]*M[3]*M[9]  - M[8]*M[1]*M[7]   + M[8]*M[3]*M[5];
    Inverse[15] =  M[0]*M[5]*M[10]  - M[0]*M[6]*M[9]   - M[4]*M[1]*M[10] + M[4]*M[2]*M[9]  + M[8]*M[1]*M[6]   - M[8]*M[2]*M[5];

    float Determinant = M[0]*Inverse[0] + M[1]*Inverse[4] + M[2]*Inverse[8] + M[3]*Inverse[12];
    if (Determinant > -1e-12f && Determinant < 1e-12f)
        return Matrix4f{};

    const float Reciprocal = 1.0f / Determinant;
    Matrix4f Result;
    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
            Result.Column[Column][Row] = Inverse[Column * 4 + Row] * Reciprocal;
    return Result;
}

} // namespace Frontier

#endif
