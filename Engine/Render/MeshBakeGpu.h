/*==========================================================================================================================================
                                                             MESHBAKEGPU.H
==========================================================================================================================================*/
// 🧩 GPU mesh-map baking: uploads BakeMesh.cpp's low/high mesh and BVH once, then dispatches
// Engine/Render/Shader/MeshBakeCompute.slang to evaluate every requested map for an entire texture in one pass.
// 📝 This is the Vulkan counterpart of Engine/ContentInterchange/BakeMesh.cpp's RunMeshBake (CPU) - both produce
// identical BakePartResult/BakeMapImage output so callers (BakingWorkspacePanel) can select either backend without
// changing anything downstream. See Engine/Render/MeshBakeGpuMirror.h for the Vulkan-free regression reference.

#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/Render/SurfaceRasterization.h"
#include <array>
#include <cstdint>
#include <memory>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU BUFFER LAYOUTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Canonical channel indices - must match MeshBakeCompute.slang's kChannel* constants exactly, and must cover
// every key BakeMesh.h's ImplementedMeshMapKeys() returns (enforced by MeshBakeGpuChannelIndex's unit test).
enum class MeshBakeGpuChannelIndex : uint32_t
{
    Normal = 0, WorldNormal, ObjectNormal, BentNormal, BentWorldNormal, BentObjectNormal, BevelNormal, Occlusion,
    OcclusionSecondary, Curvature, Convexity, Cavity, Thickness, Height, VectorDisplacement, Position, Id,
    UvIsland, Uv, Wireframe, Coverage, Alpha, BevelMask, Dust, Dirt, EdgeWear, BaseColor, Count
};
constexpr uint32_t MeshBakeGpuChannelCount = uint32_t(MeshBakeGpuChannelIndex::Count);
// 📝 Returns MeshBakeGpuChannelIndex::Count (invalid) if Key is not one of BakeMesh.h's implemented keys.
MeshBakeGpuChannelIndex MeshBakeGpuChannelForKey(const std::string &Key);

// 📝 Flags.x bits in GpuBakeSettings below - must match MeshBakeCompute.slang's kNeed* constants exactly.
enum MeshBakeGpuNeedsFlag : uint32_t
{
    MeshBakeGpuNeedHemisphereSampling = 1u,
    MeshBakeGpuNeedCurvatureSampling  = 2u,
    MeshBakeGpuNeedThickness          = 4u,
    MeshBakeGpuNeedBevel              = 8u,
};

struct GpuVec4 { float X = 0, Y = 0, Z = 0, W = 0; };
// 📝 Mirrors MeshBakeCompute.slang's GpuLowVertex exactly (48 bytes, std430-safe: every member is a float4).
struct GpuLowVertex { GpuVec4 Position, Normal, UvChart; }; // UvChart = (U, V, ChartId, unused)
// 📝 Mirrors MeshBakeCompute.slang's GpuTriangle exactly (112 bytes).
struct GpuTriangle { GpuVec4 A, B, C, NormalA, NormalB, NormalC; uint32_t Meta[4] = {0, 0, 0, 0}; }; // Meta.x = PartIndex
// 📝 Mirrors MeshBakeCompute.slang's GpuBvhNode exactly (48 bytes); LeftRightStartCount matches
// BakeTriangleBvh::Node's Left/Right/Start/Count fields, including Left<0 meaning "leaf".
struct GpuBvhNode { GpuVec4 Minimum, Maximum; int32_t LeftRightStartCount[4] = {-1, -1, 0, 0}; };
// 📝 Mirrors MeshBakeCompute.slang's GpuBakeSettings exactly, field for field, including byte order - see that
// file's own comment on why the object-to-world transform is three explicit row vectors, not a float4x4.
struct GpuBakeSettings
{
    GpuVec4 ObjectToWorldRow0, ObjectToWorldRow1, ObjectToWorldRow2;
    GpuVec4 BoundsMin, BoundsMax;
    GpuVec4 DiagonalEpsilon;             // xyz = Diagonal, w = Epsilon
    GpuVec4 FrontBackAo;                 // x=Front, y=Back, z=AoDistance, w=SecondaryAoDistance
    GpuVec4 ThicknessCurvWireMask;       // x=ThicknessRange, y=CurvatureRadius, z=WireWidth, w=MaskStrength
    GpuVec4 BevelRadiusStrength;         // x=BevelRadius, y=BevelStrength, zw=unused
    GpuVec4 DustUpFalloff;               // xyz = DustUp, w = DustFalloff
    uint32_t SampleCounts[4] = {1, 1, 0, 0}; // x=Samples, y=BevelSamples, z=Resolution, w=LowIndexCount
    uint32_t Flags[4] = {0, 0, 0, 0};        // x=NeedsMask, y=NormalPlusY(0/1), zw=unused
};

