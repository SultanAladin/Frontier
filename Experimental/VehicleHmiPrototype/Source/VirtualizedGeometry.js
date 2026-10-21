import { BuildGeometryHierarchy } from "./GeometryHierarchy.js";
import { PackHierarchyPages, PageSlotAllocator } from "./GeometryHierarchyRuntime.js";
//============================================================================================================================================
//                                                         VIRTUALIZEDGEOMETRY.JS
//============================================================================================================================================
// 🧩 GPU runtime for the hierarchy build. Phase 2: every DAG cluster of every eligible facet set becomes a
//    record the parallel cull program (Shader/VirtualizedGeometryCull.wgsl) cuts by projected error and
//    occlusion-tests in two phases against a Reversed-Z Hi-Z pyramid (Shader/HiZPyramid.wgsl).
//    Phase 3: cluster geometry streams through fixed arena slots by hierarchy page (pinned root pages first,
//    LRU eviction; non-resident clusters draw nothing and the cut falls back to resident ancestors), and a
//    visibility raster + deferred material resolve (Shader/VisibilityRaster.wgsl, Shader/MaterialResolve.wgsl)
//    produces OpaqueResolved/FinalResolved textures, with glass depth-peeled into two layers for the
//    two-layer refraction gather. Inert unless the page is opened with ?hierarchy=1.

const RecordFloats = 24;   // 96 B HierarchyClusterRecord (see VirtualizedGeometryCull.wgsl)
const CommandBytes = 20;   // IndirectDrawCommand
const ResidentFlag = 8;

function FaceNormal(Positions, A, B, C)
{
    const AB = [Positions[B * 3] - Positions[A * 3], Positions[B * 3 + 1] - Positions[A * 3 + 1], Positions[B * 3 + 2] - Positions[A * 3 + 2]];
    const AC = [Positions[C * 3] - Positions[A * 3], Positions[C * 3 + 1] - Positions[A * 3 + 1], Positions[C * 3 + 2] - Positions[A * 3 + 2]];
    const N = [AB[1] * AC[2] - AB[2] * AC[1], AB[2] * AC[0] - AB[0] * AC[2], AB[0] * AC[1] - AB[1] * AC[0]];
    const L = Math.hypot(N[0], N[1], N[2]) || 1;
    return [N[0] / L, N[1] / L, N[2] / L];
}
function FamilyFloat(Family)
{
    const View = new DataView(new ArrayBuffer(4));
    View.setUint32(0, Family, true);
    return View.getFloat32(0, true);
}

