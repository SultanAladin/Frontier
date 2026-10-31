/*====================================================================================================================================
                                             POLYGONSURFACECONSTRUCTION.JS
====================================================================================================================================*/
// 🧩 Procedural triangle-soup surfaces with per-vertex normals, wireframe barycentrics, and local bounds.

import { VecAdd, VecCross, VecNormalize, VecScale, VecSubtract } from "./SpatialMathematics.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRIANGLE ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

function CreateSurfaceAssembly()
{
    return {
        Positions: [],
        Normals: [],
        Barycentrics: [],
        TriangleCount: 0
    };
}

function AppendTriangle(Assembly, Points, Normals)
{
    // 📝 Barycentric corners are fixed per triangle; the fragment stage turns them into uniform-width wire lines.
    const CornerCoordinates = [ [ 1.0, 0.0, 0.0 ], [ 0.0, 1.0, 0.0 ], [ 0.0, 0.0, 1.0 ] ];
    for (let Corner = 0; Corner < 3; Corner += 1)
    {
        Assembly.Positions.push(Points[Corner][0], Points[Corner][1], Points[Corner][2]);
        Assembly.Normals.push(Normals[Corner][0], Normals[Corner][1], Normals[Corner][2]);
        Assembly.Barycentrics.push(CornerCoordinates[Corner][0], CornerCoordinates[Corner][1], CornerCoordinates[Corner][2]);
    }
    Assembly.TriangleCount += 1;
}

function AppendQuad(Assembly, Points, Normals)
{
    AppendTriangle(Assembly, [ Points[0], Points[1], Points[2] ], [ Normals[0], Normals[1], Normals[2] ]);
    AppendTriangle(Assembly, [ Points[0], Points[2], Points[3] ], [ Normals[0], Normals[2], Normals[3] ]);
}

