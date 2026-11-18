/*==============================================================================================================================================
                                                      ATMOSPHERICSCATTERINGSOLVER.CPP
==============================================================================================================================================*/
// 🧩 Implementation of Bruneton-2008 / Hillaire-2020 spherical atmospheric scattering, Neckel & Labs (1994) wavelength-dependent solar
//    limb darkening with continuous coronal/Mie falloff, force-field upper-sky aerosol advection, and orbital space planet integration.

#include "Graphics/Atmosphere/AtmosphericScatteringSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] float MapSubTexelToUnit(float SubCoord, uint32_t Resolution) noexcept
    {
        const float TexelCount = static_cast<float>(Resolution);
        return std::clamp((SubCoord - 0.5f / TexelCount) * (TexelCount / std::max(1.0f, TexelCount - 1.0f)), 0.0f, 1.0f);
    }

    [[nodiscard]] float MapUnitToTexelSafe(float UnitCoord, uint32_t Resolution) noexcept
    {
        const float TexelCount = static_cast<float>(Resolution);
        const float Clamped    = std::clamp(UnitCoord, 0.0f, 1.0f);
        return 0.5f / TexelCount + Clamped * (1.0f - 1.0f / TexelCount);
    }

    [[nodiscard]] float SafeSquareRoot(float Argument) noexcept
    {
        return std::sqrt(std::max(0.0f, Argument));
    }

    // 📝 Evaluate one atmospheric density profile layer. Pure linear layers (ExpScale == 0, such as stratospheric ozone) store
    //    their signed constant offset in Layer[1] so the 10..25..40 km Chappuis ozone tent evaluates without zero-truncation above 25 km.
    [[nodiscard]] float EvaluateDensityLayer(const float Layer[4], float AltitudeKm) noexcept
    {
        float Density = 0.0f;
        if (std::abs(Layer[2]) > 1.0e-6f)
        {
            Density = std::exp(-AltitudeKm / std::max(Layer[1], 1.0e-4f)) * Layer[2] + Layer[3] * AltitudeKm;
        }
        else
        {
            Density = Layer[3] * AltitudeKm + Layer[1];
        }
        return std::clamp(Density, 0.0f, 1.0f);
    }

    [[nodiscard]] float EvaluateTwoLayerProfile(const float Layer0[4], const float Layer1[4], float AltitudeKm) noexcept
    {
        if (AltitudeKm < 0.0f)
            return EvaluateDensityLayer(Layer0, 0.0f);
        if (Layer0[0] <= 0.0f)
            return EvaluateDensityLayer(Layer0, AltitudeKm);
        return (AltitudeKm < Layer0[0]) ? EvaluateDensityLayer(Layer0, AltitudeKm)
                                        : EvaluateDensityLayer(Layer1, AltitudeKm);
    }

    // 📝 Nearest positive ray-sphere intersection distance [km] for origin at radius R and polar cosine μ; returns -1 on miss.
    [[nodiscard]] float IntersectRadialSphereNearest(float RadiusKm, float PolarCosine, float SphereRadiusKm) noexcept
    {
        const float HalfB        = RadiusKm * PolarCosine;
        const float Discriminant = HalfB * HalfB - (RadiusKm * RadiusKm - SphereRadiusKm * SphereRadiusKm);
        if (Discriminant < 0.0f)
            return -1.0f;

        const float Root         = std::sqrt(Discriminant);
        const float NearDistance = -HalfB - Root;
        const float FarDistance  = -HalfB + Root;
        if (FarDistance < 0.0f)
            return -1.0f;
        return (NearDistance >= 0.0f) ? NearDistance : FarDistance;
    }

    // 📝 Full 3D ray-sphere intersection span [NearT, FarT] in km; returns false when sphere lies behind ray or is missed.
    [[nodiscard]] bool IntersectSphereSpan3D(
        const Vector3f& OriginKm,
        const Vector3f& UnitDirection,
        float           SphereRadiusKm,
        float&          OutNearKm,
        float&          OutFarKm) noexcept
    {
        const float HalfB        = DotVector(OriginKm, UnitDirection);
        const float ConstantC    = DotVector(OriginKm, OriginKm) - SphereRadiusKm * SphereRadiusKm;
        const float Discriminant = HalfB * HalfB - ConstantC;
        if (Discriminant < 0.0f)
            return false;

        const float Root = std::sqrt(Discriminant);
        OutNearKm = -HalfB - Root;
        OutFarKm  = -HalfB + Root;
        return OutFarKm > 0.0f;
    }

    [[nodiscard]] float AdvancePlanetaryRadius(float StartRadiusKm, float PolarCosine, float DistanceKm) noexcept
    {
        return SafeSquareRoot(StartRadiusKm * StartRadiusKm
                            + DistanceKm * DistanceKm
                            + 2.0f * StartRadiusKm * PolarCosine * DistanceKm);
    }

    // 📝 Bilinear lookup in a flat RGB float table with selectable horizontal wrap (REPEAT for SkyView, CLAMP for Transmittance/MS).
    [[nodiscard]] LinearColour SampleTableBilinear(
        const std::vector<float>& TexelsRgb,
        uint32_t                  Width,
        uint32_t                  Height,
        float                     CoordU,
        float                     CoordV,
        bool                      WrapHorizontal) noexcept
    {
        if (TexelsRgb.size() < static_cast<std::size_t>(Width) * Height * 3u)
            return LinearColour{};

        float Fx = CoordU * static_cast<float>(Width) - 0.5f;
        float Fy = std::clamp(CoordV, 0.0f, 1.0f) * static_cast<float>(Height) - 0.5f;

        int32_t     X0 = static_cast<int32_t>(std::floor(Fx));
        int32_t     X1 = X0 + 1;
        const float Tx = Fx - std::floor(Fx);

        if (WrapHorizontal)
        {
            const int32_t SignedWidth = static_cast<int32_t>(Width);
            X0 = ((X0 % SignedWidth) + SignedWidth) % SignedWidth;
            X1 = ((X1 % SignedWidth) + SignedWidth) % SignedWidth;
        }
        else
        {
            X0 = std::clamp(X0, 0, static_cast<int32_t>(Width) - 1);
            X1 = std::clamp(X1, 0, static_cast<int32_t>(Width) - 1);
        }

        const int32_t Y0 = std::clamp(static_cast<int32_t>(std::floor(Fy)), 0, static_cast<int32_t>(Height) - 1);
        const int32_t Y1 = std::clamp(Y0 + 1, 0, static_cast<int32_t>(Height) - 1);
        const float   Ty = std::clamp(Fy - std::floor(Fy), 0.0f, 1.0f);

        const auto ReadTexel = [&](int32_t Column, int32_t Row) noexcept -> LinearColour
        {
            const std::size_t TexelOffset = (static_cast<std::size_t>(Row) * Width + static_cast<std::size_t>(Column)) * 3u;
            return LinearColour{ TexelsRgb[TexelOffset + 0u], TexelsRgb[TexelOffset + 1u], TexelsRgb[TexelOffset + 2u] };
        };

        const LinearColour Top    = InterpolateColour(ReadTexel(X0, Y0), ReadTexel(X1, Y0), Tx);
        const LinearColour Bottom = InterpolateColour(ReadTexel(X0, Y1), ReadTexel(X1, Y1), Tx);
        return InterpolateColour(Top, Bottom, Ty);
    }

    // 📝 Encode (RadiusKm, PolarCosine) -> Bruneton (2008) limb-aware transmittance LUT UV in [0, 1]^2.
    void EncodeTransmittanceUv(
        float  RadiusKm,
        float  PolarCosine,
        float  BottomRadiusKm,
        float  TopRadiusKm,
        float& OutU,
        float& OutV) noexcept
    {
        const float ClampedR     = std::clamp(RadiusKm, BottomRadiusKm, TopRadiusKm);
        const float MaximumChord = SafeSquareRoot(TopRadiusKm * TopRadiusKm - BottomRadiusKm * BottomRadiusKm);
        const float Rho          = SafeSquareRoot(ClampedR * ClampedR - BottomRadiusKm * BottomRadiusKm);

        const float Discriminant  = ClampedR * ClampedR * (PolarCosine * PolarCosine - 1.0f) + TopRadiusKm * TopRadiusKm;
        const float DistanceToTop = std::max(0.0f, -ClampedR * PolarCosine + SafeSquareRoot(Discriminant));

        const float MinimumDistance = TopRadiusKm - ClampedR;
        const float MaximumDistance = Rho + MaximumChord;
        const float XMu             = (MaximumDistance > MinimumDistance + 1.0e-5f) ? std::clamp((DistanceToTop - MinimumDistance) / (MaximumDistance - MinimumDistance), 0.0f, 1.0f) : 0.0f;
        const float XR              = (MaximumChord > 1.0e-5f) ? std::clamp(Rho / MaximumChord, 0.0f, 1.0f) : 0.0f;

        OutU = MapUnitToTexelSafe(XMu, Atmosphere::TransmittanceExtentX);
        OutV = MapUnitToTexelSafe(XR,  Atmosphere::TransmittanceExtentY);
    }

    // 📝 Decode Bruneton (2008) transmittance LUT UV back into (RadiusKm, PolarCosine, PathLengthToTopKm).
    void DecodeTransmittanceUv(
        float  CoordU,
        float  CoordV,
        float  BottomRadiusKm,
        float  TopRadiusKm,
        float& OutRadiusKm,
        float& OutPolarCosine,
        float& OutDistanceToTopKm) noexcept
    {
        const float XMu          = MapSubTexelToUnit(CoordU, Atmosphere::TransmittanceExtentX);
        const float XR           = MapSubTexelToUnit(CoordV, Atmosphere::TransmittanceExtentY);
        const float MaximumChord = SafeSquareRoot(TopRadiusKm * TopRadiusKm - BottomRadiusKm * BottomRadiusKm);
        const float Rho          = MaximumChord * XR;
        const float Radius       = SafeSquareRoot(Rho * Rho + BottomRadiusKm * BottomRadiusKm);

        const float MinimumDistance = TopRadiusKm - Radius;
        const float MaximumDistance = Rho + MaximumChord;
        const float Distance        = MinimumDistance + XMu * (MaximumDistance - MinimumDistance);
        const float Cosine          = (Distance <= 1.0e-5f)
                                      ? 1.0f
                                      : std::clamp((MaximumChord * MaximumChord - Rho * Rho - Distance * Distance) / (2.0f * Radius * Distance), -1.0f, 1.0f);

        OutRadiusKm        = Radius;
        OutPolarCosine     = Cosine;
        OutDistanceToTopKm = Distance;
    }

    // 📝 Exact geometric-limb polar angle θ_limb = π - arcsin(R_b / r), where cos(θ_limb) = -sqrt(1 - (R_b/r)^2).
    [[nodiscard]] float ComputeLimbPolarAngle(float ViewerRadiusKm, float BottomRadiusKm) noexcept
    {
        const float SineNadirCone = std::clamp(BottomRadiusKm / std::max(ViewerRadiusKm, BottomRadiusKm + 1.0e-4f), 0.0f, 1.0f);
        return Atmosphere::PiConstant - std::asin(SineNadirCone);
    }

    // 📝 Golden-ratio Fibonacci lattice direction over the full 4π sphere for isotropic MultiScatter integration.
    [[nodiscard]] Vector3f EvaluateFibonacciDirection(uint32_t SampleIndex, uint32_t SampleCount) noexcept
    {
        constexpr float GoldenAngle = 2.39996322972865332f;
        const float     Vertical    = 1.0f - 2.0f * (static_cast<float>(SampleIndex) + 0.5f) / static_cast<float>(SampleCount);
        const float     Radial      = SafeSquareRoot(1.0f - Vertical * Vertical);
        const float     Azimuth     = static_cast<float>(SampleIndex) * GoldenAngle;
        return Vector3f{ Radial * std::cos(Azimuth), Vertical, Radial * std::sin(Azimuth) };
    }

    // 📝 Single-ray single+multi scattering along [0, PathLengthKm] from (0, StartRadiusKm, 0) in LocalDir.
    [[nodiscard]] LinearColour IntegrateSkyRayRadiometric(
        const AtmosphericLookupTables& Tables,
        const AtmosphereUniformBlock&  Profile,
        float                          StartRadiusKm,
        const Vector3f&                LocalDirection,
        const Vector3f&                SolarDirection,
        float                          PathLengthKm,
        uint32_t                       StepCount,
        LinearColour&                  OutThroughput) noexcept
    {
        OutThroughput = LinearColour{ 1.0f, 1.0f, 1.0f };
        if (PathLengthKm <= 1.0e-5f || StepCount == 0u)
            return LinearColour{};

        const float CosineScatter = DotVector(LocalDirection, SolarDirection);
        const float RayleighPh    = EvaluateRayleighPhaseFunction(CosineScatter);
        const float MiePh         = EvaluateCornetteShanksPhaseFunction(CosineScatter, Profile.MieAsymmetry);

        LinearColour Accumulated        {};
        float        PreviousDistanceKm = 0.0f;

        for (uint32_t Step = 0u; Step < StepCount; ++Step)
        {
            // Mild power-law step distribution puts finer spacing through the dense lower scale heights near the viewer.
            const float NextFraction     = static_cast<float>(Step + 1u) / static_cast<float>(StepCount);
            const float NextDistanceKm   = PathLengthKm * std::pow(NextFraction, 1.25f);
            const float DeltaKm          = std::max(1.0e-5f, NextDistanceKm - PreviousDistanceKm);
            const float MiddleDistanceKm = PreviousDistanceKm + 0.5f * DeltaKm;
            PreviousDistanceKm           = NextDistanceKm;

            const Vector3f SamplePositionKm {
                LocalDirection.XCoord * MiddleDistanceKm,
                StartRadiusKm + LocalDirection.YCoord * MiddleDistanceKm,
                LocalDirection.ZCoord * MiddleDistanceKm };
            const float    SampleRadiusKm   = std::max(Profile.BottomRadius, VectorLength(SamplePositionKm));
            const float    SampleAltitudeKm = SampleRadiusKm - Profile.BottomRadius;

            LinearColour RayleighScatter{};
            LinearColour MieScatter{};
            LinearColour Extinction{};
            EvaluateMediumCoefficients(Profile, SampleAltitudeKm, RayleighScatter, MieScatter, Extinction);

            const float        DeltaMetres       = DeltaKm * 1000.0f;
            const LinearColour StepTransmittance {
                std::exp(-Extinction.Red   * DeltaMetres),
                std::exp(-Extinction.Green * DeltaMetres),
                std::exp(-Extinction.Blue  * DeltaMetres) };

            const Vector3f     LocalUp          = ScaleVector(SamplePositionKm, 1.0f / SampleRadiusKm);
            const float        SolarCosine      = DotVector(LocalUp, SolarDirection);
            const LinearColour SunTransmittance = SampleTransmittanceToSpace(Tables, Profile, SampleRadiusKm, SolarCosine);
            const LinearColour MultiPsi         = ScaleColour(
                SampleMultiScatterFactor(Tables, Profile, SampleRadiusKm, SolarCosine),
                Profile.MultipleScatteringFactor);

            const LinearColour SingleTerm{
                (RayleighScatter.Red   * RayleighPh + MieScatter.Red   * MiePh) * SunTransmittance.Red,
                (RayleighScatter.Green * RayleighPh + MieScatter.Green * MiePh) * SunTransmittance.Green,
                (RayleighScatter.Blue  * RayleighPh + MieScatter.Blue  * MiePh) * SunTransmittance.Blue };

            const LinearColour TotalScatter = AddColour(RayleighScatter, MieScatter);
            const LinearColour InScatter    = AddColour(SingleTerm, MultiplyColour(TotalScatter, MultiPsi));

            // Analytic Hillaire energy-conserving step integral S * (1 - exp(-σ_t * Δs)) / σ_t.
            const LinearColour StepIntegral{
                InScatter.Red   * (1.0f - StepTransmittance.Red)   / std::max(Extinction.Red,   1.0e-9f),
                InScatter.Green * (1.0f - StepTransmittance.Green) / std::max(Extinction.Green, 1.0e-9f),
                InScatter.Blue  * (1.0f - StepTransmittance.Blue)  / std::max(Extinction.Blue,  1.0e-9f) };

            Accumulated   = AddColour(Accumulated, MultiplyColour(OutThroughput, StepIntegral));
            OutThroughput = MultiplyColour(OutThroughput, StepTransmittance);
        }

        return Accumulated;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                  DOMAIN-ROTATED 3D SPHERICAL PLANET & CLOUD NOISE
    //--------------------------------------------------------------------------------------------------------------------

    using Atmosphere::EvaluateRotatedFbm3D;

    // 📝 Evaluate realistic synoptic + force-field planetary cloud cover in [0, 1] on the unit planet sphere (+Y polar axis).
    // 📝 Scene-local planet axis -> geographic axis: rotation about +X mapping the local vertical (0, 1, 0) to the sub-viewer latitude.
    [[nodiscard]] Vector3f ResolveOrbitalGeographicAxis(const Vector3f& LocalAxis) noexcept
    {
        constexpr float SubViewerLatitudeRadians = 0.32f;   // [rad] - ~18°N: subtropical ocean / continent mix beneath the viewer
        const float     CosineTilt               = std::sin(SubViewerLatitudeRadians);
        const float     SineTilt                 = std::cos(SubViewerLatitudeRadians);
        return Vector3f{
            LocalAxis.XCoord,
            LocalAxis.YCoord * CosineTilt - LocalAxis.ZCoord * SineTilt,
            LocalAxis.YCoord * SineTilt + LocalAxis.ZCoord * CosineTilt };
    }

    [[nodiscard]] float EvaluateOrbitalCloudField(
        const Vector3f&             UnitSpherePoint,
        const AtmosphericFlowField* OptionalFlowField,
        float                       CloudCoverage,
        float                       SequenceSeconds) noexcept
    {
        if (CloudCoverage <= 0.005f)
            return 0.0f;

        // Zonal trade winds & mid-latitude westerlies advect the spherical cloud deck continuously with SequenceSeconds.
        const float Latitude         = std::asin(std::clamp(UnitSpherePoint.YCoord, -1.0f, 1.0f));
        const float Longitude        = std::atan2(UnitSpherePoint.ZCoord, UnitSpherePoint.XCoord);
        const float ZonalRate        = std::cos(3.0f * Latitude) * 0.00055f + 0.00028f;
        const float DriftedLongitude = Longitude - SequenceSeconds * ZonalRate;
        const float CosineLatitude   = std::cos(Latitude);

        Vector3f SampleSphere{
            CosineLatitude * std::cos(DriftedLongitude),
            UnitSpherePoint.YCoord,
            CosineLatitude * std::sin(DriftedLongitude) };

        // Embedded physical mesoscale tropical/extratropical cyclone spiral with clear calm eye wall.
        const auto ApplyOrbitalCyclone = [&](float CentreLongitude, float CentreLatitude, float RadiusRadians, float SpinSign, float& EyeClear, float& WallBoost)
        {
            float DeltaLongitude = DriftedLongitude - CentreLongitude;
            if (DeltaLongitude >  Atmosphere::PiConstant) DeltaLongitude -= Atmosphere::TauConstant;
            if (DeltaLongitude < -Atmosphere::PiConstant) DeltaLongitude += Atmosphere::TauConstant;
            const float ScaledLongitude = DeltaLongitude * std::max(0.25f, std::cos(CentreLatitude));
            const float DeltaLatitude   = Latitude - CentreLatitude;
            const float Distance        = std::sqrt(ScaledLongitude * ScaledLongitude + DeltaLatitude * DeltaLatitude);
            if (Distance < RadiusRadians)
            {
                const float Falloff          = (1.0f - Distance / RadiusRadians);
                const float Twist            = SpinSign * Falloff * Falloff * (2.65f + SequenceSeconds * 0.0035f);
                const float TwistCosine      = std::cos(Twist);
                const float TwistSine        = std::sin(Twist);
                const float RotatedLongitude = ScaledLongitude * TwistCosine - DeltaLatitude * TwistSine;
                const float RotatedLatitude  = ScaledLongitude * TwistSine + DeltaLatitude * TwistCosine;
                const float NewLongitude     = CentreLongitude + RotatedLongitude / std::max(0.25f, std::cos(CentreLatitude));
                const float NewLatitude      = std::clamp(CentreLatitude + RotatedLatitude, -1.52f, 1.52f);
                SampleSphere = Vector3f{
                    std::cos(NewLatitude) * std::cos(NewLongitude),
                    std::sin(NewLatitude),
                    std::cos(NewLatitude) * std::sin(NewLongitude) };

                const float EyeRadiusFraction = Distance / (RadiusRadians * 0.13f);
                EyeClear  = std::max(EyeClear, std::exp(-EyeRadiusFraction * EyeRadiusFraction * 1.4f));
                const float WallDifference = Distance / (RadiusRadians * 0.24f) - 1.0f;
                WallBoost = std::max(WallBoost, std::exp(-WallDifference * WallDifference * 2.1f) * 0.42f
                                              + Falloff * 0.22f * (0.5f + 0.5f * std::cos(2.0f * std::atan2(DeltaLatitude, ScaledLongitude) + 4.5f * SpinSign * std::log(std::max(0.08f, Distance / RadiusRadians)))));
            }
        };

        float EyeClear  = 0.0f;
        float WallBoost = 0.0f;
        ApplyOrbitalCyclone( 0.28f,  0.34f, 0.34f,  1.0f, EyeClear, WallBoost);
        ApplyOrbitalCyclone(-0.72f, -0.42f, 0.38f, -1.0f, EyeClear, WallBoost);

        // Couple registered 3D AtmosphericFlowField operators directly into mesoscale orbital cloud advection.
        if (OptionalFlowField != nullptr)
        {
            const Vector3f           TangentWorld {
                SampleSphere.XCoord * 18000.0f,
                SampleSphere.ZCoord * 18000.0f,
                2500.0f };
            const FlowAdvectedSample Flow         = TraceFlowCharacteristicBackward(*OptionalFlowField, TangentWorld, SequenceSeconds * 0.35f, 3u);
            SampleSphere.XCoord += (Flow.SourcePositionMetres.XCoord - TangentWorld.XCoord) * 1.8e-5f;
            SampleSphere.ZCoord += (Flow.SourcePositionMetres.YCoord - TangentWorld.YCoord) * 1.8e-5f;
            SampleSphere         = NormalizeVector(SampleSphere);
            WallBoost           += std::clamp(Flow.CondensationDelta * 0.15f, -0.25f, 0.25f);
        }

        // Domain-warped fluid cloud streaks (ITCZ equatorial band + mid-latitude frontal comma clouds + crisp cellular detail).
        const Vector3f Warp         {
            EvaluateRotatedFbm3D(ScaleVector(SampleSphere, 3.1f), 4u, 301u) - 0.5f,
            EvaluateRotatedFbm3D(ScaleVector(SampleSphere, 3.1f), 4u, 409u) - 0.5f,
            EvaluateRotatedFbm3D(ScaleVector(SampleSphere, 3.1f), 4u, 521u) - 0.5f };
        const Vector3f WarpedPt     = AddVector(ScaleVector(SampleSphere, 4.6f), ScaleVector(Warp, 1.55f));
        const float    MacroSystems = EvaluateRotatedFbm3D(WarpedPt, 6u, 613u);
        const float    FineCells    = EvaluateRotatedFbm3D(ScaleVector(WarpedPt, 3.4f), 4u, 733u);

        // Climatological latitude bands: ITCZ near equator + storm tracks near ±45°, clearer subtropical highs near ±24°.
        const float LatitudeMagnitude = std::abs(Latitude);
        const float ItczBand          = 0.12f * std::exp(-(LatitudeMagnitude * LatitudeMagnitude) / 0.035f);
        const float StormTrack        = 0.14f * std::exp(-((LatitudeMagnitude - 0.78f) * (LatitudeMagnitude - 0.78f)) / 0.09f);
        const float SubtropDry        = -0.09f * std::exp(-((LatitudeMagnitude - 0.40f) * (LatitudeMagnitude - 0.40f)) / 0.045f);

        const float Combined    = MacroSystems * 0.78f + FineCells * 0.28f + ItczBand + StormTrack + SubtropDry + WallBoost;
        const float Threshold   = 0.68f - 0.34f * std::clamp(CloudCoverage, 0.0f, 1.0f);
        const float RawDensity  = std::clamp((Combined - Threshold) / 0.21f, 0.0f, 1.0f);
        const float SmoothCloud = RawDensity * RawDensity * (3.0f - 2.0f * RawDensity);
        return SmoothCloud * (1.0f - 0.94f * EyeClear);
    }

    // 📝 Evaluate wavelength-dependent Neckel & Labs (1994) 5th-order solar photosphere limb darkening for μ = cos(heliocentric angle).
    [[nodiscard]] LinearColour EvaluateNeckelLabsLimbDarkening(float MuPhotosphere) noexcept
    {
        const float Mu  = std::clamp(MuPhotosphere, 0.0f, 1.0f);
        const float Mu2 = Mu * Mu;
        const float Mu3 = Mu2 * Mu;
        const float Mu4 = Mu2 * Mu2;
        const float Mu5 = Mu4 * Mu;

        // Exact published Neckel & Labs (1994, Solar Physics 153) polynomial coefficients at 650 nm (R), 550 nm (G), 450 nm (B).
        const float LimbRed   = 0.34685f + 1.37539f * Mu - 2.04425f * Mu2 + 2.70493f * Mu3 - 1.94290f * Mu4 + 0.55999f * Mu5;
        const float LimbGreen = 0.23698f + 1.37916f * Mu - 1.83226f * Mu2 + 2.34540f * Mu3 - 1.65319f * Mu4 + 0.52391f * Mu5;
        const float LimbBlue  = 0.11903f + 1.24089f * Mu - 1.19977f * Mu2 + 1.33936f * Mu3 - 0.98690f * Mu4 + 0.48739f * Mu5;

        return LinearColour{
            std::clamp(LimbRed,   0.0f, 1.05f),
            std::clamp(LimbGreen, 0.0f, 1.05f),
            std::clamp(LimbBlue,  0.0f, 1.05f) };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool QueryGroundSphereIntersection(
    float RadiusKm,
    float PolarCosine,
    float BottomRadiusKm) noexcept
{
    if (PolarCosine >= 0.0f)
        return false;
    const float Discriminant = RadiusKm * RadiusKm * (PolarCosine * PolarCosine - 1.0f) + BottomRadiusKm * BottomRadiusKm;
    return Discriminant >= 0.0f;
}

void EvaluateMediumCoefficients(
    const AtmosphereUniformBlock& Profile,
    float                         AltitudeKm,
    LinearColour&                 OutRayleighScatter,
    LinearColour&                 OutMieScatter,
    LinearColour&                 OutTotalExtinction) noexcept
{
    const float ClampedAltitude = std::max(0.0f, AltitudeKm);
    const float ThicknessKm     = std::max(1.0f, Profile.TopRadius - Profile.BottomRadius);
    if (ClampedAltitude > ThicknessKm)
    {
        OutRayleighScatter = LinearColour{};
        OutMieScatter      = LinearColour{};
        OutTotalExtinction = LinearColour{};
        return;
    }

    const float RayleighDensity = EvaluateTwoLayerProfile(Profile.RayleighProfileLayer0, Profile.RayleighProfileLayer1, ClampedAltitude);
    const float MieDensity      = EvaluateTwoLayerProfile(Profile.MieProfileLayer0,      Profile.MieProfileLayer1,      ClampedAltitude);
    const float OzoneDensity    = EvaluateTwoLayerProfile(Profile.OzoneProfileLayer0,    Profile.OzoneProfileLayer1,    ClampedAltitude);

    OutRayleighScatter = LinearColour{
        Profile.RayleighScatteringSeaLevel[0] * RayleighDensity,
        Profile.RayleighScatteringSeaLevel[1] * RayleighDensity,
        Profile.RayleighScatteringSeaLevel[2] * RayleighDensity };

    OutMieScatter = LinearColour{
        Profile.MieScatteringSeaLevel[0] * MieDensity,
        Profile.MieScatteringSeaLevel[1] * MieDensity,
        Profile.MieScatteringSeaLevel[2] * MieDensity };

    OutTotalExtinction = LinearColour{
        OutRayleighScatter.Red   + Profile.MieExtinctionSeaLevel[0] * MieDensity + Profile.OzoneAbsorptionPeak[0] * OzoneDensity,
        OutRayleighScatter.Green + Profile.MieExtinctionSeaLevel[1] * MieDensity + Profile.OzoneAbsorptionPeak[1] * OzoneDensity,
        OutRayleighScatter.Blue  + Profile.MieExtinctionSeaLevel[2] * MieDensity + Profile.OzoneAbsorptionPeak[2] * OzoneDensity };
}

float EvaluateRayleighPhaseFunction(float CosineAngle) noexcept
{
    return (3.0f / (16.0f * Atmosphere::PiConstant)) * (1.0f + CosineAngle * CosineAngle);
}

float EvaluateCornetteShanksPhaseFunction(float CosineAngle, float Asymmetry) noexcept
{
    const float G                    = std::clamp(Asymmetry, -0.99f, 0.99f);
    const float GSquared             = G * G;
    const float Numerator            = 3.0f * (1.0f - GSquared) * (1.0f + CosineAngle * CosineAngle);
    const float PhaseDenominatorTerm = std::max(1.0e-4f, 1.0f + GSquared - 2.0f * G * CosineAngle);
    const float Denominator          = 8.0f * Atmosphere::PiConstant * (2.0f + GSquared) * PhaseDenominatorTerm * std::sqrt(PhaseDenominatorTerm);
    return Numerator / Denominator;
}

void SynthesizeAtmosphereFoundationTables(
    const AtmosphereUniformBlock& Profile,
    AtmosphericLookupTables&      OutTables)
{
    constexpr uint32_t TransmittanceWidth  = Atmosphere::TransmittanceExtentX;
    constexpr uint32_t TransmittanceHeight = Atmosphere::TransmittanceExtentY;
    constexpr uint32_t MultiScatterWidth   = Atmosphere::MultiScatterExtentX;
    constexpr uint32_t MultiScatterHeight  = Atmosphere::MultiScatterExtentY;

    // ① Bake 256x64 limb-aware optical transmittance table T(r, μ) (Bruneton 2008 / Hillaire 2020).
    OutTables.TransmittanceRgb.assign(static_cast<std::size_t>(TransmittanceWidth) * TransmittanceHeight * 3u, 1.0f);

    for (uint32_t Row = 0u; Row < TransmittanceHeight; ++Row)
    {
        const float CoordV = (static_cast<float>(Row) + 0.5f) / static_cast<float>(TransmittanceHeight);
        for (uint32_t Column = 0u; Column < TransmittanceWidth; ++Column)
        {
            const float CoordU = (static_cast<float>(Column) + 0.5f) / static_cast<float>(TransmittanceWidth);

            float RadiusKm        = Profile.BottomRadius;
            float PolarCosine     = 1.0f;
            float DistanceToTopKm = 0.0f;
            DecodeTransmittanceUv(CoordU, CoordV, Profile.BottomRadius, Profile.TopRadius, RadiusKm, PolarCosine, DistanceToTopKm);

            LinearColour OpticalDepth{};
            if (DistanceToTopKm > 1.0e-5f)
            {
                constexpr uint32_t StepCount = 40u;
                const float        StepKm    = DistanceToTopKm / static_cast<float>(StepCount);
                for (uint32_t Step = 0u; Step < StepCount; ++Step)
                {
                    const float SampleDistanceKm = (static_cast<float>(Step) + 0.5f) * StepKm;
                    const float SampleRadius     = AdvancePlanetaryRadius(RadiusKm, PolarCosine, SampleDistanceKm);
                    const float SampleAltitudeKm = std::max(0.0f, SampleRadius - Profile.BottomRadius);

                    LinearColour RayScatter{}, MieScatter{}, Extinction{};
                    EvaluateMediumCoefficients(Profile, SampleAltitudeKm, RayScatter, MieScatter, Extinction);

                    OpticalDepth.Red   += Extinction.Red   * StepKm * 1000.0f;
                    OpticalDepth.Green += Extinction.Green * StepKm * 1000.0f;
                    OpticalDepth.Blue  += Extinction.Blue  * StepKm * 1000.0f;
                }
            }

            const std::size_t TexelOffset = (static_cast<std::size_t>(Row) * TransmittanceWidth + Column) * 3u;
            OutTables.TransmittanceRgb[TexelOffset + 0u] = std::exp(-OpticalDepth.Red);
            OutTables.TransmittanceRgb[TexelOffset + 1u] = std::exp(-OpticalDepth.Green);
            OutTables.TransmittanceRgb[TexelOffset + 2u] = std::exp(-OpticalDepth.Blue);
        }
    }

    OutTables.FoundationTablesReady = true;

    // ② Bake 32x32 isotropic infinite-order multiple-scattering table Ψ_ms(r, μ_s) = L_2nd / (1 - f_ms) (Hillaire 2020 §4.1).
    OutTables.MultiScatterRgb.assign(static_cast<std::size_t>(MultiScatterWidth) * MultiScatterHeight * 3u, 0.0f);
    const LinearColour GroundAlbedo = Atmosphere::RetrieveGroundReflectance(Profile);
    const float        ThicknessKm  = std::max(1.0f, Profile.TopRadius - Profile.BottomRadius);
    constexpr float    IsotropicPh  = 1.0f / (4.0f * Atmosphere::PiConstant);
    constexpr float    SphereSolid  = 4.0f * Atmosphere::PiConstant;

    for (uint32_t Row = 0u; Row < MultiScatterHeight; ++Row)
    {
        const float CoordV      = MapSubTexelToUnit((static_cast<float>(Row) + 0.5f) / static_cast<float>(MultiScatterHeight), MultiScatterHeight);
        const float StartRadius = Profile.BottomRadius + std::clamp(CoordV * ThicknessKm, 0.005f, ThicknessKm - 0.005f);

        for (uint32_t Column = 0u; Column < MultiScatterWidth; ++Column)
        {
            const float    CoordU      = MapSubTexelToUnit((static_cast<float>(Column) + 0.5f) / static_cast<float>(MultiScatterWidth), MultiScatterWidth);
            const float    SolarCosine = std::clamp(2.0f * CoordU - 1.0f, -1.0f, 1.0f);
            const Vector3f SolarAxis   { SafeSquareRoot(1.0f - SolarCosine * SolarCosine), SolarCosine, 0.0f };

            constexpr uint32_t DirectionCount = 64u;
            constexpr uint32_t MarchSteps     = 24u;
            LinearColour       SecondOrderSum {};
            LinearColour       TransferSum    {};

            for (uint32_t DirectionIndex = 0u; DirectionIndex < DirectionCount; ++DirectionIndex)
            {
                const Vector3f RayDirection   = EvaluateFibonacciDirection(DirectionIndex, DirectionCount);
                const float    DistanceTop    = IntersectRadialSphereNearest(StartRadius, RayDirection.YCoord, Profile.TopRadius);
                const float    DistanceGround = QueryGroundSphereIntersection(StartRadius, RayDirection.YCoord, Profile.BottomRadius)
                                                ? IntersectRadialSphereNearest(StartRadius, RayDirection.YCoord, Profile.BottomRadius)
                                                : -1.0f;
                const bool     HitsGround     = (DistanceGround > 0.0f);
                const float    PathLengthKm   = HitsGround ? DistanceGround : std::max(0.0f, DistanceTop);
                if (PathLengthKm <= 1.0e-5f)
                    continue;

                const float  StepKm      = PathLengthKm / static_cast<float>(MarchSteps);
                const float  StepMetres  = StepKm * 1000.0f;
                LinearColour Throughput  { 1.0f, 1.0f, 1.0f };
                LinearColour RaySecond   {};
                LinearColour RayTransfer {};

                for (uint32_t Step = 0u; Step < MarchSteps; ++Step)
                {
                    const float    MarchStepDistanceKm    = (static_cast<float>(Step) + 0.5f) * StepKm;
                    const Vector3f PositionKm             {
                        RayDirection.XCoord * MarchStepDistanceKm,
                        StartRadius + RayDirection.YCoord * MarchStepDistanceKm,
                        RayDirection.ZCoord * MarchStepDistanceKm };
                    const float    LightMarchSampleRadius = std::max(Profile.BottomRadius, VectorLength(PositionKm));
                    const float    LightMarchAltitudeKm   = LightMarchSampleRadius - Profile.BottomRadius;

                    LinearColour RayScatter{}, MieScatter{}, Extinction{};
                    EvaluateMediumCoefficients(Profile, LightMarchAltitudeKm, RayScatter, MieScatter, Extinction);
                    const LinearColour TotalScatter = AddColour(RayScatter, MieScatter);

                    const LinearColour StepTransmittance{
                        std::exp(-Extinction.Red   * StepMetres),
                        std::exp(-Extinction.Green * StepMetres),
                        std::exp(-Extinction.Blue  * StepMetres) };

                    const LinearColour ScatterInt{
                        TotalScatter.Red   * (1.0f - StepTransmittance.Red)   / std::max(Extinction.Red,   1.0e-9f),
                        TotalScatter.Green * (1.0f - StepTransmittance.Green) / std::max(Extinction.Green, 1.0e-9f),
                        TotalScatter.Blue  * (1.0f - StepTransmittance.Blue)  / std::max(Extinction.Blue,  1.0e-9f) };

                    const float        SampleSunCosine  = DotVector(ScaleVector(PositionKm, 1.0f / LightMarchSampleRadius), SolarAxis);
                    const LinearColour SunTransmittance = SampleTransmittanceToSpace(OutTables, Profile, LightMarchSampleRadius, SampleSunCosine);

                    RayTransfer = AddColour(RayTransfer, MultiplyColour(Throughput, ScatterInt));
                    RaySecond   = AddColour(RaySecond, ScaleColour(MultiplyColour(MultiplyColour(Throughput, ScatterInt), SunTransmittance), IsotropicPh));
                    Throughput  = MultiplyColour(Throughput, StepTransmittance);
                }

                // Diffuse planetary reflectance bounce along ground-intersecting rays (cool oceanic Earth reflectance).
                if (HitsGround)
                {
                    const Vector3f GroundPositionKm {
                        RayDirection.XCoord * PathLengthKm,
                        StartRadius + RayDirection.YCoord * PathLengthKm,
                        RayDirection.ZCoord * PathLengthKm };
                    const Vector3f GroundNormal     = NormalizeVector(GroundPositionKm);
                    const float    GroundSunCosine  = std::max(0.0f, DotVector(GroundNormal, SolarAxis));
                    if (GroundSunCosine > 0.0f)
                    {
                        const LinearColour SunAtGround  = SampleTransmittanceToSpace(OutTables, Profile, Profile.BottomRadius + 0.005f, GroundSunCosine);
                        const LinearColour GroundBounce = ScaleColour(
                            MultiplyColour(MultiplyColour(Throughput, SunAtGround), GroundAlbedo),
                            GroundSunCosine / Atmosphere::PiConstant);
                        RaySecond = AddColour(RaySecond, GroundBounce);
                    }
                }

                const float SolidAngleWeight = SphereSolid / static_cast<float>(DirectionCount);
                SecondOrderSum = AddColour(SecondOrderSum, ScaleColour(RaySecond,   SolidAngleWeight * IsotropicPh));
                TransferSum    = AddColour(TransferSum,    ScaleColour(RayTransfer, SolidAngleWeight * IsotropicPh));
            }

            const std::size_t TexelOffset = (static_cast<std::size_t>(Row) * MultiScatterWidth + Column) * 3u;
            OutTables.MultiScatterRgb[TexelOffset + 0u] = SecondOrderSum.Red   / std::max(1.0e-3f, 1.0f - TransferSum.Red);
            OutTables.MultiScatterRgb[TexelOffset + 1u] = SecondOrderSum.Green / std::max(1.0e-3f, 1.0f - TransferSum.Green);
            OutTables.MultiScatterRgb[TexelOffset + 2u] = SecondOrderSum.Blue  / std::max(1.0e-3f, 1.0f - TransferSum.Blue);
        }
    }

    OutTables.SkyViewReady = false;
}

void SynthesizeSkyViewTable(
    const AtmosphereUniformBlock& Profile,
    AtmosphericLookupTables&      InOutTables)
{
    if (!InOutTables.FoundationTablesReady)
        SynthesizeAtmosphereFoundationTables(Profile, InOutTables);

    constexpr uint32_t SkyViewWidth  = Atmosphere::SkyViewExtentX;
    constexpr uint32_t SkyViewHeight = Atmosphere::SkyViewExtentY;
    constexpr uint32_t HalfHeight    = SkyViewHeight / 2u;
    InOutTables.SkyViewRgb.assign(static_cast<std::size_t>(SkyViewWidth) * SkyViewHeight * 3u, 0.0f);

    const float ThicknessKm             = std::max(1.0f, Profile.TopRadius - Profile.BottomRadius);
    const float ViewerClampedAltitudeKm = std::clamp(Atmosphere::RetrieveViewerAltitudeKm(Profile), 0.005f, ThicknessKm - 0.05f);
    const float StartRadiusKm           = Profile.BottomRadius + ViewerClampedAltitudeKm;
    const float LimbAngle               = ComputeLimbPolarAngle(StartRadiusKm, Profile.BottomRadius);
    InOutTables.BakedViewerRadiusKm = StartRadiusKm;

    const Vector3f     SolarAxis    = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const LinearColour SolarFlux    { Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };
    const LinearColour GroundAlbedo = Atmosphere::RetrieveGroundReflectance(Profile);

    // Natural night-sky background (OI 557.7 nm + OH/Na airglow, zodiacal light, unresolved starlight): ~22 V mag/arcsec^2 at the
    // vertical, i.e. E_V0 * 10^(-0.4 * 22) / 2.35e-11 sr ≈ 1.8e-8 in solar-calibrated units, green-dominant (OI line). Emitted at all
    // times; it only becomes visible once Rayleigh twilight fades, so no solar gating is applied.
    constexpr float NightBackgroundMagnitude = 22.0f;   // [mag/arcsec^2] - Dark-site V surface brightness at the vertical
    constexpr float SolarApparentMagnitude   = -26.74f; // [mag]          - Johnson V apparent magnitude of the Sun
    constexpr float SteradiansPerArcsecond2  = 2.3504e-11f;
    const float     NightBackgroundLuminance = Atmosphere::SolarIlluminanceCalibration
                                               * std::pow(10.0f, -0.4f * (NightBackgroundMagnitude - SolarApparentMagnitude)) / SteradiansPerArcsecond2;
    const LinearColour NightBackground = ScaleColour(LinearColour{ 0.90f, 1.04f, 0.88f }, NightBackgroundLuminance);

    // First pass: bake upper sky hemisphere (rows HalfHeight .. SkyViewHeight-1) so grazing lower-hemisphere Fresnel water reflection can read exact limb sky.
    for (uint32_t Row = HalfHeight; Row < SkyViewHeight; ++Row)
    {
        const uint32_t UpperRow           = Row - HalfHeight;
        const float    QuadraticParameter = MapSubTexelToUnit((static_cast<float>(UpperRow) + 0.5f) / static_cast<float>(HalfHeight), HalfHeight);
        // Slightly nudge QuadraticParameter=0 inside sky cone so row HalfHeight never clips the solid planet surface.
        const float    PolarAngle    = std::max(0.0f, LimbAngle * (1.0f - QuadraticParameter * QuadraticParameter) - 1.5e-4f);
        const float    CosinePolar   = std::cos(PolarAngle);
        const float    SinePolar     = std::sin(PolarAngle);
        const float    DistanceToTop = std::max(0.0f, IntersectRadialSphereNearest(StartRadiusKm, CosinePolar, Profile.TopRadius));
        const float    AirglowLimb   = 1.0f + 0.85f * std::exp(-std::max(0.0f, CosinePolar) * 5.0f);

        for (uint32_t Column = 0u; Column < SkyViewWidth; ++Column)
        {
            const float    CoordU        = (static_cast<float>(Column) + 0.5f) / static_cast<float>(SkyViewWidth);
            const float    Azimuth       = (CoordU * 2.0f - 1.0f) * Atmosphere::PiConstant;
            const Vector3f ViewDirection { SinePolar * std::cos(Azimuth), CosinePolar, SinePolar * std::sin(Azimuth) };

            LinearColour       Throughput   {};
            const LinearColour UnitRadiance = IntegrateSkyRayRadiometric(
                InOutTables, Profile, StartRadiusKm, ViewDirection, SolarAxis, DistanceToTop, 48u, Throughput);

            LinearColour Calibrated = MultiplyColour(UnitRadiance, SolarFlux);
            Calibrated              = AddColour(Calibrated, ScaleColour(MultiplyColour(NightBackground, Throughput), AirglowLimb));

            const std::size_t TexelOffset = (static_cast<std::size_t>(Row) * SkyViewWidth + Column) * 3u;
            InOutTables.SkyViewRgb[TexelOffset + 0u] = std::max(0.0f, Calibrated.Red);
            InOutTables.SkyViewRgb[TexelOffset + 1u] = std::max(0.0f, Calibrated.Green);
            InOutTables.SkyViewRgb[TexelOffset + 2u] = std::max(0.0f, Calibrated.Blue);
        }
    }

    // Second pass: bake lower planetary hemisphere (rows 0 .. HalfHeight-1) with exact Rayleigh+Mie aerial perspective + oceanic Fresnel & diffuse bounce.
    // Guarantees C0 physical continuity at the geometric limb and strictly zero muddy brown/beige lower-hemisphere bands.
    for (uint32_t Row = 0u; Row < HalfHeight; ++Row)
    {
        const uint32_t MirrorOffset       = (HalfHeight - 1u) - Row;
        const float    QuadraticParameter = MapSubTexelToUnit((static_cast<float>(MirrorOffset) + 0.5f) / static_cast<float>(HalfHeight), HalfHeight);
        const float    PolarAngle         = std::min(Atmosphere::PiConstant,
                                               LimbAngle + (Atmosphere::PiConstant - LimbAngle) * QuadraticParameter * QuadraticParameter + 2.0e-4f);
        const float    CosinePolar        = std::cos(PolarAngle);
        const float    SinePolar          = std::sin(PolarAngle);
        const float    DistanceGround     = std::max(0.005f, IntersectRadialSphereNearest(StartRadiusKm, CosinePolar, Profile.BottomRadius));

        // Sample matching upper-hemisphere sky elevation for physical Schlick dielectric water reflection.
        const uint32_t ReflectedRow = std::min(SkyViewHeight - 1u, HalfHeight + std::min(HalfHeight - 1u, static_cast<uint32_t>(MirrorOffset * 0.85f)));

        for (uint32_t Column = 0u; Column < SkyViewWidth; ++Column)
        {
            const float    CoordU        = (static_cast<float>(Column) + 0.5f) / static_cast<float>(SkyViewWidth);
            const float    Azimuth       = (CoordU * 2.0f - 1.0f) * Atmosphere::PiConstant;
            const Vector3f ViewDirection { SinePolar * std::cos(Azimuth), CosinePolar, SinePolar * std::sin(Azimuth) };

            LinearColour       Throughput     {};
            const LinearColour AerialUnit     = IntegrateSkyRayRadiometric(
                InOutTables, Profile, StartRadiusKm, ViewDirection, SolarAxis, DistanceGround, 32u, Throughput);
            const LinearColour AerialRadiance = MultiplyColour(AerialUnit, SolarFlux);

            const Vector3f SurfacePositionKm {
                ViewDirection.XCoord * DistanceGround,
                StartRadiusKm + ViewDirection.YCoord * DistanceGround,
                ViewDirection.ZCoord * DistanceGround };
            const Vector3f SurfaceNormal     = NormalizeVector(SurfacePositionKm);
            const float    SurfaceSunCosine  = DotVector(SurfaceNormal, SolarAxis);
            const float    ClampedSunCosine  = std::max(0.0f, SurfaceSunCosine);
            const float    NormalViewCosine  = std::clamp(-DotVector(ViewDirection, SurfaceNormal), 0.0f, 1.0f);

            // Upwelling subterranean oceanic Rayleigh + diffuse irradiance (pure cool cerulean water body, never brown/tan).
            const LinearColour SunAtSurface = (ClampedSunCosine > 0.0f)
                ? SampleTransmittanceToSpace(InOutTables, Profile, Profile.BottomRadius + 0.005f, ClampedSunCosine)
                : LinearColour{};
            const LinearColour SkyAtSurface = ScaleColour(
                SampleMultiScatterFactor(InOutTables, Profile, Profile.BottomRadius + 0.005f, SurfaceSunCosine),
                Atmosphere::PiConstant * Profile.MultipleScatteringFactor);

            const LinearColour TotalIrradiance{
                (SunAtSurface.Red   * ClampedSunCosine + SkyAtSurface.Red)   * SolarFlux.Red,
                (SunAtSurface.Green * ClampedSunCosine + SkyAtSurface.Green) * SolarFlux.Green,
                (SunAtSurface.Blue  * ClampedSunCosine + SkyAtSurface.Blue)  * SolarFlux.Blue };

            const LinearColour WaterBody = ScaleColour(MultiplyColour(GroundAlbedo, TotalIrradiance), 1.0f / Atmosphere::PiConstant);

            // Physical Schlick dielectric Fresnel water reflection F0 = 0.02 -> 1.0 at grazing limb angle.
            const float FresnelComplement        = 1.0f - NormalViewCosine;
            const float FresnelComplementSquared = FresnelComplement * FresnelComplement;
            const float FresnelFactor            = 0.02f + 0.98f * (FresnelComplementSquared * FresnelComplementSquared * FresnelComplement);

            const std::size_t  ReflectedTexelOffset = (static_cast<std::size_t>(ReflectedRow) * SkyViewWidth + Column) * 3u;
            const LinearColour ReflectedSky         {
                InOutTables.SkyViewRgb[ReflectedTexelOffset + 0u] * 0.78f,
                InOutTables.SkyViewRgb[ReflectedTexelOffset + 1u] * 0.84f,
                InOutTables.SkyViewRgb[ReflectedTexelOffset + 2u] * 0.92f };

            // Broad Cox-Munk anisotropic wind-ruffled ocean solar glitter path strictly along the sunlit solar azimuth.
            LinearColour SolarGlint{};
            if (SolarAxis.YCoord > -0.015f && ClampedSunCosine > 0.0f)
            {
                const Vector3f  HalfVector          = NormalizeVector(SubtractVector(SolarAxis, ViewDirection));
                const float     NormalHalfwayCosine = std::max(0.0f, DotVector(SurfaceNormal, HalfVector));
                constexpr float RoughnessSquared    = 0.016f; // Wind-ruffled sea slope variance σ^2
                const float     Denom               = NormalHalfwayCosine * NormalHalfwayCosine * (RoughnessSquared - 1.0f) + 1.0f;
                const float     GgxDistance         = RoughnessSquared / std::max(1.0e-5f, Atmosphere::PiConstant * Denom * Denom);
                const float     GlintGain           = std::min(6.0f, GgxDistance * FresnelFactor * 0.16f) * std::clamp((SolarAxis.YCoord + 0.015f) * 18.0f, 0.0f, 1.0f);
                SolarGlint = ScaleColour(MultiplyColour(SunAtSurface, SolarFlux), GlintGain);
            }

            LinearColour SurfaceOut = AddColour(
                AddColour(ScaleColour(WaterBody, 1.0f - FresnelFactor), ScaleColour(ReflectedSky, FresnelFactor)),
                SolarGlint);

            const LinearColour Combined    = AddColour(AerialRadiance, MultiplyColour(Throughput, SurfaceOut));
            const std::size_t  TexelOffset = (static_cast<std::size_t>(Row) * SkyViewWidth + Column) * 3u;
            InOutTables.SkyViewRgb[TexelOffset + 0u] = std::max(0.0f, Combined.Red);
            InOutTables.SkyViewRgb[TexelOffset + 1u] = std::max(0.0f, Combined.Green);
            InOutTables.SkyViewRgb[TexelOffset + 2u] = std::max(0.0f, Combined.Blue);
        }
    }

    InOutTables.SkyViewReady = true;
}

LinearColour SampleTransmittanceToSpace(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    float                          RadiusKm,
    float                          PolarCosine) noexcept
{
    if (!Tables.FoundationTablesReady)
        return LinearColour{ 1.0f, 1.0f, 1.0f };
    if (RadiusKm >= Profile.TopRadius)
        return LinearColour{ 1.0f, 1.0f, 1.0f };
    if (QueryGroundSphereIntersection(RadiusKm, PolarCosine, Profile.BottomRadius))
        return LinearColour{ 0.0f, 0.0f, 0.0f };

    float CoordU = 0.0f;
    float CoordV = 0.0f;
    EncodeTransmittanceUv(RadiusKm, std::clamp(PolarCosine, -1.0f, 1.0f), Profile.BottomRadius, Profile.TopRadius, CoordU, CoordV);
    return SampleTableBilinear(
        Tables.TransmittanceRgb,
        Atmosphere::TransmittanceExtentX,
        Atmosphere::TransmittanceExtentY,
        CoordU,
        CoordV,
        false);
}

LinearColour SampleSegmentTransmittance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    float                          StartRadiusKm,
    float                          PolarCosine,
    float                          DistanceKm) noexcept
{
    if (DistanceKm <= 1.0e-5f || !Tables.FoundationTablesReady)
        return LinearColour{ 1.0f, 1.0f, 1.0f };

    const float EndRadiusKm    = std::clamp(AdvancePlanetaryRadius(StartRadiusKm, PolarCosine, DistanceKm), Profile.BottomRadius, Profile.TopRadius);
    const float EndPolarCosine = std::clamp((StartRadiusKm * PolarCosine + DistanceKm) / std::max(1.0f, EndRadiusKm), -1.0f, 1.0f);

    // Reverse upward if ray heads toward planet so neither T(r, μ) lookup hits the solid sphere.
    if (QueryGroundSphereIntersection(StartRadiusKm, PolarCosine, Profile.BottomRadius))
    {
        const LinearColour NearToSpace = SampleTransmittanceToSpace(Tables, Profile, EndRadiusKm,   -EndPolarCosine);
        const LinearColour FarToSpace  = SampleTransmittanceToSpace(Tables, Profile, StartRadiusKm, -PolarCosine);
        return LinearColour{
            std::clamp(NearToSpace.Red   / std::max(FarToSpace.Red,   1.0e-6f), 0.0f, 1.0f),
            std::clamp(NearToSpace.Green / std::max(FarToSpace.Green, 1.0e-6f), 0.0f, 1.0f),
            std::clamp(NearToSpace.Blue  / std::max(FarToSpace.Blue,  1.0e-6f), 0.0f, 1.0f) };
    }

    const LinearColour NearToSpace = SampleTransmittanceToSpace(Tables, Profile, StartRadiusKm, PolarCosine);
    const LinearColour FarToSpace  = SampleTransmittanceToSpace(Tables, Profile, EndRadiusKm,   EndPolarCosine);
    return LinearColour{
        std::clamp(NearToSpace.Red   / std::max(FarToSpace.Red,   1.0e-6f), 0.0f, 1.0f),
        std::clamp(NearToSpace.Green / std::max(FarToSpace.Green, 1.0e-6f), 0.0f, 1.0f),
        std::clamp(NearToSpace.Blue  / std::max(FarToSpace.Blue,  1.0e-6f), 0.0f, 1.0f) };
}

LinearColour SampleMultiScatterFactor(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    float                          RadiusKm,
    float                          SolarCosine) noexcept
{
    if (!Tables.FoundationTablesReady)
        return LinearColour{};

    const float ThicknessKm = std::max(1.0f, Profile.TopRadius - Profile.BottomRadius);
    const float UnitU       = std::clamp(0.5f + 0.5f * SolarCosine, 0.0f, 1.0f);
    const float UnitV       = std::clamp((RadiusKm - Profile.BottomRadius) / ThicknessKm, 0.0f, 1.0f);
    const float CoordU      = MapUnitToTexelSafe(UnitU, Atmosphere::MultiScatterExtentX);
    const float CoordV      = MapUnitToTexelSafe(UnitV, Atmosphere::MultiScatterExtentY);
    return SampleTableBilinear(
        Tables.MultiScatterRgb,
        Atmosphere::MultiScatterExtentX,
        Atmosphere::MultiScatterExtentY,
        CoordU,
        CoordV,
        false);
}

LinearColour SampleSkyViewRadiance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                LocalDirection) noexcept
{
    if (!Tables.SkyViewReady)
        return LinearColour{};

    const float StartRadiusKm = (Tables.BakedViewerRadiusKm > 0.0f)
                              ? Tables.BakedViewerRadiusKm
                              : (Profile.BottomRadius + Atmosphere::RetrieveViewerAltitudeKm(Profile));
    const float LimbAngle     = ComputeLimbPolarAngle(StartRadiusKm, Profile.BottomRadius);
    const float PolarAngle    = std::acos(std::clamp(LocalDirection.YCoord, -1.0f, 1.0f));
    const float Azimuth       = std::atan2(LocalDirection.ZCoord, LocalDirection.XCoord);
    const float CoordU        = Azimuth / Atmosphere::TauConstant + 0.5f;

    constexpr uint32_t HalfHeight = Atmosphere::SkyViewExtentY / 2u;
    if (PolarAngle <= LimbAngle)
    {
        // Sample strictly within the upper-hemisphere texel bank [0.5, 1.0] — never blurs across the limb boundary.
        const float QuadraticParameter = SafeSquareRoot(1.0f - PolarAngle / std::max(1.0e-4f, LimbAngle));
        const float SubV               = MapUnitToTexelSafe(QuadraticParameter, HalfHeight);
        const float CoordV             = 0.5f + 0.5f * SubV;
        return SampleTableBilinear(Tables.SkyViewRgb, Atmosphere::SkyViewExtentX, Atmosphere::SkyViewExtentY, CoordU, CoordV, true);
    }

    const float QuadraticParameter = SafeSquareRoot((PolarAngle - LimbAngle) / std::max(1.0e-4f, Atmosphere::PiConstant - LimbAngle));
    const float SubV               = MapUnitToTexelSafe(1.0f - QuadraticParameter, HalfHeight);
    const float CoordV             = 0.5f * SubV;
    return SampleTableBilinear(Tables.SkyViewRgb, Atmosphere::SkyViewExtentX, Atmosphere::SkyViewExtentY, CoordU, CoordV, true);
}

