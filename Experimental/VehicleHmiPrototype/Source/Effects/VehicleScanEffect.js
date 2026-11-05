//============================================================================================================================================
//                                                          VEHICLESCANEFFECT.JS
//============================================================================================================================================
// 🧩 1:1 Automotive X-Ray Scan, Make-It-Disappear Dissolve, Fresnel Hologram Ghost, 14°/28° Dihedral Feature Wireframe,
//    Dual Wavefront Curtain Planes, and 130 Floating Holographic Sparkles.
//
// 📝 Supports all four inspection modes used across the configurator and test hooks:
//      0 = Solid       (ramp -> 0)
//      1 = Ghost       (full-vehicle Fresnel hologram + dihedral wireframe, solid shell dissolved)
//      2 = Wireframe   (full-vehicle 14°/28° dihedral wireframe, solid shell dissolved)
//      3 = Scan        (6.6 s cycle: 4.6 s ease-in-out cubic nose->tail sweep [3.25 -> -3.35 m], 1.0 s hold, 1.0 s restore,
//                       with dual wavefront curtain planes and 130 #8fd8f2 floating sparkles)

import { VertexStride } from "../FacetStructure.js";
import { BuildVehicleEdgeBuffer } from "./DihedralEdges.js";
import { BuildScanSparkleBuffer, DefaultSparkleColour, DefaultSparkleOpacity } from "./ScanSparkles.js";

export const VehicleViewMode = Object.freeze({
    Solid:     0,
    Ghost:     1,
    Wireframe: 2,
    Scan:      3
});

export const ScanCycleSeconds = 6.6;
export const ScanSweepSeconds = 4.6;
export const ScanHoldSeconds  = 1.0;
export const ScanStartX       = 3.25;
export const ScanEndX         = -3.35;
export const ScanDampLambda   = 3.0;

// Linear sRGB conversions of #a9dcec, #38bdf8, and #8fd8f2 from reference Truck.tsx.
const GhostBaseLinear   = Object.freeze([0.3968, 0.7157, 0.8400]); // #a9dcec
const GhostAccentLinear = Object.freeze([0.0395, 0.5089, 0.9387]); // #38bdf8
const ScanEdgeLinear    = DefaultSparkleColour;                    // #8fd8f2

const ScanUniformBytes = 96; // 24 floats (6 × vec4f)

export function EaseInOutCubic(Progress)
{
    const X = Math.max(0.0, Math.min(1.0, Progress));
    return X < 0.5 ? 4.0 * X * X * X : 1.0 - Math.pow(-2.0 * X + 2.0, 3.0) * 0.5;
}

export function DampScalar(Current, Target, Lambda, Seconds)
{
    const Step = Math.max(0.0, Math.min(0.1, Number(Seconds) || 0.0));
    return Current + (Target - Current) * (1.0 - Math.exp(-Lambda * Step));
}

export class VehicleScanController
{
    constructor()
    {
        this.Mode          = VehicleViewMode.Solid;
        this.PriorMode     = VehicleViewMode.Solid;
        this.ActiveMode    = VehicleViewMode.Scan;
        this.Clock         = 0.0;
        this.SweepTime     = 0.0;
        this.Ramp          = 0.0;
        this.ScanX         = 3.30;
        this.HoloAmount    = 0.0;
        this.CycleAmount   = 1.0;
        this.Sweeping      = false;
        this.CurtainAlpha0 = 0.0;
        this.CurtainAlpha1 = 0.0;
        this.EdgeOpacity   = 0.0;
        this.SparkleAlpha  = 0.0;
    }

