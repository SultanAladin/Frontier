/*============================================================================================================================================
                                                       CLIPMAPSURFACE.JS
============================================================================================================================================*/
// 🧩 Nested crust tessellation windows and their LOD diagnostics, shared by rasterization and the minimap

export function ConstructTerrainClipmap(Override = {})
{
    return {
        RingCount: 6,
        InnerExtent: 12800,             // [m]
        GridSegments: 64,               // [idx]
        ...Override
    };
}

const TierTint = [
    [0.16, 0.86, 0.94], [0.24, 0.66, 0.98], [0.42, 0.48, 0.98],
    [0.66, 0.40, 0.94], [0.90, 0.38, 0.72], [0.98, 0.56, 0.34],
    [0.98, 0.78, 0.30], [0.70, 0.90, 0.36]
];

export function ResolveTerrainWindows(Clipmap, CameraX, CameraZ)
{
    const Windows = [];
    for (let Level = 0; Level < Clipmap.RingCount; ++Level)
    {
        const Extent = Clipmap.InnerExtent * Math.pow(2, Level);
        const CellSpacing = Extent / Clipmap.GridSegments;
        const OriginCellsX = Math.floor(CameraX / CellSpacing) - Math.floor(Clipmap.GridSegments / 2);
        const OriginCellsZ = Math.floor(CameraZ / CellSpacing) - Math.floor(Clipmap.GridSegments / 2);
        Windows.push({
            Level,
            Extent,
            CellSpacing,
            OriginX: OriginCellsX * CellSpacing,
            OriginZ: OriginCellsZ * CellSpacing,
            Tint: TierTint[Level % TierTint.length],
            TriangleCount: Clipmap.GridSegments * Clipmap.GridSegments * 2
        });
    }
    return Windows;
}

export function ComputeTerrainReport(Clipmap, Windows, DomainExtent)
{
    const FinestSpacing = Windows[0].CellSpacing;
    const UniformCells = (DomainExtent / FinestSpacing) ** 2;
    const AdaptiveCells = Windows.reduce((Accumulator, Window) => Accumulator + Window.TriangleCount * 0.5, 0);
    return {
        FinestSpacing,
        CoarsestSpacing: Windows[Windows.length - 1].CellSpacing,
        UniformCells,
        AdaptiveCells,
        SavingsFactor: UniformCells / Math.max(1, AdaptiveCells),
        TriangleCount: Windows.reduce((Accumulator, Window) => Accumulator + Window.TriangleCount, 0)
    };
}

export function ConstructOverlayLines(Heightfield, Windows, WaterWindows, Options)
{
    const Positions = [];
    const AppendLine = (FirstX, FirstY, FirstZ, SecondX, SecondY, SecondZ) =>
        Positions.push(FirstX, FirstY, FirstZ, SecondX, SecondY, SecondZ);

    if (Options.Brush && Options.Brush.Radius > 0)
    {
        const SegmentCount = 72;
        for (let Segment = 0; Segment < SegmentCount; ++Segment)
        {
            const FirstAngle = Segment / SegmentCount * Math.PI * 2;
            const SecondAngle = (Segment + 1) / SegmentCount * Math.PI * 2;
            const FirstX = Options.Brush.X + Math.cos(FirstAngle) * Options.Brush.Radius;
            const FirstZ = Options.Brush.Z + Math.sin(FirstAngle) * Options.Brush.Radius;
            const SecondX = Options.Brush.X + Math.cos(SecondAngle) * Options.Brush.Radius;
            const SecondZ = Options.Brush.Z + Math.sin(SecondAngle) * Options.Brush.Radius;
            AppendLine(FirstX, Heightfield.SampleElevation(FirstX, FirstZ) + 30, FirstZ,
                SecondX, Heightfield.SampleElevation(SecondX, SecondZ) + 30, SecondZ);
        }
    }

    if (Options.ShowSimulationGrid && WaterWindows.length > 0)
    {
        const Window = WaterWindows[0];
        const Step = 4;
        const Y = Options.SeaLevel + 6;
        for (let Cell = 0; Cell <= WaterWindows[0].Extent / WaterWindows[0].CellSpacing; Cell += Step)
        {
            const Offset = Cell * Window.CellSpacing;
            AppendLine(Window.WorldOriginX + Offset, Y, Window.WorldOriginZ,
                Window.WorldOriginX + Offset, Y, Window.WorldOriginZ + Window.Extent);
            AppendLine(Window.WorldOriginX, Y, Window.WorldOriginZ + Offset,
                Window.WorldOriginX + Window.Extent, Y, Window.WorldOriginZ + Offset);
        }
    }

    if (Options.ShowLodBounds)
    {
        for (const Window of WaterWindows)
        {
            const Y = Options.SeaLevel + 40 + Window.Level * 12;
            const MinimumX = Window.WorldOriginX;
            const MinimumZ = Window.WorldOriginZ;
            const MaximumX = MinimumX + Window.Extent;
            const MaximumZ = MinimumZ + Window.Extent;
            AppendLine(MinimumX, Y, MinimumZ, MaximumX, Y, MinimumZ);
            AppendLine(MaximumX, Y, MinimumZ, MaximumX, Y, MaximumZ);
            AppendLine(MaximumX, Y, MaximumZ, MinimumX, Y, MaximumZ);
            AppendLine(MinimumX, Y, MaximumZ, MinimumX, Y, MinimumZ);
        }
    }
    return new Float32Array(Positions);
}
