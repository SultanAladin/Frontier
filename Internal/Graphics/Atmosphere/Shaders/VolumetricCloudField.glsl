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

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                  VOLUMETRIC CLOUD FIELD 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// GPU mirror of the texture-free optics of VolumetricCloudSolver.cpp (the flow field lives in AtmosphericFlowField.glsl, included here).
// Every function here is a line-for-line port of its C++ namesake so the CPU reference harness (AtmosphereCelestialValidation) stays the
// ground truth for the GPU path:
//   - std140 CloudFieldBlock (112 B) mirror of CloudFieldUniformBlock, plus the 32 B CrepuscularRayBlock
//   - approximate Mie phase (Jendersie & d'Eon 2023: HG diffraction peak + Draine lobe), Wrenninge multiple-scatter octaves with the δ-M
//     diffusion tail, Beer-Powder, two-stream ambient transmission
//   - signed-distance convective field: Poisson germ-grain thermal envelopes (dome, bubble-column tower, Quílez anvil wedge with
//     overshooting top) grown by a Quílez SDF fBm bubble cascade, flat condensation floor, penetrable fringe, Worley value erosion,
//     glaciation-faded detail, and the four sample fidelities (EnvelopeBound / AmbientCascade / ShadowCascade / FullDetail)
//   - cirrus ice phase (δ-M scaled HG mixture + 22° hexagonal-plate halo with prismatic dispersion) and genus height envelopes
//   - two-speed march steps, fixed-orientation solar shadow cone (uneroded taps) and two-hemisphere sky ambient
//   - crepuscular rays: boundary-layer haze march with cloud-shadowed solar in-scatter (the shaft deficit the sky composite subtracts)
// The shape/detail noise volumes and weather map are textures the consuming shader binds; they are not declared here. The cell field reads
// the weather map through a prototype the includer must define:  vec2 SampleCloudWeatherMap(vec2 TileCoord)  -> (coverage, top variation),
// TileCoord in weather-map tiles (REPEAT addressing). The full-detail erosion additionally needs the prefiltered detail fBm at the sample,
// which the includer samples from its own detail volume and passes in. Only FullDetail fidelity reads it, so the shadow and ambient taps
// (and the crepuscular march below them) stay texture-free. The transmittance LUT is likewise a prototype:
// vec3 SampleTransmittanceToSpace(float RadiusFromCentre, float ViewZenithCosine).
//
// The including shader picks the bindings:  #define FLOW_FIELD_BINDING n  /  #define CLOUD_FIELD_BINDING n  /
// #define CREPUSCULAR_FIELD_BINDING n  (defaults 1, 2 and 4; the celestial block owns 3).

#ifndef VOLUMETRIC_CLOUD_FIELD_GLSL
#define VOLUMETRIC_CLOUD_FIELD_GLSL

#ifndef CLOUD_FIELD_BINDING
#define CLOUD_FIELD_BINDING 2
#endif

#ifndef CREPUSCULAR_FIELD_BINDING
#define CREPUSCULAR_FIELD_BINDING 4
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
    vec4 PhasePack;             // x peak g; y Draine g; z Draine α; w Draine weight (ResolveApproximateMieParameters of the droplet size)
    vec4 CirrusPack;            // x altitude [km]; y coverage; z optical depth peak; w filament scale [km] (0 = veil disabled)
    vec4 QualityPack;           // x primary step limit; y solar cone taps; z trace distance [km]; w sequence seconds [s]
    vec4 LightingPack;          // x droplet diameter [µm]; y diffraction peak fraction (δ-M); z scatter octave count; w ambient gain
} CloudField;

// 📝 Sample fidelity of the stratum density (mirror of CloudDensityFidelity): the empty-space probe needs only a conservative bound, light
//    taps need the billow scale that casts self-shadow across turrets, primary samples need every footprint-resolved octave plus erosion.
const float FidelityEnvelopeBound = 0.0;
const float FidelityAmbientCascade = 1.0;
const float FidelityShadowCascade = 2.0;
const float FidelityFullDetail    = 3.0;

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
    float Eccentricity        = clamp(Asymmetry, -0.999, 0.999);
    float EccentricitySquared = Eccentricity * Eccentricity;
    float Denominator         = max(1.0e-7, 1.0 + EccentricitySquared - 2.0 * Eccentricity * CosineAngle);
    return (1.0 - EccentricitySquared) / (4.0 * CloudPi * Denominator * sqrt(Denominator));
}

// 📝 Approximate Mie phase function (Jendersie & d'Eon 2023): φ = (1 - w) · HG(g_peak) + w · Draine(g_D, α). The narrow HG lobe carries the
//    droplet diffraction peak, the Draine lobe (HG × (1 + α cos²θ), exactly normalised) the broad water-droplet body. PhasePack holds the CPU
//    fit of ResolveApproximateMieParameters for the configured droplet diameter, so the shader needs no transcendental fits of its own;
//    EccentricityScale flattens the lobes through the multiple-scattering octaves.
float EvaluateApproximateMiePhase(float CosineAngle, float EccentricityScale)
{
    float PeakG   = clamp(CloudField.PhasePack.x * EccentricityScale, -0.999, 0.999);
    float DraineG = clamp(CloudField.PhasePack.y * EccentricityScale, -0.999, 0.999);
    float Alpha   = CloudField.PhasePack.z;
    float Normaliser = 1.0 + Alpha * (1.0 + 2.0 * DraineG * DraineG) / 3.0;
    float Draine  = EvaluateHenyeyGreenstein(CosineAngle, DraineG) * (1.0 + Alpha * CosineAngle * CosineAngle) / Normaliser;
    return (1.0 - CloudField.PhasePack.w) * EvaluateHenyeyGreenstein(CosineAngle, PeakG) + CloudField.PhasePack.w * Draine;
}

