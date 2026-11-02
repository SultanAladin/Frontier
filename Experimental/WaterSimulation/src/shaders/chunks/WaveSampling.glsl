// 🧩 Unified water surface sampling: spectral cascades, analytic Gerstner sum, global grid field, and ripples
#include "Common.glsl"
#include "TerrainSampling.glsl"

// 0 = spectral ocean, 1 = Gerstner crest sum, 2 = global grid wave solve
uniform int   uSimulationCategory;
uniform float uSimulationTime;
uniform float uWaveTime;
uniform float uWaveHeight;
uniform float uChoppiness;

// Spectral cascades
uniform sampler2D uSpectralDisplacement0;
uniform sampler2D uSpectralDisplacement1;
uniform sampler2D uSpectralDisplacement2;
uniform sampler2D uSpectralSlope0;
uniform sampler2D uSpectralSlope1;
uniform sampler2D uSpectralSlope2;
uniform vec3 uCascadeTile;
uniform vec2 uCascadeDrift0;
uniform vec2 uCascadeDrift1;
uniform vec2 uCascadeDrift2;
uniform int  uCascadeCount;

// Analytic crest sum
uniform vec4 uGerstnerDirection[16];
uniform vec4 uGerstnerShape[16];
uniform int  uGerstnerCount;

// Global grid fields
uniform sampler2D uShallowWaterField;
uniform float uShallowDomain;
uniform float uShallowAmplitude;
uniform float uShallowTexelWorld;

uniform sampler2D uRippleField;
uniform float uRippleDomain;
uniform float uRippleAmplitude;
uniform float uRippleTexelWorld;
uniform int   uRippleEnabled;

struct WaveSample
{
    vec3  Displacement;
    vec2  Slope;
    float CrestFoam;
    float Height;
};

vec2 FieldCoordinate(vec2 WorldXz, float Domain)
{
    return WorldXz / Domain + 0.5;
}

bool InsideField(vec2 Coordinate)
{
    return Coordinate.x >= 0.0 && Coordinate.x <= 1.0 && Coordinate.y >= 0.0 && Coordinate.y <= 1.0;
}

// 📝 Cascades are sampled with repeat wrapping, so the surface tiles instead of ending at the domain edge
void AccumulateCascade(vec2 WorldXz, vec2 Drift, float TileSize, sampler2D DisplacementTexture, sampler2D SlopeTexture,
                       float DepthFade, inout WaveSample Sample)
{
    vec2 Coordinate = (WorldXz + Drift) / TileSize;
    vec4 DisplacementEntry = textureLod(DisplacementTexture, Coordinate, 0.0);
    vec4 SlopeEntry = textureLod(SlopeTexture, Coordinate, 0.0);

    Sample.Displacement += DisplacementEntry.xyz * DepthFade;
    Sample.Slope += SlopeEntry.xy * DepthFade;
    Sample.CrestFoam = max(Sample.CrestFoam, SlopeEntry.z * DepthFade);
}

void AccumulateSpectrum(vec2 WorldXz, float DepthFade, inout WaveSample Sample)
{
    AccumulateCascade(WorldXz, uCascadeDrift0, uCascadeTile.x,
                      uSpectralDisplacement0, uSpectralSlope0, DepthFade, Sample);

    if (uCascadeCount > 1)
    {
        AccumulateCascade(WorldXz, uCascadeDrift1, uCascadeTile.y,
                          uSpectralDisplacement1, uSpectralSlope1, DepthFade, Sample);
    }

    if (uCascadeCount > 2)
    {
        AccumulateCascade(WorldXz, uCascadeDrift2, uCascadeTile.z,
                          uSpectralDisplacement2, uSpectralSlope2, DepthFade, Sample);
    }
}

void AccumulateGerstner(vec2 WorldXz, float DepthFade, inout WaveSample Sample)
{
    vec3 Displacement = vec3(0.0);
    vec3 Tangent = vec3(1.0, 0.0, 0.0);
    vec3 Bitangent = vec3(0.0, 0.0, 1.0);
    float SumXX = 0.0;
    float SumZZ = 0.0;
    float SumXZ = 0.0;

    for (int Index = 0; Index < 16; Index++)
    {
        if (Index >= uGerstnerCount)
        {
            break;
        }

        vec4 Layout = uGerstnerDirection[Index];
        vec4 Shape = uGerstnerShape[Index];
        vec2 Direction = normalize(Layout.xy);
        float Wavelength = max(Layout.z, 0.6);
        float Steepness = Layout.w;
        float Amplitude = Shape.x * DepthFade;
        float WaveNumber = TwoPi / Wavelength;
        float AngularSpeed = sqrt(Gravity * WaveNumber) * Shape.y;
        float Phase = WaveNumber * dot(Direction, WorldXz) - AngularSpeed * uWaveTime + Shape.z;
        float Sine = sin(Phase);
        float Cosine = cos(Phase);
        float SteepnessScale = Saturate(Steepness) / max(WaveNumber * Amplitude * float(uGerstnerCount), 0.0001);

        Displacement.x += SteepnessScale * Amplitude * Direction.x * Cosine;
        Displacement.z += SteepnessScale * Amplitude * Direction.y * Cosine;
        Displacement.y += Amplitude * Sine;

        float Chop = SteepnessScale * Amplitude * WaveNumber * Sine;
        Tangent.x   -= Chop * Direction.x * Direction.x;
        Tangent.y   += Amplitude * Direction.x * WaveNumber * Cosine;
        Tangent.z   -= Chop * Direction.x * Direction.y;
        Bitangent.x -= Chop * Direction.x * Direction.y;
        Bitangent.y += Amplitude * Direction.y * WaveNumber * Cosine;
        Bitangent.z -= Chop * Direction.y * Direction.y;

        SumXX += Chop * Direction.x * Direction.x;
        SumZZ += Chop * Direction.y * Direction.y;
        SumXZ += Chop * Direction.x * Direction.y;
    }

    vec3 Normal = normalize(cross(Bitangent, Tangent));

    Sample.Displacement += Displacement;
    Sample.Slope += EncodeSlopeNormal(Normal);

    float Jacobian = (1.0 - SumXX) * (1.0 - SumZZ) - SumXZ * SumXZ;
    Sample.CrestFoam = max(Sample.CrestFoam, 1.0 - smoothstep(0.04, 0.62, Jacobian));
}

