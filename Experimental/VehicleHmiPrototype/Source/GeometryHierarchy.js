//============================================================================================================================================
//                                                          GEOMETRYHIERARCHY.JS
//============================================================================================================================================
// 🧩 Virtualized geometry build: weld, adjacency meshlets, boundary-locked QEM simplification into an error-monotonic cluster DAG.

// 📝 Phase 1 scope: the build side only (runtime cut selection is Phase 2). Anything that arrives as a triangle
//    soup is accepted — authored meshes, heightmap-generated meshes and voxelized-SDF-converted meshes alike —
//    because displacement is already real geometry by the time it reaches this builder. Families carry the
//    material identity; a family is opaque or transmissive (glass), and the two classes never share a cluster,
//    so later phases can route transmission without re-cutting the hierarchy. Deterministic: every tie breaks
//    on ascending index, so two builds of the same input are byte-identical.

const DefaultTriangleLimit = 128;   // [-] triangles per meshlet
const DefaultVertexLimit   = 256;   // [-] unique vertices per meshlet
const DefaultGroupLimit    = 8;     // [-] child clusters per simplification group
const DefaultTargetRatio   = 0.5;   // [-] triangles kept per simplification pass
const DefaultMaxLevels     = 32;    // [-]

//------------------------------------------------------------------------------------------------------------------------
//                                                     SMALL VECTOR SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

function Subtract(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
function Cross(A, B) { return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]]; }
function Dot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
function Length(A) { return Math.hypot(A[0], A[1], A[2]); }
function Normalize(A) { const L = Length(A); return L > 0 ? [A[0] / L, A[1] / L, A[2] / L] : [0, 0, 1]; }
function ReadPosition(Positions, Vertex) { return [Positions[Vertex * 3], Positions[Vertex * 3 + 1], Positions[Vertex * 3 + 2]]; }

function TriangleNormal(Positions, Triangles, Triangle)
{
    const A = ReadPosition(Positions, Triangles[Triangle * 3]);
    const B = ReadPosition(Positions, Triangles[Triangle * 3 + 1]);
    const C = ReadPosition(Positions, Triangles[Triangle * 3 + 2]);
    return Cross(Subtract(B, A), Subtract(C, A));   // 📝 unnormalized: length is twice the area
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WELD AND ADJACENCY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Soup corners weld on exact position bits. Corner normals are not part of the key: the hierarchy shades from
//    face normals, and welding smooth-shaded duplicates apart would manufacture cracks the source does not have.
export function WeldFacetSoup(Source)
{
    const Positions = [], Triangles = [], Families = [], Welded = new Map();
    const Weld = (P) =>
    {
        const Key = (P[0] + 0) + "," + (P[1] + 0) + "," + (P[2] + 0);
        let Vertex = Welded.get(Key);
        if (Vertex === undefined)
        {
            Vertex = Positions.length / 3;
            Welded.set(Key, Vertex);
            Positions.push(P[0], P[1], P[2]);
        }
        return Vertex;
    };
    for (const Facet of Source.Facets)
    {
        for (let Corner = 0; Corner < 3; ++Corner) Triangles.push(Weld(Facet.Corners[Corner]));
        Families.push(Facet.Family ?? 0);
    }
    return { Positions: new Float64Array(Positions), Triangles: new Uint32Array(Triangles), Families: new Uint32Array(Families) };
}

// 📝 Edge incidence over an indexed geometry: every undirected edge lists its triangles once, so adjacency,
//    border detection and family-border detection all read the same table.
function BuildEdgeIncidence(Triangles, VertexCount)
{
    const Edges = new Map();
    const Key = (A, B) => (A < B ? A * VertexCount + B : B * VertexCount + A);
    for (let Triangle = 0; Triangle < Triangles.length / 3; ++Triangle)
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const A = Triangles[Triangle * 3 + Corner], B = Triangles[Triangle * 3 + (Corner + 1) % 3];
            const Identity = Key(A, B);
            let Entry = Edges.get(Identity);
            if (!Entry) { Entry = { A: Math.min(A, B), B: Math.max(A, B), Triangles: [] }; Edges.set(Identity, Entry); }
            Entry.Triangles.push(Triangle);
        }
    const Adjacent = Array.from({ length: Triangles.length / 3 }, () => []);
    for (const Entry of Edges.values())
        for (const Triangle of Entry.Triangles)
            for (const Other of Entry.Triangles)
                if (Other !== Triangle) Adjacent[Triangle].push(Other);
    for (const List of Adjacent) { List.sort((A, B) => A - B); }
    return { Edges, Adjacent };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MESHLET PARTITION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Greedy adjacency expansion (not spatial median splits): a seed grows across shared edges, scoring
//    candidates by normal coherence with the running cone and distance to the seed centroid. Partition keys
//    (material class, and the simplification group at upper levels) are hard walls the growth never crosses.
function BuildMeshlets(Geometry, PartitionKeys, TriangleLimit, VertexLimit)
{
    const { Positions, Triangles } = Geometry;
    const TriangleCount = Triangles.length / 3;
    const { Adjacent } = BuildEdgeIncidence(Triangles, Positions.length / 3);
    const Normals = [], Centroids = [];
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        Normals.push(Normalize(TriangleNormal(Positions, Triangles, Triangle)));
        const A = ReadPosition(Positions, Triangles[Triangle * 3]);
        const B = ReadPosition(Positions, Triangles[Triangle * 3 + 1]);
        const C = ReadPosition(Positions, Triangles[Triangle * 3 + 2]);
        Centroids.push([(A[0] + B[0] + C[0]) / 3, (A[1] + B[1] + C[1]) / 3, (A[2] + B[2] + C[2]) / 3]);
    }
    const Assigned = new Int32Array(TriangleCount).fill(-1);
    const Meshlets = [];
    // 📝 The distance term is normalised by the radius a full meshlet would have at this tessellation scale.
    //    A fixed absolute weight once let normal alignment dominate completely on rolling terrain: growth ran
    //    in long snakes along constant-slope bands and stranded 1-4 triangle slivers between them, and slivers
    //    have no interior, so whole DAG levels later found nothing they were allowed to simplify.
    let SpacingTotal = 0, SpacingPairs = 0;
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (const Other of Adjacent[Triangle])
            if (Other > Triangle) { SpacingTotal += Length(Subtract(Centroids[Triangle], Centroids[Other])); ++SpacingPairs; }
    const MeanSpacing = SpacingPairs ? SpacingTotal / SpacingPairs : 1;
    const ExpectedRadius = Math.max(MeanSpacing * Math.sqrt(TriangleLimit / Math.PI), 1e-12);
    for (let Seed = 0; Seed < TriangleCount; ++Seed)
    {
        if (Assigned[Seed] >= 0) continue;
        const Identity = Meshlets.length, Key = PartitionKeys[Seed];
        const Members = [], Vertices = new Set();
        const AddVertex = (Triangle) =>
        {
            let Added = 0;
            for (let Corner = 0; Corner < 3; ++Corner) if (!Vertices.has(Triangles[Triangle * 3 + Corner])) ++Added;
            return Added;
        };
        const Admit = (Triangle) =>
        {
            Assigned[Triangle] = Identity;
            Members.push(Triangle);
            for (let Corner = 0; Corner < 3; ++Corner) Vertices.add(Triangles[Triangle * 3 + Corner]);
        };
        Admit(Seed);
        let Cone = [...Normals[Seed]];
        const Frontier = new Set(Adjacent[Seed].filter((Other) => Assigned[Other] < 0 && PartitionKeys[Other] === Key));
        while (Members.length < TriangleLimit && Frontier.size)
        {
            let Best = -1, BestScore = -Infinity;
            for (const Candidate of Frontier)
            {
                if (Assigned[Candidate] >= 0) continue;
                if (Vertices.size + AddVertex(Candidate) > VertexLimit) continue;
                const Alignment = Dot(Normals[Candidate], Cone);
                const Distance = Length(Subtract(Centroids[Candidate], Centroids[Seed]));
                const Score = Alignment - 0.3 * (Distance / ExpectedRadius);
                if (Score > BestScore || (Score === BestScore && Candidate < Best)) { BestScore = Score; Best = Candidate; }
            }
            if (Best < 0) break;
            Frontier.delete(Best);
            Admit(Best);
            Cone = Normalize([Cone[0] + Normals[Best][0], Cone[1] + Normals[Best][1], Cone[2] + Normals[Best][2]]);
            for (const Other of Adjacent[Best])
                if (Assigned[Other] < 0 && PartitionKeys[Other] === Key) Frontier.add(Other);
        }
        Members.sort((A, B) => A - B);
        Meshlets.push(Members);
    }
    return Meshlets;
}

