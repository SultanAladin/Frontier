/*============================================================================================================================================
                                                      GPUSHALLOWWATER.JS
============================================================================================================================================*/
// 🧩 Ping-pong state fields for the nested 2.5D shallow-water tiers, rolling windows and two-way LOD coupling

import { CreateFloatTarget, CreateFullscreenVertexArray, CreateProgramBundle, ReadFloatRegion } from './GlResources.js';
import { FullscreenVertexShader, SolverFragmentShader } from './Shaders.js';
import { ResolvePositiveModulo } from './WaterClipmapLayout.js';

function DecodeHalfFloat(Bits)
{
    const Sign = (Bits & 0x8000) ? -1 : 1;
    const Exponent = (Bits >> 10) & 0x1F;
    const Mantissa = Bits & 0x03FF;
    if (Exponent === 0) return Sign * Math.pow(2, -14) * (Mantissa / 1024);
    if (Exponent === 31) return Mantissa ? NaN : Sign * Infinity;
    return Sign * Math.pow(2, Exponent - 15) * (1 + Mantissa / 1024);
}

export class GpuShallowWater
{
    constructor(Context, Configuration, Capabilities)
    {
        this.Context = Context;
        this.Configuration = Configuration;
        this.UseHalfFloat = Capabilities.UseHalfFloat;
        this.Resolution = Configuration.GridResolution;
        this.Program = CreateProgramBundle(Context, FullscreenVertexShader, SolverFragmentShader, 'ShallowWaterSolver');
        this.FullscreenVertexArray = CreateFullscreenVertexArray(Context);
        this.Rings = [];
        for (let Level = 0; Level < Configuration.RingCount; ++Level)
        {
            this.Rings.push({
                Targets: [
                    CreateFloatTarget(Context, this.Resolution, this.Resolution, this.UseHalfFloat),
                    CreateFloatTarget(Context, this.Resolution, this.Resolution, this.UseHalfFloat)
                ],
                ReadIndex: 0,
                PreviousOriginX: Number.MIN_SAFE_INTEGER,
                PreviousOriginZ: Number.MIN_SAFE_INTEGER
            });
        }
        this.PendingImpulse = [0, 0, 0, 1];
        this.ElapsedSeconds = 0;
        this.FrameIndex = 0;
        this.Probe = { Eta: 0, VelocityX: 0, VelocityZ: 0, Depth: 0 };
        this.FinalizeFields();
    }

    get ReadTarget()
    {
        return (Level) => this.Rings[Level].Targets[this.Rings[Level].ReadIndex];
    }

    FinalizeFields()
    {
        const Context = this.Context;
        Context.clearColor(0, 0, 0, 0);
        for (const Ring of this.Rings)
        {
            for (const Target of Ring.Targets)
            {
                Context.bindFramebuffer(Context.FRAMEBUFFER, Target.Framebuffer);
                Context.clear(Context.COLOR_BUFFER_BIT);
            }
            Ring.ReadIndex = 0;
            Ring.PreviousOriginX = Number.MIN_SAFE_INTEGER;
            Ring.PreviousOriginZ = Number.MIN_SAFE_INTEGER;
        }
        Context.bindFramebuffer(Context.FRAMEBUFFER, null);
        this.ElapsedSeconds = 0;
        this.FrameIndex = 0;
    }

    InjectDisturbance(WorldX, WorldZ, Amplitude, Radius)
    {
        this.PendingImpulse = [WorldX, WorldZ, Amplitude, Radius];
    }

    Reclaim()
    {
        const Context = this.Context;
        for (const Ring of this.Rings)
        {
            for (const Target of Ring.Targets)
            {
                Context.deleteFramebuffer(Target.Framebuffer);
                Context.deleteTexture(Target.Texture);
            }
        }
        Context.deleteVertexArray(this.FullscreenVertexArray);
        Context.deleteProgram(this.Program.Program);
        this.Rings.length = 0;
    }

    BindTexture(Unit, Texture)
    {
        const Context = this.Context;
        Context.activeTexture(Context.TEXTURE0 + Unit);
        Context.bindTexture(Context.TEXTURE_2D, Texture);
    }

