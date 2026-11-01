/*==========================================================================================================================================
                                                   OCTET VOLUME ATLAS
                                     ------------------------------------------------------------------
                 Two-level instanced 8-wide quantized compressed BVH (TLAS + 8-wide BLAS) for GTX compute ray queries
==========================================================================================================================================*/
// 🧩 Collapses 16-bin SAH bottom-level trees into 8-wide power-of-two quantized Octet blocks (Ylitie-Karras-Laine 2017)
//    beneath a dynamic refittable top-level instance SAH enclosure tree (zero VK_KHR_ray_query / RT-core requirement).

#pragma once

#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace Frontier
{

struct BoundingVolumeBuildSpec
{
    uint32_t                                               DefaultClassification = 0u;
    std::function<bool(uint32_t)>                          ClassifyGlass         = [](uint32_t ClassId) noexcept { return ClassId == 7u || ClassId == 12u; };
    std::function<void(uint32_t, float&, std::array<float, 3>&)> GlassOptics     = [](uint32_t ClassId, float& OutIor, std::array<float, 3>& OutAbsorb) noexcept
    {
        OutIor    = (ClassId == 12u) ? 1.50f : 1.52f;
        OutAbsorb = (ClassId == 12u) ? std::array<float, 3>{0.04f, 0.04f, 0.04f} : std::array<float, 3>{0.10f, 0.03f, 0.05f};
    };
};

struct GlassRefractionHit
{
    bool                 HitGlass           = false;
    uint32_t             ClassificationId   = 0u;
    float                FrontHitMeters     = 1.0e30f;
    float                ThicknessMeters    = 0.014f;
    float                RefractiveIndex    = 1.52f;
    std::array<float, 3> AbsorptionPerMeter = {0.10f, 0.03f, 0.05f};
    std::array<float, 3> FrontNormal        = {0.0f, 1.0f, 0.0f};
};

//------------------------------------------------------------------------------------------------------------------------
//                                  BOTTOM-LEVEL 8-WIDE QUANTIZED BLOCK & INSTANCE SPECS
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) OctetInteriorBlock
{
    std::array<float, 3>    OriginMeters{0.0f, 0.0f, 0.0f};      // [m] - parent enclosure lower corner
    uint32_t                PackedExponentsMask = 0u;            // [bits] - [eX+128 | (eY+128)<<8 | (eZ+128)<<16 | mask<<24]
    std::array<uint32_t, 8> SlotLinks{};                         // [bits] - bit31=1: internal block idx; bit31=0: count<<24 | firstTri
    std::array<uint8_t, 8>  QuantizedLowX{};                     // [0..255] - conservative lower X slab in step units
    std::array<uint8_t, 8>  QuantizedHighX{};                    // [0..255] - conservative upper X slab in step units
    std::array<uint8_t, 8>  QuantizedLowY{};                     // [0..255] - conservative lower Y slab in step units
    std::array<uint8_t, 8>  QuantizedHighY{};                    // [0..255] - conservative upper Y slab in step units
    std::array<uint8_t, 8>  QuantizedLowZ{};                     // [0..255] - conservative lower Z slab in step units
    std::array<uint8_t, 8>  QuantizedHighZ{};                    // [0..255] - conservative upper Z slab in step units
};
static_assert(sizeof(OctetInteriorBlock) == 96u, "OctetInteriorBlock must match 6 x float4 GPU stride (96 B)");

struct InstancePlacementSpec
{
    uint32_t              BottomArchiveIndex    = 0u;                              // [-] - index into registered bottom archives
    std::array<float, 16> ObjectToWorldColumn   = {1.0f, 0.0f, 0.0f, 0.0f,         // [-] - column-major 4x4 affine transform
                                                   0.0f, 1.0f, 0.0f, 0.0f,
                                                   0.0f, 0.0f, 1.0f, 0.0f,
                                                   0.0f, 0.0f, 0.0f, 1.0f};
    uint32_t              ClassificationOverride = 0xFFFFFFFFu;                    // [-] - 0xFFFFFFFF keeps triangle ClassId
    bool                  OverrideOpticsEnabled  = false;                          // [-] - true overrides surface tint/emission
    std::array<float, 3>  AlbedoMultiplier       = {1.0f, 1.0f, 1.0f};             // [-] - per-instance tint multiplier
    float                 SelfRadianceScale      = 0.0f;                           // [W/(m^2*sr)] - thermal/luminous boost
};

struct OctetInstanceHit
{
    bool                 Hit               = false;
    float                DistanceMeters    = 1.0e30f;          // [m] - world-space ray parameter t
    uint32_t             InstanceIndex     = 0xFFFFFFFFu;      // [-] - top-level instance index
    uint32_t             PrimitiveIndex    = 0xFFFFFFFFu;      // [-] - bottom-level triangle index
    uint32_t             ClassificationId  = 0u;               // [-] - resolved material slot [0..63]
    uint32_t             VisitedBlockCount = 0u;               // [-] - 8-wide + TLAS slab step count
    std::array<float, 3> WorldPosition{0.0f, 0.0f, 0.0f};      // [m]
    std::array<float, 3> GeometricNormal{0.0f, 1.0f, 0.0f};    // [-] - world-space unit normal
    std::array<float, 3> WorldTangent{1.0f, 0.0f, 0.0f};       // [-] - world-space unit tangent
    std::array<float, 2> TextureCoord{0.0f, 0.0f};             // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   OCTET VOLUME ATLAS
//------------------------------------------------------------------------------------------------------------------------

class OctetVolumeAtlas
{
  public:
    OctetVolumeAtlas() = default;

