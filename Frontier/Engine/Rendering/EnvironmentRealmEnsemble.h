/*==========================================================================================================================================
                                                ENVIRONMENT REALM ENSEMBLE
                                     ------------------------------------------------------------------
             Multi-instance, portal-ready world coordinator supporting arbitrary planets, suns, moons,
                  atmospheres, global/local volumetric clouds, optical lens flares, and fog media.
==========================================================================================================================================*/
// 🧩 Zero-singleton multi-realm environment state + packed 16-byte float4 GPU storage buffer serialization.

#pragma once

#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CelestialDomeModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/FogMedium.h"
#include "Engine/Rendering/LensFlareModel.h"
#include "Engine/Rendering/SolarSkyEnvironment.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                      SCENE OCCLUDERS & DIELECTRIC GLASS PANES
//------------------------------------------------------------------------------------------------------------------------

struct RealmSphereObstacle
{
    std::array<float, 3> CenterMeters = {0.0f, 1.5f, 12.0f};
    float                RadiusMeters = 1.5f;
    std::array<float, 3> AlbedoRgb    = {0.72f, 0.70f, 0.66f};
    float                Roughness    = 0.45f;
};

struct RealmGlassPane
{
    std::array<float, 3> CenterMeters      = {0.0f, 2.0f, 6.0f};
    std::array<float, 3> NormalUnit        = {0.0f, 0.0f, -1.0f};
    float                HalfWidthMeters   = 1.5f;
    float                HalfHeightMeters  = 1.5f;
    std::array<float, 3> TintTransmittance = {0.82f, 0.91f, 0.95f};
    float                FresnelF0         = 0.04f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           PER-SUN COUPLED OPTICS DESCRIPTOR
//------------------------------------------------------------------------------------------------------------------------

struct RealmSolarSource
{
    std::array<float, 3> DirectionUnit      = {0.0f, 0.70710678f, 0.70710678f};
    std::array<float, 3> DiscIrradiance     = {1.0f, 0.98f, 0.95f};
    SunDiscProfile       Disc{};
    FlareOpticsProfile   Flare{};
    float                MasterFlareGain    = 1.0f;
    float                ChromaticShift     = 0.006f;
    float                HaloRadiusNdc      = 0.34f;
    float                HaloWidthNdc       = 0.032f;
    float                ApertureRoundness  = 0.25f;
    bool                 EnableLensFlare    = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                        INDEPENDENT ENVIRONMENT REALM INSTANCE
//------------------------------------------------------------------------------------------------------------------------

struct EnvironmentRealm
{
    // Planet & atmosphere configuration (each realm can represent an entirely distinct planet/world)
    AtmosphereParameters Atmosphere          = MakeEarthAtmosphere();
    float                ObserverAltitudeKm  = 0.05f;
    float                GroundPlaneY        = 0.0f;
    std::array<float, 3> GroundAlbedoRgb     = {0.18f, 0.17f, 0.15f};
    float                TwilightStrength    = 1.0f;
    uint64_t             AtmosphereRevision  = 1u;
    uint64_t             SkyViewRevision     = 1u;

    // Arbitrary N suns, K moons (6-body 2K atlas), P ringed/atmospheric planets, and custom/Hipparcos stars
    std::vector<RealmSolarSource>        Suns{};
    std::vector<LunarBodyDescriptor>     Moons{};
    std::vector<PlanetaryBodyDescriptor> Planets{};
    std::vector<CustomStarDescriptor>    CustomStars{};

    float                StarBrightnessScale     = 1.0f;
    float                StarLimitingMagnitude   = 6.5f;
    float                ScintillationStrength   = 1.0f;
    float                MilkyWayIntensity       = 1.0f;
    std::array<float, 3> StarBasisRow0           = {1.0f, 0.0f, 0.0f};
    std::array<float, 3> StarBasisRow1           = {0.0f, 1.0f, 0.0f};

    // Arbitrary C global planetary slabs + bounded local 3D cloud volumes
    std::vector<CloudVolumeProfile> CloudVolumes{};
    float                           CloudShadowHalfSpanM = 3500.0f;
    float                           CloudWindSeconds     = 0.0f;

    // Analytic exponential height fog + aerial perspective + bounded 3D volumetric fog & local light sources
    FogParameters                        HeightFog{};
    float                                HeightFogScatterAlbedo = 0.88f;
    float                                HeightFogPhaseG        = 0.55f;
    bool                                 EnableAerialPerspective = true;
    std::vector<LocalFogVolumeProfile>   LocalFogVolumes{};
    std::vector<PhotometricFogLuminaire> Luminaires{};
    std::vector<LightningFogChannel>     LightningChannels{};
    std::vector<RadiantFogFacet>         RadiantFacets{};

