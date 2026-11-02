/*====================================================================================================================================
                                                    SPECTRAL OCEAN FIELD.JS
====================================================================================================================================*/
// 🧩 Three tiled FFT cascades: spectrum initialization, dispersion evolution, butterfly transforms, and slope/foam resolve

import { RenderPair } from './GraphicsDevice.js';
import { CascadeProfile, ComputeCascadeScales } from './OceanSpectrumModel.js';

export class SpectralOceanField
{
    constructor(Device, Programs, Descriptor = {})
    {
        this.Device = Device;
        this.Programs = Programs;
        this.Resolution = Descriptor.Resolution ?? 256;
        this.LogSize = Math.round(Math.log2(this.Resolution));
        this.ActiveCascadeCount = 3;
        this.ShortWaveCutoff = 0.028;
        this.SwellWavelength = 96.0;
        this.SwellStrength = 0.075;
        this.Cascades = [];

        for (let Index = 0; Index < 3; Index++)
        {
            this.Cascades.push(this.CreateCascade(Index));
        }
    }

    CreateCascade(Index)
    {
        const Device = this.Device;
        const Size = this.Resolution;
        const Profile = CascadeProfile[Index];

        const PairDescriptor =
        {
            Label:  `SpectrumPair${Index}`,
            Width:  Size,
            Height: Size,
            Filter: 'nearest'
        };

        return {
            Index:            Index,
            TileSize:         Profile.TileSize,
            SpectrumScale:    0.0,
            RandomSeed:       Profile.RandomSeed,
            Drift:            [Index * 137.3 + 12.7, Index * 91.7 + 41.3],
            InitialSpectrum:  Device.CreateTarget({
                Label:  `InitialSpectrum${Index}`,
                Width:  Size,
                Height: Size,
                Format: 'rgba32f',
                Filter: 'nearest'
            }),
            StageA:           new RenderPair(Device, PairDescriptor),
            StageB:           new RenderPair(Device, PairDescriptor),
            Displacement:     Device.CreateTarget({
                Label:  `CascadeDisplacement${Index}`,
                Width:  Size,
                Height: Size,
                Format: 'rgba16f',
                Filter: 'linear',
                Wrap:   'repeat'
            }),
            Slope:            Device.CreateTarget({
                Label:  `CascadeSlope${Index}`,
                Width:  Size,
                Height: Size,
                Format: 'rgba16f',
                Filter: 'linear',
                Wrap:   'repeat'
            })
        };
    }

    // 💡 Wave height is honoured by solving the spectral scale from the target variance, not by tuning constants
    Configure(Settings, WindBearing)
    {
        const Descriptor =
        {
            WindSpeed:        Settings.WindSpeed,
            WindX:            Math.cos(WindBearing),
            WindZ:            Math.sin(WindBearing),
            ShortWaveCutoff:  this.ShortWaveCutoff,
            SwellWavelength:  this.SwellWavelength,
            SwellStrength:    this.SwellStrength
        };

        const VarianceTarget = Math.pow(Settings.WaveHeight * 0.30, 2.0);
        const Scales = ComputeCascadeScales(Descriptor, this.Resolution, VarianceTarget, 3);

        for (let Index = 0; Index < 3; Index++)
        {
            this.Cascades[Index].SpectrumScale = Scales[Index].SpectrumScale;
        }

        const Program = this.Programs.get('SpectrumInitialize').Use();
        const Uniforms = Program.Uniforms;

        Uniforms.SetInt('uSpectrumLogSize', this.LogSize);
        Uniforms.SetVec2('uWindDirection', Descriptor.WindX, Descriptor.WindZ);
        Uniforms.SetFloat('uWindSpeed', Settings.WindSpeed);
        Uniforms.SetFloat('uSwellWavelength', this.SwellWavelength);
        Uniforms.SetFloat('uSwellStrength', this.SwellStrength);
        Uniforms.SetFloat('uShortWaveCutoff', this.ShortWaveCutoff);

        for (const Cascade of this.Cascades)
        {
            Uniforms.SetFloat('uCascadeLength', Cascade.TileSize);
            Uniforms.SetFloat('uSpectrumScale', Cascade.SpectrumScale);
            Uniforms.SetFloat('uRandomSeed', Cascade.RandomSeed);
            Uniforms.SetInt('uRandomSeedBits', Math.round(Cascade.RandomSeed * 7919.0) | 0);

            Cascade.InitialSpectrum.Bind();
            this.Device.DrawFullscreen();
        }

        return this;
    }