// 📝 Wrenninge (2013) multiple-scattering octaves: a = b = c = 0.5 on extinction, energy, and eccentricity.
float EvaluateScatterOctaves(float CosineScatter, float SunOpticalDepth)
{
    uint  Octaves    = clamp(uint(CloudField.LightingPack.z + 0.5), 1u, 8u);
    float ScatterSum = 0.0;
    float Extinction = 1.0;
    float Energy     = 1.0;
    float Eccentric  = 1.0;
    for (uint Octave = 0u; Octave < Octaves; ++Octave)
    {
        ScatterSum += Energy * EvaluateApproximateMiePhase(CosineScatter, Eccentric) * exp(-Extinction * SunOpticalDepth);
        Extinction *= 0.5;
        Energy     *= 0.5;
        Eccentric  *= 0.5;
    }
    // δ-M scaled droplet asymmetry g' ≈ 0.70 (the Draine lobe once the diffraction peak is folded into the extinction): the two-stream
    // diffusion tail the octaves miss deep inside a cumulus.
    float DiffuseTransmission = 1.0 / (1.0 + 0.75 * (1.0 - 0.70) * SunOpticalDepth);
    return ScatterSum + 0.3 / CloudPi * DiffuseTransmission * (1.0 - exp(-0.5 * SunOpticalDepth));
}

