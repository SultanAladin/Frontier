// Coarse 3D finite elements for browser comparison: linear elastic shell (CST membrane + hinge bending)
// or constant-strain tetrahedral solid. Incremental, quasi-static contact about an inflated reference.
// Not a calibrated hyperelastic/cord-reinforced production tyre. No XPBD constraints are used here.
export const TyreAngularCount = 16;
export const TyreSection = [[0.3448, -0.105], [0.395, -0.145], [0.478, -0.145],
    [0.50855, 0], [0.478, 0.145], [0.395, 0.145], [0.3448, 0.105]];
export const TyreSurfaceCount = TyreAngularCount * TyreSection.length;
const MaximumCompression = 0.14;
const CurveCount = 15;
const Curves = new Map();
const Geometry = new Map();
const Subtract = (A, B) => A.map((Entry, Axis) => Entry - B[Axis]);
const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Dot = (A, B) => A.reduce((Sum, Entry, Axis) => Sum + Entry * B[Axis], 0);
const Unit = A => { const Length = Math.hypot(...A); return A.map(Entry => Entry / Length); };

function Accumulate(Rows, Indices, Coefficients, Scale)
{
    for (let Row = 0; Row < Indices.length; ++Row)
        for (let Column = 0; Column < Indices.length; ++Column)
        {
            const Contribution = Scale * Coefficients[Row] * Coefficients[Column];
            if (Math.abs(Contribution) < 1e-15) continue;
            const Entries = Rows[Indices[Row]], Index = Indices[Column];
            Entries.set(Index, (Entries.get(Index) || 0) + Contribution);
        }
}

// Plane stress: exact CST strain-displacement gradients in each triangle's tangent basis.
export function MembraneStiffness(Points, Young = 7e6, Poisson = 0.35, Thickness = 0.009)
{
    const First = Subtract(Points[1], Points[0]), Second = Subtract(Points[2], Points[0]);
    const Tangent = Unit(First), Normal = Unit(Cross(First, Second)), Across = Cross(Normal, Tangent);
    const Length = Math.hypot(...First), Abscissa = Dot(Second, Tangent), Height = Dot(Second, Across);
    const Area = Length * Height / 2;
    const Gradients = [[-1 / Length, (Abscissa / Length - 1) / Height],
        [1 / Length, -Abscissa / (Length * Height)], [0, 1 / Height]];
    const Strain = Array.from({ length: 3 }, () => new Float64Array(9));
    for (let Corner = 0; Corner < 3; ++Corner)
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            const [FirstGradient, SecondGradient] = Gradients[Corner];
            Strain[0][Corner * 3 + Axis] = FirstGradient * Tangent[Axis];
            Strain[1][Corner * 3 + Axis] = SecondGradient * Across[Axis];
            Strain[2][Corner * 3 + Axis] = SecondGradient * Tangent[Axis] + FirstGradient * Across[Axis];
        }
    const Elasticity = [[1, Poisson, 0], [Poisson, 1, 0], [0, 0, (1 - Poisson) / 2]];
    const Stiffness = Array.from({ length: 9 }, () => new Float64Array(9));
    for (let Row = 0; Row < 9; ++Row)
        for (let Column = 0; Column < 9; ++Column)
            for (let A = 0; A < 3; ++A)
                for (let B = 0; B < 3; ++B)
                    Stiffness[Row][Column] += Area * Thickness * Young / (1 - Poisson ** 2)
                        * Strain[A][Row] * Elasticity[A][B] * Strain[B][Column];
    return { Stiffness, Area, Strain };
}