    Advance(Seconds, RequestedMode)
    {
        const Mode = Math.max(0, Math.min(3, Number(RequestedMode) | 0));
        if (Mode !== this.PriorMode)
        {
            if (Mode === VehicleViewMode.Scan)
                this.SweepTime = 0.0;
            if (Mode !== VehicleViewMode.Solid)
                this.ActiveMode = Mode;
            this.PriorMode = Mode;
        }
        this.Mode = Mode;

        const Step = Math.max(0.0, Math.min(0.05, Number(Seconds) || 0.0));
        this.Clock += Step;

        const Target = Mode !== VehicleViewMode.Solid ? 1.0 : 0.0;
        this.Ramp = DampScalar(this.Ramp, Target, ScanDampLambda, Step);
        if (Math.abs(this.Ramp - Target) < 0.002)
            this.Ramp = Target;

        const EffectiveMode = Mode !== VehicleViewMode.Solid ? Mode : this.ActiveMode;
        let CycleAmount = 1.0;
        let Sweeping = false;

        if (EffectiveMode === VehicleViewMode.Scan)
        {
            if (Mode === VehicleViewMode.Scan || this.Ramp > 0.002)
                this.SweepTime += Step;
            const Phase = this.SweepTime % ScanCycleSeconds;
            const SweepProgress = Phase < ScanSweepSeconds ? EaseInOutCubic(Phase / ScanSweepSeconds) : 1.0;
            Sweeping = Phase < ScanSweepSeconds;
            this.ScanX = ScanStartX + (ScanEndX - ScanStartX) * SweepProgress;
            CycleAmount = Phase < (ScanSweepSeconds + ScanHoldSeconds)
                        ? 1.0
                        : Math.max(0.0, 1.0 - (Phase - ScanSweepSeconds - ScanHoldSeconds) / (ScanCycleSeconds - ScanSweepSeconds - ScanHoldSeconds));
        }
        else
        {
            // Full-body Ghost or Wireframe: scan front sits well behind the rear bumper so the entire car is revealed.
            this.ScanX = -99.0;
            CycleAmount = 1.0;
            Sweeping = false;
        }

        this.CycleAmount   = CycleAmount;
        this.Sweeping      = Sweeping;
        this.HoloAmount    = this.Ramp * (EffectiveMode === VehicleViewMode.Scan ? CycleAmount : 1.0);
        this.EdgeOpacity   = EffectiveMode === VehicleViewMode.Ghost || EffectiveMode === VehicleViewMode.Wireframe || EffectiveMode === VehicleViewMode.Scan
                           ? (EffectiveMode === VehicleViewMode.Wireframe ? 0.82 : 0.55) * this.HoloAmount
                           : 0.0;
        const CurtainVis   = EffectiveMode === VehicleViewMode.Scan ? this.Ramp * CycleAmount * (Sweeping ? 1.0 : 0.25) : 0.0;
        this.CurtainAlpha0 = 0.075 * CurtainVis;
        this.CurtainAlpha1 = 0.300 * CurtainVis;
        this.SparkleAlpha  = EffectiveMode === VehicleViewMode.Scan ? DefaultSparkleOpacity * this.HoloAmount : 0.0;
    }

    get Active()
    {
        return this.HoloAmount > 0.002;
    }

    get EffectiveMode()
    {
        return this.Mode !== VehicleViewMode.Solid ? this.Mode : this.ActiveMode;
    }

    EncodeUniformScalars()
    {
        const Scalars = new Float32Array(ScanUniformBytes / 4);
        // vec4 0: ScanX, HoloAmount, Clock, EffectiveMode
        Scalars[0]  = this.ScanX;
        Scalars[1]  = this.HoloAmount;
        Scalars[2]  = this.Clock;
        Scalars[3]  = this.EffectiveMode;
        // vec4 1: EdgeOpacity, CurtainAlpha0, CurtainAlpha1, SparkleAlpha
        Scalars[4]  = this.EdgeOpacity;
        Scalars[5]  = this.CurtainAlpha0;
        Scalars[6]  = this.CurtainAlpha1;
        Scalars[7]  = this.SparkleAlpha;
        // vec4 2: GhostBaseLinear.rgb, ClipOffset (0.05 m)
        Scalars[8]  = GhostBaseLinear[0];
        Scalars[9]  = GhostBaseLinear[1];
        Scalars[10] = GhostBaseLinear[2];
        Scalars[11] = 0.05;
        // vec4 3: GhostAccentLinear.rgb, LineWidthPixels
        Scalars[12] = GhostAccentLinear[0];
        Scalars[13] = GhostAccentLinear[1];
        Scalars[14] = GhostAccentLinear[2];
        Scalars[15] = 1.45;
        // vec4 4: ScanEdgeLinear.rgb, CurtainCentreZ
        Scalars[16] = ScanEdgeLinear[0];
        Scalars[17] = ScanEdgeLinear[1];
        Scalars[18] = ScanEdgeLinear[2];
        Scalars[19] = 0.62;
        // vec4 5: Curtain0 (width 2.4, height 2.5), Curtain1 (width 0.55, height 2.5)
        Scalars[20] = 2.40;
        Scalars[21] = 2.50;
        Scalars[22] = 0.55;
        Scalars[23] = 2.50;
        return Scalars;
    }
}

export class VehicleScanEffect
{
    constructor(Exchange, Shaders)
    {
        this.Exchange  = Exchange;
        this.Shaders   = Shaders;
        this.MainState = new VehicleScanController();
        this.HmiState  = new VehicleScanController();
    }

