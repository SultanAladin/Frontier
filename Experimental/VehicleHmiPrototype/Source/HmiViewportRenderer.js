//============================================================================================================================================
//                                                          HMIVIEWPORTRENDERER.JS
//============================================================================================================================================
// 🧩 Dedicated 1024 × 640 ReSTIR GI configurator viewport renderer for the Cabin HMI Garage screen.
//
// 📝 Replaces the legacy studio forward raster pass with the engine's full ReSTIR GI + BVH path-traced reflections +
//    SVGF temporal/wavelet variance reconstruction + TAA + ACES tonemapped presentation pipeline, isolated to ONLY 1
//    hero vehicle (excluding all guest lobby vehicles and plates from both primary visibility and the BVH TLAS) with
//    an orbiting turntable camera and 1:1 X-Ray Scan & Material Ripple integration.

import { SurfaceProjection } from "./SurfaceProjection.js";
import { NearestDepthSpace } from "./NearestDepthSpace.js";
import { ResamplingSequence } from "./ResamplingSequence.js";
import { VarianceIntegrator } from "./VarianceIntegrator.js";
import { DisplayProjection } from "./DisplayProjection.js";
import { ResolveHmiPreviewExtent } from "./HmiPreviewExtent.js";
import { TransformPoint, ConstructViewTransform, ConstructPerspectiveTransform, MultiplyTransform, InvertTransform,
         ProjectRadicalInverse } from "./TransformProjection.js";

export { HmiPreviewExtent } from "./HmiPreviewExtent.js";

const FrameRecordBytes = 528;

const FeatureBits = Object.freeze({
    Indirect: 1, TemporalReuse: 2, SpatialReuse: 4, VisibilityReuse: 8, VarianceKernel: 16, Reflection: 32,
    CardRadiosity: 64, Antialias: 128, DirectShadow: 256, AutoExposure: 512, RadianceRefresh: 1024,
    TemporalRestart: 2048,
    RayTracing: 8192, RayTracedShadows: 16384, RussianRoulette: 32768, FireflyFilter: 65536, MultiBounceNEE: 131072,
    RayTracedReflections: 262144
});

const ExtendedBits = Object.freeze({
    ScreenTraversal: 1, ScreenIndirect: 2, ScreenReflection: 4, LightGrid: 8, EmissiveLights: 16, IesProfiles: 32
});

export class HmiViewportRenderer
{
    constructor(Exchange, Shaders, GradeName = "high")
    {
        this.Exchange   = Exchange;
        this.Shaders    = Shaders;
        this.GradeName  = GradeName;
        this.Extent     = ResolveHmiPreviewExtent(GradeName);
        this.TraceScale = GradeName === "high" ? 1 : 2;
        this.TraceExtent = [Math.ceil(this.Extent[0] / this.TraceScale), Math.ceil(this.Extent[1] / this.TraceScale)];

        this.FrameIndex         = 0;
        this.PriorValid         = false;
        this.WasActive          = false;
        this.PreviousUnjittered = null;
        this.ViewProjectionInverse = null;
    }

    static ResolveExcludedOccupants(Scene)
    {
        const Excluded = new Set();
        for (const OccIdx of Scene.DisplayOccupants || [])
            Excluded.add(OccIdx);
        const Plates = Scene.PlateOccupants || [];
        for (let Index = 1; Index < Plates.length; ++Index)
            Excluded.add(Plates[Index]);
        return Excluded;
    }