// 3D Hooke elasticity integrated exactly over a linear tetrahedron (not a spring lattice).
export function TetrahedralStiffness(Points, Young = 7e6, Poisson = 0.35)
{
    const Edges = Points.slice(1).map(Point => Subtract(Point, Points[0]));
    const Determinant = Dot(Edges[0], Cross(Edges[1], Edges[2]));
    const Gradients = [Cross(Edges[1], Edges[2]), Cross(Edges[2], Edges[0]), Cross(Edges[0], Edges[1])]
        .map(Gradient => Gradient.map(Entry => Entry / Determinant));
    Gradients.unshift([0, 1, 2].map(Axis => -Gradients.reduce((Sum, Gradient) => Sum + Gradient[Axis], 0)));
    const Strain = Array.from({ length: 6 }, () => new Float64Array(12));
    for (let Corner = 0; Corner < 4; ++Corner)
    {
        const [First, Second, Third] = Gradients[Corner], Offset = Corner * 3;
        Strain[0][Offset] = First; Strain[1][Offset + 1] = Second; Strain[2][Offset + 2] = Third;
        Strain[3][Offset] = Second; Strain[3][Offset + 1] = First;
        Strain[4][Offset + 1] = Third; Strain[4][Offset + 2] = Second;
        Strain[5][Offset] = Third; Strain[5][Offset + 2] = First;
    }
    const Lambda = Young * Poisson / ((1 + Poisson) * (1 - 2 * Poisson));
    const Mu = Young / (2 * (1 + Poisson)), Volume = Math.abs(Determinant) / 6;
    const Stiffness = Array.from({ length: 12 }, () => new Float64Array(12));
    for (let Row = 0; Row < 12; ++Row)
        for (let Column = 0; Column < 12; ++Column)
            for (let A = 0; A < 6; ++A)
                for (let B = 0; B < 6; ++B)
                {
                    const Elasticity = A < 3 && B < 3 ? Lambda + (A === B ? 2 * Mu : 0) : A === B ? Mu : 0;
                    Stiffness[Row][Column] += Volume * Strain[A][Row] * Elasticity * Strain[B][Column];
                }
    return { Stiffness, Volume, Strain };
}

function AddStiffness(Rows, Corners, Stiffness)
{
    const Indices = Corners.flatMap(Corner => [Corner * 3, Corner * 3 + 1, Corner * 3 + 2]);
    for (let Row = 0; Row < Indices.length; ++Row)
        for (let Column = 0; Column < Indices.length; ++Column)
        {
            const Entries = Rows[Indices[Row]], Index = Indices[Column];
            Entries.set(Index, (Entries.get(Index) || 0) + Stiffness[Row][Column]);
        }
}

function Dihedral(Points)
{
    const Axis = Unit(Subtract(Points[1], Points[0]));
    const First = Unit(Cross(Subtract(Points[1], Points[0]), Subtract(Points[2], Points[0])));
    const Second = Unit(Cross(Subtract(Points[3], Points[0]), Subtract(Points[1], Points[0])));
    return Math.atan2(Dot(Cross(First, Second), Axis), Dot(First, Second));
}

