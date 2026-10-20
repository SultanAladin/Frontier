// CPU counterpart of VehicleVisual.wgsl. Deform each wheel triangle once, then refit its BLAS.
// Ray queries consume those triangles directly; no shape evaluation occurs inside a BVH traversal.
import { CourseMaterial } from "./MaterialSpecification.js";
import { TyreSection, TyreAngularCount, TyreSurfaceCount } from "./TyreContinuumElasticity.js";
const Pi = Math.PI, Rim = 0.3448, Radius = 0.50855;
const Clamp = (Entry, Minimum, Maximum) => Math.max(Minimum, Math.min(Maximum, Entry));
const Mix = (First, Second, Blend) => First * (1 - Blend) + Second * Blend;
const Unit = Vector => { const Length = Math.hypot(...Vector) || 1; return Vector.map(Entry => Entry / Length); };
const Cross = (First, Second) => [First[1] * Second[2] - First[2] * Second[1], First[2] * Second[0] - First[0] * Second[2], First[0] * Second[1] - First[1] * Second[0]];

function ConstructContinuumProjection(Position)
{
    const Phi = Math.atan2(Position[2], Position[0]), Radial = Math.hypot(Position[0], Position[2]);
    let Nearest = Infinity, Segment = 0, Blend = 0;
    for (let Index = 0; Index < TyreSection.length - 1; ++Index)
    {
        const First = TyreSection[Index], Second = TyreSection[Index + 1];
        const RadialEdge = Second[0] - First[0], LateralEdge = Second[1] - First[1];
        const Along = Clamp(((Radial - First[0]) * RadialEdge + (Position[1] - First[1]) * LateralEdge)
            / (RadialEdge ** 2 + LateralEdge ** 2), 0, 1);
        const Distance = (Radial - First[0] - Along * RadialEdge) ** 2 + (Position[1] - First[1] - Along * LateralEdge) ** 2;
        if (Distance < Nearest) { Nearest = Distance; Segment = Index; Blend = Along; }
    }
    return { Phi, Radial, Segment, Blend, Sine: Math.sin(Phi), Cosine: Math.cos(Phi) };
}

export function PrepareWheelVertexCpu(Position, Normal)
{
    const UnitNormal = Unit(Normal);
    const First = Unit(Cross(UnitNormal, Math.abs(UnitNormal[0]) > 0.8 ? [0, 1, 0] : [1, 0, 0]));
    const Second = Cross(UnitNormal, First);
    const Continuum = [ConstructContinuumProjection(Position), ...[First, Second].map(Tangent =>
        ConstructContinuumProjection(Position.map((Entry, Axis) => Entry + Tangent[Axis] * 0.0005)))];
    return { ...Continuum[0], First, Second, Continuum };
}

function ContinuumShift(Encoded, Wheel, Position, ContactAngle, Projection = null)
{
    const Coordinates = Projection || ConstructContinuumProjection(Position);
    const { Segment, Blend } = Coordinates;
    const Coordinate = (((Coordinates.Phi - ContactAngle) / (2 * Pi) + 2) % 1) * TyreAngularCount;
    const Low = Math.floor(Coordinate) % TyreAngularCount, High = (Low + 1) % TyreAngularCount;
    const Offset = 180 + Wheel * TyreSurfaceCount * 4;
    const Shift = [0, 1, 2].map(Axis => Mix(
        Mix(Encoded[Offset + (Low * 7 + Segment) * 4 + Axis], Encoded[Offset + (Low * 7 + Segment + 1) * 4 + Axis], Blend),
        Mix(Encoded[Offset + (High * 7 + Segment) * 4 + Axis], Encoded[Offset + (High * 7 + Segment + 1) * 4 + Axis], Blend),
        Coordinate - Math.floor(Coordinate)));
    return [Shift[0] * Coordinates.Cosine - Shift[2] * Coordinates.Sine, Shift[1], Shift[0] * Coordinates.Sine + Shift[2] * Coordinates.Cosine];
}

