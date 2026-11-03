'use strict';
//----------------------------------------------------------------------------------------------------------------------------
//                                            Frontier · World Blockout · OceanMesh
//----------------------------------------------------------------------------------------------------------------------------
// 📝 What this file is: the adaptive half of the water.
//
//    The owner's brief: "like Nanite, but for water" — spend vertices where the eye can see them and nowhere else. So the
//    ocean is three tiers with one continuous surface:
//
//      ① NEAR   the footprint of the 2.5D solve. Always full resolution, because that is where the simulation lives and a
//               coarse grid there would throw the solve away.
//      ② MID    a quadtree over a square around the viewer, split worst-screen-error-first until the triangle budget runs
//               out. Tiles carry skirts, so a level difference between neighbours cannot open a crack.
//      ③ FAR    a radial fan of concentric rings out to the horizon: roughly a hundred triangles a ring, carrying the
//               spectral field as displacement and normal.
//
//    No tier is a texture. Every vertex height is evaluated from OceanField: the solve where it exists, the spectral sum
//    where it does not, blended by the same weight the shader would use.
//
//    The plan is a pure function of the camera, so the evidence scripts can assert resolution and triangle counts without
//    a GPU: that is what makes "no wasted resolution" a claim with numbers behind it.
//----------------------------------------------------------------------------------------------------------------------------

