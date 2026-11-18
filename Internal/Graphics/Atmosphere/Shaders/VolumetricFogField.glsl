// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                  VOLUMETRIC FOG FIELD 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// GPU mirror of VolumetricFogSolver.cpp. Every function here is a line-for-line port of its C++ namesake so the CPU reference harness
// (AtmosphereCelestialValidation) stays the ground truth for the GPU path:
//   - std140 FogFieldBlock (272 B) mirror of FogFieldUniformBlock
//   - closed-form exponential height-fog segment integral and sun-ward column, advected fBm thickness patches
//   - ellipsoidal fog banks with fBm-distorted edges, pooling and clumping; SunTransmittanceMarch through the banks
//   - dual-phase bounded-age advection through the SAME AtmosphericFlowField the clouds use (AtmosphericFlowField.glsl)
//   - Wrenninge multiple-scatter octaves + two-stream diffusion tail, sky ambient, energy-conserving Beer-Lambert segments
// A flow-free scene binds a zeroed FlowFieldBlock (wind off, no curl, no sources): the displacement is then zero.
//
// The including shader picks the bindings:  #define FLOW_FIELD_BINDING n  /  #define FOG_FIELD_BINDING n  (defaults 1 and 3).

#ifndef VOLUMETRIC_FOG_FIELD_GLSL
#define VOLUMETRIC_FOG_FIELD_GLSL

#ifndef FOG_FIELD_BINDING
#define FOG_FIELD_BINDING 3
#endif

#include "AtmosphericFlowField.glsl"
#include "AtmosphereLatticeNoise.glsl"

const float FogPi                     = 3.14159265358979323846;
const uint  MaximumFogVolumeSlots     = 4u;
const float AdvectionStationSpacingKm = 0.25;   // mirrors AdvectionStationSpacingKm
const uint  AdvectionStationLimit     = 24u;    // mirrors AdvectionStationLimit
const uint  HeightNoiseSalt           = 7001u;
const uint  VolumeNoiseSalt           = 7103u;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// UNIFORM BLOCK (std140, mirror VolumetricFogSolver.h::FogFieldUniformBlock 272 B)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

struct FogVolumeEntry
{
    vec4 CentreSoftness;     // xyz world centre [m] (+Z up); w edge softness
    vec4 ExtentExtinction;   // xyz half-axes [m]; w core σ_t [1/km]
    vec4 NoisePack;          // x noise strength; y noise scale [m]; z active (1/0); w unused
};

layout(std140, binding = FOG_FIELD_BINDING) uniform FogFieldBlock
{
    vec4           HeightPack;    // x floor altitude [km]; y ground σ_t [1/km] (0 = height fog off); z scale height [km]; w flow variation
    vec4           OpticsPack;    // x variation scale [km]; y flow coupling; z ω_0; w ambient strength
    vec4           PhasePack;     // x forward g; y backward g; z forward lobe weight; w primary step count (0 = fog off)
    vec4           QualityPack;   // x sun taps; y trace distance [km]; z sequence seconds [s]; w active volume count
    vec4           FlowPack;      // x advection period [s]; yzw unused
    FogVolumeEntry Volumes[MaximumFogVolumeSlots];
} FogField;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// HEIGHT FOG (closed form)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateFogSmoothStep(float EdgeLow, float EdgeHigh, float Parameter)
{
    float Ratio = clamp((Parameter - EdgeLow) / max(1.0e-6, EdgeHigh - EdgeLow), 0.0, 1.0);
    return Ratio * Ratio * (3.0 - 2.0 * Ratio);
}

float EvaluateHeightAntiderivative(float AltitudeKm)
{
    float Relative    = AltitudeKm - FogField.HeightPack.x;
    float ScaleHeight = max(1.0e-4, FogField.HeightPack.z);
    if (Relative <= 0.0)
        return Relative;
    return ScaleHeight * (1.0 - exp(-Relative / ScaleHeight));
}

float EvaluateHeightProfile(float AltitudeKm)
{
    float Relative = AltitudeKm - FogField.HeightPack.x;
    return (Relative <= 0.0) ? 1.0 : exp(-Relative / max(1.0e-4, FogField.HeightPack.z));
}