// 📝 Flattens a BakePart + its BakeTriangleBvh into the exact GPU buffer layouts above - pure data
// transformation, no Vulkan involved, so it is directly unit-testable (see MeshBakeGpuMirror.h's caller).
struct MeshBakeGpuPlan
{
    std::vector<GpuLowVertex> LowVertices;
    std::vector<uint32_t> LowIndices;
    std::vector<GpuTriangle> HighTriangles;
    std::vector<GpuBvhNode> BvhNodes;
    std::vector<uint32_t> BvhOrder;
    GpuBakeSettings Settings{};
    // 📝 Index MeshBakeGpuChannelCount long; kInvalidSlot (0xffffffff) where that canonical channel was not
    // requested, otherwise its compact slot in the Output buffer (0..RequestedChannelKeys.size()-1).
    std::array<uint32_t, MeshBakeGpuChannelCount> ChannelSlots{};
    std::vector<std::string> RequestedChannelKeys; // in compact slot order, for re-attaching BakeMapImage::Key
};
constexpr uint32_t MeshBakeGpuInvalidSlot = 0xffffffffu;

// 📝 Pure CPU construction (no Vulkan access needed): builds everything RunMeshBakeGpu's Vulkan path uploads
// verbatim, and everything MeshBakeGpuMirror.h's regression test exercises without any GPU at all.
MeshBakeGpuPlan BuildMeshBakeGpuPlan(const BakePart &Target, const BakeMeshSet &High, const BakeSettings &Settings);

//------------------------------------------------------------------------------------------------------------------------
//                                                     VULKAN DISPATCH (borrowed device, caller-owned submission)
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ Borrowed-device ownership, same contract as VectorCoverageRasterization: the caller serializes one queue,
// records at most one dispatch per submission, confirms accepted submissions and retires every use before
// destruction. This class never calls vkQueueSubmit or waits on a fence itself.
class MeshBakeGpuRasterization
{
  public:
    MeshBakeGpuRasterization();
    ~MeshBakeGpuRasterization();
    MeshBakeGpuRasterization(const MeshBakeGpuRasterization &) = delete;
    MeshBakeGpuRasterization &operator=(const MeshBakeGpuRasterization &) = delete;
    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader);
    // ⚠️ Plan must be retired (no earlier submission still reading this instance's buffers) before calling again.
    void ConfigureRetiredPlan(const MeshBakeGpuPlan &Plan, uint64_t CompletedSerial);
    bool ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConstructReadbackCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    // 📝 Returns one BakeMapImage per plan.RequestedChannelKeys entry, Rgba sized Resolution*Resolution*4.
    [[nodiscard]] std::vector<BakeMapImage> RetrieveReadback(uint64_t CompletedSerial);
    void ConfirmSubmission() noexcept;
    void Finalize() noexcept;
    [[nodiscard]] uint64_t QueryDispatchCount() const noexcept;

  private:
    struct BakeAllocation;
    std::unique_ptr<BakeAllocation> Allocation;
};
} // namespace Frontier
