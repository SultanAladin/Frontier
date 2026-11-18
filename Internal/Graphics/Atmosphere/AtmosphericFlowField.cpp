/*==============================================================================================================================================
                                                         ATMOSPHERICFLOWFIELD.CPP
==============================================================================================================================================*/
// 🧩 Implementation of physical 3D force-field atmospheric velocity evaluation, divergence-free curl turbulence, Ekman wind shear, and
//    4th-order Runge-Kutta semi-Lagrangian backward streamline advection for clouds, upper-atmosphere aerosol veils, and orbital storms.

#include "Graphics/Atmosphere/AtmosphericFlowField.h"
#include "Graphics/Atmosphere/AtmosphereProfile.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] float EvaluateQuinticFade(float Parameter) noexcept
    {
        const float Clamped = std::clamp(Parameter, 0.0f, 1.0f);
        return Clamped * Clamped * Clamped * (Clamped * (Clamped * 6.0f - 15.0f) + 10.0f);
    }

    [[nodiscard]] float HashLatticeUint(int32_t CellX, int32_t CellY, int32_t CellZ, uint32_t ChannelSeed) noexcept
    {
        uint32_t Mixed = static_cast<uint32_t>(CellX) * 0x27D4EB2Du
                       ^ static_cast<uint32_t>(CellY) * 0x165667B1u
                       ^ static_cast<uint32_t>(CellZ) * 0x9E3779B1u
                       ^ ChannelSeed * 0x85EBCA6Bu;
        Mixed ^= Mixed >> 15u;
        Mixed *= 0x2C1B3C6Du;
        Mixed ^= Mixed >> 12u;
        Mixed *= 0x297A2D39u;
        Mixed ^= Mixed >> 15u;
        return static_cast<float>(Mixed >> 8u) * (2.0f / 16777216.0f) - 1.0f;
    }

    // 📝 C2-continuous quintic-interpolated 3D scalar potential component (smooth first and second derivatives for clean curl).
    [[nodiscard]] float EvaluateSmoothPotential(float CoordX, float CoordY, float CoordZ, uint32_t ChannelSeed) noexcept
    {
        const float FloorX = std::floor(CoordX);
        const float FloorY = std::floor(CoordY);
        const float FloorZ = std::floor(CoordZ);

        const int32_t CellX = static_cast<int32_t>(FloorX);
        const int32_t CellY = static_cast<int32_t>(FloorY);
        const int32_t CellZ = static_cast<int32_t>(FloorZ);

        const float WeightX = EvaluateQuinticFade(CoordX - FloorX);
        const float WeightY = EvaluateQuinticFade(CoordY - FloorY);
        const float WeightZ = EvaluateQuinticFade(CoordZ - FloorZ);

        const auto Blend = [](float Left, float Right, float Alpha) noexcept -> float
        {
            return Left + (Right - Left) * Alpha;
        };

        const float Corner000 = HashLatticeUint(CellX,     CellY,     CellZ,     ChannelSeed);
        const float Corner100 = HashLatticeUint(CellX + 1, CellY,     CellZ,     ChannelSeed);
        const float Corner010 = HashLatticeUint(CellX,     CellY + 1, CellZ,     ChannelSeed);
        const float Corner110 = HashLatticeUint(CellX + 1, CellY + 1, CellZ,     ChannelSeed);
        const float Corner001 = HashLatticeUint(CellX,     CellY,     CellZ + 1, ChannelSeed);
        const float Corner101 = HashLatticeUint(CellX + 1, CellY,     CellZ + 1, ChannelSeed);
        const float Corner011 = HashLatticeUint(CellX,     CellY + 1, CellZ + 1, ChannelSeed);
        const float Corner111 = HashLatticeUint(CellX + 1, CellY + 1, CellZ + 1, ChannelSeed);

        const float SliceLow  = Blend(Blend(Corner000, Corner100, WeightX), Blend(Corner010, Corner110, WeightX), WeightY);
        const float SliceHigh = Blend(Blend(Corner001, Corner101, WeightX), Blend(Corner011, Corner111, WeightX), WeightY);
        return Blend(SliceLow, SliceHigh, WeightZ);
    }

    // 📝 Two-octave domain-rotated vector potential ψ(p) preventing axis-aligned fluid streamlines.
    [[nodiscard]] Vector3f EvaluateVectorPotential(float ScaledX, float ScaledY, float ScaledZ) noexcept
    {
        const float RotatedX =  0.80f * ScaledX - 0.48f * ScaledY + 0.36f * ScaledZ;
        const float RotatedY =  0.48f * ScaledX + 0.86f * ScaledY + 0.16f * ScaledZ;
        const float RotatedZ = -0.36f * ScaledX + 0.04f * ScaledY + 0.93f * ScaledZ;

        const float FineX = RotatedX * 2.07f + 11.3f;
        const float FineY = RotatedY * 2.07f -  6.7f;
        const float FineZ = RotatedZ * 2.07f +  4.9f;

        return Vector3f{
            EvaluateSmoothPotential(ScaledX, ScaledY, ScaledZ, 1u) + 0.45f * EvaluateSmoothPotential(FineX, FineY, FineZ, 4u),
            EvaluateSmoothPotential(ScaledX, ScaledY, ScaledZ, 2u) + 0.45f * EvaluateSmoothPotential(FineX, FineY, FineZ, 5u),
            EvaluateSmoothPotential(ScaledX, ScaledY, ScaledZ, 3u) + 0.45f * EvaluateSmoothPotential(FineX, FineY, FineZ, 6u) };
    }

    // 📝 Evaluate one 3D force-field operator with C2-smooth radial and vertical envelopes.
    void AccumulateFlowSource(
        const FlowSourceDescriptor& Source,
        const Vector3f&             PositionMetres,
        float                       SequenceSeconds,
        float                       MasterGain,
        FlowVelocitySample&         InOutSample) noexcept
    {
        if (!Source.ActivationEnabled || MasterGain <= 1.0e-5f)
            return;

        const float OuterRadius  = std::max(50.0f, Source.OuterRadiusMetres);
        const float InnerRadius  = std::clamp(Source.InnerEyeRadiusMetres, 15.0f, OuterRadius * 0.72f);
        const float VerticalSpan = std::max(100.0f, Source.VerticalHalfSpanMetres);

        const float OffsetX        = PositionMetres.XCoord - Source.WorldCentreMetres.XCoord;
        const float OffsetY        = PositionMetres.YCoord - Source.WorldCentreMetres.YCoord;
        const float OffsetZ        = PositionMetres.ZCoord - Source.WorldCentreMetres.ZCoord;
        const float RadialSquared  = OffsetX * OffsetX + OffsetY * OffsetY;
        const float RadialDistance = std::sqrt(RadialSquared);

        if (RadialDistance >= OuterRadius)
            return;

        // C2 vertical envelope: projected sources gently attenuate over 3.5x VerticalSpan so ground sources drive high clouds & sky veils.
        const float EffectiveSpan    = Source.ProjectAcrossAltitude ? (VerticalSpan * 3.5f) : VerticalSpan;
        const float VerticalFraction = std::abs(OffsetZ) / EffectiveSpan;
        if (VerticalFraction >= 1.0f)
            return;

        const float RadialEnvelope   = 1.0f - EvaluateQuinticFade(RadialDistance / OuterRadius);
        const float VerticalEnvelope = 1.0f - EvaluateQuinticFade(VerticalFraction);
        const float Envelope         = RadialEnvelope * VerticalEnvelope * MasterGain;

        const float InverseRadial = (RadialDistance > 1.0e-3f) ? (1.0f / RadialDistance) : 0.0f;
        const float RadialX       = OffsetX * InverseRadial;
        const float RadialY       = OffsetY * InverseRadial;
        const float TangentX      = -RadialY;
        const float TangentY      =  RadialX;

        switch (Source.Category)
        {
            case FlowSourceCategory::DirectionalJet:
            {
                // ① Localized jet-stream corridor aligned to PrimaryAxis with cross-stream Gaussian concentration.
                const Vector3f JetAxis    = NormalizeVector(Source.PrimaryAxis);
                const float    CrossX     = OffsetX - JetAxis.XCoord * (OffsetX * JetAxis.XCoord + OffsetY * JetAxis.YCoord);
                const float    CrossY     = OffsetY - JetAxis.YCoord * (OffsetX * JetAxis.XCoord + OffsetY * JetAxis.YCoord);
                const float    CrossWidth = std::max(80.0f, InnerRadius * 1.45f);
                const float    Corridor   = std::exp(-(CrossX * CrossX + CrossY * CrossY) / (CrossWidth * CrossWidth));
                const float    Speed      = Source.PeakSpeedMetresPerSec * Envelope * (0.28f + 0.72f * Corridor);

                InOutSample.VelocityMetresPerSec.XCoord += JetAxis.XCoord * Speed;
                InOutSample.VelocityMetresPerSec.YCoord += JetAxis.YCoord * Speed;
                InOutSample.VelocityMetresPerSec.ZCoord += JetAxis.ZCoord * Speed + Source.VerticalLiftSpeed * Envelope * Corridor * 0.35f;
                InOutSample.CondensationDelta           += Source.CondensationGain * Envelope * (Corridor * 0.55f - 0.12f);
                InOutSample.SwirlIntensity              += std::abs(Speed) / CrossWidth;
                break;
            }

            case FlowSourceCategory::CyclonicSwirl:
            {
                // ② Physical Rankine / Holland cyclonic circulation:
                //    - Solid-body tangential spin inside InnerRadius, smooth 1/(1 + (r/R_eye)^1.6) decay outside
                //    - Logarithmic spiral boundary-layer inflow toward the eyewall
                //    - Calm sinking eye subsidence clearing the central eye + rising eyewall ring at r ≈ 1.15 * InnerRadius
                const float SpinSign     = (Source.PrimaryAxis.ZCoord < 0.0f) ? -1.0f : 1.0f;
                const float EyeRatio     = RadialDistance / InnerRadius;
                const float RankineCurve = (2.0f * EyeRatio) / (1.0f + EyeRatio * EyeRatio);
                const float SwirlSpeed   = Source.PeakSpeedMetresPerSec * RankineCurve * Envelope * SpinSign;

                const float InflowMask  = std::clamp((EyeRatio - 0.55f) / 0.85f, 0.0f, 1.0f);
                const float InflowSpeed = -std::abs(Source.PeakSpeedMetresPerSec) * Source.RadialInflowFraction * InflowMask * Envelope;

                const float EyeInterior       = std::exp(-(EyeRatio * EyeRatio) * 1.45f);
                const float EyewallDifference = EyeRatio - 1.18f;
                const float EyewallRing       = std::exp(-(EyewallDifference * EyewallDifference) / 0.34f);

                // Azimuthal logarithmic spiral rainband modulation carried by the rotating cyclone.
                const float Azimuth     = std::atan2(OffsetY, OffsetX);
                const float SpiralPhase = 2.0f * Azimuth + 2.6f * SpinSign * std::log(std::max(0.22f, EyeRatio))
                                          - SpinSign * SequenceSeconds * 0.012f;
                const float SpiralBand  = std::cos(SpiralPhase) * std::clamp((EyeRatio - 0.75f) / 0.80f, 0.0f, 1.0f);

                InOutSample.VelocityMetresPerSec.XCoord += TangentX * SwirlSpeed + RadialX * InflowSpeed;
                InOutSample.VelocityMetresPerSec.YCoord += TangentY * SwirlSpeed + RadialY * InflowSpeed;
                InOutSample.VelocityMetresPerSec.ZCoord += Source.VerticalLiftSpeed * Envelope * (EyewallRing - 1.25f * EyeInterior);
                InOutSample.CondensationDelta           += Source.CondensationGain * Envelope
                                                         * (0.85f * EyewallRing + 0.38f * SpiralBand - 1.65f * EyeInterior);
                InOutSample.SwirlIntensity              += std::abs(SwirlSpeed) / InnerRadius;
                break;
            }

            case FlowSourceCategory::RadialOutflow:
            {
                // ③ Divergent microburst / outflow front pushing cloud matter radially outward and stacking an arcus ring.
                const float EyeRatio    = RadialDistance / InnerRadius;
                const float OutflowRamp = (1.85f * EyeRatio) / (0.65f + EyeRatio * EyeRatio);
                const float RadialSpeed = Source.PeakSpeedMetresPerSec * OutflowRamp * Envelope;

                const float ClearedEye = std::exp(-(EyeRatio * EyeRatio) * 0.88f);
                const float RingFront  = RadialDistance / OuterRadius - 0.56f;
                const float ArcusRing  = std::exp(-(RingFront * RingFront) / 0.045f);

                InOutSample.VelocityMetresPerSec.XCoord += RadialX * RadialSpeed;
                InOutSample.VelocityMetresPerSec.YCoord += RadialY * RadialSpeed;
                InOutSample.VelocityMetresPerSec.ZCoord += Source.VerticalLiftSpeed * Envelope * (ArcusRing * 0.9f - ClearedEye * 1.1f);
                InOutSample.CondensationDelta           += Source.CondensationGain * Envelope * (0.95f * ArcusRing - 1.55f * ClearedEye);
                InOutSample.SwirlIntensity              += std::abs(RadialSpeed) / OuterRadius;
                break;
            }

            case FlowSourceCategory::BuoyantUpdraft:
            {
                // ④ Convective thermal plume: low-level inflow, strong central vertical ascent, anvil-top divergence.
                const float PlumeAxial  = std::exp(-RadialSquared / (InnerRadius * InnerRadius * 1.35f));
                const float HeightSign  = std::clamp(OffsetZ / VerticalSpan, -1.0f, 1.0f);
                const float DivergeRate = Source.PeakSpeedMetresPerSec * Source.RadialInflowFraction * HeightSign * Envelope;
                const float AscentSpeed = Source.VerticalLiftSpeed * PlumeAxial * Envelope;

                InOutSample.VelocityMetresPerSec.XCoord += RadialX * DivergeRate;
                InOutSample.VelocityMetresPerSec.YCoord += RadialY * DivergeRate;
                InOutSample.VelocityMetresPerSec.ZCoord += AscentSpeed;
                InOutSample.CondensationDelta           += Source.CondensationGain * PlumeAxial * Envelope;
                InOutSample.SwirlIntensity              += std::abs(AscentSpeed) / InnerRadius;
                break;
            }

            case FlowSourceCategory::IncompressibleCurl:
            {
                // ⑤ Localized divergence-free 3D eddy volume churning cloud silhouettes without artificial sink collapse.
                const Vector3f EddyVelocity = EvaluateDivergenceFreeCurl3D(
                    PositionMetres,
                    std::max(150.0f, Source.WaveLengthMetres),
                    SequenceSeconds);
                const float    Amplitude    = Source.PeakSpeedMetresPerSec * Envelope;
                InOutSample.VelocityMetresPerSec.XCoord += EddyVelocity.XCoord * Amplitude;
                InOutSample.VelocityMetresPerSec.YCoord += EddyVelocity.YCoord * Amplitude;
                InOutSample.VelocityMetresPerSec.ZCoord += EddyVelocity.ZCoord * Amplitude * 0.45f;
                InOutSample.SwirlIntensity              += std::abs(Amplitude) / std::max(150.0f, Source.WaveLengthMetres);
                break;
            }

            case FlowSourceCategory::AtmosphericWave:
            {
                // ⑥ Gravity / lee wave train: propagating vertical undulation generating periodic lenticular wave crests.
                const Vector3f WaveAxis   = NormalizeVector(Source.PrimaryAxis);
                const float    WaveNumber = Atmosphere::TauConstant / std::max(250.0f, Source.WaveLengthMetres);
                const float    AlongAxis  = OffsetX * WaveAxis.XCoord + OffsetY * WaveAxis.YCoord;
                const float    Phase      = AlongAxis * WaveNumber - SequenceSeconds * (Source.PeakSpeedMetresPerSec * WaveNumber);
                const float    Crest      = std::sin(Phase);

                InOutSample.VelocityMetresPerSec.XCoord += WaveAxis.XCoord * Source.PeakSpeedMetresPerSec * 0.45f * Envelope;
                InOutSample.VelocityMetresPerSec.YCoord += WaveAxis.YCoord * Source.PeakSpeedMetresPerSec * 0.45f * Envelope;
                InOutSample.VelocityMetresPerSec.ZCoord += Source.VerticalLiftSpeed * Crest * Envelope;
                InOutSample.CondensationDelta           += Source.CondensationGain * Crest * Envelope;
                InOutSample.SwirlIntensity              += std::abs(Source.VerticalLiftSpeed * WaveNumber * Envelope);
                break;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool RegisterFlowSource(AtmosphericFlowField& Field, const FlowSourceDescriptor& Descriptor) noexcept
{
    if (Field.SourceCount >= MaximumFlowSourceCount)
        return false;

    Field.Sources[Field.SourceCount]                   = Descriptor;
    Field.Sources[Field.SourceCount].ActivationEnabled = true;
    ++Field.SourceCount;
    return true;
}

void ClearFlowSources(AtmosphericFlowField& Field) noexcept
{
    Field.SourceCount = 0u;
    for (uint32_t Slot = 0u; Slot < MaximumFlowSourceCount; ++Slot)
        Field.Sources[Slot] = FlowSourceDescriptor{};
}

Vector3f EvaluateDivergenceFreeCurl3D(
    const Vector3f& WorldPositionMetres,
    float           WavelengthMetres,
    float           SequenceSeconds) noexcept
{
    const float InverseScale = 1.0f / std::max(80.0f, WavelengthMetres);
    const float TimeDrift    = SequenceSeconds * 0.0045f;
    const float SampleX      = WorldPositionMetres.XCoord * InverseScale + TimeDrift;
    const float SampleY      = WorldPositionMetres.YCoord * InverseScale - TimeDrift * 0.71f;
    const float SampleZ      = WorldPositionMetres.ZCoord * InverseScale + TimeDrift * 0.37f;

    // 📝 Central finite-difference curl ∇ × ψ = (∂ψ_z/∂y − ∂ψ_y/∂z, ∂ψ_x/∂z − ∂ψ_z/∂x, ∂ψ_y/∂x − ∂ψ_x/∂y): divergence vanishes identically.
    constexpr float Step           = 0.035f;
    constexpr float InverseTwoStep = 1.0f / (2.0f * Step);

    const Vector3f PlusX  = EvaluateVectorPotential(SampleX + Step, SampleY,        SampleZ);
    const Vector3f MinusX = EvaluateVectorPotential(SampleX - Step, SampleY,        SampleZ);
    const Vector3f PlusY  = EvaluateVectorPotential(SampleX,        SampleY + Step, SampleZ);
    const Vector3f MinusY = EvaluateVectorPotential(SampleX,        SampleY - Step, SampleZ);
    const Vector3f PlusZ  = EvaluateVectorPotential(SampleX,        SampleY,        SampleZ + Step);
    const Vector3f MinusZ = EvaluateVectorPotential(SampleX,        SampleY,        SampleZ - Step);

    const float DPsiZDy = (PlusY.ZCoord - MinusY.ZCoord) * InverseTwoStep;
    const float DPsiYDz = (PlusZ.YCoord - MinusZ.YCoord) * InverseTwoStep;
    const float DPsiXDz = (PlusZ.XCoord - MinusZ.XCoord) * InverseTwoStep;
    const float DPsiZDx = (PlusX.ZCoord - MinusX.ZCoord) * InverseTwoStep;
    const float DPsiYDx = (PlusX.YCoord - MinusX.YCoord) * InverseTwoStep;
    const float DPsiXDy = (PlusY.XCoord - MinusY.XCoord) * InverseTwoStep;

    return Vector3f{
        (DPsiZDy - DPsiYDz) * 0.55f,
        (DPsiXDz - DPsiZDx) * 0.55f,
        (DPsiYDx - DPsiXDy) * 0.55f };
}

FlowVelocitySample EvaluateAtmosphericFlow(
    const AtmosphericFlowField& Field,
    const Vector3f&             WorldPositionMetres,
    float                       SequenceSeconds) noexcept
{
    FlowVelocitySample Result{};

    // ① Ekman-veered, power-law altitude-sheared synoptic geostrophic wind.
    if (Field.Synoptic.WindEnabled)
    {
        const float Altitude          = std::max(10.0f, WorldPositionMetres.ZCoord);
        const float ReferenceAltitude = std::max(50.0f, Field.Synoptic.ReferenceAltitudeMetres);
        const float SpeedProfile      = std::clamp(std::pow(Altitude / ReferenceAltitude, Field.Synoptic.AltitudeShearExponent), 0.25f, 2.40f);
        const float VeerFraction      = std::clamp((Altitude - ReferenceAltitude) / 6000.0f, -0.5f, 1.2f);
        const float VeerAngle         = VeerFraction * Field.Synoptic.EkmanVeerRadians;
        const float CosineVeer        = std::cos(VeerAngle);
        const float SineVeer          = std::sin(VeerAngle);

        const float ReferenceX = Field.Synoptic.ReferenceVelocity.XCoord;
        const float ReferenceY = Field.Synoptic.ReferenceVelocity.YCoord;
        Result.VelocityMetresPerSec.XCoord += (ReferenceX * CosineVeer - ReferenceY * SineVeer) * SpeedProfile;
        Result.VelocityMetresPerSec.YCoord += (ReferenceX * SineVeer + ReferenceY * CosineVeer) * SpeedProfile;
        Result.VelocityMetresPerSec.ZCoord += Field.Synoptic.ReferenceVelocity.ZCoord;

        // Background incompressible 3D fluid curl turbulence.
        if (Field.Synoptic.CurlSpeedMetresPerSec > 1.0e-4f)
        {
            const Vector3f Curl = EvaluateDivergenceFreeCurl3D(
                WorldPositionMetres,
                Field.Synoptic.CurlWavelengthMetres,
                SequenceSeconds);
            Result.VelocityMetresPerSec.XCoord += Curl.XCoord * Field.Synoptic.CurlSpeedMetresPerSec;
            Result.VelocityMetresPerSec.YCoord += Curl.YCoord * Field.Synoptic.CurlSpeedMetresPerSec;
            Result.VelocityMetresPerSec.ZCoord += Curl.ZCoord * Field.Synoptic.CurlSpeedMetresPerSec * 0.30f;
        }
    }

    // ② Superpose all active 3D force-field sources.
    const uint32_t ActiveSlots = std::min(Field.SourceCount, MaximumFlowSourceCount);
    for (uint32_t Index = 0u; Index < ActiveSlots; ++Index)
    {
        AccumulateFlowSource(
            Field.Sources[Index],
            WorldPositionMetres,
            SequenceSeconds,
            Field.Synoptic.ForceCouplingScale,
            Result);
    }

    return Result;
}

FlowAdvectedSample TraceFlowCharacteristicBackward(
    const AtmosphericFlowField& Field,
    const Vector3f&             WorldPositionMetres,
    float                       SequenceSeconds,
    uint32_t                    SubstepCount) noexcept
{
    FlowAdvectedSample       Advected {};
    const FlowVelocitySample Instant  = EvaluateAtmosphericFlow(Field, WorldPositionMetres, SequenceSeconds);
    Advected.SourcePositionMetres = WorldPositionMetres;
    Advected.LocalVelocity        = Instant.VelocityMetresPerSec;
    Advected.CondensationDelta    = Instant.CondensationDelta;

    const float ClampedSpan = std::clamp(
        SequenceSeconds,
        -Field.Synoptic.MaximumAdvectionSpanSeconds,
         Field.Synoptic.MaximumAdvectionSpanSeconds);

    if (std::abs(ClampedSpan) <= 1.0e-4f)
        return Advected;

    // 📝 Classical 4th-order Runge-Kutta backward streamline solve dX/ds = -u(X, t - s) for s in [0, ClampedSpan].
    //    Preserves curved circular cyclone orbits without outward Euler spiral drift.
    const uint32_t Steps                   = std::clamp(SubstepCount, 1u, 16u);
    const float    StepSeconds             = ClampedSpan / static_cast<float>(Steps);
    Vector3f       ParcelCoord             = WorldPositionMetres;
    float          TimeCursor              = SequenceSeconds;
    float          CondensationAccumulated = 0.5f * Instant.CondensationDelta;

    for (uint32_t StepIndex = 0u; StepIndex < Steps; ++StepIndex)
    {
        const FlowVelocitySample SlopeFirst   = EvaluateAtmosphericFlow(Field, ParcelCoord, TimeCursor);
        const Vector3f           MiddleCoordA = SubtractVector(ParcelCoord, ScaleVector(SlopeFirst.VelocityMetresPerSec, 0.5f * StepSeconds));
        const FlowVelocitySample SlopeSecond  = EvaluateAtmosphericFlow(Field, MiddleCoordA, TimeCursor - 0.5f * StepSeconds);
        const Vector3f           MiddleCoordB = SubtractVector(ParcelCoord, ScaleVector(SlopeSecond.VelocityMetresPerSec, 0.5f * StepSeconds));
        const FlowVelocitySample SlopeThird   = EvaluateAtmosphericFlow(Field, MiddleCoordB, TimeCursor - 0.5f * StepSeconds);
        const Vector3f           EndCoord     = SubtractVector(ParcelCoord, ScaleVector(SlopeThird.VelocityMetresPerSec, StepSeconds));
        const FlowVelocitySample SlopeFourth  = EvaluateAtmosphericFlow(Field, EndCoord, TimeCursor - StepSeconds);

        const Vector3f WeightedVelocity{
            (SlopeFirst.VelocityMetresPerSec.XCoord + 2.0f * SlopeSecond.VelocityMetresPerSec.XCoord
             + 2.0f * SlopeThird.VelocityMetresPerSec.XCoord + SlopeFourth.VelocityMetresPerSec.XCoord) * (1.0f / 6.0f),
            (SlopeFirst.VelocityMetresPerSec.YCoord + 2.0f * SlopeSecond.VelocityMetresPerSec.YCoord
             + 2.0f * SlopeThird.VelocityMetresPerSec.YCoord + SlopeFourth.VelocityMetresPerSec.YCoord) * (1.0f / 6.0f),
            (SlopeFirst.VelocityMetresPerSec.ZCoord + 2.0f * SlopeSecond.VelocityMetresPerSec.ZCoord
             + 2.0f * SlopeThird.VelocityMetresPerSec.ZCoord + SlopeFourth.VelocityMetresPerSec.ZCoord) * (1.0f / 6.0f) };

        ParcelCoord    = SubtractVector(ParcelCoord, ScaleVector(WeightedVelocity, StepSeconds));
        TimeCursor    -= StepSeconds;
        CondensationAccumulated += SlopeSecond.CondensationDelta * (0.5f / static_cast<float>(Steps));
    }

    // Couple dynamical force-field condensation cleanly (instantaneous eyewall/outflow cavity + Lagrangian memory).
    const float RampIn              = std::clamp(std::abs(ClampedSpan) / 45.0f, 0.25f, 1.0f);
    Advected.SourcePositionMetres = ParcelCoord;
    Advected.IntegratedLiftMetres = WorldPositionMetres.ZCoord - ParcelCoord.ZCoord;
    Advected.CondensationDelta    = std::clamp((0.65f * Instant.CondensationDelta + 0.35f * CondensationAccumulated) * RampIn, -1.5f, 1.5f);
    Advected.DisplacementMetres   = VectorLength(SubtractVector(WorldPositionMetres, ParcelCoord));
    return Advected;
}

void EncodeFlowFieldUniformBlock(
    const AtmosphericFlowField& Field,
    float                       SequenceSeconds,
    FlowFieldUniformBlock&      OutUniforms) noexcept
{
    std::memset(&OutUniforms, 0, sizeof(FlowFieldUniformBlock));

    const float WindGate = Field.Synoptic.WindEnabled ? 1.0f : 0.0f;
    OutUniforms.SynopticVelocityAltitude[0] = Field.Synoptic.ReferenceVelocity.XCoord * WindGate;
    OutUniforms.SynopticVelocityAltitude[1] = Field.Synoptic.ReferenceVelocity.YCoord * WindGate;
    OutUniforms.SynopticVelocityAltitude[2] = Field.Synoptic.ReferenceVelocity.ZCoord * WindGate;
    OutUniforms.SynopticVelocityAltitude[3] = std::max(50.0f, Field.Synoptic.ReferenceAltitudeMetres);

    OutUniforms.ShearVeerCurlPack[0] = Field.Synoptic.AltitudeShearExponent;
    OutUniforms.ShearVeerCurlPack[1] = Field.Synoptic.EkmanVeerRadians;
    OutUniforms.ShearVeerCurlPack[2] = Field.Synoptic.CurlSpeedMetresPerSec * WindGate;
    OutUniforms.ShearVeerCurlPack[3] = std::max(80.0f, Field.Synoptic.CurlWavelengthMetres);

    const uint32_t ActiveSlots = std::min(Field.SourceCount, MaximumFlowSourceCount);
    OutUniforms.ControlClockPack[0] = Field.Synoptic.ForceCouplingScale;
    OutUniforms.ControlClockPack[1] = Field.Synoptic.SkyVeilCouplingScale;
    OutUniforms.ControlClockPack[2] = std::clamp(SequenceSeconds, -Field.Synoptic.MaximumAdvectionSpanSeconds, Field.Synoptic.MaximumAdvectionSpanSeconds);
    OutUniforms.ControlClockPack[3] = static_cast<float>(ActiveSlots);

    for (uint32_t Index = 0u; Index < ActiveSlots; ++Index)
    {
        const FlowSourceDescriptor& SourceDescriptor = Field.Sources[Index];
        FlowSourceUniformEntry&     TargetEntry      = OutUniforms.Sources[Index];
        const Vector3f              UnitAxis         = NormalizeVector(SourceDescriptor.PrimaryAxis);
        const uint32_t              MaskBits         = (SourceDescriptor.ActivationEnabled ? 1u : 0u) | (SourceDescriptor.ProjectAcrossAltitude ? 2u : 0u);

        TargetEntry.CentreOuterRadius[0] = SourceDescriptor.WorldCentreMetres.XCoord;
        TargetEntry.CentreOuterRadius[1] = SourceDescriptor.WorldCentreMetres.YCoord;
        TargetEntry.CentreOuterRadius[2] = SourceDescriptor.WorldCentreMetres.ZCoord;
        TargetEntry.CentreOuterRadius[3] = std::max(50.0f, SourceDescriptor.OuterRadiusMetres);

        TargetEntry.AxisInnerRadius[0] = UnitAxis.XCoord;
        TargetEntry.AxisInnerRadius[1] = UnitAxis.YCoord;
        TargetEntry.AxisInnerRadius[2] = UnitAxis.ZCoord;
        TargetEntry.AxisInnerRadius[3] = std::max(15.0f, SourceDescriptor.InnerEyeRadiusMetres);

        TargetEntry.KinematicsPack[0] = SourceDescriptor.PeakSpeedMetresPerSec;
        TargetEntry.KinematicsPack[1] = SourceDescriptor.RadialInflowFraction;
        TargetEntry.KinematicsPack[2] = SourceDescriptor.VerticalLiftSpeed;
        TargetEntry.KinematicsPack[3] = std::max(100.0f, SourceDescriptor.VerticalHalfSpanMetres);

        TargetEntry.ModulationCategory[0] = SourceDescriptor.CondensationGain;
        TargetEntry.ModulationCategory[1] = std::max(150.0f, SourceDescriptor.WaveLengthMetres);
        TargetEntry.ModulationCategory[2] = static_cast<float>(static_cast<uint32_t>(SourceDescriptor.Category));
        TargetEntry.ModulationCategory[3] = static_cast<float>(MaskBits);
    }
}

} // namespace Frontier