// 📝 δ-M: the g ≈ 0.99 lobe carries the diffraction peak (≲ 1° wide), indistinguishable from the unscattered beam, so that fraction leaves
//    the extinction seen by the sunlight paths. LightingPack.y carries the fraction the CPU derived from the Draine weight.
float ResolveDiffractionPeakFraction()
{
    return clamp(CloudField.LightingPack.y, 0.0, 0.9);
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
// SIGNED-DISTANCE CONVECTIVE FIELD (mirror of EvaluateConvectiveEnvelopeDistance / ApplyBubbleCascade / EvaluateStratumDensityAdvected)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

vec2 SampleCloudWeatherMap(vec2 TileCoord);
vec3 SampleTransmittanceToSpace(float RadiusFromCentre, float ViewZenithCosine);

// 📝 Local coverage from the weather map and the flow condensation (mirror of ResolveLocalCoverage).
float ResolveLocalCoverage(float WeatherCoverage, float CondensationDelta)
{
    float Coverage = clamp(CloudField.StratumGeometryPack.z, 0.0, 1.0);
    if (uint(CloudField.StratumGeometryPack.w + 0.5) == GenusStratus)
        return clamp(Coverage + (WeatherCoverage - 0.5) * 0.25 * min(1.0, 4.0 * (1.0 - Coverage)), 0.0, 1.0);
    return clamp(Coverage + (WeatherCoverage - 0.5) * 1.10 * (1.0 - 0.6 * Coverage * Coverage) + CondensationDelta * 0.25, 0.0, 1.0);
}

// 📝 Polynomial smooth minimum of two signed distances [km] over a blend width [km] (Quílez): rounded fillets.
float BlendDistanceUnion(float Left, float Right, float BlendWidth)
{
    float Overlap = max(BlendWidth - abs(Left - Right), 0.0) / max(1.0e-6, BlendWidth);
    return min(Left, Right) - Overlap * Overlap * BlendWidth * 0.25;
}

// 📝 Polynomial smooth maximum of two signed distances [km] (rounded intersection).
float BlendDistanceIntersection(float Left, float Right, float BlendWidth)
{
    float Overlap = max(BlendWidth - abs(Left - Right), 0.0) / max(1.0e-6, BlendWidth);
    return max(Left, Right) + Overlap * Overlap * BlendWidth * 0.25;
}

// 📝 Signed distance to an axis-aligned ellipsoid with semi-axes (RadiusX, RadiusY, RadiusZ) [km] (Quílez k0 (k0 - 1) / k1).
float EvaluateEllipsoidDistance(float OffsetX, float OffsetY, float OffsetZ, float RadiusX, float RadiusY, float RadiusZ)
{
    float NormalX   = OffsetX / RadiusX;
    float NormalY   = OffsetY / RadiusY;
    float NormalZ   = OffsetZ / RadiusZ;
    float Ratio     = sqrt(NormalX * NormalX + NormalY * NormalY + NormalZ * NormalZ);
    if (Ratio < 1.0e-4)
        return -min(RadiusX, min(RadiusY, RadiusZ));
    float GradientX = NormalX / RadiusX;
    float GradientY = NormalY / RadiusY;
    float GradientZ = NormalZ / RadiusZ;
    return Ratio * (Ratio - 1.0) / sqrt(GradientX * GradientX + GradientY * GradientY + GradientZ * GradientZ);
}

// 📝 One bubble octave (the Quílez per-octave lattice distance): each corner of the unit lattice cell carries a sphere of hashed
//    radius in [0, 0.5); the value is the nearest sphere surface in lattice units.
float EvaluateBubbleLatticeDistance(vec3 Coord, uint Salt)
{
    vec3  Floored = floor(Coord);
    ivec3 Cell    = ivec3(Floored);
    vec3  Delta   = Coord - Floored;
    float Nearest = 1.0e9;
    for (int Corner = 0; Corner < 8; ++Corner)
    {
        ivec3 CornerOffset = ivec3(Corner & 1, (Corner >> 1) & 1, Corner >> 2);
        float Radius       = 0.5 * HashLatticeCell(Cell.x + CornerOffset.x, Cell.y + CornerOffset.y, Cell.z + CornerOffset.z, Salt);
        vec3  ToCorner     = Delta - vec3(CornerOffset);
        Nearest = min(Nearest, sqrt(dot(ToCorner, ToCorner)) - Radius);
    }
    return Nearest;
}

// 📝 Smooth-union radius of a cascade octave in lattice units: always larger than the protrusion, or shell bubbles float free.
float ResolveCascadeBlend(float Protrusion)
{
    return max(0.3, Protrusion + 0.14);
}

// 📝 Bubble cascade (Quílez SDF fBm): every octave unions a lattice of random spheres clipped to a shell just outside the current surface,
//    then halves the lattice and rotates the domain (an exact 2 × rotation). Bubbles therefore grow on the surface of the octave before
//    them — thermal → turret → sub-turret → cauliflower (Bouthors & Neyret 2004). A fractional OctaveBudget fades the last octave in by
//    shrinking its bubbles and their shell (cross-fading distances leaves free-floating beads).
float ApplyBubbleCascade(vec3 PositionKm, float EnvelopeDistanceKm, float CellKm, float Protrusion, float OctaveBudget, uint Salt)
{
    float  Distance = EnvelopeDistanceKm;
    float  Scale    = CellKm;
    vec3   Coord    = PositionKm / CellKm;
    uint   FullOctaves = uint(max(0.0, OctaveBudget));
    float  Partial  = OctaveBudget - float(FullOctaves);
    float  Blend    = ResolveCascadeBlend(Protrusion);
    for (uint Octave = 0u; Octave <= FullOctaves && Octave < 6u; ++Octave)
    {
        float Weight = (Octave < FullOctaves) ? 1.0 : Partial;
        if (Weight <= 0.0 || Distance > 2.0 * Scale * (Protrusion + 0.25 * Blend))
            break;
        float Bubble = Scale * (EvaluateBubbleLatticeDistance(Coord, Salt + 977u * Octave) + 0.5 * (1.0 - Weight));
        Bubble       = BlendDistanceIntersection(Bubble, Distance - Protrusion * Weight * Scale, 0.3 * Scale);
        Distance     = BlendDistanceUnion(Bubble, Distance, Blend * Scale);
        Coord = vec3( 0.00 * Coord.x - 1.60 * Coord.y - 1.20 * Coord.z,
                      1.60 * Coord.x + 0.72 * Coord.y - 0.96 * Coord.z,
                      1.20 * Coord.x - 0.96 * Coord.y + 1.28 * Coord.z);
        Scale *= 0.5;
    }
    return Distance;
}

// 📝 First bubble lattice spacing [km] per genus: about half a thermal radius, so the first octave reads as turrets.
float ResolveCascadeCellKm()
{
    uint  Genus  = uint(CloudField.StratumGeometryPack.w + 0.5);
    float Shape  = max(0.1, CloudField.StratumNoisePack.x);
    if (Genus == GenusCumulonimbus)
        return max(0.2, Shape * 0.26);
    return (Genus == GenusStratocumulus) ? max(0.1, Shape * 0.30) : max(0.1, Shape * 0.36);
}

// 📝 Bubble protrusion (shell depth per octave): cauliflower Cumulus / Cumulonimbus, flatter Stratocumulus rolls.
float ResolveCascadeProtrusion(uint Genus)
{
    return (Genus == GenusStratocumulus) ? 0.18 : 0.24;
}

float ResolveCascadeGrowthKm(float CellKm, float Protrusion)
{
    return 2.0 * CellKm * (Protrusion + 0.25 * ResolveCascadeBlend(Protrusion));
}

// 📝 Penetrable fringe [km]: depth over which the dimensional profile rises from 0 at the cascade surface to 1 inside; the noise erosion
//    acts only within it (Nubis value erosion), widening with the sample footprint for LOD.
float ResolveFringeKm(float SampleFootprintKm)
{
    return clamp(max(CloudField.StratumNoisePack.y * 0.6, 1.5 * SampleFootprintKm), 0.06, 0.6);
}

// 📝 Fair-weather thermal envelope [km]: a flat-based dome plus two hashed flanking lobes merged through rounded fillets, so no two cumulus
//    share a silhouette. Offsets are in km about the thermal centre.
float EvaluateThermalDomeDistance(float OffsetX, float HeightKm, float OffsetZ, float RadiusKm, float TopKm, int LatticeX, int LatticeZ)
{
    float Distance = EvaluateEllipsoidDistance(OffsetX, HeightKm, OffsetZ, RadiusKm, TopKm, RadiusKm);
    for (int Lobe = 0; Lobe < 2; ++Lobe)
    {
        float Azimuth    = CloudTau * HashLatticeCell(LatticeX, 10 + Lobe, LatticeZ, 7103u);
        float LobeRadius = RadiusKm * (0.50 + 0.25 * HashLatticeCell(LatticeX, 12 + Lobe, LatticeZ, 7211u));
        float LobeTop    = TopKm * (0.45 + 0.40 * HashLatticeCell(LatticeX, 14 + Lobe, LatticeZ, 7307u));
        float LobeX      = OffsetX - 0.55 * RadiusKm * cos(Azimuth);
        float LobeZ      = OffsetZ - 0.55 * RadiusKm * sin(Azimuth);
        Distance = BlendDistanceUnion(Distance, EvaluateEllipsoidDistance(LobeX, HeightKm, LobeZ, LobeRadius, LobeTop, LobeRadius), 0.35 * RadiusKm);
    }
    return Distance;
}

// 📝 Towering-cumulus column [km]: a leaning stack of successively released thermal bubbles whose radii shrink with height (the updraft
//    narrows as it entrains dry air) and whose centres random-walk sideways, so the silhouette bulges and pinches instead of rising as a
//    straight pillar. Flat condensation base under the lowest bubble.
float EvaluateBubbleColumnDistance(float OffsetX, float HeightKm, float OffsetZ, float RadiusKm, float TopKm, int LatticeX, int LatticeZ, uint Salt)
{
    int   BubbleCount = int(clamp(ceil(TopKm / (1.1 * RadiusKm)), 1.0, 6.0));
    float Segment     = TopKm / float(BubbleCount);
    float CentreX     = 0.0;
    float CentreZ     = 0.0;
    float Distance    = 1.0e9;
    for (int Bubble = 0; Bubble < BubbleCount; ++Bubble)
    {
        float Rise       = (float(Bubble) + 0.5) / float(BubbleCount);
        float Taper      = 1.0 - 0.42 * Rise;
        float Radius     = RadiusKm * Taper * (0.72 + 0.55 * HashLatticeCell(LatticeX, 40 + Bubble, LatticeZ, Salt));
        float HalfHeight = min(Radius, 0.75 * Segment + 0.25 * Radius);
        float CentreY    = (Bubble == BubbleCount - 1) ? TopKm - HalfHeight : (float(Bubble) + 0.55) * Segment;
        Distance = BlendDistanceUnion(Distance,
            EvaluateEllipsoidDistance(OffsetX - CentreX, HeightKm - CentreY, OffsetZ - CentreZ, Radius, HalfHeight, Radius * 0.92),
            0.22 * Radius);
        float Heading = CloudTau * HashLatticeCell(LatticeX, 50 + Bubble, LatticeZ, Salt + 11u);
        float Drift   = 0.50 * Radius * HashLatticeCell(LatticeX, 60 + Bubble, LatticeZ, Salt + 23u);
        CentreX += Drift * cos(Heading);
        CentreZ += Drift * sin(Heading);
    }
    return BlendDistanceIntersection(Distance, -HeightKm, 0.05 * RadiusKm);
}

// 📝 Mature cumulonimbus envelope [km]: a leaning, upward-widening updraft column with a rounded crown and flanking towers, an anvil wedge
//    flat on top at the tropopause that fans out and thins downwind along the shear (+X), and an overshooting dome above the anvil.
//    OutAnvilWeight reports how much of the local distance belongs to the glaciated anvil (smooth, fibrous detail).
float EvaluateStormTowerDistance(float OffsetX, float HeightKm, float OffsetZ, float RadiusKm, float TopKm, int LatticeX, int LatticeZ,
                                 float AnvilSpread, out float OutAnvilWeight)
{
    float Lean  = 0.25 * RadiusKm * (HashLatticeCell(LatticeX, 20, LatticeZ, 7411u) - 0.3) * min(1.0, HeightKm / TopKm);
    float AxisX = OffsetX - Lean;
    float Tower = EvaluateBubbleColumnDistance(OffsetX - Lean, HeightKm, OffsetZ, 0.95 * RadiusKm, TopKm * 0.97, LatticeX, LatticeZ, 7451u);
    for (int Flank = 0; Flank < 3; ++Flank)
    {
        float Azimuth  = CloudTau * HashLatticeCell(LatticeX, 22 + Flank, LatticeZ, 7517u);
        float FlankRad = RadiusKm * (0.42 + 0.22 * HashLatticeCell(LatticeX, 25 + Flank, LatticeZ, 7621u));
        float FlankTop = TopKm * (0.22 + 0.38 * HashLatticeCell(LatticeX, 28 + Flank, LatticeZ, 7723u));
        float FlankX   = OffsetX - 0.85 * RadiusKm * cos(Azimuth);
        float FlankZ   = OffsetZ - 0.85 * RadiusKm * sin(Azimuth);
        Tower = BlendDistanceUnion(Tower, EvaluateEllipsoidDistance(FlankX, HeightKm, FlankZ, FlankRad, FlankTop, FlankRad), 0.4 * RadiusKm);
    }
    float Length    = RadiusKm * (2.2 + 3.0 * AnvilSpread);
    float Back      = RadiusKm * 0.9;
    float Along     = clamp((OffsetX + Back) / (Length + Back), 0.0, 1.0);
    float HalfWidth = RadiusKm * (1.0 + (0.2 + 1.6 * AnvilSpread) * Along);
    float Thickness = TopKm * (0.19 - 0.14 * Along);
    // Plan-ellipse distance (Quílez k0 (k0 - 1) / k1): scaling by the minor axis instead would under-report the distance along the long
    // axis, widen the cascade shell there, and let bubbles float detached beside the anvil edge.
    float SemiMajor = 0.5 * (Length + Back);
    float PlanX     = OffsetX - 0.5 * (Length - Back);
    float NormalK0  = sqrt((PlanX / SemiMajor) * (PlanX / SemiMajor) + (OffsetZ / HalfWidth) * (OffsetZ / HalfWidth));
    float NormalK1  = sqrt((PlanX / (SemiMajor * SemiMajor)) * (PlanX / (SemiMajor * SemiMajor))
                           + (OffsetZ / (HalfWidth * HalfWidth)) * (OffsetZ / (HalfWidth * HalfWidth)));
    float PlanDist  = NormalK0 * (NormalK0 - 1.0) / max(1.0e-6, NormalK1);
    float SlabDist  = max(TopKm - Thickness - HeightKm, HeightKm - TopKm);
    float Anvil     = BlendDistanceIntersection(PlanDist, SlabDist, 0.6 * Thickness);
    float Overshoot = EvaluateEllipsoidDistance(AxisX - 0.2 * RadiusKm, HeightKm - TopKm, OffsetZ, 0.5 * RadiusKm, 0.09 * TopKm, 0.5 * RadiusKm);
    float Crown     = BlendDistanceUnion(Anvil, Overshoot, 0.3 * RadiusKm);
    OutAnvilWeight  = EvaluateCloudSmoothStep(0.5 * RadiusKm, -0.5 * RadiusKm, Crown - Tower);
    return BlendDistanceUnion(Tower, Crown, 0.8 * RadiusKm);
}

// 📝 Convective envelope distance [km] for Cumulus / Stratocumulus / Cumulonimbus at an advected horizontal position [km] and a height above
//    the condensation level [km]. Thermals come from a Boolean germ-grain model on a jittered 2D lattice (spacing = shape scale) whose
//    Poisson disc coverage is inverted so the configured local coverage is met. Coverage is evaluated at the thermal CENTRE, or the coverage
//    gradient would slice one thermal into flat walls. OutGlaciation is the anvil (ice) weight at the point.
float EvaluateConvectiveEnvelopeDistance(float HorizontalX, float HorizontalZ, float HeightKm, float CondensationDelta, out float OutGlaciation)
{
    uint  Genus          = uint(CloudField.StratumGeometryPack.w + 0.5);
    bool  TowerCondition = Genus == GenusCumulonimbus;
    bool  RollCondition  = Genus == GenusStratocumulus;
    float Spacing        = max(0.1, CloudField.StratumNoisePack.x);
    float Thickness      = max(0.05, CloudField.StratumGeometryPack.y - CloudField.StratumGeometryPack.x);
    float AnvilSpread    = CloudField.StratumOpticsPack.z;
    float RadiusRatio    = TowerCondition ? 0.36 : (RollCondition ? 0.42 : 0.36);
    float DiscArea       = CloudPi * RadiusRatio * RadiusRatio;
    float CascadeGrowth  = ResolveCascadeGrowthKm(ResolveCascadeCellKm(), ResolveCascadeProtrusion(Genus)) / Spacing;
    int   Reach          = TowerCondition ? 2 : 1;
    float RadiusLimit    = TowerCondition ? (1.98 - CascadeGrowth) / (2.2 + 3.0 * AnvilSpread) : (0.98 - CascadeGrowth) / (RollCondition ? 1.7 : 1.32);
    float CellX          = HorizontalX / Spacing;
    float CellZ          = HorizontalZ / Spacing;
    int   AnchorX        = int(floor(CellX));
    int   AnchorZ        = int(floor(CellZ));
    float WeatherTile    = max(1.0, CloudField.StratumNoisePack.w * 4.0);
    float BestDistance   = 1.0e9;
    float Glaciation     = 0.0;
    for (int OffsetZ = -Reach; OffsetZ <= Reach; ++OffsetZ)
    {
        for (int OffsetX = -Reach; OffsetX <= Reach; ++OffsetX)
        {
            int   LatticeX  = AnchorX + OffsetX;
            int   LatticeZ  = AnchorZ + OffsetZ;
            float CentreX   = float(LatticeX) + HashLatticeCell(LatticeX, 1, LatticeZ, 6203u);
            float CentreZ   = float(LatticeZ) + HashLatticeCell(LatticeX, 2, LatticeZ, 6307u);
            vec2  Weather   = SampleCloudWeatherMap(vec2(CentreX, CentreZ) * Spacing / WeatherTile);
            float Coverage  = ResolveLocalCoverage(Weather.x, CondensationDelta);
            float Occupancy = -log(1.0 - min(Coverage, 0.985)) / DiscArea;
            float RadiusGain = 1.0;
            if (Occupancy > 1.0)
            {
                RadiusGain = sqrt(Occupancy);
                Occupancy  = 1.0;
            }
            float Existence = clamp((Occupancy - HashLatticeCell(LatticeX, 0, LatticeZ, 6101u)) / 0.12, 0.0, 1.0);
            if (Existence <= 0.0)
                continue;
            float RadiusCells = min(RadiusLimit, RadiusGain * RadiusRatio * (0.7 + 0.6 * HashLatticeCell(LatticeX, 3, LatticeZ, 6409u)))
                                * (0.35 + 0.65 * sqrt(Existence));
            float RadiusKm    = RadiusCells * Spacing;
            float TopHash     = HashLatticeCell(LatticeX, 4, LatticeZ, 6511u);
            float LocalX      = (CellX - CentreX) * Spacing;
            float LocalZ      = (CellZ - CentreZ) * Spacing;
            float PlanarGap   = sqrt(LocalX * LocalX + LocalZ * LocalZ) - RadiusKm * (TowerCondition ? 2.2 + 3.0 * AnvilSpread : 1.7);
            if (PlanarGap > BestDistance + 0.5 * RadiusKm)
                continue;

            float Distance  = 1.0e9;
            float AnvilPart = 0.0;
            if (TowerCondition)
            {
                if (TopHash > 0.66)
                    Distance = EvaluateStormTowerDistance(LocalX, HeightKm, LocalZ, RadiusKm, Thickness * (0.90 + 0.08 * (TopHash - 0.66) / 0.34),
                                                           LatticeX, LatticeZ, AnvilSpread, AnvilPart);
                else
                {
                    float CongestusTop = Thickness * (0.10 + 0.35 * TopHash / 0.66);
                    Distance = (CongestusTop > 1.4 * RadiusKm * 0.75)
                                   ? EvaluateBubbleColumnDistance(LocalX, HeightKm, LocalZ, RadiusKm * 0.75, CongestusTop, LatticeX, LatticeZ, 7561u)
                                   : EvaluateThermalDomeDistance(LocalX, HeightKm, LocalZ, RadiusKm * 0.75, CongestusTop, LatticeX, LatticeZ);
                }
            }
            else if (RollCondition)
            {
                Distance = EvaluateEllipsoidDistance(LocalX, HeightKm, LocalZ, RadiusKm * 1.6, Thickness * (0.62 + 0.38 * TopHash), RadiusKm);
            }
            else
            {
                float TopKm = Thickness * clamp(0.30 + 0.40 * TopHash + 0.30 * Weather.y, 0.25, 1.0);
                Distance = EvaluateThermalDomeDistance(LocalX, HeightKm, LocalZ, RadiusKm, TopKm, LatticeX, LatticeZ);
            }
            if (Distance < BestDistance)
                Glaciation = AnvilPart;
            BestDistance = BlendDistanceUnion(BestDistance, Distance, 0.25 * Spacing * RadiusRatio);
        }
    }
    OutGlaciation = Glaciation;
    return BestDistance;
}

// 📝 Worley value erosion of a fringe profile (mirror of ApplyDetailErosion): the complement of the prefiltered detail fBm carves convex
//    crowns; the lowest 20 % of the stratum erodes by the fBm itself (ragged floor wisps). DetailFbm comes from the includer's detail
//    volume; StrengthScale fades the erosion out over a glaciated anvil.
float ApplyDetailErosion(float Profile, float DetailFbm, float HeightFraction, float StrengthScale)
{
    float FloorBlend     = clamp(1.0 - HeightFraction * 5.0, 0.0, 1.0);
    float DetailModifier = (1.0 - DetailFbm) + (2.0 * DetailFbm - 1.0) * FloorBlend;
    float Threshold      = DetailModifier * CloudField.StratumNoisePack.z * StrengthScale;
    return clamp((Profile - Threshold) / max(1.0e-6, 1.0 - Threshold), 0.0, 1.0);
}

// 📝 Convective stratum density at an already-advected planet-local position (mirror of the convective branch of
//    EvaluateStratumDensityAdvected): ① thermal envelopes as a signed distance, ② bubble cascade grown on their surface, ③ flat
//    condensation floor (and the Stratocumulus inversion lid), ④ dimensional profile across the penetrable fringe, ⑤ Worley value erosion
//    of the fringe only. Fidelity picks the octave budget and the erosion (see the Fidelity* constants above).
float EvaluateConvectiveDensity(float HorizontalX, float HorizontalZ, float AltitudeKm, float CondensationDelta, float Fidelity,
                                float SampleFootprintKm, float DetailFbm)
{
    uint  Genus     = uint(CloudField.StratumGeometryPack.w + 0.5);
    float Thickness = max(0.05, CloudField.StratumGeometryPack.y - CloudField.StratumGeometryPack.x);
    float HeightFraction = (AltitudeKm - CloudField.StratumGeometryPack.x) / Thickness;
    if (HeightFraction <= 0.0 || HeightFraction >= 1.0)
        return 0.0;
    float HeightKm   = AltitudeKm - CloudField.StratumGeometryPack.x;
    float Glaciation = 0.0;
    float Envelope   = EvaluateConvectiveEnvelopeDistance(HorizontalX, HorizontalZ, HeightKm, CondensationDelta, Glaciation);
    float CellKm     = ResolveCascadeCellKm();
    // Bubbles bulge upward and sideways; the lowest few hundred metres stay smooth so the base reads as one flat, darker floor, and the
    // glaciated anvil keeps only a gentle undulation.
    float Protrusion = ResolveCascadeProtrusion(Genus) * (1.0 - 0.9 * Glaciation) * (0.3 + 0.7 * EvaluateCloudSmoothStep(0.0, 0.6, HeightKm));
    float EdgeFringe = ResolveFringeKm(SampleFootprintKm) * (1.0 - 0.6 * Glaciation);
    float Growth     = ResolveCascadeGrowthKm(CellKm, ResolveCascadeProtrusion(Genus));
    if (Envelope - Growth >= 0.0)
        return 0.0;
    // Liquid surface ramp: once the fringe has been carved, liquid water closes within ~25 m of the eroded iso-surface — crisp
    // cauliflower texture rather than a soft glow — widening with the footprint for LOD.
    float SurfaceRamp = clamp(max(0.05, 0.6 * SampleFootprintKm) / EdgeFringe, 0.12, 1.0);
    if (Fidelity == FidelityEnvelopeBound)
        return clamp((Growth - Envelope) / (EdgeFringe * SurfaceRamp), 0.0, 1.0);

    float OctaveBudget = (Fidelity == FidelityAmbientCascade) ? 1.0 : 2.0;
    if (Fidelity == FidelityFullDetail)
        OctaveBudget = clamp(log2(CellKm / max(0.012, 1.5 * SampleFootprintKm)) + 1.0, 1.0, 5.0);
    float Distance = ApplyBubbleCascade(vec3(HorizontalX, HeightKm, HorizontalZ), Envelope, CellKm, Protrusion, OctaveBudget, 8101u);
    // Condensation floor: flat at the lifting condensation level with a gentle 0..90 m moisture undulation, so the underside reads as a
    // mottled ceiling rather than a polished plane; Stratocumulus is also lidded by the inversion.
    float FloorLift = 0.09 * EvaluateQuinticNoise3D(vec3(HorizontalX / 0.45, 0.5, HorizontalZ / 0.45), 8209u);
    Distance = BlendDistanceIntersection(Distance, FloorLift - HeightKm, 0.04);
    if (Genus == GenusStratocumulus)
        Distance = BlendDistanceIntersection(Distance, HeightKm - 0.97 * Thickness, 0.1);
    float Profile = clamp(-Distance / EdgeFringe, 0.0, 1.0);
    if (Profile <= 0.0)
        return 0.0;
    if (Fidelity != FidelityFullDetail)
    {
        // Light taps see the mean erosion depth (half the strength) instead of the per-point noise.
        float MeanCut = 0.5 * CloudField.StratumNoisePack.z * (1.0 - 0.95 * Glaciation);
        return EvaluateCloudSmoothStep(0.0, SurfaceRamp, (Profile - MeanCut) / (1.0 - MeanCut));
    }
    float Eroded = ApplyDetailErosion(Profile, DetailFbm, HeightFraction, 1.0 - 0.95 * Glaciation);
    return EvaluateCloudSmoothStep(0.0, SurfaceRamp, Eroded);
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

// 📝 Conservative density bound for the empty-space probe (mirror of EvaluateCloudDensityBound): the EnvelopeBound fidelity, whose value is
//    an upper bound on every finer fidelity, so a probe that finds it empty is guaranteed to be.
float EvaluateCloudDensityBound(float HorizontalX, float HorizontalZ, float AltitudeKm, float CondensationDelta)
{
    return EvaluateConvectiveDensity(HorizontalX, HorizontalZ, AltitudeKm, CondensationDelta, FidelityEnvelopeBound, 0.0, 0.5);
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// CREPUSCULAR RAYS (mirror of IntegrateCrepuscularRays)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

// 📝 Boundary-layer haze march: single-scattered sunlight and sky light in the aerosol, darkened wherever the cloud deck shadows the parcel.
//    The caller subtracts OutDeficit from its clean-air sky radiance (the shafts) and composites OutHaze / OutTransmittance over the clouds.
layout(std140, binding = CREPUSCULAR_FIELD_BINDING) uniform CrepuscularRayBlock
{
    vec4 HazePack;    // x haze extinction at the surface [1/km]; y scale height [km]; z HG g; w single-scatter albedo
    vec4 MarchPack;   // x maximum distance [km]; y step count; z enabled (0 / 1); w reserved
} CrepuscularRay;

// 📝 Straight-ray / concentric-sphere intersection in the local tangent frame (mirror of IntersectConcentricSphere): returns the near and
//    far roots of |Viewer + Direction · t| = SphereRadius through OutNear / OutFar.
bool IntersectConcentricSphere(vec3 Viewer, vec3 Direction, float SphereRadius, out float OutNear, out float OutFar)
{
    float HalfProjection = dot(Viewer, Direction);
    float Discriminant   = HalfProjection * HalfProjection - (dot(Viewer, Viewer) - SphereRadius * SphereRadius);
    if (Discriminant <= 0.0)
    {
        OutNear = 0.0;
        OutFar  = 0.0;
        return false;
    }
    float Depth = sqrt(Discriminant);
    OutNear = -HalfProjection - Depth;
    OutFar  = -HalfProjection + Depth;
    return true;
}

// 📝 Distant cloud shadow of a planet-local point [km] (mirror of the stratum term of EvaluateCloudShadowTransmittance): 16 coarse taps up the
//    solar axis through the stratum, read at ShadowCascade fidelity (turret-scale billows cast the shadow, the fine ripples do not). The
//    flow advection and the cirrus veil term of the CPU reference are left to the includer, which owns those volumes.
float EvaluateCloudShadowTransmittance(vec3 PlanetPositionKm, vec3 SolarAxis, float SequenceSeconds)
{
    float Radius     = length(PlanetPositionKm);
    vec3  Up         = PlanetPositionKm / max(1.0, Radius);
    float SunCosine  = dot(Up, SolarAxis);
    if (SunCosine <= 0.01)
        return 0.0;
    float FloorDistance   = max(0.0, (Atmosphere.BottomRadius + CloudField.StratumGeometryPack.x - Radius) / SunCosine);
    float CeilingDistance = max(0.0, (Atmosphere.BottomRadius + CloudField.StratumGeometryPack.y - Radius) / SunCosine);
    if (CloudField.StratumGeometryPack.y <= CloudField.StratumGeometryPack.x || CeilingDistance <= FloorDistance)
        return 1.0;
    const uint  ShadowSteps = 16u;
    float       StepKm      = (CeilingDistance - FloorDistance) / float(ShadowSteps);
    float       OpticalDepth = 0.0;
    for (uint Step = 0u; Step < ShadowSteps; ++Step)
    {
        vec3 SamplePoint = PlanetPositionKm + SolarAxis * (FloorDistance + (float(Step) + 0.5) * StepKm);
        // Horizontal km offsets about the planet origin: the stratum is sampled in its own tangent frame, 1 km of altitude spans ~1 km of
        // curvature error at these distances, which the envelope blend absorbs.
        OpticalDepth += EvaluateConvectiveDensity(SamplePoint.x, SamplePoint.z, SamplePoint.y - Atmosphere.BottomRadius, 0.0,
                                                   FidelityShadowCascade, StepKm, 0.5)
                        * CloudField.StratumOpticsPack.x * StepKm;
    }
    return exp(-OpticalDepth);
}

vec3 IntegrateCrepuscularRays(vec3 LocalDirection, float SequenceSeconds, float StartJitter, vec3 HazeAmbient,
                             out vec3 OutHazeRadiance, out float OutHazeTransmittance)
{
    OutHazeRadiance     = vec3(0.0);
    OutHazeTransmittance = 1.0;
    vec3  Deficit = vec3(0.0);
    if (CrepuscularRay.MarchPack.z < 0.5 || CloudField.StratumGeometryPack.z <= 0.0)
        return Deficit;
    vec3  SolarAxis = normalize(Atmosphere.SolarDirection.xyz);
    if (SolarAxis.y <= 0.01)
        return Deficit;

    float ViewerRadius = Atmosphere.BottomRadius + Atmosphere.OzoneAbsorptionPeak.w;
    vec3  Viewer       = vec3(0.0, ViewerRadius, 0.0);
    float FloorRadius  = Atmosphere.BottomRadius + CloudField.StratumGeometryPack.x;
    float EndKm        = max(0.5, CrepuscularRay.MarchPack.x);
    float FloorNear    = 0.0, FloorFar = 0.0;
    if (ViewerRadius < FloorRadius && IntersectConcentricSphere(Viewer, LocalDirection, FloorRadius, FloorNear, FloorFar))
        EndKm = min(EndKm, FloorFar);
    float GroundNear = 0.0, GroundFar = 0.0;
    if (IntersectConcentricSphere(Viewer, LocalDirection, Atmosphere.BottomRadius, GroundNear, GroundFar) && GroundNear > 0.0)
        EndKm = min(EndKm, GroundNear);

    float        CosineScatter = dot(LocalDirection, SolarAxis);
    float        RayleighPhase = EvaluateRayleighPhase(CosineScatter);
    float        AerosolPhase  = EvaluateCornetteShanksPhase(CosineScatter, Atmosphere.MieAsymmetry);
    float        HazePhase     = EvaluateHenyeyGreenstein(CosineScatter, CrepuscularRay.HazePack.z);
    vec3         SolarFlux     = Atmosphere.SolarIlluminance.rgb;
    uint         StepCount     = uint(clamp(CrepuscularRay.MarchPack.y, 4.0, 128.0));
    vec3         AirOpticalDepth = vec3(0.0);
    float        HazeOpticalDepth = 0.0;
    float        PreviousKm       = 0.0;
    // Quadratic sample spacing: shafts are sharpest close to the viewer, where a step subtends the largest angle.
    for (uint Step = 0u; Step < StepCount; ++Step)
    {
        float Fraction = (float(Step) + clamp(StartJitter, 0.0, 1.0)) / float(StepCount);
        float SampleKm = EndKm * Fraction * Fraction;
        float Next     = float(Step) + 1.0;
        float BoundKm  = EndKm * (Next / float(StepCount)) * (Next / float(StepCount));
        float StepKm   = BoundKm - PreviousKm;
        PreviousKm     = BoundKm;

        vec3  Position   = Viewer + LocalDirection * SampleKm;
        float Radius     = length(Position);
        float AltitudeKm = Radius - Atmosphere.BottomRadius;
        // Mirror of EvaluateMediumCoefficients: the Rayleigh / aerosol split of the shared profile helper, in per-km units.
        float RayleighDensity = EvaluateProfileDensity(Atmosphere.RayleighProfileLayer0, Atmosphere.RayleighProfileLayer1, AltitudeKm);
        float MieDensity      = EvaluateProfileDensity(Atmosphere.MieProfileLayer0,      Atmosphere.MieProfileLayer1,      AltitudeKm);
        float OzoneDensity    = EvaluateProfileDensity(Atmosphere.OzoneProfileLayer0,    Atmosphere.OzoneProfileLayer1,    AltitudeKm);
        vec3  RayleighScatter = Atmosphere.RayleighScatteringSeaLevel.rgb * RayleighDensity;
        vec3  MieScatter      = Atmosphere.MieScatteringSeaLevel.rgb      * MieDensity;
        vec3  Extinction      = (RayleighScatter + Atmosphere.MieExtinctionSeaLevel.rgb * MieDensity
                                 + Atmosphere.OzoneAbsorptionPeak.rgb * OzoneDensity) * 1000.0;
        vec3  AirScatterPerKm = (RayleighScatter * RayleighPhase + MieScatter * AerosolPhase) * 1000.0;
        float HazeSigma       = max(0.0, CrepuscularRay.HazePack.x) * exp(-max(0.0, AltitudeKm) / max(0.1, CrepuscularRay.HazePack.y));

        vec3  HalfAir  = AirOpticalDepth + 0.5 * Extinction * StepKm;   // per-km coefficients, km steps
        float HalfHaze = HazeOpticalDepth + 0.5 * HazeSigma * StepKm;
        vec3  ViewTrans = exp(-HalfAir - vec3(HalfHaze));

        vec3  SunRadiance = SolarFlux * SampleTransmittanceToSpace(Radius, dot(normalize(Position), SolarAxis));
        float Shadow      = EvaluateCloudShadowTransmittance(Position, SolarAxis, SequenceSeconds);
        vec3  Lit         = SunRadiance * ViewTrans;

        Deficit        += AirScatterPerKm * Lit * ((1.0 - Shadow) * StepKm);
        float HazeScatter = HazeSigma * clamp(CrepuscularRay.HazePack.w, 0.0, 1.0) * StepKm;
        OutHazeRadiance  += (Lit * HazePhase * Shadow + HazeAmbient * ViewTrans) * HazeScatter;

        AirOpticalDepth  += Extinction * StepKm;
        HazeOpticalDepth += HazeSigma * StepKm;
    }
    OutHazeTransmittance = exp(-HazeOpticalDepth);
    return Deficit;
}

#endif