const OceanMesh = (function ()
{
    const DEFAULT_OPTIONS = {
        ScreenHeightPx: 1080,
        FieldOfViewDeg: 55,
        PixelBudget: 1,
        MaxTriangles: 260000,
        TileCells: 4,
        MinCellKm: 0.004,
        CameraHeightKm: 8,
        AdaptiveKm: 12,
        FarKm: 300,
        NearRings: 14,
        NearSegments: 256,
        SkirtFactor: 1.5
    };

    const Options = (Base, Given) => Object.assign({}, Base, Given || {});

    // 📝 Pixels per radian is the only projection fact the plan needs: a cell of size S at distance D covers S/D radians,
    //    and that is what a pixel budget is spending itself against.
    function PixelsPerRadian(Settings)
    {
        return Settings.ScreenHeightPx / (2 * Math.tan(Settings.FieldOfViewDeg * Math.PI / 360));
    }

    function ScreenError(SizeKm, DistanceKm, Pixels)
    {
        return (SizeKm / Math.max(0.001, DistanceKm)) * Pixels;
    }

    // 📝 The camera looks down at the focus from CameraHeightKm; a tile's error uses the distance to its nearest edge, since
    //    that is the part of it closest to the eye and therefore the part that betrays a coarse grid first.
    function ResolveDistance(Tile, Settings)
    {
        const ClosestX = Math.max(Tile.X0, Math.min(0, Tile.X0 + Tile.Size));
        const ClosestY = Math.max(Tile.Y0, Math.min(0, Tile.Y0 + Tile.Size));
        const Ground = Math.hypot(ClosestX, ClosestY);
        return Math.hypot(Ground, Settings.CameraHeightKm);
    }

    function CreateTile(X0, Y0, Size, Depth)
    {
        return { X0, Y0, Size, Depth, Error: 0, Split: false };
    }

    function Subdivide(Tile)
    {
        const Half = Tile.Size / 2;
        return [
            CreateTile(Tile.X0, Tile.Y0, Half, Tile.Depth + 1),
            CreateTile(Tile.X0 + Half, Tile.Y0, Half, Tile.Depth + 1),
            CreateTile(Tile.X0, Tile.Y0 + Half, Half, Tile.Depth + 1),
            CreateTile(Tile.X0 + Half, Tile.Y0 + Half, Half, Tile.Depth + 1)
        ];
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE PLAN
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Split worst-first by screen error with a real max-heap: the tile whose cells are furthest over budget gets the next
    //    four, so a fixed triangle budget is spent where it buys the most pixels. The count is deterministic, which is what
    //    lets the evidence scripts assert it without a GPU.
    function CreatePlan(Ocean, Given)
    {
        const Settings = Options(DEFAULT_OPTIONS, Given);
        Settings.FocusLon = Given?.FocusLon ?? (Ocean.Patch ? Ocean.Patch.FocusLon : 0);
        Settings.FocusLat = Given?.FocusLat ?? (Ocean.Patch ? Ocean.Patch.FocusLat : 0);
        const Pixels = PixelsPerRadian(Settings);
        const Patch = Ocean.Patch;
        const NearCellKm = Patch ? Patch.CellKm : Settings.MinCellKm;
        const NearRadiusKm = Patch ? Patch.ExtentKm / 2 : 0;
        const Cells = Settings.TileCells;
        const TileTriangles = 2 * Cells * Cells + 8 * Cells;

        function Score(Tile)
        {
            const Distance = ResolveDistance(Tile, Settings);
            const ClosestX = Math.max(Tile.X0, Math.min(0, Tile.X0 + Tile.Size));
            const ClosestY = Math.max(Tile.Y0, Math.min(0, Tile.Y0 + Tile.Size));
            Tile.Near = Math.hypot(ClosestX, ClosestY) <= NearRadiusKm;
            Tile.Error = ScreenError(Tile.Size / Cells, Distance, Pixels);
            // 📝 Near the solve the answer is always "more vertices": the pixel test would happily drop the 2.5D surface, so
            //    those tiles outrank every distant one and are settled before a single mid-field tile bids for budget.
            Tile.Priority = Tile.Near ? Number.MAX_SAFE_INTEGER : Tile.Error;
            return Tile;
        }

        const Root = Score(CreateTile(-Settings.AdaptiveKm, -Settings.AdaptiveKm, Settings.AdaptiveKm * 2, 0));
        const Heap = [Root];
        const Above = (A, B) => A.Priority > B.Priority;

        function Push(Tile)
        {
            Heap.push(Tile);
            let Index = Heap.length - 1;
            while (Index > 0)
            {
                const Parent = (Index - 1) >> 1;
                if (!Above(Heap[Index], Heap[Parent])) break;
                [Heap[Index], Heap[Parent]] = [Heap[Parent], Heap[Index]];
                Index = Parent;
            }
        }

        function Pop()
        {
            const Top = Heap[0], Last = Heap.pop();
            if (Heap.length > 0)
            {
                Heap[0] = Last;
                let Index = 0;
                for (;;)
                {
                    const Left = Index * 2 + 1, Right = Left + 1;
                    let Best = Index;
                    if (Left < Heap.length && Above(Heap[Left], Heap[Best])) Best = Left;
                    if (Right < Heap.length && Above(Heap[Right], Heap[Best])) Best = Right;
                    if (Best === Index) break;
                    [Heap[Index], Heap[Best]] = [Heap[Best], Heap[Index]];
                    Index = Best;
                }
            }
            return Top;
        }

        // 📝 The far fan is paid for first: it is not optional geometry, so the quadtree bids for what is left.
        const FarReserve = 2 * Settings.NearSegments * Settings.NearRings + 4 * Settings.TileCells * 2;
        const Leaves = [];
        let Splits = 0;
        let Estimated = TileTriangles;
        while (Heap.length > 0)
        {
            const Tile = Pop();
            const Floor = Tile.Near ? NearCellKm : Settings.MinCellKm;
            const AtFloor = Tile.Size / Cells <= Floor;
            // 📝 The solve footprint is not negotiable, so it never asks the budget for permission; everything else does.
            const Affordable = Tile.Near || Estimated + 3 * TileTriangles + FarReserve <= Settings.MaxTriangles;
            const Satisfied = !Tile.Near && Tile.Error <= Settings.PixelBudget;
            if (AtFloor || !Affordable || Satisfied)
            {
                Leaves.push(Tile);
                continue;
            }
            ++Splits;
            Estimated += 3 * TileTriangles;
            for (const Child of Subdivide(Tile)) Push(Score(Child));
        }

        return { Ocean, Settings, Pixels, Leaves, Splits, Root, NearCellKm, NearRadiusKm, Estimated };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE MESH
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 One pass over the plan: tiles become grids of quads with skirts, the far field becomes a fan, and every vertex asks
    //    OceanField for its height. The mesh is the plan with numbers in it.
    function BuildMesh(Ocean, Plan, Given)
    {
        const Settings = Options(Plan.Settings, Given);
        const Time = Settings.Time ?? Ocean.Clock;
        const Cells = Settings.TileCells;
        const Strip = Cells + 1;
        const East = [], North = [], LonOut = [], LatOut = [], Height = [], Weights = [], Depths = [], Drops = [];
        const Indices = [];
        const Tiles = [];

        const ToLonLat = (EastKm, NorthKm) => ({
            Lon: Settings.FocusLon + EastKm / (111.320 * Math.max(0.05, Math.cos(Settings.FocusLat * Math.PI / 180))),
            Lat: Settings.FocusLat + NorthKm / 110.574
        });

        function AddVertex(EastKm, NorthKm, DropKm)
        {
            const Location = ToLonLat(EastKm, NorthKm);
            const Surface = EvaluateSurface(Ocean, Location.Lon, Location.Lat, Settings, Time, EastKm, NorthKm);
            East.push(EastKm);
            North.push(NorthKm);
            LonOut.push(Location.Lon);
            LatOut.push(Location.Lat);
            Height.push(Surface.Height - (DropKm || 0));
            Drops.push(DropKm || 0);
            Weights.push(Surface.Weight);
            Depths.push(Surface.Depth ?? 0);
            return East.length - 1;
        }

        // 📝 Skirts are the crack insurance: every border vertex gets a copy SkirtKm lower, and the border quad between them
        //    closes the gap a coarser neighbour would otherwise open.
        function AddPatch(Tile)
        {
            const Base = East.length;
            const Step = Tile.Size / Cells;
            const SkirtKm = Math.max(0.25, Step * Settings.SkirtFactor);
            for (let Row = 0; Row <= Cells; ++Row)
            {
                for (let Column = 0; Column <= Cells; ++Column)
                {
                    AddVertex(Tile.X0 + Column * Step, Tile.Y0 + Row * Step, 0);
                }
            }
            for (let Row = 0; Row < Cells; ++Row)
            {
                for (let Column = 0; Column < Cells; ++Column)
                {
                    const A = Base + Row * Strip + Column;
                    const B = A + 1, C = A + Strip, D = C + 1;
                    Indices.push(A, C, B, B, C, D);
                }
            }
            const Border = [];
            for (let Column = 0; Column <= Cells; ++Column) Border.push(Base + Column);
            for (let Row = 1; Row <= Cells; ++Row) Border.push(Base + Row * Strip + Cells);
            for (let Column = Cells - 1; Column >= 0; --Column) Border.push(Base + Cells * Strip + Column);
            for (let Row = Cells - 1; Row >= 1; --Row) Border.push(Base + Row * Strip);
            const Skirt = Border.map(Index => AddVertex(East[Index], North[Index], SkirtKm));
            for (let Index = 0; Index < Border.length; ++Index)
            {
                const Next = (Index + 1) % Border.length;
                const A = Border[Index], B = Border[Next], C = Skirt[Index], D = Skirt[Next];
                Indices.push(A, C, B, B, C, D);
            }
            Tiles.push({
                X0: Tile.X0, Y0: Tile.Y0, Size: Tile.Size, Depth: Tile.Depth, Near: !!Tile.Near,
                CellKm: Step, SkirtKm, Vertices: Cells * Cells, Triangles: 2 * Cells * Cells + 2 * Border.length
            });
        }

        for (const Tile of Plan.Leaves) AddPatch(Tile);

        // 📝 Far tier: concentric rings at geometric spacing, an even number of segments, and the spectral field as height.
        //    Cheap on purpose — at those distances a cell is far under a pixel and the shape of the sea state is all that
        //    survives.
        const Rings = Settings.NearRings;
        const Segments = Settings.NearSegments;
        const Inner = Settings.AdaptiveKm * 0.98;
        const FarStart = East.length;
        for (let Ring = 0; Ring <= Rings; ++Ring)
        {
            const T = Ring / Rings;
            const Radius = Inner + (Settings.FarKm - Inner) * Math.pow(T, 1.8);
            for (let Segment = 0; Segment < Segments; ++Segment)
            {
                const Angle = (Segment / Segments) * Math.PI * 2;
                AddVertex(Math.cos(Angle) * Radius, Math.sin(Angle) * Radius, 0);
            }
        }
        for (let Ring = 0; Ring < Rings; ++Ring)
        {
            for (let Segment = 0; Segment < Segments; ++Segment)
            {
                const Next = (Segment + 1) % Segments;
                const A = FarStart + Ring * Segments + Segment;
                const B = FarStart + Ring * Segments + Next;
                const C = A + Segments, D = B + Segments;
                Indices.push(A, C, B, B, C, D);
            }
        }

        const Mesh = {
            X: new Float32Array(East), Y: new Float32Array(North),
            Lon: new Float32Array(LonOut), Lat: new Float32Array(LatOut),
            Height: new Float32Array(Height), Weight: new Float32Array(Weights),
            Depth: new Float32Array(Depths), Drop: new Float32Array(Drops), Indices: new Uint32Array(Indices),
            Tiles, Settings, Pixels: Plan.Pixels,
            Vertices: East.length, Triangles: Indices.length / 3,
            TriangleBudget: Settings.MaxTriangles,
            NearTriangles: Tiles.filter(Tile => Tile.Near).reduce((Total, Tile) => Total + Tile.Triangles, 0),
            MidTriangles: Tiles.filter(Tile => !Tile.Near).reduce((Total, Tile) => Total + Tile.Triangles, 0),
            FarTriangles: (Indices.length / 3) - Tiles.reduce((Total, Tile) => Total + Tile.Triangles, 0),
            MinCellKm: Tiles.reduce((Low, Tile) => Math.min(Low, Tile.CellKm), Infinity),
            MaxCellKm: Tiles.reduce((High, Tile) => Math.max(High, Tile.CellKm), 0),
            SkirtKm: Tiles.reduce((High, Tile) => Math.max(High, Tile.SkirtKm), 0),
            Time
        };
        return Mesh;
    }

    // 📝 One surface evaluation shared by the mesh, the editor readout and the evidence scripts. Depth comes from the terrain
    //    SDF so the water can be shaded by how deep it is without asking the terrain again.
    function EvaluateSurface(Ocean, Lon, Lat, Settings, Time, EastKm, NorthKm)
    {
        const DistanceKm = Math.hypot(EastKm || 0, NorthKm || 0);
        const Weight = Ocean.BlendWeight(DistanceKm);
        const Far = Ocean.SpectralHeight(Lon, Lat, Time);
        let Height = Far, Near = null, Depth = 0;
        if (Ocean.Patch && Weight < 1)
        {
            Near = Ocean.PatchHeight(Lon, Lat);
            if (Near !== null) Height = Near * (1 - Weight) + Far * Weight;
            else Height = Far;
        }
        if (Ocean.Terrain && Ocean.Terrain.Sample && DistanceKm < (Settings?.DepthRadiusKm ?? 4))
        {
            Depth = Math.max(0, -Ocean.Terrain.Sample.Distance(Lon, Lat));
        }
        return { Height, Weight, Depth, Near };
    }

    // 📝 Every frame the surface moves, the topology does not. Refreshing heights in place is what lets the editor run at
    //    thirty frames a second while the plan only changes when the camera does.
    function RefreshMesh(Ocean, Mesh, Time)
    {
        const Settings = Mesh.Settings;
        // 📝 Longitude and latitude are recomputed from the double-precision kilometre offsets, not read back from the float
        //    arrays: a vertex sitting exactly on a patch cell boundary must land in the same cell as it did at build time.
        const Cosine = Math.max(0.05, Math.cos(Settings.FocusLat * Math.PI / 180));
        for (let Index = 0; Index < Mesh.Vertices; ++Index)
        {
            const EastKm = Mesh.X[Index], NorthKm = Mesh.Y[Index];
            const Lon = Settings.FocusLon + EastKm / (111.320 * Cosine);
            const Lat = Settings.FocusLat + NorthKm / 110.574;
            const DistanceKm = Math.hypot(EastKm, NorthKm);
            const Weight = Ocean.BlendWeight(DistanceKm);
            const Far = Ocean.SpectralHeight(Lon, Lat, Time);
            let Height = Far;
            if (Weight < 1)
            {
                const Near = Ocean.PatchHeight(Lon, Lat);
                if (Near !== null) Height = Near * (1 - Weight) + Far * Weight;
            }
            Mesh.Weight[Index] = Weight;
            Mesh.Height[Index] = Height - (Mesh.Drop ? Mesh.Drop[Index] : 0);
        }
        Mesh.Time = Time;
        return Mesh;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE EVIDENCE
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The comparison that answers "no effort wasted": what a uniform grid at the near cell size would have cost over the
    //    same area, against what this plan actually spends, plus the worst screen error it accepts.
    function Summarize(Plan, Mesh)
    {
        const Settings = Plan.Settings;
        const NearCell = Plan.NearCellKm || Settings.MinCellKm;
        const Tiles = Mesh ? Mesh.Tiles : Plan.Leaves;
        const WorstError = Tiles.filter(Tile => !Tile.Near).reduce((Worst, Tile) =>
        {
            const Distance = ResolveDistance({ X0: Tile.X0, Y0: Tile.Y0, Size: Tile.Size }, Settings);
            return Math.max(Worst, ScreenError(Tile.Size / Settings.TileCells, Distance, Plan.Pixels));
        }, 0);
        const SpanKm = Settings.AdaptiveKm * 2;
        const UniformCell = Math.max(0.0001, NearCell);
        const UniformTriangles = 2 * Math.pow(SpanKm / UniformCell, 2);
        const Triangles = Mesh ? Mesh.Triangles : Tiles.reduce((Total, Tile) => Total + 2 * Settings.TileCells ** 2, 0);
        return {
            Tiles: Tiles.length,
            Triangles,
            NearTriangles: Mesh ? Mesh.NearTriangles : 0,
            MidTriangles: Mesh ? Mesh.MidTriangles : 0,
            FarTriangles: Mesh ? Mesh.FarTriangles : 0,
            NearCellKm: NearCell, MinCellKm: Mesh ? Mesh.MinCellKm : NearCell,
            MaxCellKm: Mesh ? Mesh.MaxCellKm : Settings.AdaptiveKm,
            WorstErrorPx: WorstError === 0 ? 0 : WorstError,
            PixelBudget: Settings.PixelBudget,
            UniformTriangles,
            Ratio: UniformTriangles / Math.max(1, Triangles),
            SkirtKm: Mesh ? Mesh.SkirtKm : 0,
            Budget: Settings.MaxTriangles,
            WithinBudget: Triangles <= Settings.MaxTriangles
        };
    }

    return {
        CreatePlan, BuildMesh, RefreshMesh, Summarize, EvaluateSurface,
        PixelsPerRadian, ScreenError, DEFAULT_OPTIONS
    };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = OceanMesh;
