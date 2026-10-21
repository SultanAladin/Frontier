import { BuildGeometryHierarchy } from "./GeometryHierarchy.js";
//============================================================================================================================================
//                                                         VIRTUALIZEDGEOMETRY.JS
//============================================================================================================================================
// 🧩 GPU Phase-2 runtime for the hierarchy build: every DAG cluster of every eligible facet set becomes a record
//    the parallel cull program (Shader/VirtualizedGeometryCull.wgsl) cuts by projected error, frustum- and
//    cone-culls, and occlusion-tests in two phases against a Reversed-Z Hi-Z pyramid built from the surface
//    depth (Shader/HiZPyramid.wgsl). Phase-1 draws render, the pyramid is rebuilt from their depth, then
//    phase-2 draws add newly disoccluded clusters. Inert unless the page is opened with ?hierarchy=1, in which
//    case it takes over the eligible static facet sets from the lossless cluster path (same eligibility rule:
//    >= 256 facets, no dynamic vertex capacity, no Dynamic-role occupants).

const RecordFloats = 24;   // 96 B HierarchyClusterRecord (see VirtualizedGeometryCull.wgsl)
const CommandBytes = 20;   // IndirectDrawCommand

function FaceNormal(Positions, A, B, C)
{
    const AB = [Positions[B * 3] - Positions[A * 3], Positions[B * 3 + 1] - Positions[A * 3 + 1], Positions[B * 3 + 2] - Positions[A * 3 + 2]];
    const AC = [Positions[C * 3] - Positions[A * 3], Positions[C * 3 + 1] - Positions[A * 3 + 1], Positions[C * 3 + 2] - Positions[A * 3 + 2]];
    const N = [AB[1] * AC[2] - AB[2] * AC[1], AB[2] * AC[0] - AB[0] * AC[2], AB[0] * AC[1] - AB[1] * AC[0]];
    const L = Math.hypot(N[0], N[1], N[2]) || 1;
    return [N[0] / L, N[1] / L, N[2] / L];
}

