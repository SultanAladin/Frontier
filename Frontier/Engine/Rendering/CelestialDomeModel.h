/*==========================================================================================================================================
                                                 CELESTIAL DOME MODEL
                                     ------------------------------------------------------------------
                 41k Hipparcos octahedral star index + Kolmogorov scintillation + Milky Way,
                 2K 6-body lunar sphere atlas (phase, earthshine, libration), and 3D ringed
                 planetary bodies with simulated spherical atmospheres & mutual shadows.
==========================================================================================================================================*/
// 🧩 Multi-sun, multi-body celestial dome model for both CPU reference and GPU Vulkan Slang submission.

#pragma once

#include "Engine/Foundation/Math.h"
#include "Engine/Rendering/CelestialModel.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                        SOLAR ILLUMINATION FOR CELESTIAL BODIES
//------------------------------------------------------------------------------------------------------------------------

struct CelestialSolarSource
{
    Vec3  DirectionUnit = {0.32f, 0.78f, 0.54f};   // [unit] local East (+X), North (+Y), Up (+Z)
    Vec3  IrradianceRgb = {18.5f, 18.0f, 17.2f};   // [W/m^2] exoatmospheric solar irradiance
    float AngularRadius = 0.0046524f;              // [rad] 0.2666 deg mean solar half-angle
};

//------------------------------------------------------------------------------------------------------------------------
//                                        OCTAHEDRAL STAR CATALOGUE & CUSTOM STARS
//------------------------------------------------------------------------------------------------------------------------

struct BinnedCatalogueStar
{
    Vec3  DirectionUnit{};     // [unit] East (+X), North (+Y), Up (+Z)
    Vec3  ChromaticityRgb{};   // [-] Ballesteros (2012) B-V Planckian chromaticity
    float RelativeFlux = 0.0f; // [-] 10^(-0.4 * Magnitude)
    float Magnitude    = 6.0f;
};

struct StarCellRange
{
    uint32_t First = 0u;
    uint32_t Count = 0u;
};

struct CustomStarDescriptor
{
    Vec3  DirectionUnit       = {0.22f, 0.72f, 0.66f};
    float AngularRadiusRad    = 0.0022f;
    float TemperatureKelvin   = 9200.0f;
    float RadianceGain        = 18.0f;
    float CoronaGlowStrength  = 0.85f;
    float SceneIrradianceGain = 0.35f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                         2K 6-BODY LUNAR SPHERE ATLAS & MOONS
//------------------------------------------------------------------------------------------------------------------------

enum class LunarSurfacePreset : uint8_t
{
    LunaSelenographic = 0u,   // Real 2048x1024 MoonAlbedoSource2048.jpg selenographic reflectance
    EmberVolcanic     = 1u,   // Basaltic mare + incandescent volcanic fissure network
    GlacierEuropa     = 2u,   // High-albedo fractured water-ice crust & amber lineae
    SulfurIo          = 3u,   // Sulfur-silicate volcanic plains & calderas
    ShroudTitan       = 4u,   // Dense orange-gold photochemical nitrogen-methane shroud
    ShardCaptured     = 5u    // Dark high-contrast cratered asteroid moonlet
};

inline constexpr uint32_t LunarAtlasLayerCount = 6u;

class LunarSixBodyAtlas
{
  public:
    bool BuildFromLunaSource(const std::string& LunaJpegOrPpmPath,
                             uint32_t           Width  = 2048u,
                             uint32_t           Height = 1024u);

    [[nodiscard]] bool                  Ready() const noexcept { return Width_ > 0u && Height_ > 0u && Layers_[0].Loaded(); }
    [[nodiscard]] uint32_t              Width() const noexcept { return Width_; }
    [[nodiscard]] uint32_t              Height() const noexcept { return Height_; }
    [[nodiscard]] const LunarAlbedoMap& LayerMap(LunarSurfacePreset Preset) const noexcept;
    [[nodiscard]] const std::vector<uint8_t>& Rgba8SrgbBytes() const noexcept { return Rgba8Srgb_; }

