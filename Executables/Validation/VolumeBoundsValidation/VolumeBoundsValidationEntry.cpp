/*==============================================================================================================================================
                                                    VOLUMEBOUNDSVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for the LBVH build's scene-bounds reduce: brings up a HEADLESS Vulkan device, uploads generated triangle geometry, runs
//    VolumeBoundsSubmission over it, reads the box back and judges it against a CPU min/max over the same centroids. Nothing before this proved the
//    shader works — VolumeBoundsOrderedIntProbe proved the ordered-int TRANSFORM on the CPU, but it executes no Vulkan, so the shared-memory halving
//    reduction, the partial-tile identity seeding, the six atomics and the seed/barrier ordering are all untested until this runs.
//
//    🔴 THE JUDGEMENT IS BIT-EXACT, NOT APPROXIMATE. min and max are selections, not arithmetic — they return one of their inputs unmodified, so no
//       rounding is introduced no matter how the reduction is associated. The GPU's tree order and the CPU's linear order must therefore agree to
//       the last bit. Judging with a tolerance would hide exactly the bugs this gate exists to catch: a lane that folds in a stale shared slot, or a
//       partial tile whose identity seeding is wrong, both perturb the box by a tiny amount that an epsilon would forgive.
//
//    📝 Headless on purpose, same as the radix gate: InitializeVulkanHost never creates or queries a surface, so a zero extension count yields a
//       compute-capable device with no window.

#define _CRT_SECURE_NO_WARNINGS

#include "Graphics/Acceleration/VolumeBoundsSubmission.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Where the compiled .comp.spv files live, relative to the repo root this exe is launched from.
const char* ShaderDirectoryPath = "Internal/Graphics/Acceleration/Shaders";

// The stride-32 RenderVertex the shader reads: position @0, normal @12, texcoord @24. Only the position is meaningful here, but the full stride must
// be emitted or the shader's indexing walks off the intended element.
struct RenderVertex
{
    float PositionX, PositionY, PositionZ;
    float NormalX,   NormalY,   NormalZ;
    float TexU,      TexV;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE DISTRIBUTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each shape targets a specific way the reduce can be wrong, rather than variety for its own sake.
enum class SceneShape
{
    OriginCentred,     // 🔴 straddles all three axes: the only case that exercises the ordered-int negative half
    PositiveOctant,    // entirely positive: passes even with a broken sign flip, so it is the CONTROL
    NegativeOctant,    // entirely negative: every accumulator lives in the flipped half
    FlatPlane,         // zero extent on one axis: degenerate box, the Morton pass will divide by this
    SinglePoint,       // every centroid identical: minimum == maximum on all three axes
    ThinSliver,        // enormous extent on one axis, microscopic on the others
    FarFromOrigin,     // large coordinates: tests that precision holds where floats are sparse
    ExtremeMagnitude   // near the float range limits: the ordering must still hold
};

const char* DescribeShape(SceneShape Shape)
{
    switch (Shape)
    {
        case SceneShape::OriginCentred:    return "origin-centred";
        case SceneShape::PositiveOctant:   return "positive-octant";
        case SceneShape::NegativeOctant:   return "negative-octant";
        case SceneShape::FlatPlane:        return "flat-plane";
        case SceneShape::SinglePoint:      return "single-point";
        case SceneShape::ThinSliver:       return "thin-sliver";
        case SceneShape::FarFromOrigin:    return "far-from-origin";
        case SceneShape::ExtremeMagnitude: return "extreme-magnitude";
    }
    return "unknown";
}

// Build TriangleCount triangles as a flat vertex stream plus an index stream (three consecutive indices per triangle). The corners are scattered
// around a per-triangle centre so the CENTROID lands where the shape intends; the shader reduces centroids, not corners.
void BuildScene(SceneShape             Shape,
                uint32_t               TriangleCount,
                uint32_t               Seed,
                std::vector<RenderVertex>& OutVertices,
                std::vector<uint32_t>&     OutIndices)
{
    OutVertices.clear();
    OutIndices.clear();
    OutVertices.reserve((size_t)TriangleCount * 3u);
    OutIndices.reserve((size_t)TriangleCount * 3u);

    std::mt19937 Generator(Seed);
    std::uniform_real_distribution<float> UnitSpread(-1.0f, 1.0f);

    // 🔴 EVERY COORDINATE IS QUANTISED ONTO A PER-TRIANGLE LATTICE BEFORE IT IS USED. See the note above the corner placement below for why the
    //    gate cannot judge bit-exactly without this. Two properties of the lattice are load-bearing and were each established by measurement:
    //
    //      · THE STEP IS THREE TIMES A POWER OF TWO, so every corner is an integer multiple of Step/3 and the three-corner sum is an exact multiple
    //        of Step. Both the multiply by three and the divide by three are then exact, and all six summation orders agree.
    //      · THE STEP IS SIZED FROM THE TRIANGLE'S REACH, NOT FROM ITS CENTRE. A step scaled to a small centre makes the lattice multiple of a much
    //        larger corner exceed 2^24, past which the multiple is not representable in a float and the corner silently leaves the lattice. This is
    //        the residue that a centre-derived step left behind: 321 of 800000, all of them triangles whose offsets dwarf their centre.
    //
    //    A FIXED step cannot work at all, because once the float spacing at a magnitude exceeds the step, snapping becomes a no-op — at 1e30 a
    //    0.75 cm lattice does nothing. And the underlying hazard is not confined to large coordinates: the naive (c+c+c)/3 == c identity holds for
    //    only ~85% of values at EVERY scale from 1e3 to 1e30. It is a mantissa-headroom problem that merely became visible at the extremes.
    //    Probes: _ClaudeScratch/build/ExtremeMagnitudeCentroidProbe.cpp and ScaledLatticeProbe.cpp (800000/800000 across 1e-3 .. 1e30).
    const auto BuildLatticeStep = [](float Centre, float OffsetA, float OffsetB) -> float
    {
        // The largest coordinate the triangle will contain, with headroom, so that every corner's lattice multiple stays inside 2^24.
        const float Reach = std::fmaxf(std::fabsf(Centre),
                            std::fmaxf(std::fabsf(OffsetA) + std::fabsf(OffsetB), 1.0e-30f)) * 4.0f;
        int Exponent = 0;
        std::frexpf(Reach, &Exponent);
        return std::ldexpf(3.0f, Exponent - 21);
    };

    const auto ToLattice = [](float Value, float Step) -> float
    {
        if (Value == 0.0f || !std::isfinite(Value)) return Value;
        return std::nearbyintf(Value / Step) * Step;
    };

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        float CentreX = 0.0f, CentreY = 0.0f, CentreZ = 0.0f;
        switch (Shape)
        {
            case SceneShape::OriginCentred:
                CentreX = UnitSpread(Generator) * 500.0f;
                CentreY = UnitSpread(Generator) * 500.0f;
                CentreZ = UnitSpread(Generator) * 500.0f;
                break;
            case SceneShape::PositiveOctant:
                CentreX = 900.0f + UnitSpread(Generator) * 100.0f;
                CentreY = 900.0f + UnitSpread(Generator) * 100.0f;
                CentreZ = 900.0f + UnitSpread(Generator) * 100.0f;
                break;
            case SceneShape::NegativeOctant:
                CentreX = -900.0f + UnitSpread(Generator) * 100.0f;
                CentreY = -900.0f + UnitSpread(Generator) * 100.0f;
                CentreZ = -900.0f + UnitSpread(Generator) * 100.0f;
                break;
            case SceneShape::FlatPlane:
                CentreX = UnitSpread(Generator) * 500.0f;
                CentreY = UnitSpread(Generator) * 500.0f;
                CentreZ = 0.0f;                                  // exactly flat, no jitter at all
                break;
            case SceneShape::SinglePoint:
                CentreX = 12.75f; CentreY = -7.5f; CentreZ = 3.0f;   // lattice-friendly, and still a mix of signs
                break;
            case SceneShape::ThinSliver:
                // 📝 The two thin axes quantise to exactly 0 against a lattice sized by the enormous X reach, rather than to a microscopic jitter.
                //    The shape still does its job: a vast extent on X and zero extent on the other two.
                CentreX = UnitSpread(Generator) * 10000.0f;
                CentreY = UnitSpread(Generator) * 0.001f;
                CentreZ = UnitSpread(Generator) * 0.001f;
                break;
            case SceneShape::FarFromOrigin:
                CentreX = 1.0e6f + UnitSpread(Generator) * 1000.0f;
                CentreY = -2.0e6f + UnitSpread(Generator) * 1000.0f;
                CentreZ = 5.0e5f + UnitSpread(Generator) * 1000.0f;
                break;
            case SceneShape::ExtremeMagnitude:
                // 📝 Quantised like every other shape. An earlier revision exempted this one and gave it degenerate triangles instead, on the theory
                //    that no lattice could work past ~1.7e7; that was wrong on both counts — (c+c+c)/3 is inexact at every magnitude, and an
                //    exponent-scaled lattice is exact at every magnitude. The shape is a genuine test again, not a special case.
                CentreX = UnitSpread(Generator) * 1.0e30f;
                CentreY = UnitSpread(Generator) * 1.0e30f;
                CentreZ = UnitSpread(Generator) * 1.0e30f;
                break;
        }

        // 🔴 THE CORNERS MUST LIE ON THE LATTICE, NOT MERELY SUM TO THE CENTRE IN EXACT ARITHMETIC. Two offsets free and the third their negation
        //    makes the mean the centre in REAL arithmetic — but not in float. (c+a) + (c+b) + (c-a-b) does not round to 3c, and which way it rounds
        //    depends on the ASSOCIATION of the adds. The GPU associates differently from this oracle, so ~3% of triangles disagreed in the last bit
        //    and the bit-exact gate reported 104 spurious failures against a shader that was correct all along. Measured, not assumed:
        //    _ClaudeScratch/build/CentroidAssociationProbe.cpp (3.11% inexact, worst delta 4.77e-07).
        //
        //    The remedy is to make the centroid REPRESENTABLE rather than to relax the comparison. With every coordinate on a step of the form
        //    3 * 2^k, each corner is an exact multiple of Step/3, the three-term sum is an exact multiple of Step, and the divide by three is exact
        //    under every one of the six associations. Verified in _ClaudeScratch/build/ScaledLatticeProbe.cpp, 800000/800000 across 1e-3 .. 1e30.
        //    A tolerance would have hidden precisely the bugs this gate exists to catch, so it is not an option.
        // The corners spread proportionally to the centre's own magnitude, so the shape keeps its character at 1e3 and at 1e30 alike. A floor keeps
        // the spread meaningful for shapes centred at or near zero.
        const auto SpreadFor = [&](float Centre) -> float
        {
            return std::fmaxf(std::fabsf(Centre) * 0.01f, 4.0f);
        };

        const float OffsetAX = UnitSpread(Generator) * SpreadFor(CentreX);
        const float OffsetAY = UnitSpread(Generator) * SpreadFor(CentreY);
        const float OffsetAZ = UnitSpread(Generator) * SpreadFor(CentreZ);
        const float OffsetBX = UnitSpread(Generator) * SpreadFor(CentreX);
        const float OffsetBY = UnitSpread(Generator) * SpreadFor(CentreY);
        const float OffsetBZ = UnitSpread(Generator) * SpreadFor(CentreZ);

        // One lattice step per axis, sized from that axis's reach, then everything on that axis quantised onto it.
        const float StepX = BuildLatticeStep(CentreX, OffsetAX, OffsetBX);
        const float StepY = BuildLatticeStep(CentreY, OffsetAY, OffsetBY);
        const float StepZ = BuildLatticeStep(CentreZ, OffsetAZ, OffsetBZ);

        CentreX = ToLattice(CentreX, StepX);
        CentreY = ToLattice(CentreY, StepY);
        CentreZ = ToLattice(CentreZ, StepZ);

        const float LatticeAX = ToLattice(OffsetAX, StepX);
        const float LatticeAY = ToLattice(OffsetAY, StepY);
        const float LatticeAZ = ToLattice(OffsetAZ, StepZ);
        const float LatticeBX = ToLattice(OffsetBX, StepX);
        const float LatticeBY = ToLattice(OffsetBY, StepY);
        const float LatticeBZ = ToLattice(OffsetBZ, StepZ);

        const uint32_t BaseIndex = (uint32_t)OutVertices.size();

        RenderVertex Corner = {};
        Corner.PositionX = CentreX + LatticeAX; Corner.PositionY = CentreY + LatticeAY; Corner.PositionZ = CentreZ + LatticeAZ;
        OutVertices.push_back(Corner);
        Corner.PositionX = CentreX + LatticeBX; Corner.PositionY = CentreY + LatticeBY; Corner.PositionZ = CentreZ + LatticeBZ;
        OutVertices.push_back(Corner);
        Corner.PositionX = CentreX - LatticeAX - LatticeBX;
        Corner.PositionY = CentreY - LatticeAY - LatticeBY;
        Corner.PositionZ = CentreZ - LatticeAZ - LatticeBZ;
        OutVertices.push_back(Corner);

        OutIndices.push_back(BaseIndex + 0u);
        OutIndices.push_back(BaseIndex + 1u);
        OutIndices.push_back(BaseIndex + 2u);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE ORACLE
//------------------------------------------------------------------------------------------------------------------------

// The reference box: a plain linear min/max over the same centroids, computed the SAME way the shader computes them — sum the three corners, then
// divide by three. Any other formulation (averaging pairwise, multiplying by 1/3) rounds differently and would produce spurious mismatches.
//
// 📝 Matching the shader's FORMULA is necessary but was not sufficient. Writing the same expression does not pin down the ASSOCIATION of the two
//    adds, and the two compilers chose differently, which is what produced the first run's 104 spurious failures. What actually makes this oracle
//    sound is that BuildScene quantises its geometry onto a 3 * 2^k lattice, so the sum is exact and every association agrees. The formula is aligned
//    here anyway, so that the oracle stays correct if the lattice is ever relaxed for a new shape.
VolumeBounds ComputeReferenceBounds(const std::vector<RenderVertex>& Vertices,
                                    const std::vector<uint32_t>&     Indices,
                                    uint32_t                         TriangleCount)
{
    VolumeBounds Reference;
    if (TriangleCount == 0)
        return Reference;   // EmptyCondition stays true

    float MinimumX =  std::numeric_limits<float>::infinity();
    float MinimumY =  std::numeric_limits<float>::infinity();
    float MinimumZ =  std::numeric_limits<float>::infinity();
    float MaximumX = -std::numeric_limits<float>::infinity();
    float MaximumY = -std::numeric_limits<float>::infinity();
    float MaximumZ = -std::numeric_limits<float>::infinity();

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const RenderVertex& A = Vertices[Indices[Triangle * 3u + 0u]];
        const RenderVertex& B = Vertices[Indices[Triangle * 3u + 1u]];
        const RenderVertex& C = Vertices[Indices[Triangle * 3u + 2u]];

        const float CentroidX = (A.PositionX + B.PositionX + C.PositionX) / 3.0f;
        const float CentroidY = (A.PositionY + B.PositionY + C.PositionY) / 3.0f;
        const float CentroidZ = (A.PositionZ + B.PositionZ + C.PositionZ) / 3.0f;

        MinimumX = std::min(MinimumX, CentroidX);  MaximumX = std::max(MaximumX, CentroidX);
        MinimumY = std::min(MinimumY, CentroidY);  MaximumY = std::max(MaximumY, CentroidY);
        MinimumZ = std::min(MinimumZ, CentroidZ);  MaximumZ = std::max(MaximumZ, CentroidZ);
    }

    Reference.MinimumX = MinimumX; Reference.MinimumY = MinimumY; Reference.MinimumZ = MinimumZ;
    Reference.MaximumX = MaximumX; Reference.MaximumY = MaximumY; Reference.MaximumZ = MaximumZ;
    Reference.EmptyCondition = false;
    return Reference;
}

// Bit comparison rather than ==, so that a +0.0 / -0.0 divergence is caught rather than silently accepted.
bool ValuesMatchExactly(float Left, float Right)
{
    uint32_t LeftBits, RightBits;
    std::memcpy(&LeftBits,  &Left,  sizeof(LeftBits));
    std::memcpy(&RightBits, &Right, sizeof(RightBits));
    return LeftBits == RightBits;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DEVICE PLUMBING
//------------------------------------------------------------------------------------------------------------------------

uint32_t SelectMemoryTypeIndex(VkPhysicalDevice PhysicalDevice, uint32_t CompatibleTypesBitmask,
                               VkMemoryPropertyFlags RequiredProperties, bool& FoundEnabled)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
    {
        const bool TypeCompatible = (CompatibleTypesBitmask & (1u << Index)) != 0;
        const bool PropertyMatch  = (MemoryProperties.memoryTypes[Index].propertyFlags & RequiredProperties) == RequiredProperties;
        if (TypeCompatible && PropertyMatch) { FoundEnabled = true; return Index; }
    }
    FoundEnabled = false;
    return 0;
}

// A host-visible buffer holding Bytes of the caller's data. Host-visible rather than device-local + staging because this is a validation harness and
// the simpler path has fewer places to be wrong; the shader reads it as a storage buffer either way.
bool CreateFilledBuffer(VulkanHost& Host, const void* SourceData, VkDeviceSize Bytes,
                        VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = Bytes;
    BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);

    bool Found = false;
    const uint32_t TypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Found);
    if (!Found)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = TypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, OutMemory, 0, Bytes, 0, &Mapped) != VK_SUCCESS || Mapped == nullptr)
        return false;
    std::memcpy(Mapped, SourceData, (size_t)Bytes);
    vkUnmapMemory(Host.Device, OutMemory);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          ONE CASE
//------------------------------------------------------------------------------------------------------------------------

uint32_t FailureTally = 0;
uint32_t CaseTally    = 0;

// Run the reduce over one generated scene and judge the box. Returns false on a failure of any kind — including a plumbing failure, which must not be
// reported as a pass.
bool ExecuteCase(VulkanHost& Host, VkCommandPool CommandPool, SceneShape Shape, uint32_t TriangleCount, uint32_t Seed)
{
    ++CaseTally;

    std::vector<RenderVertex> Vertices;
    std::vector<uint32_t>     Indices;
    BuildScene(Shape, TriangleCount, Seed, Vertices, Indices);

    const VolumeBounds Reference = ComputeReferenceBounds(Vertices, Indices, TriangleCount);

    VkBuffer VertexBuffer = VK_NULL_HANDLE, IndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory VertexMemory = VK_NULL_HANDLE, IndexMemory = VK_NULL_HANDLE;

    const VkDeviceSize VertexBytes = (VkDeviceSize)Vertices.size() * sizeof(RenderVertex);
    const VkDeviceSize IndexBytes  = (VkDeviceSize)Indices.size()  * sizeof(uint32_t);

    if (!CreateFilledBuffer(Host, Vertices.data(), VertexBytes, VertexBuffer, VertexMemory) ||
        !CreateFilledBuffer(Host, Indices.data(),  IndexBytes,  IndexBuffer,  IndexMemory))
    {
        std::printf("  %-17s %9u  GEOMETRY UPLOAD FAILED\n", DescribeShape(Shape), TriangleCount);
        ++FailureTally;
        return false;
    }

    VolumeBoundsSubmission Bounds;
    bool Passed = false;

    if (!InitializeVolumeBoundsSubmission(Bounds, Host, ShaderDirectoryPath))
    {
        std::printf("  %-17s %9u  INITIALIZE FAILED\n", DescribeShape(Shape), TriangleCount);
        ++FailureTally;
    }
    else if (!BindVolumeBoundsGeometry(Bounds, VertexBuffer, VertexBytes, IndexBuffer, IndexBytes, TriangleCount))
    {
        std::printf("  %-17s %9u  GEOMETRY BIND FAILED\n", DescribeShape(Shape), TriangleCount);
        ++FailureTally;
    }
    else
    {
        // Record and submit the reduce.
        VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandAllocate.commandPool        = CommandPool;
        CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandAllocate.commandBufferCount = 1;
        VkCommandBuffer ReduceCommand = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &ReduceCommand);

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(ReduceCommand, &BeginInformation);
        RecordVolumeBoundsReduce(Bounds, ReduceCommand);
        vkEndCommandBuffer(ReduceCommand);

        VkFence Fence = VK_NULL_HANDLE;
        VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence);

        VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        SubmitInformation.commandBufferCount = 1;
        SubmitInformation.pCommandBuffers    = &ReduceCommand;
        vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence);

        // 🔴 A bounded wait, not UINT64_MAX: a hang must be a REPORTED OUTCOME rather than an unattended process that never returns.
        const VkResult WaitResult = vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, 30ull * 1000ull * 1000ull * 1000ull);
        vkDestroyFence(Host.Device, Fence, Host.Allocator);
        vkFreeCommandBuffers(Host.Device, CommandPool, 1, &ReduceCommand);

        if (WaitResult != VK_SUCCESS)
        {
            std::printf("  %-17s %9u  DISPATCH TIMED OUT\n", DescribeShape(Shape), TriangleCount);
            ++FailureTally;
        }
        else
        {
            VolumeBounds Result;
            if (!RetrieveVolumeBounds(Bounds, CommandPool, Result))
            {
                std::printf("  %-17s %9u  READBACK FAILED\n", DescribeShape(Shape), TriangleCount);
                ++FailureTally;
            }
            else
            {
                const bool EmptyAgrees = (Result.EmptyCondition == Reference.EmptyCondition);
                bool BoxAgrees = true;
                if (!Reference.EmptyCondition)
                {
                    BoxAgrees =
                        ValuesMatchExactly(Result.MinimumX, Reference.MinimumX) &&
                        ValuesMatchExactly(Result.MinimumY, Reference.MinimumY) &&
                        ValuesMatchExactly(Result.MinimumZ, Reference.MinimumZ) &&
                        ValuesMatchExactly(Result.MaximumX, Reference.MaximumX) &&
                        ValuesMatchExactly(Result.MaximumY, Reference.MaximumY) &&
                        ValuesMatchExactly(Result.MaximumZ, Reference.MaximumZ);
                }

                Passed = EmptyAgrees && BoxAgrees;
                if (!Passed) ++FailureTally;

                std::printf("  %-17s %9u  empty %s  box %s  %s\n",
                            DescribeShape(Shape), TriangleCount,
                            EmptyAgrees ? "y" : "N",
                            BoxAgrees   ? "y" : "N",
                            Passed ? "PASS" : "FAIL");

                if (!Passed && !Reference.EmptyCondition)
                {
                    std::printf("      expected min (%.9g %.9g %.9g) max (%.9g %.9g %.9g)\n",
                                Reference.MinimumX, Reference.MinimumY, Reference.MinimumZ,
                                Reference.MaximumX, Reference.MaximumY, Reference.MaximumZ);
                    std::printf("      actual   min (%.9g %.9g %.9g) max (%.9g %.9g %.9g)\n",
                                Result.MinimumX, Result.MinimumY, Result.MinimumZ,
                                Result.MaximumX, Result.MaximumY, Result.MaximumZ);
                }
            }
        }
    }

    FinalizeVolumeBoundsSubmission(Bounds);
    vkDestroyBuffer(Host.Device, VertexBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, VertexMemory, Host.Allocator);
    vkDestroyBuffer(Host.Device, IndexBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, IndexMemory, Host.Allocator);
    return Passed;
}

// 🔴 The reseed check: run the SAME submission twice over two different scenes and confirm the second result reflects only the second scene. If the
//    accumulators are not reseeded the second box is the UNION of both — well-formed, larger, and undetectable by any single-run test. This is the
//    LBVH silent-wrong the plan warns about, in its bounds-reduce form.
void ExecuteReseedCheck(VulkanHost& Host, VkCommandPool CommandPool)
{
    std::printf("\n---- reseed across rebuilds ");
    for (int Dash = 0; Dash < 66; ++Dash) std::printf("-");
    std::printf("\n");

    // A large scene, then a small one entirely inside it. If the box is not reseeded the second run returns the LARGE box.
    std::vector<RenderVertex> WideVertices,  NarrowVertices;
    std::vector<uint32_t>     WideIndices,   NarrowIndices;
    BuildScene(SceneShape::OriginCentred, 4096u, 11u, WideVertices, WideIndices);
    BuildScene(SceneShape::SinglePoint,   4096u, 12u, NarrowVertices, NarrowIndices);

    const VolumeBounds NarrowReference = ComputeReferenceBounds(NarrowVertices, NarrowIndices, 4096u);

    VolumeBoundsSubmission Bounds;
    if (!InitializeVolumeBoundsSubmission(Bounds, Host, ShaderDirectoryPath))
    {
        std::printf("  INITIALIZE FAILED\n");
        ++FailureTally;
        ++CaseTally;
        return;
    }

    struct SceneBuffers { VkBuffer Vertex, Index; VkDeviceMemory VertexMemory, IndexMemory; VkDeviceSize VertexBytes, IndexBytes; };
    SceneBuffers Wide = {}, Narrow = {};
    Wide.VertexBytes   = (VkDeviceSize)WideVertices.size()   * sizeof(RenderVertex);
    Wide.IndexBytes    = (VkDeviceSize)WideIndices.size()    * sizeof(uint32_t);
    Narrow.VertexBytes = (VkDeviceSize)NarrowVertices.size() * sizeof(RenderVertex);
    Narrow.IndexBytes  = (VkDeviceSize)NarrowIndices.size()  * sizeof(uint32_t);
    CreateFilledBuffer(Host, WideVertices.data(),   Wide.VertexBytes,   Wide.Vertex,   Wide.VertexMemory);
    CreateFilledBuffer(Host, WideIndices.data(),    Wide.IndexBytes,    Wide.Index,    Wide.IndexMemory);
    CreateFilledBuffer(Host, NarrowVertices.data(), Narrow.VertexBytes, Narrow.Vertex, Narrow.VertexMemory);
    CreateFilledBuffer(Host, NarrowIndices.data(),  Narrow.IndexBytes,  Narrow.Index,  Narrow.IndexMemory);

    auto RunOnce = [&](const SceneBuffers& Scene, VolumeBounds& OutResult)
    {
        BindVolumeBoundsGeometry(Bounds, Scene.Vertex, Scene.VertexBytes, Scene.Index, Scene.IndexBytes, 4096u);

        VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandAllocate.commandPool        = CommandPool;
        CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandAllocate.commandBufferCount = 1;
        VkCommandBuffer ReduceCommand = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &ReduceCommand);

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(ReduceCommand, &BeginInformation);
        RecordVolumeBoundsReduce(Bounds, ReduceCommand);
        vkEndCommandBuffer(ReduceCommand);

        VkFence Fence = VK_NULL_HANDLE;
        VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence);
        VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        SubmitInformation.commandBufferCount = 1;
        SubmitInformation.pCommandBuffers    = &ReduceCommand;
        vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence);
        vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, 30ull * 1000ull * 1000ull * 1000ull);
        vkDestroyFence(Host.Device, Fence, Host.Allocator);
        vkFreeCommandBuffers(Host.Device, CommandPool, 1, &ReduceCommand);

        // vkDeviceWaitIdle before the next bind: BindVolumeBoundsGeometry rewrites the descriptor set, which is undefined while a command buffer
        // that bound it is still executing.
        vkDeviceWaitIdle(Host.Device);
        RetrieveVolumeBounds(Bounds, CommandPool, OutResult);
    };

    VolumeBounds WideResult, NarrowResult;
    RunOnce(Wide,   WideResult);
    RunOnce(Narrow, NarrowResult);

    ++CaseTally;
    const bool Reseeded =
        ValuesMatchExactly(NarrowResult.MinimumX, NarrowReference.MinimumX) &&
        ValuesMatchExactly(NarrowResult.MinimumY, NarrowReference.MinimumY) &&
        ValuesMatchExactly(NarrowResult.MinimumZ, NarrowReference.MinimumZ) &&
        ValuesMatchExactly(NarrowResult.MaximumX, NarrowReference.MaximumX) &&
        ValuesMatchExactly(NarrowResult.MaximumY, NarrowReference.MaximumY) &&
        ValuesMatchExactly(NarrowResult.MaximumZ, NarrowReference.MaximumZ);
    if (!Reseeded) ++FailureTally;

    std::printf("  wide box   min (%9.2f %9.2f %9.2f) max (%9.2f %9.2f %9.2f)\n",
                WideResult.MinimumX, WideResult.MinimumY, WideResult.MinimumZ,
                WideResult.MaximumX, WideResult.MaximumY, WideResult.MaximumZ);
    std::printf("  narrow box min (%9.2f %9.2f %9.2f) max (%9.2f %9.2f %9.2f)\n",
                NarrowResult.MinimumX, NarrowResult.MinimumY, NarrowResult.MinimumZ,
                NarrowResult.MaximumX, NarrowResult.MaximumY, NarrowResult.MaximumZ);
    std::printf("  second run reflects only the second scene   %s\n", Reseeded ? "PASS" : "FAIL (accumulators not reseeded)");

    FinalizeVolumeBoundsSubmission(Bounds);
    vkDestroyBuffer(Host.Device, Wide.Vertex,   Host.Allocator);  vkFreeMemory(Host.Device, Wide.VertexMemory,   Host.Allocator);
    vkDestroyBuffer(Host.Device, Wide.Index,    Host.Allocator);  vkFreeMemory(Host.Device, Wide.IndexMemory,    Host.Allocator);
    vkDestroyBuffer(Host.Device, Narrow.Vertex, Host.Allocator);  vkFreeMemory(Host.Device, Narrow.VertexMemory, Host.Allocator);
    vkDestroyBuffer(Host.Device, Narrow.Index,  Host.Allocator);  vkFreeMemory(Host.Device, Narrow.IndexMemory,  Host.Allocator);
}

} // namespace

int main(int ArgumentCount, char** ArgumentValues)
{
    bool StressEnabled = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
        if (std::strcmp(ArgumentValues[Index], "--stress") == 0) StressEnabled = true;

    std::printf("==== VolumeBoundsReduce validation ====\n\n");

    VulkanHost Host;
    if (!InitializeVulkanHost(Host, nullptr, 0))
    {
        std::printf("headless Vulkan host creation FAILED\n");
        return 1;
    }

    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(Host.Device, &PoolInformation, Host.Allocator, &CommandPool) != VK_SUCCESS)
    {
        std::printf("command pool creation FAILED\n");
        FinalizeVulkanHost(Host);
        return 1;
    }

    // 📝 Counts chosen to straddle every tile edge of the 256-wide workgroup: a partial final tile is where the identity seeding is exercised, and
    //    an exact multiple is where an off-by-one in the guard would hide.
    const uint32_t TriangleCounts[] = { 1u, 2u, 3u, 255u, 256u, 257u, 511u, 512u, 513u, 1023u, 1024u, 1025u, 65535u, 65536u, 200000u };
    const SceneShape Shapes[] = {
        SceneShape::OriginCentred, SceneShape::PositiveOctant, SceneShape::NegativeOctant, SceneShape::FlatPlane,
        SceneShape::SinglePoint,   SceneShape::ThinSliver,     SceneShape::FarFromOrigin,  SceneShape::ExtremeMagnitude,
    };

    std::printf("---- correctness suite ");
    for (int Dash = 0; Dash < 71; ++Dash) std::printf("-");
    std::printf("\n");

    uint32_t Seed = 1000u;
    for (SceneShape Shape : Shapes)
        for (uint32_t Count : TriangleCounts)
            ExecuteCase(Host, CommandPool, Shape, Count, Seed++);

    ExecuteReseedCheck(Host, CommandPool);

    if (StressEnabled)
    {
        std::printf("\n---- stress suite ");
        for (int Dash = 0; Dash < 76; ++Dash) std::printf("-");
        std::printf("\n");

        std::printf("  [repetition] 12 runs, origin-centred, 500,000 triangles\n");
        for (int Run = 0; Run < 12; ++Run)
            ExecuteCase(Host, CommandPool, SceneShape::OriginCentred, 500000u, 5000u + (uint32_t)Run);

        std::printf("  [randomized] 24 runs, random shape and count\n");
        std::mt19937 Generator(4242u);
        std::uniform_int_distribution<uint32_t> CountSpread(1u, 300000u);
        for (int Run = 0; Run < 24; ++Run)
        {
            const SceneShape Shape = Shapes[Run % (int)(sizeof(Shapes) / sizeof(Shapes[0]))];
            ExecuteCase(Host, CommandPool, Shape, CountSpread(Generator), 9000u + (uint32_t)Run);
        }
    }

    std::printf("\n==== %s : %u cases, %u failure%s ====\n",
                FailureTally == 0 ? "PASS" : "FAIL", CaseTally, FailureTally, FailureTally == 1 ? "" : "s");

    vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
    FinalizeVulkanHost(Host);
    return FailureTally == 0 ? 0 : 1;
}