    // Per-realm geometric occluders & dielectric glass panes
    std::vector<RealmSphereObstacle> SolidOccluders{};
    std::vector<RealmGlassPane>      GlassPanes{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                      CROSS-REALM PORTAL APERTURE DESCRIPTOR
//------------------------------------------------------------------------------------------------------------------------

struct PortalAperture
{
    uint32_t             SourceRealmIndex   = 0u;
    uint32_t             TargetRealmIndex   = 1u;
    std::array<float, 3> CenterMeters       = {0.0f, 2.2f, 9.0f};
    std::array<float, 3> NormalUnit         = {0.0f, 0.0f, -1.0f}; // Faces viewer in SourceRealm
    std::array<float, 3> RightUnit          = {1.0f, 0.0f, 0.0f};
    std::array<float, 3> UpUnit             = {0.0f, 1.0f, 0.0f};
    float                HalfWidthMeters    = 2.2f;
    float                HalfHeightMeters   = 2.0f;
    float                FrameWidthMeters   = 0.14f;
    std::array<float, 3> FrameAlbedoRgb     = {0.16f, 0.17f, 0.20f};

    // Target realm camera/ray frame corresponding to the portal plane
    std::array<float, 3> TargetOriginMeters = {0.0f, 2.2f, 0.0f};
    std::array<float, 3> TargetRightUnit    = {1.0f, 0.0f, 0.0f};
    std::array<float, 3> TargetUpUnit       = {0.0f, 1.0f, 0.0f};
    std::array<float, 3> TargetForwardUnit  = {0.0f, 0.0f, 1.0f};
};

//------------------------------------------------------------------------------------------------------------------------
//                                            CAMERA VIEWPORT CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct EnvironmentCameraView
{
    uint32_t             ActiveRealmIndex = 0u;
    std::array<float, 3> PositionMeters   = {0.0f, 2.0f, 0.0f};
    std::array<float, 3> RightUnit        = {1.0f, 0.0f, 0.0f};
    std::array<float, 3> UpUnit           = {0.0f, 1.0f, 0.0f};
    std::array<float, 3> ForwardUnit      = {0.0f, 0.0f, 1.0f};
    float                VerticalFovRad   = 1.04719755f; // 60 deg
    uint32_t             ViewportWidth    = 640u;
    uint32_t             ViewportHeight   = 360u;
    float                ElapsedSeconds   = 0.0f;
    float                ExposureScale    = 1.0f;
    uint32_t             FrameIndex       = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                      GPU FLOAT4 TABLE LAYOUT CONSTANTS & PAYLOAD
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kEnsembleHeaderFloat4Count       = 10u;
inline constexpr uint32_t kRealmRecordFloat4Stride         = 16u;
inline constexpr uint32_t kSunRecordFloat4Stride           = 6u;
inline constexpr uint32_t kMoonRecordFloat4Stride          = 4u;
inline constexpr uint32_t kPlanetRecordFloat4Stride        = 6u;
inline constexpr uint32_t kCloudRecordFloat4Stride         = 5u;
inline constexpr uint32_t kFogBoxRecordFloat4Stride        = 3u;
inline constexpr uint32_t kLuminaireRecordFloat4Stride     = 3u;
inline constexpr uint32_t kLightningRecordFloat4Stride     = 3u;
inline constexpr uint32_t kFacetRecordFloat4Stride         = 3u;
inline constexpr uint32_t kSphereRecordFloat4Stride        = 2u;
inline constexpr uint32_t kGlassRecordFloat4Stride         = 3u;
inline constexpr uint32_t kPortalRecordFloat4Stride        = 9u;
inline constexpr uint32_t kFlareSourceRecordFloat4Stride   = 4u;
inline constexpr uint32_t kCustomStarRecordFloat4Stride    = 2u;

struct ActiveFlareCandidate
{
    uint32_t             RealmIndex        = 0u;
    uint32_t             SunIndexInRealm   = 0u;
    int32_t              ViaPortalIndex    = -1; // -1 = direct in camera realm; >= 0 = viewed through portal
    uint32_t             GlobalSunSlot     = 0u;
    std::array<float, 3> CameraWorldDir    = {0.0f, 1.0f, 0.0f};
    std::array<float, 2> ScreenUv          = {0.5f, 0.5f};
    RealmSolarSource     SourceCopy{};
};

struct PackedEnsembleGpuPayload
{
    std::vector<std::array<float, 4>> Float4Slots{};
    std::vector<ActiveFlareCandidate> FlareCandidates{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                      MULTI-REALM ENSEMBLE CONTAINER & ROUTING
//------------------------------------------------------------------------------------------------------------------------

struct EnvironmentRealmEnsemble
{
    std::vector<EnvironmentRealm> Realms{};
    std::vector<PortalAperture>   Portals{};
};

struct RoutedRealmRay
{
    uint32_t             RealmIndex       = 0u;
    int32_t              PortalIndex      = -1;
    bool                 HitPortalFrame   = false;
    std::array<float, 3> FrameAlbedoRgb   = {0.0f, 0.0f, 0.0f};
    float                PortalHitMeters  = 0.0f;
    std::array<float, 3> RayOriginMeters  = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> RayDirectionUnit = {0.0f, 0.0f, 1.0f};
};

// Routes a camera ray against any portals in ActiveRealmIndex before solid geometry intersection.
[[nodiscard]] RoutedRealmRay RouteRayAcrossPortals(const EnvironmentRealmEnsemble& Ensemble,
                                                   uint32_t                        StartRealmIndex,
                                                   std::array<float, 3>            RayOriginMeters,
                                                   std::array<float, 3>            RayDirectionUnit) noexcept;

// Projects a camera-world direction into normalized screen UV [0, 1]^2; returns false if behind camera.
[[nodiscard]] bool ProjectDirectionToScreenUv(const EnvironmentCameraView& View,
                                              std::array<float, 3>         WorldDirectionUnit,
                                              std::array<float, 2>&        OutUv) noexcept;

// Packs all realms, suns, moons, planets, clouds, fog volumes, portals, and active flare candidates into a GPU float4 SSBO.
[[nodiscard]] PackedEnsembleGpuPayload PackEnsembleGpuPayload(const EnvironmentRealmEnsemble& Ensemble,
                                                              const EnvironmentCameraView&    View,
                                                              uint32_t                        HipparcosStarCount = 0u);

} // namespace Frontier
