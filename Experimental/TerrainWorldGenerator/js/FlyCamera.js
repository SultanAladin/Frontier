/*====================================================================================================
                                      FLYCAMERA.JS
====================================================================================================*/
// 🧩 Unreal-style WASD fly camera: WASD translate along the camera frame, Q/E move up
// and down, Shift sprints, and mouse delta rotates yaw/pitch. An orbit fallback is kept
// for pointer-lock-free hosts (drag to orbit the view target).

// [m/s] - Ground walking pace.
const WalkSpeedMps = 8.0;
// [m/s] - Sprint pace (Shift held).
const SprintSpeedMps = 30.0;
// [rad/px] - Mouse look sensitivity.
const SensitivityRadPerPx = 0.0025;
// [rad] - Pitch clamp so the camera never flips over the poles.
const PitchMinRad = -1.50;
const PitchMaxRad = 1.00;

function BuildVector(X, Y, Z) { return [X, Y, Z]; }

function Add(A, B) { return [A[0] + B[0], A[1] + B[1], A[2] + B[2]]; }

function Subtract(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }

function Scale(Vector, Factor) { return [Vector[0] * Factor, Vector[1] * Factor, Vector[2] * Factor]; }

function Length(Vector) { return Math.hypot(Vector[0], Vector[1], Vector[2]); }

function Normalize(Vector)
{
    const L = Length(Vector);
    if (L === 0.0) return [0.0, 0.0, 0.0];
    return [Vector[0] / L, Vector[1] / L, Vector[2] / L];
}

function Cross(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    ];
}

function LookAt(Eye, Target, WorldUp)
{
    const Forward = Normalize(Subtract(Target, Eye));
    const Side = Normalize(Cross(Forward, WorldUp));
    const Up = Cross(Side, Forward);
    return [
        Side[0], Up[0], -Forward[0], 0.0,
        Side[1], Up[1], -Forward[1], 0.0,
        Side[2], Up[2], -Forward[2], 0.0,
        -Dot(Side, Eye), -Dot(Up, Eye), Dot(Forward, Eye), 1.0
    ];
}

function Dot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }

function Perspective(FieldOfViewRad, Aspect, Near, Far)
{
    const F = 1.0 / Math.tan(FieldOfViewRad / 2.0);
    const Nf = 1.0 / (Near - Far);
    return [
        F / Aspect, 0.0, 0.0, 0.0,
        0.0, F, 0.0, 0.0,
        0.0, 0.0, (Far + Near) * Nf, -1.0,
        0.0, 0.0, 2.0 * Far * Near * Nf, 0.0
    ];
}

function Multiply(A, B)
{
    const Out = new Float32Array(16);
    for (let I = 0; I < 4; I++)
    {
        for (let J = 0; J < 4; J++)
        {
            let Sum = 0.0;
            for (let K = 0; K < 4; K++) Sum += A[I * 4 + K] * B[K * 4 + J];
            Out[I * 4 + J] = Sum;
        }
    }
    return Out;
}

function Inverse4(A)
{
    // Simple affine inverse: rotation is orthonormal, translation is negated into it.
    const Out = new Float32Array(16);
    Out[0] = A[0]; Out[1] = A[4]; Out[2] = A[8]; Out[3] = 0.0;
    Out[4] = A[1]; Out[5] = A[5]; Out[6] = A[9]; Out[7] = 0.0;
    Out[8] = A[2]; Out[9] = A[6]; Out[10] = A[10]; Out[11] = 0.0;
    Out[12] = -(A[0] * A[12] + A[1] * A[13] + A[2] * A[14]);
    Out[13] = -(A[4] * A[12] + A[5] * A[13] + A[6] * A[14]);
    Out[14] = -(A[8] * A[12] + A[9] * A[13] + A[10] * A[14]);
    Out[15] = 1.0;
    return Out;
}

function TransformPoint(Matrix, Point)
{
    const X = Point[0];
    const Y = Point[1];
    const Z = Point[2];
    const W = Point[3] === undefined ? 1.0 : Point[3];
    const InvW = 1.0 / (Matrix[3] * X + Matrix[7] * Y + Matrix[11] * Z + Matrix[15] * W);
    return [
        (Matrix[0] * X + Matrix[4] * Y + Matrix[8] * Z + Matrix[12] * W) * InvW,
        (Matrix[1] * X + Matrix[5] * Y + Matrix[9] * Z + Matrix[13] * W) * InvW,
        (Matrix[2] * X + Matrix[6] * Y + Matrix[10] * Z + Matrix[14] * W) * InvW
    ];
}