LinearColour IntegrateAtmosphericSegment(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                StartPositionKm,
    const Vector3f&                LocalDirection,
    float                          SegmentDistanceKm,
    uint32_t                       StepCount,
    LinearColour&                  OutTransmittance) noexcept
{
    OutTransmittance = LinearColour{ 1.0f, 1.0f, 1.0f };
    if (SegmentDistanceKm <= 1.0e-5f || StepCount == 0u)
        return LinearColour{};

    const Vector3f     SolarAxis     = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const LinearColour SolarFlux     { Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };
    const float        CosineScatter = DotVector(LocalDirection, SolarAxis);
    const float        RayleighPh    = EvaluateRayleighPhaseFunction(CosineScatter);
    const float        MiePh         = EvaluateCornetteShanksPhaseFunction(CosineScatter, Profile.MieAsymmetry);

    const float  StepKm     = SegmentDistanceKm / static_cast<float>(StepCount);
    const float  StepMetres = StepKm * 1000.0f;
    LinearColour Radiance   {};

    for (uint32_t Step = 0u; Step < StepCount; ++Step)
    {
        const float    MarchStepDistanceKm = (static_cast<float>(Step) + 0.5f) * StepKm;
        const Vector3f SamplePositionKm    = AddVector(StartPositionKm, ScaleVector(LocalDirection, MarchStepDistanceKm));
        const float    SampleRadius        = VectorLength(SamplePositionKm);
        const float    SampleAltitudeKm    = SampleRadius - Profile.BottomRadius;
        if (SampleAltitudeKm < 0.0f || SampleRadius > Profile.TopRadius)
            continue;

        LinearColour RayScatter{}, MieScatter{}, Extinction{};
        EvaluateMediumCoefficients(Profile, SampleAltitudeKm, RayScatter, MieScatter, Extinction);

        const LinearColour StepTransmittance{
            std::exp(-Extinction.Red   * StepMetres),
            std::exp(-Extinction.Green * StepMetres),
            std::exp(-Extinction.Blue  * StepMetres) };

        const float        ColumnSolarCosine = DotVector(ScaleVector(SamplePositionKm, 1.0f / std::max(1.0f, SampleRadius)), SolarAxis);
        const LinearColour SunTransmittance  = SampleTransmittanceToSpace(Tables, Profile, SampleRadius, ColumnSolarCosine);
        const LinearColour MultiPsi          = ScaleColour(
            SampleMultiScatterFactor(Tables, Profile, SampleRadius, ColumnSolarCosine),
            Profile.MultipleScatteringFactor);

        const LinearColour InScatter{
            (RayScatter.Red   * RayleighPh + MieScatter.Red   * MiePh) * SunTransmittance.Red   + (RayScatter.Red   + MieScatter.Red)   * MultiPsi.Red,
            (RayScatter.Green * RayleighPh + MieScatter.Green * MiePh) * SunTransmittance.Green + (RayScatter.Green + MieScatter.Green) * MultiPsi.Green,
            (RayScatter.Blue  * RayleighPh + MieScatter.Blue  * MiePh) * SunTransmittance.Blue  + (RayScatter.Blue  + MieScatter.Blue)  * MultiPsi.Blue };

        const LinearColour StepIntegral{
            InScatter.Red   * (1.0f - StepTransmittance.Red)   / std::max(Extinction.Red,   1.0e-9f),
            InScatter.Green * (1.0f - StepTransmittance.Green) / std::max(Extinction.Green, 1.0e-9f),
            InScatter.Blue  * (1.0f - StepTransmittance.Blue)  / std::max(Extinction.Blue,  1.0e-9f) };

        Radiance         = AddColour(Radiance, MultiplyColour(OutTransmittance, StepIntegral));
        OutTransmittance = MultiplyColour(OutTransmittance, StepTransmittance);
    }

    return MultiplyColour(Radiance, SolarFlux);
}

