/*============================================================================================================================================
                                                     SDFSURFACEEXTRACTION.JS
============================================================================================================================================*/
// 🧩 Dual-contouring (surface nets) conversion of a volumetric crust SDF into an editable polygon surface

const CornerOffsets = [
    [0, 0, 0], [1, 0, 0], [1, 0, 1], [0, 0, 1],
    [0, 1, 0], [1, 1, 0], [1, 1, 1], [0, 1, 1]
];

const EdgePairs = [
    [0, 1], [1, 2], [2, 3], [3, 0],
    [4, 5], [5, 6], [6, 7], [7, 4],
    [0, 4], [1, 5], [2, 6], [3, 7]
];

export function ConstructExtractionSpecification(Override = {})
{
    return {
        CenterX: 0,                     // [m]
        CenterZ: 0,                    // [m]
        HorizontalExtent: 51200,       // [m]
        VoxelSize: 800,                // [m]
        VerticalMargin: 1200,          // [m]
        ...Override
    };
}

function NormalizeVector(Vector)
{
    const Length = Math.hypot(Vector[0], Vector[1], Vector[2]) || 1;
    return [Vector[0] / Length, Vector[1] / Length, Vector[2] / Length];
}

// 📝 The isosurface is y = H(x,z), clipped to a bounded solid by lateral and basal planes; every
//    evaluation therefore comes from the same signed-distance definition used for rendering.
export function ConvertSdfToPolygonSurface(Heightfield, Specification)
{
    const StartedAt = Date.now();
    const HorizontalCount = Math.max(8, Math.round(Specification.HorizontalExtent / Specification.VoxelSize));
    const MinimumElevation = Math.min(...SampleExtremes(Heightfield, Specification, HorizontalCount));
    const VerticalMinimum = MinimumElevation - Specification.VerticalMargin;
    const VerticalMaximum = Heightfield.Field.Configuration.LandElevation + Heightfield.Field.Configuration.MountainAmplitude + Specification.VerticalMargin;
    const VerticalCount = Math.max(8, Math.ceil((VerticalMaximum - VerticalMinimum) / Specification.VoxelSize));
    const CellSizeX = Specification.HorizontalExtent / HorizontalCount;
    const CellSizeZ = Specification.HorizontalExtent / HorizontalCount;
    const CellSizeY = (VerticalMaximum - VerticalMinimum) / VerticalCount;
    const OriginX = Specification.CenterX - Specification.HorizontalExtent * 0.5;
    const OriginZ = Specification.CenterZ - Specification.HorizontalExtent * 0.5;
    const OriginY = VerticalMinimum;
    const CountX = HorizontalCount;
    const CountY = VerticalCount;
    const CountZ = HorizontalCount;
    const PointStrideX = 1;
    const PointStrideZ = CountX + 1;
    const PointStrideY = (CountX + 1) * (CountZ + 1);
    const CellStrideX = 1;
    const CellStrideZ = CountX;
    const CellStrideY = CountX * CountZ;

    const EvaluateField = (WorldX, WorldY, WorldZ) =>
    {
        const Surface = WorldY - Heightfield.SampleElevation(WorldX, WorldZ);
        const Lateral = Math.max(Math.abs(WorldX - Specification.CenterX), Math.abs(WorldZ - Specification.CenterZ)) - Specification.HorizontalExtent * 0.5 + CellSizeX * 0.5;
        const Basal = OriginY + CellSizeY * 0.5 - WorldY;
        return Math.max(Surface, Lateral, Basal);
    };

    // ① Sample the volumetric field on a regular lattice.
    const PointCount = (CountX + 1) * (CountY + 1) * (CountZ + 1);
    const FieldSamples = new Float32Array(PointCount);
    for (let LayerY = 0; LayerY <= CountY; ++LayerY)
    {
        const WorldY = OriginY + LayerY * CellSizeY;
        for (let LayerZ = 0; LayerZ <= CountZ; ++LayerZ)
        {
            const WorldZ = OriginZ + LayerZ * CellSizeZ;
            for (let LayerX = 0; LayerX <= CountX; ++LayerX)
            {
                FieldSamples[LayerX * PointStrideX + LayerZ * PointStrideZ + LayerY * PointStrideY] =
                    EvaluateField(OriginX + LayerX * CellSizeX, WorldY, WorldZ);
            }
        }
    }

    const ResolvePointIndex = (LayerX, LayerY, LayerZ) =>
        LayerX * PointStrideX + LayerZ * PointStrideZ + LayerY * PointStrideY;
    const ResolveCellIndex = (LayerX, LayerY, LayerZ) =>
        LayerX * CellStrideX + LayerZ * CellStrideZ + LayerY * CellStrideY;

    // ② Place one dual vertex per sign-changing cell at the mean of its edge zero crossings.
    const VertexPositions = [];
    const CellVertexIndices = new Int32Array(CountX * CountY * CountZ).fill(-1);
    for (let LayerY = 0; LayerY < CountY; ++LayerY)
    {
        for (let LayerZ = 0; LayerZ < CountZ; ++LayerZ)
        {
            for (let LayerX = 0; LayerX < CountX; ++LayerX)
            {
                const CornerSamples = new Float32Array(8);
                let Mask = 0;
                for (let Corner = 0; Corner < 8; ++Corner)
                {
                    const Offset = CornerOffsets[Corner];
                    CornerSamples[Corner] = FieldSamples[ResolvePointIndex(LayerX + Offset[0], LayerY + Offset[1], LayerZ + Offset[2])];
                    if (CornerSamples[Corner] > 0) Mask |= 1 << Corner;
                }
                if (Mask === 0 || Mask === 255) continue;
                let AccumulatorX = 0;
                let AccumulatorY = 0;
                let AccumulatorZ = 0;
                let CrossingCount = 0;
                for (const [FirstCorner, SecondCorner] of EdgePairs)
                {
                    if ((Mask & (1 << FirstCorner)) === (Mask & (1 << SecondCorner))) continue;
                    const First = CornerOffsets[FirstCorner];
                    const Second = CornerOffsets[SecondCorner];
                    const FirstSample = CornerSamples[FirstCorner];
                    const SecondSample = CornerSamples[SecondCorner];
                    const Parameter = FirstSample / (FirstSample - SecondSample);
                    AccumulatorX += First[0] + (Second[0] - First[0]) * Parameter;
                    AccumulatorY += First[1] + (Second[1] - First[1]) * Parameter;
                    AccumulatorZ += First[2] + (Second[2] - First[2]) * Parameter;
                    ++CrossingCount;
                }
                if (CrossingCount === 0) continue;
                CellVertexIndices[ResolveCellIndex(LayerX, LayerY, LayerZ)] = VertexPositions.length / 3;
                VertexPositions.push(
                    OriginX + (LayerX + AccumulatorX / CrossingCount) * CellSizeX,
                    OriginY + (LayerY + AccumulatorY / CrossingCount) * CellSizeY,
                    OriginZ + (LayerZ + AccumulatorZ / CrossingCount) * CellSizeZ
                );
            }
        }
    }

    // ③ Emit a quad around every sign-changing lattice edge; orient each triangle by the field gradient.
    const TriangleIndices = [];
    const AxisStrides = [
        { Along: PointStrideX, CellAlong: CellStrideX, Other: [PointStrideY, PointStrideZ], CellOther: [CellStrideY, CellStrideZ], Limits: [CountX, CountY, CountZ] },
        { Along: PointStrideY, CellAlong: CellStrideY, Other: [PointStrideZ, PointStrideX], CellOther: [CellStrideZ, CellStrideX], Limits: [CountY, CountZ, CountX] },
        { Along: PointStrideZ, CellAlong: CellStrideZ, Other: [PointStrideX, PointStrideY], CellOther: [CellStrideX, CellStrideY], Limits: [CountZ, CountX, CountY] }
    ];

    for (let AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
    {
        const Axis = AxisStrides[AxisIndex];
        const Coordinates = [0, 0, 0];
        for (Coordinates[0] = 0; Coordinates[0] < Axis.Limits[0]; ++Coordinates[0])
        {
            for (Coordinates[1] = 1; Coordinates[1] < Axis.Limits[1]; ++Coordinates[1])
            {
                for (Coordinates[2] = 1; Coordinates[2] < Axis.Limits[2]; ++Coordinates[2])
                {
                    const LatticeX = AxisIndex === 0 ? Coordinates[0] : AxisIndex === 1 ? Coordinates[2] : Coordinates[1];
                    const LatticeY = AxisIndex === 0 ? Coordinates[1] : AxisIndex === 1 ? Coordinates[0] : Coordinates[2];
                    const LatticeZ = AxisIndex === 0 ? Coordinates[2] : AxisIndex === 1 ? Coordinates[1] : Coordinates[0];
                    const StartPoint = ResolvePointIndex(LatticeX, LatticeY, LatticeZ);
                    if ((FieldSamples[StartPoint] > 0) === (FieldSamples[StartPoint + Axis.Along] > 0)) continue;
                    const BaseCell = ResolveCellIndex(LatticeX, LatticeY, LatticeZ);
                    const CellCorners = [
                        BaseCell - Axis.CellOther[0] - Axis.CellOther[1],
                        BaseCell - Axis.CellOther[0],
                        BaseCell,
                        BaseCell - Axis.CellOther[1]
                    ];
                    const Quad = CellCorners.map(CellIndex => CellVertexIndices[CellIndex]);
                    if (Quad.some(VertexIndex => VertexIndex < 0)) continue;
                    const Forward = FieldSamples[StartPoint] < 0;
                    const Emit = (First, Second, Third) =>
                    {
                        TriangleIndices.push(First, Forward ? Second : Third, Forward ? Third : Second);
                    };
                    Emit(Quad[0], Quad[1], Quad[2]);
                    Emit(Quad[0], Quad[2], Quad[3]);
                }
            }
        }
    }

    // ④ Derive gradient normals and enforce outward winding per triangle.
    const Positions = new Float32Array(VertexPositions);
    const Normals = new Float32Array(Positions.length);
    const Epsilon = Specification.VoxelSize * 0.5;
    for (let VertexIndex = 0; VertexIndex < Positions.length / 3; ++VertexIndex)
    {
        const WorldX = Positions[VertexIndex * 3];
        const WorldY = Positions[VertexIndex * 3 + 1];
        const WorldZ = Positions[VertexIndex * 3 + 2];
        const Gradient = NormalizeVector([
            EvaluateField(WorldX + Epsilon, WorldY, WorldZ) - EvaluateField(WorldX - Epsilon, WorldY, WorldZ),
            EvaluateField(WorldX, WorldY + Epsilon, WorldZ) - EvaluateField(WorldX, WorldY - Epsilon, WorldZ),
            EvaluateField(WorldX, WorldY, WorldZ + Epsilon) - EvaluateField(WorldX, WorldY, WorldZ - Epsilon)
        ]);
        Normals[VertexIndex * 3] = Gradient[0];
        Normals[VertexIndex * 3 + 1] = Gradient[1];
        Normals[VertexIndex * 3 + 2] = Gradient[2];
    }

    const Indices = new Uint32Array(TriangleIndices.length);
    for (let TriangleOffset = 0; TriangleOffset < TriangleIndices.length; TriangleOffset += 3)
    {
        const First = TriangleIndices[TriangleOffset];
        const Second = TriangleIndices[TriangleOffset + 1];
        const Third = TriangleIndices[TriangleOffset + 2];
        const EdgeA = [Positions[Second * 3] - Positions[First * 3], Positions[Second * 3 + 1] - Positions[First * 3 + 1], Positions[Second * 3 + 2] - Positions[First * 3 + 2]];
        const EdgeB = [Positions[Third * 3] - Positions[First * 3], Positions[Third * 3 + 1] - Positions[First * 3 + 1], Positions[Third * 3 + 2] - Positions[First * 3 + 2]];
        const FaceNormal = [
            EdgeA[1] * EdgeB[2] - EdgeA[2] * EdgeB[1],
            EdgeA[2] * EdgeB[0] - EdgeA[0] * EdgeB[2],
            EdgeA[0] * EdgeB[1] - EdgeA[1] * EdgeB[0]
        ];
        const AverageNormal = [
            Normals[First * 3] + Normals[Second * 3] + Normals[Third * 3],
            Normals[First * 3 + 1] + Normals[Second * 3 + 1] + Normals[Third * 3 + 1],
            Normals[First * 3 + 2] + Normals[Second * 3 + 2] + Normals[Third * 3 + 2]
        ];
        const Aligned = FaceNormal[0] * AverageNormal[0] + FaceNormal[1] * AverageNormal[1] + FaceNormal[2] * AverageNormal[2] >= 0;
        Indices[TriangleOffset] = First;
        Indices[TriangleOffset + 1] = Aligned ? Second : Third;
        Indices[TriangleOffset + 2] = Aligned ? Third : Second;
    }

    return {
        VertexPositions: Positions,
        VertexNormals: Normals,
        TriangleIndices: Indices,
        VertexCount: Positions.length / 3,
        TriangleCount: Indices.length / 3,
        VoxelCount: CountX * CountY * CountZ,
        CellSize: CellSizeX,
        Bounds: { MinimumX: OriginX, MinimumY: OriginY, MinimumZ: OriginZ, MaximumX: OriginX + CountX * CellSizeX, MaximumY: VerticalMaximum, MaximumZ: OriginZ + CountZ * CellSizeZ },
        ConversionMilliseconds: Date.now() - StartedAt
    };
}

function SampleExtremes(Heightfield, Specification, HorizontalCount)
{
    const Extremes = [];
    for (let SampleIndex = 0; SampleIndex < HorizontalCount; SampleIndex += 4)
    {
        const Offset = -Specification.HorizontalExtent * 0.5 + SampleIndex * Specification.HorizontalExtent / HorizontalCount;
        Extremes.push(
            Heightfield.SampleElevation(Specification.CenterX + Offset, Specification.CenterZ + Offset),
            Heightfield.SampleElevation(Specification.CenterX - Offset, Specification.CenterZ + Offset)
        );
    }
    return Extremes;
}

export function EncodePolygonSurfaceAsWavefront(Surface)
{
    const Lines = ['# Frontier WorldForge crust polygon surface', `# vertices ${Surface.VertexCount} triangles ${Surface.TriangleCount}`];
    const Positions = Surface.VertexPositions;
    const Normals = Surface.VertexNormals;
    for (let VertexIndex = 0; VertexIndex < Surface.VertexCount; ++VertexIndex)
        Lines.push(`v ${Positions[VertexIndex * 3].toFixed(3)} ${Positions[VertexIndex * 3 + 1].toFixed(3)} ${Positions[VertexIndex * 3 + 2].toFixed(3)}`);
    for (let VertexIndex = 0; VertexIndex < Surface.VertexCount; ++VertexIndex)
        Lines.push(`vn ${Normals[VertexIndex * 3].toFixed(5)} ${Normals[VertexIndex * 3 + 1].toFixed(5)} ${Normals[VertexIndex * 3 + 2].toFixed(5)}`);
    for (let TriangleOffset = 0; TriangleOffset < Surface.TriangleIndices.length; TriangleOffset += 3)
    {
        const A = Surface.TriangleIndices[TriangleOffset] + 1;
        const B = Surface.TriangleIndices[TriangleOffset + 1] + 1;
        const C = Surface.TriangleIndices[TriangleOffset + 2] + 1;
        Lines.push(`f ${A}//${A} ${B}//${B} ${C}//${C}`);
    }
    return Lines.join('\n') + '\n';
}

// 📝 Euler characteristic V − E + F is 2 for a watertight genus-zero crust solid.
export function ComputeEulerCharacteristic(Surface)
{
    const EdgeSet = new Set();
    const Indices = Surface.TriangleIndices;
    for (let Offset = 0; Offset < Indices.length; Offset += 3)
    {
        const Corners = [Indices[Offset], Indices[Offset + 1], Indices[Offset + 2]];
        for (let CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const First = Corners[CornerIndex];
            const Second = Corners[(CornerIndex + 1) % 3];
            const Key = First < Second ? `${First}:${Second}` : `${Second}:${First}`;
            EdgeSet.add(Key);
        }
    }
    return Surface.VertexCount - EdgeSet.size + Surface.TriangleCount;
}
