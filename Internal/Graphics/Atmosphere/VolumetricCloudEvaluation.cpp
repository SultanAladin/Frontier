/*==============================================================================================================================================
                                                    VOLUMETRICCLOUDEVALUATION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of Nubis Perlin-Worley volumetric clouds, Laine-Karras Owen-scrambled Sobol quadrature, AtmosphericWindField backward
//    streamline advection, 4-octave multiple-scattering Henyey-Greenstein + Beer-Powder lighting, and spherical aerial perspective.

#include "Graphics/Atmosphere/VolumetricCloudEvaluation.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float PiConstant = 3.14159265358979323846f;

[[nodiscard]] uint32_t ReverseBits32(uint32_t Word) noexcept
{
    Word = ((Word & 0x55555555u) << 1u) | ((Word >> 1u) & 0x55555555u);
    Word = ((Word & 0x33333333u) << 2u) | ((Word >> 2u) & 0x33333333u);
    Word = ((Word & 0x0F0F0F0Fu) << 4u) | ((Word >> 4u) & 0x0F0F0F0Fu);
    Word = ((Word & 0x00FF00FFu) << 8u) | ((Word >> 8u) & 0x00FF00FFu);
    return (Word << 16u) | (Word >> 16u);
}

// 📝 Laine-Karras (2011) fast nested uniform Owen permutation in reversed-bit order.
[[nodiscard]] uint32_t ApplyOwenScrambleReversed(uint32_t ReversedWord, uint32_t Seed) noexcept
{
    ReversedWord += Seed;
    ReversedWord ^= ReversedWord * 0x6C50B47Cu;
    ReversedWord ^= ReversedWord * 0xB82F1E52u;
    ReversedWord ^= ReversedWord * 0xC7AFE638u;
    ReversedWord ^= ReversedWord * 0x8D22F6E6u;
    return ReversedWord;
}

[[nodiscard]] uint32_t HashCloudCell3(int32_t XIdx, int32_t YIdx, int32_t ZIdx, uint32_t Salt) noexcept
{
    uint32_t Word = static_cast<uint32_t>(XIdx) * 73856093u
                  ^ static_cast<uint32_t>(YIdx) * 19349663u
                  ^ static_cast<uint32_t>(ZIdx) * 83492791u
                  ^ (Salt * 1664525u + 1013904223u);
    Word = (Word ^ (Word >> 15u)) * 2246822519u;
    Word = (Word ^ (Word >> 13u)) * 3266489917u;
    return Word ^ (Word >> 16u);
}

// 📝 Ken Perlin's improved gradient noise (quintic fade, 12 edge gradients). A GRADIENT basis is what makes cloud shapes
//    connected and cusped: value noise (a random scalar per lattice point) can only ever produce rounded lumps, which is the
//    "blobby" artifact — it has zero derivative continuity information and no structure to erode along.
[[nodiscard]] float EvaluatePerlinGradient3(float XCoord, float YCoord, float ZCoord, uint32_t Salt) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(XCoord));
    const int32_t IY = static_cast<int32_t>(std::floor(YCoord));
    const int32_t IZ = static_cast<int32_t>(std::floor(ZCoord));

    const float FX = XCoord - static_cast<float>(IX);
    const float FY = YCoord - static_cast<float>(IY);
    const float FZ = ZCoord - static_cast<float>(IZ);

    // Perlin's quintic fade 6t^5 - 15t^4 + 10t^3: C2-continuous, so FBM octaves do not expose lattice-aligned creases.
    const float UX = FX * FX * FX * (FX * (FX * 6.0f - 15.0f) + 10.0f);
    const float UY = FY * FY * FY * (FY * (FY * 6.0f - 15.0f) + 10.0f);
    const float UZ = FZ * FZ * FZ * (FZ * (FZ * 6.0f - 15.0f) + 10.0f);

    // Dot product of the offset vector with one of the 12 cube-edge gradients selected by the corner hash.
    auto LatticeTerm = [&](int32_t DX, int32_t DY, int32_t DZ, float OX, float OY, float OZ) noexcept -> float
    {
        const uint32_t Selector = HashCloudCell3(IX + DX, IY + DY, IZ + DZ, Salt) % 12u;
        float GradX = 0.0f, GradY = 0.0f, GradZ = 0.0f;
        const uint32_t Axis = Selector >> 2u;             // 0..2 -> which axis carries no component
        const float    SignA = ((Selector & 3u) < 2u) ? 1.0f : -1.0f;
        const float    SignB = ((Selector & 1u) == 0u) ? 1.0f : -1.0f;
        if (Axis == 0u)      { GradY = SignA; GradZ = SignB; }
        else if (Axis == 1u) { GradX = SignA; GradZ = SignB; }
        else                 { GradX = SignA; GradY = SignB; }
        return GradX * OX + GradY * OY + GradZ * OZ;
    };

    auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };

    const float Lower = Mix(
        Mix(LatticeTerm(0, 0, 0, FX,         FY,         FZ),
            LatticeTerm(1, 0, 0, FX - 1.0f,  FY,         FZ),         UX),
        Mix(LatticeTerm(0, 1, 0, FX,         FY - 1.0f,  FZ),
            LatticeTerm(1, 1, 0, FX - 1.0f,  FY - 1.0f,  FZ),         UX),
        UY);
    const float Upper = Mix(
        Mix(LatticeTerm(0, 0, 1, FX,         FY,         FZ - 1.0f),
            LatticeTerm(1, 0, 1, FX - 1.0f,  FY,         FZ - 1.0f),  UX),
        Mix(LatticeTerm(0, 1, 1, FX,         FY - 1.0f,  FZ - 1.0f),
            LatticeTerm(1, 1, 1, FX - 1.0f,  FY - 1.0f,  FZ - 1.0f),  UX),
        UY);

    // Gradient noise is bounded by ~±0.866; the 1.15 gain maps it back to roughly [-1, 1].
    return std::clamp(Mix(Lower, Upper, UZ) * 1.15f, -1.0f, 1.0f);
}

// 📝 3D inverted Worley cellular noise: the billow operator of every production cloud renderer (Schneider / Nubis 2015).
//    Inverting the F1 distance turns isolated feature points into connected, creased domes — the cauliflower structure of a cumulus.
[[nodiscard]] float EvaluateInvertedWorley3(float XCoord, float YCoord, float ZCoord, uint32_t Salt) noexcept
{
    const int32_t AnchorX = static_cast<int32_t>(std::floor(XCoord));
    const int32_t AnchorY = static_cast<int32_t>(std::floor(YCoord));
    const int32_t AnchorZ = static_cast<int32_t>(std::floor(ZCoord));

    float MinDistSq = 4.0f;
    for (int32_t DZ = -1; DZ <= 1; ++DZ)
    {
        for (int32_t DY = -1; DY <= 1; ++DY)
        {
            for (int32_t DX = -1; DX <= 1; ++DX)
            {
                const int32_t CellX = AnchorX + DX;
                const int32_t CellY = AnchorY + DY;
                const int32_t CellZ = AnchorZ + DZ;
                const uint32_t H0   = HashCloudCell3(CellX, CellY, CellZ, Salt);
                const uint32_t H1   = HashCloudCell3(CellX + 19, CellY - 7, CellZ + 31, Salt + 5u);

                const float FeatureX = static_cast<float>(CellX) + static_cast<float>(H0 & 0x3FFu) * (1.0f / 1023.0f);
                const float FeatureY = static_cast<float>(CellY) + static_cast<float>((H0 >> 10u) & 0x3FFu) * (1.0f / 1023.0f);
                const float FeatureZ = static_cast<float>(CellZ) + static_cast<float>(H1 & 0x3FFu) * (1.0f / 1023.0f);

                const float DiffX  = FeatureX - XCoord;
                const float DiffY  = FeatureY - YCoord;
                const float DiffZ  = FeatureZ - ZCoord;
                const float DistSq = DiffX * DiffX + DiffY * DiffY + DiffZ * DiffZ;
                if (DistSq < MinDistSq)
                    MinDistSq = DistSq;
            }
        }
    }
    return std::clamp(1.0f - std::sqrt(MinDistSq) * 0.92f, 0.0f, 1.0f);
}

[[nodiscard]] float EvaluateRotatedPerlinFbm(const Vector3f& Coord, uint32_t Octaves, uint32_t Salt) noexcept
{
    // Summed gradient octaves renormalised back into [0, 1] (each octave contributes 0.5 * (noise + 1)).
    float Sum = 0.0f;
    float Amp = 0.50f;
    float PX = Coord.XCoord, PY = Coord.YCoord, PZ = Coord.ZCoord;
    for (uint32_t Octave = 0u; Octave < Octaves; ++Octave)
    {
        Sum += Amp * (EvaluatePerlinGradient3(PX, PY, PZ, Salt + Octave * 13u) * 0.5f + 0.5f);
        const float RX =  0.80f * PX - 0.48f * PY + 0.36f * PZ;
        const float RY =  0.48f * PX + 0.86f * PY + 0.16f * PZ;
        const float RZ = -0.36f * PX + 0.04f * PY + 0.93f * PZ;
        PX = RX * 2.03f + 9.4f;
        PY = RY * 2.03f - 5.8f;
        PZ = RZ * 2.03f + 3.2f;
        Amp *= 0.50f;
    }
    return Sum;
}

[[nodiscard]] float EvaluateRotatedWorleyFbm(const Vector3f& Coord, uint32_t Salt) noexcept
{
    const float W0 = EvaluateInvertedWorley3(Coord.XCoord, Coord.YCoord, Coord.ZCoord, Salt);
    const float R1X =  0.80f * Coord.XCoord - 0.48f * Coord.YCoord + 0.36f * Coord.ZCoord;
    const float R1Y =  0.48f * Coord.XCoord + 0.86f * Coord.YCoord + 0.16f * Coord.ZCoord;
    const float R1Z = -0.36f * Coord.XCoord + 0.04f * Coord.YCoord + 0.93f * Coord.ZCoord;
    const float W1  = EvaluateInvertedWorley3(R1X * 2.12f + 4.3f, R1Y * 2.12f - 2.7f, R1Z * 2.12f, Salt + 17u);

    const float R2X =  0.80f * R1X - 0.48f * R1Y + 0.36f * R1Z;
    const float R2Y =  0.48f * R1X + 0.86f * R1Y + 0.16f * R1Z;
    const float R2Z = -0.36f * R1X + 0.04f * R1Y + 0.93f * R1Z;
    const float W2  = EvaluateInvertedWorley3(R2X * 4.35f - 1.9f, R2Y * 4.35f + 6.1f, R2Z * 4.35f, Salt + 37u);

    return 0.625f * W0 + 0.250f * W1 + 0.125f * W2;
}

[[nodiscard]] float RemapClamped(float Input, float OldMin, float OldMax, float NewMin, float NewMax) noexcept
{
    const float T = std::clamp((Input - OldMin) / std::max(1.0e-5f, OldMax - OldMin), 0.0f, 1.0f);
    return NewMin + T * (NewMax - NewMin);
}

[[nodiscard]] float SmoothStepScalar(float Edge0, float Edge1, float Input) noexcept
{
    const float T = std::clamp((Input - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

// 📝 Per-preset Nubis vertical altitude profile g(h) for h in [0, 1].
[[nodiscard]] float EvaluateAltitudeProfile(CloudFormationPreset Preset, float HeightFraction, float StormFraction) noexcept
{
    const float ClampedH  = std::clamp(HeightFraction, 0.0f, 1.0f);
    const float LowerRamp = SmoothStepScalar(0.0f, 0.11f, ClampedH);

    switch (Preset)
    {
        case CloudFormationPreset::StratocumulusSheet:
            return LowerRamp * (1.0f - SmoothStepScalar(0.32f, 0.68f, ClampedH));

        case CloudFormationPreset::ToweringCumulonimbus:
        {
            const float Column   = LowerRamp * (1.0f - SmoothStepScalar(0.84f, 1.0f, ClampedH));
            const float AnvilCap = SmoothStepScalar(0.68f, 0.84f, ClampedH) * (1.0f - SmoothStepScalar(0.91f, 1.0f, ClampedH)) * 0.45f;
            return std::clamp(Column + AnvilCap, 0.0f, 1.25f);
        }

        case CloudFormationPreset::CyclonicSpiralDeck:
        case CloudFormationPreset::FairWeatherCumulus:
        default:
        {
            const float CumulusTop = 0.72f + 0.24f * StormFraction;
            return LowerRamp * (1.0f - SmoothStepScalar(CumulusTop * 0.62f, CumulusTop, ClampedH));
        }
    }
}

[[nodiscard]] float EvaluateDualLobeHenyeyGreenstein(float CosineTheta, float ForwardG, float BackG, float ForwardWeight) noexcept
{
    auto SingleHg = [](float Mu, float G) noexcept -> float
    {
        const float G2    = G * G;
        const float Denom = std::pow(std::max(1.0e-4f, 1.0f + G2 - 2.0f * G * Mu), 1.5f);
        return (1.0f - G2) / (4.0f * PiConstant * Denom);
    };
    return ForwardWeight * SingleHg(CosineTheta, ForwardG) + (1.0f - ForwardWeight) * SingleHg(CosineTheta, BackG);
}

[[nodiscard]] std::array<float, 2> IntersectPlanetSphereMetres(const Vector3f& OriginPlanetMetres,
                                                               const Vector3f& Direction,
                                                               float           SphereRadiusMetres) noexcept
{
    const double OX = OriginPlanetMetres.XCoord;
    const double OY = OriginPlanetMetres.YCoord;
    const double OZ = OriginPlanetMetres.ZCoord;
    const double DX = Direction.XCoord;
    const double DY = Direction.YCoord;
    const double DZ = Direction.ZCoord;
    const double R  = SphereRadiusMetres;

    const double ProjB = OX * DX + OY * DY + OZ * DZ;
    const double TermC = (OX * OX + OY * OY + OZ * OZ) - R * R;
    const double Disc  = ProjB * ProjB - TermC;
    if (Disc < 0.0)
        return { -1.0f, -1.0f };
    const double Root = std::sqrt(Disc);
    return { static_cast<float>(-ProjB - Root), static_cast<float>(-ProjB + Root) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float EvaluateOwenSobolUnit(uint32_t SampleIndex, uint32_t DimensionIndex, uint32_t PixelScrambleSeed) noexcept
{
    uint32_t SobolBits = 0u;
    if ((DimensionIndex & 1u) == 0u)
    {
        SobolBits = ReverseBits32(SampleIndex);
    }
    else
    {
        uint32_t IndexIter = SampleIndex;
        uint32_t DirMatrix = 0x80000000u;
        while (IndexIter != 0u)
        {
            if ((IndexIter & 1u) != 0u)
                SobolBits ^= DirMatrix;
            IndexIter >>= 1u;
            DirMatrix ^= (DirMatrix >> 1u);
        }
    }

    const uint32_t DimSeed   = PixelScrambleSeed ^ (DimensionIndex * 0x9E3779B9u + 0x7F4A7C15u);
    const uint32_t Scrambled = ReverseBits32(ApplyOwenScrambleReversed(ReverseBits32(SobolBits), DimSeed));
    return static_cast<float>(Scrambled >> 8u) * (1.0f / 16777216.0f);
}

namespace
{

[[nodiscard]] float EvaluateCloudDensityInternal(const VolumetricCloudConfiguration& CloudConfiguration,
                                                 const Vector3f&                     WorldPositionMetres,
                                                 float                               SequenceSeconds,
                                                 const AtmosphericWindField*         OptionalWindField,
                                                 const AdvectionOffsetVolume*        OptionalOffsetVolume,
                                                 bool                                IncludeFineErosion,
                                                 uint32_t                            MaxStreamlineSteps) noexcept
{
    if (!CloudConfiguration.CloudsEnabled)
        return 0.0f;

    const float StratumTopMetres = CloudConfiguration.LowerAltitudeMetres + std::max(100.0f, CloudConfiguration.StratumThicknessMetres);
    const float HeightFraction   = (WorldPositionMetres.ZCoord - CloudConfiguration.LowerAltitudeMetres)
                                 / std::max(100.0f, StratumTopMetres - CloudConfiguration.LowerAltitudeMetres);
    if (HeightFraction <= 0.0f || HeightFraction >= 1.0f)
        return 0.0f;

    // ① Semi-Lagrangian backward characteristic advection through the active AtmosphericWindField / ForceFields!
    Vector3f LagrangianPos   = WorldPositionMetres;
    float    ConvergenceLift = 0.0f;

    // Prefer the trilinearly sampled baked displacement volume (constant cost per sample); fall back to the exact streamline.
    Vector3f BakedDisplacement{ 0.0f, 0.0f, 0.0f };
    float    BakedConvergence = 0.0f;
    const bool OffsetResolved = (OptionalOffsetVolume != nullptr)
                             && SampleAdvectionOffset(*OptionalOffsetVolume, WorldPositionMetres, BakedDisplacement, BakedConvergence);

    if (OffsetResolved)
    {
        LagrangianPos   = AddVector(WorldPositionMetres, BakedDisplacement);
        ConvergenceLift = BakedConvergence;
    }
    else if (OptionalWindField != nullptr && OptionalWindField->FieldEnabled)
    {
        const float EffectiveSpan = CloudConfiguration.CumulativeAdvectionEnabled
            ? std::max(0.0f, SequenceSeconds)
            : CloudConfiguration.AdvectionSpanSeconds;
        if (EffectiveSpan > 1.0e-3f)
        {
            const uint32_t SubSteps = std::min(MaxStreamlineSteps,
                static_cast<uint32_t>(std::clamp(std::ceil(EffectiveSpan / 50.0f), 2.0f, 6.0f)));
            LagrangianPos = IntegrateStreamlineBackward(
                *OptionalWindField,
                WorldPositionMetres,
                SequenceSeconds,
                EffectiveSpan,
                SubSteps,
                AdvectionMediumTarget::CloudStratum);
        }
        if (IncludeFineErosion)
        {
            const WindSampleEvaluation LocalWind = EvaluateWindSample(
                *OptionalWindField, WorldPositionMetres, SequenceSeconds, AdvectionMediumTarget::CloudStratum);
            ConvergenceLift = LocalWind.ConvergenceScalar;
        }
    }

    LagrangianPos.XCoord -= HeightFraction * 260.0f;

    // ② Weather map: macro coverage + storm convective towers concentrated where ForceField eyewalls/plumes converge!
    const float WeatherMacro = EvaluateRotatedPerlinFbm(
        Vector3f{ LagrangianPos.XCoord * 0.000068f + 2.3f, LagrangianPos.YCoord * 0.000068f - 1.7f, 0.35f }, 4u, 11u);
    const float WeatherBands = EvaluateRotatedPerlinFbm(
        Vector3f{ LagrangianPos.XCoord * 0.000145f - 4.1f, LagrangianPos.YCoord * 0.000145f + 3.2f, 1.15f }, 3u, 29u);

    float LocalCoverage = SmoothStepScalar(
        1.0f - std::clamp(CloudConfiguration.MacroCoverage + 0.16f * ConvergenceLift, 0.02f, 0.98f) - 0.18f,
        1.0f - std::clamp(CloudConfiguration.MacroCoverage + 0.16f * ConvergenceLift, 0.02f, 0.98f) + 0.24f,
        0.65f * WeatherMacro + 0.35f * WeatherBands);

    float LocalizedBoost = 0.0f;
    for (const LocalizedCloudCell& Cell : CloudConfiguration.LocalizedCells)
    {
        if (!Cell.ActivationEnabled)
            continue;
        const float DX = (LagrangianPos.XCoord - Cell.CenterMetres.XCoord) / std::max(50.0f, Cell.HalfExtentsMetres.XCoord);
        const float DY = (LagrangianPos.YCoord - Cell.CenterMetres.YCoord) / std::max(50.0f, Cell.HalfExtentsMetres.YCoord);
        const float DZ = (LagrangianPos.ZCoord - Cell.CenterMetres.ZCoord) / std::max(50.0f, Cell.HalfExtentsMetres.ZCoord);
        const float DistSq = DX * DX + DY * DY + DZ * DZ;
        if (DistSq < 1.0f)
        {
            const float Envelope = (1.0f - DistSq) * (1.0f - DistSq);
            LocalizedBoost = std::max(LocalizedBoost, Envelope * Cell.PeakDensityMultiplier);
        }
    }

    LocalCoverage = std::clamp(LocalCoverage + LocalizedBoost * 0.72f, 0.0f, 1.0f);
    if (LocalCoverage <= 1.0e-3f)
        return 0.0f;

    const float StormFraction = std::clamp(
        CloudConfiguration.PrecipitationBias + 0.45f * std::max(0.0f, ConvergenceLift) + 0.35f * LocalizedBoost, 0.0f, 1.0f);
    const float AltitudeWeight = EvaluateAltitudeProfile(CloudConfiguration.FormationPreset, HeightFraction, StormFraction);
    if (AltitudeWeight <= 1.0e-4f)
        return 0.0f;

    // ③ Nubis macro shape. A gradient Perlin FBM dilated by 3-octave inverted Worley is the production recipe: the Worley
    //    dilation pinches the Perlin field into a CONNECTED mass of domes (cauliflower billows) rather than isolated lumps, and the
    //    gradient basis keeps the field C2-continuous so no lattice creases leak into the silhouette.
    // Cell aspect matters: shape frequencies near 3 km horizontally against a ~2 km deep deck produce lenses that read as flat
    // pancakes. Tripling the horizontal frequency (≈1.8 km cells) with a taller vertical scale gives aspect ratios near unity, which
    // is the geometry of a real cumulus humilis, and the depth-limited rays stop clipping the tops off.
    const Vector3f ShapeCoord{
        LagrangianPos.XCoord * 0.000560f,
        LagrangianPos.YCoord * 0.000560f,
        LagrangianPos.ZCoord * 0.000920f };
    const float PerlinMacro = EvaluateRotatedPerlinFbm(ShapeCoord, 5u, 53u);
    const float WorleyMacro = EvaluateRotatedWorleyFbm(ScaleVector(ShapeCoord, 1.45f), 97u);

    // Nubis' "Perlin dilated by Worley". The literature form (Perlin - (1 - Worley)) / Worley subtracts an absolute threshold from
    // the Perlin field; with a five-octave FBM whose summed amplitude sits near 0.5 that carves away almost the entire deck, so the
    // field is first contrast-normalised to its own working range and then MULTIPLIED by the cellular dilation. Multiplying keeps the
    // cell mass connected (the Worley is a smooth envelope) while the Perlin supplies the internal structure to erode along.
    const float PerlinContrast = std::clamp((PerlinMacro - 0.30f) / 0.42f, 0.0f, 1.0f);
    const float WorleyDilation = std::clamp(0.25f + 1.30f * WorleyMacro, 0.0f, 1.0f);
    const float PerlinWorley   = PerlinContrast * WorleyDilation;

    float CoarseCloud = RemapClamped(PerlinWorley * AltitudeWeight, 1.0f - LocalCoverage, 1.0f, 0.0f, 1.0f);
    if (CoarseCloud <= 1.0e-4f)
        return 0.0f;

    // ④ Billow detail. Two physically distinct eroders, both carved with a REMAP so they sculpt the surface instead of subtracting a
    //    fixed absolute mass (an absolute subtraction deletes any cell whose interior happens to be thinner than the constant — which
    //    is exactly how a cumulus field collapses into a scatter of pebbles):
    //
    //      (a) inverted Worley  — crumples the margin into rounded lobes (the "many small puffballs" scale of a cumulus)
    //      (b) |Perlin| billow  — folds the field about zero, producing the sharp CUSPS and creases that a smooth eroder cannot make.
    //                             A plain `density -= smooth_noise` rounds every surface off, which is the literal definition of a blob.
    //
    //    Both fade out toward the dense core and toward the lit crown, so the body of the cloud stays solid and only the silhouette
    //    gets sculpted. Shadow-cone taps run a single coarse octave.
    if (IncludeFineErosion && CloudConfiguration.DetailErosionStrength > 1.0e-3f)
    {
        const bool  CoarseLod = (MaxStreamlineSteps <= 2u);
        const float LaceScale = CoarseLod ? 1.45f : 1.0f;

        // (a) Worley lobe crumple.
        const Vector3f LobeCoord{
            LagrangianPos.XCoord * 0.00135f * LaceScale,
            LagrangianPos.YCoord * 0.00135f * LaceScale,
            LagrangianPos.ZCoord * 0.00160f * LaceScale };
        const float LobeWorley = EvaluateRotatedWorleyFbm(LobeCoord, 181u);

        // (b) Billow cusps from the absolute value of a gradient field.
        const Vector3f CuspCoord{
            LagrangianPos.XCoord * 0.00078f,
            LagrangianPos.YCoord * 0.00078f,
            LagrangianPos.ZCoord * 0.00096f };
        const float CuspA = std::fabs(EvaluatePerlinGradient3(CuspCoord.XCoord, CuspCoord.YCoord, CuspCoord.ZCoord, 211u));
        const float CuspB = CoarseLod ? CuspA : std::fabs(EvaluatePerlinGradient3(
            CuspCoord.XCoord * 2.13f + 5.1f, CuspCoord.YCoord * 2.13f - 3.3f, CuspCoord.ZCoord * 2.13f + 1.7f, 233u));
        const float Cusp = std::clamp(CuspA * 0.66f + CuspB * 0.44f, 0.0f, 1.0f);

        // Sculpt the SILHOUETTE band, not the feather edge. Erosion is applied where the mass is mid-density — that is where the
        // visible outline lives — and tapered back to zero at the very low-density skin. Carving the skin instead only detaches
        // its last few samples, which is what renders as confetti shards floating beside a cloud.
        const float SilhouetteBand = SmoothStepScalar(0.02f, 0.26f, CoarseCloud)
                                   * (1.0f - 0.85f * SmoothStepScalar(0.58f, 0.95f, CoarseCloud));
        const float CrownWeight    = 0.55f + 0.45f * (1.0f - SmoothStepScalar(0.55f, 1.0f, HeightFraction));

        const float ErodeAmount = CloudConfiguration.DetailErosionStrength * SilhouetteBand * CrownWeight
                                * ((1.0f - LobeWorley) * 0.58f + Cusp * 0.62f);
        CoarseCloud = RemapClamped(CoarseCloud, std::clamp(ErodeAmount, 0.0f, 0.62f), 1.0f, 0.0f, 1.0f);
        if (CoarseCloud <= 1.0e-4f)
            return 0.0f;
    }

    // Condensation fills outward from the saturated core: a concave response thickens the body while the envelope stays a thin
    // skin, so a cell reads as a solid volume rather than a hollow shell.
    CoarseCloud = std::pow(CoarseCloud, 0.78f);

    // Normalised field -> droplet density. The remap above is deliberately conservative (it only marks where condensate exists),
    // so the optical thickness of the core is set here, where it belongs: a cumulus core is radiatively opaque.
    const float AdiabaticGain = 0.45f + 0.85f * SmoothStepScalar(0.0f, 0.55f, HeightFraction);
    return std::clamp(CoarseCloud * AdiabaticGain * CloudConfiguration.CondensateDensityGain
                      * (1.0f + 0.35f * LocalizedBoost), 0.0f, 1.0f);
}

} // namespace

float EvaluateCloudShapeNoise(const Vector3f& Coord, uint32_t Octaves, uint32_t Salt) noexcept
{
    return EvaluateRotatedPerlinFbm(Coord, Octaves, Salt);
}

float EvaluateCloudDensityField(const VolumetricCloudConfiguration& CloudConfiguration,
                                const Vector3f&                     WorldPositionMetres,
                                float                               SequenceSeconds,
                                const AtmosphericWindField*         OptionalWindField) noexcept
{
    return EvaluateCloudDensityInternal(
        CloudConfiguration, WorldPositionMetres, SequenceSeconds, OptionalWindField, nullptr, true, 5u);
}

void PrepareCloudAdvectionOffsetVolume(AdvectionOffsetVolume&              OffsetVolume,
                                       const VolumetricCloudConfiguration& CloudConfiguration,
                                       const RadiometricSkyConfiguration&  SkyConfiguration,
                                       const AtmosphericWindField*         OptionalWindField) noexcept
{
    OffsetVolume = AdvectionOffsetVolume{};
    if (OptionalWindField == nullptr || !OptionalWindField->FieldEnabled)
        return;

    const float EffectiveSpan = CloudConfiguration.CumulativeAdvectionEnabled
        ? std::max(0.0f, SkyConfiguration.SequenceSeconds)
        : CloudConfiguration.AdvectionSpanSeconds;
    if (EffectiveSpan <= 1.0e-3f)
        return;

    // Bounds span the full synoptic weather field (±23 km) with ~0.5 km horizontal spacing and 10 vertical layers across the deck.
    BakeAdvectionOffsetVolume(
        OffsetVolume,
        *OptionalWindField,
        AdvectionMediumTarget::CloudStratum,
        SkyConfiguration.SequenceSeconds,
        EffectiveSpan,
        6u,
        Vector3f{ -23000.0f, -23000.0f, CloudConfiguration.LowerAltitudeMetres - 900.0f },
        Vector3f{ 480.0f, 480.0f, std::max(120.0f, CloudConfiguration.StratumThicknessMetres / 8.0f) });
}

VolumetricCloudRayResult EvaluateVolumetricCloudRay(const VolumetricCloudConfiguration& CloudConfiguration,
                                                    const RadiometricSkyTables&         SkyTables,
                                                    const RadiometricSkyConfiguration&  SkyConfiguration,
                                                    const Vector3f&                     CameraSceneMetres,
                                                    const Vector3f&                     RayDirectionScene,
                                                    const AtmosphericWindField*         OptionalWindField,
                                                    uint32_t                            PixelCoordX,
                                                    uint32_t                            PixelCoordY,
                                                    uint32_t                            SubSampleIndex,
                                                    const AdvectionOffsetVolume*        PreparedCloudOffsetVolume) noexcept
{
    VolumetricCloudRayResult Result{};
    if (!CloudConfiguration.CloudsEnabled || CloudConfiguration.MacroCoverage <= 0.001f)
        return Result;

    const Vector3f ViewDir = NormalizeVector(RayDirectionScene);
    const Vector3f SunDir  = NormalizeVector(SkyConfiguration.SolarDirectionScene);

    const float PlanetRadiusMetres = SkyConfiguration.BottomRadiusKilometres * 1000.0f;
    const float LowerStratumMetres = PlanetRadiusMetres + CloudConfiguration.LowerAltitudeMetres;
    const float UpperStratumMetres = LowerStratumMetres + std::max(100.0f, CloudConfiguration.StratumThicknessMetres);

    const Vector3f CamPlanetMetres{
        CameraSceneMetres.XCoord,
        CameraSceneMetres.YCoord,
        PlanetRadiusMetres + std::max(1.5f, SkyConfiguration.ObserverAltitudeMetres + CameraSceneMetres.ZCoord) };

    const float CamRadiusMetres = VectorLength(CamPlanetMetres);
    const auto [GroundNear, GroundFar] = IntersectPlanetSphereMetres(CamPlanetMetres, ViewDir, PlanetRadiusMetres);
    (void)GroundFar;
    const auto [LowerNear, LowerFar]   = IntersectPlanetSphereMetres(CamPlanetMetres, ViewDir, LowerStratumMetres);
    const auto [UpperNear, UpperFar]   = IntersectPlanetSphereMetres(CamPlanetMetres, ViewDir, UpperStratumMetres);

    float MarchStart = 0.0f;
    float MarchEnd   = 0.0f;

    if (CamRadiusMetres < LowerStratumMetres)
    {
        if (GroundNear > 0.0f || LowerFar <= 0.0f || UpperFar <= 0.0f)
            return Result;
        MarchStart = std::max(0.0f, LowerFar);
        MarchEnd   = UpperFar;
    }
    else if (CamRadiusMetres <= UpperStratumMetres)
    {
        MarchStart = 0.0f;
        MarchEnd   = (GroundNear > 0.0f && LowerNear > 0.0f) ? LowerNear : UpperFar;
    }
    else
    {
        if (UpperFar <= 0.0f)
            return Result;
        MarchStart = std::max(0.0f, UpperNear);
        MarchEnd   = (LowerNear > 0.0f) ? LowerNear : UpperFar;
        if (GroundNear > 0.0f)
            MarchEnd = std::min(MarchEnd, GroundNear);
    }

    if (MarchStart >= CloudConfiguration.MaximumTraceDistanceMetres || MarchEnd <= MarchStart)
        return Result;

    MarchEnd = std::min(MarchEnd, MarchStart + CloudConfiguration.MaximumTraceDistanceMetres);

    // Uniform march whose step length is bounded by the target step size. A fixed sample count spread over a span that reaches the
    // far cutoff gives half-kilometre steps, and one such step can graze an entire cumulus margin (stair-stepped outlines). The
    // sample count therefore follows the span instead of the other way round, capped so the per-ray cost stays predictable.
    const float    TargetStep = std::max(12.0f, CloudConfiguration.TargetMarchStepMetres);
    const uint32_t StepBudget = std::clamp(
        static_cast<uint32_t>(std::ceil((MarchEnd - MarchStart) / TargetStep)), 24u, 200u);
    const uint32_t StepCount  = std::max(StepBudget, std::clamp(CloudConfiguration.PrimaryMarchStepCount, 24u, 200u));
    const float    StepMetres = (MarchEnd - MarchStart) / static_cast<float>(StepCount);

    // Owen-scrambled (0,2) Sobol ray offset eliminates diagonal/crosshatch raymarch banding!
    const uint32_t PixelSeed  = (PixelCoordX * 1973u) ^ (PixelCoordY * 9277u) ^ 0x68BC21EBu;
    const float    RayJitter  = EvaluateOwenSobolUnit(SubSampleIndex, 0u, PixelSeed);

    const float CosineSunView = DotVector(ViewDir, SunDir);

    // ② Wind-offset volume (Nubis / Frostbite): the caller bakes the coarse Lagrangian displacement field once per integration, so every
    //    march / shadow-cone density sample is a trilinear fetch instead of a fresh semi-Lagrangian characteristic integration.
    const AdvectionOffsetVolume* ActiveOffsetVolume =
        (PreparedCloudOffsetVolume != nullptr && PreparedCloudOffsetVolume->ReadyCondition) ? PreparedCloudOffsetVolume : nullptr;

    // Sample ambient sky hemisphere colors once per ray for top/bottom cloud ambient bounce.
    const SpectralColor OverheadSkyAmbient = SampleSkyViewTable(
        SkyTables, SkyConfiguration, Vector3f{ 0.0f, 0.0f, 0.92f });
    const SpectralColor GrazingSkyAmbient  = SampleSkyViewTable(
        SkyTables, SkyConfiguration, Vector3f{ SunDir.XCoord * 0.5f, SunDir.YCoord * 0.5f, 0.14f });

    // Orthonormal cone basis around SunDir for 6-tap self-shadow cone sampling.
    const Vector3f SunRefUp  = (std::fabs(SunDir.ZCoord) < 0.95f) ? Vector3f{ 0.0f, 0.0f, 1.0f } : Vector3f{ 1.0f, 0.0f, 0.0f };
    const Vector3f ConeBasisU = NormalizeVector(CrossVector(SunRefUp, SunDir));
    const Vector3f ConeBasisV = CrossVector(SunDir, ConeBasisU);

    static constexpr std::array<std::array<float, 3u>, 6u> ConeOffsets {{
        {  0.12f,  0.05f, 0.45f },
        { -0.18f,  0.14f, 0.95f },
        {  0.08f, -0.22f, 1.65f },
        { -0.24f, -0.11f, 2.60f },
        {  0.21f,  0.19f, 4.10f },
        {  0.00f,  0.00f, 8.50f }
    }};

    float         CloudTransmittance  = 1.0f;
    SpectralColor CloudRadiance{ 0.0f, 0.0f, 0.0f };
    float         WeightedDistanceSum = 0.0f;
    float         WeightAccumulated   = 0.0f;
    uint32_t      FineCountdown       = 0u;
    uint32_t      Step                = 0u;

    while (Step < StepCount)
    {
        if (CloudTransmittance < 0.008f)
        {
            CloudTransmittance = 0.0f;
            break;
        }

        const bool     CoarseProbe  = (FineCountdown == 0u) && (Step + 2u < StepCount);
        const float    TravelMetres = MarchStart + (static_cast<float>(Step) + RayJitter) * StepMetres;
        const Vector3f PlanetPos    = AddVector(CamPlanetMetres, ScaleVector(ViewDir, TravelMetres));
        const float    SampleRadM   = VectorLength(PlanetPos);
        const float    SampleAltM   = SampleRadM - PlanetRadiusMetres;

        const Vector3f EvalScenePos{ PlanetPos.XCoord, PlanetPos.YCoord, SampleAltM };
        const float    Density = EvaluateCloudDensityInternal(
            CloudConfiguration, EvalScenePos, SkyConfiguration.SequenceSeconds, OptionalWindField, ActiveOffsetVolume, !CoarseProbe, 4u);

        if (Density <= 1.0e-4f)
        {
            // Any nonzero density switches the ray to fine integration, so a freshly detected margin is never skipped past
            // (hole-punching the midpoint doubled the density evaluations and produced sparse speckle along the outlines).
            if (FineCountdown > 0u)
            {
                --FineCountdown;
                ++Step;
            }
            else
            {
                Step += 2u;
            }
            continue;
        }

        if (CoarseProbe)
        {
            // Transition from coarse empty-space skip to fine 1x integration without missing the cloud entry margin!
            FineCountdown = 5u;
            if (Step > 0u)
                --Step;
            continue;
        }

        FineCountdown = 4u;
        ++Step;

        const float ExtinctionSigma = Density * CloudConfiguration.ExtinctionPerMetre;
        const float StepOptical     = ExtinctionSigma * StepMetres;
        const float StepTrans       = std::exp(-StepOptical);
        const float OpacityWeight   = CloudTransmittance * (1.0f - StepTrans);

        WeightedDistanceSum += TravelMetres * OpacityWeight;
        WeightAccumulated   += OpacityWeight;

        // 6-tap sun-cone optical depth toward SolarDirection (macro Perlin-Worley LOD per Nubis GPU Pro 7).
        float SunOpticalDepth = 0.0f;
        const uint32_t ConeSteps    = std::min<uint32_t>(CloudConfiguration.SunConeStepCount, 6u);
        const float    UnitConeSpan = 95.0f;
        for (uint32_t Tap = 0u; Tap < ConeSteps; ++Tap)
        {
            const auto& Off = ConeOffsets[Tap];
            const Vector3f ConeOffset = AddVector(
                ScaleVector(SunDir, Off[2] * UnitConeSpan),
                AddVector(ScaleVector(ConeBasisU, Off[0] * Off[2] * UnitConeSpan),
                          ScaleVector(ConeBasisV, Off[1] * Off[2] * UnitConeSpan)));
            const Vector3f TapPlanetPos = AddVector(PlanetPos, ConeOffset);
            const float    TapAltM      = VectorLength(TapPlanetPos) - PlanetRadiusMetres;
            const float    TapDensity   = EvaluateCloudDensityInternal(
                CloudConfiguration, Vector3f{ TapPlanetPos.XCoord, TapPlanetPos.YCoord, TapAltM },
                SkyConfiguration.SequenceSeconds, OptionalWindField, ActiveOffsetVolume, false, 2u);
            const float    TapSegLen    = (Tap == 0u) ? (Off[2] * UnitConeSpan) : ((Off[2] - ConeOffsets[Tap - 1u][2]) * UnitConeSpan);
            SunOpticalDepth += TapDensity * CloudConfiguration.ExtinctionPerMetre * TapSegLen;
        }

        // Solar spectral transmittance from the atmosphere LUT at the exact spherical cloud altitude!
        const Vector3f LocalUp    = ScaleVector(PlanetPos, 1.0f / std::max(1.0f, SampleRadM));
        const float    LocalSunMu = DotVector(LocalUp, SunDir);
        const SpectralColor AtmoSunTrans = EvaluateTransmittanceToSpace(
            SkyTables, SkyConfiguration, SampleRadM * 0.001f, LocalSunMu);
        const SpectralColor SunIrradiance = MultiplySpectral(SkyConfiguration.ExtraterrestrialSolarFlux, AtmoSunTrans);

        // Schneider Beer-Powder dark-edge relief (softened along forward sun view so silver linings stay radiant, and relaxed as
        // the sun approaches the horizon where grazing illumination makes the deck a pure sky-dome scatterer).
        const float ForwardRelax = 0.5f + 0.5f * std::max(0.0f, CosineSunView);
        const float GrazingRelax = std::clamp(SunDir.ZCoord * 1.6f + 0.34f, 0.30f, 1.0f);
        const float PowderTerm   = 1.0f - CloudConfiguration.PowderEffectStrength * GrazingRelax
                                 * (1.0f - ForwardRelax * 0.65f) * std::exp(-SunOpticalDepth * 2.1f);

        // Hillaire / Wrenninge 4-octave multiple-scattering sum so thick cumulus cores stay luminous instead of soot-black.
        // Octave 0 is the true single-scattered term (fully extinguished by the whole cone optical depth, which is what keeps the
        // shaded edge and underside dark). Octaves 1..n represent paths that scatter several times inside the deck and therefore see
        // only a shrinking fraction of that depth — a short ladder (x0.28) lifts deep interiors toward mid-grey without flattening
        // the silhouette, whereas a long ladder (x0.52) leaves cumulonimbus cores as black voids.
        float MultiOctaveSunEnergy = 0.0f;
        float AttenuationOctave    = 1.00f;
        float ContributionOctave   = 1.00f;
        float PhaseScaleOctave     = 1.00f;
        for (uint32_t Octave = 0u; Octave < 4u; ++Octave)
        {
            const float OctavePhase = EvaluateDualLobeHenyeyGreenstein(
                CosineSunView,
                CloudConfiguration.ForwardLobeAnisotropy * PhaseScaleOctave,
                CloudConfiguration.BackLobeAnisotropy * PhaseScaleOctave,
                CloudConfiguration.ForwardLobeMix);
            MultiOctaveSunEnergy += ContributionOctave * OctavePhase * std::exp(-SunOpticalDepth * AttenuationOctave);
            AttenuationOctave    *= 0.28f;
            ContributionOctave   *= 0.46f;
            PhaseScaleOctave     *= 0.50f;
        }
        MultiOctaveSunEnergy *= PowderTerm;

        // Height-dependent sky-dome + lower-deck ambient irradiance.
        const float HeightFrac = std::clamp(
            (SampleAltM - CloudConfiguration.LowerAltitudeMetres) / std::max(100.0f, CloudConfiguration.StratumThicknessMetres),
            0.0f, 1.0f);
        const float SkyAmbientOcclusion = 0.22f + 0.78f * std::pow(HeightFrac, 0.68f);
        // The sky dome supplies only a small fraction of a dayside cloud's illumination (the photosphere dominates), but it becomes
        // the dominant source once the sun grazes the horizon — so the dome gain rises as the solar elevation falls.
        const float SolarHeight  = std::clamp(SunDir.ZCoord * 1.5f + 0.30f, 0.0f, 1.0f);
        const float DomeBlend    = std::clamp(HeightFrac * 0.55f + (1.0f - SolarHeight) * 0.45f, 0.0f, 1.0f);
        const float AmbientGain  = 0.15f + 0.42f * (1.0f - SolarHeight);
        const SpectralColor AmbientLight{
            (OverheadSkyAmbient.Red   * DomeBlend + GrazingSkyAmbient.Red   * (1.0f - DomeBlend)) * SkyAmbientOcclusion * AmbientGain,
            (OverheadSkyAmbient.Green * DomeBlend + GrazingSkyAmbient.Green * (1.0f - DomeBlend)) * SkyAmbientOcclusion * AmbientGain,
            (OverheadSkyAmbient.Blue  * DomeBlend + GrazingSkyAmbient.Blue  * (1.0f - DomeBlend)) * SkyAmbientOcclusion * AmbientGain };

        const float ScatteringSigma = ExtinctionSigma * CloudConfiguration.SingleScatteringAlbedo;
        const SpectralColor StepSource{
            ScatteringSigma * (SunIrradiance.Red   * MultiOctaveSunEnergy + AmbientLight.Red),
            ScatteringSigma * (SunIrradiance.Green * MultiOctaveSunEnergy + AmbientLight.Green),
            ScatteringSigma * (SunIrradiance.Blue  * MultiOctaveSunEnergy + AmbientLight.Blue) };

        // Energy-conserving analytical step integration (1 - exp(-sigma_t * ds)) / sigma_t.
        const float StepIntegral = (1.0f - StepTrans) / std::max(1.0e-6f, ExtinctionSigma);
        CloudRadiance.Red   += CloudTransmittance * StepSource.Red   * StepIntegral;
        CloudRadiance.Green += CloudTransmittance * StepSource.Green * StepIntegral;
        CloudRadiance.Blue  += CloudTransmittance * StepSource.Blue  * StepIntegral;

        CloudTransmittance *= StepTrans;
    }

    Result.Transmittance = CloudTransmittance;
    const float CloudCoverageAlpha = 1.0f - CloudTransmittance;
    if (CloudCoverageAlpha <= 1.0e-4f || WeightAccumulated <= 1.0e-5f)
        return Result;

    Result.WeightedHitDistanceMetres = WeightedDistanceSum / WeightAccumulated;

    // ⑤ Spherical Aerial Perspective between camera and mean cloud distance: distant horizon clouds naturally redden at sunset
    //    and blend into blue Rayleigh airlight at noon without double-counting background sky!
    const float DistanceKm = Result.WeightedHitDistanceMetres * 0.001f;
    const float AerialTauR = SkyConfiguration.RayleighScatteringPerKm.Red   * DistanceKm * 0.72f + SkyConfiguration.MieExtinctionPerKm * DistanceKm * 0.35f;
    const float AerialTauG = SkyConfiguration.RayleighScatteringPerKm.Green * DistanceKm * 0.72f + SkyConfiguration.MieExtinctionPerKm * DistanceKm * 0.35f;
    const float AerialTauB = SkyConfiguration.RayleighScatteringPerKm.Blue  * DistanceKm * 0.72f + SkyConfiguration.MieExtinctionPerKm * DistanceKm * 0.35f;

    const SpectralColor AerialTrans{
        std::exp(-AerialTauR),
        std::exp(-AerialTauG),
        std::exp(-AerialTauB) };
    const SpectralColor ClearSkyDir = SampleSkyViewTable(SkyTables, SkyConfiguration, ViewDir);

    // Premultiplied cloud radiance attenuated by aerial transmittance + foreground airlight over the cloud's coverage alpha:
    Result.InscatteredRadiance = SpectralColor{
        CloudRadiance.Red   * AerialTrans.Red   + ClearSkyDir.Red   * (1.0f - AerialTrans.Red)   * CloudCoverageAlpha,
        CloudRadiance.Green * AerialTrans.Green + ClearSkyDir.Green * (1.0f - AerialTrans.Green) * CloudCoverageAlpha,
        CloudRadiance.Blue  * AerialTrans.Blue  + ClearSkyDir.Blue  * (1.0f - AerialTrans.Blue)  * CloudCoverageAlpha };

    return Result;
}

} // namespace Frontier