    async ConstructResources({ Scene, OccupantRecords, MaterialRecords, VehicleRecords, MainFrameRecords, HmiFrameRecords = null })
    {
        const Exchange = this.Exchange;
        this.Scene = Scene;

        const EdgeData = BuildVehicleEdgeBuffer(Scene);
        this.EdgeCount = EdgeData.EdgeCount;
        this.EdgeBuffer = Exchange.ConstructRecordsFrom(EdgeData.Scalars, GPUBufferUsage.STORAGE, "Vehicle dihedral edges");

        const SparkleData = BuildScanSparkleBuffer();
        this.SparkleCount = SparkleData.Count;
        this.SparkleBuffer = Exchange.ConstructRecordsFrom(SparkleData.Scalars, GPUBufferUsage.STORAGE, "Vehicle scan sparkles");

        this.MainUniform = Exchange.ConstructRecords(ScanUniformBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Main vehicle scan uniform");
        this.HmiUniform  = Exchange.ConstructRecords(ScanUniformBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "HMI vehicle scan uniform");

        // Precompute hero vehicle draw runs (Occupants 0..4 only) so Ghost & Transition passes draw only the 1 hero car.
        this.VehicleDrawRuns = [];
        const BodyOccupant = Scene.VehicleOccupants.Body;
        this.VehicleDrawRuns.push({ FacetSet: Scene.Occupants[BodyOccupant].FacetSet, First: BodyOccupant, Count: 1 });
        const FirstWheel = Scene.VehicleOccupants.Wheels[0];
        this.VehicleDrawRuns.push({ FacetSet: Scene.Occupants[FirstWheel].FacetSet, First: FirstWheel, Count: 4 });

        const Stages = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
        this.Layout = Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "StorageRead", "StorageRead", "Uniform", "StorageRead", "StorageRead"],
            Stages, "Vehicle scan effect");

        this.MainGroup = Exchange.ConstructDescriptorGroup(this.Layout, [
            MainFrameRecords, OccupantRecords, MaterialRecords, VehicleRecords, this.MainUniform, this.EdgeBuffer, this.SparkleBuffer]);
        if (HmiFrameRecords)
            this.AttachHmiFrameRecords(HmiFrameRecords, OccupantRecords, MaterialRecords, VehicleRecords);

        const MeshVertexLayouts = [{ arrayStride: VertexStride, attributes: [
            { shaderLocation: 0, offset: 0,  format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "uint32" },
            { shaderLocation: 2, offset: 16, format: "float32x3" },
            { shaderLocation: 3, offset: 28, format: "uint32" }] }];

        const AlphaBlend = {
            color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
            alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" }
        };
        const AdditiveBlend = {
            color: { srcFactor: "src-alpha", dstFactor: "one", operation: "add" },
            alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" }
        };
        const ReadDepth = { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater-equal" };

        // ① Smooth semi-transparent solid shell across the [-0.9, -0.05] dissolve band where primary G-buffer yielded to floor.
        this.TransitionProgram = await this.Shaders.ConstructRasterProgram("VehicleScan.wgsl", {
            VertexEntry: "ScanMeshVertex", FragmentEntry: "TransitionSolidFragment",
            VertexLayouts: MeshVertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: ReadDepth,
            Targets: [{ format: "rgba16float", blend: AlphaBlend }]
        }, [this.Layout]);

        // ② Double-sided additive Fresnel + scanline + Gaussian-wavefront Hologram Ghost shell.
        this.GhostProgram = await this.Shaders.ConstructRasterProgram("VehicleScan.wgsl", {
            VertexEntry: "ScanMeshVertex", FragmentEntry: "GhostShellFragment",
            VertexLayouts: MeshVertexLayouts,
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: ReadDepth,
            Targets: [{ format: "rgba16float", blend: AdditiveBlend }]
        }, [this.Layout]);

        // ③ Quad-expanded anti-aliased 14°/28° dihedral feature-edge wireframe clipped to BodyLocal.x >= uScanX + 0.05.
        this.WireProgram = await this.Shaders.ConstructRasterProgram("VehicleScan.wgsl", {
            VertexEntry: "WireEdgeVertex", FragmentEntry: "WireEdgeFragment",
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: ReadDepth,
            Targets: [{ format: "rgba16float", blend: AlphaBlend }]
        }, [this.Layout]);

        // ④ Dual additive wavefront curtain planes (2.4 × 2.5 m @ 0.075 and 0.55 × 2.5 m @ 0.30) riding x = uScanX.
        this.CurtainProgram = await this.Shaders.ConstructRasterProgram("VehicleScan.wgsl", {
            VertexEntry: "CurtainPlaneVertex", FragmentEntry: "CurtainPlaneFragment",
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: ReadDepth,
            Targets: [{ format: "rgba16float", blend: AdditiveBlend }]
        }, [this.Layout]);

        // ⑤ 130 floating #8fd8f2 holographic sparkles drawn post-TAA so particles remain crisp.
        this.SparkleProgram = await this.Shaders.ConstructRasterProgram("VehicleScan.wgsl", {
            VertexEntry: "SparkleQuadVertex", FragmentEntry: "SparkleQuadFragment",
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: ReadDepth,
            Targets: [{ format: "rgba16float", blend: AdditiveBlend }]
        }, [this.Layout]);
    }

