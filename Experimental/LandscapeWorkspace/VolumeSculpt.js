/*==========================================================================================================================================
                                                               VOLUMESCULPT.JS
==========================================================================================================================================*/
// 🧩 Persistent region-owned XYZ clay/carving strokes, continuous capsule sweeps and shared field evaluation.

export const MaximumSculptSamples = 2048;
let Compilations = new WeakMap();
const EmptyStrokes = [];

export function InvalidateSculptStrokes() { Compilations = new WeakMap(); }
const Clamp = Amount => Math.max(0, Math.min(1, Amount));

export function ValidateSculptStrokes(Region)
{
    if (Region.SculptStrokes === undefined) return 0;
    if (!Array.isArray(Region.SculptStrokes) || Region.SculptStrokes.length > 256) throw Error('Maximum 256 sculpt strokes per region.');
    let Samples = 0;
    for (const Stroke of Region.SculptStrokes)
    {
        if (!['add', 'remove'].includes(Stroke.Mode) || !Number.isFinite(Stroke.Radius) || Stroke.Radius < .001 || Stroke.Radius > 1e6 ||
            !Number.isFinite(Stroke.Strength) || Stroke.Strength <= 0 || Stroke.Strength > 1 ||
            !Array.isArray(Stroke.Points) || !Stroke.Points.length || Stroke.Points.length > MaximumSculptSamples ||
            Stroke.Points.some(Point => !Array.isArray(Point) || Point.length !== 3 || Point.some(Coordinate =>
                !Number.isFinite(Coordinate) || Math.abs(Coordinate) > 1e9))) throw Error('Invalid XYZ sculpt stroke.');
        Samples += Stroke.Points.length;
    }
    return Samples;
}

export function CompileSculptStrokes(Region)
{
    const Source = Region.SculptStrokes;
    if (!Source?.length) return EmptyStrokes;
    const Cached = Compilations.get(Region);
    if (Cached && Cached.Source === Source && Cached.Count === Source.length && Cached.Last === Source.at(-1)) return Cached.Strokes;
    const Strokes = Source.map(Stroke => ({ ...Stroke, Blend: Stroke.Radius * .15,
        Minimum: [0, 1, 2].map(Axis => Math.min(...Stroke.Points.map(Point => Point[Axis])) - Stroke.Radius * 1.2),
        Maximum: [0, 1, 2].map(Axis => Math.max(...Stroke.Points.map(Point => Point[Axis])) + Stroke.Radius * 1.2) }));
    Compilations.set(Region, { Source, Count: Source.length, Last: Source.at(-1), Strokes });
    return Strokes;
}

export function SculptCapsuleDistance(Point, First, Second, Radius)
{
    const Direction = Second.map((Coordinate, Axis) => Coordinate - First[Axis]);
    const Denominator = Direction.reduce((Sum, Coordinate) => Sum + Coordinate * Coordinate, 0);
    const Fraction = Clamp(Point.reduce((Sum, Coordinate, Axis) => Sum + (Coordinate - First[Axis]) * Direction[Axis], 0) / (Denominator || 1));
    return Math.hypot(...Point.map((Coordinate, Axis) => Coordinate - First[Axis] - Fraction * Direction[Axis])) - Radius;
}

function SmoothUnion(First, Second, Blend)
{
    const Weight = Math.max(Blend - Math.abs(First - Second), 0) / Blend;
    return Math.min(First, Second) - Weight * Weight * Blend * .25;
}

export function EvaluateSculptStrokes(Region, Distance, X, Y, Z)
{
    if (!Region.SculptStrokes?.length) return Distance;
    const Point = [X, Y, Z];
    for (const Stroke of CompileSculptStrokes(Region))
    {
        const Box = Math.max(...Point.map((Coordinate, Axis) => Math.max(Stroke.Minimum[Axis] - Coordinate, Coordinate - Stroke.Maximum[Axis])), 0);
        if (Box > (Stroke.Mode === 'add' ? Distance : -Distance) + Stroke.Blend) continue;
        let Brush = Infinity;
        for (let Index = 0; Index < Stroke.Points.length; Index++)
            Brush = Math.min(Brush, SculptCapsuleDistance(Point, Stroke.Points[Math.max(0, Index - 1)], Stroke.Points[Index], Stroke.Radius));
        Distance = Stroke.Mode === 'add' ? SmoothUnion(Distance, Brush, Stroke.Blend) : -SmoothUnion(-Distance, Brush, Stroke.Blend);
    }
    return Distance;
}

export function SculptVerticalBounds(Region, Bounds)
{
    let [Lower, Upper] = Bounds;
    for (const Stroke of CompileSculptStrokes(Region))
    {
        if (Stroke.Mode !== 'add') continue;
        Lower = Math.min(Lower, Stroke.Minimum[1]);
        Upper = Math.max(Upper, Stroke.Maximum[1]);
    }
    return [Lower, Upper];
}

export function SculptBrushCentre(Position, Normal, Radius, Strength, Mode)
{
    const Sign = Mode === 'add' ? -1 : 1;
    return Position.map((Coordinate, Axis) => Coordinate + Normal[Axis] * Radius * (1 - Strength) * Sign);
}
