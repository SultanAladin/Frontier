/*==========================================================================================================================================
                                                               VOLUMEDETAIL.JS
==========================================================================================================================================*/
// 🧩 Region-owned anisotropic XYZ lattices, bounded working bricks and matching coarse/fine tetrahedral interfaces.

import { ConstructVolumeLattice } from './VolumeLattice.js';
import {
    PartitionSurfaceRegions, ContourTetrahedron, SurfaceBufferAccumulator, SurfaceCapacityExceeded,
    MaximumSurfaceSamples, MaximumSurfaceTriangles, MaximumSurfaceRegions, SurfacePreparationSliceMilliseconds, SurfaceCellCorners, SurfaceCellTetrahedra
} from './VolumeSurface.js';

const ContextLattices = new WeakMap();
const Clamp = (Coordinate, Lower, Upper) => Math.max(Lower, Math.min(Upper, Coordinate));
const PowerOfTwo = Ratio => 2 ** Math.max(0, Math.ceil(Math.log2(Math.max(1, Ratio))));
const Normalize = Direction => { const Length = Math.hypot(...Direction); return Length > 1e-15 ? Direction.map(Coordinate => Coordinate / Length) : [0, 1, 0]; };

//------------------------------------------------------------------------------------------------------------------------
//                                                   INDEPENDENT REGION SAMPLING
//------------------------------------------------------------------------------------------------------------------------