  private:
    uint32_t                                      Width_  = 0u;
    uint32_t                                      Height_ = 0u;
    std::array<LunarAlbedoMap, LunarAtlasLayerCount> Layers_{};
    std::vector<uint8_t>                          Rgba8Srgb_{};
};

struct LunarBodyDescriptor
{
    LunarSurfacePreset Preset                = LunarSurfacePreset::LunaSelenographic;
    Vec3               DirectionUnit         = {-0.34f, 0.76f, 0.55f};
    float              AngularRadiusRad      = 0.016f;   // [rad] 0.0046 physical Luna, larger for telephoto/exomoon
    float              SurfaceBrightness     = 2.6f;
    float              EarthshineStrength    = 0.045f;
    float              AtmosphericHaze       = 0.04f;
    float              AxialTiltRad          = 0.117f;   // [rad] 6.7 deg for Luna
    float              SpinLongitudeRad      = 0.0f;
    float              LibrationLatitudeRad  = 0.0f;
    float              SceneBounceIrradiance = 0.28f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                  SIMULATED PLANETARY ATMOSPHERE & RINGED PLANETS
//------------------------------------------------------------------------------------------------------------------------

struct PlanetaryAtmosphereProfile
{
    bool  Enabled                = true;
    float ShellHeightFraction    = 0.095f;
    Vec3  RayleighScattering     = {5.8e-6f, 13.5e-6f, 33.1e-6f};
    float RayleighScaleFraction  = 0.016f;
    float MieScattering          = 18.0e-6f;
    float MieScaleFraction       = 0.0045f;
    float MieAnisotropyG         = 0.76f;
    Vec3  AbsorptionCoefficients = {0.65e-6f, 1.88e-6f, 0.12e-6f};
    float OpticalDensityScale    = 5.2e5f;
};

struct PlanetaryRingDescriptor
{
    bool  Enabled            = true;
    Vec3  RingNormalUnit     = {0.18f, -0.32f, 0.93f};
    float InnerRadiusRatio   = 1.24f;
    float OuterRadiusRatio   = 2.38f;
    Vec3  InnerTintRgb       = {0.58f, 0.52f, 0.45f};
    Vec3  OuterTintRgb       = {0.86f, 0.82f, 0.74f};
    float PeakOpticalDepth   = 1.65f;
    float IcePhaseAsymmetryG = 0.38f;
};

enum class PlanetarySurfaceArchetype : uint8_t
{
    BandedGasGiant     = 0u,
    OceanicTerrestrial = 1u,
    RustyDesertWorld   = 2u,
    MethaneIceGiant    = 3u
};

struct PlanetaryBodyDescriptor
{
    PlanetarySurfaceArchetype  Archetype          = PlanetarySurfaceArchetype::BandedGasGiant;
    Vec3                       DirectionUnit      = {0.28f, 0.82f, 0.50f};
    float                      AngularRadiusRad   = 0.072f;
    Vec3                       SpinAxisUnit       = {0.18f, -0.32f, 0.93f};
    float                      SpinPhaseRad       = 0.65f;
    float                      SurfaceAlbedoScale = 1.35f;
    PlanetaryAtmosphereProfile Atmosphere{};
    PlanetaryRingDescriptor    Rings{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                            CELESTIAL EVALUATION DIAGNOSTICS
//------------------------------------------------------------------------------------------------------------------------

struct CelestialRaySample
{
    Vec3  RadianceRgb{};
    float OcclusionTransmittance = 1.0f;
    bool  HitLunarDisc           = false;
    bool  HitPlanetBody          = false;
    bool  HitPlanetAtmosphere    = false;
    bool  HitPlanetaryRings      = false;
    bool  InPlanetShadowOnRings  = false;
    bool  InRingShadowOnPlanet   = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   CELESTIAL DOME MODEL
//------------------------------------------------------------------------------------------------------------------------

class CelestialDomeModel
{
  public:
    static constexpr uint32_t OctahedralGridAxis  = 32u;
    static constexpr uint32_t OctahedralCellCount = OctahedralGridAxis * OctahedralGridAxis;

    CelestialDomeModel();

    void ClearBodies() noexcept;

    bool LoadHipparcosCatalogueCsv(const std::string& CsvPath,
                                   double             JulianUt1    = 2461343.85,
                                   double             LatitudeDeg  = -26.188,
                                   double             LongitudeDeg = 28.320);

    void BuildProceduralStarCatalogue(uint32_t StarCount = 8000u, uint64_t Seed = 0x51A7CA7A109FULL);

    bool LoadLunarAtlas(const std::string& LunaJpegOrPpmPath,
                        uint32_t           AtlasWidth  = 2048u,
                        uint32_t           AtlasHeight = 1024u);

    void RegisterCustomStar(const CustomStarDescriptor& Star);
    void RegisterLunarBody(const LunarBodyDescriptor& Moon);
    void RegisterPlanetaryBody(const PlanetaryBodyDescriptor& Planet);

    void ConfigureStarfieldAppearance(float StarBrightnessGain,
                                      float MagnitudeLimit,
                                      float MilkyWayBandStrength,
                                      float ScintillationAmount,
                                      float TimeSeconds,
                                      float ObserverAltitudeKm = 0.05f) noexcept;

    // Multi-sun celestial ray evaluation (stars, Milky Way, custom stars, 6-body moons, ringed planets + atmospheres).
    [[nodiscard]] CelestialRaySample EvaluateCelestialRay(
        Vec3                                   DirectionUnit,
        std::span<const CelestialSolarSource>  SolarSources,
        float                                  PixelSpanRad = 0.0018f) const noexcept;

    // Convenience single-sun overload
    [[nodiscard]] CelestialRaySample EvaluateCelestialRaySingleSun(
        Vec3  DirectionUnit,
        Vec3  PrimarySunDirectionUnit,
        Vec3  PrimarySunRadianceRgb,
        float SolarElevationDegrees,
        float PixelSpanRad = 0.0018f) const noexcept;

    [[nodiscard]] Vec3 EvaluateNocturnalDirectIrradiance(
        Vec3                                  SurfaceNormalUnit,
        std::span<const CelestialSolarSource> SolarSources) const noexcept;

    [[nodiscard]] uint32_t SourceStarCount() const noexcept { return SourceStarCount_; }
    [[nodiscard]] uint32_t BinnedStarEntryCount() const noexcept { return static_cast<uint32_t>(BinnedStars_.size()); }
    [[nodiscard]] uint32_t NonEmptyCellCount() const noexcept;

    [[nodiscard]] const std::array<StarCellRange, OctahedralCellCount>& StarCells() const noexcept { return StarCells_; }
    [[nodiscard]] const std::vector<BinnedCatalogueStar>&               BinnedStars() const noexcept { return BinnedStars_; }
    [[nodiscard]] const LunarSixBodyAtlas&                              LunarAtlas() const noexcept { return LunarAtlas_; }
    [[nodiscard]] const std::vector<CustomStarDescriptor>&              CustomStars() const noexcept { return CustomStars_; }
    [[nodiscard]] const std::vector<LunarBodyDescriptor>&               Moons() const noexcept { return Moons_; }
    [[nodiscard]] const std::vector<PlanetaryBodyDescriptor>&           Planets() const noexcept { return Planets_; }

    [[nodiscard]] static uint32_t OctahedralCellForDirection(Vec3 DirectionUnit) noexcept;
    [[nodiscard]] static Vec3     KelvinToBlackbodyChromaticity(float Kelvin) noexcept;

  private:
    void RebuildOctahedralStarIndex(const std::vector<StarRecord>& Records);

    [[nodiscard]] Vec3 EvaluateCatalogueStarsAndMilkyWay(Vec3  DirectionUnit,
                                                         float NightVisibilityWeight,
                                                         float PixelSpanRad) const noexcept;

    [[nodiscard]] Vec3 EvaluateCustomStarsAlongRay(Vec3 DirectionUnit, float PixelSpanRad) const noexcept;

    [[nodiscard]] CelestialRaySample EvaluateSinglePlanetAndRings(
        const PlanetaryBodyDescriptor&        Planet,
        Vec3                                  RayDirUnit,
        std::span<const CelestialSolarSource> SolarSources,
        float                                 PixelSpanRad) const noexcept;

    [[nodiscard]] CelestialRaySample EvaluateSingleMoon(
        const LunarBodyDescriptor&            Moon,
        Vec3                                  RayDirUnit,
        std::span<const CelestialSolarSource> SolarSources,
        float                                 PixelSpanRad) const noexcept;

    uint32_t                                       SourceStarCount_ = 0u;
    std::array<StarCellRange, OctahedralCellCount> StarCells_{};
    std::vector<BinnedCatalogueStar>               BinnedStars_;
    LunarSixBodyAtlas                              LunarAtlas_;

    std::vector<CustomStarDescriptor>              CustomStars_;
    std::vector<LunarBodyDescriptor>               Moons_;
    std::vector<PlanetaryBodyDescriptor>           Planets_;

    float StarBrightnessGain_   = 1.35f;
    float MagnitudeLimit_       = 6.8f;
    float MilkyWayBandStrength_ = 1.0f;
    float ScintillationAmount_  = 0.35f;
    float TimeSeconds_          = 0.0f;
    float ObserverAltitudeKm_   = 0.05f;
};

} // namespace Frontier
