/*====================================================================================================================================
                                                   SPATIALMATHEMATICS.JS
====================================================================================================================================*/
// 🧩 Column-major 4x4 placement algebra, projection, and ray recovery for the viewport rasterization.

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

export function VecAdd(Left, Right)
{
    return [ Left[0] + Right[0], Left[1] + Right[1], Left[2] + Right[2] ];
}

export function VecSubtract(Left, Right)
{
    return [ Left[0] - Right[0], Left[1] - Right[1], Left[2] - Right[2] ];
}

export function VecScale(Operand, Factor)
{
    return [ Operand[0] * Factor, Operand[1] * Factor, Operand[2] * Factor ];
}

export function VecDot(Left, Right)
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

export function VecCross(Left, Right)
{
    return [
        Left[1] * Right[2] - Left[2] * Right[1],
        Left[2] * Right[0] - Left[0] * Right[2],
        Left[0] * Right[1] - Left[1] * Right[0]
    ];
}

export function VecNormalize(Operand)
{
    const Length = Math.hypot(Operand[0], Operand[1], Operand[2]) || 1.0;
    return [ Operand[0] / Length, Operand[1] / Length, Operand[2] / Length ];
}

export function VecLerp(Start, End, Interpolant)
{
    return [
        Start[0] + (End[0] - Start[0]) * Interpolant,
        Start[1] + (End[1] - Start[1]) * Interpolant,
        Start[2] + (End[2] - Start[2]) * Interpolant
    ];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATRIX PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

export function MatrixIdentity()
{
    const Result = new Float32Array(16);
    Result[0] = 1.0;
    Result[5] = 1.0;
    Result[10] = 1.0;
    Result[15] = 1.0;
    return Result;
}

// 📝 Column-major product, matching the layout consumed by WebGL uploads (out = Left x Right).
export function MatrixMultiply(Left, Right)
{
    const Result = new Float32Array(16);
    for (let Column = 0; Column < 4; Column += 1)
    {
        for (let Row = 0; Row < 4; Row += 1)
        {
            let Accumulator = 0.0;
            for (let Step = 0; Step < 4; Step += 1)
            {
                Accumulator += Left[Step * 4 + Row] * Right[Column * 4 + Step];
            }
            Result[Column * 4 + Row] = Accumulator;
        }
    }
    return Result;
}

export function MatrixPerspective(FieldOfViewRadians, AspectRatio, NearPlane, FarPlane)
{
    const FocalLength = 1.0 / Math.tan(FieldOfViewRadians * 0.5);
    const Result = new Float32Array(16);
    Result[0]  = FocalLength / AspectRatio;
    Result[5]  = FocalLength;
    Result[10] = (FarPlane + NearPlane) / (NearPlane - FarPlane);
    Result[11] = -1.0;
    Result[14] = (2.0 * FarPlane * NearPlane) / (NearPlane - FarPlane);
    return Result;
}

export function MatrixOrthographic(HalfWidth, HalfHeight, NearPlane, FarPlane)
{
    const Result = new Float32Array(16);
    Result[0]  = 1.0 / HalfWidth;
    Result[5]  = 1.0 / HalfHeight;
    Result[10] = -2.0 / (FarPlane - NearPlane);
    Result[14] = -(FarPlane + NearPlane) / (FarPlane - NearPlane);
    Result[15] = 1.0;
    return Result;
}

export function MatrixViewOrientation(EyePosition, LookTarget, UpDirection)
{
    const Forward = VecNormalize(VecSubtract(LookTarget, EyePosition));
    const Lateral = VecNormalize(VecCross(Forward, UpDirection));
    const Elevation = VecCross(Lateral, Forward);

    const Result = new Float32Array(16);
    Result[0]  = Lateral[0];
    Result[1]  = Elevation[0];
    Result[2]  = -Forward[0];
    Result[4]  = Lateral[1];
    Result[5]  = Elevation[1];
    Result[6]  = -Forward[1];
    Result[8]  = Lateral[2];
    Result[9]  = Elevation[2];
    Result[10] = -Forward[2];
    Result[12] = -VecDot(Lateral, EyePosition);
    Result[13] = -VecDot(Elevation, EyePosition);
    Result[14] = VecDot(Forward, EyePosition);
    Result[15] = 1.0;
    return Result;
}

// 📝 Placement = Translate x RotateZ x RotateY x RotateX x Scale. Rotation arrives in degrees.
export function MatrixModelPlacement(Translation, RotationDegrees, Scaling)
{
    const RadiansX = RotationDegrees[0] * Math.PI / 180.0;
    const RadiansY = RotationDegrees[1] * Math.PI / 180.0;
    const RadiansZ = RotationDegrees[2] * Math.PI / 180.0;

    const CosineX = Math.cos(RadiansX), SineX = Math.sin(RadiansX);
    const CosineY = Math.cos(RadiansY), SineY = Math.sin(RadiansY);
    const CosineZ = Math.cos(RadiansZ), SineZ = Math.sin(RadiansZ);

    const Rotation = new Float32Array([
        CosineZ * CosineY,                     SineZ * CosineY,                    -SineY,       0.0,
        CosineZ * SineY * SineX - SineZ * CosineX, SineZ * SineY * SineX + CosineZ * CosineX, CosineY * SineX, 0.0,
        CosineZ * SineY * CosineX + SineZ * SineX, SineZ * SineY * CosineX - CosineZ * SineX, CosineY * CosineX, 0.0,
        0.0,                               0.0,                                0.0,          1.0
    ]);

    const Result = new Float32Array(16);
    for (let Column = 0; Column < 3; Column += 1)
    {
        const AxisScale = Scaling[Column];
        Result[Column * 4 + 0] = Rotation[Column * 4 + 0] * AxisScale;
        Result[Column * 4 + 1] = Rotation[Column * 4 + 1] * AxisScale;
        Result[Column * 4 + 2] = Rotation[Column * 4 + 2] * AxisScale;
    }
    Result[12] = Translation[0];
    Result[13] = Translation[1];
    Result[14] = Translation[2];
    Result[15] = 1.0;
    return Result;
}

export function MatrixInverse(Source)
{
    const Result = new Float32Array(16);
    const A00 = Source[0],  A01 = Source[1],  A02 = Source[2],  A03 = Source[3];
    const A10 = Source[4],  A11 = Source[5],  A12 = Source[6],  A13 = Source[7];
    const A20 = Source[8],  A21 = Source[9],  A22 = Source[10], A23 = Source[11];
    const A30 = Source[12], A31 = Source[13], A32 = Source[14], A33 = Source[15];

    const B00 = A00 * A11 - A01 * A10;
    const B01 = A00 * A12 - A02 * A10;
    const B02 = A00 * A13 - A03 * A10;
    const B03 = A01 * A12 - A02 * A11;
    const B04 = A01 * A13 - A03 * A11;
    const B05 = A02 * A13 - A03 * A12;
    const B06 = A20 * A31 - A21 * A30;
    const B07 = A20 * A32 - A22 * A30;
    const B08 = A20 * A33 - A23 * A30;
    const B09 = A21 * A32 - A22 * A31;
    const B10 = A21 * A33 - A23 * A31;
    const B11 = A22 * A33 - A23 * A32;

    let Determinant = B00 * B11 - B01 * B10 + B02 * B09 + B03 * B08 - B04 * B07 + B05 * B06;
    if (Math.abs(Determinant) < 1.0e-12)
    {
        return MatrixIdentity();
    }
    Determinant = 1.0 / Determinant;

    Result[0]  = (A11 * B11 - A12 * B10 + A13 * B09) * Determinant;
    Result[1]  = (A02 * B10 - A01 * B11 - A03 * B09) * Determinant;
    Result[2]  = (A31 * B05 - A32 * B04 + A33 * B03) * Determinant;
    Result[3]  = (A22 * B04 - A21 * B05 - A23 * B03) * Determinant;
    Result[4]  = (A12 * B08 - A10 * B11 - A13 * B07) * Determinant;
    Result[5]  = (A00 * B11 - A02 * B08 + A03 * B07) * Determinant;
    Result[6]  = (A32 * B02 - A30 * B05 - A33 * B01) * Determinant;
    Result[7]  = (A20 * B05 - A22 * B02 + A23 * B01) * Determinant;
    Result[8]  = (A10 * B10 - A11 * B08 + A13 * B06) * Determinant;
    Result[9]  = (A01 * B08 - A00 * B10 - A03 * B06) * Determinant;
    Result[10] = (A30 * B04 - A31 * B02 + A33 * B00) * Determinant;
    Result[11] = (A21 * B02 - A20 * B04 - A23 * B00) * Determinant;
    Result[12] = (A11 * B07 - A10 * B09 - A12 * B06) * Determinant;
    Result[13] = (A00 * B09 - A01 * B07 + A02 * B06) * Determinant;
    Result[14] = (A31 * B01 - A30 * B03 - A32 * B00) * Determinant;
    Result[15] = (A20 * B03 - A21 * B01 + A22 * B00) * Determinant;
    return Result;
}

// 📝 Upper-left 3x3 inverse transpose, so non-uniform scaling still shades with correct normals.
export function MatrixNormalRecovery(Placement)
{
    // Column-major source: entry (row R, column C) lives at Placement[C * 4 + R].
    const A00 = Placement[0], A01 = Placement[3], A02 = Placement[6];
    const A10 = Placement[1], A11 = Placement[4], A12 = Placement[7];
    const A20 = Placement[2], A21 = Placement[5], A22 = Placement[8];

    const Determinant = A00 * (A11 * A22 - A12 * A21)
        - A01 * (A10 * A22 - A12 * A20)
        + A02 * (A10 * A21 - A11 * A20);

    if (Math.abs(Determinant) < 1.0e-12)
    {
        return new Float32Array([ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 ]);
    }
    const Reciprocal = 1.0 / Determinant;

    const Inverse00 =  (A11 * A22 - A12 * A21) * Reciprocal;
    const Inverse01 = -(A01 * A22 - A02 * A21) * Reciprocal;
    const Inverse02 =  (A01 * A12 - A02 * A11) * Reciprocal;
    const Inverse10 = -(A10 * A22 - A12 * A20) * Reciprocal;
    const Inverse11 =  (A00 * A22 - A02 * A20) * Reciprocal;
    const Inverse12 = -(A00 * A12 - A02 * A10) * Reciprocal;
    const Inverse20 =  (A10 * A21 - A11 * A20) * Reciprocal;
    const Inverse21 = -(A00 * A21 - A01 * A20) * Reciprocal;
    const Inverse22 =  (A00 * A11 - A01 * A10) * Reciprocal;

    // Listing the inverse row-major into a column-major array performs the transpose.
    return new Float32Array([
        Inverse00, Inverse01, Inverse02,
        Inverse10, Inverse11, Inverse12,
        Inverse20, Inverse21, Inverse22
    ]);
}

export function VectorTransformMatrix(Matrix, Point)
{
    const X = Point[0], Y = Point[1], Z = Point[2];
    return [
        Matrix[0] * X + Matrix[4] * Y + Matrix[8]  * Z + Matrix[12],
        Matrix[1] * X + Matrix[5] * Y + Matrix[9]  * Z + Matrix[13],
        Matrix[2] * X + Matrix[6] * Y + Matrix[10] * Z + Matrix[14],
        Matrix[3] * X + Matrix[7] * Y + Matrix[11] * Z + Matrix[15]
    ];
}

// 📝 Returns normalized device coordinates plus the clip-space W sign test in the fourth slot.
export function ProjectPoint(Matrix, Point)
{
    const Clipped = VectorTransformMatrix(Matrix, Point);
    const Reciprocal = Clipped[3] !== 0.0 ? 1.0 / Clipped[3] : 0.0;
    return [
        Clipped[0] * Reciprocal,
        Clipped[1] * Reciprocal,
        Clipped[2] * Reciprocal,
        Clipped[3]
    ];
}

export function RayFromScreen(ProjectionView, ScreenX, ScreenY, ViewportWidth, ViewportHeight)
{
    const NormalizedX = (ScreenX / ViewportWidth) * 2.0 - 1.0;
    const NormalizedY = -(ScreenY / ViewportHeight) * 2.0 + 1.0;

    const Inverse = MatrixInverse(ProjectionView);
    const NearPoint = ProjectPoint(Inverse, [ NormalizedX, NormalizedY, -1.0 ]);
    const FarPoint  = ProjectPoint(Inverse, [ NormalizedX, NormalizedY,  1.0 ]);

    const Origin = [ NearPoint[0], NearPoint[1], NearPoint[2] ];
    const Direction = VecNormalize(VecSubtract([ FarPoint[0], FarPoint[1], FarPoint[2] ], Origin));
    return { Origin, Direction };
}