export function PlanWorkingSurface(Field, Request = {}, WorkingCells = 64, ContextExpansion = 1)
{
    if (!Field.Entries.length) return { Regions: [], Samples: 0, Working: null, Key: 'empty' };
    if (Field.Entries.length > MaximumSurfaceRegions) throw Error('Too many allocated regions for working detail.');
    const Selected = Field.Entries.find(Entry => Entry.LayerIndex === Request.LayerIndex &&
        Entry.Layer.Regions.indexOf(Entry.Region) === Request.RegionIndex) || Field.Entries[0];
    const Longest = Math.max(...Field.Entries.map(Entry => Math.max(Entry.Upper[0] - Entry.Lower[0], Entry.Upper[2] - Entry.Lower[2])));
    const Regions = PartitionSurfaceRegions(Field, Math.max(.05, Longest / 64));
    const OwnerLattice = ConstructVolumeLattice(Selected.Layer, Selected.Region);
    let Samples = 0;
    for (let Expansion = ContextExpansion; Expansion <= 65536; Expansion *= 2)
    {
        Samples = 0;
        for (const Region of Regions)
        {
            Region.Anchor = Region.Entries.includes(Selected) ? Selected : Region.Entries[0];
            const Lattice = ConstructVolumeLattice(Region.Anchor.Layer, Region.Anchor.Region);
            const Minimum = [0, 1, 2].map(Axis => Math.min(...Region.Entries.map(Entry => Entry.Lower[Axis])));
            const Maximum = [0, 1, 2].map(Axis => Math.max(...Region.Entries.map(Entry => Entry.Upper[Axis])));
            // 📝 A common dyadic factor keeps all six tetrahedral diagonals nested under anisotropic XYZ refinement.
            const Factor = Math.max(...[128, 96, 128].map((Count, Axis) =>
                PowerOfTwo((Maximum[Axis] - Minimum[Axis]) / Lattice.Pitch[Axis] / Count))) * Expansion;
            Region.Factors = [Factor, Factor, Factor];
            Region.Pitch = Region.Factors.map((Factor, Axis) => Lattice.Pitch[Axis] * Factor);
            Region.Minimum = Minimum.map((Coordinate, Axis) => Lattice.Minimum[Axis] +
                (Math.floor((Coordinate - Lattice.Minimum[Axis]) / Region.Pitch[Axis]) - 1) * Region.Pitch[Axis]);
            Region.Maximum = Maximum.map((Coordinate, Axis) => Lattice.Minimum[Axis] +
                (Math.ceil((Coordinate - Lattice.Minimum[Axis]) / Region.Pitch[Axis]) + 1) * Region.Pitch[Axis]);
            Region.Samples = Region.Maximum.map((Coordinate, Axis) => Math.round((Coordinate - Region.Minimum[Axis]) / Region.Pitch[Axis]) + 1);
            Region.Count = Region.Samples.reduce((Product, Count) => Product * Count, 1);
            Samples += Region.Count;
        }
        if (Samples <= MaximumSurfaceSamples / 2) break;
    }
    if (Samples > MaximumSurfaceSamples / 2) throw Error('The landscape context exceeds its bounded lattice budget.');
    const Region = Regions.find(Part => Part.Entries.includes(Selected));
    const Footprint = Math.max(.001, (Request.Distance || 1000) * 3 / (1.65 * Math.max(64, Request.PixelHeight || 800)));
    const Level = Request.Force ? 0 : Math.max(0, Math.floor(Math.log2(Footprint / Math.min(OwnerLattice.Pitch[0], OwnerLattice.Pitch[2]))));
    const Factors = Region.Factors.map(Factor => Math.min(Factor, 2 ** Math.min(Level, 12)));
    const Pitch = Factors.map((Factor, Axis) => OwnerLattice.Pitch[Axis] * Factor);
    const Point = [0, 1, 2].map(Axis => Clamp(Request.Point?.[Axis] ?? (Selected.Lower[Axis] + Selected.Upper[Axis]) / 2,
        Selected.Lower[Axis], Selected.Upper[Axis]));
    const Available = Region.Maximum.map((Coordinate, Axis) => Math.round((Coordinate - Region.Minimum[Axis]) / Pitch[Axis]));
    const Cells = Available.map(Count => Math.max(2, Math.min(WorkingCells, Count - 2)));
    const Offset = Point.map((Coordinate, Axis) => Clamp(Math.round((Coordinate - Region.Minimum[Axis]) / Pitch[Axis] - Cells[Axis] / 2),
        1, Available[Axis] - Cells[Axis] - 1));
    const Minimum = Offset.map((Coordinate, Axis) => Region.Minimum[Axis] + Coordinate * Pitch[Axis]);
    const Maximum = Minimum.map((Coordinate, Axis) => Coordinate + Cells[Axis] * Pitch[Axis]);
    const Working = {
        LayerIndex: Selected.LayerIndex, RegionIndex: Selected.Layer.Regions.indexOf(Selected.Region),
        Dimensions: OwnerLattice.Dimensions, TargetPitch: OwnerLattice.Pitch, Pitch, Factors, Point, Minimum, Maximum, Cells,
        DenseCells: OwnerLattice.Cells, Region
    };
    const Key = JSON.stringify([Working.LayerIndex, Working.RegionIndex, OwnerLattice.Dimensions, Minimum, Cells, Factors]);
    return { Regions, Samples, Working, Key };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATCHING TETRAHEDRAL BOUNDARIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 This is the actual coarse piecewise-linear tetrahedral field, not trilinear interpolation.
// Nested fine samples on a working-box face reproduce the clipped coarse contours up to floating-point rounding.
export function InterpolateLatticeTetrahedron(Lattice, Position)
{
    const Cell = Position.map((Coordinate, Axis) => Clamp((Coordinate - Lattice.Minimum[Axis]) / Lattice.Pitch[Axis], 0, Lattice.Samples[Axis] - 1));
    const Lower = Cell.map((Coordinate, Axis) => Math.min(Lattice.Samples[Axis] - 2, Math.floor(Coordinate)));
    const Fraction = Cell.map((Coordinate, Axis) => Coordinate - Lower[Axis]);
    const Axes = [0, 1, 2].sort((First, Second) => Fraction[Second] - Fraction[First]);
    const Offset = (X, Y, Z) => (Z * Lattice.Samples[1] + Y) * Lattice.Samples[0] + X;
    const Index = [...Lower];
    let Previous = Lattice.Distances[Offset(...Index)], Distance = Previous;
    for (const Axis of Axes)
    {
        Index[Axis]++;
        const Next = Lattice.Distances[Offset(...Index)];
        Distance += (Next - Previous) * Fraction[Axis];
        Previous = Next;
    }
    return Distance;
}

export function WorkingBoundaryWeight(Working, Position)
{
    const Distance = Math.min(...Position.map((Coordinate, Axis) =>
        Math.min(Coordinate - Working.Minimum[Axis], Working.Maximum[Axis] - Coordinate) / Working.Pitch[Axis]));
    const Fraction = Clamp(Distance / 4, 0, 1);
    return Fraction * Fraction * (3 - 2 * Fraction);
}

function LatticeGradient(Lattice, X, Y, Z)
{
    const At = (A, B, C) => Lattice.Distances[(C * Lattice.Samples[1] + B) * Lattice.Samples[0] + A];
    const Coordinate = [X, Y, Z];
    return Normalize(Coordinate.map((Part, Axis) =>
    {
        const Upper = [...Coordinate], Lower = [...Coordinate];
        Upper[Axis] = Math.min(Part + 1, Lattice.Samples[Axis] - 1);
        Lower[Axis] = Math.max(0, Part - 1);
        return (At(...Upper) - At(...Lower)) / ((Upper[Axis] - Lower[Axis]) * Lattice.Pitch[Axis]);
    }));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   BOUNDED WORKING-BRICK EXTRACTION
//------------------------------------------------------------------------------------------------------------------------

export async function ConstructWorkingSurface(Field, Request = {}, Options = {})
{
    const Start = performance.now();
    let Plan = PlanWorkingSurface(Field, Request);
    const RequestedKey = Request.Key || Plan.Key;
    const Valid = Options.Current || (() => true);
    let Slice = Start, MaximumSliceMilliseconds = 0, Evaluations = 0, Completed = 0;
    const Colours = Field.Layers.map(Layer => [1, 3, 5].map(Offset => parseInt(Layer.Color.slice(Offset, Offset + 2), 16) / 255));
    async function Checkpoint()
    {
        if (!Valid()) throw new DOMException('Working detail superseded.', 'AbortError');
        if (performance.now() - Slice < SurfacePreparationSliceMilliseconds) return;
        MaximumSliceMilliseconds = Math.max(MaximumSliceMilliseconds, performance.now() - Slice);
        Options.Progress?.({ Completed, Samples: Math.min(MaximumSurfaceSamples, Plan.Samples + 67 ** 3), Phase: 'working' });
        await (Options.YieldExecution || (() => new Promise(Resolve => setTimeout(Resolve, 0))))();
        if (!Valid()) throw new DOMException('Working detail superseded.', 'AbortError');
        Slice = performance.now();
    }
    async function Sample(Lattice, Coarse = null, Working = null)
    {
        const [SamplesX, SamplesY, SamplesZ] = Lattice.Samples;
        const Count = SamplesX * SamplesY * SamplesZ;
        if (Count + Completed > MaximumSurfaceSamples) throw Error('The working lattice exceeds its sampling budget.');
        Lattice.Distances = new Float32Array(Count);
        Lattice.Indices = new Uint8Array(Count);
        const Planars = Lattice.Entries.map(() => new Float64Array(SamplesX * SamplesZ));
        for (let Z = 0; Z < SamplesZ; Z++)
        for (let X = 0; X < SamplesX; X++)
        {
            Lattice.Entries.forEach((Entry, Index) => Planars[Index][Z * SamplesX + X] = Field.Planar(Entry,
                Lattice.Minimum[0] + X * Lattice.Pitch[0], Lattice.Minimum[2] + Z * Lattice.Pitch[2]));
            if ((X & 15) === 0) await Checkpoint();
        }
        const Column = new Float64Array(Lattice.Entries.length);
        let Index = 0;
        for (let Z = 0; Z < SamplesZ; Z++)
        for (let Y = 0; Y < SamplesY; Y++)
        for (let X = 0; X < SamplesX; X++)
        {
            Planars.forEach((Part, EntryIndex) => Column[EntryIndex] = Part[Z * SamplesX + X]);
            const Position = [X, Y, Z].map((Coordinate, Axis) => Lattice.Minimum[Axis] + Coordinate * Lattice.Pitch[Axis]);
            const Result = Field.Sample(...Position, Lattice.Entries, Column);
            Evaluations++;
            const Weight = Working ? WorkingBoundaryWeight(Working, Position) : 1;
            Lattice.Distances[Index] = Weight === 1 ? Result.Distance :
                InterpolateLatticeTetrahedron(Coarse, Position) * (1 - Weight) + Result.Distance * Weight;
            Lattice.Indices[Index++] = Result.LayerIndex;
            Completed++;
            if ((Index & 31) === 0) await Checkpoint();
        }
    }
    async function Contour(Lattice, Output, Working = false)
    {
        const Origin = [0, 1, 2].map(Axis => Axis === 1 ? 0 :
            Lattice.Minimum[Axis] + Math.floor((Lattice.Samples[Axis] - 1) / 2) * Lattice.Pitch[Axis]);
        Output.ConfigureOrigin(Origin);
        const Vertex = (X, Y, Z) =>
        {
            const Index = (Z * Lattice.Samples[1] + Y) * Lattice.Samples[0] + X;
            return { Position: [X, Y, Z].map((Coordinate, Axis) => Lattice.Minimum[Axis] + Coordinate * Lattice.Pitch[Axis]),
                Normal: LatticeGradient(Lattice, X, Y, Z), Distance: Lattice.Distances[Index],
                Color: Colours[Lattice.Indices[Index]] || [.72, .64, .5] };
        };
        const Lower = Working ? 1 : 0, Padding = Working ? 2 : 1;
        for (let Z = Lower; Z < Lattice.Samples[2] - Padding; Z++)
        for (let Y = Lower; Y < Lattice.Samples[1] - Padding; Y++)
        for (let X = Lower; X < Lattice.Samples[0] - Padding; X++)
        {
            const Cell = SurfaceCellCorners.map(([DX, DY, DZ]) => ((Z + DZ) * Lattice.Samples[1] + Y + DY) * Lattice.Samples[0] + X + DX);
            if (Cell.some(Offset => Lattice.Distances[Offset] < 0) && Cell.some(Offset => Lattice.Distances[Offset] >= 0))
            {
                const Vertices = SurfaceCellCorners.map(([DX, DY, DZ]) => Vertex(X + DX, Y + DY, Z + DZ));
                for (const Tetrahedron of SurfaceCellTetrahedra) ContourTetrahedron(Tetrahedron.map(Offset => Vertices[Offset]), Output.AccumulateTriangle);
            }
            if ((X & 7) === 0) await Checkpoint();
        }
    }
    const SelectionKey = JSON.stringify([Plan.Working?.LayerIndex, Plan.Working?.RegionIndex]);
    const Cached = ContextLattices.get(Field), ContextReused = Cached?.SelectionKey === SelectionKey;
    let Context, ContextExpansion = ContextReused ? Cached.Expansion : 1;
    if (ContextReused)
    {
        Plan = PlanWorkingSurface(Field, Request, 64, ContextExpansion);
        Plan.Regions = Cached.Regions;
        if (Plan.Working) Plan.Working.Region = Plan.Regions.find(Region => Region.Entries.some(Entry =>
            Entry.LayerIndex === Plan.Working.LayerIndex && Entry.Layer.Regions.indexOf(Entry.Region) === Plan.Working.RegionIndex));
        Context = Cached.Surface;
        Completed = Plan.Samples;
    }
    else
    {
        for (; ContextExpansion <= 128; ContextExpansion *= 2)
        {
            Plan = PlanWorkingSurface(Field, Request, 64, ContextExpansion);
            // 📝 Reserve all three possible fine-brick attempts before sampling another context approximation.
            if (Completed + Plan.Samples + 67 ** 3 + 51 ** 3 + 35 ** 3 > MaximumSurfaceSamples) continue;
            const Accumulator = SurfaceBufferAccumulator([0, 0, 0], Math.floor(MaximumSurfaceTriangles * .75));
            try
            {
                for (const Region of Plan.Regions)
                {
                    await Sample(Region);
                    await Contour(Region, Accumulator);
                }
                Context = Accumulator.Finalize();
                break;
            }
            catch (Failure)
            {
                if (!(Failure instanceof SurfaceCapacityExceeded)) throw Failure;
            }
        }
        if (!Context) throw new SurfaceCapacityExceeded();
        if (!Valid()) throw new DOMException('Working detail superseded.', 'AbortError');
        ContextLattices.set(Field, { SelectionKey, Expansion: ContextExpansion, Regions: Plan.Regions, Surface: Context });
    }
    let Detail = null, Working = Plan.Working, Limited = false;
    if (Working)
    {
        for (const Cells of [64, 48, 32])
        {
            const Candidate = Cells === 64 ? Plan : PlanWorkingSurface(Field, Request, Cells, ContextExpansion);
            const Window = Candidate.Working;
            const Fine = { Minimum: Window.Minimum.map((Coordinate, Axis) => Coordinate - Window.Pitch[Axis]),
                Pitch: Window.Pitch, Samples: Window.Cells.map(Count => Count + 3), Entries: Window.Region.Entries };
            try
            {
                await Sample(Fine, Working.Region, Window);
                const Output = SurfaceBufferAccumulator([0, 0, 0], MaximumSurfaceTriangles - Context.Triangles);
                await Contour(Fine, Output, true);
                Detail = Output.Finalize();
                Working = Window;
                break;
            }
            catch (Failure)
            {
                if (!(Failure instanceof SurfaceCapacityExceeded)) throw Failure;
                Limited = true;
            }
        }
    }
    if (!Valid()) throw new DOMException('Working detail superseded.', 'AbortError');
    const Parts = Detail ? [Context, Detail] : [Context];
    const Specification = Working && {
        LayerIndex: Working.LayerIndex, RegionIndex: Working.RegionIndex, Dimensions: Working.Dimensions,
        TargetPitch: Working.TargetPitch, Pitch: Working.Pitch, Factors: Working.Factors,
        Minimum: Working.Minimum, Maximum: Working.Maximum, Point: Working.Point, Cells: Working.Cells,
        DenseCells: Working.DenseCells, Resident: Boolean(Detail), Limited: Limited || !Detail
    };
    return {
        Chunks: Parts.flatMap(Part => Part.Chunks), Origins: Parts.flatMap(Part => Part.Origins),
        Sections: Parts.flatMap((Part, Section) => Part.Chunks.map(() => Section)),
        Triangles: Parts.reduce((Sum, Part) => Sum + Part.Triangles, 0), Bytes: Parts.reduce((Sum, Part) => Sum + Part.Bytes, 0),
        Origin: [0, 0, 0], Quality: Working ? Math.round(Working.Dimensions[0] / Working.Factors[0]) : 32,
        Pitch: Working ? Math.max(...Working.Pitch) : 1, Samples: Completed, Evaluations, Regions: Plan.Regions.length,
        Working: Specification, DetailMinimum: Detail ? Working.Minimum : null, DetailMaximum: Detail ? Working.Maximum : null,
        DetailKey: RequestedKey, PreparationMilliseconds: performance.now() - Start, MaximumSliceMilliseconds,
        ContextTriangles: Context.Triangles, DetailTriangles: Detail?.Triangles || 0, ContextReused, ContextExpansion
    };
}
