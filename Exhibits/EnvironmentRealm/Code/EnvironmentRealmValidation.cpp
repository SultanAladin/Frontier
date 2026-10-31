/*==========================================================================================================================================
                                             ENVIRONMENT REALM VALIDATION & EXHIBITS
                                     ------------------------------------------------------------------
             End-to-end CPU + Vulkan 1.3 Slang GPU verification of all 5 unified rendering systems
                    plus multi-instance / cross-realm Day-Night Portal ensemble rendering.
==========================================================================================================================================*/

#include "Engine/Rendering/AtmosphereLutSubmission.h"
#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CelestialDomeModel.h"
#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/EnvironmentRealmEnsemble.h"
#include "Engine/Rendering/EnvironmentSubmissionSequence.h"
#include "Engine/Rendering/FogMedium.h"
#include "Engine/Rendering/LensFlareModel.h"
#include "Engine/Rendering/LunarDiscProjection.h"
#include "Engine/Rendering/SolarDiscFilter.h"
#include "Engine/Rendering/SolarSkyEnvironment.h"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

void RequireCheck(bool Condition, const char* Expression, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[EnvironmentRealmValidation] FAILED at line " << Line << ": " << Expression << " (" << Detail << ")\n";
        std::exit(1);
    }
}

#define REQUIRE_ENV(Cond, Msg) RequireCheck((Cond), #Cond, (Msg), __LINE__)

std::array<float, 3> Norm3(float X, float Y, float Z)
{
    const float L = std::sqrt(X * X + Y * Y + Z * Z);
    return (L > 1.0e-12f) ? std::array<float, 3>{X / L, Y / L, Z / L} : std::array<float, 3>{0.0f, 1.0f, 0.0f};
}

Vec3 NormVec3(float X, float Y, float Z)
{
    const auto A = Norm3(X, Y, Z);
    return {A[0], A[1], A[2]};
}

SpatialVector NormSpatial(double X, double Y, double Z)
{
    const double L = std::sqrt(X * X + Y * Y + Z * Z);
    return (L > 1.0e-12) ? SpatialVector{X / L, Y / L, Z / L} : SpatialVector{0.0, 0.0, 1.0};
}

std::array<float, 3> Cross3(std::array<float, 3> A, std::array<float, 3> B)
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

EnvironmentCameraView MakeLookAtCamera(uint32_t             RealmIndex,
                                       std::array<float, 3> Eye,
                                       std::array<float, 3> Target,
                                       uint32_t             W,
                                       uint32_t             H,
                                       float                FovDeg   = 60.0f,
                                       float                Exposure = 1.0f)
{
    EnvironmentCameraView Cam{};
    Cam.ActiveRealmIndex = RealmIndex;
    Cam.PositionMeters   = Eye;
    Cam.ForwardUnit      = Norm3(Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]);
    const auto WorldUp   = std::array<float, 3>{0.0f, 1.0f, 0.0f};
    const auto RightRaw  = Cross3(WorldUp, Cam.ForwardUnit);
    Cam.RightUnit        = Norm3(RightRaw[0], RightRaw[1], RightRaw[2]);
    const auto UpRaw     = Cross3(Cam.ForwardUnit, Cam.RightUnit);
    Cam.UpUnit           = Norm3(UpRaw[0], UpRaw[1], UpRaw[2]);
    Cam.VerticalFovRad   = FovDeg * 3.14159265358979323846f / 180.0f;
    Cam.ViewportWidth    = W;
    Cam.ViewportHeight   = H;
    Cam.ExposureScale    = Exposure;
    return Cam;
}

