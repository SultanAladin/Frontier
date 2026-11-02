/*====================================================================================================================================
                                                        UNIFORM BINDERS.JS
====================================================================================================================================*/
// 🧩 Single place that pushes the wave-field and atmosphere uniforms into any compiled program

import { SmoothStep } from './LinearAlgebra.js';

const DegreeToRadian = Math.PI / 180.0;

export class WaveUniformBinder
{
    constructor(SpectralField, GerstnerSet, ShallowField, RippleField)
    {
        this.Spectral = SpectralField;
        this.Gerstner = GerstnerSet;
        this.Shallow = ShallowField;
        this.Ripple = RippleField;
    }

    Apply(Uniforms, Settings, SimulationTime)
    {
        Uniforms.SetInt('uSimulationCategory', Settings.SimulationCategory | 0);
        Uniforms.SetFloat('uSimulationTime', SimulationTime);
        Uniforms.SetFloat('uWaveTime', SimulationTime * Settings.WaveSpeed);
        Uniforms.SetFloat('uWaveHeight', Settings.WaveHeight);
        Uniforms.SetFloat('uChoppiness', Settings.Choppiness);

        if (this.Spectral)
        {
            this.Spectral.Apply(Uniforms, Settings);
        }

        if (this.Gerstner)
        {
            this.Gerstner.Apply(Uniforms);
        }

        if (this.Shallow)
        {
            Uniforms.SetTexture('uShallowWaterField', this.Shallow.Texture, 0);
            Uniforms.SetFloat('uShallowDomain', this.Shallow.Domain);
            Uniforms.SetFloat('uShallowAmplitude', Settings.WaveHeight * 0.55 + 0.35);
            Uniforms.SetFloat('uShallowTexelWorld', this.Shallow.Domain / this.Shallow.Resolution);
        }

        if (this.Ripple)
        {
            Uniforms.SetTexture('uRippleField', this.Ripple.Texture, 1);
            Uniforms.SetFloat('uRippleDomain', this.Ripple.Domain);
            Uniforms.SetFloat('uRippleAmplitude', 0.32 * Settings.RippleStrength);
            Uniforms.SetFloat('uRippleTexelWorld', this.Ripple.Domain / this.Ripple.Resolution);
            Uniforms.SetInt('uRippleEnabled', Settings.RippleEnabled ? 1 : 0);
        }
    }
}

export class AtmosphereUniformBinder
{
    constructor()
    {
        this.SunDirection = [0, 1, 0];
        this.SunRadiance = [1, 1, 1];
        this.HazeValue = 0.3;
    }

    Configure(Settings)
    {
        const Elevation = Math.max(Settings.SunElevation, 0.5) * DegreeToRadian;
        const Azimuth = Settings.SunAzimuth * DegreeToRadian;
        const ElevationSine = Math.sin(Elevation);

        this.SunDirection =
        [
            Math.cos(Elevation) * Math.sin(Azimuth),
            ElevationSine,
            Math.cos(Elevation) * Math.cos(Azimuth)
        ];

        const Warmth = SmoothStep(0.03, 0.42, ElevationSine);
        const Intensity = 2.6 + 3.4 * SmoothStep(0.0, 0.55, ElevationSine);
        const WarmTint = [1.0, 0.48, 0.22];
        const CoolTint = [1.0, 0.94, 0.85];

        this.SunRadiance =
        [
            (WarmTint[0] + (CoolTint[0] - WarmTint[0]) * Warmth) * Intensity,
            (WarmTint[1] + (CoolTint[1] - WarmTint[1]) * Warmth) * Intensity,
            (WarmTint[2] + (CoolTint[2] - WarmTint[2]) * Warmth) * Intensity
        ];

        this.HazeValue = Math.min(1.0, Settings.SkyHaze + (1.0 - ElevationSine) * 0.45);
        return this;
    }

    Apply(Uniforms, Settings, SimulationTime)
    {
        Uniforms.SetVec3('uSunDirection', this.SunDirection[0], this.SunDirection[1], this.SunDirection[2]);
        Uniforms.SetVec3('uSunRadiance', this.SunRadiance[0], this.SunRadiance[1], this.SunRadiance[2]);
        Uniforms.SetFloat('uSkyTime', SimulationTime);
        Uniforms.SetFloat('uCloudCover', Settings.CloudCover);
        Uniforms.SetFloat('uSkyHaze', this.HazeValue);
    }
}