export class VirtualizedGeometry
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders = Shaders;
        this.Cache = new Map();
        const Query = new URLSearchParams(location.search);
        this.Enabled = Query.get("hierarchy") === "1";
        this.StreamSlots = Number.parseInt(Query.get("streampages") ?? "", 10);
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
        this.DummyHiZ = E.ConstructTexture({ label: "Hi-Z dummy", size: [1, 1], format: "r32float",
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING });
        this.PreviousHiZView = this.DummyHiZ.createView();

        // Phase-3 programs: visibility raster (opaque/glass pipelines share one entry; the glass-back peel
        // entry samples the front glass depth) and the two deferred resolve entries.
        this.VisibilityLayout = E.ConstructDescriptorLayout(["Uniform", "StorageRead", "Texture2D", "Sampler", "Uniform", "StorageRead"],
            GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Visibility raster");
        this.GlassBackLayout = E.ConstructDescriptorLayout(["TextureDepth2D"], GPUShaderStage.FRAGMENT, "Glass back depth");
        const VertexLayouts = [{ arrayStride: 32, attributes: [
            { shaderLocation: 0, offset: 0, format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "uint32" },
            { shaderLocation: 2, offset: 16, format: "float32x3" },
            { shaderLocation: 3, offset: 28, format: "uint32" }] },
            { arrayStride: 4, stepMode: "instance", attributes: [{ shaderLocation: 4, offset: 0, format: "uint32" }] }];
        const VisTargets = [{ format: "rg32uint" }, { format: "rg16float" }, { format: "rg32uint" }, { format: "rg16float" }];
        this.VisibilityOpaqueProgram = await this.Shaders.ConstructRasterProgram("VisibilityRaster.wgsl", {
            VertexEntry: "VisibilityVertex", FragmentEntry: "VisibilityFragment", VertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "greater" }, Targets: VisTargets
        }, [this.VisibilityLayout]);
        this.VisibilityGlassProgram = await this.Shaders.ConstructRasterProgram("VisibilityRaster.wgsl", {
            VertexEntry: "VisibilityVertex", FragmentEntry: "VisibilityFragment", VertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater" }, Targets: VisTargets
        }, [this.VisibilityLayout]);
        this.VisibilityGlassBackProgram = await this.Shaders.ConstructRasterProgram("VisibilityRaster.wgsl", {
            VertexEntry: "VisibilityVertex", FragmentEntry: "VisibilityGlassBackFragment", VertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "greater" },
            Targets: [{ format: "rg32uint" }, { format: "rg16float" }]
        }, [this.VisibilityLayout, this.GlassBackLayout]);
        this.ResolveLayout = E.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "StorageRead", "StorageRead", "TextureUint2D", "Texture2D", "StorageTexture2D:rgba16float",
             "TextureUint2D", "Texture2D", "TextureUint2D", "Texture2D", "StorageTexture2D:rgba16float", "Texture2D", "Texture2DUnfiltered"],
            Compute, "Material resolve");
        this.ResolveOpaqueProgram = await this.Shaders.ConstructComputeProgram("MaterialResolve.wgsl", "ResolveOpaqueVisibility", [this.ResolveLayout]);
        this.ResolveRefractionProgram = await this.Shaders.ConstructComputeProgram("MaterialResolve.wgsl", "ResolveRefractionGather", [this.ResolveLayout]);
        this.DebugIdsProgram = await this.Shaders.ConstructComputeProgram("MaterialResolve.wgsl", "DebugVisibilityIds", [this.ResolveLayout]);
        this.DebugGlassProgram = await this.Shaders.ConstructComputeProgram("MaterialResolve.wgsl", "DebugGlassLayers", [this.ResolveLayout]);
        this.DebugCopyProgram = await this.Shaders.ConstructComputeProgram("MaterialResolve.wgsl", "DebugCopyResolved", [this.ResolveLayout]);
        this.DebugHiZProgram = await this.Shaders.ConstructComputeProgram("MaterialResolve.wgsl", "DebugHiZView", [this.ResolveLayout]);
    }

    ConstructTargets(Width, Height)
    {
        if (!this.Enabled) return;
        for (const Texture of this.HiZTextures ?? []) Texture.destroy();
        for (const Texture of this.VisTextures ?? []) Texture.destroy();
        this.HiZTextures = []; this.HiZViews = [];
        let W = Width, H = Height;
        while (true)
        {
            const Texture = this.Exchange.ConstructTexture({ label: `Hi-Z mip ${this.HiZTextures.length}`, size: [W, H], format: "r32float",
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING });
            this.HiZTextures.push(Texture); this.HiZViews.push(Texture.createView());
            if (W === 1 && H === 1) break;
            W = Math.max(1, W >> 1); H = Math.max(1, H >> 1);
        }
        this.PreviousHiZView = this.DummyHiZ.createView();
        this.Extent = [Width, Height];
        // Phase-3 targets: visibility ids/barycentrics per layer plus resolved colour outputs.
        const E = this.Exchange;
        const Make = (Label, Format, Usage) => E.ConstructTexture({ label: Label, size: [Width, Height], format: Format, usage: Usage });
        const VisUsage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING;
        this.OpaqueVisTexture = Make("Opaque visibility", "rg32uint", VisUsage);
        this.OpaqueBaryTexture = Make("Opaque barycentrics", "rg16float", VisUsage);
        this.GlassVisTexture = Make("Glass visibility", "rg32uint", VisUsage);
        this.GlassBaryTexture = Make("Glass barycentrics", "rg16float", VisUsage);
        this.GlassBackVisTexture = Make("Glass back visibility", "rg32uint", VisUsage);
        this.GlassBackBaryTexture = Make("Glass back barycentrics", "rg16float", VisUsage);
        this.VisDepthTexture = Make("Visibility depth", "depth32float", GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING);
        this.GlassBackDepthTexture = Make("Glass back depth", "depth32float", GPUTextureUsage.RENDER_ATTACHMENT);
        const ResolveUsage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        this.OpaqueResolvedTexture = Make("Opaque resolved", "rgba16float", ResolveUsage);
        this.FinalResolvedTexture = Make("Final resolved", "rgba16float", ResolveUsage);
        this.VisTextures = [this.OpaqueVisTexture, this.OpaqueBaryTexture, this.GlassVisTexture, this.GlassBaryTexture,
            this.GlassBackVisTexture, this.GlassBackBaryTexture, this.VisDepthTexture, this.GlassBackDepthTexture,
            this.OpaqueResolvedTexture, this.FinalResolvedTexture];
        this.ResolveView = this.FinalResolvedTexture.createView();
    }

    // Builds (once per facet-set revision) expanded per-corner geometry for one set's whole DAG.
    BuildSet(FacetSetIndex, Set)
    {
        const Cached = this.Cache.get(FacetSetIndex);
        if (Cached && Cached.FacetCount === Set.Facets.length) return Cached;
        const Hierarchy = BuildGeometryHierarchy(Set, {});
        const Vertices = [], Indices = [], CornerPos = [], CornerUv = [], Records = [];
        for (const Level of Hierarchy.Levels)
            for (const ClusterId of Level.Clusters)
            {
                const Node = Hierarchy.Clusters[ClusterId];
                const BaseVertex = Vertices.length / 8, FirstIndex = Indices.length;
                for (const Triangle of Node.Triangles)
                {
                    const Corner = [0, 1, 2].map((C) => Level.Triangles[Triangle * 3 + C]);
                    const Normal = FaceNormal(Level.Positions, Corner[0], Corner[1], Corner[2]);
                    const FamFloat = FamilyFloat(Level.Families[Triangle]);
                    Corner.forEach((Vertex, C) =>
                    {
                        Indices.push(Vertices.length / 8);
                        const P = [Level.Positions[Vertex * 3], Level.Positions[Vertex * 3 + 1], Level.Positions[Vertex * 3 + 2]];
                        Vertices.push(P[0], P[1], P[2], FamFloat, Normal[0], Normal[1], Normal[2], 0);
                        CornerPos.push(P[0], P[1], P[2], FamFloat);
                        CornerUv.push(Level.CornerUVs ? Level.CornerUVs[Triangle * 6 + C * 2] : 0,
                                      Level.CornerUVs ? Level.CornerUVs[Triangle * 6 + C * 2 + 1] : 0, 0, 0);
                    });
                }
                Records.push({ Node, ClusterId, FirstIndex, BaseVertex, IndexCount: Node.TriangleCount * 3,
                               VertexCount: Node.TriangleCount * 3 });
            }
        const Built = { FacetCount: Set.Facets.length, Hierarchy, Records,
                        Vertices: new Float32Array(Vertices), Indices: new Uint32Array(Indices),
                        CornerPos: new Float32Array(CornerPos), CornerUv: new Float32Array(CornerUv) };
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
        this.Draws = [];            // { Record, Occupant, Page, ClassMask }
        this.OwnedOccupants = new Set();
        this.Pages = [];            // global streaming pages across sets
        const RecordMirrors = [];
        this.PageLocalFirstIndices = [];

        for (const Run of Scene.DrawRuns)
        {
            const Set = Scene.FacetSets[Run.FacetSet];
            const Eligible = Set.Facets.length >= 256 && !Set.VertexCapacity &&
                Array.from({ length: Run.Count }, (_, I) => Scene.Occupants[Run.First + I]).every((O) => !(O.Roles & 8));
            if (!Eligible) continue;
            const Built = this.BuildSet(Run.FacetSet, Set);
            if (!Built) continue;
            const SetPages = PackHierarchyPages(Built.Hierarchy, {});
            const RecordByCluster = new Map(Built.Records.map((Record) => [Record.ClusterId, Record]));
            // Per-page payload: concatenated expanded vertices/indices/corner data of the page's clusters.
            for (const Page of SetPages)
            {
                const GlobalPage = this.Pages.length;
                const Vertices = [], Indices = [], CPos = [], CUv = [];
                const ClusterSlices = new Map();
                for (const ClusterId of Page.Clusters)
                {
                    const Record = RecordByCluster.get(ClusterId);
                    const LocalFirstIndex = Indices.length, LocalBaseVertex = Vertices.length / 8;
                    for (let I = 0; I < Record.VertexCount; ++I)
                    {
                        const Source = (Record.BaseVertex + I) * 8;
                        Vertices.push(...Built.Vertices.subarray(Source, Source + 8));
                        CPos.push(...Built.CornerPos.subarray((Record.BaseVertex + I) * 4, (Record.BaseVertex + I) * 4 + 4));
                        CUv.push(...Built.CornerUv.subarray((Record.BaseVertex + I) * 4, (Record.BaseVertex + I) * 4 + 4));
                    }
                    for (let I = 0; I < Record.IndexCount; ++I)
                        Indices.push(Built.Indices[Record.FirstIndex + I] - Record.BaseVertex + LocalBaseVertex);
                    ClusterSlices.set(ClusterId, { LocalFirstIndex });
                }
                this.Pages.push({ GlobalPage, Pinned: Page.Pinned, Built, ClusterSlices,
                                  Vertices: new Float32Array(Vertices), Indices: new Uint32Array(Indices),
                                  CornerPos: new Float32Array(CPos), CornerUv: new Float32Array(CUv),
                                  DrawRecords: [] });
                for (const ClusterId of Page.Clusters) Built.Hierarchy.Clusters[ClusterId].GlobalPage = GlobalPage;
            }
            for (let O = Run.First; O < Run.First + Run.Count; ++O)
            {
                this.OwnedOccupants.add(O);
                for (const Record of Built.Records)
                {
                    const Node = Record.Node;
                    const Page = this.Pages[Node.GlobalPage];
                    const Floats = new Float32Array(RecordFloats), U32 = new Uint32Array(Floats.buffer);
                    Floats.set(Node.TightMinimum ?? Node.Minimum, 0); U32[3] = O;
                    Floats.set(Node.TightMaximum ?? Node.Maximum, 4);
                    U32[7] = Page.ClusterSlices.get(Record.ClusterId).LocalFirstIndex;   // slot offset added on load
                    Floats.set([...Node.Sphere.Center, Node.Sphere.Radius], 8);
                    Floats.set([...(Node.ParentSphere?.Center ?? Node.Sphere.Center), Node.ParentSphere?.Radius ?? Node.Sphere.Radius], 12);
                    Floats.set([...Node.Cone.Axis, Node.Cone.Cutoff], 16);
                    Floats[20] = Node.Error;
                    Floats[21] = Number.isFinite(Node.ParentError) ? Node.ParentError : 1e30;
                    U32[22] = Record.IndexCount;
                    U32[23] = Node.ClassMask | (Node.Cone.Cutoff > -1 ? 4 : 0) | (Node.Level << 8);   // resident bit set on load
                    const DrawIndex = this.Draws.length;
                    this.Draws.push({ Record: DrawIndex, Occupant: O, Page: Node.GlobalPage, ClassMask: Node.ClassMask });
                    this.PageLocalFirstIndices[DrawIndex] = Page.ClusterSlices.get(Record.ClusterId).LocalFirstIndex;
                    Page.DrawRecords.push(DrawIndex);
                    RecordMirrors.push(Floats);
                }
            }
        }
        this.RecordCount = this.Draws.length;
        if (!this.RecordCount) { this.Records = null; return this; }

        // Arena slots: every page streams through SlotCount slots sized to the largest page payload.
        this.SlotVertexCount = Math.max(1, ...this.Pages.map((P) => P.Vertices.length / 8));
        this.SlotIndexCount = Math.max(1, ...this.Pages.map((P) => P.Indices.length));
        const SlotCount = Math.max(1, Math.min(this.StreamSlots > 0 ? this.StreamSlots : this.Pages.length, this.Pages.length));
        this.Allocator = new PageSlotAllocator(SlotCount);
        this.Pages.forEach((Page) => { if (Page.Pinned) this.Allocator.Pin(Page.GlobalPage); });
        this.StreamQueue = this.Pages.map((P) => P.GlobalPage);
        this.RecordMirrors = RecordMirrors;

        const E = this.Exchange;
        const Make = (Bytes, Usage, Label) =>
        {
            const Buffer = E.ConstructRecords(Math.max(4, Bytes), Usage | GPUBufferUsage.COPY_DST, Label);
            this.Buffers.push(Buffer);
            return Buffer;
        };
        const Flat = new Float32Array(this.RecordCount * RecordFloats);
        RecordMirrors.forEach((Floats, I) => Flat.set(Floats, I * RecordFloats));
        this.ClusterRecords = Make(Flat.byteLength, GPUBufferUsage.STORAGE, "Hierarchy cluster records");
        E.Device.queue.writeBuffer(this.ClusterRecords, 0, Flat);
        this.VertexBuffer = Make(this.SlotVertexCount * SlotCount * 32, GPUBufferUsage.VERTEX, "Hierarchy arena vertices");
        this.IndexBuffer = Make(this.SlotIndexCount * SlotCount * 4, GPUBufferUsage.INDEX, "Hierarchy arena indices");
        this.CornerPosBuffer = Make(this.SlotVertexCount * SlotCount * 16, GPUBufferUsage.STORAGE, "Hierarchy corner positions");
        this.CornerUvBuffer = Make(this.SlotVertexCount * SlotCount * 16, GPUBufferUsage.STORAGE, "Hierarchy corner UVs");
        const CommandUsage = GPUBufferUsage.STORAGE | GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_SRC;
        this.Phase1Commands = E.ConstructRecords(this.RecordCount * CommandBytes, CommandUsage, "Hierarchy phase-1 draws");
        this.Phase2Commands = E.ConstructRecords(this.RecordCount * CommandBytes, CommandUsage, "Hierarchy phase-2 draws");
        this.States = E.ConstructRecords(this.RecordCount * 4, GPUBufferUsage.STORAGE, "Hierarchy cluster states");
        this.Buffers.push(this.Phase1Commands, this.Phase2Commands, this.States);
        this.Records = true;
        this.RefreshGroups();
        return this;
    }

    // 📝 Streams up to MaxLoads queued pages into arena slots, patching cluster records (FirstIndex + resident
    //    bit) and clearing records of any evicted page so the cut falls back to resident ancestors.
    ServiceStreaming(MaxLoads = 2)
    {
        if (!this.Records || !this.StreamQueue?.length) return;
        const Queue = this.Exchange.Device.queue;
        let Loads = 0;
        while (Loads < MaxLoads && this.StreamQueue.length)
        {
            const PageIndex = this.StreamQueue[0];
            if (this.Allocator.IsResident(PageIndex)) { this.StreamQueue.shift(); continue; }
            const Result = this.Allocator.Ensure(PageIndex, this.FrameCount);
            if (Result.Slot < 0) break;   // every slot pinned: wait for eviction pressure to change
            this.StreamQueue.shift();
            const Page = this.Pages[PageIndex];
            if (Result.EvictedPage !== null && Result.EvictedPage !== undefined && Result.EvictedPage >= 0)
                for (const DrawIndex of this.Pages[Result.EvictedPage].DrawRecords)
                {
                    const U32 = new Uint32Array(this.RecordMirrors[DrawIndex].buffer);
                    U32[23] &= ~ResidentFlag;
                    Queue.writeBuffer(this.ClusterRecords, DrawIndex * RecordFloats * 4, this.RecordMirrors[DrawIndex]);
                }
            const VertexBase = Result.Slot * this.SlotVertexCount, IndexBase = Result.Slot * this.SlotIndexCount;
            Queue.writeBuffer(this.VertexBuffer, VertexBase * 32, Page.Vertices);
            Queue.writeBuffer(this.CornerPosBuffer, VertexBase * 16, Page.CornerPos);
            Queue.writeBuffer(this.CornerUvBuffer, VertexBase * 16, Page.CornerUv);
            const Adjusted = new Uint32Array(Page.Indices.length);
            for (let I = 0; I < Page.Indices.length; ++I) Adjusted[I] = Page.Indices[I] + VertexBase;
            Queue.writeBuffer(this.IndexBuffer, IndexBase * 4, Adjusted);
            for (const DrawIndex of Page.DrawRecords)
            {
                const U32 = new Uint32Array(this.RecordMirrors[DrawIndex].buffer);
                // FirstIndex was stored page-local at build time; add the slot's index base and set resident.
                U32[7] = IndexBase + this.PageLocalFirstIndices[DrawIndex];
                U32[23] |= ResidentFlag;
                Queue.writeBuffer(this.ClusterRecords, DrawIndex * RecordFloats * 4, this.RecordMirrors[DrawIndex]);
            }
            ++Loads;
        }
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
        this.ServiceStreaming(2);
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

    // 📝 Phase-3 visibility raster + deferred resolve. Reuses both phases' indirect commands: opaque records
    //    write visibility + depth, transmissive records depth-test without writing, then a peel pass captures
    //    the glass back layer and two compute entries resolve material and refraction. Outputs land in
    //    OpaqueResolvedTexture / FinalResolvedTexture (this.ResolveView) for the display chain to consume.
    RecordVisibilityResolve(Encoder, RasterGroup, Instances)
    {
        if (!this.Records || !this.OpaqueVisTexture) return;
        const E = this.Exchange;
        const DrawClass = (Pass, Commands, ClassMaskWanted) =>
        {
            Pass.setVertexBuffer(0, this.VertexBuffer);
            Pass.setIndexBuffer(this.IndexBuffer, "uint32");
            for (const Draw of this.Draws)
            {
                if (((Draw.ClassMask & 2) !== 0) !== ClassMaskWanted) continue;
                Pass.setVertexBuffer(1, Instances, Draw.Occupant * 4, 4);
                Pass.drawIndexedIndirect(Commands, Draw.Record * CommandBytes);
            }
        };
        const Attachments = (Views, DepthView) => ({
            colorAttachments: Views.map((view) => ({ view, clearValue: [0, 0, 0, 0], loadOp: "clear", storeOp: "store" })),
            depthStencilAttachment: { view: DepthView, depthClearValue: 0.0, depthLoadOp: "clear", depthStoreOp: "store" }
        });
        const VisPass = Encoder.beginRenderPass({ label: "Visibility raster (opaque + glass front)", ...Attachments(
            [this.OpaqueVisTexture.createView(), this.OpaqueBaryTexture.createView(),
             this.GlassVisTexture.createView(), this.GlassBaryTexture.createView()], this.VisDepthTexture.createView()) });
        VisPass.setBindGroup(0, RasterGroup);
        VisPass.setPipeline(this.VisibilityOpaqueProgram);
        DrawClass(VisPass, this.Phase1Commands, false);
        DrawClass(VisPass, this.Phase2Commands, false);
        VisPass.setPipeline(this.VisibilityGlassProgram);
        DrawClass(VisPass, this.Phase1Commands, true);
        DrawClass(VisPass, this.Phase2Commands, true);
        VisPass.end();
        const Peel = Encoder.beginRenderPass({ label: "Visibility raster (glass back peel)", ...Attachments(
            [this.GlassBackVisTexture.createView(), this.GlassBackBaryTexture.createView()], this.GlassBackDepthTexture.createView()) });
        Peel.setBindGroup(0, RasterGroup);
        Peel.setBindGroup(1, E.ConstructDescriptorGroup(this.GlassBackLayout, [this.VisDepthTexture.createView()]));
        Peel.setPipeline(this.VisibilityGlassBackProgram);
        DrawClass(Peel, this.Phase1Commands, true);
        DrawClass(Peel, this.Phase2Commands, true);
        Peel.end();
        const ResolveGroup = E.ConstructDescriptorGroup(this.ResolveLayout,
            [this.FrameRecords, this.OccupantRecords, this.CornerPosBuffer, this.CornerUvBuffer,
             this.OpaqueVisTexture.createView(), this.OpaqueBaryTexture.createView(), this.OpaqueResolvedTexture.createView(),
             this.GlassVisTexture.createView(), this.GlassBaryTexture.createView(),
             this.GlassBackVisTexture.createView(), this.GlassBackBaryTexture.createView(),
             this.FinalResolvedTexture.createView(), this.OpaqueResolvedTexture.createView(),
             this.CurrentHiZView ?? this.PreviousHiZView]);
        for (const Program of [this.ResolveOpaqueProgram, this.ResolveRefractionProgram])
        {
            const Pass = Encoder.beginComputePass({ label: Program === this.ResolveOpaqueProgram ? "Resolve opaque visibility" : "Resolve refraction gather" });
            Pass.setPipeline(Program);
            Pass.setBindGroup(0, ResolveGroup);
            Pass.dispatchWorkgroups(Math.ceil(this.Extent[0] / 8), Math.ceil(this.Extent[1] / 8));
            Pass.end();
        }
    }

    // 📝 Display-inspection debug views (Display list modes 10-13): writes the chosen cluster debug image
    //    into the display inspection target, which DisplayRecombination presents un-tonemapped like the
    //    other inspection modes. Mode 11 copies the deferred resolve output itself.
    RecordDebugView(Encoder, TargetView, Mode)
    {
        if (!this.Records || !this.OpaqueVisTexture || !TargetView) return;
        const E = this.Exchange;
        const Program = Mode === 10 ? this.DebugIdsProgram : Mode === 13 ? this.DebugGlassProgram
                      : Mode === 12 ? this.DebugHiZProgram : this.DebugCopyProgram;
        if (!Program) return;
        const Group = E.ConstructDescriptorGroup(this.ResolveLayout,
            [this.FrameRecords, this.OccupantRecords, this.CornerPosBuffer, this.CornerUvBuffer,
             this.OpaqueVisTexture.createView(), this.OpaqueBaryTexture.createView(), this.OpaqueResolvedTexture.createView(),
             this.GlassVisTexture.createView(), this.GlassBaryTexture.createView(),
             this.GlassBackVisTexture.createView(), this.GlassBackBaryTexture.createView(),
             TargetView, Mode === 11 ? this.FinalResolvedTexture.createView() : this.OpaqueResolvedTexture.createView(),
             this.CurrentHiZView ?? this.PreviousHiZView]);
        const Pass = Encoder.beginComputePass({ label: "Cluster debug view " + Mode });
        Pass.setPipeline(Program);
        Pass.setBindGroup(0, Group);
        Pass.dispatchWorkgroups(Math.ceil(this.Extent[0] / 8), Math.ceil(this.Extent[1] / 8));
        Pass.end();
    }

    OwnsOccupant(Occupant) { return this.OwnedOccupants?.has(Occupant) ?? false; }
}
