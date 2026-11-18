// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                  VOLUMETRIC CLOUD FIELD 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// GPU mirror of the texture-free optics of VolumetricCloudSolver.cpp (the flow field lives in AtmosphericFlowField.glsl, included here).
// Every function here is a line-for-line port of its C++ namesake so the CPU reference harness (AtmosphereCelestialValidation) stays the
// ground truth for the GPU path:
//   - std140 CloudFieldBlock (96 B) mirror of CloudFieldUniformBlock
//   - Henyey-Greenstein dual-lobe phase, Wrenninge multiple-scatter octaves, Beer-Powder, two-stream ambient transmission
//   - cirrus ice phase (δ-M scaled HG mixture + 22° hexagonal-plate halo with prismatic dispersion) and genus height envelopes
//   - germ-grain convective cell field (exact HashLatticeCell port, per-thermal coverage, smooth union), standardised turret blend,
//     glaciated anvil, inverted-Worley detail modifier, liquid-water saturation, stratiform soft ramp
//   - two-speed march steps, fixed-orientation solar shadow cone (uneroded taps) and two-hemisphere sky ambient
// The shape/detail noise volumes and weather map are textures the consuming shader binds; they are not declared here. The cell field reads
// the weather map through a prototype the includer must define:  vec2 SampleCloudWeatherMap(vec2 TileCoord)  -> (coverage, top variation),
// TileCoord in weather-map tiles (REPEAT addressing).
//
// The including shader picks the bindings:  #define FLOW_FIELD_BINDING n  /  #define CLOUD_FIELD_BINDING n  (defaults 1 and 2).

#ifndef VOLUMETRIC_CLOUD_FIELD_GLSL
#define VOLUMETRIC_CLOUD_FIELD_GLSL

#ifndef CLOUD_FIELD_BINDING
#define CLOUD_FIELD_BINDING 2
#endif

#include "AtmosphericFlowField.glsl"
#include "AtmosphereLatticeNoise.glsl"

const float CloudPi                  = 3.14159265358979323846;
const float CloudTau                 = 6.28318530717958647692;
const float CloudDegreesToRadians    = 0.01745329251994329577;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// UNIFORM BLOCK (std140, mirror VolumetricCloudSolver.h::CloudFieldUniformBlock 96 B)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

layout(std140, binding = CLOUD_FIELD_BINDING) uniform CloudFieldBlock
{
    vec4 StratumGeometryPack;   // x floor altitude [km]; y ceiling altitude [km]; z coverage; w genus index
    vec4 StratumNoisePack;      // x shape scale [km]; y detail scale [km]; z erosion strength; w weather scale [km]
    vec4 StratumOpticsPack;     // x σ_t [1/km]; y single-scatter albedo; z anvil spread; w powder strength
    vec4 PhasePack;             // x forward g; y backward g; z forward lobe weight; w octave count
    vec4 CirrusPack;            // x altitude [km]; y coverage; z optical depth peak; w filament scale [km] (0 = veil disabled)
    vec4 QualityPack;           // x primary step limit; y solar cone taps; z trace distance [km]; w sequence seconds [s]
} CloudField;

const uint GenusCumulus       = 0u;
const uint GenusStratocumulus = 1u;
const uint GenusStratus       = 2u;
const uint GenusCumulonimbus  = 3u;
const uint GenusCirrus        = 4u;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// SHAPING
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateCloudSmoothStep(float EdgeLow, float EdgeHigh, float Parameter)
{
    float Ratio = clamp((Parameter - EdgeLow) / max(1.0e-8, EdgeHigh - EdgeLow), 0.0, 1.0);
    return Ratio * Ratio * (3.0 - 2.0 * Ratio);
}


// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// CLOUD OPTICS
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateHenyeyGreenstein(float CosineAngle, float Asymmetry)
{
    float Eccentricity        = clamp(Asymmetry, -0.99, 0.99);
    float EccentricitySquared = Eccentricity * Eccentricity;
    float Denominator         = max(1.0e-4, 1.0 + EccentricitySquared - 2.0 * Eccentricity * CosineAngle);
    return (1.0 - EccentricitySquared) / (4.0 * CloudPi * Denominator * sqrt(Denominator));
}

