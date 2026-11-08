/*==========================================================================================================================================
                                                             VOLUMEREFINEMENT.JS
==========================================================================================================================================*/
// 🧩 Always-high, budgeted XYZ surface projection and conforming shared-edge refinement; never a heightfield or pixel renderer.

import {
    ConstructVolumeSurface, SurfaceBufferAccumulator, MaximumSurfaceSamples, MaximumSurfaceTriangles, SurfacePreparationSliceMilliseconds
} from './VolumeSurface.js';
import { ProjectVolumeCamera } from './VolumeRayField.js';

const Normalize = Direction => { const Length = Math.hypot(...Direction); return Length > 1e-12 ? Direction.map(Component => Component / Length) : [0, 1, 0]; };
const YieldExecution = () => new Promise(Resolve => setTimeout(Resolve, 0));

export async function ConstructHighSurface(Field, Options = {})
{
    const Start = performance.now();
    const InitialSurface = await ConstructVolumeSurface(Field, 48, Options);
    const MaximumSamples = Math.min(Options.MaximumSamples || MaximumSurfaceSamples, MaximumSurfaceSamples);
    const TriangleLimit = Math.min(Options.MaximumTriangles || MaximumSurfaceTriangles, MaximumSurfaceTriangles);
    const Vertices = [], Faces = [], VertexIndices = new Map(), Edges = new Map();
    const Quantization = Math.max(1e-7, InitialSurface.Pitch * 1e-5), Origin = InitialSurface.Origin;
    let Slice = performance.now(), MaximumSlice = InitialSurface.MaximumSliceMilliseconds, Samples = InitialSurface.Samples, Projected = 0;
    async function Checkpoint(Force = false)
    {
        if (Options.Current && !Options.Current()) throw new DOMException('High surface preparation superseded.', 'AbortError');
        const Elapsed = performance.now() - Slice;
        if (!Force && Elapsed < SurfacePreparationSliceMilliseconds) return;
        MaximumSlice = Math.max(MaximumSlice, Elapsed);
        Options.Progress?.({ Completed: Samples, Samples: MaximumSamples, Quality: 96, Phase: 'high-geometry' });
        await (Options.YieldExecution || YieldExecution)();
        if (Options.Current && !Options.Current()) throw new DOMException('High surface preparation superseded.', 'AbortError');
        Slice = performance.now();
    }
    for (let Part = 0; Part < InitialSurface.Chunks.length; Part++)
    {
        const Chunk = InitialSurface.Chunks[Part], Local = InitialSurface.Origins?.[Part] || Origin;
        let Face = [];
        for (let Offset = 0; Offset < Chunk.length; Offset += 9)
        {
            const Position = [0, 1, 2].map(Axis => Chunk[Offset + Axis] + Local[Axis]);
            const Key = Position.map((Component, Axis) => Math.round((Component - Origin[Axis]) / Quantization)).join(',');
            let Index = VertexIndices.get(Key);
            if (Index === undefined)
            {
                Index = Vertices.length;
                VertexIndices.set(Key, Index);
                Vertices.push({ Position, Normal: Array.from(Chunk.subarray(Offset + 3, Offset + 6)), Color: Array.from(Chunk.subarray(Offset + 6, Offset + 9)) });
            }
            Face.push(Index);
            if (Face.length === 3) { Faces.push(Face); Face = []; }
            if ((Offset % (9 * 256)) === 0) await Checkpoint();
        }
    }
    const Projection = Options.Camera ? ProjectVolumeCamera(Options.Camera) : null;
    function Edge(First, Second)
    {
        const Key = First < Second ? `${First}:${Second}` : `${Second}:${First}`;
        let Found = Edges.get(Key);
        if (!Found)
        {
            const A = Vertices[First], B = Vertices[Second];
            const Middle = A.Position.map((Component, Axis) => (Component + B.Position[Axis]) * .5);
            const Length = Math.hypot(...A.Position.map((Component, Axis) => Component - B.Position[Axis]));
            A.MaximumShift = Math.min(A.MaximumShift ?? Infinity, Length * .2);
            B.MaximumShift = Math.min(B.MaximumShift ?? Infinity, Length * .2);
            const Normal = Normalize(A.Normal.map((Component, Axis) => Component + B.Normal[Axis]));
            let Weight = 1;
            if (Projection)
            {
                const TowardEye = Projection.Eye.map((Component, Axis) => Component - Middle[Axis]);
                const Distance = Math.hypot(...TowardEye) || 1;
                const Facing = Normal.reduce((Sum, Component, Axis) => Sum + Component * TowardEye[Axis] / Distance, 0);
                Weight = Math.max(.025, Facing) / Math.max(InitialSurface.Pitch, Distance);
            }
            Found = { First, Second, Count: 0, Middle: -1, Score: Length * Length * Weight };
            Edges.set(Key, Found);
        }
        return Found;
    }
    for (const Face of Faces)
    {
        Edge(Face[0], Face[1]).Count++; Edge(Face[1], Face[2]).Count++; Edge(Face[2], Face[0]).Count++;
        if ((Edges.size & 127) === 0) await Checkpoint();
    }
    const Candidates = [...Edges.values()].sort((A, B) => B.Score - A.Score);
    let PredictedTriangles = Faces.length, SplitEdges = 0;
    for (const Candidate of Candidates)
    {
        if (PredictedTriangles + Candidate.Count > TriangleLimit) continue;
        if ((Vertices.length + 1) * 7 + InitialSurface.Samples > MaximumSamples) break;
        const A = Vertices[Candidate.First], B = Vertices[Candidate.Second];
        Candidate.Middle = Vertices.length;
        Vertices.push({ Position: A.Position.map((Component, Axis) => (Component + B.Position[Axis]) * .5),
            Normal: Normalize(A.Normal.map((Component, Axis) => Component + B.Normal[Axis])), Color: A.Color,
            MaximumShift: Math.hypot(...A.Position.map((Component, Axis) => Component - B.Position[Axis])) * .2 });
        PredictedTriangles += Candidate.Count;
        SplitEdges++;
        if ((SplitEdges & 127) === 0) await Checkpoint();
    }
    const Epsilon = Math.max(.001, InitialSurface.Pitch * .005);
    function Sample(Position)
    {
        Samples++;
        return Field.Sample(...Position).Distance;
    }
    // 📝 New vertices are projected onto the actual XYZ zero set; splitting a polygon without field resampling is not detail.
    for (let Index = 0; Index < Vertices.length; Index++)
    {
        const Vertex = Vertices[Index];
        let RemainingShift = Math.min(InitialSurface.Pitch * .4, Vertex.MaximumShift ?? InitialSurface.Pitch * .4);
        for (let Iteration = 0; Iteration < 2 && Samples + 7 <= MaximumSamples; Iteration++)
        {
            const Distance = Sample(Vertex.Position);
            const Gradient = [0, 1, 2].map(Axis =>
            {
                const Lower = [...Vertex.Position], Upper = [...Vertex.Position];
                Lower[Axis] -= Epsilon; Upper[Axis] += Epsilon;
                return (Sample(Upper) - Sample(Lower)) / (2 * Epsilon);
            });
            const Length = Math.hypot(...Gradient);
            if (!Number.isFinite(Distance) || !Number.isFinite(Length) || Length < 1e-9) break;
            Vertex.Normal = Gradient.map(Component => Component / Length);
            const Shift = Math.max(-RemainingShift, Math.min(RemainingShift, Distance / Length));
            RemainingShift -= Math.abs(Shift);
            Vertex.Position = Vertex.Position.map((Component, Axis) => Component - Vertex.Normal[Axis] * Shift);
            if (Iteration === 0) Projected++;
            if (Math.abs(Shift) < Epsilon * .1) break;
        }
        if ((Index & 31) === 0) await Checkpoint();
    }
    const Accumulator = SurfaceBufferAccumulator(Origin, TriangleLimit);
    const Triangle = (A, B, C) => Accumulator.AccumulateTriangle(Vertices[A], Vertices[B], Vertices[C]);
    let FaceIndex = 0;
    for (const [A, B, C] of Faces)
    {
        const AB = Edge(A, B).Middle, BC = Edge(B, C).Middle, CA = Edge(C, A).Middle;
        const Count = Number(AB >= 0) + Number(BC >= 0) + Number(CA >= 0);
        if (!Count) Triangle(A, B, C);
        else if (Count === 3)
        {
            Triangle(A, AB, CA); Triangle(AB, B, BC); Triangle(CA, BC, C); Triangle(AB, BC, CA);
        }
        else if (Count === 1)
        {
            if (AB >= 0) { Triangle(A, AB, C); Triangle(AB, B, C); }
            else if (BC >= 0) { Triangle(B, BC, A); Triangle(BC, C, A); }
            else { Triangle(C, CA, B); Triangle(CA, A, B); }
        }
        else
        {
            const [First, Middle, Last, FM, ML] = CA < 0 ? [A, B, C, AB, BC] : AB < 0 ? [B, C, A, BC, CA] : [C, A, B, CA, AB];
            Triangle(Middle, ML, FM); Triangle(First, FM, Last); Triangle(FM, ML, Last);
        }
        if ((++FaceIndex & 127) === 0) await Checkpoint();
    }
    await Checkpoint(true);
    return { ...InitialSurface, ...Accumulator.Finalize(), Quality: 96, Samples, Evaluations: Samples,
        PreparationMilliseconds: performance.now() - Start, MaximumSliceMilliseconds: MaximumSlice,
        Refinement: { Method: 'projected-adaptive-xyz', InitialPitch: InitialSurface.Pitch, InitialTriangles: InitialSurface.Triangles,
            SplitEdges, ProjectedVertices: Projected, Vertices: Vertices.length, Samples, TriangleLimit,
            Limited: PredictedTriangles >= TriangleLimit - 2 || Samples + 7 > MaximumSamples } };
}