LinearColour EvaluateSolarRadiometricDisc(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                LocalDirection,
    float                          SolarAngularRadiusRadians,
    float                          SolarDiscIntensity,
    float                          ViewerAltitudeKm,
    bool                           VacuumSpaceCondition) noexcept
{
    if (SolarDiscIntensity <= 0.0f)
        return LinearColour{};

    const Vector3f SolarAxis   = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const float    HalfRadians = std::max(0.0010f, SolarAngularRadiusRadians);
    // Stable small-angle chord identity θ = 2 arcsin(||v - s|| / 2) avoids float32 acos(1 - ε) quantization near disc centre!
    const float    HalfChord                = 0.5f * VectorLength(SubtractVector(LocalDirection, SolarAxis));
    const float    AngularSeparationRadians = 2.0f * std::asin(std::clamp(HalfChord, 0.0f, 1.0f));

    // Early out beyond 48 solar radii where coronal & small-angle aureole contribution is < 1e-6.
    if (AngularSeparationRadians > HalfRadians * 48.0f)
        return LinearColour{};

    const LinearColour SolarFlux{ Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };

    // Evaluate exact per-ray transmittance so grazing sunrise/sunset discs are cleanly clipped by the curved planet limb
    // and show authentic differential bottom-to-top atmospheric reddening across the photosphere!
    LinearColour LineTransmittance{ 1.0f, 1.0f, 1.0f };
    if (!VacuumSpaceCondition)
    {
        const float ViewerRadiusKm = Profile.BottomRadius + std::max(0.005f, ViewerAltitudeKm);
        if (QueryGroundSphereIntersection(ViewerRadiusKm, LocalDirection.YCoord, Profile.BottomRadius))
            return LinearColour{};
        LineTransmittance = SampleTransmittanceToSpace(Tables, Profile, ViewerRadiusKm, LocalDirection.YCoord);
        if (LineTransmittance.Red + LineTransmittance.Green + LineTransmittance.Blue <= 1.0e-6f)
            return LinearColour{};
    }

    const float NormalizedRadius = AngularSeparationRadians / HalfRadians;

    // ① Neckel & Labs (1994) wavelength-dependent photospheric limb darkening with C∞ optical PSF limb roll-off (zero hard edge step).
    LinearColour Photosphere{};
    if (NormalizedRadius < 1.32f)
    {
        const float        ClampedR = std::min(1.0f, NormalizedRadius);
        const float        MuSun    = SafeSquareRoot(1.0f - ClampedR * ClampedR);
        const LinearColour Ld       = EvaluateNeckelLabsLimbDarkening(MuSun);
        // Seeing-limited limb (~12 arcsec ≈ 0.012 solar radii); pixel-scale antialiasing comes from the caller's supersampling.
        const float EdgeErfc  = 0.5f * std::erfc((NormalizedRadius - 0.997f) / 0.012f);
        Photosphere           = ScaleColour(Ld, EdgeErfc * SolarDiscIntensity);
    }

    // ② Strictly monotonic K+F solar corona + small-angle forward-scatter diffraction/aerosol aureole (dL/dθ < 0 for all θ > 0).
    const float InnerTerm  = 1.0f / std::pow(1.0f + (NormalizedRadius / 1.18f) * (NormalizedRadius / 1.18f), 1.85f);
    const float MiddleTerm = 1.0f / std::pow(1.0f + (NormalizedRadius / 3.40f) * (NormalizedRadius / 3.40f), 1.38f);
    const float OuterTerm  = 1.0f / std::pow(1.0f + (NormalizedRadius / 11.5f) * (NormalizedRadius / 11.5f), 1.22f);

    // Couple atmospheric aureole gently to aerosol optical thickness along the line of sight; pure crisp K+F corona in vacuum.
    const float AtmosphereScatterFactor = VacuumSpaceCondition
        ? 0.38f
        : std::clamp(0.55f + 0.85f * (1.0f - LineTransmittance.Blue), 0.55f, 1.35f);

    const float CoronaProfile = SolarDiscIntensity * (0.042f * InnerTerm + 0.0085f * AtmosphereScatterFactor * MiddleTerm + 0.0014f * AtmosphereScatterFactor * OuterTerm);
    // Softly vanish at the 48-radius truncation window so zero boundary discontinuity ever appears.
    const float WindowFade   = std::clamp(1.0f - NormalizedRadius / 48.0f, 0.0f, 1.0f);
    const float SmoothWindow = WindowFade * WindowFade;

    // Slight warm long-wavelength weighting in outer F-corona / aerosol diffraction halo.
    const LinearColour CoronaColour{
        CoronaProfile * SmoothWindow * 1.04f,
        CoronaProfile * SmoothWindow * 0.99f,
        CoronaProfile * SmoothWindow * 0.91f };

    const LinearColour Combined = AddColour(Photosphere, CoronaColour);
    return MultiplyColour(MultiplyColour(Combined, LineTransmittance), SolarFlux);
}