// 📝 The grid solve supplies the large swell, so its height is scaled by the shared wave height
void AccumulateGridWaves(vec2 WorldXz, inout WaveSample Sample)
{
    vec2 Coordinate = FieldCoordinate(WorldXz, uShallowDomain);

    if (!InsideField(Coordinate))
    {
        return;
    }

    float Texel = uShallowTexelWorld;
    float Center = textureLod(uShallowWaterField, Coordinate, 0.0).r;
    float LeftSample = textureLod(uShallowWaterField, Coordinate - vec2(Texel / uShallowDomain, 0.0), 0.0).r;
    float RightSample = textureLod(uShallowWaterField, Coordinate + vec2(Texel / uShallowDomain, 0.0), 0.0).r;
    float DownSample = textureLod(uShallowWaterField, Coordinate - vec2(0.0, Texel / uShallowDomain), 0.0).r;
    float UpSample = textureLod(uShallowWaterField, Coordinate + vec2(0.0, Texel / uShallowDomain), 0.0).r;

    float Height = Center * uShallowAmplitude;
    Sample.Displacement.y += Height;
    Sample.Slope += vec2(RightSample - LeftSample, UpSample - DownSample) * (uShallowAmplitude / (2.0 * Texel));

    float Curvature = (LeftSample + RightSample + DownSample + UpSample - 4.0 * Center) / max(Texel * Texel, 0.0001);
    Sample.CrestFoam = max(Sample.CrestFoam, Saturate(-Curvature * 0.45 * uWaveHeight));
}

void AccumulateRipples(vec2 WorldXz, inout WaveSample Sample)
{
    if (uRippleEnabled == 0)
    {
        return;
    }

    vec2 Coordinate = FieldCoordinate(WorldXz, uRippleDomain);

    if (!InsideField(Coordinate))
    {
        return;
    }

    float Texel = uRippleTexelWorld;
    float Center = textureLod(uRippleField, Coordinate, 0.0).r;
    float LeftSample = textureLod(uRippleField, Coordinate - vec2(Texel / uRippleDomain, 0.0), 0.0).r;
    float RightSample = textureLod(uRippleField, Coordinate + vec2(Texel / uRippleDomain, 0.0), 0.0).r;
    float DownSample = textureLod(uRippleField, Coordinate - vec2(0.0, Texel / uRippleDomain), 0.0).r;
    float UpSample = textureLod(uRippleField, Coordinate + vec2(0.0, Texel / uRippleDomain), 0.0).r;

    Sample.Displacement.y += Center * uRippleAmplitude;
    Sample.Slope += vec2(RightSample - LeftSample, UpSample - DownSample) * (uRippleAmplitude / (2.0 * Texel));
}

WaveSample SampleWaves(vec2 WorldXz)
{
    WaveSample Sample;
    Sample.Displacement = vec3(0.0);
    Sample.Slope = vec2(0.0);
    Sample.CrestFoam = 0.0;
    Sample.Height = 0.0;

    float Depth = max(SeaLevel - SampleTerrainHeight(WorldXz), 0.0);
    float DepthFade = mix(0.16, 1.0, smoothstep(0.0, 9.0, Depth));

    if (uSimulationCategory == 0)
    {
        AccumulateSpectrum(WorldXz, DepthFade, Sample);
    }
    else if (uSimulationCategory == 1)
    {
        AccumulateGerstner(WorldXz, DepthFade, Sample);
    }
    else
    {
        AccumulateGridWaves(WorldXz, Sample);
    }

    AccumulateRipples(WorldXz, Sample);

    Sample.Height = Sample.Displacement.y;
    return Sample;
}

float SampleWaterHeight(vec2 WorldXz)
{
    return SeaLevel + SampleWaves(WorldXz).Height;
}

vec3 SampleWaterNormal(vec2 WorldXz)
{
    return DecodeSlopeNormal(SampleWaves(WorldXz).Slope);
}