class FlyCamera
{
    constructor()
    {
        this.Position = BuildVector(0.0, 40.0, 0.0);
        this.Yaw = 0.0;
        this.Pitch = -0.25;
        this.WalkSpeedMps = WalkSpeedMps;
        this.SprintSpeedMps = SprintSpeedMps;
        this.SensitivityRadPerPx = SensitivityRadPerPx;
        this.Keys = {};
        this.OrbitYaw = 0.0;
        this.OrbitPitch = -0.25;
        this.OrbitDistanceM = 250.0;
    }

    // Reset the camera over a world position (used for base zoom-to).
    ResetOver(WorldX, WorldZ)
    {
        this.Position = BuildVector(WorldX, 60.0, WorldZ);
        this.Yaw = 0.0;
        this.Pitch = -0.35;
    }

    ForwardVector()
    {
        return BuildVector(
            Math.sin(this.Yaw) * Math.cos(this.Pitch),
            -Math.sin(this.Pitch),
            -Math.cos(this.Yaw) * Math.cos(this.Pitch)
        );
    }

    RightVector()
    {
        return BuildVector(Math.cos(this.Yaw), 0.0, Math.sin(this.Yaw));
    }

    // [rad/px] - Rotate yaw/pitch from mouse delta.
    Rotate(DeltaX, DeltaY)
    {
        this.Yaw -= DeltaX * this.SensitivityRadPerPx;
        this.Pitch -= DeltaY * this.SensitivityRadPerPx;
        this.Pitch = Math.min(PitchMaxRad, Math.max(PitchMinRad, this.Pitch));
    }

    // Orbit fallback (no pointer lock): drag rotates around the view target.
    Orbit(DeltaX, DeltaY)
    {
        this.OrbitYaw -= DeltaX * this.SensitivityRadPerPx;
        this.OrbitPitch -= DeltaY * this.SensitivityRadPerPx;
        this.OrbitPitch = Math.min(PitchMaxRad, Math.max(PitchMinRad, this.OrbitPitch));
    }

    OrbitZoom(Delta)
    {
        this.OrbitDistanceM = Math.min(2000.0, Math.max(40.0, this.OrbitDistanceM * (1.0 + Delta)));
    }

    // Advance the camera by DeltaTauSec seconds according to the held keys.
    Update(DeltaTauSec)
    {
        const Forward = this.ForwardVector();
        const Right = this.RightVector();
        const Up = BuildVector(0.0, 1.0, 0.0);
        const Speed = this.Keys['Shift'] ? this.SprintSpeedMps : this.WalkSpeedMps;
        const Move = BuildVector(0.0, 0.0, 0.0);
        if (this.Keys['w'] || this.Keys['W']) { AddInPlace(Move, Scale(Forward, 1.0)); }
        if (this.Keys['s'] || this.Keys['S']) { AddInPlace(Move, Scale(Forward, -1.0)); }
        if (this.Keys['d'] || this.Keys['D']) { AddInPlace(Move, Scale(Right, 1.0)); }
        if (this.Keys['a'] || this.Keys['A']) { AddInPlace(Move, Scale(Right, -1.0)); }
        if (this.Keys['e'] || this.Keys['E']) { AddInPlace(Move, Scale(Up, 1.0)); }
        if (this.Keys['q'] || this.Keys['Q']) { AddInPlace(Move, Scale(Up, -1.0)); }
        const MoveLength = Length(Move);
        if (MoveLength > 0.0)
        {
            const Step = Scale(Normalize(Move), Speed * DeltaTauSec);
            this.Position = Add(this.Position, Step);
        }
    }

    ViewMatrix()
    {
        const Target = Add(this.Position, this.ForwardVector());
        return LookAt(this.Position, Target, BuildVector(0.0, 1.0, 0.0));
    }

    OrbitViewMatrix()
    {
        const Forward = BuildVector(
            Math.sin(this.OrbitYaw) * Math.cos(this.OrbitPitch),
            -Math.sin(this.OrbitPitch),
            -Math.cos(this.OrbitYaw) * Math.cos(this.OrbitPitch)
        );
        const Eye = Add(Subtract(this.Position, Scale(Forward, this.OrbitDistanceM)), BuildVector(0.0, 0.0, 0.0));
        return LookAt(Eye, this.Position, BuildVector(0.0, 1.0, 0.0));
    }

    ProjectionMatrix(Aspect, Near = 0.5, Far = 40000.0)
    {
        return Perspective(Math.PI / 4.0, Aspect, Near, Far);
    }

    ViewProjectionMatrix(Aspect)
    {
        return Multiply(this.ProjectionMatrix(Aspect), this.ViewMatrix());
    }
}

function AddInPlace(A, B)
{
    A[0] += B[0];
    A[1] += B[1];
    A[2] += B[2];
}

export { FlyCamera, BuildVector, Add, Subtract, Scale, Length, Normalize, Cross, Dot, LookAt, Perspective, Multiply, Inverse4, TransformPoint };
