/*==========================================================================================================================================
                                                 BOUNDING VOLUME ATLAS
                                     ------------------------------------------------------------------
             Two-level instanced software ray-acceleration architecture (binary SAH instance TLAS +
             quantized 8-wide compressed bounding-volume BLAS with octahedral crease-smoothed vertex
             normals, exact unpolarized Fresnel / total-internal-reflection curved dielectric refraction,
             Beer-Lambert glass shadow transmittance, and flux-weighted emissive facet CDF construction)
             targeting GTX-first Vulkan compute execution without hardware RTX ray-query dependencies.
==========================================================================================================================================*/
// 🧩 16-bin SAH & 8-wide quantized CWBVH builder, two-level instanced TLAS/BLAS ray oracle, dielectric optics, & emissive CDF.

#pragma once

#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                              PRIMITIVE & CELL STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EnclosureCell
{
    std::array<float, 3> LowerBounds  = {0.0f, 0.0f, 0.0f}; // [m]
    uint32_t             LeftOrFirst  = 0u;                 // Interior: LeftCell index; Leaf: FirstTriangle index
    std::array<float, 3> UpperBounds  = {0.0f, 0.0f, 0.0f}; // [m]
    uint32_t             RightOrCount = 0u;                 // Interior: RightCell | 0x80000000u; Leaf: TriangleCount
};

// 80-byte (5 × float4) quantized 8-wide compressed bounding-volume cell (Ylitie-Karras-Laine conservative power-of-two grid).
struct CompressedWideCell
{
    std::array<float, 3> ParentOrigin        = {0.0f, 0.0f, 0.0f}; // [m] - Quantization origin P
    uint32_t             PackedExponentsMask = 0u;                 // byte0=expX+127, byte1=expY+127, byte2=expZ+127, byte3=interiorMask
    uint32_t             ChildBaseIndex      = 0u;                 // First interior wide-cell child index
    uint32_t             PrimitiveBaseIndex  = 0u;                 // First leaf triangle index in WideTriangles
    uint32_t             ChildMetaLow        = 0u;                 // Children 0..3 metadata bytes
    uint32_t             ChildMetaHigh       = 0u;                 // Children 4..7 metadata bytes
    uint32_t             QuantMinXLow        = 0u;                 // Children 0..3 quantized min X [0..255]
    uint32_t             QuantMinXHigh       = 0u;                 // Children 4..7 quantized min X [0..255]
    uint32_t             QuantMaxXLow        = 0u;                 // Children 0..3 quantized max X [0..255]
    uint32_t             QuantMaxXHigh       = 0u;                 // Children 4..7 quantized max X [0..255]
    uint32_t             QuantMinYLow        = 0u;                 // Children 0..3 quantized min Y [0..255]
    uint32_t             QuantMinYHigh       = 0u;                 // Children 4..7 quantized min Y [0..255]
    uint32_t             QuantMaxYLow        = 0u;                 // Children 0..3 quantized max Y [0..255]
    uint32_t             QuantMaxYHigh       = 0u;                 // Children 4..7 quantized max Y [0..255]
    uint32_t             QuantMinZLow        = 0u;                 // Children 0..3 quantized min Z [0..255]
    uint32_t             QuantMinZHigh       = 0u;                 // Children 4..7 quantized min Z [0..255]
    uint32_t             QuantMaxZLow        = 0u;                 // Children 0..3 quantized max Z [0..255]
    uint32_t             QuantMaxZHigh       = 0u;                 // Children 4..7 quantized max Z [0..255]
};

struct PackedRayTriangle
{
    std::array<float, 3>  Vertex0           = {};
    float                 Uv0U              = 0.0f;
    std::array<float, 3>  Edge1             = {}; // Vertex1 - Vertex0
    float                 Uv0V              = 0.0f;
    std::array<float, 3>  Edge2             = {}; // Vertex2 - Vertex0
    uint32_t              ClassificationId  = 0u;
    std::array<float, 2>  Uv1               = {};
    std::array<float, 2>  Uv2               = {};
    bool                  TransmissiveGlass = false;
    float                 RefractiveIndex   = 1.52f;
    std::array<float, 3>  AbsorptionPerM    = {0.12f, 0.04f, 0.06f};
    std::array<uint32_t, 3> OctVertexNormals = {0u, 0u, 0u}; // Packed 2x snorm16 octahedral smooth normals per corner
    float                 SurfaceAreaM2     = 0.0f;          // [m^2]
};