    async ConstructResources({ Scene, OccupantRecords, MaterialRecords, Distance, Cards, RayTracing, Marks, Effects, VehicleRecords, Shadow, Atmosphere, ScanEffect })
    {
        const Exchange = this.Exchange;
        this.Scene      = Scene;
        this.Distance   = Distance;
        this.Cards      = Cards;
        this.RayTracing = RayTracing;
        this.Shadow     = Shadow;
        this.Atmosphere = Atmosphere;
        this.ScanEffect = ScanEffect;

        this.ExcludedOccupants = HmiViewportRenderer.ResolveExcludedOccupants(Scene);
        this.FilteredTlas = RayTracing.ConstructFilteredView(Scene, this.ExcludedOccupants, MaterialRecords, RayTracing.TraversalResources[3]);

        // Dedicated EffectRecord uniform kept at MaskMode = 0 so world derez/rez never hides the configurator preview car.
        this.NeutralEffectRecords = Exchange.ConstructRecords(Effects?.Records?.size || 1152, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "HMI neutral effect record");
        this.NeutralEffects = { Records: this.NeutralEffectRecords, Record() {} };

        this.FrameRecords = Exchange.ConstructRecords(FrameRecordBytes, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "HMI frame record");

        this.Surface = new SurfaceProjection(Exchange, this.Shaders);
        await this.Surface.ConstructPrograms(this.FrameRecords, OccupantRecords, MaterialRecords, Distance.TraversalLayout,
                                             Cards.SamplingLayout, RayTracing.TraversalLayout, Marks, this.NeutralEffects, VehicleRecords);

        this.NearestDepth = new NearestDepthSpace(Exchange, this.Shaders);
        await this.NearestDepth.ConstructPrograms();

        this.Resampling = new ResamplingSequence(Exchange, this.Shaders);
        await this.Resampling.ConstructPrograms(this.FrameRecords, MaterialRecords, Distance.TraversalLayout, Cards.SamplingLayout,
                                                this.NearestDepth.TraversalLayout);

        this.Diffuse = new VarianceIntegrator(Exchange, this.Shaders, "HMI Diffuse",
                                              { MinimumWeight: 0.08, TallyLimit: 24, WaveletIterations: 3, ColourSigma: 4.0 });
        await this.Diffuse.ConstructPrograms(this.FrameRecords);

        this.Glossy = new VarianceIntegrator(Exchange, this.Shaders, "HMI Glossy",
                                             { MinimumWeight: 0.12, TallyLimit: 12, WaveletIterations: 2, ColourSigma: 3.0 });
        await this.Glossy.ConstructPrograms(this.FrameRecords);

        this.Display = new DisplayProjection(Exchange, this.Shaders);
        await this.Display.ConstructPrograms(this.FrameRecords, MaterialRecords, Distance.TraversalLayout, Cards.SamplingLayout,
                                             OccupantRecords, RayTracing.TraversalLayout);

        if (ScanEffect)
            ScanEffect.AttachHmiFrameRecords(this.FrameRecords, OccupantRecords, MaterialRecords, VehicleRecords);

        const [Width, Height] = this.Extent;
        this.Surface.ConstructTargets(Width, Height, Shadow, Atmosphere);
        this.NearestDepth.ConstructTargets(this.Extent, this.Surface);
        this.Resampling.ConstructTargets(this.TraceExtent, this.Surface);
        this.Diffuse.ConstructTargets(this.TraceExtent, this.Extent, this.Surface,
                                      [this.Resampling.Views.Irradiance, this.Resampling.Views.Irradiance], this.Resampling.Views.Guides);
        this.Glossy.ConstructTargets(this.TraceExtent, this.Extent, this.Surface,
                                     [this.Resampling.Views.Reflection, this.Resampling.Views.Reflection], this.Resampling.Views.Guides);
        this.Display.ConstructTargets(this.Extent, this.Surface, this.Resampling, this.Diffuse, this.Glossy, Cards);
        this.NearestDepth.ConstructTraversalGroups(this.Surface, this.Display, this.FilteredTlas);
    }

    ResolveCamera(BodyTransform, Preview)
    {
        const Centre   = TransformPoint(BodyTransform, [0.0, 0.0, 0.42]);
        const Heading  = Math.atan2(BodyTransform[1], BodyTransform[0]);
        const Yaw      = Heading + Math.PI + (Preview.Yaw ?? 0.6);
        const Pitch    = Math.max(0.06, Math.min(1.05, Preview.Pitch ?? 0.22));
        const Distance = Math.max(6.0, Math.min(15.0, Preview.Distance ?? 8.8));
        const Eye = [
            Centre[0] + Distance * Math.cos(Pitch) * Math.cos(Yaw),
            Centre[1] + Distance * Math.cos(Pitch) * Math.sin(Yaw),
            Centre[2] + Distance * Math.sin(Pitch)
        ];
        return {
            Position: Eye,
            Target: Centre,
            Up: [0.0, 0.0, 1.0],
            VerticalFieldOfView: (34.0 * Math.PI) / 180.0,
            Aspect: this.Extent[0] / this.Extent[1],
            Near: 0.12
        };
    }