export function ResolveWheelVertexCpu(Encoded, Wheel, Family, Position, Normal = [0, 0, 1], Projection = null)
{
    if (Family !== CourseMaterial.Tyre) return { Position: Position.slice(), Normal: Normal.slice() };
    if (Encoded[20 + Wheel * 4] > 0.5) return { Position: [0, 0, 0], Normal: Normal.slice() };
    const Offset = 36 + Wheel * 4, ContactAngle = Encoded[Offset], Deflection = Encoded[Offset + 1];
    const Radial = Projection?.Radial ?? Math.hypot(Position[0], Position[2]);
    if (Deflection <= 1e-5 || Radial <= Rim + 1e-4) return { Position: Position.slice(), Normal: Normal.slice() };
    const Phi = Projection?.Phi ?? Math.atan2(Position[2], Position[0]);
    let Delta = Phi - ContactAngle;
    Delta -= 2 * Pi * Math.round(Delta / (2 * Pi));
    const Mode = Encoded[21 + Wheel * 4];
    if (Mode > 1.5)
    {
        const Prepared = Projection || PrepareWheelVertexCpu(Position, Normal);
        const Shift = ContinuumShift(Encoded, Wheel, Position, ContactAngle, Prepared.Continuum[0]);
        const { First, Second } = Prepared, Epsilon = 0.0005;
        const Tangents = [First, Second].map((Tangent, Index) =>
        {
            const NearShift = ContinuumShift(Encoded, Wheel, Position.map((Entry, Axis) => Entry + Tangent[Axis] * Epsilon), ContactAngle, Prepared.Continuum[Index + 1]);
            return Tangent.map((Entry, Axis) => Entry + (NearShift[Axis] - Shift[Axis]) / Epsilon);
        });
        return { Position: Position.map((Entry, Axis) => Entry + Shift[Axis]), Normal: Unit(Cross(...Tangents)) };
    }
    const Fraction = Clamp((Radial - Rim) / (Radius - Rim), 0, 1);
    if (Mode > 0.5)
    {
        const Coordinate = ((Delta / (2 * Pi) + 1) % 1) * 32, Low = Math.floor(Coordinate) % 32, High = (Low + 1) % 32;
        const First = Encoded[52 + Wheel * 32 + Low], Second = Encoded[52 + Wheel * 32 + High];
        const NewRadius = Math.max(Rim, Radial - Mix(First, Second, Coordinate - Math.floor(Coordinate)) * Fraction);
        const Slope = -(Second - First) * 32 / (2 * Pi) * Fraction / Math.max(Radial, 0.01);
        return { Position: [NewRadius * Math.cos(Phi), Position[1], NewRadius * Math.sin(Phi)],
            Normal: Unit(Normal.map((Entry, Axis) => Entry - [-Math.sin(Phi), 0, Math.cos(Phi)][Axis] * Slope)) };
    }
    const Pressure = Math.max(0.05, Encoded[Offset + 2] / 220000);
    const Span = Clamp(0.44 / Math.sqrt(Math.max(0.15, Pressure)), 0.20, 1.15);
    const Absolute = Math.abs(Delta);
    let RadialShift = 0, LateralShift = 0;
    if (Absolute < Span)
    {
        const Envelope = 0.5 * (1 + Math.cos(Absolute / Span * Pi));
        RadialShift = -Deflection * Envelope * Fraction;
        LateralShift = Math.sign(Position[1]) * Deflection * 0.48 / (0.4 + 0.6 * Math.sqrt(Pressure)) * Envelope * Math.sin(Fraction * Pi);
    }
    if (Absolute >= Span && Absolute < Span * 1.5)
        RadialShift += Math.sin((Absolute - Span) / (Span * 0.5) * Pi) * 0.12 * Deflection * Fraction;
    if (Absolute < Span * 1.2)
        LateralShift += Encoded[Offset + 3] * 0.5 * (1 + Math.cos(Absolute / (Span * 1.2) * Pi)) * Fraction;
    const NewRadius = Math.max(Rim, Radial + RadialShift);
    const Tilt = Absolute < Span ? -Math.sin(Delta / Span * Pi) * Deflection / (Radius * Span) : 0;
    return { Position: [NewRadius * Math.cos(Phi), Position[1] + LateralShift, NewRadius * Math.sin(Phi)],
        Normal: Absolute < Span ? Unit(Normal.map((Entry, Axis) => Entry + [-Math.sin(Phi), 0, Math.cos(Phi)][Axis] * Tilt)) : Normal.slice() };
}
