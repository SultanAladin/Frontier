// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                     CELESTIAL FIELD 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// GPU mirror of the texture-free lunar physics in CelestialFieldSolver.cpp, ported line for line so the CPU reference harness stays the
// ground truth for the GPU path:
//   - std140 CelestialBlock (608 B) mirror of CelestialFieldSolver.h::CelestialUniformBlock (2 vec4 + 6 x 96 B moon entries)
//   - lunar volumetric medium (Rayleigh + Mie exponentials, detached haze Gaussian, smooth top taper above the haze flank)
//   - radius-uniform chord partition: equal Δr sub-segments either side of closest approach (no ghost limb rings at the tangent point
//     or an obliquely crossed detached layer) and the radial step budget resolving the thinnest vertical structure
//   - sun-ward lunar transmittance with the soft body umbra / penumbra
//   - Hapke shadow-hiding opposition surge and the Lommel-Seeliger / Oren-Nayar regolith reflectance blend
// Per-preset constants the 608 B block does not carry (Mie extinction/scatter ratio, haze half-width and scatter, opposition width) are
// keyed by the preset index in PhaseSpinPack.w; they match BuildLunarPreset exactly. Albedo maps and the star catalogue are bound by the
// consuming shader.
//
// The including shader picks the binding:  #define CELESTIAL_FIELD_BINDING n  (default 3).

#ifndef CELESTIAL_FIELD_GLSL
#define CELESTIAL_FIELD_GLSL

#ifndef CELESTIAL_FIELD_BINDING
#define CELESTIAL_FIELD_BINDING 3
#endif

const uint MaximumLunarBodySlots = 6u;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// UNIFORM BLOCK (std140, mirrors CelestialFieldSolver.h::CelestialUniformBlock, 608 bytes)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

struct LunarEntry
{
    vec4 DirectionRadius;               // xyz local direction (+Y up); w angular radius [rad]
    vec4 AlbedoRoughness;               // xyz regolith tint * geometric albedo; w roughness
    vec4 PhaseSpinPack;                 // x Lommel-Seeliger weight; y opposition amplitude; z spin longitude [rad]; w preset index
    vec4 AtmosphereGeometryPack;        // x body radius R_m [km]; y top altitude H_a [km] (0 = airless); z H_R [km]; w H_M [km]
    vec4 AtmosphereRayleighAsymmetry;   // xyz Rayleigh β_R [1/km]; w Mie asymmetry g
    vec4 AtmosphereMieHazePack;         // xyz Mie β_M [1/km]; w detached haze altitude [km] (0 = none)
};

layout(std140, binding = CELESTIAL_FIELD_BINDING) uniform CelestialBlock
{
    vec4       StarControlPack;   // x sidereal rotation [rad]; y site latitude [rad]; z star scale; w Milky Way scale
    vec4       OpticsCountPack;   // x pixel angle [rad]; y scintillation; z sequence seconds [s]; w active moon count
    LunarEntry Moons[MaximumLunarBodySlots];
} Celestial;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// PRESET CONSTANTS (Luna, Ember, Glacier, Sulfur, Shroud, Shard — BuildLunarPreset)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

const vec3 PresetMieScattering[6] = vec3[6](
    vec3(0.00070, 0.00058, 0.00026), vec3(0.0072, 0.0042, 0.0019), vec3(0.0011, 0.0016, 0.0021),
    vec3(0.0048, 0.0034, 0.0009),    vec3(0.0070, 0.0044, 0.0016), vec3(0.0003, 0.0003, 0.0003));
const vec3 PresetMieExtinction[6] = vec3[6](
    vec3(0.00080, 0.00068, 0.00034), vec3(0.0084, 0.0056, 0.0032), vec3(0.0012, 0.0017, 0.0022),
    vec3(0.0054, 0.0042, 0.0020),    vec3(0.0090, 0.0066, 0.0040), vec3(0.0004, 0.0004, 0.0004));
const vec3 PresetHazeScatter[6] = vec3[6](
    vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.00045, 0.00090, 0.00230), vec3(0.0006, 0.0009, 0.0018));
const float PresetHazeHalfWidthKm[6]   = float[6](1.0, 1.0, 1.0, 1.0, 22.0, 9.0);
const float PresetOppositionWidth[6]   = float[6](0.075, 0.10, 0.05, 0.09, 0.20, 0.07);

struct LunarAtmosphereProfile
{
    float BodyRadiusKm;
    float TopAltitudeKm;
    float RayleighScaleHeightKm;
    float MieScaleHeightKm;
    vec3  RayleighScatteringPerKm;
    vec3  MieScatteringPerKm;
    vec3  MieExtinctionPerKm;
    float DetachedHazeAltitudeKm;
    float DetachedHazeHalfWidthKm;
    vec3  DetachedHazeScatterPerKm;
    bool  AtmosphereEnabled;
};