function MeasureCluster(Geometry, Members, Error)
{
    const { Positions, Triangles } = Geometry;
    const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
    const Center = [0, 0, 0];
    let Samples = 0;
    let Axis = [0, 0, 0];
    const Normals = [];
    for (const Triangle of Members)
    {
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const P = ReadPosition(Positions, Triangles[Triangle * 3 + Corner]);
            for (let A = 0; A < 3; ++A) { Minimum[A] = Math.min(Minimum[A], P[A]); Maximum[A] = Math.max(Maximum[A], P[A]); Center[A] += P[A]; }
            ++Samples;
        }
        const Normal = TriangleNormal(Positions, Triangles, Triangle);
        Normals.push(Normal);
        Axis = [Axis[0] + Normal[0], Axis[1] + Normal[1], Axis[2] + Normal[2]];
    }
    for (let A = 0; A < 3; ++A) Center[A] /= Math.max(1, Samples);
    let Radius = 0;
    for (const Triangle of Members)
        for (let Corner = 0; Corner < 3; ++Corner)
            Radius = Math.max(Radius, Length(Subtract(ReadPosition(Positions, Triangles[Triangle * 3 + Corner]), Center)));
    Axis = Normalize(Axis);
    let Cutoff = 1;
    for (const Normal of Normals) Cutoff = Math.min(Cutoff, Dot(Axis, Normalize(Normal)));
    // 📝 Bounds expand by the node's own error, so a bounds test against this node conservatively covers the
    //    original surface it stands for. This is what lets a parent stand in for its children without cracks
    //    in visibility (geometric cracks are prevented separately by the border lock during simplification).
    return {
        Minimum: Minimum.map((Value) => Value - Error),
        Maximum: Maximum.map((Value) => Value + Error),
        Sphere: { Center, Radius: Radius + Error },
        Cone: { Axis, Cutoff }
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     QEM SIMPLIFICATION
//------------------------------------------------------------------------------------------------------------------------

function QuadricFromPlane(Normal, D, Weight)
{
    const [A, B, C] = Normal;
    return new Float64Array([
        Weight * A * A, Weight * A * B, Weight * A * C, Weight * A * D,
        Weight * B * B, Weight * B * C, Weight * B * D,
        Weight * C * C, Weight * C * D, Weight * D * D]);
}

function QuadricError(Quadric, P)
{
    const [X, Y, Z] = P, Q = Quadric;
    return Q[0] * X * X + 2 * Q[1] * X * Y + 2 * Q[2] * X * Z + 2 * Q[3] * X +
           Q[4] * Y * Y + 2 * Q[5] * Y * Z + 2 * Q[6] * Y + Q[7] * Z * Z + 2 * Q[8] * Z + Q[9];
}

function QuadricOptimum(Quadric, Fallbacks)
{
    const Q = Quadric;
    const M = [[Q[0], Q[1], Q[2]], [Q[1], Q[4], Q[5]], [Q[2], Q[5], Q[7]]];
    const V = [-Q[3], -Q[6], -Q[8]];
    // Solve M x = V by Cramer; near-singular quadrics fall back to the cheapest endpoint/midpoint.
    const Det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
                M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    if (Math.abs(Det) > 1e-12)
    {
        const Replace = (Column) =>
        {
            const R = M.map((Row, Index) => Row.map((Value, Axis) => (Axis === Column ? V[Index] : Value)));
            return R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) -
                   R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
                   R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
        };
        const Optimum = [Replace(0) / Det, Replace(1) / Det, Replace(2) / Det];
        // 📝 Near-parallel plane sets put the algebraic optimum arbitrarily far from the edge being collapsed
        //    at deceptively low quadric cost. The optimum may only compete with the endpoint/midpoint fallbacks
        //    if it stays within half an edge length of the midpoint — then no endpoint ever moves farther than
        //    one edge length, which is what keeps the accumulated error bound (and with it every parent bound)
        //    proportional to the local tessellation instead of the whole asset.
        if (Optimum.every(Number.isFinite) && Fallbacks.length >= 2)
        {
            const EdgeLength = Length(Subtract(Fallbacks[0], Fallbacks[1]));
            const Midpoint = [0, 1, 2].map((Axis) => (Fallbacks[0][Axis] + Fallbacks[1][Axis]) / 2);
            if (Length(Subtract(Optimum, Midpoint)) <= EdgeLength * 0.5) Fallbacks.push(Optimum);
        }
    }
    let Best = Fallbacks[0], BestError = Infinity;
    for (const Candidate of Fallbacks)
    {
        const Error = QuadricError(Quadric, Candidate);
        if (Error < BestError) { BestError = Error; Best = Candidate; }
    }
    return { Position: Best, Error: Math.max(0, BestError) };
}