    AttachHmiFrameRecords(HmiFrameRecords, OccupantRecords, MaterialRecords, VehicleRecords)
    {
        this.HmiGroup = this.Exchange.ConstructDescriptorGroup(this.Layout, [
            HmiFrameRecords, OccupantRecords, MaterialRecords, VehicleRecords, this.HmiUniform, this.EdgeBuffer, this.SparkleBuffer]);
    }

    Advance(Seconds, MainMode, HmiMode = MainMode)
    {
        this.MainState.Advance(Seconds, MainMode);
        this.HmiState.Advance(Seconds, HmiMode);
        if (this.MainUniform)
            this.Exchange.Device.queue.writeBuffer(this.MainUniform, 0, this.MainState.EncodeUniformScalars());
        if (this.HmiUniform)
            this.Exchange.Device.queue.writeBuffer(this.HmiUniform, 0, this.HmiState.EncodeUniformScalars());
    }

    RecordOverlay(Encoder, TargetView, DepthView, VertexRecords, UseHmi = false)
    {
        const State = UseHmi ? this.HmiState : this.MainState;
        const Group = UseHmi ? this.HmiGroup : this.MainGroup;
        if (!State.Active || !Group || !this.GhostProgram)
            return;

        const Pass = Encoder.beginRenderPass({
            label: UseHmi ? "HMI vehicle scan overlay" : "Main vehicle scan overlay",
            colorAttachments: [{ view: TargetView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: { view: DepthView, depthReadOnly: true }
        });
        Pass.setBindGroup(0, Group);

        // ① Fading solid transition band [-0.9, -0.05]
        Pass.setPipeline(this.TransitionProgram);
        Pass.setVertexBuffer(0, VertexRecords);
        for (const Run of this.VehicleDrawRuns)
        {
            const Set = this.Scene.FacetSets[Run.FacetSet];
            Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
        }

        // ② Double-sided Fresnel Hologram Ghost (in Ghost and Scan modes)
        if (State.EffectiveMode === VehicleViewMode.Ghost || State.EffectiveMode === VehicleViewMode.Scan)
        {
            Pass.setPipeline(this.GhostProgram);
            Pass.setVertexBuffer(0, VertexRecords);
            for (const Run of this.VehicleDrawRuns)
            {
                const Set = this.Scene.FacetSets[Run.FacetSet];
                Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
            }
        }

        // ③ Dihedral feature-edge wireframe (14° body / 28° wheels)
        if (State.EdgeOpacity > 0.002 && this.EdgeCount > 0)
        {
            Pass.setPipeline(this.WireProgram);
            Pass.draw(6, this.EdgeCount);
        }

        // ④ Dual scan wavefront curtain planes (in Scan mode)
        if ((State.CurtainAlpha0 > 0.001 || State.CurtainAlpha1 > 0.001) && State.EffectiveMode === VehicleViewMode.Scan)
        {
            Pass.setPipeline(this.CurtainProgram);
            Pass.draw(6, 2);
        }

        Pass.end();
    }

    RecordPostOverlay(Encoder, TargetView, DepthView, UseHmi = false)
    {
        const State = UseHmi ? this.HmiState : this.MainState;
        const Group = UseHmi ? this.HmiGroup : this.MainGroup;
        if (State.SparkleAlpha <= 0.002 || !Group || !this.SparkleProgram || this.SparkleCount === 0)
            return;

        const Pass = Encoder.beginRenderPass({
            label: UseHmi ? "HMI scan sparkles" : "Main scan sparkles",
            colorAttachments: [{ view: TargetView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: { view: DepthView, depthReadOnly: true }
        });
        Pass.setBindGroup(0, Group);
        Pass.setPipeline(this.SparkleProgram);
        Pass.draw(6, this.SparkleCount);
        Pass.end();
    }
}
