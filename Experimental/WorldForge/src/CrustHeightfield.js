/*============================================================================================================================================
                                                       CRUSTHEIGHTFIELD.JS
============================================================================================================================================*/
// 🧩 Cached elevation raster of the authored crust field plus replayable sculpt strokes and ray queries

import { EvaluateFractalNoise } from './NumericField.js';

export const SculptTool = Object.freeze({
    Raise: 'Raise',
    Lower: 'Lower',
    Smooth: 'Smooth',
    Flatten: 'Flatten',
    Erode: 'Erode',
    Roughen: 'Roughen'
});

export class CrustHeightfield
{
    constructor(Field, Resolution = 1024)
    {
        this.Field = Field;
        this.Resolution = Resolution;
        this.BaseElevations = new Float32Array(Resolution * Resolution);
        this.SculptDeltas = new Float32Array(Resolution * Resolution);
        this.Elevations = new Float32Array(Resolution * Resolution);
        this.StrokeCommands = [];
        this.UndoCommands = [];
        this.GenerationRevision = 0;
    }

    get CellSpacing()
    {
        return this.Field.Configuration.Extent / this.Resolution;
    }

    get Extent()
    {
        return this.Field.Configuration.Extent;
    }

    ResolveWorldX(Column)
    {
        return -this.Extent * 0.5 + (Column + 0.5) * this.CellSpacing;
    }

    ResolveWorldZ(Row)
    {
        return -this.Extent * 0.5 + (Row + 0.5) * this.CellSpacing;
    }

    ResolveColumn(WorldX)
    {
        return (WorldX + this.Extent * 0.5) / this.CellSpacing - 0.5;
    }

