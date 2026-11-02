#version 300 es
precision highp float;

#include "Common.glsl"

uniform sampler2D uInitialSpectrum;
uniform int   uSpectrumLogSize;
uniform float uCascadeLength;
uniform float uSimulationTime;

layout(location = 0) out vec4 FragColorA;
layout(location = 1) out vec4 FragColorB;



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

void main()
{
    ivec2 Texel = ivec2(gl_FragCoord.xy);
    int Size = 1 << uSpectrumLogSize;
    ivec2 FrequencyIndex = ivec2(ReverseBits(Texel.x, uSpectrumLogSize), ReverseBits(Texel.y, uSpectrumLogSize));
    vec2 WaveVector = FrequencyVector(FrequencyIndex.x, FrequencyIndex.y, Size);

    vec2 OriginalCoordinate = (vec2(Texel) + 0.5) / float(Size);
    vec2 Original = textureLod(uInitialSpectrum, OriginalCoordinate, 0.0).xy;

    ivec2 NegatedIndex = ivec2((Size - FrequencyIndex.x) % Size, (Size - FrequencyIndex.y) % Size);
    vec2 NegatedCoordinate = (vec2(float(ReverseBits(NegatedIndex.x, uSpectrumLogSize)), float(ReverseBits(NegatedIndex.y, uSpectrumLogSize))) + 0.5)
                             / float(Size);
    vec2 Partner = textureLod(uInitialSpectrum, NegatedCoordinate, 0.0).xy;

    float WaveNumber = length(WaveVector);
    float Omega = sqrt(Gravity * WaveNumber);
    float Phase = Omega * uSimulationTime;
    float CosPhase = cos(Phase);
    float SinPhase = sin(Phase);

    // h̃(k,t) = h0(k) e^{iωt} + conj(h0(-k)) e^{-iωt}
    vec2 Positive = vec2(Original.x * CosPhase - Original.y * SinPhase,
                         Original.x * SinPhase + Original.y * CosPhase);
    vec2 Conjugated = vec2(Partner.x, -Partner.y);
    vec2 Negative = vec2(Conjugated.x * CosPhase + Conjugated.y * SinPhase,
                         Conjugated.y * CosPhase - Conjugated.x * SinPhase);

    vec2 Height = Positive + Negative;

    // Displacement spectra: -i (k/|k|) h̃, packed as two real rotations
    vec2 Direction = WaveVector / max(WaveNumber, 0.00001);
    vec2 Rotation = vec2(Height.y, -Height.x);
    vec2 HorizontalX = Rotation * Direction.x;
    vec2 HorizontalZ = Rotation * Direction.y;

    FragColorA = vec4(Height, HorizontalX);
    FragColorB = vec4(HorizontalZ, 0.0, 0.0);
}