float EvaluateDualLobePhase(float CosineAngle, float EccentricityScale)
{
    float Weight = clamp(CloudField.PhasePack.z, 0.0, 1.0);
    return Weight * EvaluateHenyeyGreenstein(CosineAngle, CloudField.PhasePack.x * EccentricityScale)
         + (1.0 - Weight) * EvaluateHenyeyGreenstein(CosineAngle, CloudField.PhasePack.y * EccentricityScale);
}

// 📝 Wrenninge (2013) multiple-scattering octaves: a = b = c = 0.5 on extinction, energy, and eccentricity.
float EvaluateScatterOctaves(float CosineScatter, float SunOpticalDepth)
{
    uint  Octaves    = clamp(uint(CloudField.PhasePack.w + 0.5), 1u, 8u);
    float ScatterSum = 0.0;
    float Extinction = 1.0;
    float Energy     = 1.0;
    float Eccentric  = 1.0;
    for (uint Octave = 0u; Octave < Octaves; ++Octave)
    {
        ScatterSum += Energy * EvaluateDualLobePhase(CosineScatter, Eccentric) * exp(-Extinction * SunOpticalDepth);
        Extinction *= 0.5;
        Energy     *= 0.5;
        Eccentric  *= 0.5;
    }
    // Two-stream diffusion tail (mirror of EvaluateScatterOctaves): the diffuse field the octaves miss deep inside a cumulus.
    float DiffuseTransmission = 1.0 / (1.0 + 0.75 * (1.0 - 0.85) * SunOpticalDepth);
    return ScatterSum + 0.3 / CloudPi * DiffuseTransmission * (1.0 - exp(-0.5 * SunOpticalDepth));
}

// 📝 Beer-Powder in-scatter deficit at cloud edges seen away from the sun.
float EvaluatePowderFactor(float Density, float CosineScatter)
{
    float LocalDepth   = Density * CloudField.StratumOpticsPack.x * 0.09;
    float PowderTerm   = 1.0 - exp(-2.0 * LocalDepth);
    float PowderWeight = clamp(CloudField.StratumOpticsPack.w, 0.0, 1.0) * (0.5 - 0.5 * CosineScatter);
    return (1.0 - PowderWeight) + PowderWeight * PowderTerm;
}

// 📝 Conservative two-stream diffuse transmission of sky ambient through the overlying column: T = 1 / (1 + ¾ (1 - g) τ_up), g ≈ 0.85.
float EvaluateAmbientDiffuseTransmission(float UpOpticalDepth)
{
    return 1.0 / (1.0 + 0.75 * (1.0 - 0.85) * UpOpticalDepth);
}

// 📝 Sky ambient through the overlying and underlying columns (mirror of the march): each hemisphere's cosine-weighted radiance
//    reaches the sample through the two-stream transmission and scatters isotropically (2π · L / 4π = L / 2 per hemisphere).
vec3 ResolveHemisphereAmbient(vec3 AmbientTop, vec3 AmbientFloor, float UpOpticalDepth, float DownOpticalDepth)
{
    return AmbientTop * (0.5 * EvaluateAmbientDiffuseTransmission(UpOpticalDepth))
         + AmbientFloor * (0.5 * EvaluateAmbientDiffuseTransmission(DownOpticalDepth));
}

// Unit-disc offsets for the solar shadow cone taps (mirror of ConeOffsets). The cone keeps one fixed orientation per sun direction:
// rotating it per pixel turns the nearest taps into surface-scale shading grain. Only the tap radii are jittered.
const vec2 SolarConeOffsets[12] = vec2[12](
    vec2( 0.38,  0.12), vec2(-0.22,  0.41), vec2(-0.35, -0.28), vec2( 0.18, -0.44), vec2( 0.47,  0.36), vec2(-0.51,  0.07),
    vec2( 0.05,  0.58), vec2( 0.60, -0.21), vec2(-0.30, -0.62), vec2( 0.27,  0.66), vec2(-0.68,  0.40), vec2( 0.71, -0.55));