bool WritePngRgba(const std::filesystem::path& FilePath, uint32_t W, uint32_t H, const std::vector<uint8_t>& Rgba)
{
    std::error_code Ec;
    std::filesystem::create_directories(FilePath.parent_path(), Ec);
    return stbi_write_png(FilePath.string().c_str(), static_cast<int>(W), static_cast<int>(H), 4, Rgba.data(), static_cast<int>(W * 4u)) != 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                      1. CPU ORACLE VERIFICATION OF ALL 5 COUPLED PHYSICAL MODELS
//------------------------------------------------------------------------------------------------------------------------

void VerifyPhysicalModelsAgainstOracles(const std::filesystem::path& RepoRoot)
{
    std::cout << "[1/4] Verifying 6-pt Gauss-Legendre Solar/Sky NEE (< 2.85% vs FP64 Oracle)...\n";
    {
        SolarSkySpecification Spec{};
        Spec.Atmosphere         = MakeEarthAtmosphere();
        Spec.LocalUpDirection   = {0.0, 0.0, 1.0};
        Spec.ObserverAltitudeKm = 0.05;

        const std::array<double, 4> SunElevationsDeg  = {54.0, 16.0, 3.2, -2.5};
        const std::array<double, 4> ViewElevationsDeg = {68.0, 30.0, 9.5, 24.0};
        double                      MaxRelErr         = 0.0;

        for (double SunDeg : SunElevationsDeg)
        {
            const double          SunRad = SunDeg * 3.14159265358979323846 / 180.0;
            SolarSkySourceProfile Source{};
            Source.SolarDirection  = NormSpatial(0.18 * std::cos(SunRad), 0.9836 * std::cos(SunRad), std::sin(SunRad));
            Source.SolarIrradiance = {18.5, 18.0, 17.2};
            Spec.SolarSources      = {Source};

            for (double ViewDeg : ViewElevationsDeg)
            {
                const double        ViewRad = ViewDeg * 3.14159265358979323846 / 180.0;
                const SpatialVector ViewDir = NormSpatial(0.32 * std::cos(ViewRad), 0.9474 * std::cos(ViewRad), std::sin(ViewRad));
                const SpectrumRgb   Fast6Pt = EvaluateSolarSkyGaussLegendre6Pt(Spec, {0.0, 0.0, 50.0}, ViewDir);
                const SpectrumRgb   Oracle  = EvaluateSolarSkyOracleContinuum(Spec, {0.0, 0.0, 50.0}, ViewDir, 256u);

                const double FastLum   = 0.2126 * Fast6Pt.R + 0.7152 * Fast6Pt.G + 0.0722 * Fast6Pt.B;
                const double OracleLum = 0.2126 * Oracle.R + 0.7152 * Oracle.G + 0.0722 * Oracle.B;
                if (OracleLum > 1.0e-4)
                {
                    const double RelErr = std::abs(FastLum - OracleLum) / OracleLum;
                    MaxRelErr           = std::max(MaxRelErr, RelErr);
                    if (RelErr >= 0.0285)
                    {
                        std::cerr << "  [SkyGL] SunDeg=" << SunDeg << " ViewDeg=" << ViewDeg
                                  << " FastLum=" << FastLum << " OracleLum=" << OracleLum
                                  << " RelErr=" << (RelErr * 100.0) << "%\n";
                    }
                    REQUIRE_ENV(RelErr < 0.0285, "6-pt Gauss-Legendre sky quadrature must stay within 2.85% of FP64 oracle");
                }
            }
        }
        std::cout << "      Max 6-pt GL sky relative error across day/golden-hour/twilight = " << (MaxRelErr * 100.0) << "% (< 2.85%)\n";
    }

    std::cout << "[2/4] Verifying Meeus Ephemeris, 2048x1024x6 Lunar Atlas, Scintillating Stars & Ringed Planets...\n";
    {
        // Meeus Ch. 47 geocentric + topocentric ephemeris
        const double Jd       = JulianDate(2026, 10, 31, 20.0);
        const auto   SunPos   = SunPosition(Jd, -26.188, 28.320);
        const auto   SunVec   = SunDirectionLocal(SunPos);
        const auto   MoonGeo  = MoonGeocentric(Jd);
        const auto   MoonTopo = MoonTopocentric(MoonGeo, Jd, -26.188, 28.320, 1650.0);
        const double LenSq    = SunVec.X * SunVec.X + SunVec.Y * SunVec.Y + SunVec.Z * SunVec.Z;
        REQUIRE_ENV(std::abs(LenSq - 1.0) < 1.0e-6, "Solar ephemeris vector must be unit length");
        REQUIRE_ENV(MoonGeo.DistanceKm > 350000.0 && MoonGeo.DistanceKm < 410000.0 && MoonTopo.AngularRadiusRad > 0.004,
                    "Meeus Ch.47 lunar perigee/apogee distance and topocentric radius must be physical");

        // SolarDiscPixelFilter finite-pixel anti-aliasing
        const SunDiscProfile       Disc{};
        const SolarDiscPixelFilter Filter(Disc);
        const SpectrumRgb          CentIn = Filter.Evaluate(0.0, 0.0, 0.0006, 0.0006, {100.0, 100.0, 100.0});
        const SpectrumRgb          OutIn  = Filter.Evaluate(0.04, 0.0, 0.0006, 0.0006, {100.0, 100.0, 100.0});
        REQUIRE_ENV(CentIn.R > 50.0 && OutIn.R < 1.0e-4, "SolarDiscPixelFilter must preserve core energy and vanish outside limb");

        // CelestialDomeModel: 2048x1024x6 Lunar Atlas + Hipparcos8.csv + Ringed Planets
        CelestialDomeModel Dome{};
        const auto         JpgPath = RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg";
        const auto         CsvPath = RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/Hipparcos8.csv";
        REQUIRE_ENV(Dome.LoadLunarAtlas(JpgPath.string(), 2048u, 1024u), "MoonAlbedoSource2048.jpg must build 6-body 2K atlas");
        REQUIRE_ENV(Dome.LunarAtlas().Width() == 2048u && Dome.LunarAtlas().Height() == 1024u, "Lunar atlas must be 2048x1024");
        REQUIRE_ENV(Dome.LoadHipparcosCatalogueCsv(CsvPath.string()), "Hipparcos8.csv must decode into 32x32 octahedral index");
        REQUIRE_ENV(Dome.SourceStarCount() > 30000u && Dome.NonEmptyCellCount() > 800u,
                    "Octahedral star index must populate > 30k Hipparcos stars across sphere");

        // Lunar phase ordering: Full Moon > Quarter Moon > New Moon (Earthshine > 0)
        LunarBodyDescriptor Moon{};
        Moon.Preset           = LunarSurfacePreset::LunaSelenographic;
        Moon.DirectionUnit    = NormVec3(0.0f, 0.8f, 0.6f);
        Moon.AngularRadiusRad = 0.03f;
        Dome.RegisterLunarBody(Moon);

        const std::array<CelestialSolarSource, 1> FullSun{{{NormVec3(0.0f, -0.8f, -0.6f), {18.0f, 18.0f, 18.0f}, 0.00465f}}};
        const std::array<CelestialSolarSource, 1> QuarterSun{{{NormVec3(1.0f, 0.0f, 0.0f), {18.0f, 18.0f, 18.0f}, 0.00465f}}};
        const std::array<CelestialSolarSource, 1> NewSun{{{NormVec3(0.0f, 0.8f, 0.6f), {18.0f, 18.0f, 18.0f}, 0.00465f}}};

        const auto FullRay    = Dome.EvaluateCelestialRay(Moon.DirectionUnit, FullSun, 0.001f);
        const auto QuarterRay = Dome.EvaluateCelestialRay(Moon.DirectionUnit, QuarterSun, 0.001f);
        const auto NewRay     = Dome.EvaluateCelestialRay(Moon.DirectionUnit, NewSun, 0.001f);
        REQUIRE_ENV(FullRay.HitLunarDisc && FullRay.RadianceRgb.Y > QuarterRay.RadianceRgb.Y &&
                        QuarterRay.RadianceRgb.Y > NewRay.RadianceRgb.Y && NewRay.RadianceRgb.Z > 0.0f,
                    "Lunar Lommel-Seeliger phase curve and earthshine must satisfy Full > Quarter > New > 0");

        // Ringed & Atmospheric Planet mutual shadow check
        Dome.ClearBodies();
        PlanetaryBodyDescriptor Giant{};
        Giant.Archetype               = PlanetarySurfaceArchetype::BandedGasGiant;
        Giant.DirectionUnit           = NormVec3(0.0f, 1.0f, 0.0f);
        Giant.AngularRadiusRad        = 0.06f;
        Giant.SpinAxisUnit            = NormVec3(0.0f, -0.35f, 0.9367f);
        Giant.Rings.Enabled           = true;
        Giant.Rings.RingNormalUnit    = Giant.SpinAxisUnit;
        Giant.Rings.InnerRadiusRatio  = 1.35f;
        Giant.Rings.OuterRadiusRatio  = 2.25f;
        Giant.Rings.PeakOpticalDepth  = 1.8f;
        Dome.RegisterPlanetaryBody(Giant);

        const std::array<CelestialSolarSource, 1> SideSun{{{NormVec3(1.0f, -0.25f, 0.12f), {20.0f, 20.0f, 20.0f}, 0.00465f}}};
        const auto LitFlank    = Dome.EvaluateCelestialRay(NormVec3(0.095f, 1.0f, 0.01f), SideSun, 0.001f);
        const auto ShadowFlank = Dome.EvaluateCelestialRay(NormVec3(-0.095f, 1.0f, -0.01f), SideSun, 0.001f);
        REQUIRE_ENV(LitFlank.HitPlanetaryRings && ShadowFlank.HitPlanetaryRings &&
                        ShadowFlank.InPlanetShadowOnRings && LitFlank.RadianceRgb.X > ShadowFlank.RadianceRgb.X * 2.5f,
                    "Planetary body must cast geometric shadow across anti-solar ring sector");
    }

    std::cout << "[3/4] Verifying Volumetric Clouds (Slabs + Local 3D Boxes + Ortho Shadows) & Coupled Volumetric Fog...\n";
    {
        CloudVolumeProfile LocalBox{};
        LocalBox.Domain              = CloudVolumeDomain::BoundedLocalBox;
        LocalBox.BoxCenterM          = {0.0, 120.0, 80.0};
        LocalBox.BoxHalfExtentsM     = {45.0, 45.0, 25.0};
        LocalBox.BoxEdgeFalloffM     = 6.0;
        LocalBox.Slab.Coverage       = 0.88;
        LocalBox.Slab.DensityScale   = 2.2;
        LocalBox.Slab.ExtinctionPerM = 0.06;

        const double InsideDens  = EvaluateCloudVolumeDensity(LocalBox, {2.0, 120.0, 80.0});
        const double OutsideDens = EvaluateCloudVolumeDensity(LocalBox, {95.0, 120.0, 80.0});
        REQUIRE_ENV(InsideDens > 0.0 && OutsideDens == 0.0, "BoundedLocalBox cloud volume must have zero leakage outside bounds");

        CloudShadowProjection ShadowMap{};
        const std::array<CloudVolumeProfile, 1> Vols{{LocalBox}};
        REQUIRE_ENV(ShadowMap.Bake(Vols, {0.0, 120.0, 0.0}, {0.0, 0.0, 1.0}, 140.0, 64u, 20u), "CloudShadowProjection::Bake failed");
        const double CenterTrans = ShadowMap.SampleTransmittance({0.0, 120.0, 0.0});
        const double ClearTrans  = ShadowMap.SampleTransmittance({120.0, 230.0, 0.0});
        REQUIRE_ENV(CenterTrans < 0.85 && ClearTrans > 0.99, "Orthographic CloudShadowProjection must attenuate beneath cloud box");

        // Volumetric fog horizontal stability + IES + lightning + fire scattering
        FogParameters Hf{};
        Hf.DensityAtBase  = 0.01;
        Hf.FalloffHeightM = 20.0;
        const double HorizTrans = FogTransmittance(Hf, {0.0, 0.0, 2.0}, {0.0, 1.0, 0.0}, 40.0);
        REQUIRE_ENV(std::isfinite(HorizTrans) && HorizTrans > 0.60 && HorizTrans < 0.72,
                    "FogTransmittance must remain exact and finite on horizontal rays (dz = 0)");

        LocalFogVolumeProfile FogBox{};
        FogBox.BoxCenterM      = {0.0, 15.0, 3.0};
        FogBox.BoxHalfExtentsM = {8.0, 8.0, 4.0};
        FogBox.ExtinctionPerM  = 0.06;

        PhotometricFogLuminaire Spot{};
        Spot.PositionM     = {0.0, 15.0, 5.5};
        Spot.AxisUnit      = {0.0, 0.0, -1.0};
        Spot.PeakIntensity = {140.0, 115.0, 80.0};

        LightningFogChannel Bolt{};
        Bolt.SegmentStartM = {-2.0, 16.0, 8.0};
        Bolt.SegmentEndM   = {-1.5, 16.0, 0.2};
        Bolt.LineRadiance  = {90.0, 130.0, 210.0};

        RadiantFogFacet Fire{};
        Fire.CornerA         = {1.5, 14.0, 0.2};
        Fire.CornerB         = {2.5, 14.0, 0.2};
        Fire.CornerC         = {2.0, 14.0, 1.4};
        Fire.SurfaceRadiance = {30.0, 11.0, 2.5};

        UnifiedVolumetricFogSpecification UnlitSpec{};
        UnlitSpec.HeightFog    = Hf;
        UnlitSpec.LocalVolumes = std::span<const LocalFogVolumeProfile>{&FogBox, 1};
        const auto UnlitRes    = EvaluateUnifiedVolumetricFog(UnlitSpec, {0.0, 0.0, 2.0}, {0.0, 1.0, 0.0}, 25.0);

        UnifiedVolumetricFogSpecification LitSpec = UnlitSpec;
        LitSpec.Luminaires        = std::span<const PhotometricFogLuminaire>{&Spot, 1};
        LitSpec.LightningChannels = std::span<const LightningFogChannel>{&Bolt, 1};
        LitSpec.RadiantFacets     = std::span<const RadiantFogFacet>{&Fire, 1};
        const auto LitRes         = EvaluateUnifiedVolumetricFog(LitSpec, {0.0, 0.0, 2.0}, {0.0, 1.0, 0.0}, 25.0);

        REQUIRE_ENV(LitRes.Radiance.R > UnlitRes.Radiance.R * 3.0 && LitRes.Transmittance < 0.85,
                    "Unified volumetric fog must scatter IES luminaire, lightning channel, and thermal fire facet radiance");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                  2. BUILD THE MULTI-REALM & DAY/NIGHT PORTAL ENSEMBLE SCENES
//------------------------------------------------------------------------------------------------------------------------

EnvironmentRealmEnsemble BuildThreeRealmPortalWorld()
{
    EnvironmentRealmEnsemble World{};
    World.Realms.resize(3u);

    // ============================================================================================================
    // REALM 0: Sunny Golden-Blue Daytime World (Global + Local 3D Clouds, Cloud Shadows, Sun Shafts, Lens Flare)
    // ============================================================================================================
    {
        EnvironmentRealm& R   = World.Realms[0];
        R.Atmosphere          = MakeEarthAtmosphere();
        R.ObserverAltitudeKm  = 0.08f;
        R.GroundPlaneY        = 0.0f;
        R.GroundAlbedoRgb     = {0.22f, 0.24f, 0.19f};
        R.StarBrightnessScale = 0.0f;
        R.MilkyWayIntensity   = 0.0f;
        R.AtmosphereRevision  = 101u;
        R.SkyViewRevision     = 201u;

        RealmSolarSource Sun{};
        Sun.DirectionUnit            = Norm3(-0.38f, 0.34f, 0.86f);
        Sun.DiscIrradiance           = {1.25f, 1.16f, 1.02f};
        Sun.MasterFlareGain          = 1.15f;
        Sun.ChromaticShift           = 0.008f;
        Sun.Flare.ApertureBlades     = 6u;
        Sun.Flare.GhostStrength      = 0.95;
        Sun.Flare.HaloStrength       = 0.65;
        Sun.Flare.StreakStrength     = 0.75;
        Sun.Flare.AnamorphicStrength = 0.55;
        Sun.Flare.StarburstStrength  = 1.10;
        R.Suns.push_back(Sun);

        LunarBodyDescriptor DayMoon{};
        DayMoon.Preset            = LunarSurfacePreset::LunaSelenographic;
        DayMoon.DirectionUnit     = NormVec3(0.45f, 0.52f, 0.72f);
        DayMoon.AngularRadiusRad  = 0.032f;
        DayMoon.SurfaceBrightness = 1.6f;
        R.Moons.push_back(DayMoon);

        CloudVolumeProfile GlobalSlab{};
        GlobalSlab.Domain              = CloudVolumeDomain::PlanetarySlab;
        GlobalSlab.Slab.BaseHeightM    = 1350.0;
        GlobalSlab.Slab.TopHeightM     = 2000.0;
        GlobalSlab.Slab.Coverage       = 0.52;
        GlobalSlab.Slab.DensityScale   = 1.35;
        GlobalSlab.Slab.ExtinctionPerM = 0.042;
        GlobalSlab.PowderStrength      = 0.82;
        R.CloudVolumes.push_back(GlobalSlab);

        CloudVolumeProfile LocalPlume{};
        LocalPlume.Domain              = CloudVolumeDomain::BoundedLocalBox;
        LocalPlume.BoxCenterM          = {-18.0, 22.0, 48.0};
        LocalPlume.BoxHalfExtentsM     = {14.0, 6.5, 12.0};
        LocalPlume.BoxEdgeFalloffM     = 4.0;
        LocalPlume.Slab.Coverage       = 0.78;
        LocalPlume.Slab.DensityScale   = 1.8;
        LocalPlume.Slab.ExtinctionPerM = 0.09;
        R.CloudVolumes.push_back(LocalPlume);
        R.CloudShadowHalfSpanM = 120.0f;

        R.HeightFog.DensityAtBase  = 0.0012;
        R.HeightFog.FalloffHeightM = 12.0;
        R.HeightFog.BaseHeightM    = 0.0;

        LocalFogVolumeProfile ShaftBox{};
        ShaftBox.BoxCenterM       = {-7.0, 3.2, 16.0};
        ShaftBox.BoxHalfExtentsM  = {6.0, 3.5, 8.0};
        ShaftBox.ExtinctionPerM   = 0.032;
        ShaftBox.NoiseModulation  = 0.45;
        ShaftBox.NoiseScaleM      = 4.5;
        ShaftBox.PhaseAsymmetryG  = 0.68;
        R.LocalFogVolumes.push_back(ShaftBox);

        R.SolidOccluders.push_back({{-6.2f, 2.0f, 14.5f}, 2.0f, {0.76f, 0.72f, 0.66f}, 0.4f});
        R.SolidOccluders.push_back({{7.0f, 1.4f, 18.0f}, 1.4f, {0.65f, 0.34f, 0.26f}, 0.35f});

        RealmGlassPane Glass{};
        Glass.CenterMeters      = {5.2f, 2.2f, 9.5f};
        Glass.NormalUnit        = {0.0f, 0.0f, -1.0f};
        Glass.HalfWidthMeters   = 1.5f;
        Glass.HalfHeightMeters  = 1.8f;
        Glass.TintTransmittance = {0.72f, 0.90f, 0.98f};
        Glass.FresnelF0         = 0.05f;
        R.GlassPanes.push_back(Glass);
    }

    // ============================================================================================================
    // REALM 1: Deep Starry Night World (Hipparcos Stars + Milky Way + Ringed Gas Giant + 2 Moons + Nocturnal Fog)
    // ============================================================================================================
    {
        EnvironmentRealm& R     = World.Realms[1];
        R.Atmosphere            = MakeEarthAtmosphere();
        R.ObserverAltitudeKm    = 0.20f;
        R.GroundPlaneY          = 0.0f;
        R.GroundAlbedoRgb       = {0.14f, 0.15f, 0.19f};
        R.StarBrightnessScale   = 2.2f;
        R.StarLimitingMagnitude = 6.6f;
        R.ScintillationStrength = 1.25f;
        R.MilkyWayIntensity     = 2.4f;
        R.StarBasisRow0         = Norm3(0.82f, 0.18f, -0.54f);
        R.StarBasisRow1         = Norm3(-0.22f, 0.97f, -0.06f);
        R.AtmosphereRevision    = 102u;
        R.SkyViewRevision       = 202u;

        RealmSolarSource NightSun{};
        NightSun.DirectionUnit   = Norm3(-0.72f, -0.38f, -0.58f);
        NightSun.DiscIrradiance  = {1.30f, 1.24f, 1.18f};
        NightSun.EnableLensFlare = false;
        R.Suns.push_back(NightSun);

        LunarBodyDescriptor Luna{};
        Luna.Preset             = LunarSurfacePreset::LunaSelenographic;
        Luna.DirectionUnit      = NormVec3(-0.16f, 0.42f, 0.89f);
        Luna.AngularRadiusRad   = 0.048f;
        Luna.SurfaceBrightness  = 2.4f;
        Luna.EarthshineStrength = 0.014f;
        R.Moons.push_back(Luna);

        LunarBodyDescriptor VolcanicMoon{};
        VolcanicMoon.Preset             = LunarSurfacePreset::EmberVolcanic;
        VolcanicMoon.DirectionUnit      = NormVec3(0.28f, 0.54f, 0.79f);
        VolcanicMoon.AngularRadiusRad   = 0.026f;
        VolcanicMoon.SurfaceBrightness  = 2.8f;
        VolcanicMoon.EarthshineStrength = 0.020f;
        R.Moons.push_back(VolcanicMoon);

        PlanetaryBodyDescriptor SaturnLike{};
        SaturnLike.Archetype                      = PlanetarySurfaceArchetype::BandedGasGiant;
        SaturnLike.DirectionUnit                  = NormVec3(0.06f, 0.30f, 0.95f);
        SaturnLike.AngularRadiusRad               = 0.075f;
        SaturnLike.SpinAxisUnit                   = NormVec3(0.32f, 0.89f, -0.32f);
        SaturnLike.SurfaceAlbedoScale             = 1.25f;
        SaturnLike.Atmosphere.Enabled             = true;
        SaturnLike.Atmosphere.ShellHeightFraction = 0.09f;
        SaturnLike.Rings.Enabled                  = true;
        SaturnLike.Rings.InnerRadiusRatio         = 1.32f;
        SaturnLike.Rings.OuterRadiusRatio         = 2.28f;
        SaturnLike.Rings.PeakOpticalDepth         = 2.1f;
        SaturnLike.Rings.OuterTintRgb             = {0.90f, 0.83f, 0.70f};
        R.Planets.push_back(SaturnLike);

        PlanetaryBodyDescriptor OceanWorld{};
        OceanWorld.Archetype                      = PlanetarySurfaceArchetype::OceanicTerrestrial;
        OceanWorld.DirectionUnit                  = NormVec3(-0.34f, 0.24f, 0.91f);
        OceanWorld.AngularRadiusRad               = 0.036f;
        OceanWorld.SurfaceAlbedoScale             = 1.15f;
        OceanWorld.Atmosphere.Enabled             = true;
        OceanWorld.Atmosphere.ShellHeightFraction = 0.11f;
        OceanWorld.Rings.Enabled                  = false;
        R.Planets.push_back(OceanWorld);

        // Custom bright navigational reference star with Airy tint
        CustomStarDescriptor BrightCustom{};
        BrightCustom.DirectionUnit     = NormVec3(-0.04f, 0.58f, 0.81f);
        BrightCustom.TemperatureKelvin = 10500.0f;
        BrightCustom.RadianceGain      = 28.0f;
        R.CustomStars.push_back(BrightCustom);

        R.HeightFog.DensityAtBase  = 0.004;
        R.HeightFog.FalloffHeightM = 10.0;

        LocalFogVolumeProfile NightFog{};
        NightFog.BoxCenterM      = {0.0, 2.2, 16.0};
        NightFog.BoxHalfExtentsM = {9.0, 3.2, 9.0};
        NightFog.ExtinctionPerM  = 0.048;
        NightFog.NoiseModulation = 0.55;
        NightFog.NoiseScaleM     = 4.0;
        R.LocalFogVolumes.push_back(NightFog);

        PhotometricFogLuminaire Spot{};
        Spot.PositionM     = {-2.6, 4.8, 14.0};
        Spot.AxisUnit      = {0.18, -0.96, 0.20};
        Spot.PeakIntensity = {95.0, 74.0, 42.0};
        Spot.OuterConeCos  = 0.62;
        Spot.RangeMeters   = 14.0;
        R.Luminaires.push_back(Spot);

        LightningFogChannel Bolt{};
        Bolt.SegmentStartM = {3.2, 7.5, 19.0};
        Bolt.SegmentEndM   = {2.6, 0.2, 18.5};
        Bolt.LineRadiance  = {16.0, 20.0, 28.0};
        Bolt.CoreRadiusM   = 0.15;
        R.LightningChannels.push_back(Bolt);

        RadiantFogFacet Fire{};
        Fire.CornerA         = {0.2, 0.1, 13.0};
        Fire.CornerB         = {1.2, 0.1, 13.0};
        Fire.CornerC         = {0.7, 1.3, 13.0};
        Fire.SurfaceRadiance = {22.0, 7.5, 1.6};
        R.RadiantFacets.push_back(Fire);

        R.SolidOccluders.push_back({{-2.2f, 1.2f, 16.5f}, 1.2f, {0.55f, 0.58f, 0.66f}, 0.5f});
    }

    // ============================================================================================================
    // REALM 2: Binary-Sun Alien Exoplanet World (Distinct Rust-Violet Atmosphere LUT + 2 Suns + Ice Moons)
    // ============================================================================================================
    {
        EnvironmentRealm& R             = World.Realms[2];
        R.Atmosphere                    = MakeEarthAtmosphere();
        R.Atmosphere.RayleighScattering = {14.5e-3, 9.2e-3, 18.8e-3};
        R.Atmosphere.MieScattering      = 7.5e-3;
        R.ObserverAltitudeKm            = 0.06f;
        R.GroundAlbedoRgb               = {0.34f, 0.18f, 0.13f};
        R.StarBrightnessScale           = 0.45f;
        R.MilkyWayIntensity             = 0.6f;
        R.AtmosphereRevision            = 103u;
        R.SkyViewRevision               = 203u;

        RealmSolarSource WarmStar{};
        WarmStar.DirectionUnit           = Norm3(-0.30f, 0.16f, 0.94f);
        WarmStar.DiscIrradiance          = {1.35f, 0.82f, 0.46f};
        WarmStar.Disc.AngularRadiusRad   = 0.0085;
        WarmStar.MasterFlareGain         = 0.78f;
        WarmStar.Flare.HaloStrength      = 0.36;
        WarmStar.Flare.StarburstStrength = 1.0;
        WarmStar.Flare.ApertureBlades    = 8u;
        R.Suns.push_back(WarmStar);

        RealmSolarSource BlueStar{};
        BlueStar.DirectionUnit            = Norm3(0.26f, 0.30f, 0.92f);
        BlueStar.DiscIrradiance           = {0.65f, 0.92f, 1.45f};
        BlueStar.Disc.AngularRadiusRad    = 0.0042;
        BlueStar.MasterFlareGain          = 0.72f;
        BlueStar.Flare.HaloStrength       = 0.34;
        BlueStar.Flare.AnamorphicStrength = 0.90;
        BlueStar.Flare.ApertureBlades     = 5u;
        R.Suns.push_back(BlueStar);

        LunarBodyDescriptor EuropaMoon{};
        EuropaMoon.Preset            = LunarSurfacePreset::GlacierEuropa;
        EuropaMoon.DirectionUnit     = NormVec3(-0.04f, 0.48f, 0.88f);
        EuropaMoon.AngularRadiusRad  = 0.042f;
        EuropaMoon.SurfaceBrightness = 2.4f;
        R.Moons.push_back(EuropaMoon);

        LunarBodyDescriptor IoMoon{};
        IoMoon.Preset            = LunarSurfacePreset::SulfurIo;
        IoMoon.DirectionUnit     = NormVec3(0.42f, 0.42f, 0.80f);
        IoMoon.AngularRadiusRad  = 0.025f;
        IoMoon.SurfaceBrightness = 2.2f;
        R.Moons.push_back(IoMoon);

        PlanetaryBodyDescriptor IceGiant{};
        IceGiant.Archetype              = PlanetarySurfaceArchetype::MethaneIceGiant;
        IceGiant.DirectionUnit          = NormVec3(-0.46f, 0.36f, 0.81f);
        IceGiant.AngularRadiusRad       = 0.055f;
        IceGiant.Rings.Enabled          = true;
        IceGiant.Rings.InnerRadiusRatio = 1.45f;
        IceGiant.Rings.OuterRadiusRatio = 1.95f;
        IceGiant.Rings.PeakOpticalDepth = 0.9f;
        IceGiant.Rings.OuterTintRgb     = {0.72f, 0.88f, 0.98f};
        R.Planets.push_back(IceGiant);

        CloudVolumeProfile AlienClouds{};
        AlienClouds.Domain              = CloudVolumeDomain::PlanetarySlab;
        AlienClouds.Slab.BaseHeightM    = 1600.0;
        AlienClouds.Slab.TopHeightM     = 2100.0;
        AlienClouds.Slab.Coverage       = 0.44;
        AlienClouds.Slab.DensityScale   = 1.2;
        AlienClouds.Slab.ExtinctionPerM = 0.035;
        R.CloudVolumes.push_back(AlienClouds);

        R.HeightFog.DensityAtBase  = 0.0045;
        R.HeightFog.FalloffHeightM = 15.0;
        R.SolidOccluders.push_back({{0.0f, 1.6f, 14.0f}, 1.6f, {0.68f, 0.56f, 0.50f}, 0.4f});
    }

    // ============================================================================================================
    // CROSS-REALM PORTAL APERTURE: Connects Daytime (Realm 0) -> Starry Ringed-Planet Night (Realm 1)!
    // ============================================================================================================
    {
        PortalAperture Portal{};
        Portal.SourceRealmIndex   = 0u;
        Portal.TargetRealmIndex   = 1u;
        Portal.CenterMeters       = {0.4f, 2.35f, 8.2f};
        Portal.NormalUnit         = {0.0f, 0.0f, -1.0f};
        Portal.RightUnit          = {1.0f, 0.0f, 0.0f};
        Portal.UpUnit             = {0.0f, 1.0f, 0.0f};
        Portal.HalfWidthMeters    = 2.65f;
        Portal.HalfHeightMeters   = 2.15f;
        Portal.FrameWidthMeters   = 0.14f;
        Portal.FrameAlbedoRgb     = {0.20f, 0.22f, 0.26f};
        Portal.TargetOriginMeters = {0.0f, 2.35f, 2.0f};
        Portal.TargetRightUnit    = {1.0f, 0.0f, 0.0f};
        Portal.TargetUpUnit       = {0.0f, 1.0f, 0.0f};
        Portal.TargetForwardUnit  = {0.0f, 0.0f, 1.0f};
        World.Portals.push_back(Portal);
    }

    return World;
}

//------------------------------------------------------------------------------------------------------------------------
//                  3. GPU VULKAN 1.3 EXECUTION, READBACK VERIFICATION & EXHIBIT RENDERING
//------------------------------------------------------------------------------------------------------------------------

void VerifyGpuPipelineAndRenderExhibits(const std::filesystem::path& RepoRoot,
                                        const std::filesystem::path& ShaderDir,
                                        const std::filesystem::path& OutputDir,
                                        bool                         MirrorToTrackedShowcase)
{
    std::cout << "[4/4] Initializing Vulkan 1.3 Compute Substrate & Compiling 11 SPIR-V Pipelines...\n";
    VulkanComputeSubstrate Substrate{};
    std::string            Diag;
    REQUIRE_ENV(Substrate.Initialize("", &Diag), ("Vulkan initialization failed: " + Diag).c_str());
    std::cout << "      Active Vulkan Device: " << Substrate.DeviceName() << "\n";

    EnvironmentSubmissionSequence Sequence{};
    const auto                    JpgPath = (RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg").string();
    const auto                    CsvPath = (RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/Hipparcos8.csv").string();
    REQUIRE_ENV(Sequence.Initialize(Substrate, ShaderDir.string(), JpgPath, CsvPath, &Diag),
                ("EnvironmentSubmissionSequence::Initialize failed: " + Diag).c_str());
    std::cout << "      Uploaded 2048x1024x6 sRGB Lunar Atlas + " << Sequence.CelestialDome().IndexedStarCount()
              << " cell-indexed Hipparcos stars\n";
    REQUIRE_ENV(Sequence.CelestialDome().IndexedStarCount() > 30000u, "Hipparcos8.csv must index > 30,000 stars on GPU");

    // 3A. Verify Multi-Realm Dirty-Gated Atmosphere LUT Bakes + Numerical Readback
    EnvironmentRealmEnsemble World    = BuildThreeRealmPortalWorld();
    EnvironmentCameraView    ProbeCam = MakeLookAtCamera(0u, {0.0f, 2.2f, 0.0f}, {0.0f, 2.5f, 10.0f}, 320u, 180u);

    REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, World, ProbeCam), "Initial 3-realm GPU frame failed");
    REQUIRE_ENV(Sequence.AtmosphereLuts().LastAtmosphereBakedCount() == 3u &&
                    Sequence.AtmosphereLuts().LastSkyViewBakedCount() == 3u,
                "First frame must bake all 3 realm LUT slices");

    ProbeCam.FrameIndex = 1u;
    REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, World, ProbeCam), "Steady GPU frame failed");
    REQUIRE_ENV(Sequence.AtmosphereLuts().LastAtmosphereBakedCount() == 0u &&
                    Sequence.AtmosphereLuts().LastSkyViewBakedCount() == 0u,
                "Unchanged revisions must trigger zero AtmosphereLutSubmission dispatches");

    World.Realms[0].SkyViewRevision++;
    ProbeCam.FrameIndex = 2u;
    REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, World, ProbeCam), "SkyView-dirty GPU frame failed");
    REQUIRE_ENV(Sequence.AtmosphereLuts().LastAtmosphereBakedCount() == 0u &&
                    Sequence.AtmosphereLuts().LastSkyViewBakedCount() == 1u,
                "Per-realm SkyView dirty gate must rebake only the single modified realm slice");

    std::vector<float> GpuTrans0;
    REQUIRE_ENV(Sequence.AtmosphereLuts().ReadbackTransmittanceLayer(Substrate, 0u, GpuTrans0),
                "GPU Transmittance LUT readback failed");
    const size_t ZenithIdx  = (2u * kTransmittanceLutWidth + 12u) * 4u;
    const size_t HorizonIdx = (2u * kTransmittanceLutWidth + 240u) * 4u;
    REQUIRE_ENV(GpuTrans0[ZenithIdx + 1u] > GpuTrans0[HorizonIdx + 1u] &&
                    GpuTrans0[HorizonIdx + 0u] > GpuTrans0[HorizonIdx + 2u],
                "GPU Transmittance LUT must exhibit higher zenith transmittance and redder horizon optical path");

    std::vector<float> Shadow0, Shadow1;
    REQUIRE_ENV(Sequence.Clouds().ReadbackShadowLayer(Substrate, 0u, Shadow0) &&
                    Sequence.Clouds().ReadbackShadowLayer(Substrate, 1u, Shadow1),
                "GPU CloudShadowArray readback failed");
    float MinTr0 = 1.0f, MinTr1 = 1.0f;
    for (size_t I = 1u; I < Shadow0.size(); I += 4u)
    {
        MinTr0 = std::min(MinTr0, Shadow0[I]);
        MinTr1 = std::min(MinTr1, Shadow1[I]);
    }
    REQUIRE_ENV(MinTr0 < 0.75f && MinTr1 > 0.999f,
                "GPU CloudShadowArray must attenuate Realm 0 cloud shadows while keeping cloud-free Realm 1 at 1.0");

    // 3B. Verify GPU 1x1-Per-Source Optical Lens Flare Visibility Across 5 Physical Occlusion Scenarios!
    std::cout << "      Verifying GPU 1x1 OpticalLensFlare visibility (open sky / sphere / cloud / glass / portal)...\n";
    {
        EnvironmentRealmEnsemble FlareTest{};
        FlareTest.Realms.resize(2u);
        EnvironmentRealm& R  = FlareTest.Realms[0];
        R.ObserverAltitudeKm = 0.05f;
        RealmSolarSource Sun{};
        Sun.DirectionUnit   = Norm3(0.0f, 0.35f, 0.9367f);
        Sun.DiscIrradiance  = {1.0f, 1.0f, 1.0f};
        Sun.MasterFlareGain = 1.0f;
        R.Suns.push_back(Sun);

        EnvironmentCameraView             Cam = MakeLookAtCamera(0u, {0.0f, 2.0f, 0.0f}, {0.0f, 5.5f, 10.0f}, 320u, 180u);
        std::vector<std::array<float, 4>> Vis;

        // (1) Open sky
        REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Substrate, 1u, Vis),
                    "Open-sky flare visibility execution failed");
        const float OpenVis = Vis[0][3];
        REQUIRE_ENV(OpenVis > 0.92f, "Unoccluded solar disc must produce > 0.92 GPU flare visibility");

        // (2) Solid reversed-Z sphere directly along Sun.DirectionUnit
        R.SolidOccluders.push_back({{0.0f, 2.0f + 0.35f * 15.0f, 0.9367f * 15.0f}, 1.5f, {0.5f, 0.5f, 0.5f}, 0.5f});
        REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Substrate, 1u, Vis),
                    "Sphere-occluded flare visibility execution failed");
        REQUIRE_ENV(Vis[0][3] == 0.0f, "Reversed-Z solid sphere covering solar disc must yield 0.0 flare visibility");
        R.SolidOccluders.clear();

        // (3) Tinted dielectric glass pane ({0.28, 0.72, 0.96}) in front of the sun
        RealmGlassPane BlueGlass{};
        BlueGlass.CenterMeters      = {0.0f, 2.0f + 0.35f * 8.0f, 0.9367f * 8.0f};
        BlueGlass.NormalUnit        = {0.0f, 0.0f, -1.0f};
        BlueGlass.HalfWidthMeters   = 3.0f;
        BlueGlass.HalfHeightMeters  = 3.0f;
        BlueGlass.TintTransmittance = {0.28f, 0.72f, 0.96f};
        BlueGlass.FresnelF0         = 0.04f;
        R.GlassPanes.push_back(BlueGlass);
        REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Substrate, 1u, Vis),
                    "Glass-tinted flare visibility execution failed");
        REQUIRE_ENV(Vis[0][2] > Vis[0][1] && Vis[0][1] > Vis[0][0] && Vis[0][0] > 0.15f,
                    "Dielectric glass mask must spectrally tint GPU solar flare visibility");
        R.GlassPanes.clear();

        // (4) Volumetric cloud box partially attenuating the solar disc
        CloudVolumeProfile SunCloud{};
        SunCloud.Domain              = CloudVolumeDomain::BoundedLocalBox;
        SunCloud.BoxCenterM          = {0.0, 2.0 + 0.35 * 30.0, 0.9367 * 30.0};
        SunCloud.BoxHalfExtentsM     = {12.0, 12.0, 12.0};
        SunCloud.BoxEdgeFalloffM     = 2.0;
        SunCloud.Slab.Coverage       = 0.95;
        SunCloud.Slab.DensityScale   = 1.2;
        SunCloud.Slab.ExtinctionPerM = 0.06;
        R.CloudVolumes.push_back(SunCloud);
        Cam.FrameIndex = 0u;
        REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Substrate, 1u, Vis),
                    "Cloud-attenuated flare visibility execution failed");
        REQUIRE_ENV(Vis[0][3] > 0.05f && Vis[0][3] < OpenVis * 0.90f,
                    "Volumetric cloud transmittance must partially attenuate GPU solar flare visibility");
        R.CloudVolumes.clear();

        // (5) Cross-realm sun inside Portal Target Realm 1: visible when aligned with portal window, 0 when outside window!
        R.Suns.clear();
        FlareTest.Realms[1].Suns.push_back(Sun);
        PortalAperture Port{};
        Port.SourceRealmIndex   = 0u;
        Port.TargetRealmIndex   = 1u;
        Port.CenterMeters       = {0.0f, 2.0f + 0.35f * 8.0f, 0.9367f * 8.0f};
        Port.HalfWidthMeters    = 2.5f;
        Port.HalfHeightMeters   = 2.5f;
        Port.TargetOriginMeters = {0.0f, 2.0f, 0.0f};
        FlareTest.Portals.push_back(Port);

        REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Substrate, 1u, Vis),
                    "Portal-open flare visibility execution failed");
        const float PortalOpenVis = Vis[0][3];

        FlareTest.Portals[0].CenterMeters[0] = 6.5f;
        REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Substrate, 1u, Vis),
                    "Portal-clipped flare visibility execution failed");
        const float PortalClippedVis = Vis[0][3];
        REQUIRE_ENV(PortalOpenVis > 0.85f && PortalClippedVis == 0.0f,
                    "Cross-realm sun viewed through PortalAperture must clip cleanly against portal window bounds");
    }

    // 3C. Render 6 Showcase GPU Exhibits (800 x 450 crisp widescreen) & Verify Day/Night Portal Readback!
    constexpr uint32_t kExhibitW  = 800u;
    constexpr uint32_t kExhibitH  = 450u;
    const auto         TrackedDir = RepoRoot / "Exhibits/EnvironmentRealm/Images";

    auto SaveExhibit = [&](const char* FileName, const EnvironmentCameraView& Cam, const EnvironmentRealmEnsemble& Scene) {
        EnvironmentCameraView C = Cam;
        C.ViewportWidth         = kExhibitW;
        C.ViewportHeight        = kExhibitH;
        C.FrameIndex            = 0u;
        REQUIRE_ENV(Sequence.ExecuteFrame(Substrate, Scene, C), FileName);

        std::vector<float>   Hdr;
        std::vector<uint8_t> Srgb;
        REQUIRE_ENV(Sequence.ReadbackHdrFrame(Substrate, Hdr), "HDR frame readback failed");
        TonemapHdrToSrgb8(Hdr, kExhibitW, kExhibitH, C.ExposureScale, Srgb);

        REQUIRE_ENV(WritePngRgba(OutputDir / FileName, kExhibitW, kExhibitH, Srgb), "Failed writing PNG exhibit");
        if (MirrorToTrackedShowcase)
        {
            REQUIRE_ENV(WritePngRgba(TrackedDir / FileName, kExhibitW, kExhibitH, Srgb), "Failed mirroring PNG exhibit");
        }
        return Hdr;
    };

    std::cout << "      Rendering 6 GPU Vulkan Environment & Multi-Realm Portal Showcase Exhibits (800x450)...\n";

    // Exhibit 1: Flagship Simultaneous Day/Night Cross-Realm Portal Frame!
    {
        const auto Cam = MakeLookAtCamera(0u, {-0.6f, 2.15f, -0.5f}, {0.1f, 2.55f, 9.0f}, kExhibitW, kExhibitH, 62.0f, 1.55f);
        const auto Hdr = SaveExhibit("DayNightPortalEnsemble.png", Cam, World);

        std::vector<float> GbRealm;
        REQUIRE_ENV(Sequence.ReadbackGbufferLayer(Substrate, 1u, GbRealm), "Gbuffer Layer 1 readback failed");
        const size_t   LeftExteriorPx = (115u * kExhibitW + 75u) * 4u;
        const size_t   PortalCenterPx = (200u * kExhibitW + 435u) * 4u;
        const uint32_t ExteriorRealm  = static_cast<uint32_t>(GbRealm[LeftExteriorPx + 3u] + 0.5f);
        const uint32_t InteriorRealm  = static_cast<uint32_t>(GbRealm[PortalCenterPx + 3u] + 0.5f);
        REQUIRE_ENV(ExteriorRealm == 0u && InteriorRealm == 1u,
                    "DayNightPortalEnsemble must route exterior rays to Daytime Realm 0 and interior portal rays to Night Realm 1");
        REQUIRE_ENV(Hdr[LeftExteriorPx + 2u] > 0.08f, "Daytime exterior sky must carry scattered blue atmospheric radiance");
    }

    // Exhibit 2: Golden-Hour / Twilight Atmosphere & Full Optical Lens Flare
    {
        EnvironmentRealmEnsemble TwilightScene                       = World;
        TwilightScene.Portals.clear();
        TwilightScene.Realms[0].Suns[0].DirectionUnit                = Norm3(0.12f, 0.135f, 0.984f);
        TwilightScene.Realms[0].Suns[0].DiscIrradiance               = {1.52f, 0.92f, 0.56f};
        TwilightScene.Realms[0].Suns[0].MasterFlareGain              = 1.30f;
        TwilightScene.Realms[0].CloudVolumes[0].Slab.Coverage        = 0.34;
        TwilightScene.Realms[0].HeightFog.DensityAtBase              = 0.0008;
        TwilightScene.Realms[0].TwilightStrength                     = 1.5f;
        TwilightScene.Realms[0].SkyViewRevision++;
        const auto Cam = MakeLookAtCamera(0u, {0.0f, 2.1f, 0.0f}, {0.0f, 2.9f, 12.0f}, kExhibitW, kExhibitH, 60.0f, 1.55f);
        (void)SaveExhibit("TwilightAtmosphereAndLensFlare.png", Cam, TwilightScene);
    }

    // Exhibit 3: Celestial Dome Night Sky (2K 6-Body Moons + Ringed Gas Giant + Oceanic Exoplanet + Milky Way + Stars)
    {
        EnvironmentRealmEnsemble NightDome = World;
        NightDome.Realms[1].LocalFogVolumes.clear();
        NightDome.Realms[1].HeightFog.DensityAtBase = 0.0005;
        LunarBodyDescriptor Europa{};
        Europa.Preset            = LunarSurfacePreset::GlacierEuropa;
        Europa.DirectionUnit     = NormVec3(-0.46f, 0.44f, 0.77f);
        Europa.AngularRadiusRad  = 0.030f;
        Europa.SurfaceBrightness = 2.6f;
        NightDome.Realms[1].Moons.push_back(Europa);

        const auto Cam = MakeLookAtCamera(1u, {0.0f, 2.0f, 0.0f}, {-0.4f, 5.2f, 12.0f}, kExhibitW, kExhibitH, 58.0f, 3.2f);
        (void)SaveExhibit("CelestialDomeMoonsPlanetsStars.png", Cam, NightDome);
    }

    // Exhibit 4: Volumetric Clouds (Global Slab + Local 3D Box + Beers-Powder Silver Lining + Ortho Cloud Shadows)
    {
        EnvironmentRealmEnsemble CloudScene = World;
        CloudScene.Portals.clear();
        CloudScene.Realms[0].GlassPanes.clear();
        CloudScene.Realms[0].CloudVolumes[0].Slab.Coverage   = 0.62;
        CloudScene.Realms[0].CloudVolumes[1].BoxCenterM      = {2.0, 13.0, 34.0};
        CloudScene.Realms[0].CloudVolumes[1].BoxHalfExtentsM = {13.0, 5.5, 10.0};
        const auto Cam = MakeLookAtCamera(0u, {0.0f, 2.4f, -2.0f}, {-2.0f, 4.8f, 18.0f}, kExhibitW, kExhibitH, 62.0f, 1.65f);
        (void)SaveExhibit("VolumetricCloudsAndOrthoShadows.png", Cam, CloudScene);
    }

    // Exhibit 5: Coupled Volumetric Fog (IES Spotlight + Lightning Channel + Radiant Fire Facets)
    {
        const auto Cam = MakeLookAtCamera(1u, {0.0f, 2.2f, 2.5f}, {0.2f, 2.4f, 16.0f}, kExhibitW, kExhibitH, 60.0f, 2.6f);
        (void)SaveExhibit("VolumetricFogLuminairesAndShafts.png", Cam, World);
    }

    // Exhibit 6: Binary-Sun Alien Exoplanet Ensemble (2 Suns + Dual Lens Flares + Methane Ringed Giant + Europa & Io Moons)
    {
        const auto Cam = MakeLookAtCamera(2u, {0.0f, 2.1f, 0.0f}, {0.0f, 3.8f, 12.0f}, kExhibitW, kExhibitH, 62.0f, 1.65f);
        (void)SaveExhibit("BinarySunExoplanetEnsemble.png", Cam, World);
    }

    Sequence.Shutdown(Substrate);
    Substrate.Shutdown();
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path RepoRoot  = (Argc > 1) ? Argv[1] : ".";
    const std::filesystem::path ShaderDir = (Argc > 2) ? Argv[2] : "build/ShaderArchives/Environment";
    const std::filesystem::path OutputDir = (Argc > 3) ? Argv[3] : "build/environment-realm-exhibits";

    bool MirrorShowcase = false;
    for (int I = 1; I < Argc; ++I)
    {
        if (std::strcmp(Argv[I], "--write-showcase") == 0)
        {
            MirrorShowcase = true;
        }
    }

    VerifyPhysicalModelsAgainstOracles(RepoRoot);
    VerifyGpuPipelineAndRenderExhibits(RepoRoot, ShaderDir, OutputDir, MirrorShowcase);

    std::cout << "[EnvironmentRealmValidation] ALL CPU ORACLE & VULKAN 1.3 GPU PIPELINE CHECKS PASSED.\n";
    return 0;
}