    EncodeFrameRecord(Camera, Light, HostSettings, ScanState)
    {
        const [Width, Height] = this.Extent;
        const Sequence = (this.FrameIndex % 16) + 1;
        const JitterX  = (ProjectRadicalInverse(Sequence, 2) - 0.5) * 2.0 / Width;
        const JitterY  = (ProjectRadicalInverse(Sequence, 3) - 0.5) * 2.0 / Height;

        const View           = ConstructViewTransform(Camera.Position, Camera.Target, Camera.Up);
        const Projection     = ConstructPerspectiveTransform(Camera.VerticalFieldOfView, Camera.Aspect, Camera.Near, JitterX, JitterY);
        const Unjittered     = MultiplyTransform(ConstructPerspectiveTransform(Camera.VerticalFieldOfView, Camera.Aspect, Camera.Near), View);
        const ViewProjection = MultiplyTransform(Projection, View);
        this.ViewProjectionInverse = InvertTransform(Unjittered);

        const Previous = this.PreviousUnjittered && this.PriorValid ? this.PreviousUnjittered : Unjittered;
        this.PreviousUnjittered = Unjittered;

        const Bytes    = new ArrayBuffer(FrameRecordBytes);
        const Scalars  = new Float32Array(Bytes);
        const Integers = new Uint32Array(Bytes);

        Scalars.set(ViewProjection, 0);
        Scalars.set(InvertTransform(ViewProjection), 16);
        Scalars.set(Unjittered, 32);
        Scalars.set(Previous, 48);
        Scalars.set(View, 64);
        Scalars.set(Camera.Position, 80);
        Integers[83] = this.FrameIndex >>> 0;
        Scalars.set(Light.Direction, 84);
        Scalars[87] = performance.now() * 0.001;
        Scalars.set(Light.Illuminance, 88);
        Scalars[91] = HostSettings.SkyScale;
        Scalars.set([Width, Height, 1.0 / Width, 1.0 / Height], 92);
        Scalars.set([this.TraceExtent[0], this.TraceExtent[1], 1.0 / this.TraceExtent[0], 1.0 / this.TraceExtent[1]], 96);
        Scalars.set([JitterX, JitterY], 100);
        Integers[102] = this.TraceScale >>> 0;

        let Features = FeatureBits.Indirect | FeatureBits.SpatialReuse | FeatureBits.VisibilityReuse
                     | FeatureBits.VarianceKernel | FeatureBits.Reflection | FeatureBits.CardRadiosity
                     | FeatureBits.Antialias | FeatureBits.DirectShadow | FeatureBits.AutoExposure
                     | FeatureBits.RayTracing | FeatureBits.RayTracedShadows | FeatureBits.RussianRoulette
                     | FeatureBits.FireflyFilter | FeatureBits.MultiBounceNEE | FeatureBits.RayTracedReflections;
        if (this.PriorValid)
            Features |= FeatureBits.TemporalReuse;
        else
            Features |= FeatureBits.TemporalRestart;

        const Bounces     = Math.max(1, Math.min(4, (HostSettings.MaxBounces | 0) || 2));
        const Candidates  = Math.max(1, Math.min(4, (HostSettings.InitialCandidates | 0) || 2));
        const ReflBounces = Math.max(1, Math.min(3, (HostSettings.ReflectionBounces | 0) || 2));
        Features |= (Bounces << 20) | (Candidates << 24) | (ReflBounces << 28);

        let Extended = 0;
        if (HostSettings.LightGrid)      Extended |= ExtendedBits.LightGrid;
        if (HostSettings.EmissiveLights) Extended |= ExtendedBits.EmissiveLights;
        if (HostSettings.IesProfiles)    Extended |= ExtendedBits.IesProfiles;

        Integers[103] = Features >>> 0;
        Integers[104] = 0;
        Scalars[105]  = HostSettings.TemporalTallyLimit ?? 20.0;
        Scalars[106]  = HostSettings.SpatialRadius ?? 18.0;
        Integers[107] = (HostSettings.SpatialCount ?? 3) >>> 0;
        Scalars.set([0.0005, 0.0008, 0.0014], 108);
        Scalars[111]  = HostSettings.EmissionScale ?? 1.0;
        Scalars[112]  = 0.6;
        Scalars[113]  = HostSettings.ExposureCompensation ?? 0.0;
        Scalars[114]  = HostSettings.IndirectScale ?? 1.0;
        const RoughByte  = Math.max(1, Math.min(255, Math.round((HostSettings.ReflectionRoughness ?? 0.80) * 255.0)));
        const ScaleShort = Math.max(1, Math.min(65535, Math.round((HostSettings.ReflectionScale ?? 1.0) * 1000.0) + 1));
        Integers[115] = (1 | (RoughByte << 8) | (ScaleShort << 16)) >>> 0;
        Scalars.set(Camera.Target, 116);
        Integers[119] = 0;
        Scalars[128]  = Camera.Near;
        Integers[129] = Extended >>> 0;
        Scalars[130]  = ScanState && ScanState.Active ? ScanState.ScanX : 0.0;
        Scalars[131]  = ScanState && ScanState.Active
                      ? (ScanState.EffectiveMode === 2 ? ScanState.HoloAmount * (1.0 / 0.965) : ScanState.HoloAmount)
                      : 0.0;

        this.Exchange.Device.queue.writeBuffer(this.FrameRecords, 0, Bytes);
    }

