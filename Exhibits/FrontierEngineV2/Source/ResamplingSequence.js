//============================================================================================================================================
//                                                          RESAMPLINGSEQUENCE.JS
//============================================================================================================================================
// 🧩 ReSTIR PT Enhanced unified path-space resampling pipeline (Lin, Kettunen & Wyman 2026, PACMCGIT 9(1), Art. 13, doi:10.1145/3804494):
//    ① ResampleTemporal      : 1-spp unified diffuse + coated-GGX path tree + §6.4 Dual Motion Vectors + §5 adaptive c_Cap temporal GRIS
//    ② EvaluatePairedShifts  : §3 Pass 1 coupled shift X_{B_i} -> A via 254²/230²/210² Gaussian self-inverting maps + §6.2.2 replay queue
//    ③ ResampleSpatial       : §3 Pass 2 zero-trace pairwise-MIS GRIS merge + §6.3 marginalized RGB vector shading (Irradiance + Reflection)
//    ④ ComputeDuplicationMap : §5 24×24 shared-memory tile computing 17×17 PathSeed duplication D ∈ [0, 1] + Inspector diagnostics
//    ⑤ TraverseReflection    : retained ONLY for SDF sphere-tracing mode or 2022 baseline comparison when EnhancedUnifiedDiGi is off

import { PairingSpace } from "./PairingSpace.js?v=restir-pt-enhanced-2";

export const ReservoirStride   = 64;   // [B] - 16 × 4 B unified ReservoirRecord (§6.2.1 / §7.1)
export const PairedLayerBudget = 3;    // [-] - N = 3 coupled Gaussian pairing maps (254², 230², 210²)
export const ShiftSlotBytes    = 16;   // [B] - 1 × vec4f per (layer, pixel) in PairedShiftExchange (50% less VRAM bandwidth)