export class VirtualizedGeometry
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders = Shaders;
        this.Cache = new Map();
        this.Enabled = new URLSearchParams(location.search).get("hierarchy") === "1";
        this.FrameCount = 0;
    }

    async ConstructPrograms(FrameRecords, OccupantRecords)
    {
        if (!this.Enabled) return;
        const E = this.Exchange, Compute = GPUShaderStage.COMPUTE;
        this.FrameRecords = FrameRecords;
        this.OccupantRecords = OccupantRecords;
        this.CullLayout = E.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "StorageRead", "Storage", "Storage", "Storage", "Texture2DUnfiltered"],
            Compute, "Virtualized geometry culling");
        this.SeedLayout = E.ConstructDescriptorLayout(["TextureDepth2D", "StorageTexture2D:r32float"], Compute, "Hi-Z seed");
        this.DownsampleLayout = E.ConstructDescriptorLayout(["Texture2DUnfiltered", "StorageTexture2D:r32float"], Compute, "Hi-Z downsample");
        this.CullProgram = await this.Shaders.ConstructComputeProgram("VirtualizedGeometryCull.wgsl", "SelectAndCullPhase1", [this.CullLayout]);
        this.Phase2Program = await this.Shaders.ConstructComputeProgram("VirtualizedGeometryCull.wgsl", "CullPhase2Disoccluded", [this.CullLayout]);
        this.SeedProgram = await this.Shaders.ConstructComputeProgram("HiZPyramid.wgsl", "SeedHiZFromDepth", [this.SeedLayout]);
        this.DownsampleProgram = await this.Shaders.ConstructComputeProgram("HiZPyramid.wgsl", "DownsampleHiZMip", [this.DownsampleLayout]);
        // 1x1 dummy pyramid (depth 0 = far): every Hi-Z test against it passes, so frame one draws its full cut.
        this.DummyHiZ = E.ConstructTexture({ label: "Hi-Z dummy", size: [1, 1], format: "r32float",
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING });
        this.PreviousHiZView = this.DummyHiZ.createView();
    }

    ConstructTargets(Width, Height)
    {
        if (!this.Enabled) return;
        for (const Texture of this.HiZTextures ?? []) Texture.destroy();
        this.HiZTextures = [];
        this.HiZViews = [];
        let W = Width, H = Height;
        while (true)
        {
            const Texture = this.Exchange.ConstructTexture({ label: `Hi-Z mip ${this.HiZTextures.length}`, size: [W, H], format: "r32float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING });
            this.HiZTextures.push(Texture);
            this.HiZViews.push(Texture.createView());
            if (W === 1 && H === 1) break;
            W = Math.max(1, W >> 1); H = Math.max(1, H >> 1);
        }
        this.PreviousHiZView = this.DummyHiZ.createView();
        this.Extent = [Width, Height];
    }

    // Builds (once per facet-set revision) the GPU cluster records, expanded vertex records and local indices
    // for one facet set's whole DAG. Returns null when the set is not eligible.
    BuildSet(FacetSetIndex, Set)
    {
        const Cached = this.Cache.get(FacetSetIndex);
        if (Cached && Cached.FacetCount === Set.Facets.length) return Cached;
        const Hierarchy = BuildGeometryHierarchy(Set, {});
        const Vertices = [], Indices = [], Records = [];
        const ClusterRecordIndex = new Map();
        for (const Level of Hierarchy.Levels)
        {
            for (const ClusterId of Level.Clusters)
            {
                const Node = Hierarchy.Clusters[ClusterId];
                const BaseVertex = Vertices.length / 8, FirstIndex = Indices.length;
                for (const Triangle of Node.Triangles)
                {
                    const Corner = [0, 1, 2].map((C) => Level.Triangles[Triangle * 3 + C]);
                    const Normal = FaceNormal(Level.Positions, Corner[0], Corner[1], Corner[2]);
                    const Family = Level.Families[Triangle];
                    const FamilyBits = new DataView(new ArrayBuffer(4));
                    FamilyBits.setUint32(0, Family, true);
                    const FamilyFloat = FamilyBits.getFloat32(0, true);
                    for (const Vertex of Corner)
                    {
                        // 📝 Global index (the cull program emits BaseVertex 0): one expanded vertex per corner
                        //    so per-triangle family and face normal are exact at every DAG level.
                        Indices.push(Vertices.length / 8);
                        Vertices.push(Level.Positions[Vertex * 3], Level.Positions[Vertex * 3 + 1], Level.Positions[Vertex * 3 + 2],
                            FamilyFloat, Normal[0], Normal[1], Normal[2], 0);
                    }
                }
                ClusterRecordIndex.set(ClusterId, Records.length);
                Records.push({ Node, FirstIndex, BaseVertex, IndexCount: Node.TriangleCount * 3 });
            }
        }
        const Built = { FacetCount: Set.Facets.length, Hierarchy, Vertices: new Float32Array(Vertices),
                        Indices: new Uint32Array(Indices), Records, ClusterRecordIndex };
        this.Cache.set(FacetSetIndex, Built);
        return Built;
    }

    Prepare(Scene)
    {
        if (!this.Enabled || !this.CullProgram) return null;
        const Key = Scene.DrawRuns.map((R) => `${R.FacetSet}:${R.First}:${R.Count}`).join("|");
        if (Key === this.Key && this.Records) return this;
        this.Key = Key;
        for (const Buffer of this.Buffers ?? []) Buffer.destroy();
        this.Buffers = [];
        this.Draws = [];            // { Record, Occupant } in record order
        this.OwnedOccupants = new Set();
        const Vertices = [], Indices = [], RecordData = [];
        const RecordU32 = [];
        for (const Run of Scene.DrawRuns)
        {
            const Set = Scene.FacetSets[Run.FacetSet];
            const Eligible = Set.Facets.length >= 256 && !Set.VertexCapacity &&
                Array.from({ length: Run.Count }, (_, I) => Scene.Occupants[Run.First + I]).every((O) => !(O.Roles & 8));
            if (!Eligible) continue;
            const Built = this.BuildSet(Run.FacetSet, Set);
            if (!Built) continue;
            const VertexBase = Vertices.length / 8, IndexBase = Indices.length;
            for (const Value of Built.Vertices) Vertices.push(Value);
            for (const Index of Built.Indices) Indices.push(Index + VertexBase);
            for (let O = Run.First; O < Run.First + Run.Count; ++O)
            {
                this.OwnedOccupants.add(O);
                for (const Record of Built.Records)
                {
                    const Node = Record.Node;
                    const Floats = new Float32Array(RecordFloats), U32 = new Uint32Array(Floats.buffer);
                    Floats.set(Node.TightMinimum ?? Node.Minimum, 0); U32[3] = O;
                    Floats.set(Node.TightMaximum ?? Node.Maximum, 4); U32[7] = Record.FirstIndex + IndexBase;
                    Floats.set([...Node.Sphere.Center, Node.Sphere.Radius], 8);
                    Floats.set([...(Node.ParentSphere?.Center ?? Node.Sphere.Center), Node.ParentSphere?.Radius ?? Node.Sphere.Radius], 12);
                    Floats.set([...Node.Cone.Axis, Node.Cone.Cutoff], 16);
                    Floats[20] = Node.Error;
                    Floats[21] = Number.isFinite(Node.ParentError) ? Node.ParentError : 1e30;
                    U32[22] = Record.IndexCount;
                    U32[23] = Node.ClassMask | (Node.Cone.Cutoff > -1 ? 4 : 0) | (Node.Level << 8);
                    RecordData.push(Floats); RecordU32.push(U32);
                    this.Draws.push({ Record: this.Draws.length, Occupant: O, BaseVertex: Record.BaseVertex + VertexBase });
                }
            }
        }
        this.RecordCount = this.Draws.length;
        if (!this.RecordCount) { this.Records = null; return this; }
        const E = this.Exchange;
        const Make = (Data, Usage, Label) =>
        {
            const Buffer = E.ConstructRecords(Math.max(4, Data.byteLength), Usage | GPUBufferUsage.COPY_DST, Label);
            if (Data.byteLength) E.Device.queue.writeBuffer(Buffer, 0, Data);
            this.Buffers.push(Buffer);
            return Buffer;
        };
        const Flat = new Float32Array(this.RecordCount * RecordFloats);
        RecordData.forEach((Floats, I) => Flat.set(Floats, I * RecordFloats));
        this.ClusterRecords = Make(Flat, GPUBufferUsage.STORAGE, "Hierarchy cluster records");
        this.VertexBuffer = Make(new Float32Array(Vertices), GPUBufferUsage.VERTEX, "Hierarchy level vertices");
        this.IndexBuffer = Make(new Uint32Array(Indices), GPUBufferUsage.INDEX, "Hierarchy cluster indices");
        const CommandUsage = GPUBufferUsage.STORAGE | GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_SRC;
        this.Phase1Commands = E.ConstructRecords(this.RecordCount * CommandBytes, CommandUsage, "Hierarchy phase-1 draws");
        this.Phase2Commands = E.ConstructRecords(this.RecordCount * CommandBytes, CommandUsage, "Hierarchy phase-2 draws");
        this.States = E.ConstructRecords(this.RecordCount * 4, GPUBufferUsage.STORAGE, "Hierarchy cluster states");
        this.Buffers.push(this.Phase1Commands, this.Phase2Commands, this.States);
        this.Records = true;
        this.RefreshGroups();
        return this;
    }

    RefreshGroups()
    {
        if (!this.Records) return;
        this.CullGroup = this.Exchange.ConstructDescriptorGroup(this.CullLayout,
            [this.FrameRecords, this.OccupantRecords, this.ClusterRecords, this.Phase1Commands, this.Phase2Commands, this.States,
             this.PreviousHiZView]);
    }

    RecordCullPhase1(Encoder)
    {
        if (!this.Records) return;
        this.RefreshGroups();
        const Pass = Encoder.beginComputePass({ label: "Virtualized geometry cut + phase-1 cull" });
        Pass.setPipeline(this.CullProgram);
        Pass.setBindGroup(0, this.CullGroup);
        Pass.dispatchWorkgroups(Math.ceil(this.RecordCount / 64));
        Pass.end();
    }

    RecordHiZ(Encoder, DepthView)
    {
        if (!this.Records || !this.HiZViews?.length) return;
        const E = this.Exchange;
        const SeedGroup = E.ConstructDescriptorGroup(this.SeedLayout, [DepthView, this.HiZViews[0]]);
        let Pass = Encoder.beginComputePass({ label: "Hi-Z seed" });
        Pass.setPipeline(this.SeedProgram);
        Pass.setBindGroup(0, SeedGroup);
        Pass.dispatchWorkgroups(Math.ceil(this.Extent[0] / 8), Math.ceil(this.Extent[1] / 8));
        Pass.end();
        for (let Mip = 1; Mip < this.HiZViews.length; ++Mip)
        {
            const W = Math.max(1, this.Extent[0] >> Mip), H = Math.max(1, this.Extent[1] >> Mip);
            const Group = E.ConstructDescriptorGroup(this.DownsampleLayout, [this.HiZViews[Mip - 1], this.HiZViews[Mip]]);
            Pass = Encoder.beginComputePass({ label: `Hi-Z downsample ${Mip}` });
            Pass.setPipeline(this.DownsampleProgram);
            Pass.setBindGroup(0, Group);
            Pass.dispatchWorkgroups(Math.ceil(W / 8), Math.ceil(H / 8));
            Pass.end();
        }
        this.CurrentHiZView = this.HiZViews[0];
    }

    RecordCullPhase2(Encoder)
    {
        if (!this.Records) return;
        const Group = this.Exchange.ConstructDescriptorGroup(this.CullLayout,
            [this.FrameRecords, this.OccupantRecords, this.ClusterRecords, this.Phase1Commands, this.Phase2Commands, this.States,
             this.CurrentHiZView ?? this.PreviousHiZView]);
        const Pass = Encoder.beginComputePass({ label: "Virtualized geometry phase-2 disocclusion cull" });
        Pass.setPipeline(this.Phase2Program);
        Pass.setBindGroup(0, Group);
        Pass.dispatchWorkgroups(Math.ceil(this.RecordCount / 64));
        Pass.end();
        this.PreviousHiZView = this.CurrentHiZView ?? this.PreviousHiZView;
        ++this.FrameCount;
    }

    // Draws every record of one phase. Instance buffer slot carries the occupant identity, exactly like the
    // lossless cluster path, so SurfaceRaster.wgsl needs no changes.
    RecordDraws(Pass, Instances, Phase)
    {
        if (!this.Records) return;
        const Commands = Phase === 2 ? this.Phase2Commands : this.Phase1Commands;
        Pass.setVertexBuffer(0, this.VertexBuffer);
        Pass.setIndexBuffer(this.IndexBuffer, "uint32");
        for (const Draw of this.Draws)
        {
            Pass.setVertexBuffer(1, Instances, Draw.Occupant * 4, 4);
            Pass.drawIndexedIndirect(Commands, Draw.Record * CommandBytes);
        }
    }

    OwnsOccupant(Occupant) { return this.OwnedOccupants?.has(Occupant) ?? false; }
}