LinearColour EvaluateSolarGlareRadiance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                LocalDirection,
    float                          SolarAngularRadiusRadians,
    float                          SolarGlareStrength,
    float                          ViewerAltitudeKm,
    bool                           VacuumSpaceCondition) noexcept
{
    if (SolarGlareStrength <= 0.0f)
        return LinearColour{};

    const Vector3f SolarAxis                = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const float    HalfRadians              = std::max(0.0010f, SolarAngularRadiusRadians);
    const float    HalfChord                = 0.5f * VectorLength(SubtractVector(LocalDirection, SolarAxis));
    const float    AngularSeparationRadians = 2.0f * std::asin(std::clamp(HalfChord, 0.0f, 1.0f));
    const float    Spread                   = Atmosphere::EvaluateSolarGlareSpread(AngularSeparationRadians, HalfRadians);
    if (Spread <= 0.0f)
        return LinearColour{};

    // The glare forms inside the optics, so it is NOT clipped by the ground (or clouds) along this pixel's ray: it carries the
    // transmittance toward the sun itself — reddened at sunset, gone once the sun has set — and spills across the horizon.
    LinearColour AxisTransmittance{ 1.0f, 1.0f, 1.0f };
    if (!VacuumSpaceCondition)
    {
        const float ViewerRadiusKm = Profile.BottomRadius + std::max(0.005f, ViewerAltitudeKm);
        if (QueryGroundSphereIntersection(ViewerRadiusKm, SolarAxis.YCoord, Profile.BottomRadius))
            return LinearColour{};
        AxisTransmittance = SampleTransmittanceToSpace(Tables, Profile, ViewerRadiusKm, SolarAxis.YCoord);
    }
    const float              GlareRadiance = SolarGlareStrength * Spread;
    const LinearColour       SolarFlux{ Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };
    return MultiplyColour(MultiplyColour(LinearColour{ GlareRadiance * 1.02f, GlareRadiance, GlareRadiance * 0.96f }, AxisTransmittance), SolarFlux);
}

