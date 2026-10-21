//============================================================================================================================================
//                                                          GEOMETRYHIERARCHYCORPUS.MJS
//============================================================================================================================================
// 🧩 Deterministic hierarchy test geometry: a closed smooth solid, a heightmap cliff mesh, and a two-class solid with a glass band.

function Normalize(P) { const L = Math.hypot(P[0], P[1], P[2]) || 1; return [P[0] / L, P[1] / L, P[2] / L]; }

// 📝 Subdivided icosahedron projected to the unit sphere, emitted as facet soup. An optional latitude band
//    takes the glass family so opaque/transmissive class separation is exercised on one connected solid.
export function ConstructSphereFacets(Subdivisions = 4, GlassFamily = -1)
{
    const T = (1 + Math.sqrt(5)) / 2;
    let Vertices = [[-1, T, 0], [1, T, 0], [-1, -T, 0], [1, -T, 0], [0, -1, T], [0, 1, T], [0, -1, -T], [0, 1, -T],
        [T, 0, -1], [T, 0, 1], [-T, 0, -1], [-T, 0, 1]].map(Normalize);
    let Faces = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
        [10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11],
        [6, 2, 10], [8, 6, 7], [9, 8, 1]];
    for (let Pass = 0; Pass < Subdivisions; ++Pass)
    {
        const Midpoints = new Map();
        const Midpoint = (A, B) =>
        {
            const Key = A < B ? A + ":" + B : B + ":" + A;
            if (!Midpoints.has(Key))
            {
                Midpoints.set(Key, Vertices.length);
                Vertices.push(Normalize([0, 1, 2].map((Axis) => (Vertices[A][Axis] + Vertices[B][Axis]) / 2)));
            }
            return Midpoints.get(Key);
        };
        const Next = [];
        for (const [A, B, C] of Faces)
        {
            const AB = Midpoint(A, B), BC = Midpoint(B, C), CA = Midpoint(C, A);
            Next.push([A, AB, CA], [B, BC, AB], [C, CA, BC], [AB, BC, CA]);
        }
        Faces = Next;
    }
    return {
        FirstVertex: 0,
        Facets: Faces.map(([A, B, C]) =>
        {
            const Corners = [Vertices[A], Vertices[B], Vertices[C]];
            const CentroidY = (Corners[0][1] + Corners[1][1] + Corners[2][1]) / 3;
            const Family = GlassFamily >= 0 && Math.abs(CentroidY) < 0.25 ? GlassFamily : 0;
            return { Corners: Corners.map((P) => [...P]), Family };
        })
    };
}

// 📝 Heightmap terrain as a triangulated grid: rolling deterministic relief with one hard cliff step, the same
//    shape a heightmap or an SDF-converted cliff mesh presents after meshing (open border, sharp crease).
export function ConstructHeightfieldFacets(Resolution = 96, Extent = 40)
{
    const Height = (X, Y) =>
    {
        const Rolling = Math.sin(X * 0.31) * Math.cos(Y * 0.27) * 1.4 + Math.sin(X * 0.09 + Y * 0.13) * 2.2;
        const Cliff = X > Math.sin(Y * 0.21) * 4 ? 7.0 : 0.0;
        return Rolling + Cliff;
    };
    const Point = (I, J) =>
    {
        const X = (I / Resolution - 0.5) * Extent, Y = (J / Resolution - 0.5) * Extent;
        return [X, Y, Height(X, Y)];
    };
    const Facets = [];
    for (let J = 0; J < Resolution; ++J)
        for (let I = 0; I < Resolution; ++I)
        {
            const A = Point(I, J), B = Point(I + 1, J), C = Point(I + 1, J + 1), D = Point(I, J + 1);
            Facets.push({ Corners: [A, B, C], Family: 1 }, { Corners: [A, C, D], Family: 1 });
        }
    return { FirstVertex: 0, Facets };
}

export function ConstructTetrahedronFacets()
{
    const V = [[1, 1, 1], [-1, -1, 1], [-1, 1, -1], [1, -1, -1]];
    const Faces = [[0, 1, 2], [0, 3, 1], [0, 2, 3], [1, 3, 2]];
    return { FirstVertex: 0, Facets: Faces.map(([A, B, C]) => ({ Corners: [[...V[A]], [...V[B]], [...V[C]]], Family: 0 })) };
}