struct SoftwareRayHit
{
    bool                 Hit                     = false;
    float                DistanceMeters          = 1.0e30f; // [m]
    uint32_t             TriangleIndex           = UINT32_MAX;
    uint32_t             InstanceIndex           = 0u;
    uint32_t             ClassificationId        = 0u;
    std::array<float, 3> WorldPosition           = {};
    std::array<float, 3> PreviousWorldPosition   = {};
    std::array<float, 3> GeometricNormal         = {0.0f, 1.0f, 0.0f};
    std::array<float, 3> ShadingNormal           = {0.0f, 1.0f, 0.0f};
    std::array<float, 2> TextureCoord            = {};
    bool                 TransmissiveGlass       = false;
    float                RefractiveIndex         = 1.52f;
    std::array<float, 3> AbsorptionPerM          = {0.12f, 0.04f, 0.06f};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           DIELECTRIC BOUNDARY OPTICS
//------------------------------------------------------------------------------------------------------------------------

struct DielectricBoundaryResult
{
    float                FresnelReflectance      = 0.04f; // F(cos θ_i, η_i / η_t) in [0, 1]
    bool                 TotalInternalReflection = false; // sin^2 θ_t > 1
    std::array<float, 3> ReflectedDirection      = {0.0f, 1.0f, 0.0f};
    std::array<float, 3> RefractedDirection      = {0.0f, -1.0f, 0.0f};
};

// Evaluates exact unpolarized Fresnel equations & Snell refraction vector (with Total Internal Reflection detection).
[[nodiscard]] DielectricBoundaryResult EvaluateDielectricBoundary(std::array<float, 3> IncidentUnit,
                                                                  std::array<float, 3> BoundaryNormalUnit,
                                                                  float                EtaIncidentOverTransmitted) noexcept;

// Encodes/decodes unit normals into 32-bit 2x snorm16 octahedral coordinates (< 0.002 deg angular error).
[[nodiscard]] uint32_t             PackOctahedralNormal(std::array<float, 3> UnitNormal) noexcept;
[[nodiscard]] std::array<float, 3> UnpackOctahedralNormal(uint32_t PackedWord) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                        BOTTOM-LEVEL BOUNDING VOLUME ATLAS
//------------------------------------------------------------------------------------------------------------------------

class BoundingVolumeAtlas
{
  public:
    bool BuildFromFacetField(const FacetField&                   SourceFacets,
                             const ScreenSpaceClusterRefinement* MaterialSource = nullptr);

    bool BuildFromRefinementCut(const ScreenSpaceClusterRefinement& Refinement,
                                const ClusterRuntimeFrameReport&    CutReport);

    // CPU reference ray query over binary SAH hierarchy
    [[nodiscard]] SoftwareRayHit TraceClosestRay(std::array<float, 3> OriginMeters,
                                                 std::array<float, 3> DirectionUnit,
                                                 float                MinDistanceMeters = 1.0e-3f,
                                                 float                MaxDistanceMeters = 1.0e5f,
                                                 bool                 IncludeGlass      = false) const noexcept;

    // CPU reference ray query over quantized 8-wide CWBVH hierarchy (bit-exact oracle for Slang GPU wide search)
    [[nodiscard]] SoftwareRayHit TraceWideClosestRay(std::array<float, 3> OriginMeters,
                                                     std::array<float, 3> DirectionUnit,
                                                     float                MinDistanceMeters = 1.0e-3f,
                                                     float                MaxDistanceMeters = 1.0e5f,
                                                     bool                 IncludeGlass      = false) const noexcept;

    // CPU reference shadow transmittance: {0,0,0} when blocked by opaque geometry, Beer-Lambert tinted through glass
    [[nodiscard]] std::array<float, 3> TraceShadowTransmittance(std::array<float, 3> OriginMeters,
                                                                std::array<float, 3> DirectionUnit,
                                                                float                MaxDistanceMeters) const noexcept;

    // Legacy 2x float4 binary cell + 5x float4 triangle table packer
    void PackForGpu(std::vector<std::array<float, 4>>& OutCellFloat4Table,
                    std::vector<std::array<float, 4>>& OutTriangleFloat4Table) const;

    // 8-wide quantized CWBVH (5x float4 per wide cell) + smooth-normal 6x float4 triangle packer
    void PackWideForGpu(std::vector<std::array<float, 4>>& OutWideCellFloat4Table,
                        std::vector<std::array<float, 4>>& OutWideTriangleFloat4Table) const;

    [[nodiscard]] std::array<float, 3> LocalLowerBounds() const noexcept;
    [[nodiscard]] std::array<float, 3> LocalUpperBounds() const noexcept;