LinearColour EvaluateFlowAdvectedSkyVeil(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const AtmosphericFlowField&    FlowField,
    const Vector3f&                LocalDirection,
    float                          SequenceSeconds,
    LinearColour&                  InOutTransmittance) noexcept
{
    const float VeilScale = Profile.SolarDirection[3] * FlowField.Synoptic.SkyVeilCouplingScale;
    if (VeilScale <= 1.0e-4f || LocalDirection.YCoord <= 0.015f)
        return LinearColour{};

    // Intersect high tropopause / lower-stratosphere aerosol-ice veil at h = 11.2 km above BottomRadius.
    const float ViewerRadiusKm = Profile.BottomRadius + std::clamp(Atmosphere::RetrieveViewerAltitudeKm(Profile), 0.005f, 10.0f);
    const float VeilRadiusKm   = Profile.BottomRadius + 11.2f;
    const float HitDistanceKm  = IntersectRadialSphereNearest(ViewerRadiusKm, LocalDirection.YCoord, VeilRadiusKm);
    if (HitDistanceKm <= 0.0f || HitDistanceKm > 140.0f)
        return LinearColour{};

    // Project onto world coordinates (+Z up) and trace RK4 backward characteristic through AtmosphericFlowField!
    const Vector3f           VeilWorldMetres {
        LocalDirection.XCoord * HitDistanceKm * 1000.0f,
        LocalDirection.ZCoord * HitDistanceKm * 1000.0f,
        11200.0f };
    const FlowAdvectedSample Advected        = TraceFlowCharacteristicBackward(FlowField, VeilWorldMetres, SequenceSeconds, 4u);

    // Delicate anisotropic ice-crystal streak filaments sculpted by the advected flow streamlines.
    const Vector3f FlowCoord   {
        Advected.SourcePositionMetres.XCoord * 0.00011f,
        Advected.SourcePositionMetres.YCoord * 0.00011f,
        Advected.SourcePositionMetres.ZCoord * 0.00025f };
    const float    MacroStream = EvaluateRotatedFbm3D(FlowCoord, 4u, 911u);
    const float    FineFibres  = EvaluateRotatedFbm3D(Vector3f{ FlowCoord.XCoord * 3.2f, FlowCoord.YCoord * 1.4f, FlowCoord.ZCoord * 2.5f }, 4u, 977u);
    const float    RawWisp     = MacroStream * 0.68f + FineFibres * 0.32f + Advected.CondensationDelta * 0.16f;
    const float    Filament    = std::clamp((RawWisp - 0.56f) / 0.28f, 0.0f, 1.0f);
    if (Filament <= 1.0e-4f)
        return LinearColour{};

    // Smooth elevation taper ensures ZERO skyline white band — veil lives naturally across the open sky dome and fades into aerial perspective.
    const float LimbTaper         = std::clamp((LocalDirection.YCoord - 0.025f) / 0.16f, 0.0f, 1.0f);
    const float SmoothLimb        = LimbTaper * LimbTaper * (3.0f - 2.0f * LimbTaper);
    const float OpticalDepth      = Filament * Filament * 0.055f * VeilScale * SmoothLimb;
    const float VeilTransmittance = std::exp(-OpticalDepth);

    const Vector3f     SolarAxis     = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const LinearColour SolarFlux     { Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };
    const Vector3f     SurfaceHitKm  { LocalDirection.XCoord * HitDistanceKm, ViewerRadiusKm + LocalDirection.YCoord * HitDistanceKm, LocalDirection.ZCoord * HitDistanceKm };
    const float        VeilSunCosine = DotVector(NormalizeVector(SurfaceHitKm), SolarAxis);

    const LinearColour SunAtVeil   = SampleTransmittanceToSpace(Tables, Profile, VeilRadiusKm, VeilSunCosine);
    const LinearColour MultiAtVeil = ScaleColour(SampleMultiScatterFactor(Tables, Profile, VeilRadiusKm, VeilSunCosine), Profile.MultipleScatteringFactor);
    const LinearColour ViewToVeil  = SampleSegmentTransmittance(Tables, Profile, ViewerRadiusKm, LocalDirection.YCoord, HitDistanceKm);

    const float CosineScatter = DotVector(LocalDirection, SolarAxis);
    const float IcePhase      = 0.72f * EvaluateCornetteShanksPhaseFunction(CosineScatter, 0.72f)
                                + 0.28f * EvaluateCornetteShanksPhaseFunction(CosineScatter, -0.25f);

    const LinearColour SourceRadiance{
        (SunAtVeil.Red   * IcePhase + MultiAtVeil.Red   * 0.85f) * SolarFlux.Red,
        (SunAtVeil.Green * IcePhase + MultiAtVeil.Green * 0.85f) * SolarFlux.Green,
        (SunAtVeil.Blue  * IcePhase + MultiAtVeil.Blue  * 0.85f) * SolarFlux.Blue };

    const LinearColour Scattered = ScaleColour(MultiplyColour(SourceRadiance, ViewToVeil), 1.0f - VeilTransmittance);
    InOutTransmittance           = ScaleColour(InOutTransmittance, VeilTransmittance);
    return Scattered;
}