    Update(SimulationTime)
    {
        const Gl = this.Device.Gl;
        const ButterflyProgram = this.Programs.get('FftButterfly');
        const Size = this.Resolution;

        Gl.disable(Gl.BLEND);

        for (let CascadeIndex = 0; CascadeIndex < this.ActiveCascadeCount; CascadeIndex++)
        {
            const Cascade = this.Cascades[CascadeIndex];

            // 🔴 Uniform locations belong to the bound program: always Use() immediately before setting
            const EvolveProgram = this.Programs.get('SpectrumEvolution').Use();

            // --- Dispersion evolution into frequency space ---
            const EvolveUniforms = EvolveProgram.Uniforms;
            EvolveUniforms.SetTexture('uInitialSpectrum', Cascade.InitialSpectrum.ColorTexture, 14);
            EvolveUniforms.SetInt('uSpectrumLogSize', this.LogSize);
            EvolveUniforms.SetFloat('uCascadeLength', Cascade.TileSize);
            EvolveUniforms.SetFloat('uSimulationTime', SimulationTime);

            Cascade.StageA.Bind();
            this.Device.DrawFullscreen();

            let Source = Cascade.StageA;
            let Destination = Cascade.StageB;

            // --- Butterfly transforms: rows then columns ---
            for (let Axis = 0; Axis < 2; Axis++)
            {
                for (let Stage = 0; Stage < this.LogSize; Stage++)
                {
                    const Pass = ButterflyProgram.Use();
                    const Uniforms = Pass.Uniforms;
                    Uniforms.SetTexture('uSourceA', Source.Textures[0], 14);
                    Uniforms.SetTexture('uSourceB', Source.Textures[1], 15);
                    Uniforms.SetInt('uStage', Stage);
                    Uniforms.SetInt('uHorizontal', Axis === 0 ? 1 : 0);
                    Uniforms.SetFloat('uSign', 1.0);
                    Uniforms.SetInt('uSize', Size);

                    Destination.Bind();
                    this.Device.DrawFullscreen();

                    const Swap = Source;
                    Source = Destination;
                    Destination = Swap;
                }
            }

            // --- Spatial resolve and slope/foam derivation ---
            const ResolveProgram = this.Programs.get('SpectralResolve').Use();
            const ResolveUniforms = ResolveProgram.Uniforms;
            ResolveUniforms.SetTexture('uFieldA', Source.Textures[0], 14);
            ResolveUniforms.SetTexture('uFieldB', Source.Textures[1], 15);
            ResolveUniforms.SetFloat('uAmplitudeScale', 1.0);
            ResolveUniforms.SetFloat('uChoppiness', this.Choppiness);
            ResolveUniforms.SetInt('uResolution', Size);

            Cascade.Displacement.Bind();
            this.Device.DrawFullscreen();

            const SlopeProgram = this.Programs.get('SpectralSlope').Use();
            const SlopeUniforms = SlopeProgram.Uniforms;
            SlopeUniforms.SetTexture('uDisplacement', Cascade.Displacement.ColorTexture, 14);
            SlopeUniforms.SetInt('uResolution', Size);
            SlopeUniforms.SetFloat('uTexelWorld', Cascade.TileSize / Size);

            Cascade.Slope.Bind();
            this.Device.DrawFullscreen();
        }
    }

    Apply(Uniforms, Settings)
    {
        Uniforms.SetInt('uCascadeCount', this.ActiveCascadeCount);
        Uniforms.SetVec3('uCascadeTile',
                         this.Cascades[0].TileSize,
                         this.Cascades[1].TileSize,
                         this.Cascades[2].TileSize);

        this.Cascades.forEach((Cascade, Index) =>
        {
            Uniforms.SetTexture(`uSpectralDisplacement${Index}`, Cascade.Displacement.ColorTexture, 3 + Index * 2);
            Uniforms.SetTexture(`uSpectralSlope${Index}`, Cascade.Slope.ColorTexture, 4 + Index * 2);
            Uniforms.SetVec2(`uCascadeDrift${Index}`, Cascade.Drift[0], Cascade.Drift[1]);
        });

        return this;
    }
}