function FinalizeSurface(Assembly)
{
    const Positions = new Float32Array(Assembly.Positions);
    const Normals = new Float32Array(Assembly.Normals);
    const Barycentrics = new Float32Array(Assembly.Barycentrics);

    // 📝 The parametric grids sweep in a left-handed order, so most of their quads arrive clockwise
    //    relative to the authored outward normals (explicit face lists such as the cube arrive correct).
    //    This pass normalizes the winding once at construction, so the raster stage can trust gl_FrontFacing.
    let CorrectedTotal = 0;
    for (let Triangle = 0; Triangle < Assembly.TriangleCount; Triangle += 1)
    {
        const Offset = Triangle * 9;
        const EdgeOne = [ Positions[Offset + 3] - Positions[Offset], Positions[Offset + 4] - Positions[Offset + 1], Positions[Offset + 5] - Positions[Offset + 2] ];
        const EdgeTwo = [ Positions[Offset + 6] - Positions[Offset], Positions[Offset + 7] - Positions[Offset + 1], Positions[Offset + 8] - Positions[Offset + 2] ];

        const GeometricNormal = [
            EdgeOne[1] * EdgeTwo[2] - EdgeOne[2] * EdgeTwo[1],
            EdgeOne[2] * EdgeTwo[0] - EdgeOne[0] * EdgeTwo[2],
            EdgeOne[0] * EdgeTwo[1] - EdgeOne[1] * EdgeTwo[0]
        ];
        const AverageNormal = [
            Normals[Offset] + Normals[Offset + 3] + Normals[Offset + 6],
            Normals[Offset + 1] + Normals[Offset + 4] + Normals[Offset + 7],
            Normals[Offset + 2] + Normals[Offset + 5] + Normals[Offset + 8]
        ];
        const Agreement = GeometricNormal[0] * AverageNormal[0]
            + GeometricNormal[1] * AverageNormal[1]
            + GeometricNormal[2] * AverageNormal[2];

        if (Agreement >= 0.0)
        {
            continue;
        }

        for (let Component = 0; Component < 3; Component += 1)
        {
            const FirstIndex = Offset + 3 + Component;
            const SecondIndex = Offset + 6 + Component;

            const HeldPosition = Positions[FirstIndex];
            Positions[FirstIndex] = Positions[SecondIndex];
            Positions[SecondIndex] = HeldPosition;

            const HeldNormal = Normals[FirstIndex];
            Normals[FirstIndex] = Normals[SecondIndex];
            Normals[SecondIndex] = HeldNormal;

            const HeldBarycentric = Barycentrics[FirstIndex];
            Barycentrics[FirstIndex] = Barycentrics[SecondIndex];
            Barycentrics[SecondIndex] = HeldBarycentric;
        }
        CorrectedTotal += 1;
    }

    const MinimumBoundary = [ Infinity, Infinity, Infinity ];
    const MaximumBoundary = [ -Infinity, -Infinity, -Infinity ];

    for (let Index = 0; Index < Positions.length; Index += 3)
    {
        for (let Axis = 0; Axis < 3; Axis += 1)
        {
            const Coordinate = Positions[Index + Axis];
            if (Coordinate < MinimumBoundary[Axis]) MinimumBoundary[Axis] = Coordinate;
            if (Coordinate > MaximumBoundary[Axis]) MaximumBoundary[Axis] = Coordinate;
        }
    }

    return {
        Positions,
        Normals,
        Barycentrics,
        TriangleCount: Assembly.TriangleCount,
        VertexCount: Positions.length / 3,
        CorrectedWindings: CorrectedTotal,
        MinimumBoundary,
        MaximumBoundary
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TORUS KNOT
//------------------------------------------------------------------------------------------------------------------------

function TorusKnotCurvePoint(CurveParameter, WindingA, WindingB, Radius)
{
    const CosineU = Math.cos(CurveParameter);
    const SineU = Math.sin(CurveParameter);
    const Wound = WindingB / WindingA * CurveParameter;
    const CosineWound = Math.cos(Wound);

    return [
        Radius * (2.0 + CosineWound) * 0.5 * CosineU,
        Radius * (2.0 + CosineWound) * SineU * 0.5,
        Radius * Math.sin(Wound) * 0.5
    ];
}

export function ConstructTorusKnotSurface(WindingA = 2, WindingB = 3, Radius = 1.05, TubeRadius = 0.32, SegmentCount = 140, RadialCount = 16)
{
    const Assembly = CreateSurfaceAssembly();
    const GridPositions = [];
    const GridNormals = [];

    for (let Segment = 0; Segment < SegmentCount; Segment += 1)
    {
        const CurveParameter = Segment / SegmentCount * WindingA * Math.PI * 2.0;
        const CurvePoint = TorusKnotCurvePoint(CurveParameter, WindingA, WindingB, Radius);
        const NextPoint = TorusKnotCurvePoint(CurveParameter + 0.01, WindingA, WindingB, Radius);

        const Tangent = VecNormalize(VecSubtract(NextPoint, CurvePoint));
        const AgainstTangent = VecNormalize(VecAdd(NextPoint, CurvePoint));
        const Binormal = VecNormalize(VecCross(Tangent, AgainstTangent));
        const Normal = VecNormalize(VecCross(Binormal, Tangent));

        const PositionRow = [];
        const NormalRow = [];
        for (let Radial = 0; Radial < RadialCount; Radial += 1)
        {
            const RadialParameter = Radial / RadialCount * Math.PI * 2.0;
            const RadialCosine = Math.cos(RadialParameter);
            const RadialSine = Math.sin(RadialParameter);

            const SurfaceNormal = VecNormalize(VecAdd(VecScale(Normal, RadialCosine), VecScale(Binormal, RadialSine)));
            const SurfacePosition = VecAdd(CurvePoint, VecScale(SurfaceNormal, TubeRadius));

            PositionRow.push(SurfacePosition);
            NormalRow.push(SurfaceNormal);
        }
        GridPositions.push(PositionRow);
        GridNormals.push(NormalRow);
    }

    for (let Segment = 0; Segment < SegmentCount; Segment += 1)
    {
        const NextSegment = (Segment + 1) % SegmentCount;
        for (let Radial = 0; Radial < RadialCount; Radial += 1)
        {
            const NextRadial = (Radial + 1) % RadialCount;
            AppendQuad(
                Assembly,
                [
                    GridPositions[Segment][Radial],
                    GridPositions[NextSegment][Radial],
                    GridPositions[NextSegment][NextRadial],
                    GridPositions[Segment][NextRadial]
                ],
                [
                    GridNormals[Segment][Radial],
                    GridNormals[NextSegment][Radial],
                    GridNormals[NextSegment][NextRadial],
                    GridNormals[Segment][NextRadial]
                ]
            );
        }
    }

    return FinalizeSurface(Assembly);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RING / TORUS
//------------------------------------------------------------------------------------------------------------------------

export function ConstructTorusSurface(Radius = 1.0, TubeRadius = 0.09, SegmentCount = 128, RadialCount = 20)
{
    const Assembly = CreateSurfaceAssembly();
    const GridPositions = [];
    const GridNormals = [];

    for (let Segment = 0; Segment < SegmentCount; Segment += 1)
    {
        const RingParameter = Segment / SegmentCount * Math.PI * 2.0;
        const RingCosine = Math.cos(RingParameter);
        const RingSine = Math.sin(RingParameter);

        const PositionRow = [];
        const NormalRow = [];
        for (let Radial = 0; Radial < RadialCount; Radial += 1)
        {
            const TubeParameter = Radial / RadialCount * Math.PI * 2.0;
            const TubeCosine = Math.cos(TubeParameter);
            const TubeSine = Math.sin(TubeParameter);

            const SurfaceNormal = [ RingCosine * TubeCosine, TubeSine, RingSine * TubeCosine ];
            const SurfacePosition = [
                (Radius + TubeRadius * TubeCosine) * RingCosine,
                TubeRadius * TubeSine,
                (Radius + TubeRadius * TubeCosine) * RingSine
            ];
            PositionRow.push(SurfacePosition);
            NormalRow.push(SurfaceNormal);
        }
        GridPositions.push(PositionRow);
        GridNormals.push(NormalRow);
    }

    for (let Segment = 0; Segment < SegmentCount; Segment += 1)
    {
        const NextSegment = (Segment + 1) % SegmentCount;
        for (let Radial = 0; Radial < RadialCount; Radial += 1)
        {
            const NextRadial = (Radial + 1) % RadialCount;
            AppendQuad(
                Assembly,
                [
                    GridPositions[Segment][Radial],
                    GridPositions[NextSegment][Radial],
                    GridPositions[NextSegment][NextRadial],
                    GridPositions[Segment][NextRadial]
                ],
                [
                    GridNormals[Segment][Radial],
                    GridNormals[NextSegment][Radial],
                    GridNormals[NextSegment][NextRadial],
                    GridNormals[Segment][NextRadial]
                ]
            );
        }
    }

    return FinalizeSurface(Assembly);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    UV SPHERE
//------------------------------------------------------------------------------------------------------------------------

export function ConstructSphereSurface(Radius = 0.95, SegmentCount = 56, RingCount = 36)
{
    const Assembly = CreateSurfaceAssembly();
    const GridPositions = [];
    const GridNormals = [];

    for (let Ring = 0; Ring <= RingCount; Ring += 1)
    {
        const ElevationParameter = Ring / RingCount * Math.PI;
        const ElevationSine = Math.sin(ElevationParameter);
        const ElevationCosine = Math.cos(ElevationParameter);

        const PositionRow = [];
        const NormalRow = [];
        for (let Segment = 0; Segment <= SegmentCount; Segment += 1)
        {
            const AzimuthParameter = Segment / SegmentCount * Math.PI * 2.0;
            const SurfaceNormal = [
                ElevationSine * Math.cos(AzimuthParameter),
                ElevationCosine,
                ElevationSine * Math.sin(AzimuthParameter)
            ];
            PositionRow.push(VecScale(SurfaceNormal, Radius));
            NormalRow.push(SurfaceNormal);
        }
        GridPositions.push(PositionRow);
        GridNormals.push(NormalRow);
    }

    for (let Ring = 0; Ring < RingCount; Ring += 1)
    {
        for (let Segment = 0; Segment < SegmentCount; Segment += 1)
        {
            AppendQuad(
                Assembly,
                [
                    GridPositions[Ring][Segment],
                    GridPositions[Ring + 1][Segment],
                    GridPositions[Ring + 1][Segment + 1],
                    GridPositions[Ring][Segment + 1]
                ],
                [
                    GridNormals[Ring][Segment],
                    GridNormals[Ring + 1][Segment],
                    GridNormals[Ring + 1][Segment + 1],
                    GridNormals[Ring][Segment + 1]
                ]
            );
        }
    }

    return FinalizeSurface(Assembly);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CUBE
//------------------------------------------------------------------------------------------------------------------------

export function ConstructCubeSurface(HalfExtent = 0.72)
{
    const Assembly = CreateSurfaceAssembly();
    const Faces = [
        { Normal: [  0.0,  0.0,  1.0 ], Corners: [ [ -1, -1,  1 ], [  1, -1,  1 ], [  1,  1,  1 ], [ -1,  1,  1 ] ] },
        { Normal: [  0.0,  0.0, -1.0 ], Corners: [ [  1, -1, -1 ], [ -1, -1, -1 ], [ -1,  1, -1 ], [  1,  1, -1 ] ] },
        { Normal: [  1.0,  0.0,  0.0 ], Corners: [ [  1, -1,  1 ], [  1, -1, -1 ], [  1,  1, -1 ], [  1,  1,  1 ] ] },
        { Normal: [ -1.0,  0.0,  0.0 ], Corners: [ [ -1, -1, -1 ], [ -1, -1,  1 ], [ -1,  1,  1 ], [ -1,  1, -1 ] ] },
        { Normal: [  0.0,  1.0,  0.0 ], Corners: [ [ -1,  1,  1 ], [  1,  1,  1 ], [  1,  1, -1 ], [ -1,  1, -1 ] ] },
        { Normal: [  0.0, -1.0,  0.0 ], Corners: [ [ -1, -1, -1 ], [  1, -1, -1 ], [  1, -1,  1 ], [ -1, -1,  1 ] ] }
    ];

    for (const Face of Faces)
    {
        const Points = Face.Corners.map((Corner) => VecScale(Corner, HalfExtent));
        const Normals = Face.Corners.map(() => Face.Normal);
        AppendQuad(Assembly, Points, Normals);
    }

    return FinalizeSurface(Assembly);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CYLINDER
//------------------------------------------------------------------------------------------------------------------------

export function ConstructCylinderSurface(Radius = 0.62, HalfHeight = 0.85, SegmentCount = 64)
{
    const Assembly = CreateSurfaceAssembly();

    for (let Segment = 0; Segment < SegmentCount; Segment += 1)
    {
        const StartAngle = Segment / SegmentCount * Math.PI * 2.0;
        const EndAngle = (Segment + 1) / SegmentCount * Math.PI * 2.0;

        const StartNormal = [ Math.cos(StartAngle), 0.0, Math.sin(StartAngle) ];
        const EndNormal = [ Math.cos(EndAngle), 0.0, Math.sin(EndAngle) ];

        const StartLower = [ StartNormal[0] * Radius, -HalfHeight, StartNormal[2] * Radius ];
        const EndLower = [ EndNormal[0] * Radius, -HalfHeight, EndNormal[2] * Radius ];
        const EndUpper = [ EndNormal[0] * Radius, HalfHeight, EndNormal[2] * Radius ];
        const StartUpper = [ StartNormal[0] * Radius, HalfHeight, StartNormal[2] * Radius ];

        AppendQuad(
            Assembly,
            [ StartLower, EndLower, EndUpper, StartUpper ],
            [ StartNormal, EndNormal, EndNormal, StartNormal ]
        );
    }

    const UpNormal = [ 0.0, 1.0, 0.0 ];
    const DownNormal = [ 0.0, -1.0, 0.0 ];
    for (let Segment = 0; Segment < SegmentCount; Segment += 1)
    {
        const StartAngle = Segment / SegmentCount * Math.PI * 2.0;
        const EndAngle = (Segment + 1) / SegmentCount * Math.PI * 2.0;
        const StartPoint = [ Math.cos(StartAngle) * Radius, HalfHeight, Math.sin(StartAngle) * Radius ];
        const EndPoint = [ Math.cos(EndAngle) * Radius, HalfHeight, Math.sin(EndAngle) * Radius ];
        AppendTriangle(Assembly, [ [ 0.0, HalfHeight, 0.0 ], StartPoint, EndPoint ], [ UpNormal, UpNormal, UpNormal ]);

        const LowerStart = [ StartPoint[0], -HalfHeight, StartPoint[2] ];
        const LowerEnd = [ EndPoint[0], -HalfHeight, EndPoint[2] ];
        AppendTriangle(Assembly, [ [ 0.0, -HalfHeight, 0.0 ], LowerEnd, LowerStart ], [ DownNormal, DownNormal, DownNormal ]);
    }

    return FinalizeSurface(Assembly);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIMITIVE REGISTRY
//------------------------------------------------------------------------------------------------------------------------

const PrimitiveConstructors = {
    knot:     () => ConstructTorusKnotSurface(),
    sphere:   () => ConstructSphereSurface(),
    torus:    () => ConstructTorusSurface(0.95, 0.3, 110, 20),
    cube:     () => ConstructCubeSurface(),
    cylinder: () => ConstructCylinderSurface(),
    halo:     () => ConstructTorusSurface(1.45, 0.03, 160, 12)
};

export const PrimitiveCategory = {
    knot: "Topology Knot",
    sphere: "Geodesic Sphere",
    torus: "Ring Surface",
    cube: "Bounded Volume",
    cylinder: "Extruded Contour",
    halo: "Orbit Ring"
};

// 📝 Nominal tessellation parameters, surfaced in the inspector so the record's cost is legible.
export const PrimitiveTessellation = {
    knot:     { Segments: 140, Rings: 16 },
    sphere:   { Segments: 56, Rings: 36 },
    torus:    { Segments: 110, Rings: 20 },
    cube:     { Segments: 6, Rings: null },
    cylinder: { Segments: 64, Rings: 2 },
    halo:     { Segments: 160, Rings: 12 }
};

export function ConstructPrimitiveSurface(PrimitiveIdentity)
{
    const Constructor = PrimitiveConstructors[PrimitiveIdentity] || PrimitiveConstructors.sphere;
    return Constructor();
}