// 📝 Fixed solar cone tap: xyz offset from the sample [km], w tap segment length [km]. Spacing grows quadratically along the cone of
//    length min(2.8, 1.1 · thickness) and radius scale 0.14 + 0.08 · jitter. Taps must read the UNERODED field (detail disabled):
//    eroded billows are sharper than in-cloud light diffusion and posterise the crowns into flat lit / shadowed patches.
vec4 ResolveSolarConeTap(vec3 SolarAxis, uint Tap, uint TapCount, float ThicknessKm, float RadiusJitter)
{
    vec3  ReferenceAxis = (abs(SolarAxis.y) < 0.95) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3  TangentU      = normalize(cross(ReferenceAxis, SolarAxis));
    vec3  TangentV      = cross(SolarAxis, TangentU);
    float ConeLengthKm  = min(2.8, ThicknessKm * 1.1);
    float RadiusScale   = 0.14 + 0.08 * RadiusJitter;
    float Fraction      = float(Tap + 1u) / float(TapCount);
    float PriorFraction = float(Tap) / float(TapCount);
    float TapDistance   = ConeLengthKm * Fraction * Fraction;
    float ConeRadius    = TapDistance * RadiusScale;
    vec2  Offset        = SolarConeOffsets[min(Tap, 11u)] * ConeRadius;
    return vec4(SolarAxis * TapDistance + TangentU * Offset.x + TangentV * Offset.y, TapDistance - ConeLengthKm * PriorFraction * PriorFraction);
}

// 📝 Distant unresolved shadowing beyond the cone: xyz offset of the long-range coarse tap [km], w its optical-path weight [km].
vec4 ResolveSolarFarTap(vec3 SolarAxis, float ThicknessKm)
{
    float ConeLengthKm = min(2.8, ThicknessKm * 1.1);
    return vec4(SolarAxis * (ConeLengthKm * 2.2), ConeLengthKm * 1.2);
}

// 📝 Stratiform soft shape field (mirror of the Sc / St / Cirrus path): a wide ramp above the calibrated (1 - c) shape quantile leaves a
//    density gradient across the outer few hundred metres of every cell so the erosion sculpts billows instead of dusting a hard shell.
//    Threshold and QuantileTop come from the includer's CPU-calibrated quantile table.
float ResolveStratiformShapeField(float PrimaryShape, float Envelope, float Threshold, float QuantileTop)
{
    float SoftRamp = max(0.06, 0.35 * (QuantileTop - Threshold));
    return clamp((PrimaryShape * Envelope - Threshold) / SoftRamp, 0.0, 1.0);
}

// 📝 Genus vertical density envelope over the stratum height fraction (mirror of EvaluateGenusHeightEnvelope).
float EvaluateGenusHeightEnvelope(uint Genus, float HeightFraction, float TopVariation)
{
    if (Genus == GenusCumulus)
    {
        float TopFraction = 0.55 + 0.45 * TopVariation;
        return EvaluateCloudSmoothStep(0.0, 0.06, HeightFraction) * (1.0 - EvaluateCloudSmoothStep(TopFraction * 0.2, TopFraction, HeightFraction));
    }
    if (Genus == GenusStratocumulus)
        return EvaluateCloudSmoothStep(0.0, 0.18, HeightFraction) * (1.0 - EvaluateCloudSmoothStep(0.50, 0.95, HeightFraction));
    if (Genus == GenusStratus)
        return EvaluateCloudSmoothStep(0.0, 0.25, HeightFraction) * (1.0 - EvaluateCloudSmoothStep(0.65, 1.0, HeightFraction));
    if (Genus == GenusCumulonimbus)
        return EvaluateCloudSmoothStep(0.0, 0.03, HeightFraction) * (1.0 - EvaluateCloudSmoothStep(0.90, 1.0, HeightFraction));
    return EvaluateCloudSmoothStep(0.0, 0.3, HeightFraction) * (1.0 - EvaluateCloudSmoothStep(0.6, 1.0, HeightFraction));
}

// 📝 Closing stratus deck: coverage-gated density floor keeps an optically thin sheet across residual low-quantile patches.
float ApplyStratusClosureFloor(float Density, float Envelope, float LocalCoverage)
{
    return max(Density, Envelope * 0.35 * EvaluateCloudSmoothStep(0.90, 1.0, LocalCoverage));
}

// 📝 Footprint prefilter weights for the middle (tile / 8) and fine (tile / 16) detail octaves: an octave finer than the march step fades
//    toward the next coarser octave instead of aliasing into per-pixel speckle. Returns (MiddleWeight, FineWeight).
vec2 ResolveDetailOctaveWeights(float SampleFootprintKm, float DetailTileKm)
{
    return vec2(clamp(2.0 - SampleFootprintKm * 8.0 / DetailTileKm, 0.0, 1.0), clamp(2.0 - SampleFootprintKm * 16.0 / DetailTileKm, 0.0, 1.0));
}