    [[nodiscard]] uint32_t                            CellCount() const noexcept { return static_cast<uint32_t>(Cells_.size()); }
    [[nodiscard]] uint32_t                            WideCellCount() const noexcept { return static_cast<uint32_t>(WideCells_.size()); }
    [[nodiscard]] uint32_t                            TriangleCount() const noexcept { return static_cast<uint32_t>(Triangles_.size()); }
    [[nodiscard]] uint32_t                            WideTriangleCount() const noexcept { return static_cast<uint32_t>(WideTriangles_.size()); }
    [[nodiscard]] bool                                Empty() const noexcept { return Cells_.empty() || Triangles_.empty(); }
    [[nodiscard]] std::span<const CompressedWideCell> WideCells() const noexcept { return WideCells_; }
    [[nodiscard]] std::span<const PackedRayTriangle>  WideTriangles() const noexcept { return WideTriangles_; }
    [[nodiscard]] std::span<const PackedRayTriangle>  Triangles() const noexcept { return Triangles_; }

  private:
    uint32_t BuildRecursive(std::vector<uint32_t>&             Indices,
                            std::vector<std::array<float, 3>>& Centroids,
                            std::vector<std::array<float, 3>>& TriBoundsMin,
                            std::vector<std::array<float, 3>>& TriBoundsMax,
                            uint32_t                           Begin,
                            uint32_t                           Count);

    void     CollapseIntoCompressedWideHierarchy();
    uint32_t CollapseWideSubtree(uint32_t BinaryCellIndex);

