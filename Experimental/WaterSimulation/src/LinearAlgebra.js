/*====================================================================================================================================
                                                    LINEAR ALGEBRA.JS
====================================================================================================================================*/
// 🧩 Column-major 4x4 projection/look-at construction and vector helpers shared by the water prototype

export function Mat4Perspective(Destination, FieldOfView, Aspect, NearPlane, FarPlane)
{
    const Focal = 1.0 / Math.tan(FieldOfView * 0.5);
    const Range = 1.0 / (NearPlane - FarPlane);

    Destination.fill(0);
    Destination[0]  = Focal / Aspect;
    Destination[5]  = Focal;
    Destination[10] = (NearPlane + FarPlane) * Range;
    Destination[11] = -1.0;
    Destination[14] = 2.0 * NearPlane * FarPlane * Range;
    return Destination;
}

export function Mat4LookAt(Destination, Eye, Target, Up)
{
    const Forward = Vec3Normalize([Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]]);
    const Right = Vec3Normalize(Vec3Cross(Forward, Up));
    const UpCorrected = Vec3Cross(Right, Forward);

    Destination[0]  = Right[0];
    Destination[1]  = UpCorrected[0];
    Destination[2]  = -Forward[0];
    Destination[3]  = 0;
    Destination[4]  = Right[1];
    Destination[5]  = UpCorrected[1];
    Destination[6]  = -Forward[1];
    Destination[7]  = 0;
    Destination[8]  = Right[2];
    Destination[9]  = UpCorrected[2];
    Destination[10] = -Forward[2];
    Destination[11] = 0;
    Destination[12] = -Vec3Dot(Right, Eye);
    Destination[13] = -Vec3Dot(UpCorrected, Eye);
    Destination[14] = Vec3Dot(Forward, Eye);
    Destination[15] = 1;
    return Destination;
}

export function Mat4Multiply(Destination, Left, Right)
{
    for (let Column = 0; Column < 4; Column++)
    {
        const C0 = Right[Column * 4 + 0];
        const C1 = Right[Column * 4 + 1];
        const C2 = Right[Column * 4 + 2];
        const C3 = Right[Column * 4 + 3];

        Destination[Column * 4 + 0] = Left[0] * C0 + Left[4] * C1 + Left[8] * C2 + Left[12] * C3;
        Destination[Column * 4 + 1] = Left[1] * C0 + Left[5] * C1 + Left[9] * C2 + Left[13] * C3;
        Destination[Column * 4 + 2] = Left[2] * C0 + Left[6] * C1 + Left[10] * C2 + Left[14] * C3;
        Destination[Column * 4 + 3] = Left[3] * C0 + Left[7] * C1 + Left[11] * C2 + Left[15] * C3;
    }

    return Destination;
}

export function Mat4Identity(Destination)
{
    Destination.fill(0);
    Destination[0]  = 1;
    Destination[5]  = 1;
    Destination[10] = 1;
    Destination[15] = 1;
    return Destination;
}

// 💡 Rigid placement: yaw rotation, uniform scale, translation; used by the vessel pose
export function Mat4FromPose(Destination, Translation, YawRadians, PitchRadians, RollRadians, Scale)
{
    const CosYaw = Math.cos(YawRadians);
    const SinYaw = Math.sin(YawRadians);
    const CosPitch = Math.cos(PitchRadians);
    const SinPitch = Math.sin(PitchRadians);
    const CosRoll = Math.cos(RollRadians);
    const SinRoll = Math.sin(RollRadians);

    const Rotation =
    [
        CosYaw * CosRoll + SinYaw * SinPitch * SinRoll,  CosPitch * SinRoll, -SinYaw * CosRoll + CosYaw * SinPitch * SinRoll,
       -CosYaw * SinRoll + SinYaw * SinPitch * CosRoll,  CosPitch * CosRoll,  SinYaw * SinRoll + CosYaw * SinPitch * CosRoll,
        SinYaw * CosPitch,                             -SinPitch,            CosYaw * CosPitch
    ];

    Destination[0]  = Rotation[0] * Scale;
    Destination[1]  = Rotation[1] * Scale;
    Destination[2]  = Rotation[2] * Scale;
    Destination[3]  = 0;
    Destination[4]  = Rotation[3] * Scale;
    Destination[5]  = Rotation[4] * Scale;
    Destination[6]  = Rotation[5] * Scale;
    Destination[7]  = 0;
    Destination[8]  = Rotation[6] * Scale;
    Destination[9]  = Rotation[7] * Scale;
    Destination[10] = Rotation[8] * Scale;
    Destination[11] = 0;
    Destination[12] = Translation[0];
    Destination[13] = Translation[1];
    Destination[14] = Translation[2];
    Destination[15] = 1;

    return Destination;
}

export function Mat4MirrorAboutWater(Destination, Source)
{
    // 📝 Reflection plane y = 0: negate the y row and flip handedness by negating the y translation term
    for (let Index = 0; Index < 16; Index++)
    {
        Destination[Index] = Source[Index];
    }

    Destination[1]  = -Source[1];
    Destination[5]  = -Source[5];
    Destination[9]  = -Source[9];
    Destination[13] = -Source[13];
    return Destination;
}

export function Vec3Dot(A, B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

export function Vec3Cross(A, B)
{
    return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
}

export function Vec3Normalize(Vector)
{
    const Length = Math.hypot(Vector[0], Vector[1], Vector[2]) || 1.0;
    return [Vector[0] / Length, Vector[1] / Length, Vector[2] / Length];
}

export function Vec3Add(A, B)
{
    return [A[0] + B[0], A[1] + B[1], A[2] + B[2]];
}

export function Vec3Scale(Vector, Scalar)
{
    return [Vector[0] * Scalar, Vector[1] * Scalar, Vector[2] * Scalar];
}

export function Saturate(Scalar)
{
    return Math.min(1, Math.max(0, Scalar));
}

export function Mix(A, B, Factor)
{
    return A + (B - A) * Factor;
}

export function SmoothStep(EdgeLow, EdgeHigh, Position)
{
    const Span = EdgeHigh - EdgeLow;
    const Ratio = Span === 0 ? 0 : Saturate((Position - EdgeLow) / Span);
    return Ratio * Ratio * (3 - 2 * Ratio);
}