    ResolveRow(WorldZ)
    {
        return (WorldZ + this.Extent * 0.5) / this.CellSpacing - 0.5;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  BASE FIELD ASSEMBLY
    //------------------------------------------------------------------------------------------------------------------------

    AssembleBaseRows(FirstRow, LastRow)
    {
        const Resolution = this.Resolution;
        for (let Row = FirstRow; Row < LastRow; ++Row)
        {
            const WorldZ = this.ResolveWorldZ(Row);
            const RowOffset = Row * Resolution;
            for (let Column = 0; Column < Resolution; ++Column)
            {
                this.BaseElevations[RowOffset + Column] = this.Field.EvaluateElevation(this.ResolveWorldX(Column), WorldZ);
            }
        }
    }

    RegenerateBase()
    {
        this.AssembleBaseRows(0, this.Resolution);
        this.Reconcile();
    }

    async RegenerateBaseAsync(OnProgress = () => {})
    {
        const ChunkRows = 32;
        for (let FirstRow = 0; FirstRow < this.Resolution; FirstRow += ChunkRows)
        {
            this.AssembleBaseRows(FirstRow, Math.min(this.Resolution, FirstRow + ChunkRows));
            OnProgress(Math.min(1, (FirstRow + ChunkRows) / this.Resolution));
            await new Promise(Resolve => setTimeout(Resolve, 0));
        }
        this.Reconcile();
        OnProgress(1);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   SCULPT STROKES
    //------------------------------------------------------------------------------------------------------------------------

    ConstructStroke(Tool, WorldX, WorldZ, Radius, Strength, TargetElevation = 0)
    {
        return { Tool, WorldX, WorldZ, Radius, Strength, TargetElevation, Seed: Math.floor(Math.random() * 100000) };
    }

    ApplyStroke(Stroke)
    {
        this.StrokeCommands.push(Stroke);
        this.UndoCommands.length = 0;
        return this.EvaluateStroke(Stroke);
    }

    EvaluateStroke(Stroke)
    {
        const Resolution = this.Resolution;
        const Spacing = this.CellSpacing;
        const RadiusCells = Math.max(1, Math.ceil(Stroke.Radius / Spacing));
        const CenterColumn = Math.round(this.ResolveColumn(Stroke.WorldX));
        const CenterRow = Math.round(this.ResolveRow(Stroke.WorldZ));
        const FirstColumn = Math.max(0, CenterColumn - RadiusCells);
        const LastColumn = Math.min(Resolution - 1, CenterColumn + RadiusCells);
        const FirstRow = Math.max(0, CenterRow - RadiusCells);
        const LastRow = Math.min(Resolution - 1, CenterRow + RadiusCells);
        const ReadSnapshot = Stroke.Tool === SculptTool.Smooth || Stroke.Tool === SculptTool.Erode
            ? Float32Array.from(this.Elevations) : null;

        for (let Row = FirstRow; Row <= LastRow; ++Row)
        {
            for (let Column = FirstColumn; Column <= LastColumn; ++Column)
            {
                const DeltaX = (Column - CenterColumn) * Spacing;
                const DeltaZ = (Row - CenterRow) * Spacing;
                const Distance = Math.hypot(DeltaX, DeltaZ);
                if (Distance > Stroke.Radius) continue;
                const Falloff = Math.pow(1 - Distance / Stroke.Radius, 2) * (3 - 2 * (1 - Distance / Stroke.Radius));
                const Index = Row * Resolution + Column;
                if (Stroke.Tool === SculptTool.Raise || Stroke.Tool === SculptTool.Lower)
                {
                    const Sign = Stroke.Tool === SculptTool.Raise ? 1 : -1;
                    this.SculptDeltas[Index] += Sign * Stroke.Strength * Falloff;
                }
                else if (Stroke.Tool === SculptTool.Roughen)
                {
                    const WorldX = this.ResolveWorldX(Column);
                    const WorldZ = this.ResolveWorldZ(Row);
                    const Noise = EvaluateFractalNoise(WorldX / (Spacing * 9), WorldZ / (Spacing * 9), Stroke.Seed, 4);
                    this.SculptDeltas[Index] += Noise * Stroke.Strength * Falloff;
                }
                else if (Stroke.Tool === SculptTool.Flatten)
                {
                    const Current = this.BaseElevations[Index] + this.SculptDeltas[Index];
                    this.SculptDeltas[Index] += (Stroke.TargetElevation - Current) * Falloff;
                }
                else if (Stroke.Tool === SculptTool.Smooth)
                {
                    let Accumulator = 0;
                    let Weight = 0;
                    for (let OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
                    {
                        for (let OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            const SampleColumn = Math.max(0, Math.min(Resolution - 1, Column + OffsetX));
                            const SampleRow = Math.max(0, Math.min(Resolution - 1, Row + OffsetZ));
                            Accumulator += ReadSnapshot[SampleRow * Resolution + SampleColumn];
                            Weight += 1;
                        }
                    }
                    const Blurred = Accumulator / Weight;
                    const Current = this.BaseElevations[Index] + this.SculptDeltas[Index];
                    this.SculptDeltas[Index] += (Blurred - Current) * Falloff * Stroke.Strength;
                }
                else if (Stroke.Tool === SculptTool.Erode)
                {
                    const SampleColumn = Math.max(0, Math.min(Resolution - 1, Column - 1));
                    const SampleRow = Math.max(0, Math.min(Resolution - 1, Row - 1));
                    const Lower = Math.min(ReadSnapshot[SampleRow * Resolution + SampleColumn], ReadSnapshot[Row * Resolution + Column]);
                    const Current = this.BaseElevations[Index] + this.SculptDeltas[Index];
                    this.SculptDeltas[Index] += Math.min(0, Lower - Current) * Falloff * Stroke.Strength;
                }
                this.Elevations[Index] = this.BaseElevations[Index] + this.SculptDeltas[Index];
            }
        }
        ++this.GenerationRevision;
        return { FirstColumn, FirstRow, Width: LastColumn - FirstColumn + 1, Height: LastRow - FirstRow + 1 };
    }

    RecomputeDeltas()
    {
        this.SculptDeltas.fill(0);
        for (const Stroke of this.StrokeCommands) this.EvaluateStroke(Stroke);
        this.Reconcile();
    }

    UndoStroke()
    {
        if (this.StrokeCommands.length === 0) return false;
        this.UndoCommands.push(this.StrokeCommands.pop());
        this.RecomputeDeltas();
        return true;
    }

    RedoStroke()
    {
        if (this.UndoCommands.length === 0) return false;
        this.StrokeCommands.push(this.UndoCommands.pop());
        this.RecomputeDeltas();
        return true;
    }

    Reconcile()
    {
        for (let Index = 0; Index < this.Elevations.length; ++Index)
            this.Elevations[Index] = this.BaseElevations[Index] + this.SculptDeltas[Index];
        ++this.GenerationRevision;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  SAMPLING AND QUERIES
    //------------------------------------------------------------------------------------------------------------------------

    SampleElevation(WorldX, WorldZ)
    {
        const Column = Math.max(0, Math.min(this.Resolution - 1.001, this.ResolveColumn(WorldX)));
        const Row = Math.max(0, Math.min(this.Resolution - 1.001, this.ResolveRow(WorldZ)));
        const BaseColumn = Math.floor(Column);
        const BaseRow = Math.floor(Row);
        const FractionX = Column - BaseColumn;
        const FractionZ = Row - BaseRow;
        const NextColumn = Math.min(this.Resolution - 1, BaseColumn + 1);
        const NextRow = Math.min(this.Resolution - 1, BaseRow + 1);
        const SouthWest = this.Elevations[BaseRow * this.Resolution + BaseColumn];
        const SouthEast = this.Elevations[BaseRow * this.Resolution + NextColumn];
        const NorthWest = this.Elevations[NextRow * this.Resolution + BaseColumn];
        const NorthEast = this.Elevations[NextRow * this.Resolution + NextColumn];
        const South = SouthWest + (SouthEast - SouthWest) * FractionX;
        const North = NorthWest + (NorthEast - NorthWest) * FractionX;
        return South + (North - South) * FractionZ;
    }

    // 📝 Coarse sphere-tracing against the raster, then bisection; produces a stable brush anchor at kilometre scale.
    ResolveSurfaceIntersection(Origin, Direction, MaximumDistance = 900000)
    {
        let Travelled = 0;
        let PreviousDelta = Origin[1] - this.SampleElevation(Origin[0], Origin[2]);
        if (PreviousDelta <= 0) return { Hit: false, X: Origin[0], Y: Origin[1], Z: Origin[2], Distance: 0 };
        let Step = Math.max(this.CellSpacing * 0.5, 20);
        while (Travelled < MaximumDistance)
        {
            const Next = Math.min(Travelled + Step, MaximumDistance);
            const SampleX = Origin[0] + Direction[0] * Next;
            const SampleY = Origin[1] + Direction[1] * Next;
            const SampleZ = Origin[2] + Direction[2] * Next;
            const Delta = SampleY - this.SampleElevation(SampleX, SampleZ);
            if (Delta <= 0)
            {
                let Lower = Travelled;
                let Upper = Next;
                for (let Iteration = 0; Iteration < 28; ++Iteration)
                {
                    const Middle = (Lower + Upper) * 0.5;
                    const MiddleY = Origin[1] + Direction[1] * Middle;
                    const MiddleDelta = MiddleY - this.SampleElevation(Origin[0] + Direction[0] * Middle, Origin[2] + Direction[2] * Middle);
                    if (MiddleDelta > 0) Lower = Middle; else Upper = Middle;
                }
                const Distance = (Lower + Upper) * 0.5;
                return { Hit: true, X: Origin[0] + Direction[0] * Distance, Y: Origin[1] + Direction[1] * Distance, Z: Origin[2] + Direction[2] * Distance, Distance };
            }
            Travelled = Next;
            PreviousDelta = Delta;
            Step = Math.min(Step * 1.35, Math.max(this.CellSpacing * 2, 200 + Next * 0.012));
        }
        return { Hit: false, X: Origin[0] + Direction[0] * MaximumDistance, Y: Origin[1] + Direction[1] * MaximumDistance, Z: Origin[2] + Direction[2] * MaximumDistance, Distance: MaximumDistance };
    }

    ComputeStatistics()
    {
        let Minimum = Infinity;
        let Maximum = -Infinity;
        let LandCells = 0;
        let ShorelineCrossings = 0;
        const SeaLevel = this.Field.Configuration.SeaLevel;
        const Resolution = this.Resolution;
        for (let Row = 0; Row < Resolution; ++Row)
        {
            for (let Column = 0; Column < Resolution; ++Column)
            {
                const Elevation = this.Elevations[Row * Resolution + Column];
                Minimum = Math.min(Minimum, Elevation);
                Maximum = Math.max(Maximum, Elevation);
                if (Elevation > SeaLevel) ++LandCells;
                if (Column + 1 < Resolution && (Elevation > SeaLevel) !== (this.Elevations[Row * Resolution + Column + 1] > SeaLevel)) ++ShorelineCrossings;
            }
        }
        const CellCount = Resolution * Resolution;
        return {
            Minimum, Maximum,
            LandFraction: LandCells / CellCount,
            OceanFraction: 1 - LandCells / CellCount,
            LandAreaSquareKilometres: LandCells * (this.CellSpacing / 1000) ** 2,
            ShorelineKilometres: ShorelineCrossings * this.CellSpacing / 1000,
            CellCount
        };
    }

    ExtractRegion(FirstColumn, FirstRow, Width, Height)
    {
        const Region = new Float32Array(Width * Height);
        for (let Row = 0; Row < Height; ++Row)
            Region.set(this.Elevations.subarray((FirstRow + Row) * this.Resolution + FirstColumn, (FirstRow + Row) * this.Resolution + FirstColumn + Width), Row * Width);
        return Region;
    }
}
