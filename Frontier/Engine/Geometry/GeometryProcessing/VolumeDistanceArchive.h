/*==========================================================================================================================================
                                                 VOLUME DISTANCE ARCHIVE
                                     ------------------------------------------------------------------
             Headless mesh-to-SDF baker (wrapping Engine/ContentInterchange/VolumeDistanceField's exact
             Voronoi triangle seeding + 3D Felzenszwalb-Huttenlocher separable EDT + 3-axis parity sign),
             compact IEEE-754 Float16 binary .sdfspace disk serialization, standalone project-content
             resolution, multi-instance world SDF union composition, and GPU float4 upload packing.
==========================================================================================================================================*/
// 🧩 Headless .facet -> .sdfspace baker, binary archive codec, world SDF composer, and GPU upload table builder.

#pragma once

#include "Engine/ContentInterchange/VolumeDistanceField.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

struct HeadlessSdfBakeOptions
{
    std::array<uint32_t, 3> Dimensions   = {72u, 40u, 96u};
    float                   PaddingRatio = 0.10f;
    bool                    SignedField  = true;
    float                   NarrowBandM  = 0.0f; // 0 = full-domain exact Euclidean distance
    bool                    SkipGlass    = true; // Exclude Class 7/12 glazing when baking opaque shadow/GI SDF
};

// Headlessly converts a FacetField into BakeMeshSet and invokes BakeVolumeDistanceField (zero UI dependency).
[[nodiscard]] VolumeDistanceField BakeFacetFieldDistanceVolume(const FacetField&             SourceFacets,
                                                               const HeadlessSdfBakeOptions& Options = {});

// Compact IEEE-754 Float16 binary .sdfspace container ("SDF1" 64-byte header + N_x*N_y*N_z uint16_t payload).
[[nodiscard]] bool SaveVolumeDistanceSpace(const VolumeDistanceField&   Field,
                                           const std::filesystem::path& OutputPath,
                                           std::string*                 OutDiagnostic = nullptr);

[[nodiscard]] bool LoadVolumeDistanceSpaceFromMemory(std::span<const uint8_t> Bytes,
                                                     VolumeDistanceField&     OutField,
                                                     std::string*             OutDiagnostic = nullptr);

// Resolves direct filesystem paths, staged <Project>/Content/<Rel>, or repository EngineContent/<Rel>.
[[nodiscard]] bool LoadVolumeDistanceSpace(const std::string&   PathOrContentRel,
                                           VolumeDistanceField& OutField,
                                           std::string*         OutDiagnostic = nullptr);

struct PlacedDistanceInstance
{
    const VolumeDistanceField* Archive           = nullptr;
    FacetPoint3D               TranslationMeters = {0.0, 0.0, 0.0};
    double                     YawRadians        = 0.0;
    double                     UniformScale      = 1.0;
};

// Evaluates trilinearly interpolated signed distance [m] in local archive coordinates (with exterior box shell growth).
[[nodiscard]] float SampleVolumeDistanceMeters(const VolumeDistanceField& Field,
                                               float                      WorldX,
                                               float                      WorldY,
                                               float                      WorldZ) noexcept;

// Evaluates normalized SDF gradient normal via 4-tap tetrahedron differences.
[[nodiscard]] std::array<float, 3> SampleVolumeGradientNormal(const VolumeDistanceField& Field,
                                                              float                      WorldX,
                                                              float                      WorldY,
                                                              float                      WorldZ) noexcept;

// Fast sphere-traced solar cone visibility in [0..1] over a VolumeDistanceField.
[[nodiscard]] float TraceVolumeConeVisibility(const VolumeDistanceField& Field,
                                              std::array<float, 3>       OriginMeters,
                                              std::array<float, 3>       DirectionUnit,
                                              float                      MaxDistanceMeters = 12.0f,
                                              float                      ConeSharpness     = 14.0f,
                                              uint32_t                   MaxSteps          = 16u) noexcept;

// Unions pre-baked part .sdfspace archives (e.g. ControlVehicleBody + 4x Rim + 4x Tyre) into a single world SDF grid
// in milliseconds without running runtime triangle EDT baking.
[[nodiscard]] VolumeDistanceField ComposeWorldDistanceVolume(std::span<const PlacedDistanceInstance> Instances,
                                                             std::array<float, 3>                    WorldBoundsMin,
                                                             std::array<float, 3>                    WorldBoundsMax,
                                                             std::array<uint32_t, 3>                 Dimensions,
                                                             bool                                    IncludeGroundHalfSpace = false,
                                                             float                                   GroundPlaneY           = 0.0f);