// 📝 Cirrus ice phase: δ-M scaled (f = 0.5) HG mixture 0.78·HG(0.45) + 0.22·HG(-0.2) plus the 22° hexagonal-plate halo (sharp 0.30° inner
//    edge at the 21.7° minimum deviation, 2.2° outer tail). Returns the per-channel phase with red-inside / blue-outside prismatic dispersion.
vec3 EvaluateCirrusIcePhase(float ScatterCosine)
{
    const float HaloMinimumDeviation = 21.7 * CloudDegreesToRadians;
    float ScatterAngle   = acos(clamp(ScatterCosine, -1.0, 1.0));
    float HaloSigma      = (ScatterAngle < HaloMinimumDeviation ? 0.30 : 2.2) * CloudDegreesToRadians;
    float HaloOffset     = (ScatterAngle - HaloMinimumDeviation) / HaloSigma;
    float HaloLobe       = 0.17 * exp(-HaloOffset * HaloOffset);
    float IcePhase       = 0.78 * EvaluateHenyeyGreenstein(ScatterCosine, 0.45) + 0.22 * EvaluateHenyeyGreenstein(ScatterCosine, -0.20) + HaloLobe;
    float HaloDispersion = 0.35 * HaloLobe / max(1.0e-4, IcePhase) * clamp(HaloOffset - 0.5, -1.0, 1.0);
    return IcePhase * vec3(1.0 - HaloDispersion, 1.0, 1.0 + HaloDispersion);
}