// 📝 Simplifies one group's geometry in place-local terms. Border vertices (group boundary, open mesh border,
//    family border) never move: a collapse may land ON a locked vertex but never drag it, which is exactly the
//    condition that keeps neighbouring groups crack-free after independent simplification.
function SimplifyGroup(Positions, Triangles, Families, TargetTriangles)
{
    const VertexCount = Positions.length / 3, TriangleCount = Triangles.length / 3;
    const { Edges } = BuildEdgeIncidence(Triangles, VertexCount);
    const Locked = new Uint8Array(VertexCount);
    for (const Entry of Edges.values())
    {
        const BorderEdge = Entry.Triangles.length !== 2;
        const FamilyBorder = Entry.Triangles.length >= 2 &&
            Entry.Triangles.some((Triangle) => Families[Triangle] !== Families[Entry.Triangles[0]]);
        // 📝 Non-manifold edges (3+ triangles) count as borders too: locking them is conservative and safe.
        if (BorderEdge || FamilyBorder) { Locked[Entry.A] = 1; Locked[Entry.B] = 1; }
    }
    const LivePositions = new Float64Array(Positions);
    const LiveTriangles = new Int32Array(Triangles);
    const Quadrics = Array.from({ length: VertexCount }, () => new Float64Array(10));
    const Accumulate = (Triangle) =>
    {
        const A = ReadPosition(LivePositions, LiveTriangles[Triangle * 3]);
        const Raw = TriangleNormal(LivePositions, LiveTriangles, Triangle);
        const Area = Length(Raw) / 2;
        if (!(Area > 0)) return;
        const Normal = Normalize(Raw);
        const Plane = QuadricFromPlane(Normal, -Dot(Normal, A), Area);
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const Target = Quadrics[LiveTriangles[Triangle * 3 + Corner]];
            for (let C = 0; C < 10; ++C) Target[C] += Plane[C];
        }
    };
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle) Accumulate(Triangle);
    const Alive = new Uint8Array(TriangleCount).fill(1);
    const VertexTriangles = Array.from({ length: VertexCount }, () => new Set());
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (let Corner = 0; Corner < 3; ++Corner) VertexTriangles[LiveTriangles[Triangle * 3 + Corner]].add(Triangle);
    const Remap = new Int32Array(VertexCount);
    for (let Vertex = 0; Vertex < VertexCount; ++Vertex) Remap[Vertex] = Vertex;
    const Resolve = (Vertex) => { while (Remap[Vertex] !== Vertex) Vertex = Remap[Vertex]; return Vertex; };

    const Candidates = [];
    for (const Entry of Edges.values()) Candidates.push([Entry.A, Entry.B]);
    Candidates.sort((A, B) => A[0] - B[0] || A[1] - B[1]);
    const Versions = new Int32Array(VertexCount);
    const Heap = [];
    const HeapPush = (Item) =>
    {
        Heap.push(Item);
        let Index = Heap.length - 1;
        while (Index > 0)
        {
            const Parent = (Index - 1) >> 1;
            if (Heap[Parent].Cost <= Heap[Index].Cost) break;
            [Heap[Parent], Heap[Index]] = [Heap[Index], Heap[Parent]];
            Index = Parent;
        }
    };
    const HeapPop = () =>
    {
        const Top = Heap[0], Last = Heap.pop();
        if (Heap.length)
        {
            Heap[0] = Last;
            let Index = 0;
            while (true)
            {
                let Smallest = Index;
                const Left = Index * 2 + 1, Right = Left + 1;
                if (Left < Heap.length && Heap[Left].Cost < Heap[Smallest].Cost) Smallest = Left;
                if (Right < Heap.length && Heap[Right].Cost < Heap[Smallest].Cost) Smallest = Right;
                if (Smallest === Index) break;
                [Heap[Smallest], Heap[Index]] = [Heap[Index], Heap[Smallest]];
                Index = Smallest;
            }
        }
        return Top;
    };
    const Evaluate = (A, B) =>
    {
        A = Resolve(A); B = Resolve(B);
        if (A === B) return null;
        if (Locked[A] && Locked[B]) return null;
        const Sum = new Float64Array(10);
        for (let C = 0; C < 10; ++C) Sum[C] = Quadrics[A][C] + Quadrics[B][C];
        const PA = ReadPosition(LivePositions, A), PB = ReadPosition(LivePositions, B);
        // 📝 A locked endpoint pins the collapse position exactly: the quadric optimum is not even offered,
        //    because any movement of a shared-border vertex is a crack against the neighbouring group.
        const Pinned = Locked[A] ? PA : Locked[B] ? PB : null;
        const { Position, Error } = Pinned
            ? { Position: Pinned, Error: Math.max(0, QuadricError(Sum, Pinned)) }
            : QuadricOptimum(Sum, [PA, PB, [(PA[0] + PB[0]) / 2, (PA[1] + PB[1]) / 2, (PA[2] + PB[2]) / 2]]);
        // 📝 Deviation is a pure geometric distance: every point of the affected triangles moves at most the
        //    farthest endpoint move (convexity), and vanished free apexes are added at collapse time. The
        //    quadric value is area-weighted, so its square root is NOT a distance and must never feed the
        //    error bound — doing so once inflated a unit sphere's root error to nearly its diameter.
        const Deviation = Math.max(Length(Subtract(Position, PA)), Length(Subtract(Position, PB)));
        return { A, B, Position, Cost: Error, Deviation, VersionA: Versions[A], VersionB: Versions[B] };
    };
    for (const [A, B] of Candidates) { const Item = Evaluate(A, B); if (Item) HeapPush(Item); }

    let Remaining = TriangleCount, MaxDeviation = 0;
    const Flips = (A, B, Position) =>
    {
        for (const Triangle of VertexTriangles[A])
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
            if (Corners.includes(B)) continue;   // this triangle dies in the collapse
            const Points = Corners.map((Vertex) => (Vertex === A ? Position : ReadPosition(LivePositions, Vertex)));
            const Current = Corners.map((Vertex) => ReadPosition(LivePositions, Vertex));
            const Before = Cross(Subtract(Current[1], Current[0]), Subtract(Current[2], Current[0]));
            const After = Cross(Subtract(Points[1], Points[0]), Subtract(Points[2], Points[0]));
            if (Dot(Before, After) <= 0) return true;
        }
        return false;
    };
    while (Remaining > TargetTriangles && Heap.length)
    {
        const Item = HeapPop();
        const A = Resolve(Item.A), B = Resolve(Item.B);
        if (A === B) continue;
        // 📝 An item executes only if its endpoints are still the very vertices it was evaluated on, with the
        //    very versions it was stamped with. Anything else — an endpoint that has since collapsed into
        //    another vertex (possibly a locked border vertex), or absorbed one — is re-evaluated from scratch.
        //    Version numbers alone cannot prove freshness across a remap chain (a resolved keeper can carry the
        //    same small integer the removed endpoint once had), so identity is checked too. This is the guard
        //    that makes "locked vertices never move" a proof rather than a hope.
        if (A !== Item.A || B !== Item.B || Versions[A] !== Item.VersionA || Versions[B] !== Item.VersionB)
        {
            const Fresh = Evaluate(A, B);
            if (Fresh) HeapPush(Fresh);
            continue;
        }
        if (Locked[A] && Locked[B]) continue;
        const Keeper = Locked[A] ? A : Locked[B] ? B : A;
        const Removed = Keeper === A ? B : A;
        if (Flips(Removed, Keeper, Item.Position) || Flips(Keeper, Removed, Item.Position)) continue;
        // 📝 A collapse must not orphan a locked vertex (or the keeper): a border vertex whose every triangle
        //    dies would silently erase a point of the shared border polyline — a crack by disappearance, and
        //    one the endpoint-move deviation could not even see. Dying-triangle apex distances also feed the
        //    deviation, so vanished surface is accounted in the error bound.
        const DyingVertices = new Set([Keeper, Removed]);
        let Orphans = false;
        for (const Triangle of VertexTriangles[Removed])
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
            if (!Corners.includes(Keeper)) continue;
            for (const Vertex of Corners) DyingVertices.add(Vertex);
        }
        let DyingDeviation = 0;
        for (const Vertex of DyingVertices)
        {
            if (Vertex === Removed) continue;
            let Survives = false;
            for (const Triangle of VertexTriangles[Vertex])
            {
                if (!Alive[Triangle]) continue;
                const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
                if (!(Corners.includes(Keeper) && Corners.includes(Removed))) { Survives = true; break; }
            }
            if (Survives) continue;
            if (Vertex === Keeper || Locked[Vertex]) { Orphans = true; break; }
            // A free apex vanishing with its last triangle: its distance to the collapse point is real error.
            DyingDeviation = Math.max(DyingDeviation,
                Length(Subtract(ReadPosition(LivePositions, Vertex), Item.Position)));
        }
        if (Orphans) continue;
        // Collapse Removed into Keeper at the evaluated position.
        for (let Axis = 0; Axis < 3; ++Axis) LivePositions[Keeper * 3 + Axis] = Item.Position[Axis];
        for (let C = 0; C < 10; ++C) Quadrics[Keeper][C] += Quadrics[Removed][C];
        Remap[Removed] = Keeper;
        MaxDeviation = Math.max(MaxDeviation, Item.Deviation, DyingDeviation);
        const Affected = new Set([...VertexTriangles[Removed], ...VertexTriangles[Keeper]]);
        for (const Triangle of Affected)
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
            if (new Set(Corners).size < 3) { Alive[Triangle] = 0; --Remaining; continue; }
            for (let Corner = 0; Corner < 3; ++Corner) LiveTriangles[Triangle * 3 + Corner] = Corners[Corner];
            for (const Vertex of Corners) VertexTriangles[Vertex].add(Triangle);
        }
        VertexTriangles[Removed].clear();
        ++Versions[Keeper];
        const Neighbours = new Set();
        for (const Triangle of VertexTriangles[Keeper])
            for (let Corner = 0; Corner < 3; ++Corner)
            {
                const Vertex = Resolve(LiveTriangles[Triangle * 3 + Corner]);
                if (Vertex !== Keeper) Neighbours.add(Vertex);
            }
        for (const Vertex of Neighbours) { const Fresh = Evaluate(Keeper, Vertex); if (Fresh) HeapPush(Fresh); }
    }
    // Compact surviving geometry.
    const OutPositions = [], OutTriangles = [], OutFamilies = [], Compact = new Map();
    const EmitVertex = (Vertex) =>
    {
        Vertex = Resolve(Vertex);
        let Index = Compact.get(Vertex);
        if (Index === undefined)
        {
            Index = OutPositions.length / 3;
            Compact.set(Vertex, Index);
            OutPositions.push(LivePositions[Vertex * 3], LivePositions[Vertex * 3 + 1], LivePositions[Vertex * 3 + 2]);
        }
        return Index;
    };
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        if (!Alive[Triangle]) continue;
        for (let Corner = 0; Corner < 3; ++Corner) OutTriangles.push(EmitVertex(LiveTriangles[Triangle * 3 + Corner]));
        OutFamilies.push(Families[Triangle]);
    }
    return {
        Positions: new Float64Array(OutPositions),
        Triangles: new Uint32Array(OutTriangles),
        Families: new Uint32Array(OutFamilies),
        Deviation: MaxDeviation,
        LockedCount: Locked.reduce((Sum, Value) => Sum + Value, 0)
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HIERARCHY CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

function GroupClusters(Geometry, Meshlets, GroupLimit)
{
    const { Triangles } = Geometry;
    const ClusterOf = new Int32Array(Triangles.length / 3).fill(-1);
    Meshlets.forEach((Members, Identity) => { for (const Triangle of Members) ClusterOf[Triangle] = Identity; });
    const { Adjacent } = BuildEdgeIncidence(Triangles, Geometry.Positions.length / 3);
    const ClusterAdjacent = Meshlets.map(() => new Set());
    for (let Triangle = 0; Triangle < ClusterOf.length; ++Triangle)
        for (const Other of Adjacent[Triangle])
            if (ClusterOf[Other] !== ClusterOf[Triangle]) ClusterAdjacent[ClusterOf[Triangle]].add(ClusterOf[Other]);
    const Assigned = new Int32Array(Meshlets.length).fill(-1);
    const Groups = [];
    for (let Seed = 0; Seed < Meshlets.length; ++Seed)
    {
        if (Assigned[Seed] >= 0) continue;
        const Identity = Groups.length, Members = [Seed];
        Assigned[Seed] = Identity;
        const Frontier = [...ClusterAdjacent[Seed]].sort((A, B) => A - B);
        while (Members.length < GroupLimit && Frontier.length)
        {
            const Next = Frontier.shift();
            if (Assigned[Next] >= 0) continue;
            Assigned[Next] = Identity;
            Members.push(Next);
            for (const Other of [...ClusterAdjacent[Next]].sort((A, B) => A - B))
                if (Assigned[Other] < 0 && !Frontier.includes(Other)) Frontier.push(Other);
            Frontier.sort((A, B) => A - B);
        }
        Members.sort((A, B) => A - B);
        Groups.push(Members);
    }
    // 📝 Repair pass: greedy growth strands "hole" clusters whose neighbours were all consumed by earlier
    //    groups, leaving singleton groups. A singleton group becomes a tiny isolated partition region at the
    //    next level, meshlets there cannot fill, and the fragmentation compounds until whole levels are
    //    border-locked slivers with no interior left to simplify. Undersized groups therefore merge into
    //    their smallest neighbouring group (most shared borders, then lowest identity, for determinism).
    const MinimumGroup = Math.max(2, GroupLimit >> 2);
    let Repaired = true;
    while (Repaired)
    {
        Repaired = false;
        for (let Group = 0; Group < Groups.length; ++Group)
        {
            if (!Groups[Group].length || Groups[Group].length >= MinimumGroup) continue;
            const Neighbours = new Map();
            for (const Member of Groups[Group])
                for (const Other of ClusterAdjacent[Member])
                {
                    const OtherGroup = Assigned[Other];
                    if (OtherGroup !== Group && Groups[OtherGroup].length)
                        Neighbours.set(OtherGroup, (Neighbours.get(OtherGroup) ?? 0) + 1);
                }
            let Target = -1, TargetSize = Infinity, TargetShared = -1;
            for (const [OtherGroup, Shared] of [...Neighbours.entries()].sort((A, B) => A[0] - B[0]))
            {
                const Size = Groups[OtherGroup].length;
                if (Size + Groups[Group].length > GroupLimit * 2) continue;
                if (Size < TargetSize || (Size === TargetSize && Shared > TargetShared))
                { Target = OtherGroup; TargetSize = Size; TargetShared = Shared; }
            }
            if (Target < 0) continue;
            for (const Member of Groups[Group]) { Assigned[Member] = Target; Groups[Target].push(Member); }
            Groups[Group] = [];
            Repaired = true;
        }
    }
    return Groups.filter((Members) => Members.length).map((Members) => Members.sort((A, B) => A - B));
}

function ExtractGroupGeometry(Geometry, TriangleList)
{
    const Positions = [], Triangles = [], Families = [], Local = new Map();
    const Emit = (Vertex) =>
    {
        let Index = Local.get(Vertex);
        if (Index === undefined)
        {
            Index = Positions.length / 3;
            Local.set(Vertex, Index);
            Positions.push(Geometry.Positions[Vertex * 3], Geometry.Positions[Vertex * 3 + 1], Geometry.Positions[Vertex * 3 + 2]);
        }
        return Index;
    };
    for (const Triangle of TriangleList)
    {
        for (let Corner = 0; Corner < 3; ++Corner) Triangles.push(Emit(Geometry.Triangles[Triangle * 3 + Corner]));
        Families.push(Geometry.Families[Triangle]);
    }
    return { Positions: new Float64Array(Positions), Triangles: new Uint32Array(Triangles), Families: new Uint32Array(Families) };
}


// 📝 Exact-position weld for concatenated group output. Triangle order is preserved (child attribution via
//    GroupOf stays aligned); only index-duplicated vertices merge and index-degenerate triangles drop.
function WeldLevelGeometry(Positions, Triangles, Families, PartitionKeys, GroupOf)
{
    const Welded = new Map(), OutPositions = [], Remap = new Uint32Array(Positions.length / 3);
    for (let Vertex = 0; Vertex < Positions.length / 3; ++Vertex)
    {
        const Key = [0, 1, 2].map((A) => Positions[Vertex * 3 + A] + 0).join(",");
        let Index = Welded.get(Key);
        if (Index === undefined)
        {
            Index = OutPositions.length / 3;
            Welded.set(Key, Index);
            OutPositions.push(Positions[Vertex * 3], Positions[Vertex * 3 + 1], Positions[Vertex * 3 + 2]);
        }
        Remap[Vertex] = Index;
    }
    const OutTriangles = [], OutFamilies = [], OutKeys = [], OutGroupOf = [];
    for (let Triangle = 0; Triangle < Triangles.length / 3; ++Triangle)
    {
        const Corners = [0, 1, 2].map((Corner) => Remap[Triangles[Triangle * 3 + Corner]]);
        if (new Set(Corners).size < 3) continue;
        OutTriangles.push(...Corners);
        OutFamilies.push(Families[Triangle]);
        OutKeys.push(PartitionKeys[Triangle]);
        OutGroupOf.push(GroupOf[Triangle]);
    }
    return {
        Positions: new Float64Array(OutPositions), Triangles: new Uint32Array(OutTriangles),
        Families: new Uint32Array(OutFamilies), PartitionKeys: OutKeys, GroupOf: OutGroupOf
    };
}

export function BuildGeometryHierarchy(Source, Options = {})
{
    const TriangleLimit = Options.TriangleLimit ?? DefaultTriangleLimit;
    const VertexLimit = Options.VertexLimit ?? DefaultVertexLimit;
    const GroupLimit = Options.GroupLimit ?? DefaultGroupLimit;
    const TargetRatio = Options.TargetRatio ?? DefaultTargetRatio;
    const MaxLevels = Options.MaxLevels ?? DefaultMaxLevels;
    const Transmissive = new Set(Options.TransmissiveFamilies ?? []);
    if (!(TriangleLimit >= 1) || !(VertexLimit >= 3) || !(GroupLimit >= 1) || !(TargetRatio > 0 && TargetRatio < 1))
        throw new Error("Geometry hierarchy limits are invalid");
    const ClassOf = (Family) => (Transmissive.has(Family) ? 1 : 0);

    let Geometry = WeldFacetSoup(Source);
    const SourceTriangles = Geometry.Triangles.length / 3;
    const Clusters = [], Levels = [];
    let LevelGeometry = Geometry, ChildClusters = null;
    for (let Level = 0; Level < MaxLevels; ++Level)
    {
        const PartitionKeys = LevelGeometry.PartitionKeys ??
            Array.from(LevelGeometry.Families, (Family) => "class:" + ClassOf(Family));
        const Meshlets = BuildMeshlets(LevelGeometry, PartitionKeys, TriangleLimit, VertexLimit);
        const FirstCluster = Clusters.length;
        const LevelClusters = [];
        Meshlets.forEach((Members, LocalIdentity) =>
        {
            const Children = ChildClusters ? [...new Set(Members.map((Triangle) => ChildClusters.GroupOf[Triangle]))]
                .flatMap((Group) => ChildClusters.Groups[Group]) : [];
            const ChildError = Children.reduce((Max, Child) => Math.max(Max, Clusters[Child].Error), 0);
            const Error = Level === 0 ? 0 : ChildError + (ChildClusters?.GroupDeviation[ChildClusters.GroupOf[Members[0]]] ?? 0);
            const Measured = MeasureCluster(LevelGeometry, Members, Error);
            // 📝 Children are attributed by whole simplification group, so this node's bounds must be the UNION
            //    of its children's bounds — otherwise a parent could be culled while part of a child it stands
            //    for is visible. Conservative, but the only attribution-independent correct choice.
            for (const Child of Children)
                for (let Axis = 0; Axis < 3; ++Axis)
                {
                    Measured.Minimum[Axis] = Math.min(Measured.Minimum[Axis], Clusters[Child].Minimum[Axis]);
                    Measured.Maximum[Axis] = Math.max(Measured.Maximum[Axis], Clusters[Child].Maximum[Axis]);
                }
            if (Children.length)
            {
                const Center = [0, 1, 2].map((Axis) => (Measured.Minimum[Axis] + Measured.Maximum[Axis]) / 2);
                let Radius = Length(Subtract(Measured.Sphere.Center, Center)) + Measured.Sphere.Radius;
                let Cutoff = Measured.Cone.Cutoff;
                for (const Child of Children)
                {
                    const Node = Clusters[Child];
                    Radius = Math.max(Radius, Length(Subtract(Node.Sphere.Center, Center)) + Node.Sphere.Radius);
                    const Alignment = Math.max(-1, Math.min(1, Dot(Measured.Cone.Axis, Node.Cone.Axis)));
                    const Worst = Alignment * Node.Cone.Cutoff -
                        Math.sqrt(Math.max(0, 1 - Node.Cone.Cutoff * Node.Cone.Cutoff)) * Math.sqrt(Math.max(0, 1 - Alignment * Alignment));
                    Cutoff = Math.min(Cutoff, Worst);
                }
                Measured.Sphere = { Center, Radius };
                Measured.Cone.Cutoff = Cutoff;
            }
            let ClassMask = 0;
            for (const Triangle of Members) ClassMask |= 1 << ClassOf(LevelGeometry.Families[Triangle]);
            const Node = {
                Identity: Clusters.length, Level, Children: Children.sort((A, B) => A - B),
                FirstTriangle: Members[0] ?? 0, Triangles: Members, TriangleCount: Members.length,
                ...Measured, Error, ClassMask
            };
            Clusters.push(Node);
            LevelClusters.push(Node.Identity);
            void LocalIdentity;
        });
        Levels.push({
            Level, Clusters: LevelClusters,
            Positions: LevelGeometry.Positions, Triangles: LevelGeometry.Triangles, Families: LevelGeometry.Families,
            TriangleCount: LevelGeometry.Triangles.length / 3, VertexCount: LevelGeometry.Positions.length / 3
        });
        if (LevelGeometry.Triangles.length / 3 <= TriangleLimit) break;
        // Group, simplify, concatenate into the parent level geometry.
        // 📝 Groups grow toward the root: a fixed small group is mostly locked border at coarse levels and
        //    the DAG stalls halfway up. Doubling per level (capped) keeps the free-interior fraction workable.
        const EffectiveGroupLimit = Math.min(GroupLimit * 2 ** Level, 32);
        const Groups = GroupClusters(LevelGeometry, Meshlets, EffectiveGroupLimit);
        const ParentPositions = [], ParentTriangles = [], ParentFamilies = [], ParentKeys = [];
        const GroupOfTriangle = [], GroupDeviation = [], GroupMembers = [];
        let Progress = false;
        Groups.forEach((Members, GroupIdentity) =>
        {
            const TriangleList = Members.flatMap((Cluster) => Meshlets[Cluster]).sort((A, B) => A - B);
            const Local = ExtractGroupGeometry(LevelGeometry, TriangleList);
            const Target = Math.max(1, Math.ceil((Local.Triangles.length / 3) * TargetRatio));
            const Simplified = SimplifyGroup(Local.Positions, Local.Triangles, Local.Families, Target);
            if (Simplified.Triangles.length < Local.Triangles.length) Progress = true;
            const Base = ParentPositions.length / 3;
            for (const Value of Simplified.Positions) ParentPositions.push(Value);
            for (const Index of Simplified.Triangles) ParentTriangles.push(Index + Base);
            for (const Family of Simplified.Families)
            {
                ParentFamilies.push(Family);
                ParentKeys.push("group:" + GroupIdentity + ":class:" + ClassOf(Family));
                GroupOfTriangle.push(GroupIdentity);
            }
            GroupDeviation.push(Simplified.Deviation);
            GroupMembers.push(Members.map((Cluster) => FirstCluster + Cluster));
        });
        if (!Progress) break;   // 📝 everything locked (tiny or all-border geometry): the DAG honestly stops here
        ChildClusters = { Groups: GroupMembers, GroupOf: GroupOfTriangle, GroupDeviation };
        // 📝 Re-weld coincident vertices across groups. Border vertices never moved, so exact-position welding
        //    restores a single manifold shell: without it, every old group seam would masquerade as an open
        //    border at the next level, lock solid, and stall the DAG halfway up.
        LevelGeometry = WeldLevelGeometry(new Float64Array(ParentPositions), new Uint32Array(ParentTriangles),
            new Uint32Array(ParentFamilies), ParentKeys, GroupOfTriangle);
        ChildClusters.GroupOf = LevelGeometry.GroupOf;
        LevelGeometry.GroupOf = undefined;
    }
    return { Clusters, Levels, SourceTriangles, WeldedVertices: Geometry.Positions.length / 3 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VALIDATION SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Independent re-checks used by the proof tool and tests; nothing here trusts builder-internal bookkeeping.
export function ValidateGeometryHierarchy(Source, Hierarchy, Options = {})
{
    const TriangleLimit = Options.TriangleLimit ?? DefaultTriangleLimit;
    const VertexLimit = Options.VertexLimit ?? DefaultVertexLimit;
    const Checks = { Passed: true, Failures: [] };
    const Confirm = (Condition, Caption) => { if (!Condition) { Checks.Passed = false; Checks.Failures.push(Caption); } };
    Confirm(Hierarchy.SourceTriangles === Source.Facets.length, "source triangle count changed during weld");
    const Leaf = Hierarchy.Levels[0];
    const Covered = new Int32Array(Leaf.TriangleCount);
    for (const Identity of Leaf.Clusters)
    {
        const Node = Hierarchy.Clusters[Identity];
        Confirm(Node.TriangleCount <= TriangleLimit, "leaf meshlet exceeds the triangle limit");
        const Vertices = new Set();
        for (const Triangle of Node.Triangles)
        {
            ++Covered[Triangle];
            for (let Corner = 0; Corner < 3; ++Corner) Vertices.add(Leaf.Triangles[Triangle * 3 + Corner]);
        }
        Confirm(Vertices.size <= VertexLimit, "leaf meshlet exceeds the vertex limit");
        Confirm(Node.Error === 0, "leaf cluster carries simplification error");
    }
    Confirm([...Covered].every((Count) => Count === 1), "leaf clusters do not cover every source triangle exactly once");
    for (const Level of Hierarchy.Levels)
    {
        if (Level.Level === 0) continue;
        const ParentPositions = new Set();
        for (let Vertex = 0; Vertex < Level.VertexCount; ++Vertex)
            ParentPositions.add([0, 1, 2].map((A) => Level.Positions[Vertex * 3 + A] + 0).join(","));
        const ChildLevelEdges = BuildEdgeIncidence(Hierarchy.Levels[Level.Level - 1].Triangles,
            Hierarchy.Levels[Level.Level - 1].VertexCount).Edges;
        for (const Identity of Level.Clusters)
        {
            const Node = Hierarchy.Clusters[Identity];
            Confirm(Node.Children.length > 0, "parent cluster has no children");
            const ChildError = Math.max(...Node.Children.map((Child) => Hierarchy.Clusters[Child].Error));
            Confirm(Node.Error >= ChildError, "cluster error is not monotonic toward the root");
            // Bounds (already error-expanded) must contain every child cluster's bounds.
            for (const Child of Node.Children)
            {
                const ChildNode = Hierarchy.Clusters[Child];
                for (let A = 0; A < 3; ++A)
                {
                    Confirm(Node.Minimum[A] <= ChildNode.Minimum[A] + 1e-9, "parent bounds do not contain child bounds");
                    Confirm(Node.Maximum[A] >= ChildNode.Maximum[A] - 1e-9, "parent bounds do not contain child bounds");
                }
            }
            // Crack check: vertices on the border of the child group's union keep their exact positions in the
            // parent geometry, so independently simplified neighbours still meet along the same border polyline.
            const ChildLevel = Hierarchy.Levels[Level.Level - 1];
            const UnionTriangles = new Set(Node.Children.flatMap((Child) => Hierarchy.Clusters[Child].Triangles));
            for (const Entry of ChildLevelEdges.values())
            {
                const Inside = Entry.Triangles.filter((Triangle) => UnionTriangles.has(Triangle));
                if (!Inside.length || Inside.length === Entry.Triangles.length && Entry.Triangles.length === 2) continue;
                for (const Vertex of [Entry.A, Entry.B])
                {
                    const Key = [0, 1, 2].map((A) => ChildLevel.Positions[Vertex * 3 + A] + 0).join(",");
                    Confirm(ParentPositions.has(Key), "shared border vertex moved during simplification (crack)");
                }
            }
        }
    }
    Checks.Levels = Hierarchy.Levels.map((Level) => ({
        Level: Level.Level, Clusters: Level.Clusters.length, Triangles: Level.TriangleCount, Vertices: Level.VertexCount,
        MaxError: Math.max(0, ...Level.Clusters.map((Identity) => Hierarchy.Clusters[Identity].Error))
    }));
    return Checks;
}

// 📝 Flat soup for one level with the owning cluster per triangle — the debug views and proof renders read this.
// 📝 Diagnostic surface for tests and proof tooling: the same pipeline stages the builder runs, exposed so a
//    failing group can be replayed in isolation instead of guessed at.
export const HierarchyInternals = { BuildEdgeIncidence, BuildMeshlets, GroupClusters, ExtractGroupGeometry,
    SimplifyGroup, WeldLevelGeometry, MeasureCluster };

export function CollectHierarchyLevel(Hierarchy, Level)
{
    const Entry = Hierarchy.Levels[Level];
    if (!Entry) throw new Error("Hierarchy level does not exist: " + Level);
    const Positions = [], Families = [], Owners = [], Errors = [];
    for (const Identity of Entry.Clusters)
    {
        const Node = Hierarchy.Clusters[Identity];
        for (const Triangle of Node.Triangles)
        {
            for (let Corner = 0; Corner < 3; ++Corner)
            {
                const Vertex = Entry.Triangles[Triangle * 3 + Corner];
                Positions.push(Entry.Positions[Vertex * 3], Entry.Positions[Vertex * 3 + 1], Entry.Positions[Vertex * 3 + 2]);
            }
            Families.push(Entry.Families[Triangle]);
            Owners.push(Identity);
            Errors.push(Node.Error);
        }
    }
    return { Positions: new Float64Array(Positions), Families: new Uint32Array(Families),
             Owners: new Uint32Array(Owners), Errors: new Float64Array(Errors), TriangleCount: Families.length };
}