uint ResolveLunarPresetIndex(LunarEntry Moon)
{
    return min(uint(Moon.PhaseSpinPack.w + 0.5), 5u);
}

// 📝 Unpack one moon's medium; the Mie extinction keeps the preset's per-channel extinction/scatter ratio over the uploaded β_M.
LunarAtmosphereProfile DecodeLunarAtmosphere(LunarEntry Moon)
{
    uint Preset = ResolveLunarPresetIndex(Moon);
    LunarAtmosphereProfile Profile;
    Profile.BodyRadiusKm             = Moon.AtmosphereGeometryPack.x;
    Profile.TopAltitudeKm            = Moon.AtmosphereGeometryPack.y;
    Profile.RayleighScaleHeightKm    = Moon.AtmosphereGeometryPack.z;
    Profile.MieScaleHeightKm         = Moon.AtmosphereGeometryPack.w;
    Profile.RayleighScatteringPerKm  = Moon.AtmosphereRayleighAsymmetry.xyz;
    Profile.MieScatteringPerKm       = Moon.AtmosphereMieHazePack.xyz;
    Profile.MieExtinctionPerKm       = Moon.AtmosphereMieHazePack.xyz * (PresetMieExtinction[Preset] / max(PresetMieScattering[Preset], vec3(1.0e-6)));
    Profile.DetachedHazeAltitudeKm   = Moon.AtmosphereMieHazePack.w;
    Profile.DetachedHazeHalfWidthKm  = PresetHazeHalfWidthKm[Preset];
    Profile.AtmosphereEnabled        = Profile.TopAltitudeKm > 0.0;   // top altitude is uploaded pre-multiplied by the atmosphere gate
    Profile.DetachedHazeScatterPerKm = Profile.AtmosphereEnabled ? PresetHazeScatter[Preset] : vec3(0.0);
    return Profile;
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// LUNAR MEDIUM + RADIAL CHORD PARTITION
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateCelestialSmoothStep(float EdgeLow, float EdgeHigh, float Parameter)
{
    float Ratio = clamp((Parameter - EdgeLow) / max(1.0e-8, EdgeHigh - EdgeLow), 0.0, 1.0);
    return Ratio * Ratio * (3.0 - 2.0 * Ratio);
}

struct LunarMediumSample
{
    vec3 Scattering;     // [1/km] σ_s (Rayleigh + Mie + detached haze)
    vec3 Extinction;     // [1/km] σ_t
    vec3 RayleighPart;   // [1/km] Rayleigh share (phase P_R)
    vec3 MiePart;        // [1/km] Mie + haze share (phase P_CS)
};

LunarMediumSample EvaluateLunarMedium(LunarAtmosphereProfile Profile, float AltitudeKm)
{
    LunarMediumSample Medium = LunarMediumSample(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    if (AltitudeKm > Profile.TopAltitudeKm)
        return Medium;
    float Altitude        = max(0.0, AltitudeKm);
    float HazeClearance   = (Profile.DetachedHazeAltitudeKm > 0.0) ? Profile.DetachedHazeAltitudeKm + 3.0 * Profile.DetachedHazeHalfWidthKm : 0.0;
    float TaperStart      = min(Profile.TopAltitudeKm * 0.95, max(Profile.TopAltitudeKm * 0.70, HazeClearance));
    float TopTaper        = 1.0 - EvaluateCelestialSmoothStep(TaperStart, Profile.TopAltitudeKm, Altitude);
    float RayleighDensity = exp(-Altitude / max(0.1, Profile.RayleighScaleHeightKm)) * TopTaper;
    float MieDensity      = exp(-Altitude / max(0.1, Profile.MieScaleHeightKm)) * TopTaper;
    float HazeDensity     = 0.0;
    if (Profile.DetachedHazeAltitudeKm > 0.0)
    {
        float HazeOffset = (Altitude - Profile.DetachedHazeAltitudeKm) / max(0.5, Profile.DetachedHazeHalfWidthKm);
        HazeDensity = exp(-HazeOffset * HazeOffset) * TopTaper;
    }
    Medium.RayleighPart = Profile.RayleighScatteringPerKm * RayleighDensity;
    Medium.MiePart      = Profile.MieScatteringPerKm * MieDensity + Profile.DetachedHazeScatterPerKm * HazeDensity;
    Medium.Scattering   = Medium.RayleighPart + Medium.MiePart;
    Medium.Extinction   = Medium.RayleighPart + Profile.MieExtinctionPerKm * MieDensity + Profile.DetachedHazeScatterPerKm * HazeDensity;
    return Medium;
}

float ResolveChordRadius(float ImpactSquared, float Parameter)
{
    return sqrt(ImpactSquared + Parameter * Parameter);
}

float ResolveChordParameterAtRadius(float ImpactSquared, float Radius, float Sign)
{
    return Sign * sqrt(max(0.0, Radius * Radius - ImpactSquared));
}

// 📝 Radius-uniform chord partition over [Start, End] for a ray with closest approach at s = 0 (impact b). Returns (lower, upper, middle)
//    of sub-segment Ordinal of Count (mirror of ResolveRadialChordSegment).
vec3 ResolveRadialChordSegment(uint Ordinal, uint Count, float Start, float End, float ImpactKm)
{
    float ImpactSquared = ImpactKm * ImpactKm;
    float Pivot         = clamp(0.0, Start, End);
    float PivotRadius   = ResolveChordRadius(ImpactSquared, Pivot);
    float FrontRange    = max(0.0, ResolveChordRadius(ImpactSquared, Start) - PivotRadius);
    float RearRange     = max(0.0, ResolveChordRadius(ImpactSquared, End) - PivotRadius);

    uint FrontCount = 0u;
    if (FrontRange > 1.0e-6 && RearRange > 1.0e-6)
        FrontCount = clamp(uint(floor(float(Count) * FrontRange / (FrontRange + RearRange) + 0.5)), 1u, Count - 1u);   // std::lround
    else if (FrontRange > 1.0e-6)
        FrontCount = Count;

    float Lower;
    float Upper;
    float Middle;
    if (Ordinal < FrontCount)
    {
        float StartRadius = ResolveChordRadius(ImpactSquared, Start);
        float Increment   = FrontRange / float(FrontCount);
        Lower  = (Ordinal == 0u) ? Start : ResolveChordParameterAtRadius(ImpactSquared, StartRadius - Increment * float(Ordinal), -1.0);
        Upper  = (Ordinal + 1u == FrontCount) ? Pivot : ResolveChordParameterAtRadius(ImpactSquared, StartRadius - Increment * float(Ordinal + 1u), -1.0);
        Middle = ResolveChordParameterAtRadius(ImpactSquared, StartRadius - Increment * (float(Ordinal) + 0.5), -1.0);
    }
    else
    {
        uint  RearOrdinal = Ordinal - FrontCount;
        uint  RearCount   = Count - FrontCount;
        float Increment   = RearRange / float(RearCount);
        Lower  = (RearOrdinal == 0u) ? Pivot : ResolveChordParameterAtRadius(ImpactSquared, PivotRadius + Increment * float(RearOrdinal), 1.0);
        Upper  = (RearOrdinal + 1u == RearCount) ? End : ResolveChordParameterAtRadius(ImpactSquared, PivotRadius + Increment * float(RearOrdinal + 1u), 1.0);
        Middle = ResolveChordParameterAtRadius(ImpactSquared, PivotRadius + Increment * (float(RearOrdinal) + 0.5), 1.0);
    }
    return vec3(Lower, Upper, clamp(Middle, Lower, Upper));
}

// 📝 Radial step budget: ≈ 0.5 of the thinnest vertical structure per radial increment, bounded by the request and a hard cap of 96.
uint ResolveRadialStepCount(LunarAtmosphereProfile Profile, float RadialSpanKm, uint RequestedCount)
{
    float Structure = min(Profile.RayleighScaleHeightKm, Profile.MieScaleHeightKm);
    if (Profile.DetachedHazeAltitudeKm > 0.0)
        Structure = min(Structure, Profile.DetachedHazeHalfWidthKm);
    float Increment = max(0.5, 0.5 * Structure);
    uint  Derived   = uint(ceil(max(0.0, RadialSpanKm) / Increment));
    return clamp(Derived, max(4u, RequestedCount), 96u);
}

// 📝 Sun-ward transmittance from moon-centred Position [km] through the lunar atmosphere with the soft body umbra (penumbra ~0.53°).
vec3 EvaluateLunarSunTransmittance(LunarAtmosphereProfile Profile, vec3 Position, vec3 SolarAxis, uint StepCount)
{
    float AlongSun      = dot(Position, SolarAxis);
    float RadiusSquared = dot(Position, Position);
    float ImpactSquared = max(0.0, RadiusSquared - AlongSun * AlongSun);

    float Umbra = 1.0;
    if (AlongSun < 0.0)
    {
        float PenumbraWidth = 0.0046 * abs(AlongSun) + 1.5;
        Umbra = EvaluateCelestialSmoothStep(Profile.BodyRadiusKm - PenumbraWidth, Profile.BodyRadiusKm + PenumbraWidth, sqrt(ImpactSquared));
        if (Umbra <= 0.0)
            return vec3(0.0);
    }
    if (!Profile.AtmosphereEnabled)
        return vec3(Umbra);

    float TopRadius        = Profile.BodyRadiusKm + Profile.TopAltitudeKm;
    float ExitDiscriminant = AlongSun * AlongSun - (RadiusSquared - TopRadius * TopRadius);
    if (ExitDiscriminant <= 0.0)
        return vec3(Umbra);
    float ExitDistance = -AlongSun + sqrt(ExitDiscriminant);
    if (ExitDistance <= 0.0)
        return vec3(Umbra);

    float SunImpact    = sqrt(ImpactSquared);
    uint  SegmentCount = max(2u, StepCount);
    vec3  OpticalDepth = vec3(0.0);
    for (uint Step = 0u; Step < SegmentCount; ++Step)
    {
        vec3  Segment     = ResolveRadialChordSegment(Step, SegmentCount, AlongSun, ExitDistance + AlongSun, SunImpact);
        vec3  SamplePoint = Position + SolarAxis * (Segment.z - AlongSun);
        float AltitudeKm  = length(SamplePoint) - Profile.BodyRadiusKm;
        OpticalDepth += EvaluateLunarMedium(Profile, AltitudeKm).Extinction * (Segment.y - Segment.x);
    }
    return Umbra * exp(-OpticalDepth);
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// REGOLITH BRDF
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

// 📝 Hapke shadow-hiding opposition surge B(g) = B0 / (1 + tan(g/2) / h).
float EvaluateHapkeOppositionSurge(float PhaseAngleRadians, float SurgeAmplitude, float SurgeWidth)
{
    float HalfTangent = tan(clamp(0.5 * PhaseAngleRadians, 0.0, 1.55));
    return SurgeAmplitude / (1.0 + HalfTangent / max(1.0e-3, SurgeWidth));
}

// 📝 Porous-regolith reflectance factor: Lommel-Seeliger 2μ0/(μ0+μ) blended with Oren-Nayar rough diffuse.
float EvaluateRegolithReflectance(vec3 ShadingNormal, vec3 SolarAxis, vec3 ViewAxis, float Roughness, float LommelSeeligerWeight)
{
    float IncidentCosine = dot(ShadingNormal, SolarAxis);
    if (IncidentCosine <= 0.0)
        return 0.0;
    float EmergentCosine = max(1.0e-3, dot(ShadingNormal, ViewAxis));
    float LommelSeeliger = 2.0 * IncidentCosine / (IncidentCosine + EmergentCosine);

    float Variance             = Roughness * Roughness;
    float TermA                = 1.0 - 0.5 * Variance / (Variance + 0.33);
    float TermB                = 0.45 * Variance / (Variance + 0.09);
    vec3  SunTangent           = SolarAxis - ShadingNormal * IncidentCosine;
    vec3  ViewTangent          = ViewAxis - ShadingNormal * EmergentCosine;
    float TangentLengthProduct = length(SunTangent) * length(ViewTangent);
    float AzimuthCosine        = (TangentLengthProduct > 1.0e-6) ? max(0.0, dot(SunTangent, ViewTangent) / TangentLengthProduct) : 0.0;
    float IncidentAngle        = acos(clamp(IncidentCosine, 0.0, 1.0));
    float EmergentAngle        = acos(clamp(EmergentCosine, 0.0, 1.0));
    float AlphaAngle           = max(IncidentAngle, EmergentAngle);
    float BetaAngle            = min(IncidentAngle, EmergentAngle);
    float OrenNayar            = IncidentCosine * (TermA + TermB * AzimuthCosine * sin(AlphaAngle) * tan(min(BetaAngle, 1.5)));

    float Weight = clamp(LommelSeeligerWeight, 0.0, 1.0);
    return Weight * LommelSeeliger + (1.0 - Weight) * OrenNayar;
}

// 📝 Regolith reflectance factor for one moon entry (mirror of EvaluateLunarSurfaceRadiance): opposition surge from the preset width
//    and the geometric terminator gate (relief may catch grazing light slightly past the terminator, never deep on the night side).
float EvaluateLunarSurfaceResponse(LunarEntry Moon, vec3 SurfaceNormal, vec3 ShadingNormal, vec3 SolarAxis, vec3 ViewAxis)
{
    float TerminatorGate = EvaluateCelestialSmoothStep(-0.06, 0.02, dot(SurfaceNormal, SolarAxis));
    float PhaseAngle     = acos(clamp(dot(SolarAxis, ViewAxis), -1.0, 1.0));
    float Opposition     = 1.0 + EvaluateHapkeOppositionSurge(PhaseAngle, Moon.PhaseSpinPack.y, PresetOppositionWidth[ResolveLunarPresetIndex(Moon)]);
    return EvaluateRegolithReflectance(ShadingNormal, SolarAxis, ViewAxis, Moon.AlbedoRoughness.w, Moon.PhaseSpinPack.x) * Opposition * TerminatorGate;
}

#endif
