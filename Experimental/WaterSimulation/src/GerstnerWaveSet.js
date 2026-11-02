/*====================================================================================================================================
                                                    GERSTNER WAVE SET.JS
====================================================================================================================================*/
// 🧩 Sixteen trochoidal wave components with pseudo-random phases and directions around the wind bearing

const Gravity = 9.81;
const TwoPi = 6.28318530718;

export const GerstnerCapacity = 16;

function PhaseHash(Index, Seed)
{
    const Value = Math.sin((Index + 1) * 12.9898 + Seed * 78.233) * 43758.5453;
    return Value - Math.floor(Value);
}

export class GerstnerWaveSet
{
    constructor()
    {
        this.Direction = new Float32Array(GerstnerCapacity * 4);
        this.Shape = new Float32Array(GerstnerCapacity * 4);
        this.Count = GerstnerCapacity;
        this.Configure(1.4, 0.6, 0.8);
    }

    Configure(WaveHeight, WindBearing, Choppiness)
    {
        const ShortestWavelength = 1.6;
        const LongestWavelength = 52.0;
        const TargetRms = Math.max(WaveHeight * 0.30, 0.01);
        const BaseAmplitudes = new Float32Array(GerstnerCapacity);
        const Wavelengths = new Float32Array(GerstnerCapacity);
        const Bearings = new Float32Array(GerstnerCapacity);

        let SumSquares = 0.0;

        for (let Index = 0; Index < GerstnerCapacity; Index++)
        {
            const Ratio = Index / (GerstnerCapacity - 1);
            const Wavelength = LongestWavelength * Math.pow(ShortestWavelength / LongestWavelength, Ratio);
            const Spread = (PhaseHash(Index, 3.7) - 0.5) * 1.35 + Math.sin(Index * 1.7) * 0.16;
            const Weight = Math.pow(ShortestWavelength / Wavelength, 0.55);

            Wavelengths[Index] = Wavelength;
            Bearings[Index] = WindBearing + Spread;
            BaseAmplitudes[Index] = Weight;
            SumSquares += Weight * Weight;
        }

        const Normalization = SumSquares > 0.0 ? TargetRms / Math.sqrt(SumSquares * 0.5) : 0.0;

        for (let Index = 0; Index < GerstnerCapacity; Index++)
        {
            const Amplitude = BaseAmplitudes[Index] * Normalization;
            const Wavelength = Wavelengths[Index];
            const Bearing = Bearings[Index];
            const Steepness = Math.min(Choppiness * 0.85, 0.98);
            const Phase = PhaseHash(Index, 11.3) * TwoPi;
            const SpeedScale = 0.85 + PhaseHash(Index, 5.1) * 0.3;
            const WaveNumber = TwoPi / Wavelength;

            this.Direction[Index * 4 + 0] = Math.cos(Bearing);
            this.Direction[Index * 4 + 1] = Math.sin(Bearing);
            this.Direction[Index * 4 + 2] = Wavelength;
            this.Direction[Index * 4 + 3] = Steepness;

            this.Shape[Index * 4 + 0] = Amplitude;
            this.Shape[Index * 4 + 1] = SpeedScale;
            this.Shape[Index * 4 + 2] = Phase;
            this.Shape[Index * 4 + 3] = Math.sqrt(Gravity * WaveNumber) * SpeedScale;
        }
    }

    Apply(Uniforms)
    {
        Uniforms.SetArray4('uGerstnerDirection', this.Direction);
        Uniforms.SetArray4('uGerstnerShape', this.Shape);
        Uniforms.SetInt('uGerstnerCount', this.Count);
    }
}