export function ConstructTyreContinuum(Category = "shell")
{
    if (!["shell", "solid"].includes(Category)) throw new Error("Unknown finite element category");
    if (Geometry.has(Category)) return Geometry.get(Category);
    const Count = TyreSection.length, Surface = [], Triangles = [];
    for (let Angle = 0; Angle < TyreAngularCount; ++Angle)
        for (const [Radius, Lateral] of TyreSection)
        {
            const Phi = Angle * 2 * Math.PI / TyreAngularCount;
            Surface.push([Radius * Math.sin(Phi), Lateral, -Radius * Math.cos(Phi)]);
        }
    for (let Angle = 0; Angle < TyreAngularCount; ++Angle)
        for (let Section = 0; Section < Count - 1; ++Section)
        {
            const A = Angle * Count + Section, B = ((Angle + 1) % TyreAngularCount) * Count + Section;
            if ((Angle + Section) % 2 === 0) Triangles.push([A, B, A + 1], [B, B + 1, A + 1]);
            else Triangles.push([A, B, B + 1], [A, B + 1, A + 1]);
        }
    const Points = Surface.map(Point => Point.slice()), Tetrahedra = [];
    if (Category === "solid")
    {
        // Two through-thickness surfaces form wedge prisms; each is split into three conforming tetrahedra.
        for (let Layer = 0; Layer < 2; ++Layer)
            for (let Index = 0; Index < Surface.length; ++Index)
            {
                const Section = Index % Count, Angle = Math.floor(Index / Count) * 2 * Math.PI / TyreAngularCount;
                const Before = TyreSection[Math.max(0, Section - 1)], After = TyreSection[Math.min(Count - 1, Section + 1)];
                const Normal = Unit([After[1] - Before[1], Before[0] - After[0]]);
                const Offset = (Layer - 1) * 0.009;
                Points[Layer * Surface.length + Index] = [Surface[Index][0] + Math.sin(Angle) * Normal[0] * Offset,
                    Surface[Index][1] + Normal[1] * Offset, Surface[Index][2] - Math.cos(Angle) * Normal[0] * Offset];
            }
        for (const Triangle of Triangles)
        {
            // Mirror-consistent wedge ordering avoids a directional bias in the coarse tetrahedral split.
            const Rank = Index => Math.min(Math.floor(Index / Count), TyreAngularCount - Math.floor(Index / Count)) * Count + Index % Count;
            const [A, B, C] = Triangle.slice().sort((First, Second) => Rank(First) - Rank(Second)), Offset = Surface.length;
            Tetrahedra.push([A, B, C, C + Offset], [A, B, B + Offset, C + Offset], [A, A + Offset, B + Offset, C + Offset]);
        }
    }
    const Rows = Array.from({ length: Points.length * 3 }, () => new Map());
    const Young = 7e6, Poisson = 0.35, Thickness = 0.009;
    if (Category === "shell")
    {
        for (const Triangle of Triangles)
        {
            const Crown = Triangle.some(Index => Index % Count === 3);
            AddStiffness(Rows, Triangle, MembraneStiffness(Triangle.map(Index => Points[Index]), Crown ? Young * 4 : Young, Poisson, Thickness).Stiffness);
        }
        const Adjacency = new Map();
        for (const Triangle of Triangles)
            for (let Edge = 0; Edge < 3; ++Edge)
            {
                const A = Triangle[Edge], B = Triangle[(Edge + 1) % 3], Opposite = Triangle[(Edge + 2) % 3];
                const Key = [Math.min(A, B), Math.max(A, B)].join(":");
                if (!Adjacency.has(Key)) { Adjacency.set(Key, [A, B, Opposite]); continue; }
                const Corners = [...Adjacency.get(Key), Opposite], Coordinates = Corners.map(Index => Points[Index].slice());
                const Gradient = [];
                for (let Corner = 0; Corner < 4; ++Corner)
                    for (let Axis = 0; Axis < 3; ++Axis)
                    {
                        Coordinates[Corner][Axis] += 1e-6; const Plus = Dihedral(Coordinates);
                        Coordinates[Corner][Axis] -= 2e-6; const Minus = Dihedral(Coordinates);
                        Coordinates[Corner][Axis] += 1e-6;
                        Gradient.push(Math.atan2(Math.sin(Plus - Minus), Math.cos(Plus - Minus)) / 2e-6);
                    }
                const EdgeSquared = Dot(Subtract(Points[A], Points[B]), Subtract(Points[A], Points[B]));
                const Area = [Corners.slice(0, 3), [Corners[1], Corners[0], Corners[3]]].reduce((Sum, Face) =>
                    Sum + Math.hypot(...Cross(Subtract(Points[Face[1]], Points[Face[0]]), Subtract(Points[Face[2]], Points[Face[0]]))) / 2, 0);
                Accumulate(Rows, Corners.flatMap(Index => [Index * 3, Index * 3 + 1, Index * 3 + 2]), Gradient,
                    Young * Thickness ** 3 / (12 * (1 - Poisson ** 2)) * EdgeSquared / Area);
            }
    }
    else
        for (const Tetrahedron of Tetrahedra)
        {
            const Crown = Tetrahedron.some(Index => Index % Count === 3);
            AddStiffness(Rows, Tetrahedron, TetrahedralStiffness(Tetrahedron.map(Index => Points[Index]), Crown ? Young * 4 : Young, Poisson).Stiffness);
        }
    const Fixed = Points.map((Point, Index) => Index % Count === 0 || Index % Count === Count - 1);
    // Enclosed cavity differential from oriented surface triangles; bead cylinder is fixed and adds no differential.
    const VolumeGradient = new Float64Array(Points.length * 3);
    for (const Triangle of Triangles)
    {
        const [A, B, C] = Triangle.map(Index => Surface[Index]);
        let Normal = Cross(Subtract(B, A), Subtract(C, A));
        const Centre = A.map((Entry, Axis) => (Entry + B[Axis] + C[Axis]) / 3);
        const Section = Triangle.reduce((Sum, Index) => Sum + TyreSection[Index % Count][0], 0) / 3;
        const Reference = [Centre[0] * (Section - 0.35) / Section, Centre[1], Centre[2] * (Section - 0.35) / Section];
        if (Dot(Normal, Reference) < 0) Normal = Normal.map(Entry => -Entry);
        for (const Corner of Triangle)
            for (let Axis = 0; Axis < 3; ++Axis)
                for (let Layer = 0; Layer < (Category === "solid" ? 2 : 1); ++Layer)
                    VolumeGradient[(Corner + Layer * Surface.length) * 3 + Axis] += Normal[Axis] / (Category === "solid" ? 12 : 6);
    }
    const Result = { Category, Points, Surface, Triangles, Tetrahedra, Rows, Fixed, VolumeGradient };
    Geometry.set(Category, Result);
    return Result;
}