// 📝 δ-M scaled cirrus sheet optical depth (Wiscombe 1977): the forward diffraction spike (f ≈ 0.5) leaves the extinction.
float EvaluateCirrusOpticalDepth(float Filament, float SheetFade, float ChordKm, float HalfThicknessKm)
{
    const float DiffractionFraction = 0.5;
    return CloudField.CirrusPack.z * Filament * SheetFade * max(1.0, ChordKm / (2.0 * HalfThicknessKm)) * (1.0 - DiffractionFraction);
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// CONVECTIVE CELL FIELD + EROSION (mirror of EvaluateConvectiveCellField / ApplyDetailErosion / the convective density branch)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

vec2 SampleCloudWeatherMap(vec2 TileCoord);

// 📝 Liquid-water saturation of the eroded shape field (mirror of ResolveSaturatedDensity).
float ResolveSaturatedDensity(float ShapeField)
{
    return min(1.0, ShapeField * 2.0);
}

// 📝 Local coverage from the weather map and the flow condensation (mirror of ResolveLocalCoverage).
float ResolveLocalCoverage(float WeatherCoverage, float CondensationDelta)
{
    float Coverage = clamp(CloudField.StratumGeometryPack.z, 0.0, 1.0);
    if (uint(CloudField.StratumGeometryPack.w + 0.5) == GenusStratus)
        return clamp(Coverage + (WeatherCoverage - 0.5) * 0.25 * min(1.0, 4.0 * (1.0 - Coverage)), 0.0, 1.0);
    return clamp(Coverage + (WeatherCoverage - 0.5) * 1.10 * (1.0 - 0.6 * Coverage * Coverage) + CondensationDelta * 0.25, 0.0, 1.0);
}

float ResolveTurretAmplitude(uint Genus)
{
    if (Genus == GenusCumulonimbus)
        return 0.55;
    return (Genus == GenusStratocumulus) ? 0.32 : 0.38;
}

// 📝 Turret tile [km] of the large octave (the small octave is 0.38 of it, swizzled zyx with offsets 0.37 / 0.61, altitude × 1.2).
float ResolveTurretTileKm(uint Genus)
{
    return max(0.1, CloudField.StratumNoisePack.x * (Genus == GenusCumulonimbus ? 0.8 : (Genus == GenusStratocumulus ? 1.2 : 1.6)));
}

// 📝 Polynomial smooth maximum: neighbouring thermals merge through a rounded fillet (mirror of BlendFieldUnion).
float BlendFieldUnion(float Left, float Right)
{
    const float BlendWidth = 0.3;
    float Overlap = max(BlendWidth - abs(Left - Right), 0.0) / BlendWidth;
    return max(Left, Right) + Overlap * Overlap * BlendWidth * 0.25;
}

// 📝 Germ-grain convective cell field at a horizontal stratum position [km] (mirror of EvaluateConvectiveCellField).
float EvaluateConvectiveCellField(float HorizontalX, float HorizontalZ, float HeightFraction, float CondensationDelta)
{
    uint  Genus          = uint(CloudField.StratumGeometryPack.w + 0.5);
    bool  TowerCondition = Genus == GenusCumulonimbus;
    bool  RollCondition  = Genus == GenusStratocumulus;
    float Spacing        = max(0.1, CloudField.StratumNoisePack.x);
    float RadiusRatio    = TowerCondition ? 0.40 : (RollCondition ? 0.42 : 0.36);
    float DiscArea       = CloudPi * RadiusRatio * RadiusRatio;
    int   Reach          = TowerCondition ? 2 : 1;
    float RadiusLimit    = (float(Reach) - 0.02) / (1.0 + ResolveTurretAmplitude(Genus));
    float CellX          = HorizontalX / Spacing;
    float CellZ          = HorizontalZ / Spacing;
    int   AnchorX          = int(floor(CellX));
    int   AnchorZ          = int(floor(CellZ));
    float WeatherTile    = max(1.0, CloudField.StratumNoisePack.w * 4.0);
    float BestField      = -1.0;
    for (int OffsetZ = -Reach; OffsetZ <= Reach; ++OffsetZ)
    {
        for (int OffsetX = -Reach; OffsetX <= Reach; ++OffsetX)
        {
            int   LatticeX   = AnchorX + OffsetX;
            int   LatticeZ   = AnchorZ + OffsetZ;
            float CentreX    = float(LatticeX) + HashLatticeCell(LatticeX, 1, LatticeZ, 6203u);
            float CentreZ    = float(LatticeZ) + HashLatticeCell(LatticeX, 2, LatticeZ, 6307u);
            vec2  Weather    = SampleCloudWeatherMap(vec2(CentreX, CentreZ) * Spacing / WeatherTile);
            float Coverage   = ResolveLocalCoverage(Weather.x, CondensationDelta);
            float Occupancy  = -log(1.0 - min(Coverage, 0.985)) / DiscArea;
            float RadiusGain = 1.0;
            if (Occupancy > 1.0)
            {
                RadiusGain = sqrt(Occupancy);
                Occupancy  = 1.0;
            }
            float Existence = clamp((Occupancy - HashLatticeCell(LatticeX, 0, LatticeZ, 6101u)) / 0.12, 0.0, 1.0);
            if (Existence <= 0.0)
                continue;
            float Radius  = min(RadiusLimit, RadiusRatio * RadiusGain * (0.7 + 0.6 * HashLatticeCell(LatticeX, 3, LatticeZ, 6409u))) * sqrt(Existence);
            float TopHash = HashLatticeCell(LatticeX, 4, LatticeZ, 6511u);
            float DeltaX  = (CellX - CentreX) * (RollCondition ? 0.6 : 1.0);
            float DeltaZ  = CellZ - CentreZ;
            float Offset  = sqrt(DeltaX * DeltaX + DeltaZ * DeltaZ);
            if (TowerCondition)
            {
                float Ratio = HeightFraction / (0.86 + 0.14 * TopHash);
                if (Ratio >= 1.0)
                    continue;
                float Tower       = Radius * (1.0 - 0.15 * Ratio) * sqrt(max(0.0, 1.0 - pow(Ratio, 8.0)));
                float Spread      = EvaluateCloudSmoothStep(0.60, 0.90, Ratio);
                float Anvil       = min(RadiusLimit - 0.45 * Spread, Radius * (1.0 + 4.0 * CloudField.StratumOpticsPack.z) * Spread * sqrt(Spread)
                                                                    * (1.0 - EvaluateCloudSmoothStep(0.92, 1.0, Ratio)));
                float AnvilDeltaX = DeltaX - 0.45 * Spread;
                float TowerField  = 1.0 - Offset / max(1.0e-3, Tower);
                float AnvilField  = 1.0 - sqrt(AnvilDeltaX * AnvilDeltaX + DeltaZ * DeltaZ) / max(1.0e-3, Anvil);
                BestField = BlendFieldUnion(BestField, max(TowerField, AnvilField));
            }
            else if (RollCondition)
            {
                float Ratio = HeightFraction / (0.62 + 0.38 * TopHash);
                if (Ratio >= 1.0)
                    continue;
                BestField = BlendFieldUnion(BestField, 1.0 - Offset / max(1.0e-3, Radius * sqrt(1.0 - Ratio * Ratio * Ratio * Ratio)));
            }
            else
            {
                float Ratio = HeightFraction / clamp(0.30 + 0.40 * TopHash + 0.30 * Weather.y, 0.25, 1.0);
                if (Ratio >= 1.0)
                    continue;
                BestField = BlendFieldUnion(BestField, 1.0 - Offset / max(1.0e-3, Radius * sqrt(1.0 - Ratio * Ratio)));
            }
        }
    }
    return BestField;
}

// 📝 Standardised two-octave turret displacement from the two primary-shape samples (centred on the shape median, scaled by the
//    central 75 % quantile span, clamped to ±1).
float ResolveTurretShape(float LargeShape, float SmallShape, float ShapeMedian, float ShapeSpan)
{
    float Blend = 0.65 * (LargeShape - ShapeMedian) + 0.35 * (SmallShape - ShapeMedian);
    return clamp(Blend / max(1.0e-3, ShapeSpan) * 1.25, -1.0, 1.0);
}

// 📝 Convective shape field: cell field + glaciation-faded turret, faded at the floor / ceiling. Returns (shape field, anvil glaciation).
vec2 ResolveConvectiveShapeField(float CellField, float TurretShape, float HeightFraction)
{
    uint  Genus      = uint(CloudField.StratumGeometryPack.w + 0.5);
    float FloorFade  = EvaluateCloudSmoothStep(0.0, 0.04, HeightFraction) * (1.0 - EvaluateCloudSmoothStep(0.97, 1.0, HeightFraction));
    float Glaciation = (Genus == GenusCumulonimbus) ? EvaluateCloudSmoothStep(0.66, 0.84, HeightFraction) : 0.0;
    float ShapeField  = clamp((CellField + TurretShape * ResolveTurretAmplitude(Genus) * (1.0 - 0.75 * Glaciation)) * FloorFade / 0.45, 0.0, 1.0);
    return vec2(ShapeField, Glaciation);
}

// 📝 Inverted-Worley erosion of a shape field (mirror of ApplyDetailErosion): DetailFbm is the prefiltered detail fBm of the bound volume.
//    Its complement carves convex crowns; the lowest 20 % of the stratum erodes by the fBm itself (ragged floor wisps).
float ApplyDetailErosion(float ShapeField, float DetailFbm, float HeightFraction)
{
    float FloorBlend     = clamp(1.0 - HeightFraction * 5.0, 0.0, 1.0);
    float DetailModifier = (1.0 - DetailFbm) + (2.0 * DetailFbm - 1.0) * FloorBlend;
    float Threshold      = DetailModifier * CloudField.StratumNoisePack.z;
    float Eroded         = clamp((ShapeField - Threshold) / max(1.0e-6, 1.0 - Threshold), 0.0, 1.0);
    return ResolveSaturatedDensity(Eroded);
}

// 📝 Empty-space probe step [km] (mirror of ResolveCoarseStepKm): 1/24 of the stratum (≤ 200 m) near the viewer, then 1.5 % of range.
float ResolveCoarseStepKm(float ThicknessKm, float DistanceKm)
{
    return max(min(ThicknessKm / 24.0, 0.2), 0.015 * DistanceKm);
}

// 📝 In-cloud integration step [km] (mirror of ResolveFineStepKm): 25 m at the viewer growing with range, capped at 1/12 of the stratum.
float ResolveFineStepKm(float ThicknessKm, float DistanceKm)
{
    return min(ThicknessKm / 12.0, 0.025 + 0.0035 * DistanceKm);
}

// 📝 Final convective density: saturated base, eroded and blended back toward the smooth sheet across the glaciated anvil.
float ResolveConvectiveDensity(vec2 ShapeGlaciation, float DetailFbm, float HeightFraction)
{
    float Density = ResolveSaturatedDensity(ShapeGlaciation.x);
    if (Density <= 0.0)
        return 0.0;
    float Eroded = ApplyDetailErosion(ShapeGlaciation.x, DetailFbm, HeightFraction);
    return Density + (Eroded - Density) * (1.0 - 0.7 * ShapeGlaciation.y);
}

#endif