    std::vector<EnclosureCell>      Cells_{};
    std::vector<PackedRayTriangle>  Triangles_{};
    std::vector<CompressedWideCell> WideCells_{};
    std::vector<PackedRayTriangle>  WideTriangles_{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                  TWO-LEVEL INSTANCED ACCELERATION & EMISSIVE CDF
//------------------------------------------------------------------------------------------------------------------------

struct AffineTransform3x4
{
    std::array<float, 4> Row0 = {1.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> Row1 = {0.0f, 1.0f, 0.0f, 0.0f};
    std::array<float, 4> Row2 = {0.0f, 0.0f, 1.0f, 0.0f};

    [[nodiscard]] static AffineTransform3x4 Identity() noexcept { return AffineTransform3x4{}; }
    [[nodiscard]] static AffineTransform3x4 ComposeTrs(std::array<float, 3> TranslationMeters,
                                                       std::array<float, 3> EulerDegreesXyz,
                                                       std::array<float, 3> ScaleFactors) noexcept;
};

[[nodiscard]] AffineTransform3x4   InvertAffineTransform3x4(const AffineTransform3x4& Source) noexcept;
[[nodiscard]] std::array<float, 3> TransformAffinePoint(const AffineTransform3x4& M, std::array<float, 3> P) noexcept;
[[nodiscard]] std::array<float, 3> TransformAffineVector(const AffineTransform3x4& M, std::array<float, 3> V) noexcept;

struct VolumeInstancePlacement
{
    uint32_t           PrototypeIndex          = 0u;
    AffineTransform3x4 ObjectToWorld           = AffineTransform3x4::Identity();
    AffineTransform3x4 PreviousObjectToWorld   = AffineTransform3x4::Identity();
    uint32_t           ClassificationOverride  = UINT32_MAX; // UINT32_MAX preserves per-facet classification
    bool               ForceTransmissiveGlass  = false;
    float              GlassRefractiveIndex    = 1.52f;
    std::array<float, 3> GlassAbsorptionPerM   = {0.10f, 0.04f, 0.05f};
};

struct EmissiveSlotRadiance
{
    std::array<float, 3> RadianceRgb = {0.0f, 0.0f, 0.0f}; // [W / (sr * m^2)]
};

// Flux-weighted emissive facet record (world-space; Φ_i = π · A_i · Luminance_i).
struct EmissiveFacetRecord
{
    std::array<float, 3> WorldVertex0  = {};
    std::array<float, 3> WorldEdge1    = {};
    std::array<float, 3> WorldEdge2    = {};
    std::array<float, 3> RadianceRgb   = {};
    float                AreaM2        = 0.0f;
    float                SelectionProb = 0.0f;
    float                CdfUpper      = 0.0f;
};

struct SampledEmissiveSurfacePoint
{
    bool                 Valid              = false;
    std::array<float, 3> WorldPosition      = {};
    std::array<float, 3> WorldNormal        = {0.0f, 1.0f, 0.0f};
    std::array<float, 3> EmittedRadiance    = {};
    float                AreaPdfPerSquareM  = 0.0f; // [1 / m^2]
    uint32_t             SelectedFacetIndex = 0u;
};

class InstancedBoundingScene
{
  public:
    void     Clear() noexcept;
    uint32_t RegisterPrototype(BoundingVolumeAtlas PrototypeAtlas);

    // Rebuilds ONLY the instance TLAS + world-to-object transforms (< 0.05 ms for dynamic scenes; BLAS tables stay untouched).
    bool RebuildInstanceHierarchy(std::span<const VolumeInstancePlacement> Placements);

    // Convenience wrapper constructing a 1-instance identity scene from a single prebuilt BoundingVolumeAtlas.
    bool BuildSinglePrototypeScene(const BoundingVolumeAtlas& SingleAtlas);

    // Packs two-level TLAS + Instances + concatenated 8-wide CWBVH BLAS tables for Slang GPU search.
    void PackSceneForGpu(std::vector<std::array<float, 4>>& OutTlasCellFloat4Table,
                         std::vector<std::array<float, 4>>& OutInstanceFloat4Table,
                         std::vector<std::array<float, 4>>& OutWideCellFloat4Table,
                         std::vector<std::array<float, 4>>& OutWideTriangleFloat4Table) const;

    // Builds flux-weighted emissive triangle CDF (Φ_i = π * Area_i * Luminance_i) over all active emissive instances.
    void BuildEmissiveFacetDistribution(std::span<const EmissiveSlotRadiance> SlotEmissions,
                                        std::vector<std::array<float, 4>>&    OutEmissiveFacetFloat4Table,
                                        uint32_t&                             OutEmissiveFacetCount,
                                        float&                                OutTotalFluxWatts);

    [[nodiscard]] SampledEmissiveSurfacePoint SampleEmissiveSurface(float SelectorU, float BaryU, float BaryV) const noexcept;

    [[nodiscard]] SoftwareRayHit TraceClosestRay(std::array<float, 3> OriginMeters,
                                                 std::array<float, 3> DirectionUnit,
                                                 float                MinDistanceMeters = 1.0e-3f,
                                                 float                MaxDistanceMeters = 1.0e5f,
                                                 bool                 IncludeGlass      = false) const noexcept;

    [[nodiscard]] std::array<float, 3> TraceShadowTransmittance(std::array<float, 3> OriginMeters,
                                                                std::array<float, 3> DirectionUnit,
                                                                float                MaxDistanceMeters) const noexcept;

    [[nodiscard]] uint32_t PrototypeCount() const noexcept { return static_cast<uint32_t>(Prototypes_.size()); }
    [[nodiscard]] uint32_t InstanceCount() const noexcept { return static_cast<uint32_t>(Instances_.size()); }
    [[nodiscard]] uint32_t TlasCellCount() const noexcept { return static_cast<uint32_t>(TlasCells_.size()); }
    [[nodiscard]] uint32_t TotalWideCellCount() const noexcept { return TotalWideCellCount_; }
    [[nodiscard]] uint32_t TotalTriangleCount() const noexcept { return TotalTriangleCount_; }
    [[nodiscard]] uint32_t EmissiveFacetCount() const noexcept { return static_cast<uint32_t>(EmissiveFacets_.size()); }
    [[nodiscard]] bool     Empty() const noexcept { return Instances_.empty() || TotalWideCellCount_ == 0u; }

    [[nodiscard]] std::span<const EmissiveFacetRecord> QueryEmissiveFacetRecords() const noexcept { return PublicEmissiveFacets_; }
    [[nodiscard]] std::span<const VolumeInstancePlacement> QueryPlacements() const noexcept { return ActivePlacements_; }

  private:
    struct PrototypeSlice
    {
        BoundingVolumeAtlas Atlas{};
        uint32_t            GlobalWideCellOffset = 0u;
        uint32_t            GlobalTriangleOffset = 0u;
    };

    struct ResolvedInstance
    {
        VolumeInstancePlacement Placement{};
        AffineTransform3x4      WorldToObject{};
        std::array<float, 3>    WorldLower = {};
        std::array<float, 3>    WorldUpper = {};
        uint32_t                WideRoot   = 0u;
    };

    struct EmissiveFacetEntry
    {
        std::array<float, 3> WorldV0        = {};
        std::array<float, 3> WorldE1        = {};
        std::array<float, 3> WorldE2        = {};
        std::array<float, 3> Radiance       = {};
        float                AreaM2         = 0.0f;
        float                CdfLower       = 0.0f;
        float                CdfUpper       = 0.0f;
        float                SelectionProb  = 0.0f;
    };

    uint32_t BuildTlasRecursive(std::vector<uint32_t>& Indices, uint32_t Begin, uint32_t Count);

    std::vector<PrototypeSlice>     Prototypes_{};
    std::vector<ResolvedInstance>   Instances_{};
    std::vector<VolumeInstancePlacement> ActivePlacements_{};
    std::vector<EnclosureCell>      TlasCells_{};
    std::vector<EmissiveFacetEntry> EmissiveFacets_{};
    std::vector<EmissiveFacetRecord> PublicEmissiveFacets_{};
    uint32_t                        TotalWideCellCount_ = 0u;
    uint32_t                        TotalTriangleCount_ = 0u;
    mutable bool                    StaticTablesDirty_  = true;
    mutable std::vector<std::array<float, 4>> PackedWideCellTable_{};
    mutable std::vector<std::array<float, 4>> PackedWideTriTable_{};
};

} // namespace Frontier