function ConstructPressureStiffness(Continuum, Pressure)
{
    const Rows = Continuum.Rows.map(Row => new Map(Row));
    // Approximate initial-stress geometric stiffness of an already inflated membrane: transverse edge tension.
    // Full follower-pressure and material geometric nonlinearity are deliberately not claimed.
    const Edges = new Set();
    for (const Triangle of Continuum.Triangles)
        for (let Edge = 0; Edge < 3; ++Edge)
        {
            const A = Triangle[Edge], B = Triangle[(Edge + 1) % 3];
            const Key = [Math.min(A, B), Math.max(A, B)].join(":");
            if (Edges.has(Key)) continue;
            Edges.add(Key);
            const Direction = Unit(Subtract(Continuum.Surface[B], Continuum.Surface[A]));
            const Scale = Pressure * 0.07 * 0.35;
            for (let Layer = 0; Layer < (Continuum.Category === "solid" ? 2 : 1); ++Layer)
                for (let Row = 0; Row < 3; ++Row)
                    for (let Column = 0; Column < 3; ++Column)
                    {
                        const Contribution = Scale * ((Row === Column ? 1 : 0) - Direction[Row] * Direction[Column]) / (Continuum.Category === "solid" ? 2 : 1);
                        const First = (A + Layer * TyreSurfaceCount) * 3, Second = (B + Layer * TyreSurfaceCount) * 3;
                        for (const [From, To, Sign] of [[First, First, 1], [Second, Second, 1], [First, Second, -1], [Second, First, -1]])
                        {
                            const Entries = Rows[From + Row], Index = To + Column;
                            Entries.set(Index, (Entries.get(Index) || 0) + Sign * Contribution);
                        }
                    }
        }
    return Rows.map((Entries, Row) => ({ Indices: Int32Array.from(Entries.keys()),
        Coefficients: Float64Array.from(Entries.values()), Diagonal: Entries.get(Row) }));
}