float IntegrateHeightFogSegment(float StartAltitudeKm, float EndAltitudeKm, float LengthKm)
{
    if (FogField.HeightPack.y <= 0.0 || LengthKm <= 0.0)
        return 0.0;
    float Rise = EndAltitudeKm - StartAltitudeKm;
    if (abs(Rise) < 1.0e-4)
        return FogField.HeightPack.y * LengthKm * EvaluateHeightProfile(0.5 * (StartAltitudeKm + EndAltitudeKm));
    float Column = EvaluateHeightAntiderivative(EndAltitudeKm) - EvaluateHeightAntiderivative(StartAltitudeKm);
    return FogField.HeightPack.y * LengthKm * Column / Rise;
}

float EvaluateHeightFogSunOpticalDepth(float AltitudeKm, float UpCosine)
{
    if (FogField.HeightPack.y <= 0.0)
        return 0.0;
    float Column = max(1.0e-4, FogField.HeightPack.z) - EvaluateHeightAntiderivative(AltitudeKm);
    return FogField.HeightPack.y * Column / max(0.052, UpCosine);
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// OPTICS
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateFogHenyeyGreenstein(float CosineAngle, float Asymmetry)
{
    float G           = clamp(Asymmetry, -0.99, 0.99);
    float G2          = G * G;
    float Denominator = max(1.0e-4, 1.0 + G2 - 2.0 * G * CosineAngle);
    return (1.0 - G2) / (4.0 * FogPi * Denominator * sqrt(Denominator));
}

float EvaluatePhaseHenyeyGreenstein(float CosineAngle, float EccentricityScale)
{
    float Weight = clamp(FogField.PhasePack.z, 0.0, 1.0);
    return Weight * EvaluateFogHenyeyGreenstein(CosineAngle, FogField.PhasePack.x * EccentricityScale)
         + (1.0 - Weight) * EvaluateFogHenyeyGreenstein(CosineAngle, FogField.PhasePack.y * EccentricityScale);
}

float EvaluateFogDiffuseTransmission(float OpticalDepth)
{
    float Weight    = clamp(FogField.PhasePack.z, 0.0, 1.0);
    float Asymmetry = Weight * FogField.PhasePack.x + (1.0 - Weight) * FogField.PhasePack.y;
    return 1.0 / (1.0 + 0.75 * (1.0 - clamp(Asymmetry, -0.9, 0.9)) * max(0.0, OpticalDepth));
}

float EvaluateSunScatterSource(float CosineAngle, float SunOpticalDepth)
{
    float Source      = 0.0;
    float Energy      = 1.0;
    float Attenuation = 1.0;
    float Eccentric   = 1.0;
    for (uint Octave = 0u; Octave < 4u; ++Octave)
    {
        Source      += Energy * exp(-SunOpticalDepth * Attenuation) * EvaluatePhaseHenyeyGreenstein(CosineAngle, Eccentric);
        Energy      *= 0.5;
        Attenuation *= 0.5;
        Eccentric   *= 0.5;
    }
    float Tail = max(0.0, EvaluateFogDiffuseTransmission(SunOpticalDepth) - exp(-SunOpticalDepth * 0.125));
    return Source + Tail / (4.0 * FogPi);
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// DENSITY (height modulation + fog banks)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateHeightModulation(vec3 SourceMetres, float Condensation)
{
    float Modulation = 1.0;
    if (FogField.HeightPack.w > 0.0)
    {
        float ScaleMetres = max(1.0, FogField.OpticsPack.x * 1000.0);
        vec3  Coord       = vec3(SourceMetres.x / ScaleMetres, SourceMetres.y / ScaleMetres, SourceMetres.z * 4.0 / ScaleMetres);
        float Noise       = EvaluateRotatedFbm3D(Coord, 4u, HeightNoiseSalt);
        Modulation = clamp(1.0 + clamp(FogField.HeightPack.w, 0.0, 1.0) * (2.0 * Noise - 1.0) * 2.0, 0.0, 2.0);
    }
    return Modulation * max(0.0, 1.0 + FogField.OpticsPack.y * Condensation);
}

float EvaluateFogVolumeDensity(vec3 SourceMetres, float Condensation)
{
    float Total = 0.0;
    uint  Count = min(uint(FogField.QualityPack.w + 0.5), MaximumFogVolumeSlots);
    for (uint Index = 0u; Index < Count; ++Index)
    {
        FogVolumeEntry Volume = FogField.Volumes[Index];
        if (Volume.NoisePack.z < 0.5 || Volume.ExtentExtinction.w <= 0.0)
            continue;
        vec3  Relative = (SourceMetres - Volume.CentreSoftness.xyz) / max(vec3(1.0), Volume.ExtentExtinction.xyz);
        float Radius   = length(Relative);
        float Noise    = clamp(Volume.NoisePack.x, 0.0, 1.0);
        if (Radius >= 1.0 + 0.4 * Noise)
            continue;
        float ScaleMetres = max(1.0, Volume.NoisePack.y);
        vec3  Coord       = vec3(SourceMetres.x / ScaleMetres, SourceMetres.y / ScaleMetres, SourceMetres.z * 2.5 / ScaleMetres);
        float Clump       = EvaluateRotatedFbm3D(Coord, 4u, VolumeNoiseSalt + 97u * Index);
        float Distorted   = Radius * (1.0 - 0.8 * Noise * (Clump - 0.5));
        float Softness    = clamp(Volume.CentreSoftness.w, 0.02, 1.0);
        float Mask        = 1.0 - EvaluateFogSmoothStep(1.0 - Softness, 1.0, Distorted);
        float Pooling     = 1.0 - 0.5 * EvaluateFogSmoothStep(-0.2, 1.0, Relative.z);
        float Texture     = clamp(1.0 + Noise * (2.0 * Clump - 1.0) * 1.4, 0.0, 2.0);
        Total += Volume.ExtentExtinction.w * Mask * Pooling * Texture;
    }
    return Total * max(0.0, 1.0 + FogField.OpticsPack.y * Condensation);
}

float SunTransmittanceMarch(vec3 SourceMetres, vec3 WorldDirection, float Condensation, uint TapCount)
{
    float ReachMetres = 0.0;
    uint  Count       = min(uint(FogField.QualityPack.w + 0.5), MaximumFogVolumeSlots);
    for (uint Index = 0u; Index < Count; ++Index)
        if (FogField.Volumes[Index].NoisePack.z >= 0.5)
        {
            vec3 Extent = FogField.Volumes[Index].ExtentExtinction.xyz;
            ReachMetres = max(ReachMetres, 2.8 * max(Extent.x, max(Extent.y, Extent.z)));
        }
    if (ReachMetres <= 0.0 || TapCount == 0u)
        return 0.0;
    ReachMetres = min(ReachMetres, 6000.0);

    float OpticalDepth = 0.0;
    float Previous     = 0.0;
    for (uint Tap = 0u; Tap < TapCount; ++Tap)
    {
        float Edge     = float(Tap + 1u) / float(TapCount);
        float Distance = ReachMetres * Edge * Edge;
        float Middle   = 0.5 * (Previous + Distance);
        OpticalDepth  += EvaluateFogVolumeDensity(SourceMetres + WorldDirection * Middle, Condensation) * (Distance - Previous) * 0.001;
        Previous       = Distance;
    }
    return OpticalDepth;
}

float ResolveFogTopAltitudeKm()
{
    float Top = -1.0e9;
    if (FogField.HeightPack.y > 0.0)
        Top = FogField.HeightPack.x + 12.0 * max(1.0e-4, FogField.HeightPack.z) + ((FogField.HeightPack.w > 0.0) ? FogField.HeightPack.z * 0.7 : 0.0);
    uint Count = min(uint(FogField.QualityPack.w + 0.5), MaximumFogVolumeSlots);
    for (uint Index = 0u; Index < Count; ++Index)
        if (FogField.Volumes[Index].NoisePack.z >= 0.5)
            Top = max(Top, (FogField.Volumes[Index].CentreSoftness.z + 1.4 * FogField.Volumes[Index].ExtentExtinction.z) * 0.001 + 0.6);
    return Top;
}

bool IntersectFogSphere(float ViewerRadiusKm, vec3 Direction, float SphereRadiusKm, out float OutNear, out float OutFar)
{
    float HalfB        = ViewerRadiusKm * Direction.y;
    float Discriminant = HalfB * HalfB - (ViewerRadiusKm * ViewerRadiusKm - SphereRadiusKm * SphereRadiusKm);
    OutNear = 0.0;
    OutFar  = 0.0;
    if (Discriminant < 0.0)
        return false;
    float Root = sqrt(Discriminant);
    OutNear = -HalfB - Root;
    OutFar  = -HalfB + Root;
    return true;
}

// 📝 Dual-phase bounded-age advection (mirror of ResolveFogAdvection): xyz displacement to the parcel origin [m], w condensation coupling.
vec4 ResolveFogAdvection(vec3 WorldMetres, float SequenceSeconds)
{
    float Period       = max(1.0, FogField.FlowPack.x);
    float Elapsed      = max(0.0, SequenceSeconds);
    float PhaseFirst   = mod(Elapsed, Period);
    float PhaseSecond  = mod(Elapsed + 0.5 * Period, Period);
    float WeightFirst  = 1.0 - abs(2.0 * PhaseFirst / Period - 1.0);
    float WeightSecond = 1.0 - WeightFirst;
    FlowAdvectedSample First  = TraceFlowCharacteristicBackward(WorldMetres, PhaseFirst, 4u);
    FlowAdvectedSample Second = TraceFlowCharacteristicBackward(WorldMetres, PhaseSecond, 4u);
    vec3  Displacement = (First.SourcePositionMetres - WorldMetres) * WeightFirst + (Second.SourcePositionMetres - WorldMetres) * WeightSecond;
    float Instant      = EvaluateAtmosphericFlow(WorldMetres, SequenceSeconds).CondensationDelta;
    float Condensation = clamp(0.5 * (First.CondensationDelta + Second.CondensationDelta) + 0.5 * Instant, -1.0, 1.0);
    return vec4(Displacement, Condensation);
}

vec4 SolveFogAdvectionStation(vec3 LocalDirection, float ViewerRadiusKm, float BottomRadiusKm, float TraceStart, float StationSpacing, int Station, float SequenceSeconds)
{
    float Distance = TraceStart + StationSpacing * float(Station);
    vec3  Local    = vec3(LocalDirection.x * Distance, ViewerRadiusKm + LocalDirection.y * Distance, LocalDirection.z * Distance);
    vec3  World    = vec3(Local.x * 1000.0, Local.z * 1000.0, (length(Local) - BottomRadiusKm) * 1000.0);
    return ResolveFogAdvection(World, SequenceSeconds);
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// INTEGRATION (mirror of IntegrateVolumetricFog)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

// LocalDirection / SunDirection are atmosphere-local (+Y up); SunIlluminance and AmbientRadiance come from the CPU PrepareFogLighting
// (or an equivalent GPU reduction). Jitter in [0, 1). Returns rgb in-scattered radiance, a transmittance.
vec4 IntegrateVolumetricFog(vec3 LocalDirection, float ViewerRadiusKm, float BottomRadiusKm, vec3 SunDirection, vec3 SunIlluminance,
                            vec3 AmbientRadiance, float SceneDistanceKm, float Jitter)
{
    uint Steps = uint(FogField.PhasePack.w + 0.5);
    if (Steps == 0u)
        return vec4(0.0, 0.0, 0.0, 1.0);

    float TopRadiusKm = BottomRadiusKm + ResolveFogTopAltitudeKm();
    float TopNear     = 0.0;
    float TopFar      = 0.0;
    if (TopRadiusKm <= BottomRadiusKm || !IntersectFogSphere(ViewerRadiusKm, LocalDirection, TopRadiusKm, TopNear, TopFar) || TopFar <= 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);
    float TraceStart = max(0.0, TopNear);
    float TraceEnd   = min(TopFar, max(0.0, FogField.QualityPack.y));
    float GroundNear = 0.0;
    float GroundFar  = 0.0;
    if (IntersectFogSphere(ViewerRadiusKm, LocalDirection, BottomRadiusKm, GroundNear, GroundFar) && GroundNear > 0.0)
        TraceEnd = min(TraceEnd, GroundNear);
    if (SceneDistanceKm > 0.0)
        TraceEnd = min(TraceEnd, SceneDistanceKm);
    if (TraceEnd <= TraceStart)
        return vec4(0.0, 0.0, 0.0, 1.0);

    float PhaseCosine  = dot(LocalDirection, SunDirection);
    float Albedo       = clamp(FogField.OpticsPack.z, 0.0, 1.0);
    vec3  SunWorld     = vec3(SunDirection.x, SunDirection.z, SunDirection.y);
    vec3  Ambient      = AmbientRadiance * max(0.0, FogField.OpticsPack.w);
    bool  SunCondition = SunIlluminance.r + SunIlluminance.g + SunIlluminance.b > 0.0;
    float Sequence     = FogField.QualityPack.z;
    uint  SunTaps      = uint(FogField.QualityPack.x + 0.5);

    float Span        = TraceEnd - TraceStart;
    float Growth      = clamp(log(1.0 + Span / 0.15), 0.5, 5.0);
    float GrowthScale = Span / (exp(Growth) - 1.0);

    float Transmittance   = 1.0;
    float SegmentStart    = TraceStart;
    float StartAltitudeKm = sqrt(TraceStart * TraceStart + 2.0 * TraceStart * ViewerRadiusKm * LocalDirection.y + ViewerRadiusKm * ViewerRadiusKm) - BottomRadiusKm;
    vec3  Radiance        = vec3(0.0);

    float StationSpacing = max(AdvectionStationSpacingKm, Span / float(AdvectionStationLimit));
    int   LowerStation   = -2;
    vec4  LowerShift     = vec4(0.0);
    vec4  UpperShift     = vec4(0.0);

    for (uint Step = 0u; Step < Steps; ++Step)
    {
        float Parameter     = float(Step + 1u) / float(Steps);
        float SegmentEnd    = TraceStart + GrowthScale * (exp(Growth * Parameter) - 1.0);
        float SegmentLength = SegmentEnd - SegmentStart;
        float EndAltitudeKm = sqrt(SegmentEnd * SegmentEnd + 2.0 * SegmentEnd * ViewerRadiusKm * LocalDirection.y + ViewerRadiusKm * ViewerRadiusKm) - BottomRadiusKm;

        float ProbeDistance = SegmentStart + ((Step == 0u) ? Jitter : (0.25 + 0.5 * Jitter)) * SegmentLength;
        vec3  ProbeLocal    = vec3(LocalDirection.x * ProbeDistance, ViewerRadiusKm + LocalDirection.y * ProbeDistance, LocalDirection.z * ProbeDistance);
        float ProbeAltitude = length(ProbeLocal) - BottomRadiusKm;
        vec3  ProbeWorld    = vec3(ProbeLocal.x * 1000.0, ProbeLocal.z * 1000.0, ProbeAltitude * 1000.0);

        float StationCoordinate = (ProbeDistance - TraceStart) / StationSpacing;
        int   Station           = int(floor(StationCoordinate));
        if (Station != LowerStation)
        {
            LowerShift = (Station == LowerStation + 1) ? UpperShift
                                                 : SolveFogAdvectionStation(LocalDirection, ViewerRadiusKm, BottomRadiusKm, TraceStart, StationSpacing, Station, Sequence);
            UpperShift = SolveFogAdvectionStation(LocalDirection, ViewerRadiusKm, BottomRadiusKm, TraceStart, StationSpacing, Station + 1, Sequence);
            LowerStation  = Station;
        }
        vec4  Shift        = mix(LowerShift, UpperShift, StationCoordinate - float(Station));
        vec3  Source       = ProbeWorld + Shift.xyz;
        float Condensation = Shift.w;

        float HeightModulation = (FogField.HeightPack.y > 0.0) ? EvaluateHeightModulation(Source, Condensation) : 0.0;
        float VolumeDensity    = EvaluateFogVolumeDensity(Source, Condensation);
        float OpticalDepth     = IntegrateHeightFogSegment(StartAltitudeKm, EndAltitudeKm, SegmentLength) * HeightModulation + VolumeDensity * SegmentLength;

        if (OpticalDepth > 1.0e-7)
        {
            float ColumnModulation = 0.5 + 0.5 * HeightModulation;
            vec3  InScatter        = vec3(0.0);
            if (SunCondition)
            {
                float SunDepth = EvaluateHeightFogSunOpticalDepth(ProbeAltitude, SunDirection.y) * ColumnModulation
                               + SunTransmittanceMarch(Source, SunWorld, Condensation, SunTaps);
                InScatter = SunIlluminance * EvaluateSunScatterSource(PhaseCosine, SunDepth);
            }
            float UpDepth = EvaluateHeightFogSunOpticalDepth(ProbeAltitude, 1.0) * ColumnModulation
                          + SunTransmittanceMarch(Source, vec3(0.0, 0.0, 1.0), Condensation, 3u);
            InScatter += Ambient * EvaluateFogDiffuseTransmission(UpDepth);

            float SegmentTransmittance = exp(-OpticalDepth);
            Radiance      += InScatter * (Transmittance * Albedo * (1.0 - SegmentTransmittance));
            Transmittance *= SegmentTransmittance;
            if (Transmittance < 1.0e-4)
            {
                Transmittance = 0.0;
                break;
            }
        }
        SegmentStart    = SegmentEnd;
        StartAltitudeKm = EndAltitudeKm;
    }
    return vec4(Radiance, Transmittance);
}

#endif