    Record(Encoder, { Scene, VertexRecords, Vehicle, BodyTransform, Light, HostSettings, SamplingGroup, Cabin })
    {
        const Active = Cabin.IsGarageActive();
        if (!Active)
        {
            this.WasActive = false;
            this.PriorValid = false;
            return false;
        }
        if (!this.WasActive)
            this.PriorValid = false;
        this.WasActive = true;

        // Refresh single-vehicle BVH TLAS (1 hero vehicle + studio/world, zero guest vehicles) and draw runs.
        this.RayTracing.UpdateTlas(Scene, Vehicle, this.FilteredTlas);
        const SingleVehicleScene = {
            FacetSets: Scene.FacetSets,
            DrawRuns: Scene.ConstructFilteredDrawRuns(this.ExcludedOccupants)
        };

        const Camera = this.ResolveCamera(BodyTransform, Cabin.Preview);
        const PickF = 1.0 / Math.tan(0.5 * Camera.VerticalFieldOfView);
        const PickNear = Camera.Near, PickFar = 80.0;
        const StandardProjection = new Float32Array([
            PickF / Camera.Aspect, 0, 0, 0,
            0, PickF, 0, 0,
            0, 0, PickFar / (PickNear - PickFar), -1,
            0, 0, (PickNear * PickFar) / (PickNear - PickFar), 0
        ]);
        Cabin.PreviewInverse = InvertTransform(
            MultiplyTransform(StandardProjection, ConstructViewTransform(Camera.Position, Camera.Target, Camera.Up))
        );

        const ScanState = this.ScanEffect ? this.ScanEffect.HmiState : null;
        this.EncodeFrameRecord(Camera, Light, HostSettings, ScanState);

        const Parity     = this.FrameIndex & 1;
        const TraceGroup = this.NearestDepth.TraversalGroups[Parity];

        this.Surface.RecordSurface(Encoder, SingleVehicleScene, VertexRecords);
        this.Surface.RecordDirect(Encoder, this.Distance.TraversalGroup, SamplingGroup, this.FilteredTlas.TraversalGroup);
        this.Resampling.RecordResampling(Encoder, Parity, this.Distance.TraversalGroup, SamplingGroup, TraceGroup);
        this.Diffuse.Record(Encoder, Parity, true);
        this.Resampling.RecordReflection(Encoder, this.Distance.TraversalGroup, SamplingGroup, TraceGroup);
        this.Glossy.Record(Encoder, Parity, true);

        const Overlay = ScanState && ScanState.Active
            ? (TargetEncoder, View) => this.ScanEffect.RecordOverlay(TargetEncoder, View, this.Surface.Views.Depth, VertexRecords, true)
            : null;
        const PostOverlay = ScanState && ScanState.SparkleAlpha > 0.002
            ? (TargetEncoder, View) => this.ScanEffect.RecordPostOverlay(TargetEncoder, View, this.Surface.Views.Depth, true)
            : null;

        this.Display.RecordResolve(Encoder, Parity, undefined, null, Overlay, PostOverlay);
        this.Display.RecordPresent(Encoder, Parity, Cabin.PreviewTextureView, this.Distance.TraversalGroup);

        this.FrameIndex++;
        this.PriorValid = true;
        Cabin.Metrics.PreviewPasses++;
        return true;
    }
}