export function SolveTyreContact(Continuum, Pressure, Compression, Sweeps = 800, Previous = null, Prepared = null)
{
    if (!Number.isFinite(Pressure) || Pressure < 0 || !Number.isFinite(Compression) || Compression < 0)
        throw new Error("Invalid finite element contact input");
    const Rows = Prepared || ConstructPressureStiffness(Continuum, Pressure), Count = Rows.length;
    const Displacement = Previous ? Float64Array.from(Previous) : new Float64Array(Count);
    const Gradient = Continuum.VolumeGradient, Bulk = 1.4 * (Pressure + 101325) / 0.034;
    const Lower = new Float64Array(Count).fill(-Infinity), Floor = -0.50855 + Compression;
    for (let Corner = 0; Corner < Continuum.Points.length; ++Corner)
        if (!Continuum.Fixed[Corner]) Lower[Corner * 3 + 2] = Floor - Continuum.Points[Corner][2];
    const Active = new Uint8Array(Count);
    for (let Row = 0; Row < Count; ++Row)
    {
        if (Continuum.Fixed[Math.floor(Row / 3)]) { Active[Row] = 2; Displacement[Row] = 0; }
        else if (Displacement[Row] <= Lower[Row]) { Active[Row] = 1; Displacement[Row] = Lower[Row]; }
    }
    const Multiply = Vector =>
    {
        const Product = new Float64Array(Count), Volume = Dot(Gradient, Vector);
        for (let Row = 0; Row < Count; ++Row)
        {
            const Entries = Rows[Row];
            Product[Row] = Bulk * Gradient[Row] * Volume;
            for (let Column = 0; Column < Entries.Indices.length; ++Column)
                Product[Row] += Entries.Coefficients[Column] * Vector[Entries.Indices[Column]];
        }
        return Product;
    };
    let Iterations = 0;
    // Primal active-set contact with diagonal-preconditioned CG on the unconstrained FEM degrees of freedom.
    // Each converged reduced solve is followed by an explicit primal/dual feasibility check.
    for (let ContactIteration = 0; ContactIteration < 40; ++ContactIteration)
    {
        const Product = Multiply(Displacement), Residual = new Float64Array(Count), Direction = new Float64Array(Count);
        let PreviousDot = 0;
        for (let Row = 0; Row < Count; ++Row)
            if (!Active[Row])
            {
                Residual[Row] = -Product[Row];
                Direction[Row] = Residual[Row] / (Rows[Row].Diagonal + Bulk * Gradient[Row] ** 2);
                PreviousDot += Residual[Row] * Direction[Row];
            }
        for (let Iteration = 0; Iteration < Sweeps && PreviousDot > 1e-16; ++Iteration)
        {
            ++Iterations;
            const DirectionProduct = Multiply(Direction);
            const Denominator = Dot(Direction, DirectionProduct);
            if (Denominator <= 0) break;
            const Step = PreviousDot / Denominator;
            let NextDot = 0, ResidualSquared = 0;
            const Preconditioned = new Float64Array(Count);
            for (let Row = 0; Row < Count; ++Row)
                if (!Active[Row])
                {
                    Displacement[Row] += Step * Direction[Row];
                    Residual[Row] -= Step * DirectionProduct[Row];
                    Preconditioned[Row] = Residual[Row] / (Rows[Row].Diagonal + Bulk * Gradient[Row] ** 2);
                    NextDot += Residual[Row] * Preconditioned[Row];
                    ResidualSquared += Residual[Row] ** 2;
                }
            if (ResidualSquared < 1e-6) break;
            const Ratio = NextDot / PreviousDot;
            for (let Row = 0; Row < Count; ++Row) Direction[Row] = Preconditioned[Row] + Ratio * Direction[Row];
            PreviousDot = NextDot;
        }
        let Changed = false;
        for (let Row = 0; Row < Count; ++Row)
            if (!Active[Row] && Displacement[Row] < Lower[Row] - 1e-10)
            { Active[Row] = 1; Displacement[Row] = Lower[Row]; Changed = true; }
        if (Changed) continue;
        const Reaction = Multiply(Displacement);
        for (let Row = 0; Row < Count; ++Row)
            if (Active[Row] === 1 && Reaction[Row] < -0.001)
            { Active[Row] = 0; Changed = true; }
        if (!Changed) break;
    }
    const VolumeChange = Dot(Gradient, Displacement);
    let Reaction = 0, ResidualSquared = 0, Contacts = 0;
    for (let Row = 0; Row < Count; ++Row)
    {
        if (Continuum.Fixed[Math.floor(Row / 3)]) continue;
        let Residual = Bulk * Gradient[Row] * VolumeChange;
        const Entries = Rows[Row];
        for (let Column = 0; Column < Entries.Indices.length; ++Column)
            Residual += Entries.Coefficients[Column] * Displacement[Entries.Indices[Column]];
        if (Row % 3 === 2 && Displacement[Row] <= Lower[Row] + 1e-8)
        { Reaction += Math.max(0, Residual); ++Contacts; Residual = Math.min(0, Residual); }
        ResidualSquared += Residual ** 2;
    }
    return { Displacement, Reaction, Volume: Math.max(0.01, 0.034 + VolumeChange),
        Residual: Math.sqrt(ResidualSquared), Iterations, Contacts };
}

export class TyreContinuumElasticity
{
    constructor(Category = "shell", Pressure = 220000)
    {
        this.Category = Category;
        this.Continuum = ConstructTyreContinuum(Category);
        this.SurfaceDisplacement = new Float64Array(TyreSurfaceCount * 4);
        this.RadialDisplacement = new Float64Array(32);
        this.AssignPressure(Pressure);
        this.ResolveCompression(0);
    }