OrbitalAtmosphereResult EvaluateOrbitalPlanetRadiance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                CameraPlanetPositionKm,
    const Vector3f&                LocalDirection,
    const AtmosphericFlowField*    OptionalFlowField,
    float                          CloudCoverage,
    float                          SequenceSeconds,
    uint32_t                       AtmosphereStepCount) noexcept
{
    OrbitalAtmosphereResult Result       {};
    const float             ClosestDot   = -DotVector(CameraPlanetPositionKm, LocalDirection);
    const Vector3f          ClosestPoint = AddVector(CameraPlanetPositionKm, ScaleVector(LocalDirection, std::max(0.0f, ClosestDot)));
    Result.MinimumRayAltitudeKm = VectorLength(ClosestPoint) - Profile.BottomRadius;

    float      AerialNearKm   = 0.0f, AtmosphereFarKm = 0.0f;
    const bool HitsAtmosphere = IntersectSphereSpan3D(CameraPlanetPositionKm, LocalDirection, Profile.TopRadius, AerialNearKm, AtmosphereFarKm);

    float      PlanetNearKm  = 0.0f, PlanetFarKm = 0.0f;
    const bool HitsPlanetRaw = IntersectSphereSpan3D(CameraPlanetPositionKm, LocalDirection, Profile.BottomRadius, PlanetNearKm, PlanetFarKm);
    const bool HitsPlanet    = HitsPlanetRaw && (PlanetNearKm > 0.0f);
    Result.PlanetSurfaceHit  = HitsPlanet;

    if (!HitsAtmosphere && !HitsPlanet)
        return Result;

    const float MarchStartKm = std::max(0.0f, AerialNearKm);
    const float MarchEndKm   = HitsPlanet ? PlanetNearKm : AtmosphereFarKm;
    if (MarchEndKm <= MarchStartKm)
        return Result;

    // ① Exact 3D spherical atmosphere integration (Rayleigh + Cornette-Shanks Mie + Chappuis Ozone + MultiScatter LUT).
    const Vector3f     EntryPositionKm     = AddVector(CameraPlanetPositionKm, ScaleVector(LocalDirection, MarchStartKm));
    LinearColour       AerialTransmittance { 1.0f, 1.0f, 1.0f };
    const LinearColour AerialRadiance      = IntegrateAtmosphericSegment(
        Tables,
        Profile,
        EntryPositionKm,
        LocalDirection,
        MarchEndKm - MarchStartKm,
        std::max(16u, AtmosphereStepCount),
        AerialTransmittance);

    if (!HitsPlanet)
    {
        Result.CombinedRadiance        = AerialRadiance;
        Result.AtmosphereTransmittance = AerialTransmittance;
        return Result;
    }

    // ② Shade illuminated curved planet surface: tectonic continents, turquoise coastal shelves, biomes, mountains, polar ice caps,
    //    Cox-Munk GGX ocean specular sun glint, nightside urban clusters, and parallax 3D force-field cloud deck with cast shadows!
    const Vector3f     SolarAxis = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const LinearColour SolarFlux { Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };

    const Vector3f SurfaceHitKm = AddVector(CameraPlanetPositionKm, ScaleVector(LocalDirection, PlanetNearKm));
    const Vector3f UnitNormal   = NormalizeVector(SurfaceHitKm);
    // Geographic frame: the scene's local vertical maps to ~18°N (sub-viewer point), so orbital viewers see mid-latitude oceans,
    // continents, and both ice caps in perspective instead of looking straight down onto the pole. Lighting keeps the true normal.
    const Vector3f GeoNormal        = ResolveOrbitalGeographicAxis(UnitNormal);
    const float    AbsoluteLatitude = std::abs(GeoNormal.YCoord);

    // Domain-warped multi-octave continental tectonics.
    const Vector3f TectonicWarp    {
        EvaluateRotatedFbm3D(ScaleVector(GeoNormal, 1.85f), 4u, 11u) - 0.5f,
        EvaluateRotatedFbm3D(ScaleVector(GeoNormal, 1.85f), 4u, 23u) - 0.5f,
        EvaluateRotatedFbm3D(ScaleVector(GeoNormal, 1.85f), 4u, 41u) - 0.5f };
    const Vector3f PlatePoint      = AddVector(ScaleVector(GeoNormal, 2.55f), ScaleVector(TectonicWarp, 0.85f));
    const float    ElevationField  = EvaluateRotatedFbm3D(PlatePoint, 6u, 59u);
    const float    MountainRidge   = 1.0f - std::abs(2.0f * EvaluateRotatedFbm3D(ScaleVector(PlatePoint, 4.2f), 5u, 83u) - 1.0f);
    const float    ContinentHeight = ElevationField + 0.14f * MountainRidge * MountainRidge;

    constexpr float CoastlineLevel = 0.545f;
    const float     LandMask       = std::clamp((ContinentHeight - CoastlineLevel) / 0.022f, 0.0f, 1.0f);
    const float     CoastalShelf   = (1.0f - LandMask) * std::clamp((ContinentHeight - (CoastlineLevel - 0.055f)) / 0.055f, 0.0f, 1.0f);
    const float     PolarIceMask   = std::clamp((AbsoluteLatitude - 0.78f + 0.06f * (ElevationField - 0.5f)) / 0.075f, 0.0f, 1.0f);

    // Deep bathymetric ocean + shallow turquoise carbonate shelves.
    LinearColour SurfaceAlbedo{
        0.011f + 0.018f * CoastalShelf * CoastalShelf,
        0.038f + 0.115f * CoastalShelf * CoastalShelf,
        0.108f + 0.140f * CoastalShelf * CoastalShelf };

    // Diverse continental biomes: tropical & boreal forest, temperate grassland, subtropical desert belt, alpine rock & snow.
    Vector3f ShadedNormal = UnitNormal;
    if (LandMask > 0.001f)
    {
        const float MoistureField   = EvaluateRotatedFbm3D(AddVector(ScaleVector(GeoNormal, 5.4f), Vector3f{ 3.1f, -4.7f, 2.2f }), 5u, 127u);
        const float HadleyBelt      = std::exp(-((AbsoluteLatitude - 0.39f) * (AbsoluteLatitude - 0.39f)) / 0.032f);
        const float Aridity         = std::clamp((0.52f - MoistureField) * 2.1f + 0.58f * HadleyBelt, 0.0f, 1.0f);
        const float AlpineElevation = std::clamp((ContinentHeight - (CoastlineLevel + 0.11f)) / 0.09f, 0.0f, 1.0f);
        const float SnowLine        = std::clamp(AlpineElevation * 0.9f + (AbsoluteLatitude - 0.62f) * 2.4f, 0.0f, 1.0f);

        const LinearColour LushForest { 0.032f, 0.088f, 0.028f };
        const LinearColour WarmDesert { 0.275f, 0.205f, 0.128f };
        const LinearColour AlpineRock { 0.135f, 0.128f, 0.122f };
        const LinearColour SnowCap    { 0.660f, 0.695f, 0.740f };

        LinearColour LandColour = InterpolateColour(LushForest, WarmDesert, Aridity * Aridity);
        LandColour              = InterpolateColour(LandColour, AlpineRock, AlpineElevation * 0.65f);
        LandColour              = InterpolateColour(LandColour, SnowCap,    std::clamp((SnowLine - 0.42f) / 0.30f, 0.0f, 1.0f));
        SurfaceAlbedo           = InterpolateColour(SurfaceAlbedo, LandColour, LandMask);

        // Subtle mountain relief normal perturbation accentuates ranges near the orbital terminator.
        const float GradientOffset = 0.004f;
        const float SlopeEastward  = EvaluateRotatedFbm3D(AddVector(ScaleVector(PlatePoint, 4.2f), Vector3f{ GradientOffset, 0.0f, 0.0f }), 4u, 83u)
                                     - EvaluateRotatedFbm3D(AddVector(ScaleVector(PlatePoint, 4.2f), Vector3f{ -GradientOffset, 0.0f, 0.0f }), 4u, 83u);
        const float SlopeNorthward = EvaluateRotatedFbm3D(AddVector(ScaleVector(PlatePoint, 4.2f), Vector3f{ 0.0f, 0.0f, GradientOffset }), 4u, 83u)
                                     - EvaluateRotatedFbm3D(AddVector(ScaleVector(PlatePoint, 4.2f), Vector3f{ 0.0f, 0.0f, -GradientOffset }), 4u, 83u);
        ShadedNormal = NormalizeVector(Vector3f{
            UnitNormal.XCoord - SlopeEastward * 0.9f * LandMask,
            UnitNormal.YCoord,
            UnitNormal.ZCoord - SlopeNorthward * 0.9f * LandMask });
    }

    if (PolarIceMask > 0.001f)
    {
        const LinearColour PolarAlbedo{ 0.68f, 0.72f, 0.77f };
        SurfaceAlbedo = InterpolateColour(SurfaceAlbedo, PolarAlbedo, PolarIceMask);
    }

    // Parallax 3D spherical cloud sheet at BottomRadius + 5.8 km + ray-projected cloud shadow onto surface below!
    float CloudOpacity = 0.0f;
    float ShadowFactor = 1.0f;
    if (CloudCoverage > 0.005f)
    {
        float       CloudNearKm      = 0.0f, CloudFarKm = 0.0f;
        const float CloudSheetRadius = Profile.BottomRadius + 5.8f;
        Vector3f    CloudUnitPoint   = GeoNormal;
        if (IntersectSphereSpan3D(CameraPlanetPositionKm, LocalDirection, CloudSheetRadius, CloudNearKm, CloudFarKm) && CloudNearKm > 0.0f)
        {
            CloudUnitPoint = ResolveOrbitalGeographicAxis(NormalizeVector(AddVector(CameraPlanetPositionKm, ScaleVector(LocalDirection, CloudNearKm))));
        }
        CloudOpacity = EvaluateOrbitalCloudField(CloudUnitPoint, OptionalFlowField, CloudCoverage, SequenceSeconds);

        // Offset along SolarAxis by 5.8 km / R_b to evaluate the exact cloud casting a shadow onto SurfaceHitKm!
        const Vector3f ShadowCasterUnit = ResolveOrbitalGeographicAxis(NormalizeVector(AddVector(SurfaceHitKm, ScaleVector(SolarAxis, 5.8f))));
        const float    CasterCloud      = EvaluateOrbitalCloudField(ShadowCasterUnit, OptionalFlowField, CloudCoverage, SequenceSeconds);
        ShadowFactor                    = 1.0f - 0.78f * CasterCloud;
    }

    // Physical direct sunlight (through exact Transmittance LUT — turns rich golden-crimson at the orbital terminator!) + MultiScatter sky fill.
    const float        RawSunCosine    = DotVector(UnitNormal, SolarAxis);
    const float        BumpedSunCosine = std::max(0.0f, DotVector(ShadedNormal, SolarAxis));
    const LinearColour SunAtGround     = (RawSunCosine > -0.02f)
        ? SampleTransmittanceToSpace(Tables, Profile, Profile.BottomRadius + 0.01f, RawSunCosine)
        : LinearColour{};
    const LinearColour SkyAtGround     = ScaleColour(
        SampleMultiScatterFactor(Tables, Profile, Profile.BottomRadius + 0.01f, RawSunCosine),
        Atmosphere::PiConstant * Profile.MultipleScatteringFactor);

    const LinearColour DirectDiffuse{
        SurfaceAlbedo.Red   * (SunAtGround.Red   * BumpedSunCosine * ShadowFactor + SkyAtGround.Red)   * (SolarFlux.Red   / Atmosphere::PiConstant),
        SurfaceAlbedo.Green * (SunAtGround.Green * BumpedSunCosine * ShadowFactor + SkyAtGround.Green) * (SolarFlux.Green / Atmosphere::PiConstant),
        SurfaceAlbedo.Blue  * (SunAtGround.Blue  * BumpedSunCosine * ShadowFactor + SkyAtGround.Blue)  * (SolarFlux.Blue  / Atmosphere::PiConstant) };

    // Cox-Munk GGX microfacet ocean solar glint (open ice-free water only, shadowed by clouds).
    LinearColour OceanSpecular {};
    const float  OpenWaterMask = (1.0f - LandMask) * (1.0f - PolarIceMask);
    if (OpenWaterMask > 0.001f && RawSunCosine > 0.0f)
    {
        const Vector3f  ViewOut             = ScaleVector(LocalDirection, -1.0f);
        const Vector3f  HalfwayAxis         = NormalizeVector(AddVector(SolarAxis, ViewOut));
        const float     NormalHalfwayCosine = std::max(0.0f, DotVector(UnitNormal, HalfwayAxis));
        const float     ViewHalfwayCosine   = std::max(0.0f, DotVector(ViewOut, HalfwayAxis));
        const float     Fresnel             = 0.022f + 0.978f * std::pow(1.0f - ViewHalfwayCosine, 5.0f);
        constexpr float SlopeVariance       = 0.042f;
        const float     GgxDenominator      = NormalHalfwayCosine * NormalHalfwayCosine * (SlopeVariance - 1.0f) + 1.0f;
        const float     GgxTerm             = SlopeVariance / std::max(1.0e-4f, Atmosphere::PiConstant * GgxDenominator * GgxDenominator);
        const float     SpecularWeight      = OpenWaterMask * ShadowFactor * std::min(1.65f, GgxTerm * Fresnel * 0.22f);
        OceanSpecular             = ScaleColour(MultiplyColour(SunAtGround, SolarFlux), SpecularWeight);
    }

    LinearColour PlanetRadiance = AddColour(DirectDiffuse, OceanSpecular);

    // Realistic temperate coastal nocturnal urban light clusters on the unlit hemisphere.
    const float NightMask = std::clamp((-RawSunCosine - 0.045f) / 0.13f, 0.0f, 1.0f) * LandMask * (1.0f - PolarIceMask);
    if (NightMask > 0.005f && AbsoluteLatitude < 0.72f)
    {
        const float CoastProximity   = std::exp(-std::max(0.0f, ContinentHeight - CoastlineLevel) * 16.0f);
        const float UrbanRegionField = EvaluateRotatedFbm3D(ScaleVector(GeoNormal, 9.5f),  3u, 811u);
        const float CitySprawl       = EvaluateRotatedFbm3D(ScaleVector(GeoNormal, 42.0f), 3u, 853u);
        const float UrbanDensity     = std::clamp((UrbanRegionField - 0.50f) * 3.2f, 0.0f, 1.0f)
                                       * std::clamp((CitySprawl - 0.58f) * 5.8f, 0.0f, 1.0f)
                                       * (0.35f + 0.65f * CoastProximity);
        const float CityIntensity    = UrbanDensity * UrbanDensity * NightMask * (1.0f - 0.72f * CloudOpacity) * 0.24f;
        PlanetRadiance.Red   += CityIntensity * 1.00f;
        PlanetRadiance.Green += CityIntensity * 0.62f;
        PlanetRadiance.Blue  += CityIntensity * 0.22f;
    }

    // Physical sunlit cloud-top radiance (high single-scattering albedo ω_0 ≈ 0.99, reddened across the twilight terminator via T(R_b + 5.8 km, μ_s)).
    if (CloudOpacity > 0.001f)
    {
        const LinearColour SunAtCloud  = SampleTransmittanceToSpace(Tables, Profile, Profile.BottomRadius + 5.8f, RawSunCosine);
        const LinearColour SkyAtCloud  = ScaleColour(
            SampleMultiScatterFactor(Tables, Profile, Profile.BottomRadius + 5.8f, RawSunCosine),
            Atmosphere::PiConstant * Profile.MultipleScatteringFactor);
        const float        CloudCosine = std::max(0.0f, RawSunCosine + 0.045f) / 1.045f;
        const float        CloudAlbedo = 0.62f + 0.20f * CloudOpacity;

        // Cloud receives full upper-atmosphere transmittance (only ~45% of sea-level Rayleigh lies above 5.8 km).
        const LinearColour CloudRadiance{
            CloudAlbedo * (SunAtCloud.Red   * CloudCosine + SkyAtCloud.Red   * 1.05f) * (SolarFlux.Red   / Atmosphere::PiConstant),
            CloudAlbedo * (SunAtCloud.Green * CloudCosine + SkyAtCloud.Green * 1.05f) * (SolarFlux.Green / Atmosphere::PiConstant),
            CloudAlbedo * (SunAtCloud.Blue  * CloudCosine + SkyAtCloud.Blue  * 1.05f) * (SolarFlux.Blue  / Atmosphere::PiConstant) };

        const float BlendAlpha = std::clamp(CloudOpacity * 0.92f, 0.0f, 0.95f);
        PlanetRadiance         = InterpolateColour(PlanetRadiance, CloudRadiance, BlendAlpha);
    }

    Result.CombinedRadiance        = AddColour(AerialRadiance, MultiplyColour(AerialTransmittance, PlanetRadiance));
    Result.AtmosphereTransmittance = LinearColour{ 0.0f, 0.0f, 0.0f };
    return Result;
}

} // namespace Frontier
