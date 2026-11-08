/*==========================================================================================================================================
                                                          VOLUMEREFINEMENT.TEST.JS
==========================================================================================================================================*/
// 🧩 Genuine field-projected refinement, conforming shared edges, strict budgets and cooperative cancellation.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { ConstructHighSurface } from './VolumeRefinement.js';
import { ConstructVolumeSurface } from './VolumeSurface.js';

function Sphere()
{
    const Centre = [.07, -.033, .021], Radius = 2.073;
    return { Layers: [{ Color: '#aaaaaa' }], Entries: [{ Lower: [-6, -6, -6], Upper: [6, 6, 6] }],
        Minimum: [-6, -6, -6], Maximum: [6, 6, 6], Planar: () => -100,
        Sample: (X, Y, Z) => ({ Distance: Math.hypot(X - Centre[0], Y - Centre[1], Z - Centre[2]) - Radius, LayerIndex: 0 }) };
}

function InspectSurface(Surface, Field)
{
    const Edges = new Map();
    let Error = 0, Count = 0, Maximum = 0;
    for (let Part = 0; Part < Surface.Chunks.length; Part++)
    {
        const Chunk = Surface.Chunks[Part], Origin = Surface.Origins[Part];
        for (let Offset = 0; Offset < Chunk.length; Offset += 27)
        {
            const Positions = [0, 9, 18].map(Start => [0, 1, 2].map(Axis => Chunk[Offset + Start + Axis] + Origin[Axis]));
            const Keys = Positions.map(Point => Point.map(Component => Math.round(Component / 1e-5)).join(','));
            for (let Index = 0; Index < 3; Index++)
            {
                const Key = [Keys[Index], Keys[(Index + 1) % 3]].sort().join(':');
                Edges.set(Key, (Edges.get(Key) || 0) + 1);
            }
            for (const Position of Positions)
            {
                const Residual = Math.abs(Field.Sample(...Position).Distance);
                Error += Residual; Count++; Maximum = Math.max(Maximum, Residual);
            }
        }
    }
    return { Mean: Error / Count, Maximum, NonPairedEdges: [...Edges.values()].filter(Count => Count !== 2).length };
}

test('high detail adds genuine projected XYZ samples, not screen pixels or coplanar triangle splits', async () =>
{
    const Field = Sphere(), InitialSurface = await ConstructVolumeSurface(Field, 48);
    const High = await ConstructHighSurface(Field, { MaximumTriangles: 18000, MaximumSamples: 400000 });
    Assert.ok(High.Triangles > InitialSurface.Triangles && High.Triangles <= 18000);
    Assert.ok(High.Samples > InitialSurface.Samples && High.Samples <= 400000);
    Assert.equal(High.Quality, 96);
    Assert.equal(High.Refinement.Method, 'projected-adaptive-xyz');
    Assert.ok(High.Refinement.SplitEdges > 1000 && High.Refinement.ProjectedVertices > 1000);
    const Before = InspectSurface(InitialSurface, Field), After = InspectSurface(High, Field);
    Assert.ok(After.Mean < Before.Mean * .01, JSON.stringify({ Before, After }));
    Assert.ok(After.Maximum < 1e-5, JSON.stringify(After));
    Assert.equal(After.NonPairedEdges, 0);
});

test('high-detail preparation yields and cancels without returning a partial surface', async () =>
{
    let Current = true, Yields = 0;
    await Assert.rejects(ConstructHighSurface(Sphere(), { Current: () => Current,
        YieldExecution: async () => { if (++Yields >= 2) Current = false; } }), Error => Error.name === 'AbortError');
    Assert.equal(Yields, 2);
});