export class ResamplingSequence
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Pairing  = new PairingSpace(Exchange);
        this.Pairing.UpdateRadius(18.0);
    }

    // 📝 Updates the self-inverting Gaussian pairing maps if SpatialRadius changed, and uploads per-frame dihedral D_4 symmetries + offsets.
    PrepareFrame(FrameIndex, SpatialRadius)
    {
        this.Pairing.UpdateRadius(SpatialRadius);
        this.Pairing.EncodeFrameTransforms(FrameIndex);
    }

    async ConstructPrograms(FrameRecords, MaterialRecords, DistanceLayout, SamplingLayout, TraceLayout)
    {
        this.FrameRecords    = FrameRecords;
        this.MaterialRecords = MaterialRecords;
        const Compute        = GPUShaderStage.COMPUTE;

        this.TemporalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "TextureDepth2D", "Texture2D", "Texture2D", "StorageTexture2D:rgba16float", "StorageRead", "Storage"],
            Compute, "ReSTIR PT Enhanced temporal");

        this.PairedShiftLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Uniform", "Texture2D", "TextureDepth2D", "TextureUint2DLayered", "StorageRead", "Storage"],
            Compute, "ReSTIR PT Enhanced paired shifts");

        this.SpatialLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Uniform", "Texture2D", "TextureDepth2D", "Texture2D", "TextureUint2DLayered", "StorageRead", "StorageRead",
             "StorageTexture2D:rgba16float", "StorageTexture2D:rgba32uint"],
            Compute, "ReSTIR PT Enhanced spatial");

        this.DuplicationLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "TextureUint2D", "Storage", "StorageTexture2D:rgba16float"],
            Compute, "ReSTIR PT Enhanced duplication map");

        this.ReflectionLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Texture2D", "TextureDepth2D", "StorageTexture2D:rgba16float"],
            Compute, "Reflection traversal");

        const SharedLayouts = [DistanceLayout, SamplingLayout, TraceLayout];
        this.TemporalProgram    = await this.Shaders.ConstructComputeProgram("ReservoirTemporal.wgsl", "ResampleTemporal",
                                                                             [this.TemporalLayout, ...SharedLayouts]);
        this.PairedShiftProgram = await this.Shaders.ConstructComputeProgram("ReservoirPairedShift.wgsl", "EvaluatePairedShifts",
                                                                             [this.PairedShiftLayout]);
        this.SpatialProgram     = await this.Shaders.ConstructComputeProgram("ReservoirSpatial.wgsl", "ResampleSpatial",
                                                                             [this.SpatialLayout, ...SharedLayouts]);
        this.DuplicationProgram = await this.Shaders.ConstructComputeProgram("ReservoirDuplication.wgsl", "ComputeDuplicationMap",
                                                                             [this.DuplicationLayout]);
        this.ReflectionProgram  = await this.Shaders.ConstructComputeProgram("ReflectionTraversal.wgsl", "TraverseReflection",
                                                                             [this.ReflectionLayout, ...SharedLayouts]);
    }

    ConstructTargets(TraceExtent, Surface)
    {
        for (const Resource of this.Resources || [])
            Resource.destroy();

        const [Width, Height] = TraceExtent;
        const PixelCount      = Width * Height;
        const Usage           = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        const Construct       = (Label, Format = "rgba16float") =>
            this.Exchange.ConstructTexture({ label: Label, size: TraceExtent, format: Format, usage: Usage });

        const Guides                = [Construct("Resampling guide A"), Construct("Resampling guide B")];
        const Irradiance            = Construct("Resampled irradiance");
        const Reflection            = Construct("Reflection radiance");
        const PostSpatialMeta       = Construct("Post-spatial PathSeed & metadata", "rgba32uint");
        const DuplicationDiagnostic = Construct("Sample duplication & reconnect diagnostic");

        const Reservoirs = [0, 1].map((Slot) =>
            this.Exchange.ConstructRecords(PixelCount * ReservoirStride, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, `Reservoirs ${Slot}`)
        );
        const PairedShiftExchange = this.Exchange.ConstructRecords(
            PixelCount * PairedLayerBudget * ShiftSlotBytes,
            GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
            "Paired shift exchange"
        );

        this.Resources   = [...Guides, Irradiance, Reflection, PostSpatialMeta, DuplicationDiagnostic, ...Reservoirs, PairedShiftExchange];
        this.TraceExtent = TraceExtent;
        this.Reservoirs  = Reservoirs;
        this.Views = {
            Guides:                Guides.map((Texture) => Texture.createView()),
            Irradiance:            Irradiance.createView(),
            Reflection:            Reflection.createView(),
            PostSpatialMeta:       PostSpatialMeta.createView(),
            DuplicationDiagnostic: DuplicationDiagnostic.createView()
        };

        this.TemporalGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.TemporalLayout,
            [this.FrameRecords, Surface.Views.Attributes, Surface.Views.Depth, Surface.Views.Motion, this.Views.Guides[1 - Parity],
             this.Views.Guides[Parity], Reservoirs[1 - Parity], Reservoirs[Parity]]));

        this.PairedShiftGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.PairedShiftLayout,
            [this.FrameRecords, this.Pairing.UniformBuffer, this.Views.Guides[Parity], Surface.Views.Depth,
             this.Pairing.ArrayView, Reservoirs[Parity], PairedShiftExchange]));

        this.SpatialGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.SpatialLayout,
            [this.FrameRecords, this.Pairing.UniformBuffer, this.Views.Guides[Parity], Surface.Views.Depth, Surface.Views.Attributes,
             this.Pairing.ArrayView, Reservoirs[Parity], PairedShiftExchange, this.Views.Irradiance, this.Views.PostSpatialMeta]));

        this.DuplicationGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.DuplicationLayout,
            [this.FrameRecords, this.Views.PostSpatialMeta, Reservoirs[Parity], this.Views.DuplicationDiagnostic]));

        this.ReflectionGroup = this.Exchange.ConstructDescriptorGroup(this.ReflectionLayout,
            [this.FrameRecords, this.MaterialRecords, Surface.Views.Attributes, Surface.Views.Depth, this.Views.Reflection]);
    }

    RecordResampling(Encoder, Parity, DistanceGroup, SamplingGroup, TraceGroup, TimestampWrites)
    {
        const [Width, Height] = this.TraceExtent;
        const GroupsX = Math.ceil(Width / 8);
        const GroupsY = Math.ceil(Height / 8);
        const Pass    = Encoder.beginComputePass({ label: "ReSTIR PT Enhanced", timestampWrites: TimestampWrites });

        Pass.setBindGroup(1, DistanceGroup);
        Pass.setBindGroup(2, SamplingGroup);
        Pass.setBindGroup(3, TraceGroup);

        // ① Initial unified path-tree candidate + Dual-MV + Adaptive c_Cap Temporal GRIS
        Pass.setPipeline(this.TemporalProgram);
        Pass.setBindGroup(0, this.TemporalGroups[Parity]);
        Pass.dispatchWorkgroups(GroupsX, GroupsY);

        // ② §3 Pass 1: Zero-ray analytic coupled Gaussian paired shifts
        Pass.setPipeline(this.PairedShiftProgram);
        Pass.setBindGroup(0, this.PairedShiftGroups[Parity]);
        Pass.dispatchWorkgroups(GroupsX, GroupsY);

        // ③ §3 Pass 2: Pairwise-MIS GRIS merge + §6.3 marginalized RGB vector shading + <= 1 post-merge visibility ray
        Pass.setPipeline(this.SpatialProgram);
        Pass.setBindGroup(0, this.SpatialGroups[Parity]);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.setBindGroup(2, SamplingGroup);
        Pass.setBindGroup(3, TraceGroup);
        Pass.dispatchWorkgroups(GroupsX, GroupsY);

        // ④ §5 17×17 Sample Duplication Map + Inspector diagnostics
        Pass.setPipeline(this.DuplicationProgram);
        Pass.setBindGroup(0, this.DuplicationGroups[Parity]);
        Pass.dispatchWorkgroups(GroupsX, GroupsY);

        Pass.end();
    }

    RecordReflection(Encoder, DistanceGroup, SamplingGroup, TraceGroup, TimestampWrites)
    {
        const [Width, Height] = this.TraceExtent;
        const Pass = Encoder.beginComputePass({ label: "Reflection traversal fallback", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.ReflectionProgram);
        Pass.setBindGroup(0, this.ReflectionGroup);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.setBindGroup(2, SamplingGroup);
        Pass.setBindGroup(3, TraceGroup);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.end();
    }
}