    AssignPressure(Pressure)
    {
        if (!Number.isFinite(Pressure) || Pressure < 0) throw new Error("Invalid finite element pressure");
        // Interpolate pressure as well as compression. Nearest-bin pressure jumps create load discontinuities
        // as thermal pressures cross bin edges; continuous interpolation avoids that artificial step.
        this.Pressure = Pressure;
        const LowerPressure = Math.floor(Pressure / 20000) * 20000;
        this.PressureBlend = (Pressure - LowerPressure) / 20000;
        const RetrieveCurve = Quantized =>
        {
            const Key = `${this.Category}:${Quantized}`;
            if (!Curves.has(Key))
            {
                const Rows = ConstructPressureStiffness(this.Continuum, Quantized), Curve = [];
                for (let Sample = 0; Sample < CurveCount; ++Sample)
                    Curve.push(SolveTyreContact(this.Continuum, Quantized, Sample * MaximumCompression / (CurveCount - 1),
                        800, Curve.at(-1)?.Displacement, Rows));
                Curves.set(Key, Curve);
                if (Curves.size > 64) Curves.delete(Curves.keys().next().value);
            }
            return Curves.get(Key);
        };
        this.Curve = RetrieveCurve(LowerPressure);
        this.UpperCurve = this.PressureBlend > 0 ? RetrieveCurve(LowerPressure + 20000) : this.Curve;
    }

    EvaluateReaction(Compression)
    {
        const Coordinate = Math.max(0, Math.min(CurveCount - 1, Compression / MaximumCompression * (CurveCount - 1)));
        const Low = Math.min(CurveCount - 2, Math.floor(Coordinate)), Blend = Coordinate - Low;
        const First = this.Curve[Low].Reaction * (1 - Blend) + this.Curve[Low + 1].Reaction * Blend;
        const Second = this.UpperCurve[Low].Reaction * (1 - Blend) + this.UpperCurve[Low + 1].Reaction * Blend;
        return First * (1 - this.PressureBlend) + Second * this.PressureBlend;
    }

    ResolveCompression(Compression)
    {
        this.Compression = Math.max(0, Math.min(MaximumCompression, Compression));
        const Coordinate = this.Compression / MaximumCompression * (CurveCount - 1);
        const Low = Math.min(CurveCount - 2, Math.floor(Coordinate)), Blend = Coordinate - Low;
        const A = this.Curve[Low], B = this.Curve[Low + 1], C = this.UpperCurve[Low], D = this.UpperCurve[Low + 1];
        const AWeight = (1 - Blend) * (1 - this.PressureBlend), BWeight = Blend * (1 - this.PressureBlend);
        const CWeight = (1 - Blend) * this.PressureBlend, DWeight = Blend * this.PressureBlend;
        this.Reaction = this.EvaluateReaction(this.Compression);
        this.Volume = A.Volume * AWeight + B.Volume * BWeight + C.Volume * CWeight + D.Volume * DWeight;
        // Weighted table-node KKT residual; interpolation does not re-solve contact at the intermediate pose.
        this.Residual = A.Residual * AWeight + B.Residual * BWeight + C.Residual * CWeight + D.Residual * DWeight;
        for (let Corner = 0; Corner < TyreSurfaceCount; ++Corner)
        {
            const Phi = Math.floor(Corner / TyreSection.length) * 2 * Math.PI / TyreAngularCount;
            const Offset = (Corner + (this.Category === "solid" ? TyreSurfaceCount : 0)) * 3;
            const First = A.Displacement[Offset] * AWeight + B.Displacement[Offset] * BWeight + C.Displacement[Offset] * CWeight + D.Displacement[Offset] * DWeight;
            const Second = A.Displacement[Offset + 1] * AWeight + B.Displacement[Offset + 1] * BWeight + C.Displacement[Offset + 1] * CWeight + D.Displacement[Offset + 1] * DWeight;
            const Third = A.Displacement[Offset + 2] * AWeight + B.Displacement[Offset + 2] * BWeight + C.Displacement[Offset + 2] * CWeight + D.Displacement[Offset + 2] * DWeight;
            const Sine = Math.sin(Phi), Cosine = Math.cos(Phi), Target = Corner * 4;
            // All three components, using the outer solid face (or shell midsurface) for the display surface.
            this.SurfaceDisplacement[Target] = First * Sine - Third * Cosine;
            this.SurfaceDisplacement[Target + 1] = Second;
            this.SurfaceDisplacement[Target + 2] = First * Cosine + Third * Sine;
        }
        return this.Reaction;
    }
}