    void Clear() noexcept;

    // Appends a bottom-level 8-wide compressed archive built from a local-space FacetField; returns its archive index.
    uint32_t RegisterBottomArchive(const FacetField& Field, const BoundingVolumeBuildSpec& Spec = {});

    // Builds the top-level SAH tree over placed instances referencing registered bottom-level archives.
    bool BuildInstanceScene(std::span<const InstancePlacementSpec> Placements);

    // Refits / rebuilds ONLY the top-level instance transforms & AABBs in microseconds while keeping all 8-wide BLASes intact.
    bool RefitTopLevelInstances(std::span<const InstancePlacementSpec> UpdatedPlacements);

    [[nodiscard]] bool     Empty() const noexcept { return TopCells_.empty() || Instances_.empty(); }
    [[nodiscard]] uint32_t BottomArchiveCount() const noexcept { return static_cast<uint32_t>(Archives_.size()); }
    [[nodiscard]] uint32_t InstanceCount() const noexcept { return static_cast<uint32_t>(Instances_.size()); }
    [[nodiscard]] uint32_t TotalOctetBlockCount() const noexcept { return TotalOctetBlocks_; }
    [[nodiscard]] uint32_t TotalTriangleCount() const noexcept { return TotalTriangles_; }
    [[nodiscard]] uint32_t TopCellCount() const noexcept { return static_cast<uint32_t>(TopCells_.size()); }

    [[nodiscard]] std::array<float, 3> LowerWorldBounds() const noexcept;
    [[nodiscard]] std::array<float, 3> UpperWorldBounds() const noexcept;

    // Exact CPU reference queries mirroring OctetVolumeSearch.slang 1:1
    [[nodiscard]] OctetInstanceHit SearchClosestOpaque(const std::array<float, 3>& OriginMeters,
                                                       const std::array<float, 3>& DirectionUnit,
                                                       float                       MinDistanceMeters = 1.0e-3f,
                                                       float                       MaxDistanceMeters = 1.0e6f) const noexcept;

    [[nodiscard]] GlassRefractionHit SearchGlassRefractionPeel(const std::array<float, 3>& OriginMeters,
                                                               const std::array<float, 3>& DirectionUnit,
                                                               float                       MaxDistanceMeters) const noexcept;

    [[nodiscard]] std::array<float, 3> EvaluateShadowTransmittance(const std::array<float, 3>& OriginMeters,
                                                                   const std::array<float, 3>& DirectionUnit,
                                                                   float                       MaxDistanceMeters) const noexcept;

    // Packs two-level TLAS + 8-wide Octet BLAS + Triangle tables into contiguous float4 buffers for Slang compute:
    //   - OutTopCellTable    : 2 * float4 per TLAS cell ([Lo.xyz, LeftOrFirst], [Hi.xyz, RightOrCount | InteriorBit])
    //   - OutInstanceTable   : 9 * float4 per TLAS instance (Inv0..3, Fwd0..2, BoundsCenterRadius, TailMeta, OpticsOverride)
    //   - OutOctetBlockTable : 6 * float4 per 8-wide Octet block (96 B)
    //   - OutTriangleTable   : 5 * float4 per BLAS triangle (80 B)
    void PackForGpu(std::vector<std::array<float, 4>>& OutTopCellTable,
                    std::vector<std::array<float, 4>>& OutInstanceTable,
                    std::vector<std::array<float, 4>>& OutOctetBlockTable,
                    std::vector<std::array<float, 4>>& OutTriangleTable) const;

    // Exposes world-space transformed triangles for luminous CDF extraction or baking
    void ExtractWorldTriangles(std::vector<PackedRayTriangle>& OutWorldTriangles) const;

  private:
    struct BottomArchiveEntry
    {
        std::vector<OctetInteriorBlock> Blocks{};
        std::vector<PackedRayTriangle>  Triangles{};
        std::array<float, 3>            LocalLow{0.0f, 0.0f, 0.0f};
        std::array<float, 3>            LocalHigh{0.0f, 0.0f, 0.0f};
        uint32_t                        GlobalBlockOffset    = 0u;
        uint32_t                        GlobalTriangleOffset = 0u;
    };

    struct ResolvedInstanceEntry
    {
        InstancePlacementSpec Spec{};
        std::array<float, 16> WorldToObjectColumn{};
        std::array<float, 3>  WorldLow{0.0f, 0.0f, 0.0f};
        std::array<float, 3>  WorldHigh{0.0f, 0.0f, 0.0f};
        uint32_t              BlockOffset    = 0u;
        uint32_t              TriangleOffset = 0u;
    };

    uint32_t BuildTopRecursive(std::vector<uint32_t>& OrderedIndices, uint32_t Begin, uint32_t End);

    std::vector<BottomArchiveEntry>    Archives_{};
    std::vector<ResolvedInstanceEntry> Instances_{};
    std::vector<uint32_t>               OrderedInstanceIndices_{};
    std::vector<EnclosureCell>          TopCells_{};
    uint32_t                            TotalOctetBlocks_ = 0u;
    uint32_t                            TotalTriangles_   = 0u;
};

} // namespace Frontier