// Loads pre-baked ControlVehicle{Body,Rim,Tyre}.sdfspace archives and composes the complete vehicle world SDF.
[[nodiscard]] bool ComposeControlVehicleDistanceVolume(VolumeDistanceField&           OutWorldField,
                                                       const ControlVehiclePlacement& Placement         = {},
                                                       std::array<uint32_t, 3>        WorldDimensions   = {80u, 40u, 96u},
                                                       bool                           IncludeGroundHalf = false,
                                                       std::string*                   OutDiagnostic     = nullptr);

// Packs a VolumeDistanceField into a GPU StructuredBuffer<float4> upload array:
//   Slot 0: {MinX, MinY, MinZ, float(DimX)}
//   Slot 1: {MaxX, MaxY, MaxZ, float(DimY)}
//   Slot 2: {StepX, StepY, StepZ, float(DimZ)}
//   Slot 3: {InvSpanX, InvSpanY, InvSpanZ, ValidFlag (1.0 = active, 0.0 = empty)}
//   Slots 4..N: 4 consecutive fp32 voxel distances per float4.
void PackVolumeDistanceForGpu(const VolumeDistanceField&          Field,
                              std::vector<std::array<float, 4>>& OutGpuFloat4Table);

//------------------------------------------------------------------------------------------------------------------------
//                           FOUR-CASCADE CLIPMAP DISTANCE VOLUME & ANALYTICAL PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

enum class AnalyticalDistanceCategory : uint8_t
{
    Box      = 0,
    Sphere   = 1,
    Cylinder = 2,
};

struct AnalyticalDistancePrimitive
{
    AnalyticalDistanceCategory Category          = AnalyticalDistanceCategory::Box;
    std::array<float, 3>       CenterMeters      = {0.0f, 0.0f, 0.0f};
    std::array<float, 3>       HalfExtentsMeters = {0.5f, 0.5f, 0.5f}; // Sphere uses X = radius; Cylinder uses X = radius, Y = halfHeight
};

struct FourCascadeDistanceParameters
{
    std::array<float, 3>    FocusCenterMeters         = {0.0f, 0.8f, 0.0f};
    std::array<float, 4>    CascadeRadiiMeters        = {8.0f, 24.0f, 72.0f, 220.0f};
    std::array<uint32_t, 3> VoxelResolutionPerCascade = {32u, 24u, 32u};
    bool                    TrilinearCascadeBlend     = true;
    bool                    AnalyticalPrimitives      = true;
    bool                    IncludeGroundHalfSpace    = true;
    float                   GroundPlaneY              = 0.0f;
    uint32_t                MaxConeSteps              = 28u;
    float                   ConeTraceStepScale        = 0.92f;
};

struct FourCascadeDistanceVolume
{
    std::array<VolumeDistanceField, 4>       Cascades{};
    std::vector<AnalyticalDistancePrimitive> Primitives{};
    FourCascadeDistanceParameters            Parameters{};
};

// Evaluates exact signed distance [m] to an analytical primitive (Box, Sphere, or vertical Cylinder).
[[nodiscard]] float EvaluateAnalyticalPrimitiveDistance(const AnalyticalDistancePrimitive& Primitive,
                                                        float                              WorldX,
                                                        float                              WorldY,
                                                        float                              WorldZ) noexcept;

// Assembles 4 concentric clipmap SDF cascades around FocusCenterMeters from pre-baked archives + analytical primitives.
[[nodiscard]] bool AssembleFourCascadeDistanceVolume(FourCascadeDistanceVolume&                        OutCascadedVolume,
                                                     const ControlVehiclePlacement&                    VehiclePlacement,
                                                     std::span<const AnalyticalDistancePrimitive>      ScenePrimitives,
                                                     const FourCascadeDistanceParameters&              Parameters    = {},
                                                     std::string*                                      OutDiagnostic = nullptr);

// Evaluates seam-blended 4-cascade + analytical signed distance [m] at an arbitrary world coordinate.
[[nodiscard]] float SampleFourCascadeDistanceMeters(const FourCascadeDistanceVolume& Volume,
                                                    float                            WorldX,
                                                    float                            WorldY,
                                                    float                            WorldZ) noexcept;

// Packs a FourCascadeDistanceVolume into a self-describing GPU StructuredBuffer<float4> (Slot 3.w == 4.0f).
void PackFourCascadeDistanceForGpu(const FourCascadeDistanceVolume&    Volume,
                                   std::vector<std::array<float, 4>>& OutGpuFloat4Table);

} // namespace Frontier
