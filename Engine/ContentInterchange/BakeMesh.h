/*==========================================================================================================================================
                                                              BAKEMESH.H
==========================================================================================================================================*/
// 🧩 Geometry-only mesh-to-mesh baking core: the native counterpart of Experimental/TextureStudio/src/baking/bakeMeshes.js
//    and meshBakeCore.js. Holds the low/high mesh data model, the three built-in study meshes (Teapot/Cube/Sphere, ported
//    1:1 from the web reference's three.js geometry), a bounded CPU BVH, and the first slice of mesh-map evaluators.
//
// 📝 Scope (first native slice — see Experimental/TextureStudio/docs/baking-studio.md for the full 27-map contract):
//    implemented maps are Tangent normal, World normal, UV coverage and Alpha/hit mask. The remaining Normals/Surface/
//    Coordinates/Masks/Color maps are deliberately not evaluated yet; BakeMeshEngine reports them as not-yet-implemented
//    rather than inventing placeholder pixels.

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                        MESH DATA MODEL
//------------------------------------------------------------------------------------------------------------------------

struct BakeVertex
{
    float Position[3]{0, 0, 0};
    float Normal[3]{0, 0, 1};
    float Uv[2]{0, 0};
    // 📝 0 means "no chart"; built-in low meshes assign 1-based chart ids per UV island (matches
    // bakeMeshes.js packTeapotUVs/packedBox bakeChartId attribute).
    uint32_t ChartId = 0;
};

struct BakePart
{
    std::string Name;
    std::vector<BakeVertex> Vertices;
    std::vector<uint32_t> Indices; // triangle list, CCW seen from outside
    std::array<float, 16> ObjectToWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // row-major identity
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return uint32_t(Indices.size() / 3); }
};

struct BakeMeshSet
{
    std::vector<BakePart> Parts;
    std::string Label;
    std::string Source;
};

struct DemoBakeAssets
{
    BakeMeshSet Low;
    BakeMeshSet High;
};

// 📝 1:1 port of Experimental/TextureStudio/src/baking/bakeMeshes.js createDemoBakeAssets: Shape is one of
// "Teapot" | "Cube" | "Sphere". Low meshes carry the packed non-overlapping UV layout the web reference
// builds (teapot: 32 patches tiled 8x4 with a gutter; cube: 6 faces tiled 3x2; sphere: one pole-clamped island).
[[nodiscard]] DemoBakeAssets BuildDemoBakeAssets(const std::string &Shape);

[[nodiscard]] std::string MeshSummaryText(const BakeMeshSet &Set);
struct MeshSummary
{
    uint32_t Objects = 0;
    uint64_t Triangles = 0;
    uint64_t Vertices = 0;
    bool HasUv = true;
};
[[nodiscard]] MeshSummary SummarizeMesh(const BakeMeshSet &Set);

//------------------------------------------------------------------------------------------------------------------------
//                                                        TRIANGLE BVH
//------------------------------------------------------------------------------------------------------------------------

// 📝 A bounded, median-split (non-SAH) BVH over one merged high-mesh triangle soup. Matches the spec's own
// "CPU BVH" framing in Experimental/TextureStudio/docs/baking-studio.md; built once per bake, not persisted.
class BakeTriangleBvh
{
  public:
    void Build(const BakeMeshSet &High);
    struct Hit
    {
        bool Found = false;
        float Distance = 0;
        float Normal[3]{0, 0, 0};
        float Position[3]{0, 0, 0};
        uint32_t PartIndex = 0;
    };
    // 📝 Casts a ray from Origin along Direction (expected normalized), accepting hits in [MinDistance, MaxDistance].
    [[nodiscard]] Hit CastRay(const float Origin[3], const float Direction[3], float MinDistance, float MaxDistance) const;
    [[nodiscard]] bool Empty() const noexcept { return Triangles.empty(); }

  private:
    struct Triangle
    {
        float A[3], B[3], C[3];
        float NormalA[3], NormalB[3], NormalC[3];
        uint32_t PartIndex;
    };
    struct Node
    {
        float Minimum[3]{0, 0, 0}, Maximum[3]{0, 0, 0};
        int32_t Left = -1, Right = -1; // -1 == leaf
        uint32_t Start = 0, Count = 0;
    };
    std::vector<Triangle> Triangles;
    std::vector<uint32_t> Order;
    std::vector<Node> Nodes;
    int32_t BuildRange(uint32_t Start, uint32_t Count);
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        BAKE SETTINGS + ENGINE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Mirrors Experimental/TextureStudio/src/baking/bakeRecipe.js / meshBakeCore.js validateMeshBakeSettings' fields
// that the first native slice actually consumes. Fields the native engine does not yet evaluate (bevel/dust/mask
// strength, secondary AO, curvature radius, …) are still carried so the inspector and recipe schema stay 1:1,
// even before their maps are implemented.
struct BakeSettings
{
    uint32_t Resolution = 256;
    uint32_t Padding = 2;
    bool NormalPlusY = true; // true: OpenGL +Y, false: DirectX -Y
    bool ProjectionRay = true; // true: Normal ray, false: Nearest surface (nearest not yet implemented)
    float Front = 0.1f;
    float Back = 0.1f;
    float AoDistance = 0.5f;
    float SecondaryAoDistance = 1.0f;
    float ThicknessRange = 0.5f;
    float CurvatureRadius = 0.05f;
    uint32_t Samples = 16;
    float BevelRadius = 0.02f;
    float BevelStrength = 0.5f;
    uint32_t BevelSamples = 8;
    std::string DustAxis = "+Y";
    float DustFalloff = 2.0f;
    float WireWidth = 1.0f;
    float MaskStrength = 2.0f;
    bool MatchByName = true;
    std::vector<std::string> Channels; // mesh map keys, e.g. "normal", "world-normal", "coverage", "alpha"
};

struct BakeMapImage
{
    std::string Key;
    std::vector<uint8_t> Rgba; // Resolution * Resolution * 4, row-major, top-left origin
};

struct BakeStats
{
    uint64_t Hits = 0;
    uint64_t Misses = 0;
    uint32_t Charts = 0;
};

struct BakePartResult
{
    std::string Name;
    uint32_t Resolution = 0;
    std::vector<BakeMapImage> Maps;
    BakeStats Stats;
};

struct BakeProgress
{
    std::string Name;
    uint32_t SetIndex = 0, SetCount = 0;
    uint64_t Done = 0, Total = 0;
};

// 📝 The mesh map keys this native slice can actually evaluate; everything else in the 27-map contract is left
// as a disabled, labeled tile in the UI rather than a fake result (see BakingWorkspacePanel).
[[nodiscard]] const std::vector<std::string> &ImplementedMeshMapKeys();
[[nodiscard]] bool IsMeshMapImplemented(const std::string &Key);

// 📝 Bakes every Channel the settings request (intersected with ImplementedMeshMapKeys) for each of Targets,
// projecting against High via a freshly built BakeTriangleBvh. Mirrors bakeMeshJob.js's startMeshBake, minus
// cancellation/async plumbing (the native panel runs this on a worker thread itself).
[[nodiscard]] std::vector<BakePartResult> RunMeshBake(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings, const std::function<void(const BakeProgress &)> &OnProgress = {});
} // namespace Frontier
