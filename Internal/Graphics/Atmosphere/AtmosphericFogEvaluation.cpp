/*==============================================================================================================================================
                                                ATMOSPHERICFOGEVALUATION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the exponential height fog layer (closed-form vertical integral) and the ellipsoidal local fog volumes, both
//    advected by the shared AtmosphericWindField and lit with the same dual-lobe Henyey-Greenstein + multiple-scattering ladder the
//    volumetric cloud deck uses, so fog, cloud and sky agree on one light field.

#include "Graphics/Atmosphere/AtmosphericFogEvaluation.h"
#include "Graphics/Atmosphere/VolumetricCloudEvaluation.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float PiConstant = 3.14159265358979323846f;

[[nodiscard]] float SmoothStepScalar(float Edge0, float Edge1, float Input) noexcept
{
    const float T = std::clamp((Input - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float EvaluateDualLobePhase(float CosineTheta, float ForwardG, float BackG, float ForwardWeight) noexcept
{
    auto SingleLobe = [](float Mu, float G) noexcept -> float
    {
        const float G2    = G * G;
        const float Denom = std::pow(std::max(1.0e-4f, 1.0f + G2 - 2.0f * G * Mu), 1.5f);
        return (1.0f - G2) / (4.0f * PiConstant * Denom);
    };
    return ForwardWeight * SingleLobe(CosineTheta, ForwardG) + (1.0f - ForwardWeight) * SingleLobe(CosineTheta, BackG);
}

// 📝 Closed-form integral of exp(-(z - base)/H) along a ray, where z(s) = z0 + s * dz/ds.
//    ∫0^L exp(-(z0 + m*s - base)/H) ds = exp(-(z0 - base)/H) * (1 - exp(-m*L/H)) * H/m   for m != 0, else L * exp(...).
[[nodiscard]] float IntegrateExponentialColumn(float BaseAltitude,
                                               float ScaleHeight,
                                               float StartAltitude,
                                               float VerticalSlope,
                                               float LengthMetres) noexcept
{
    const float SafeScale = std::max(20.0f, ScaleHeight);
    const float Datum     = std::exp(-std::max(0.0f, StartAltitude - BaseAltitude) / SafeScale);
    const float SlopeTerm = VerticalSlope / SafeScale;
    if (std::fabs(SlopeTerm) < 1.0e-7f)
        return Datum * LengthMetres;
    return Datum * (1.0f - std::exp(-SlopeTerm * LengthMetres)) / SlopeTerm;
}

// 📝 Relief of an inversion cap. The cap is a material surface, so it is sampled in the LAGRANGIAN description: the noise is evaluated
//    at the position the air at this horizontal location came from. The result is that the topography of a fog sea drifts downwind
//    with the same flow that carries the clouds, instead of sitting still under a moving deck.
[[nodiscard]] float EvaluateInversionCapAltitude(const HeightFogConfiguration& Layer,
                                                 const Vector3f&               ProbeSceneMetres,
                                                 bool                          WindCoupled,
                                                 const AdvectionOffsetVolume*  PreparedWindOffsetVolume) noexcept
{
    if (Layer.InversionReliefMetres <= 0.5f)
        return Layer.InversionAltitudeMetres;

    Vector3f LagPos = ProbeSceneMetres;
    if (WindCoupled && PreparedWindOffsetVolume != nullptr)
    {
        Vector3f Displacement{ 0.0f, 0.0f, 0.0f };
        float    Convergence = 0.0f;
        if (SampleAdvectionOffset(*PreparedWindOffsetVolume, ProbeSceneMetres, Displacement, Convergence))
            LagPos = AddVector(ProbeSceneMetres, Displacement);
    }

    const float ReliefNoise = EvaluateCloudShapeNoise(
        Vector3f{ LagPos.XCoord * 0.00040f, LagPos.YCoord * 0.00040f, 0.37f }, 4u, 1493u);
    return Layer.InversionAltitudeMetres + Layer.InversionReliefMetres * (ReliefNoise - 0.5f) * 2.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float EvaluateHeightFogExtinctionPerKilometre(const HeightFogConfiguration& Layer, float AltitudeMetres) noexcept
{
    if (!Layer.LayerEnabled)
        return 0.0f;

    const float HeightAboveDatum = std::max(0.0f, AltitudeMetres - Layer.BaseAltitudeMetres);

    // Each category is a different physical stratification, not a different look:
    //   RadiationMist — shallow and very sharp (a strong surface inversion traps it under ~200 m).
    //   AdvectionFog  — deep and near-uniform (moist air cooled from below, mixed through a thick layer).
    //   ValleyStratum — flat-topped: it fills the basin up to the inversion and is cut off abruptly above it.
    float Density = 0.0f;
    switch (Layer.Category)
    {
        case FogLayerCategory::RadiationMist:
        {
            const float EffectiveScale = Layer.ScaleHeightMetres * 0.42f;
            Density = std::exp(-HeightAboveDatum / std::max(15.0f, EffectiveScale));
            Density *= (1.0f - SmoothStepScalar(0.70f, 1.0f, HeightAboveDatum / 320.0f));
            break;
        }
        case FogLayerCategory::ValleyStratum:
        {
            Density = std::exp(-HeightAboveDatum / std::max(40.0f, Layer.ScaleHeightMetres));
            if (Layer.InversionAltitudeMetres > 1.0f)
            {
                const float AboveInversion = AltitudeMetres - Layer.InversionAltitudeMetres;
                Density *= 1.0f - SmoothStepScalar(-40.0f, 90.0f, AboveInversion);
            }
            break;
        }
        case FogLayerCategory::AdvectionFog:
        default:
        {
            Density = std::exp(-HeightAboveDatum / std::max(20.0f, Layer.ScaleHeightMetres));
            break;
        }
    }

    return Layer.ExtinctionPerKilometre * std::max(0.0f, Density);
}

Vector3f ResolveAdvectedFogCenter(const AtmosphericFogConfiguration& Configuration,
                                  const LocalFogVolume&              Volume,
                                  const AtmosphericWindField*        OptionalWindField,
                                  float                              SequenceSeconds) noexcept
{
    Vector3f Center = Volume.CenterMetres;
    if (!Volume.AdvectedByForceField || !Configuration.WindAdvectionEnabled)
        return Center;
    if (OptionalWindField == nullptr || !OptionalWindField->FieldEnabled)
        return Center;

    const float Span = std::max(0.0f, Configuration.AdvectionSpanSeconds);
    if (Span <= 1.0e-3f)
        return Center;

    // Fog bodies ride the SAME characteristic as the cloud deck, so a bank of valley fog and the cloud above it cannot drift apart.
    const Vector3f Origin = IntegrateStreamlineBackward(
        *OptionalWindField,
        Volume.CenterMetres,
        SequenceSeconds,
        Span,
        4u,
        AdvectionMediumTarget::CloudStratum);

    Center.XCoord = Origin.XCoord;
    Center.YCoord = Origin.YCoord;
    Center.ZCoord = Volume.CenterMetres.ZCoord;
    return Center;
}

FogRayResult EvaluateAtmosphericFogRay(const AtmosphericFogConfiguration& Configuration,
                                       const RadiometricSkyTables&        SkyTables,
                                       const RadiometricSkyConfiguration& SkyConfiguration,
                                       const Vector3f&                    CameraSceneMetres,
                                       const Vector3f&                    RayDirectionScene,
                                       const AtmosphericWindField*        OptionalWindField,
                                       const AdvectionOffsetVolume*       PreparedWindOffsetVolume,
                                       uint32_t                           PixelCoordX,
                                       uint32_t                           PixelCoordY,
                                       uint32_t                           SubSampleIndex) noexcept
{
    FogRayResult Result{};
    const bool HeightLayerMuted = (Configuration.HeightLayer.ExtinctionPerKilometre <= 1.0e-6f)
                                || !Configuration.HeightLayer.LayerEnabled;
    if (!Configuration.FogEnabled || (HeightLayerMuted && Configuration.LocalVolumes.empty()))
        return Result;

    const Vector3f ViewDir = NormalizeVector(RayDirectionScene);
    const Vector3f SunDir  = NormalizeVector(SkyConfiguration.SolarDirectionScene);

    const float CameraAltitude = SkyConfiguration.ObserverAltitudeMetres + CameraSceneMetres.ZCoord;
    const float PlanetRadiusM  = SkyConfiguration.BottomRadiusKilometres * 1000.0f;

    // Ray span through the planetary boundary layer (fog never lives above ~3 km, so the march is bounded by the BPL top).
    const float BoundaryTopAltitude = 3200.0f;
    const float AltitudeSlope       = ViewDir.ZCoord;
    float       SpanMetres          = Configuration.MaximumTraceDistanceMetres;

    if (AltitudeSlope > 1.0e-4f)
        SpanMetres = std::min(SpanMetres, std::max(0.0f, BoundaryTopAltitude - CameraAltitude) / AltitudeSlope);
    else if (AltitudeSlope < -1.0e-4f)
    {
        // Downward rays terminate on the surface, so the span is the altitude divided by the descent rate (flat-earth limit, exact
        // enough inside a 3 km boundary layer where curvature contributes centimetres).
        SpanMetres = std::min(SpanMetres, std::max(0.0f, CameraAltitude) / std::max(1.0e-4f, -AltitudeSlope));
    }

    if (SpanMetres <= 1.0f)
        return Result;

    const float CosineSunView = DotVector(ViewDir, SunDir);

    // Owen-scrambled jitter on the local-volume march: decorrelates the sampling lattice between neighbouring pixels so a thick
    // plume shows grain-free gradients instead of ring banding, without touching the closed-form height layer.
    const uint32_t PixelSeed = (PixelCoordX * 1973u) ^ (PixelCoordY * 9277u) ^ 0x5BF03635u;
    const float    RayJitter = EvaluateOwenSobolUnit(SubSampleIndex, 1u, PixelSeed);

    // Sky-dome fill: fog is dominated by ambient skylight when the sun is low, and by the sun when it is high.
    const SpectralColor OverheadSky = SampleSkyViewTable(SkyTables, SkyConfiguration, Vector3f{ 0.0f, 0.0f, 0.92f });
    const SpectralColor HorizonSky  = SampleSkyViewTable(
        SkyTables, SkyConfiguration, Vector3f{ ViewDir.XCoord, ViewDir.YCoord, 0.06f });
    const float SolarHeight = std::clamp(SunDir.ZCoord * 1.6f + 0.28f, 0.0f, 1.0f);
    const float AmbientGain = Configuration.AmbientInscatterGain * (0.35f + 0.65f * (1.0f - SolarHeight));

    const SpectralColor Ambient{
        (OverheadSky.Red   * SolarHeight + HorizonSky.Red   * (1.0f - SolarHeight)) * AmbientGain,
        (OverheadSky.Green * SolarHeight + HorizonSky.Green * (1.0f - SolarHeight)) * AmbientGain,
        (OverheadSky.Blue  * SolarHeight + HorizonSky.Blue  * (1.0f - SolarHeight)) * AmbientGain };

    // ① Exponential height layer — closed form, no march.
    SpectralColor Radiance{ 0.0f, 0.0f, 0.0f };
    float         Transmittance = 1.0f;

    if (Configuration.HeightLayer.LayerEnabled && Configuration.HeightLayer.ExtinctionPerKilometre > 1.0e-6f)
    {
        // The inversion cap sits at a wind-advected altitude, so the effective layer thickness depends on where this particular ray
        // pierces the cap. Two fixed-point iterations are enough: the crossing altitude moves by metres, not kilometres.
        float EffectiveSpan = SpanMetres;
        if (Configuration.HeightLayer.Category == FogLayerCategory::ValleyStratum
         && Configuration.HeightLayer.InversionReliefMetres > 0.5f)
        {
            const bool WindCoupled = Configuration.WindAdvectionEnabled;
            for (uint32_t Iteration = 0u; Iteration < 2u; ++Iteration)
            {
                const float ProbeAltitude = (EffectiveSpan < SpanMetres)
                    ? std::max(Configuration.HeightLayer.BaseAltitudeMetres,
                               CameraAltitude + AltitudeSlope * EffectiveSpan)
                    : CameraAltitude + AltitudeSlope * EffectiveSpan;
                const Vector3f ProbeScene = AddVector(CameraSceneMetres, ScaleVector(ViewDir, EffectiveSpan));
                const float CapAltitude = EvaluateInversionCapAltitude(
                    Configuration.HeightLayer,
                    Vector3f{ ProbeScene.XCoord, ProbeScene.YCoord, ProbeAltitude },
                    WindCoupled && OptionalWindField != nullptr && OptionalWindField->FieldEnabled,
                    PreparedWindOffsetVolume);

                if (AltitudeSlope > 1.0e-4f)
                    EffectiveSpan = std::clamp((CapAltitude - CameraAltitude) / AltitudeSlope, 0.0f, SpanMetres);
                else if (AltitudeSlope < -1.0e-4f)
                    EffectiveSpan = std::clamp((CameraAltitude - Configuration.HeightLayer.BaseAltitudeMetres)
                                             / -AltitudeSlope, 0.0f, SpanMetres);
                else
                    EffectiveSpan = SpanMetres;
            }
        }

        const float ColumnMetres = IntegrateExponentialColumn(
            Configuration.HeightLayer.BaseAltitudeMetres,
            Configuration.HeightLayer.ScaleHeightMetres,
            CameraAltitude,
            AltitudeSlope,
            EffectiveSpan);

        // Path-averaged density seen by the ForceField advection patchiness (evaluated once at the mid-span point).
        const Vector3f MidScene = AddVector(CameraSceneMetres, ScaleVector(ViewDir, 0.5f * SpanMetres));
        float Patchiness = 1.0f;
        if (Configuration.WindAdvectionEnabled && Configuration.HeightLayer.DensityNoiseStrength > 1.0e-3f)
        {
            Vector3f LagPos = MidScene;
            if (PreparedWindOffsetVolume != nullptr && OptionalWindField != nullptr && OptionalWindField->FieldEnabled)
            {
                Vector3f Displacement{ 0.0f, 0.0f, 0.0f };
                float    Convergence = 0.0f;
                if (SampleAdvectionOffset(*PreparedWindOffsetVolume, MidScene, Displacement, Convergence))
                {
                    LagPos = AddVector(MidScene, Displacement);
                    Patchiness = std::clamp(1.0f + 0.62f * Convergence, 0.25f, 2.4f);
                }
            }
            const float Streak = EvaluateCloudShapeNoise(
                Vector3f{ LagPos.XCoord * 0.00042f, LagPos.YCoord * 0.00042f, LagPos.ZCoord * 0.00110f }, 3u, 331u);
            // Patchiness must be able to clear the layer outright (an advection bank has holes), so it multiplies the WHOLE column
            // rather than nudging the optical depth around its mean.
            Patchiness *= std::clamp(1.0f + Configuration.HeightLayer.DensityNoiseStrength * (Streak - 0.5f) * 2.6f, 0.0f, 2.2f);
        }

        const float OpticalDepth = Configuration.HeightLayer.ExtinctionPerKilometre * 0.001f * ColumnMetres
                                 * std::max(0.05f, Patchiness);
        Result.HeightOpticalDepth = OpticalDepth;
        const float LayerTransmittance = std::exp(-OpticalDepth);

        // Single scattering inside the layer, lit by the solar beam attenuated through the layer above the point plus the dome.
        const SpectralColor SunBeam = EvaluateTransmittanceToSpace(
            SkyTables, SkyConfiguration, (PlanetRadiusM + CameraAltitude) * 0.001f, std::clamp(SunDir.ZCoord, 0.0f, 1.0f));
        const float Phase = EvaluateDualLobePhase(
            CosineSunView,
            Configuration.HeightLayer.ForwardLobeAnisotropy,
            Configuration.HeightLayer.BackLobeAnisotropy,
            Configuration.HeightLayer.ForwardLobeMix);

        // A fog layer of any real optical depth is dominated by multiply-scattered light, which is isotropic. Without this fill the
        // forward lobe alone leaves a sunlit fog sea flat grey, because the camera almost never sits in the lobe.
        const float IsotropicPhase = 1.0f / (4.0f * PiConstant);
        const float MultipleScatterFill = 0.32f * (1.0f - std::exp(-1.6f * std::max(0.0f, Result.HeightOpticalDepth)));
        const float LayerScatter = Configuration.HeightLayer.SingleScatteringAlbedo
                                 * (Phase + MultipleScatterFill * IsotropicPhase);
        const SpectralColor LayerRadiance{
            (SkyConfiguration.ExtraterrestrialSolarFlux.Red   * SunBeam.Red   * LayerScatter + Ambient.Red)   * (1.0f - LayerTransmittance),
            (SkyConfiguration.ExtraterrestrialSolarFlux.Green * SunBeam.Green * LayerScatter + Ambient.Green) * (1.0f - LayerTransmittance),
            (SkyConfiguration.ExtraterrestrialSolarFlux.Blue  * SunBeam.Blue  * LayerScatter + Ambient.Blue)  * (1.0f - LayerTransmittance) };

        Radiance     = AddSpectral(Radiance, LayerRadiance);
        Transmittance *= LayerTransmittance;
    }

    // ② Local ellipsoidal fog bodies — marched with the same multiple-scattering ladder the cloud deck uses.
    if (!Configuration.LocalVolumes.empty() && Transmittance > 0.004f)
    {
        const uint32_t StepCount = std::clamp(Configuration.VolumeMarchStepCount, 8u, 96u);
        const float    StepMetres = SpanMetres / static_cast<float>(StepCount);

        for (const LocalFogVolume& Body : Configuration.LocalVolumes)
        {
            if (!Body.VolumeEnabled || Body.ExtinctionPerKilometre <= 1.0e-5f)
                continue;

            const Vector3f Center = ResolveAdvectedFogCenter(Configuration, Body, OptionalWindField, SkyConfiguration.SequenceSeconds);
            const float ExtentX = std::max(50.0f, Body.HalfExtentsMetres.XCoord);
            const float ExtentY = std::max(50.0f, Body.HalfExtentsMetres.YCoord);
            const float ExtentZ = std::max(30.0f, Body.HalfExtentsMetres.ZCoord);

            const float Phase = EvaluateDualLobePhase(
                CosineSunView,
                Configuration.HeightLayer.ForwardLobeAnisotropy * Body.AnisotropyGain,
                Configuration.HeightLayer.BackLobeAnisotropy,
                Configuration.HeightLayer.ForwardLobeMix);

            SpectralColor BodyRadiance{ 0.0f, 0.0f, 0.0f };
            float         BodyTransmittance = 1.0f;
            float         BodyOpticalDepth  = 0.0f;

            for (uint32_t Step = 0u; Step < StepCount; ++Step)
            {
                const float Travel = (static_cast<float>(Step) + RayJitter) * StepMetres;
                if (Travel > Configuration.MaximumTraceDistanceMetres)
                    break;

                const Vector3f SamplePos = AddVector(CameraSceneMetres, ScaleVector(ViewDir, Travel));
                const float NormX = (SamplePos.XCoord - Center.XCoord) / ExtentX;
                const float NormY = (SamplePos.YCoord - Center.YCoord) / ExtentY;
                const float NormZ = (SamplePos.ZCoord - Center.ZCoord) / ExtentZ;
                const float RadiusSq = NormX * NormX + NormY * NormY + NormZ * NormZ;
                if (RadiusSq >= 1.0f)
                    continue;

                // Diffuse shell: EdgeSoftness widens the envelope so the body fades instead of ending on a hard ellipsoid surface.
                float Shell = std::pow(std::clamp(1.0f - RadiusSq, 0.0f, 1.0f), 0.6f + 2.2f * Body.EdgeSoftness);

                // Billow: the same rotated-gradient basis the cloud deck uses, sampled at the Lagrangian position so the lobes
                // are stretched downstream by the flow instead of sitting statically inside the envelope.
                if (Body.DensityNoiseStrength > 1.0e-3f)
                {
                    Vector3f LagPos = SamplePos;
                    if (PreparedWindOffsetVolume != nullptr && OptionalWindField != nullptr && OptionalWindField->FieldEnabled)
                    {
                        Vector3f Displacement{ 0.0f, 0.0f, 0.0f };
                        float    Convergence = 0.0f;
                        if (SampleAdvectionOffset(*PreparedWindOffsetVolume, SamplePos, Displacement, Convergence))
                            LagPos = AddVector(SamplePos, Displacement);
                    }
                    const float Billow = EvaluateCloudShapeNoise(
                        Vector3f{ LagPos.XCoord * 0.00062f, LagPos.YCoord * 0.00062f, LagPos.ZCoord * 0.00150f }, 4u, 977u);
                    Shell *= std::clamp(1.0f - Body.DensityNoiseStrength + 2.0f * Body.DensityNoiseStrength * Billow, 0.0f, 1.6f);
                }

                const float Density = Shell * Body.ExtinctionPerKilometre * 0.001f;
                if (Density <= 1.0e-8f)
                    continue;

                const float StepOptical = Density * StepMetres;
                const float StepTrans   = std::exp(-StepOptical);
                BodyOpticalDepth += StepOptical;

                const float SampleAltitude = CameraAltitude + AltitudeSlope * Travel;
                const SpectralColor SunBeam = EvaluateTransmittanceToSpace(
                    SkyTables, SkyConfiguration, (PlanetRadiusM + std::max(0.0f, SampleAltitude)) * 0.001f,
                    std::clamp(SunDir.ZCoord, 0.0f, 1.0f));

                // Two-octave ladder so the interior of a thick plume stays a luminous grey rather than a flat silhouette. Each
                // successive octave is more isotropic (it has scattered more times), which is what makes a fog sea glow.
                const float IsotropicPhase = 1.0f / (4.0f * PiConstant);
                const float MultipleScatterFill = 0.32f * (1.0f - std::exp(-1.6f * std::max(0.0f, BodyOpticalDepth)));

                float SunEnergy = 0.0f;
                float Attenuation = 1.0f;
                float Contribution = 1.0f;
                for (uint32_t Octave = 0u; Octave < 2u; ++Octave)
                {
                    const float OctavePhase = Phase
                                            + MultipleScatterFill * IsotropicPhase * (0.45f + 0.55f * static_cast<float>(Octave));
                    SunEnergy += Contribution * OctavePhase * std::exp(-BodyOpticalDepth * Attenuation);
                    Attenuation *= 0.45f;
                    Contribution *= 0.55f;
                }

                const float Albedo = Configuration.HeightLayer.SingleScatteringAlbedo;
                const SpectralColor StepSource{
                    Density * Albedo * (SkyConfiguration.ExtraterrestrialSolarFlux.Red   * SunBeam.Red   * SunEnergy + Ambient.Red),
                    Density * Albedo * (SkyConfiguration.ExtraterrestrialSolarFlux.Green * SunBeam.Green * SunEnergy + Ambient.Green),
                    Density * Albedo * (SkyConfiguration.ExtraterrestrialSolarFlux.Blue  * SunBeam.Blue  * SunEnergy + Ambient.Blue) };

                const float StepIntegral = (1.0f - StepTrans) / std::max(1.0e-9f, Density);
                BodyRadiance.Red   += BodyTransmittance * StepSource.Red   * StepIntegral;
                BodyRadiance.Green += BodyTransmittance * StepSource.Green * StepIntegral;
                BodyRadiance.Blue  += BodyTransmittance * StepSource.Blue  * StepIntegral;
                BodyTransmittance  *= StepTrans;

                if (BodyTransmittance < 0.01f)
                    break;
            }

            if (BodyOpticalDepth <= 1.0e-6f)
                continue;

            Result.VolumeOpticalDepth += BodyOpticalDepth;
            Radiance = AddSpectral(Radiance, ScaleSpectral(BodyRadiance, Transmittance));
            Transmittance *= BodyTransmittance;
        }
    }

    Result.InscatteredRadiance = Radiance;
    Result.Transmittance       = std::clamp(Transmittance, 0.0f, 1.0f);
    return Result;
}

} // namespace Frontier
