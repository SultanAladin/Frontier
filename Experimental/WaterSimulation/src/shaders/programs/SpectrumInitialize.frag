#version 300 es
precision highp float;

#include "Common.glsl"
#include "Noise.glsl"

uniform int   uSpectrumLogSize;
uniform float uCascadeLength;
uniform vec2  uWindDirection;
uniform float uWindSpeed;
uniform float uSwellWavelength;
uniform float uSwellStrength;
uniform float uSpectrumScale;
uniform float uRandomSeed;
uniform uint  uRandomSeedBits;
uniform float uShortWaveCutoff;

out vec4 FragColor;



vec2 FrequencyVector(int IndexX, int IndexY, int Size)
{
    vec2 SignedIndex = vec2(float(IndexX), float(IndexY));
    if (IndexX >= Size / 2)
    {
        SignedIndex.x -= float(Size);
    }

    if (IndexY >= Size / 2)
    {
        SignedIndex.y -= float(Size);
    }

    return TwoPi * SignedIndex / uCascadeLength;
}

// 💡 Phillips/JONSWAP hybrid: wind-sea envelope, directional spread, and a narrow swell ring
float DirectionalSpectrum(vec2 WaveVector)
{
    float WaveNumber = length(WaveVector);

    if (WaveNumber < 0.00002)
    {
        return 0.0;
    }

    float LargestWave = uWindSpeed * uWindSpeed / Gravity;
    float Alignment = max(dot(WaveVector / WaveNumber, uWindDirection), 0.0);
    float Envelope = exp(-1.0 / max(WaveNumber * WaveNumber * LargestWave * LargestWave, 0.000001));
    float WindSea = Envelope * pow(Alignment, 2.4) / pow(WaveNumber, 4.0);
    WindSea *= exp(-WaveNumber * WaveNumber * uShortWaveCutoff * uShortWaveCutoff);

    float SwellWaveNumber = TwoPi / max(uSwellWavelength, 1.0);
    float SwellRing = exp(-pow((WaveNumber - SwellWaveNumber) / (SwellWaveNumber * 0.42), 2.0));
    float Swell = uSwellStrength * SwellRing * pow(Alignment, 12.0) / max(pow(WaveNumber, 3.0), 0.000001);

    return WindSea + Swell;
}

// 💡 A PCG integer hash replaces the sine hash: sine hashing correlates neighbouring cells and biases the
//    Gaussian magnitude, which showed up as a 33% shortfall in the reconstructed wave variance.
uint HashCell(uint X, uint Y, uint Seed)
{
    uint Value = X * 1973u + Y * 9277u + Seed * 26699u + 1u;
    Value = Value * 747796405u + 2891336453u;
    uint Word = ((Value >> ((Value >> 28u) + 4u)) ^ Value) * 277803737u;
    return (Word >> 22u) ^ Word;
}

vec2 GaussianPair(ivec2 Texel, uint Seed)
{
    uint FirstBits = HashCell(uint(Texel.x), uint(Texel.y), Seed);
    uint SecondBits = HashCell(uint(Texel.x) + 7919u, uint(Texel.y) + 104729u, Seed ^ 2654435769u);
    float First = (float(FirstBits) + 1.0) / 4294967297.0;
    float Second = float(SecondBits) / 4294967296.0;
    float Radius = sqrt(-2.0 * log(First));

    return vec2(Radius * cos(TwoPi * Second), Radius * sin(TwoPi * Second));
}

void main()
{
    ivec2 Texel = ivec2(gl_FragCoord.xy);
    int Size = 1 << uSpectrumLogSize;
    ivec2 FrequencyIndex = ivec2(ReverseBits(Texel.x, uSpectrumLogSize), ReverseBits(Texel.y, uSpectrumLogSize));
    vec2 WaveVector = FrequencyVector(FrequencyIndex.x, FrequencyIndex.y, Size);

    float SpectrumValue = DirectionalSpectrum(WaveVector) * uSpectrumScale * 0.5;
    float Amplitude = sqrt(max(SpectrumValue, 0.0));
    vec2 Gaussian = GaussianPair(Texel, uRandomSeedBits);

    // 📝 Stored in bit-reversed order so the butterfly passes end in natural frequency order
    FragColor = vec4(Gaussian * Amplitude, 0.0, 0.0);
}