    Integrate(Windows, ElevationTexture, ElevationResolution, DomainExtent)
    {
        const Context = this.Context;
        const Configuration = this.Configuration;
        const Program = this.Program;
        const Resolution = this.Resolution;
        Context.disable(Context.DEPTH_TEST);
        Context.disable(Context.BLEND);
        Context.viewport(0, 0, Resolution, Resolution);
        Context.bindVertexArray(this.FullscreenVertexArray);
        Program.Activate();

        // 📝 Coarse tiers advance first so fine-tier boundary seeding reads the current coarse field.
        for (let Level = Configuration.RingCount - 1; Level >= 0; --Level)
        {
            const Window = Windows[Level];
            const Ring = this.Rings[Level];
            const ReadTarget = Ring.Targets[Ring.ReadIndex];
            const WriteTarget = Ring.Targets[1 - Ring.ReadIndex];
            const CoarseRing = Level + 1 < Configuration.RingCount ? this.Rings[Level + 1] : null;
            const FineRing = Level > 0 ? this.Rings[Level - 1] : null;
            const CoarseWindow = CoarseRing ? Windows[Level + 1] : null;
            const FineWindow = FineRing ? Windows[Level - 1] : null;

            Context.bindFramebuffer(Context.FRAMEBUFFER, WriteTarget.Framebuffer);
            this.BindTexture(0, ReadTarget.Texture);
            this.BindTexture(1, CoarseRing ? CoarseRing.Targets[CoarseRing.ReadIndex].Texture : ReadTarget.Texture);
            this.BindTexture(2, FineRing ? FineRing.Targets[FineRing.ReadIndex].Texture : ReadTarget.Texture);
            this.BindTexture(3, ElevationTexture);
            Program.SetInteger('StateField', 0);
            Program.SetInteger('CoarseField', 1);
            Program.SetInteger('FineField', 2);
            Program.SetInteger('ElevationField', 3);
            Program.SetIVec2('Shift', Window.ShiftX, Window.ShiftZ);
            Program.SetIVec2('OriginCells', Window.OriginCellsX, Window.OriginCellsZ);
            const PreviousX = Ring.PreviousOriginX === Number.MIN_SAFE_INTEGER ? Window.OriginCellsX - Resolution : Ring.PreviousOriginX;
            const PreviousZ = Ring.PreviousOriginZ === Number.MIN_SAFE_INTEGER ? Window.OriginCellsZ - Resolution : Ring.PreviousOriginZ;
            Program.SetIVec2('PreviousOriginCells', PreviousX, PreviousZ);
            Program.SetIVec2('FineOriginCells', FineWindow ? FineWindow.OriginCellsX : 0, FineWindow ? FineWindow.OriginCellsZ : 0);
            Program.SetInteger('GridResolution', Resolution);
            Program.SetInteger('ElevationResolution', ElevationResolution);
            Program.SetInteger('CoarseEnabled', CoarseRing ? 1 : 0);
            Program.SetInteger('FineEnabled', FineRing ? 1 : 0);
            Program.SetFloat('CellSpacing', Window.CellSpacing);
            Program.SetFloat('CoarseSpacing', CoarseWindow ? CoarseWindow.CellSpacing : Window.CellSpacing);
            Program.SetFloat('FineSpacing', FineWindow ? FineWindow.CellSpacing : Window.CellSpacing);
            Program.SetFloat('DomainExtent', DomainExtent);
            Program.SetVec2('CameraOffset', 0, 0);
            Program.SetFloat('DeltaTime', Window.DeltaTime);
            Program.SetFloat('Gravity', Configuration.Gravity);
            Program.SetFloat('SeaLevel', Configuration.SeaLevel);
            Program.SetFloat('Friction', Configuration.Damping);
            Program.SetFloat('DryDepth', Configuration.DryDepth);
            Program.SetVec2('WindDirection', Configuration.WindDirection[0], Configuration.WindDirection[1]);
            Program.SetFloat('WindStrength', Configuration.WindStrength);
            Program.SetFloat('SwellAmplitude', Configuration.SwellAmplitude);
            Program.SetFloat('SwellWavelength', Configuration.SwellWavelength);
            Program.SetFloat('ElapsedSeconds', this.ElapsedSeconds);
            Program.SetFloat('MaximumDepth', Configuration.MaximumDepth);
            Program.SetVec4('Impulse', this.PendingImpulse[0], this.PendingImpulse[1], this.PendingImpulse[2], this.PendingImpulse[3]);
            Context.drawArrays(Context.TRIANGLES, 0, 3);

            Ring.ReadIndex = 1 - Ring.ReadIndex;
            Ring.PreviousOriginX = Window.OriginCellsX;
            Ring.PreviousOriginZ = Window.OriginCellsZ;
        }

        Context.bindFramebuffer(Context.FRAMEBUFFER, null);
        Context.bindVertexArray(null);
        this.PendingImpulse[2] = 0;
        this.ElapsedSeconds += Windows[0].DeltaTime;
        ++this.FrameIndex;
    }

    // 📝 Reads one live state texel near the camera; this is simulated water, not a procedural texture lookup.
    RefreshProbe(CameraX, CameraZ)
    {
        const Window = this.Configuration.GridResolution;
        const Finest = this.Rings[0];
        const Spacing = this.Configuration.InnerExtent / Window;
        const CellX = Math.floor(CameraX / Spacing);
        const CellZ = Math.floor(CameraZ / Spacing);
        const TexelX = ResolvePositiveModulo(CellX, Window);
        const TexelZ = ResolvePositiveModulo(CellZ, Window);
        const Samples = ReadFloatRegion(this.Context, Finest.Targets[Finest.ReadIndex].Framebuffer, TexelX, TexelZ, 1, 1, this.UseHalfFloat);
        const Decode = ScalarReading => this.UseHalfFloat ? DecodeHalfFloat(ScalarReading) : ScalarReading;
        this.Probe = {
            Eta: Decode(Samples[0]),
            VelocityX: Decode(Samples[1]),
            VelocityZ: Decode(Samples[2]),
            Depth: Decode(Samples[3])
        };
        return this.Probe;
    }
}
