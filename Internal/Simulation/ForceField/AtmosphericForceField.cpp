/*==============================================================================================================================================
                                                      ATMOSPHERICFORCEFIELD.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the 3D atmospheric force-field velocity solver and semi-Lagrangian backward characteristic integrator.

#include "Simulation/ForceField/AtmosphericForceField.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float LambOseenAlpha        = 1.2564312f;   // [-] - Maximizes tangential speed at r == InnerRadiusMetres
constexpr float LambOseenNormalization = 1.3979528f;  // [-] - 1 / (1 - exp(-LambOseenAlpha)) so v_theta(r_eye) == PeakVelocity

[[nodiscard]] uint32_t MixLatticeHash(int32_t XIndex, int32_t YIndex, int32_t ZIndex, uint32_t ChannelSalt) noexcept
{
    uint32_t HashWord = static_cast<uint32_t>(XIndex) * 73856093u
                      ^ static_cast<uint32_t>(YIndex) * 19349663u
                      ^ static_cast<uint32_t>(ZIndex) * 83492791u
                      ^ (ChannelSalt * 2654435761u);
    HashWord = (HashWord ^ (HashWord >> 15u)) * 2246822519u;
    HashWord = (HashWord ^ (HashWord >> 13u)) * 3266489917u;
    return HashWord ^ (HashWord >> 16u);
}

[[nodiscard]] float EvaluateSmoothLatticeScalar(float XCoord, float YCoord, float ZCoord, uint32_t ChannelSalt) noexcept
{
    const int32_t CellX = static_cast<int32_t>(std::floor(XCoord));
    const int32_t CellY = static_cast<int32_t>(std::floor(YCoord));
    const int32_t CellZ = static_cast<int32_t>(std::floor(ZCoord));

    const float FracX = XCoord - static_cast<float>(CellX);
    const float FracY = YCoord - static_cast<float>(CellY);
    const float FracZ = ZCoord - static_cast<float>(CellZ);

    // Quintic C2 Perlin fade so finite-difference curl derivatives stay continuous across cell faces.
    const float SmoothX = FracX * FracX * FracX * (FracX * (FracX * 6.0f - 15.0f) + 10.0f);
    const float SmoothY = FracY * FracY * FracY * (FracY * (FracY * 6.0f - 15.0f) + 10.0f);
    const float SmoothZ = FracZ * FracZ * FracZ * (FracZ * (FracZ * 6.0f - 15.0f) + 10.0f);

    auto FetchCorner = [&](int32_t OffsetX, int32_t OffsetY, int32_t OffsetZ) noexcept -> float
    {
        const uint32_t Bits = MixLatticeHash(CellX + OffsetX, CellY + OffsetY, CellZ + OffsetZ, ChannelSalt);
        return static_cast<float>(Bits & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;
    };

    auto Interpolate = [](float Lower, float Upper, float Weight) noexcept -> float
    {
        return Lower + (Upper - Lower) * Weight;
    };

    const float Slice0 = Interpolate(
        Interpolate(FetchCorner(0, 0, 0), FetchCorner(1, 0, 0), SmoothX),
        Interpolate(FetchCorner(0, 1, 0), FetchCorner(1, 1, 0), SmoothX),
        SmoothY);
    const float Slice1 = Interpolate(
        Interpolate(FetchCorner(0, 0, 1), FetchCorner(1, 0, 1), SmoothX),
        Interpolate(FetchCorner(0, 1, 1), FetchCorner(1, 1, 1), SmoothX),
        SmoothY);
    return Interpolate(Slice0, Slice1, SmoothZ);
}

// 📝 Multi-octave domain-rotated 3D vector potential Psi(x, t) whose curl del x Psi is identically divergence-free.
[[nodiscard]] Vector3f EvaluateTurbulenceVectorPotential(const Vector3f& ScaledCoord, float TemporalPhase) noexcept
{
    Vector3f Potential{ 0.0f, 0.0f, 0.0f };
    float    Amplitude = 0.58f;
    float    CoordX    = ScaledCoord.XCoord + TemporalPhase * 0.07f;
    float    CoordY    = ScaledCoord.YCoord - TemporalPhase * 0.05f;
    float    CoordZ    = ScaledCoord.ZCoord + TemporalPhase * 0.03f;

    for (uint32_t Octave = 0u; Octave < 3u; ++Octave)
    {
        Potential.XCoord += Amplitude * EvaluateSmoothLatticeScalar(CoordX, CoordY, CoordZ, 101u + Octave * 17u);
        Potential.YCoord += Amplitude * EvaluateSmoothLatticeScalar(CoordX, CoordY, CoordZ, 211u + Octave * 23u);
        Potential.ZCoord += Amplitude * EvaluateSmoothLatticeScalar(CoordX, CoordY, CoordZ, 307u + Octave * 31u);

        // Orthogonal SO(3) rotation eliminates grid-axis alignment across octaves.
        const float RotatedX =  0.80f * CoordX - 0.48f * CoordY + 0.36f * CoordZ;
        const float RotatedY =  0.48f * CoordX + 0.86f * CoordY + 0.16f * CoordZ;
        const float RotatedZ = -0.36f * CoordX + 0.04f * CoordY + 0.93f * CoordZ;
        CoordX    = RotatedX * 2.03f + 7.1f;
        CoordY    = RotatedY * 2.03f - 5.3f;
        CoordZ    = RotatedZ * 2.03f + 3.7f;
        Amplitude *= 0.50f;
    }
    return Potential;
}

[[nodiscard]] Vector3f EvaluateDivergenceFreeCurlVelocity(const Vector3f& WorldPositionMetres,
                                                          float           SpatialScaleMetres,
                                                          float           AmplitudeMetresPerSec,
                                                          float           SequenceSeconds) noexcept
{
    const float InverseScale = 1.0f / std::max(50.0f, SpatialScaleMetres);
    const Vector3f Scaled{
        WorldPositionMetres.XCoord * InverseScale,
        WorldPositionMetres.YCoord * InverseScale,
        WorldPositionMetres.ZCoord * InverseScale };

    constexpr float StepDelta   = 0.065f;
    constexpr float InverseSpan = 1.0f / (2.0f * StepDelta);
    const float     Phase       = SequenceSeconds * 0.25f;

    const Vector3f PlusX  = EvaluateTurbulenceVectorPotential(Vector3f{ Scaled.XCoord + StepDelta, Scaled.YCoord, Scaled.ZCoord }, Phase);
    const Vector3f MinusX = EvaluateTurbulenceVectorPotential(Vector3f{ Scaled.XCoord - StepDelta, Scaled.YCoord, Scaled.ZCoord }, Phase);
    const Vector3f PlusY  = EvaluateTurbulenceVectorPotential(Vector3f{ Scaled.XCoord, Scaled.YCoord + StepDelta, Scaled.ZCoord }, Phase);
    const Vector3f MinusY = EvaluateTurbulenceVectorPotential(Vector3f{ Scaled.XCoord, Scaled.YCoord - StepDelta, Scaled.ZCoord }, Phase);
    const Vector3f PlusZ  = EvaluateTurbulenceVectorPotential(Vector3f{ Scaled.XCoord, Scaled.YCoord, Scaled.ZCoord + StepDelta }, Phase);
    const Vector3f MinusZ = EvaluateTurbulenceVectorPotential(Vector3f{ Scaled.XCoord, Scaled.YCoord, Scaled.ZCoord - StepDelta }, Phase);

    const float DPsiZDy = (PlusY.ZCoord - MinusY.ZCoord) * InverseSpan;
    const float DPsiYDz = (PlusZ.YCoord - MinusZ.YCoord) * InverseSpan;
    const float DPsiXDz = (PlusZ.XCoord - MinusZ.XCoord) * InverseSpan;
    const float DPsiZDx = (PlusX.ZCoord - MinusX.ZCoord) * InverseSpan;
    const float DPsiYDx = (PlusX.YCoord - MinusX.YCoord) * InverseSpan;
    const float DPsiXDy = (PlusY.XCoord - MinusY.XCoord) * InverseSpan;

    return Vector3f{
        (DPsiZDy - DPsiYDz) * AmplitudeMetresPerSec,
        (DPsiXDz - DPsiZDx) * AmplitudeMetresPerSec,
        (DPsiYDx - DPsiXDy) * AmplitudeMetresPerSec * 0.38f };
}

[[nodiscard]] Vector3f EvaluateKinematicVelocity(const AtmosphericWindField& Field,
                                                 const Vector3f&             WorldPositionMetres,
                                                 float                       SequenceSeconds,
                                                 AdvectionMediumTarget       TargetMedium,
                                                 bool                        IncludeCurlTurbulence,
                                                 float&                      OutConvergence) noexcept
{
    OutConvergence = 0.0f;
    if (!Field.FieldEnabled)
        return Vector3f{ 0.0f, 0.0f, 0.0f };

    // ① Background geostrophic Ekman boundary-layer profile (power-law speed shear + altitude directional veering).
    const float ClampedAltitude = std::max(10.0f, WorldPositionMetres.ZCoord);
    const float ReferenceAlt    = std::max(100.0f, Field.ReferenceAltitudeMetres);
    const float SpeedShear      = std::clamp(std::pow(ClampedAltitude / ReferenceAlt, Field.ShearExponent), 0.25f, 2.35f);
    const float VeerAngle       = Field.EkmanVeeringRadians * (1.0f - std::exp(-ClampedAltitude / ReferenceAlt));
    const float CosVeer         = std::cos(VeerAngle);
    const float SinVeer         = std::sin(VeerAngle);

    Vector3f TotalVelocity{
        (Field.GeostrophicVelocity.XCoord * CosVeer - Field.GeostrophicVelocity.YCoord * SinVeer) * SpeedShear,
        (Field.GeostrophicVelocity.XCoord * SinVeer + Field.GeostrophicVelocity.YCoord * CosVeer) * SpeedShear,
        Field.GeostrophicVelocity.ZCoord };

    // Inspect whether any active source carries SoloCondition.
    bool SoloFilterActive = false;
    for (const ForceSourceDescriptor& Source : Field.SourceEntries)
    {
        if (Source.ActivationEnabled && Source.AtmosphereCouplingEnabled && Source.SoloCondition)
        {
            SoloFilterActive = true;
            break;
        }
    }

    // ② Accumulate spatial ForceField contributors (jets, Lamb-Oseen cyclones, buoyant plumes, gravity-wave strata).
    for (const ForceSourceDescriptor& Source : Field.SourceEntries)
    {
        if (!Source.ActivationEnabled || !Source.AtmosphereCouplingEnabled)
            continue;
        if (SoloFilterActive && !Source.SoloCondition)
            continue;

        const float CouplingGain = (TargetMedium == AdvectionMediumTarget::SkyAerosolCirrus)
                                 ? Source.SkyAerosolCouplingGain
                                 : Source.CloudAdvectionGain;
        if ( std::fabs(CouplingGain) <= 1.0e-5f )
            continue;

        const Vector3f Displacement = SubtractVector(WorldPositionMetres, Source.OriginMetres);
        const float    VerticalSpan = std::max(250.0f, Source.VerticalScaleMetres);
        const float    VertRatio    = Displacement.ZCoord / VerticalSpan;
        const float    VerticalWeight = Source.AltitudeProjectionEnabled
                                      ? (1.0f / (1.0f + 0.45f * VertRatio * VertRatio))
                                      : std::exp(-VertRatio * VertRatio);

        const float InnerRadius = std::max(50.0f, Source.InnerRadiusMetres);
        const float OuterRadius = std::max(InnerRadius * 1.35f, Source.OuterRadiusMetres);

        switch (Source.Category)
        {
            case ForceSourceCategory::DirectionalJet:
            {
                const Vector3f JetAxis = NormalizeVector(Source.AxisDirection);
                const float    AlongJet = DotVector(Displacement, JetAxis);
                const Vector3f CrossVec = SubtractVector(Displacement, ScaleVector(JetAxis, AlongJet));
                const float    LateralDist = std::sqrt(CrossVec.XCoord * CrossVec.XCoord + CrossVec.YCoord * CrossVec.YCoord);
                if (LateralDist >= OuterRadius)
                    break;

                const float OuterNorm  = std::clamp(1.0f - LateralDist / OuterRadius, 0.0f, 1.0f);
                const float OuterFade  = OuterNorm * OuterNorm * (3.0f - 2.0f * OuterNorm);
                const float AxisBell   = std::exp(-(LateralDist * LateralDist) / (2.0f * InnerRadius * InnerRadius));
                const float AlongFade  = 1.0f / (1.0f + (AlongJet * AlongJet) / (OuterRadius * OuterRadius * 2.25f));
                const float JetSpeed   = Source.PeakVelocityMetresPerSecond * AxisBell * OuterFade * AlongFade * VerticalWeight * CouplingGain;

                // Subtle meandering Rossby/Kelvin-Helmholtz transverse wave along the jet axis.
                const Vector3f PerpHorizontal = NormalizeVector(Vector3f{ -JetAxis.YCoord, JetAxis.XCoord, 0.0f });
                const float    MeanderPhase   = AlongJet / std::max(400.0f, InnerRadius * 1.6f) - SequenceSeconds * 0.035f;
                const float    MeanderSpeed   = 0.24f * JetSpeed * std::sin(MeanderPhase);

                TotalVelocity = AddVector(TotalVelocity, AddVector(ScaleVector(JetAxis, JetSpeed), ScaleVector(PerpHorizontal, MeanderSpeed)));
                OutConvergence += 0.35f * AxisBell * OuterFade * VerticalWeight;
                break;
            }

            case ForceSourceCategory::CyclonicCirculation:
            {
                const float RadialSq   = Displacement.XCoord * Displacement.XCoord + Displacement.YCoord * Displacement.YCoord;
                const float RadialDist = std::sqrt(RadialSq);
                if (RadialDist >= OuterRadius)
                    break;

                const float OuterNorm = std::clamp(1.0f - RadialDist / OuterRadius, 0.0f, 1.0f);
                const float OuterFade = OuterNorm * OuterNorm * (3.0f - 2.0f * OuterNorm);

                // Lamb-Oseen viscous tangential velocity: non-singular rigid-body core inside InnerRadius, 1/r tail outside.
                const float Ratio     = RadialDist / InnerRadius;
                const float RatioSq   = Ratio * Ratio;
                const float LambProfile = (Ratio > 1.0e-4f)
                    ? (LambOseenNormalization * (1.0f - std::exp(-LambOseenAlpha * RatioSq)) / Ratio)
                    : (LambOseenNormalization * LambOseenAlpha * Ratio);

                const float SpinSign       = (Source.AxisDirection.ZCoord < 0.0f) ? -1.0f : 1.0f;
                const float TangentialVel  = SpinSign * Source.PeakVelocityMetresPerSecond * LambProfile * OuterFade * VerticalWeight * CouplingGain;

                Vector3f RadialUnit{ 0.0f, 0.0f, 0.0f };
                Vector3f TangentUnit{ 0.0f, 0.0f, 0.0f };
                if (RadialDist > 1.0e-4f)
                {
                    const float InvR = 1.0f / RadialDist;
                    RadialUnit  = Vector3f{ Displacement.XCoord * InvR, Displacement.YCoord * InvR, 0.0f };
                    TangentUnit = Vector3f{ -RadialUnit.YCoord, RadialUnit.XCoord, 0.0f };
                }

                // Low-level Ekman convergence inward, turning to upper-tropospheric anvil outflow above OriginMetres.Z + VerticalSpan.
                const float InflowAltSign = (Displacement.ZCoord < VerticalSpan * 0.75f) ? -1.0f : 0.45f;
                const float RadialSpeed   = InflowAltSign * std::fabs(Source.PeakVelocityMetresPerSecond)
                                          * Source.RadialInflowRatio * LambProfile * OuterFade * VerticalWeight * CouplingGain;

                // Eyewall ring updraft peaking near r == InnerRadius, calm subsiding eye at r == 0.
                const float EyewallRing  = RatioSq * std::exp(1.0f - RatioSq);
                const float UpdraftSpeed = Source.UpdraftPeakMetresPerSecond * (EyewallRing - 0.22f * std::exp(-2.5f * RatioSq))
                                         * OuterFade * VerticalWeight * CouplingGain;

                TotalVelocity.XCoord += TangentUnit.XCoord * TangentialVel + RadialUnit.XCoord * RadialSpeed;
                TotalVelocity.YCoord += TangentUnit.YCoord * TangentialVel + RadialUnit.YCoord * RadialSpeed;
                TotalVelocity.ZCoord += UpdraftSpeed;

                OutConvergence += std::clamp((EyewallRing * 0.95f - 0.30f * std::exp(-3.0f * RatioSq)) * OuterFade * VerticalWeight, -1.0f, 1.0f);
                break;
            }

            case ForceSourceCategory::RadialPlume:
            {
                const float RadialDist = std::sqrt(Displacement.XCoord * Displacement.XCoord + Displacement.YCoord * Displacement.YCoord);
                if (RadialDist >= OuterRadius)
                    break;

                const float OuterNorm   = std::clamp(1.0f - RadialDist / OuterRadius, 0.0f, 1.0f);
                const float OuterFade   = OuterNorm * OuterNorm * (3.0f - 2.0f * OuterNorm);
                const float PlumeColumn = std::exp(-(RadialDist * RadialDist) / (2.0f * InnerRadius * InnerRadius));
                const float DivergeSign = std::tanh(Displacement.ZCoord / std::max(200.0f, VerticalSpan * 0.35f));

                if (RadialDist > 1.0e-4f)
                {
                    const float InvR       = 1.0f / RadialDist;
                    const float OutflowVel = Source.PeakVelocityMetresPerSecond * DivergeSign
                                           * (RadialDist / InnerRadius) * PlumeColumn * OuterFade * CouplingGain;
                    TotalVelocity.XCoord += Displacement.XCoord * InvR * OutflowVel;
                    TotalVelocity.YCoord += Displacement.YCoord * InvR * OutflowVel;
                }
                TotalVelocity.ZCoord += Source.UpdraftPeakMetresPerSecond * PlumeColumn * OuterFade * VerticalWeight * CouplingGain;
                OutConvergence       += PlumeColumn * OuterFade * VerticalWeight;
                break;
            }

            case ForceSourceCategory::ShearWaveStratum:
            {
                const Vector3f WaveAxis  = NormalizeVector(Source.AxisDirection);
                const float    PhasePos  = DotVector(WorldPositionMetres, WaveAxis) / std::max(300.0f, InnerRadius)
                                         - SequenceSeconds * 0.045f;
                const float    WaveCrest = std::sin(PhasePos);
                const float    ShearGain = Source.PeakVelocityMetresPerSecond * VerticalWeight * CouplingGain;

                TotalVelocity.XCoord += WaveAxis.XCoord * ShearGain * (0.65f + 0.35f * WaveCrest);
                TotalVelocity.YCoord += WaveAxis.YCoord * ShearGain * (0.65f + 0.35f * WaveCrest);
                TotalVelocity.ZCoord += Source.UpdraftPeakMetresPerSecond * WaveCrest * VerticalWeight * CouplingGain;
                OutConvergence       += 0.45f * WaveCrest * VerticalWeight;
                break;
            }
        }
    }

    // ③ Sub-grid divergence-free 3D curl turbulence so cloud margins and sky cirrus filaments billow organically.
    if (IncludeCurlTurbulence && Field.CurlTurbulenceEnabled && Field.CurlAmplitudeMetresPerSec > 1.0e-4f)
    {
        const float MediumScale = (TargetMedium == AdvectionMediumTarget::SkyAerosolCirrus) ? 1.25f : 1.0f;
        const Vector3f CurlVel  = EvaluateDivergenceFreeCurlVelocity(
            WorldPositionMetres,
            Field.CurlSpatialScaleMetres,
            Field.CurlAmplitudeMetresPerSec * MediumScale,
            SequenceSeconds);
        TotalVelocity = AddVector(TotalVelocity, CurlVel);
    }

    return TotalVelocity;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConfigureDefaultWindField(AtmosphericWindField& Field) noexcept
{
    Field = AtmosphericWindField{};
}

void AppendForceSource(AtmosphericWindField& Field, const ForceSourceDescriptor& Source)
{
    Field.SourceEntries.push_back(Source);
}

WindSampleEvaluation EvaluateWindSample(const AtmosphericWindField& Field,
                                        const Vector3f&             WorldPositionMetres,
                                        float                       SequenceSeconds,
                                        AdvectionMediumTarget       TargetMedium) noexcept
{
    WindSampleEvaluation Sample{};
    if (!Field.FieldEnabled)
        return Sample;

    float ConvergenceCenter = 0.0f;
    Sample.VelocityMetresPerSecond = EvaluateKinematicVelocity(
        Field, WorldPositionMetres, SequenceSeconds, TargetMedium, true, ConvergenceCenter);
    Sample.SpeedMetresPerSecond   = VectorLength(Sample.VelocityMetresPerSecond);
    Sample.UpdraftMetresPerSecond = Sample.VelocityMetresPerSecond.ZCoord;
    Sample.ConvergenceScalar      = std::clamp(ConvergenceCenter, -1.0f, 1.0f);

    // Central horizontal vorticity estimate on the smooth analytical flow (avoids redundant 6-tap curl stencils).
    constexpr float ProbeSpanMetres = 16.0f;
    float DiscardConv = 0.0f;
    const Vector3f SmoothCenter = EvaluateKinematicVelocity(
        Field, WorldPositionMetres, SequenceSeconds, TargetMedium, false, DiscardConv);
    const Vector3f VelPlusX = EvaluateKinematicVelocity(
        Field,
        Vector3f{ WorldPositionMetres.XCoord + ProbeSpanMetres, WorldPositionMetres.YCoord, WorldPositionMetres.ZCoord },
        SequenceSeconds,
        TargetMedium,
        false,
        DiscardConv);
    const Vector3f VelPlusY = EvaluateKinematicVelocity(
        Field,
        Vector3f{ WorldPositionMetres.XCoord, WorldPositionMetres.YCoord + ProbeSpanMetres, WorldPositionMetres.ZCoord },
        SequenceSeconds,
        TargetMedium,
        false,
        DiscardConv);

    const float InvSpan = 1.0f / ProbeSpanMetres;
    const float DvDx    = (VelPlusX.YCoord - SmoothCenter.YCoord) * InvSpan;
    const float DuDy    = (VelPlusY.XCoord - SmoothCenter.XCoord) * InvSpan;
    const float DwDx    = (VelPlusX.ZCoord - SmoothCenter.ZCoord) * InvSpan;
    const float DwDy    = (VelPlusY.ZCoord - SmoothCenter.ZCoord) * InvSpan;

    Sample.VorticityPerSecond = Vector3f{ DwDy, -DwDx, DvDx - DuDy };
    return Sample;
}

Vector3f IntegrateStreamlineBackward(const AtmosphericWindField& Field,
                                     const Vector3f&             WorldPositionMetres,
                                     float                       SequenceSeconds,
                                     float                       AdvectionSpanSeconds,
                                     uint32_t                    IntegrationStepCount,
                                     AdvectionMediumTarget       TargetMedium) noexcept
{
    if (!Field.FieldEnabled || AdvectionSpanSeconds <= 1.0e-4f || IntegrationStepCount == 0u)
        return WorldPositionMetres;

    const uint32_t ClampedSteps = std::clamp(IntegrationStepCount, 1u, 12u);
    const float    StepSeconds  = AdvectionSpanSeconds / static_cast<float>(ClampedSteps);

    Vector3f MaterialPoint = WorldPositionMetres;
    float    CurrentClock  = SequenceSeconds;
    float    DiscardConv   = 0.0f;

    for (uint32_t StepIndex = 0u; StepIndex < ClampedSteps; ++StepIndex)
    {
        const Vector3f VelocityStart = EvaluateKinematicVelocity(Field, MaterialPoint, CurrentClock, TargetMedium, false, DiscardConv);
        const Vector3f MidpointPos   = SubtractVector(MaterialPoint, ScaleVector(VelocityStart, 0.5f * StepSeconds));
        const Vector3f VelocityMid   = EvaluateKinematicVelocity(Field, MidpointPos, CurrentClock - 0.5f * StepSeconds, TargetMedium, false, DiscardConv);

        MaterialPoint = SubtractVector(MaterialPoint, ScaleVector(VelocityMid, StepSeconds));
        CurrentClock -= StepSeconds;
    }

    // Apply divergence-free sub-grid 3D curl eddy displacement once over the effective eddy correlation timescale.
    if (Field.CurlTurbulenceEnabled && Field.CurlAmplitudeMetresPerSec > 1.0e-4f)
    {
        const float MediumScale   = (TargetMedium == AdvectionMediumTarget::SkyAerosolCirrus) ? 1.25f : 1.0f;
        const float EddyTimeSpan  = std::min(AdvectionSpanSeconds, 65.0f);
        const Vector3f MidTrajPos = ScaleVector(AddVector(WorldPositionMetres, MaterialPoint), 0.5f);
        const Vector3f CurlVel    = EvaluateDivergenceFreeCurlVelocity(
            MidTrajPos,
            Field.CurlSpatialScaleMetres,
            Field.CurlAmplitudeMetresPerSec * MediumScale,
            SequenceSeconds - 0.5f * EddyTimeSpan);
        MaterialPoint = SubtractVector(MaterialPoint, ScaleVector(CurlVel, EddyTimeSpan));
    }

    return MaterialPoint;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              BAKED LAGRANGIAN OFFSET VOLUME
//------------------------------------------------------------------------------------------------------------------------

void BakeAdvectionOffsetVolume(AdvectionOffsetVolume&      Volume,
                               const AtmosphericWindField& Field,
                               AdvectionMediumTarget       TargetMedium,
                               float                       SequenceSeconds,
                               float                       AdvectionSpanSeconds,
                               uint32_t                    IntegrationStepCount,
                               const Vector3f&             OriginMetres,
                               const Vector3f&             CellSizeMetres) noexcept
{
    constexpr uint32_t DimX = AdvectionOffsetVolume::DimensionX;
    constexpr uint32_t DimY = AdvectionOffsetVolume::DimensionY;
    constexpr uint32_t DimZ = AdvectionOffsetVolume::DimensionZ;

    Volume.OriginMetres           = OriginMetres;
    Volume.CellSizeMetres         = Vector3f{
        std::max(25.0f, CellSizeMetres.XCoord),
        std::max(25.0f, CellSizeMetres.YCoord),
        std::max(25.0f, CellSizeMetres.ZCoord) };
    Volume.BakedSequenceSeconds   = SequenceSeconds;
    Volume.AdvectionSpanSeconds   = std::max(0.0f, AdvectionSpanSeconds);
    Volume.DisplacementsMetres.assign(static_cast<size_t>(DimX) * DimY * DimZ, Vector3f{ 0.0f, 0.0f, 0.0f });
    Volume.ConvergenceScalars.assign(static_cast<size_t>(DimX) * DimY * DimZ, 0.0f);
    Volume.ReadyCondition         = false;

    if (!Field.FieldEnabled || Volume.AdvectionSpanSeconds <= 1.0e-3f)
        return;

    for (uint32_t ZIdx = 0u; ZIdx < DimZ; ++ZIdx)
    {
        for (uint32_t YIdx = 0u; YIdx < DimY; ++YIdx)
        {
            for (uint32_t XIdx = 0u; XIdx < DimX; ++XIdx)
            {
                const Vector3f GridPosition = AddVector(
                    Volume.OriginMetres,
                    Vector3f{
                        Volume.CellSizeMetres.XCoord * static_cast<float>(XIdx),
                        Volume.CellSizeMetres.YCoord * static_cast<float>(YIdx),
                        Volume.CellSizeMetres.ZCoord * static_cast<float>(ZIdx) });

                const Vector3f MaterialOrigin = IntegrateStreamlineBackward(
                    Field,
                    GridPosition,
                    SequenceSeconds,
                    Volume.AdvectionSpanSeconds,
                    IntegrationStepCount,
                    TargetMedium);

                Volume.DisplacementsMetres[static_cast<size_t>((ZIdx * DimY + YIdx) * DimX + XIdx)] =
                    SubtractVector(MaterialOrigin, GridPosition);
            }
        }
    }

    // Precompute the horizontal convergence of the displacement field, C = -(dX0/dx + dY0/dy), so sampling is a bilinear fetch.
    for (uint32_t ZIdx = 0u; ZIdx < DimZ; ++ZIdx)
    {
        for (uint32_t YIdx = 0u; YIdx < DimY; ++YIdx)
        {
            const uint32_t RowAbove = (YIdx + 1u < DimY) ? YIdx + 1u : YIdx;
            const uint32_t RowBelow = (YIdx > 0u) ? YIdx - 1u : YIdx;
            const float    RowSpan  = (RowAbove == RowBelow) ? 1.0f : static_cast<float>(RowAbove - RowBelow) * Volume.CellSizeMetres.YCoord;
            for (uint32_t XIdx = 0u; XIdx < DimX; ++XIdx)
            {
                const uint32_t ColRight = (XIdx + 1u < DimX) ? XIdx + 1u : XIdx;
                const uint32_t ColLeft  = (XIdx > 0u) ? XIdx - 1u : XIdx;
                const float    ColSpan  = (ColRight == ColLeft) ? 1.0f : static_cast<float>(ColRight - ColLeft) * Volume.CellSizeMetres.XCoord;

                const Vector3f& Right = Volume.DisplacementsMetres[static_cast<size_t>((ZIdx * DimY + YIdx) * DimX + ColRight)];
                const Vector3f& Left  = Volume.DisplacementsMetres[static_cast<size_t>((ZIdx * DimY + YIdx) * DimX + ColLeft)];
                const Vector3f& Upper = Volume.DisplacementsMetres[static_cast<size_t>((ZIdx * DimY + RowAbove) * DimX + XIdx)];
                const Vector3f& Lower = Volume.DisplacementsMetres[static_cast<size_t>((ZIdx * DimY + RowBelow) * DimX + XIdx)];

                const float Divergence = (Right.XCoord - Left.XCoord) / ColSpan + (Upper.YCoord - Lower.YCoord) / RowSpan;
                Volume.ConvergenceScalars[static_cast<size_t>((ZIdx * DimY + YIdx) * DimX + XIdx)] = std::clamp(-Divergence, -1.0f, 1.0f);
            }
        }
    }

    Volume.ReadyCondition = true;
}

bool SampleAdvectionOffset(const AdvectionOffsetVolume& Volume,
                           const Vector3f&             WorldPositionMetres,
                           Vector3f&                   OutDisplacementMetres,
                           float&                      OutConvergenceScalar) noexcept
{
    OutDisplacementMetres = Vector3f{ 0.0f, 0.0f, 0.0f };
    OutConvergenceScalar  = 0.0f;
    if (!Volume.ReadyCondition || Volume.DisplacementsMetres.empty())
        return false;

    constexpr uint32_t DimX = AdvectionOffsetVolume::DimensionX;
    constexpr uint32_t DimY = AdvectionOffsetVolume::DimensionY;
    constexpr uint32_t DimZ = AdvectionOffsetVolume::DimensionZ;

    // Grid-relative continuous coordinates clamped to the baked volume. The Lagrangian displacement varies smoothly at synoptic
    // scales, so clamping the outermost cell is physically preferable to dropping the field and re-integrating exactly.
    const float GridX = std::clamp((WorldPositionMetres.XCoord - Volume.OriginMetres.XCoord) / Volume.CellSizeMetres.XCoord,
                                   0.0f, static_cast<float>(DimX - 1u));
    const float GridY = std::clamp((WorldPositionMetres.YCoord - Volume.OriginMetres.YCoord) / Volume.CellSizeMetres.YCoord,
                                   0.0f, static_cast<float>(DimY - 1u));
    const float GridZ = std::clamp((WorldPositionMetres.ZCoord - Volume.OriginMetres.ZCoord) / Volume.CellSizeMetres.ZCoord,
                                   0.0f, static_cast<float>(DimZ - 1u));

    const uint32_t CellX = std::min(DimX - 2u, static_cast<uint32_t>(GridX));
    const uint32_t CellY = std::min(DimY - 2u, static_cast<uint32_t>(GridY));
    const uint32_t CellZ = std::min(DimZ - 2u, static_cast<uint32_t>(GridZ));

    const float FracX = std::clamp(GridX - static_cast<float>(CellX), 0.0f, 1.0f);
    const float FracY = std::clamp(GridY - static_cast<float>(CellY), 0.0f, 1.0f);
    const float FracZ = std::clamp(GridZ - static_cast<float>(CellZ), 0.0f, 1.0f);

    auto Fetch = [&](uint32_t XIdx, uint32_t YIdx, uint32_t ZIdx) noexcept -> const Vector3f&
    {
        return Volume.DisplacementsMetres[static_cast<size_t>((ZIdx * DimY + YIdx) * DimX + XIdx)];
    };
    auto MixVector = [](const Vector3f& Low, const Vector3f& High, float Alpha) noexcept -> Vector3f
    {
        return Vector3f{
            Low.XCoord + (High.XCoord - Low.XCoord) * Alpha,
            Low.YCoord + (High.YCoord - Low.YCoord) * Alpha,
            Low.ZCoord + (High.ZCoord - Low.ZCoord) * Alpha };
    };

    const Vector3f SlabLow = MixVector(
        MixVector(Fetch(CellX, CellY, CellZ), Fetch(CellX + 1u, CellY, CellZ), FracX),
        MixVector(Fetch(CellX, CellY + 1u, CellZ), Fetch(CellX + 1u, CellY + 1u, CellZ), FracX),
        FracY);
    const Vector3f SlabHigh = MixVector(
        MixVector(Fetch(CellX, CellY, CellZ + 1u), Fetch(CellX + 1u, CellY, CellZ + 1u), FracX),
        MixVector(Fetch(CellX, CellY + 1u, CellZ + 1u), Fetch(CellX + 1u, CellY + 1u, CellZ + 1u), FracX),
        FracY);
    OutDisplacementMetres = MixVector(SlabLow, SlabHigh, FracZ);

    auto FetchConvergence = [&](uint32_t XIdx, uint32_t YIdx, uint32_t ZIdx) noexcept -> float
    {
        return Volume.ConvergenceScalars[static_cast<size_t>((ZIdx * DimY + YIdx) * DimX + XIdx)];
    };
    auto MixScalar = [](float Low, float High, float Alpha) noexcept -> float { return Low + (High - Low) * Alpha; };

    const float SlabConvLow = MixScalar(
        MixScalar(FetchConvergence(CellX, CellY, CellZ), FetchConvergence(CellX + 1u, CellY, CellZ), FracX),
        MixScalar(FetchConvergence(CellX, CellY + 1u, CellZ), FetchConvergence(CellX + 1u, CellY + 1u, CellZ), FracX),
        FracY);
    const float SlabConvHigh = MixScalar(
        MixScalar(FetchConvergence(CellX, CellY, CellZ + 1u), FetchConvergence(CellX + 1u, CellY, CellZ + 1u), FracX),
        MixScalar(FetchConvergence(CellX, CellY + 1u, CellZ + 1u), FetchConvergence(CellX + 1u, CellY + 1u, CellZ + 1u), FracX),
        FracY);
    OutConvergenceScalar = MixScalar(SlabConvLow, SlabConvHigh, FracZ);

    return true;
}

} // namespace Frontier
