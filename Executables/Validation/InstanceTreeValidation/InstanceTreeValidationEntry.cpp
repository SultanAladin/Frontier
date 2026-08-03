/*==============================================================================================================================================
                                                     INSTANCETREEVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for the TOP-level (TLAS) build's last two dispatches: InstanceTreeBuild.comp (the Karras radix tree) and InstanceTreeRefit.comp
//    (the bottom-up bounds climb). It runs the WHOLE four-dispatch chain on a real device — reduce -> Morton -> radix sort -> tree -> refit — because
//    the tree is only meaningful over keys the sort actually produced, and driving it with hand-fed keys would validate a pipeline the engine does
//    not run.
//
//    🔴 STRUCTURE IS CHECKED SEPARATELY FROM BOUNDS, AND BOTH ARE NECESSARY. A tree can be a perfectly well-formed binary tree over the wrong leaves,
//       and it can have the right shape with boxes short by one subtree. The structural assertions (one parent each, connected, every leaf once, no
//       cycles, leaf order == sorted order) catch the first; the containment assertions catch the second. Neither implies the other, and the two
//       failure signatures in the engine are completely different — a wrong shape is a persistent miss, a short box is an intermittent flicker.
//
//    ⚠️ THE ARRIVAL-COUNTER RACE IS THE REASON THE REPETITION SUITE EXISTS. A refit that lets both lanes climb, or that reads a sibling's box before
//       it lands, is right in most frames. One run of one scene cannot see it. --stress runs the same scene many times and fails if ANY run differs,
//       which is the only shape of check that can catch a race whose usual outcome is the correct answer.
//
//    📝 Headless on purpose: InitializeVulkanHost never creates or queries a surface, so a zero extension count yields a compute device, no window.

#define _CRT_SECURE_NO_WARNINGS

#include "Graphics/Acceleration/InstanceBoundsSubmission.h"
#include "Graphics/Acceleration/InstanceTreeSubmission.h"
#include "Graphics/Acceleration/RadixSortSubmission.h"
#include "Graphics/Acceleration/GeometryArenaSubmission.h"
#include "Graphics/Acceleration/GeometryTreeBuild.h"
#include "Graphics/Scene/SuzanneScene.h"
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

constexpr uint32_t WordsPerNode = InstanceTreeWordsPerNode;
constexpr uint32_t LeafFlag     = 0xFFFFu;

// Where the shader modules land — ShaderPlan.ps1 writes each .spv beside its source.
const char* const ShaderDirectory = "Internal/Graphics/Acceleration/Shaders";

//------------------------------------------------------------------------------------------------------------------------
//                                                          SMALL GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

struct LocalBox
{
    float MinimumX = 0.0f, MinimumY = 0.0f, MinimumZ = 0.0f;
    float MaximumX = 0.0f, MaximumY = 0.0f, MaximumZ = 0.0f;
};

struct Matrix4
{
    float M[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // column-major, matching SuzanneSceneInstance::Model
};

void TransformPoint(const Matrix4& Transform, float X, float Y, float Z, float& OutX, float& OutY, float& OutZ)
{
    OutX = Transform.M[0] * X + Transform.M[4] * Y + Transform.M[8]  * Z + Transform.M[12];
    OutY = Transform.M[1] * X + Transform.M[5] * Y + Transform.M[9]  * Z + Transform.M[13];
    OutZ = Transform.M[2] * X + Transform.M[6] * Y + Transform.M[10] * Z + Transform.M[14];
}

struct WorldBox
{
    float MinimumX =  std::numeric_limits<float>::infinity();
    float MinimumY =  std::numeric_limits<float>::infinity();
    float MinimumZ =  std::numeric_limits<float>::infinity();
    float MaximumX = -std::numeric_limits<float>::infinity();
    float MaximumY = -std::numeric_limits<float>::infinity();
    float MaximumZ = -std::numeric_limits<float>::infinity();

    bool EmptyCondition() const { return MinimumX > MaximumX; }

    void Absorb(float X, float Y, float Z)
    {
        MinimumX = std::min(MinimumX, X); MaximumX = std::max(MaximumX, X);
        MinimumY = std::min(MinimumY, Y); MaximumY = std::max(MaximumY, Y);
        MinimumZ = std::min(MinimumZ, Z); MaximumZ = std::max(MaximumZ, Z);
    }

    void Absorb(const WorldBox& Other)
    {
        if (Other.EmptyCondition()) return;
        MinimumX = std::min(MinimumX, Other.MinimumX); MaximumX = std::max(MaximumX, Other.MaximumX);
        MinimumY = std::min(MinimumY, Other.MinimumY); MaximumY = std::max(MaximumY, Other.MaximumY);
        MinimumZ = std::min(MinimumZ, Other.MinimumZ); MaximumZ = std::max(MaximumZ, Other.MaximumZ);
    }
};

// 🔴 THE EIGHT-CORNER RULE, which the refit's leaf pass must match exactly. Two corners is correct only without rotation and is too SMALL under it —
//    a box too small means a ray MISSES geometry it should hit, silently and view-dependently.
WorldBox TransformBoxByEightCorners(const LocalBox& Local, const Matrix4& Transform)
{
    WorldBox World;
    for (uint32_t Corner = 0; Corner < 8u; ++Corner)
    {
        const float LocalX = (Corner & 1u) ? Local.MaximumX : Local.MinimumX;
        const float LocalY = (Corner & 2u) ? Local.MaximumY : Local.MinimumY;
        const float LocalZ = (Corner & 4u) ? Local.MaximumZ : Local.MinimumZ;
        float WorldX, WorldY, WorldZ;
        TransformPoint(Transform, LocalX, LocalY, LocalZ, WorldX, WorldY, WorldZ);
        World.Absorb(WorldX, WorldY, WorldZ);
    }
    return World;
}

bool ValuesMatchExactly(float Left, float Right)
{
    if (std::isinf(Left) && std::isinf(Right)) return std::signbit(Left) == std::signbit(Right);
    return Left == Right;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Shapes chosen for how they stress the TREE, which is a different list from the bounds gate's. What matters here is the DISTRIBUTION OF MORTON
//    CODES the tree is built over, and above all how many of them collide.
enum class SceneShape
{
    ScatteredGrid,      // ordinary well-spread instances: the baseline
    RotatedRing,        // every instance rotated, so leaf boxes are genuinely oblique
    IdenticalCentroids, // 🔴 EVERY Morton code identical: the duplicate-key case that HANGS without the index tiebreak
    TwoClusters,        // two dense clumps far apart: a deep tree with a shallow root split
    CoplanarRow,        // all centroids on a plane: one Morton axis constant, so codes share long prefixes
    DenseCell,          // many instances inside a single Morton cell: heavy but not total duplication
    OutOfRangeOrdinals, // some instances dropped by the ordinal test: their leaves must be inert, not scene-swallowing
    NegativeOctant      // entirely negative coordinates
};

const char* DescribeShape(SceneShape Shape)
{
    switch (Shape)
    {
        case SceneShape::ScatteredGrid:      return "scattered-grid";
        case SceneShape::RotatedRing:        return "rotated-ring";
        case SceneShape::IdenticalCentroids: return "identical-centroids";
        case SceneShape::TwoClusters:        return "two-clusters";
        case SceneShape::CoplanarRow:        return "coplanar-row";
        case SceneShape::DenseCell:          return "dense-cell";
        case SceneShape::OutOfRangeOrdinals: return "out-of-range";
        case SceneShape::NegativeOctant:     return "negative-octant";
    }
    return "unknown";
}

struct SyntheticScene
{
    std::vector<SuzanneSceneInstance> Instances;
    std::vector<GeometryArenaSlice>   Slices;
    std::vector<uint32_t>             NodeWords;
    std::vector<LocalBox>             MeshBoxes;    // parallel to Slices, for the oracle
    uint32_t                          SliceCount = 0;
};

// 🔴 EVERY VALUE HERE IS EXACTLY REPRESENTABLE. Coordinates are multiples of 0.25 and scales are powers of two, so the corner transform and the
//    min/max reduction the refit performs are exact — which is what lets the box comparison below be bit-exact rather than tolerance-based.
float QuarterLattice(std::mt19937& Generator, float HalfSpread)
{
    std::uniform_int_distribution<int> Steps(-(int)(HalfSpread * 4.0f), (int)(HalfSpread * 4.0f));
    return (float)Steps(Generator) * 0.25f;
}

// A signed permutation matrix times a power-of-two scale, plus a lattice translation. Always exactly representable, and a genuine rotation/reflection.
Matrix4 ComposeTransform(uint32_t PermutationChoice, float Scale, float TranslateX, float TranslateY, float TranslateZ)
{
    static const uint32_t Permutations[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    const uint32_t* Order = Permutations[PermutationChoice % 6u];

    const float Signs[3] = { (PermutationChoice & 8u)  ? -1.0f : 1.0f,
                             (PermutationChoice & 16u) ? -1.0f : 1.0f,
                             (PermutationChoice & 32u) ? -1.0f : 1.0f };

    Matrix4 Result;
    for (uint32_t Index = 0; Index < 16u; ++Index) Result.M[Index] = 0.0f;
    for (uint32_t SourceAxis = 0; SourceAxis < 3u; ++SourceAxis)
        Result.M[SourceAxis * 4u + Order[SourceAxis]] = Signs[SourceAxis] * Scale;
    Result.M[12] = TranslateX;
    Result.M[13] = TranslateY;
    Result.M[14] = TranslateZ;
    Result.M[15] = 1.0f;
    return Result;
}

void BuildSyntheticScene(SceneShape Shape, uint32_t InstanceCount, uint32_t Seed, SyntheticScene& Out)
{
    Out = SyntheticScene();
    std::mt19937 Generator(Seed);

    // ─── the meshes ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    const uint32_t MeshCount = 4u;
    for (uint32_t Mesh = 0; Mesh < MeshCount; ++Mesh)
    {
        LocalBox Box;
        const float HalfExtent = (float)(1u << (Mesh % 4u));      // 1, 2, 4, 8 — exact
        Box.MinimumX = -HalfExtent;         Box.MaximumX = HalfExtent;
        Box.MinimumY = -HalfExtent * 0.5f;  Box.MaximumY = HalfExtent * 0.5f;
        Box.MinimumZ = -HalfExtent * 0.25f; Box.MaximumZ = HalfExtent * 0.25f;
        Out.MeshBoxes.push_back(Box);
    }

    // ─── the arena: slice table + node words ─────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 NodeOffset is in WORDS. Each mesh gets a different node count so the offsets are non-trivial; a table whose every NodeOffset were 0 would
    //    let an ordinal-lookup bug pass unnoticed.
    for (uint32_t Mesh = 0; Mesh < MeshCount; ++Mesh)
    {
        const uint32_t NodeCount  = 1u + Mesh;
        const uint32_t NodeOffset = (uint32_t)Out.NodeWords.size();

        GeometryArenaSlice Slice = {};
        Slice.NodeOffset = NodeOffset;
        Slice.NodeCount  = NodeCount;
        Out.Slices.push_back(Slice);

        Out.NodeWords.resize((size_t)NodeOffset + (size_t)NodeCount * WordsPerNode, 0u);

        const LocalBox& Box = Out.MeshBoxes[Mesh];
        const float RootValues[6] = { Box.MinimumX, Box.MinimumY, Box.MinimumZ, Box.MaximumX, Box.MaximumY, Box.MaximumZ };
        for (uint32_t Word = 0; Word < 6u; ++Word)
            std::memcpy(&Out.NodeWords[NodeOffset + Word], &RootValues[Word], sizeof(uint32_t));

        // 📝 POISON, not zeros, in the non-root words: a shader that mistook NodeOffset for a node index would read these, and a zero box would look
        //    like a plausible degenerate result while this reads as a wild coordinate no comparison can forgive.
        for (uint32_t Word = 6u; Word < NodeCount * WordsPerNode; ++Word)
            Out.NodeWords[NodeOffset + Word] = 0x7F7FFFFFu;
    }
    Out.SliceCount = MeshCount;

    // ─── the instances ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    for (uint32_t Index = 0; Index < InstanceCount; ++Index)
    {
        SuzanneSceneInstance Instance;
        Instance.PartitionId = Index;
        Instance.MaterialId  = 0;
        Instance.MeshOrdinal = Index % MeshCount;

        uint32_t PermutationChoice = 0;
        float    Scale = 1.0f;
        float    TranslateX = 0.0f, TranslateY = 0.0f, TranslateZ = 0.0f;

        switch (Shape)
        {
            case SceneShape::ScatteredGrid:
                TranslateX = QuarterLattice(Generator, 256.0f);
                TranslateY = QuarterLattice(Generator, 256.0f);
                TranslateZ = QuarterLattice(Generator, 256.0f);
                break;

            case SceneShape::RotatedRing:
                PermutationChoice = 1u + (Index % 5u) + ((Index % 7u) << 3u);
                TranslateX = QuarterLattice(Generator, 128.0f);
                TranslateY = QuarterLattice(Generator, 128.0f);
                TranslateZ = QuarterLattice(Generator, 128.0f);
                break;

            case SceneShape::IdenticalCentroids:
                // 🔴 THE HANG CASE. Every instance at the same place with the same mesh, so every Morton code is identical and δ over the raw codes
                //    would be infinite for every candidate split. This case passing at all is what proves the index tiebreak works; it passing at
                //    65,536 instances is what proves it works at a depth where a linear fallback would time out.
                Instance.MeshOrdinal = 0u;
                TranslateX = 12.0f; TranslateY = -6.0f; TranslateZ = 2.0f;
                break;

            case SceneShape::TwoClusters:
                // Two tight clumps far apart: the root splits on a high bit and both subtrees are deep. Exercises a long climb in the refit.
                if ((Index & 1u) == 0u)
                {
                    TranslateX = -400.0f + QuarterLattice(Generator, 2.0f);
                    TranslateY = -400.0f + QuarterLattice(Generator, 2.0f);
                    TranslateZ = -400.0f + QuarterLattice(Generator, 2.0f);
                }
                else
                {
                    TranslateX = 400.0f + QuarterLattice(Generator, 2.0f);
                    TranslateY = 400.0f + QuarterLattice(Generator, 2.0f);
                    TranslateZ = 400.0f + QuarterLattice(Generator, 2.0f);
                }
                break;

            case SceneShape::CoplanarRow:
                TranslateX = QuarterLattice(Generator, 300.0f);
                TranslateY = QuarterLattice(Generator, 300.0f);
                TranslateZ = 0.0f;
                break;

            case SceneShape::DenseCell:
                // Heavy but not total collision: the spread is far below one Morton bucket of the scene extent, so most codes repeat and a few do not.
                TranslateX = 100.0f + QuarterLattice(Generator, 1.0f);
                TranslateY = 100.0f + QuarterLattice(Generator, 1.0f);
                TranslateZ = 100.0f + QuarterLattice(Generator, 1.0f);
                break;

            case SceneShape::OutOfRangeOrdinals:
                if ((Index % 3u) == 2u)
                    Instance.MeshOrdinal = MeshCount + (Index % 17u);
                TranslateX = QuarterLattice(Generator, 150.0f);
                TranslateY = QuarterLattice(Generator, 150.0f);
                TranslateZ = QuarterLattice(Generator, 150.0f);
                break;

            case SceneShape::NegativeOctant:
                TranslateX = -512.0f + QuarterLattice(Generator, 64.0f);
                TranslateY = -512.0f + QuarterLattice(Generator, 64.0f);
                TranslateZ = -512.0f + QuarterLattice(Generator, 64.0f);
                break;
        }

        const Matrix4 Transform = ComposeTransform(PermutationChoice, Scale, TranslateX, TranslateY, TranslateZ);
        std::memcpy(Instance.Model, Transform.M, sizeof(Transform.M));
        Out.Instances.push_back(Instance);
    }
}

// The oracle for one leaf: the eight-corner world box of the instance, or an EMPTY box when the ordinal test drops it.
WorldBox ReferenceLeafBox(const SyntheticScene& Scene, uint32_t InstanceIndex)
{
    const SuzanneSceneInstance& Instance = Scene.Instances[InstanceIndex];
    if (Instance.MeshOrdinal >= Scene.SliceCount)
        return WorldBox();                       // inverted / empty — the dropped leaf

    Matrix4 Transform;
    std::memcpy(Transform.M, Instance.Model, sizeof(Transform.M));
    return TransformBoxByEightCorners(Scene.MeshBoxes[Instance.MeshOrdinal], Transform);
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

// A host-visible storage buffer, for the scene inputs the harness feeds in. Simpler than device-local + staging, and this is a harness.
bool CreateHostBufferWithUsage(VulkanHost& Host, const void* SourceData, VkDeviceSize Bytes,
                               VkBufferUsageFlags Usage, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (Bytes == 0) return false;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = Bytes;
    BufferInformation.usage       = Usage;
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

    if (SourceData != nullptr)
    {
        void* Mapped = nullptr;
        if (vkMapMemory(Host.Device, OutMemory, 0, Bytes, 0, &Mapped) != VK_SUCCESS || Mapped == nullptr)
            return false;
        std::memcpy(Mapped, SourceData, (size_t)Bytes);
        vkUnmapMemory(Host.Device, OutMemory);
    }
    return true;
}

// The scene-upload form: a plain host-visible storage buffer, which is what every scene buffer here is.
bool CreateHostBuffer(VulkanHost& Host, const void* SourceData, VkDeviceSize Bytes,
                      VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    return CreateHostBufferWithUsage(Host, SourceData, Bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, OutBuffer, OutMemory);
}

// Read Count uints back out of a host-visible buffer's memory.
bool ReadHostBuffer(VulkanHost& Host, VkDeviceMemory Memory, uint32_t Count, uint32_t* OutValues)
{
    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Memory, 0, (VkDeviceSize)Count * sizeof(uint32_t), 0, &Mapped) != VK_SUCCESS || Mapped == nullptr)
        return false;
    std::memcpy(OutValues, Mapped, (size_t)Count * sizeof(uint32_t));
    vkUnmapMemory(Host.Device, Memory);
    return true;
}

// Submit a recorded command buffer and wait, bounded. Returns false on timeout — a hang must be a reported OUTCOME, not an unattended process, and
// the duplicate-key case is precisely a hang candidate.
bool SubmitAndWait(VulkanHost& Host, VkCommandPool CommandPool, VkCommandBuffer Command)
{
    VkFence Fence = VK_NULL_HANDLE;
    VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence);

    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    SubmitInformation.commandBufferCount = 1;
    SubmitInformation.pCommandBuffers    = &Command;
    vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence);

    const VkResult WaitResult = vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, 30ull * 1000ull * 1000ull * 1000ull);
    vkDestroyFence(Host.Device, Fence, Host.Allocator);
    vkFreeCommandBuffers(Host.Device, CommandPool, 1, &Command);
    return WaitResult == VK_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ONE CASE
//------------------------------------------------------------------------------------------------------------------------

uint32_t FailureTally = 0;
uint32_t CaseTally    = 0;

struct CaseBuffers
{
    VkBuffer       Instance = VK_NULL_HANDLE, Slice = VK_NULL_HANDLE, Node = VK_NULL_HANDLE;
    VkDeviceMemory InstanceMemory = VK_NULL_HANDLE, SliceMemory = VK_NULL_HANDLE, NodeMemory = VK_NULL_HANDLE;
    VkDeviceSize   InstanceBytes = 0, SliceBytes = 0, NodeBytes = 0;

    // Host-visible destination for the pre-sort Morton snapshot. Host-visible rather than device-local + staging because it is read once per run by
    // the harness and never by a shader, so the staging round trip would buy nothing.
    VkBuffer       MortonSnapshot       = VK_NULL_HANDLE;
    VkDeviceMemory MortonSnapshotMemory = VK_NULL_HANDLE;
    VkDeviceSize   MortonSnapshotBytes  = 0;
};

void DestroyCaseBuffers(VulkanHost& Host, CaseBuffers& Buffers)
{
    const auto Drop = [&](VkBuffer& Buffer, VkDeviceMemory& Memory)
    {
        if (Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Buffer, Host.Allocator);
        if (Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Memory, Host.Allocator);
        Buffer = VK_NULL_HANDLE;
        Memory = VK_NULL_HANDLE;
    };
    Drop(Buffers.Instance,       Buffers.InstanceMemory);
    Drop(Buffers.Slice,          Buffers.SliceMemory);
    Drop(Buffers.Node,           Buffers.NodeMemory);
    Drop(Buffers.MortonSnapshot, Buffers.MortonSnapshotMemory);
}

// The whole chain over one scene, with the resulting tree read back. Returns false when the device work itself failed (which is a case failure, and
// distinct from the tree being wrong).
bool ExecuteChain(VulkanHost&            Host,
                  VkCommandPool          CommandPool,
                  const SyntheticScene&  Scene,
                  std::vector<uint32_t>& OutNodeWords,
                  std::vector<uint32_t>& OutParents,
                  std::vector<uint32_t>& OutSortedKeys,
                  std::vector<uint32_t>& OutSortedPayloads,
                  std::vector<uint32_t>& OutMortonKeys,
                  const char*&           OutFailureReason)
{
    OutFailureReason = nullptr;
    const uint32_t InstanceCount = (uint32_t)Scene.Instances.size();

    CaseBuffers Buffers;
    Buffers.InstanceBytes = (VkDeviceSize)Scene.Instances.size() * sizeof(SuzanneSceneInstance);
    Buffers.SliceBytes    = (VkDeviceSize)Scene.Slices.size()    * sizeof(GeometryArenaSlice);
    Buffers.NodeBytes     = (VkDeviceSize)Scene.NodeWords.size() * sizeof(uint32_t);

    const auto Abandon = [&](const char* Reason) -> bool
    {
        OutFailureReason = Reason;
        DestroyCaseBuffers(Host, Buffers);
        return false;
    };

    Buffers.MortonSnapshotBytes = (VkDeviceSize)InstanceCount * sizeof(uint32_t);

    if (!CreateHostBuffer(Host, Scene.Instances.data(), Buffers.InstanceBytes, Buffers.Instance, Buffers.InstanceMemory) ||
        !CreateHostBuffer(Host, Scene.Slices.data(),    Buffers.SliceBytes,    Buffers.Slice,    Buffers.SliceMemory) ||
        !CreateHostBuffer(Host, Scene.NodeWords.data(), Buffers.NodeBytes,     Buffers.Node,     Buffers.NodeMemory) ||
        !CreateHostBufferWithUsage(Host, nullptr, Buffers.MortonSnapshotBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   Buffers.MortonSnapshot, Buffers.MortonSnapshotMemory))
        return Abandon("scene buffer creation failed");

    // ─── the four submissions ────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    InstanceBoundsSubmission Bounds;
    RadixSortSubmission      Sort;
    InstanceTreeSubmission   Tree;

    const auto Teardown = [&]()
    {
        FinalizeInstanceTreeSubmission(Tree);
        FinalizeRadixSortSubmission(Sort);
        FinalizeInstanceBoundsSubmission(Bounds);
        DestroyCaseBuffers(Host, Buffers);
    };

    if (!InitializeInstanceBoundsSubmission(Bounds, Host, ShaderDirectory))
    {
        Teardown();
        OutFailureReason = "InstanceBounds init failed";
        return false;
    }
    if (!InitializeRadixSortSubmission(Sort, Host, InstanceCount, ShaderDirectory))
    {
        Teardown();
        OutFailureReason = "RadixSort init failed";
        return false;
    }
    if (!InitializeInstanceTreeSubmission(Tree, Host, InstanceCount, ShaderDirectory))
    {
        Teardown();
        OutFailureReason = "InstanceTree init failed";
        return false;
    }

    // 🔴 THE MORTON PASS WRITES STRAIGHT INTO THE SORT'S INPUT BUFFERS. Routing the codes through the host would be device -> host -> device every
    //    frame for data the GPU just produced; SetRadixSortKeyCount exists so the count can be declared without moving anything.
    VkBuffer MortonKeyBuffer = VK_NULL_HANDLE, MortonPayloadBuffer = VK_NULL_HANDLE;
    RetrieveRadixSortInputBuffers(Sort, MortonKeyBuffer, MortonPayloadBuffer);

    const VkDeviceSize KeyBytes = (VkDeviceSize)InstanceCount * sizeof(uint32_t);

    if (!BindInstanceBoundsScene(Bounds, Buffers.Instance, Buffers.InstanceBytes, Buffers.Slice, Buffers.SliceBytes,
                                 Buffers.Node, Buffers.NodeBytes, InstanceCount, Scene.SliceCount) ||
        !BindInstanceMortonTarget(Bounds, MortonKeyBuffer, KeyBytes, MortonPayloadBuffer, KeyBytes) ||
        !SetRadixSortKeyCount(Sort, InstanceCount))
    {
        Teardown();
        OutFailureReason = "bounds/sort bind failed";
        return false;
    }

    // 🔴 THE SORT'S RESULT PAIR, NOT THE PRIMARY PAIR BY NAME. The sort ping-pongs; RetrieveRadixSortedBuffers exists so a caller never has to know
    //    which side won. Binding the wrong side builds a tree over UNSORTED keys, which is well-formed and useless.
    VkBuffer SortedKeyBuffer = VK_NULL_HANDLE, SortedPayloadBuffer = VK_NULL_HANDLE;
    RetrieveRadixSortedBuffers(Sort, SortedKeyBuffer, SortedPayloadBuffer);

    if (!BindInstanceTreeSorted(Tree, SortedKeyBuffer, KeyBytes, SortedPayloadBuffer, KeyBytes, InstanceCount) ||
        !BindInstanceTreeScene(Tree, Buffers.Instance, Buffers.InstanceBytes, Buffers.Slice, Buffers.SliceBytes,
                               Buffers.Node, Buffers.NodeBytes, SortedPayloadBuffer, KeyBytes, Scene.SliceCount))
    {
        Teardown();
        OutFailureReason = "tree bind failed";
        return false;
    }

    // ─── record all four dispatches into ONE command buffer ──────────────────────────────────────────────────────────────────────────────────
    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &Command) != VK_SUCCESS)
    {
        Teardown();
        OutFailureReason = "command buffer allocation failed";
        return false;
    }

    VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(Command, &BeginInformation);

    RecordInstanceBoundsReduce(Bounds, Command);
    RecordInstanceMortonCode(Bounds, Command);

    // ─── snapshot the Morton codes BEFORE the sort consumes them in place ────────────────────────────────────────────────────────────────────
    // 🔴 THIS IS THE MEASUREMENT THAT SPLITS THE LAST UNSPLIT PAIR. The determinism check proved the SORTED keys differ between identical runs, but
    //    that is consistent with two very different faults: a nondeterministic reduce/Morton feeding a correct sort, or a deterministic Morton pass
    //    feeding a nondeterministic sort. The sort overwrites its input in place, so by readback time the evidence is gone — the copy has to be
    //    recorded here, between the two dispatches, or the question cannot be asked at all.
    //
    // ⚠️ NO COMPUTE->TRANSFER BARRIER IS RECORDED HERE, AND ITS ABSENCE IS DELIBERATE. RecordInstanceMortonCode fences its own output for both
    //    SHADER_READ and TRANSFER_READ, so this copy is already ordered against the emit. An earlier revision of this block did record one — and in
    //    doing so accidentally supplied the production barrier that was MISSING from the submission, which turned the real bug into a gate that
    //    passed only while being measured. Adding it back would re-mask exactly that. If the emit ever stops fencing itself, this gate must fail.
    {
        VkBufferMemoryBarrier MortonBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        MortonBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        MortonBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        MortonBarrier.buffer              = MortonKeyBuffer;
        MortonBarrier.offset              = 0;
        MortonBarrier.size                = KeyBytes;

        VkBufferCopy SnapshotRegion = {};
        SnapshotRegion.srcOffset = 0;
        SnapshotRegion.dstOffset = 0;
        SnapshotRegion.size      = KeyBytes;
        vkCmdCopyBuffer(Command, MortonKeyBuffer, Buffers.MortonSnapshot, 1, &SnapshotRegion);

        // The sort reads the same buffer as a shader input, so the copy must finish before it starts.
        MortonBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        MortonBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &MortonBarrier, 0, nullptr);
    }

    RecordRadixSort(Sort, Command);
    RecordInstanceTreeBuild(Tree, Command);
    RecordInstanceTreeRefit(Tree, Command);

    vkEndCommandBuffer(Command);

    if (!SubmitAndWait(Host, CommandPool, Command))
    {
        Teardown();
        OutFailureReason = "SUBMIT TIMED OUT (30s) — likely the duplicate-key hang";
        return false;
    }

    // ─── read everything back ────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    const uint32_t TotalNodes = 2u * InstanceCount - 1u;
    OutNodeWords.assign((size_t)TotalNodes * WordsPerNode, 0u);
    OutParents.assign(TotalNodes, 0u);
    OutSortedKeys.assign(InstanceCount, 0u);
    OutSortedPayloads.assign(InstanceCount, 0u);
    OutMortonKeys.assign(InstanceCount, 0u);

    if (!ReadHostBuffer(Host, Buffers.MortonSnapshotMemory, InstanceCount, OutMortonKeys.data()))
    {
        Teardown();
        OutFailureReason = "Morton snapshot readback failed";
        return false;
    }

    if (!RetrieveInstanceTreeReadback(Tree, CommandPool, InstanceCount, OutNodeWords.data(), OutParents.data()))
    {
        Teardown();
        OutFailureReason = "tree readback failed";
        return false;
    }
    if (!RetrieveRadixSortReadback(Sort, CommandPool, InstanceCount, OutSortedKeys.data(), OutSortedPayloads.data()))
    {
        Teardown();
        OutFailureReason = "sort readback failed";
        return false;
    }

    Teardown();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         THE ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

float NodeBoxComponent(const std::vector<uint32_t>& NodeWords, uint32_t NodeIndex, uint32_t Component)
{
    float Value;
    std::memcpy(&Value, &NodeWords[(size_t)NodeIndex * WordsPerNode + Component], sizeof(Value));
    return Value;
}

WorldBox ReadNodeBox(const std::vector<uint32_t>& NodeWords, uint32_t NodeIndex)
{
    WorldBox Box;
    Box.MinimumX = NodeBoxComponent(NodeWords, NodeIndex, 0);
    Box.MinimumY = NodeBoxComponent(NodeWords, NodeIndex, 1);
    Box.MinimumZ = NodeBoxComponent(NodeWords, NodeIndex, 2);
    Box.MaximumX = NodeBoxComponent(NodeWords, NodeIndex, 3);
    Box.MaximumY = NodeBoxComponent(NodeWords, NodeIndex, 4);
    Box.MaximumZ = NodeBoxComponent(NodeWords, NodeIndex, 5);
    return Box;
}

// Judge one completed chain. Every failure prints its own line, and the tally is what the exit code reads.
void JudgeTree(const char*                  Label,
               const SyntheticScene&        Scene,
               const std::vector<uint32_t>& NodeWords,
               const std::vector<uint32_t>& Parents,
               const std::vector<uint32_t>& SortedKeys,
               const std::vector<uint32_t>& SortedPayloads)
{
    const uint32_t InstanceCount = (uint32_t)Scene.Instances.size();
    const uint32_t InternalCount = InstanceCount - 1u;
    const uint32_t TotalNodes    = 2u * InstanceCount - 1u;

    uint32_t LocalFailures = 0;
    const auto Fail = [&](const char* Reason, long long Detail)
    {
        // 📝 Every distinct assertion prints. An earlier revision capped this at the first four, which hid the leaf-box failures behind the root-box
        //    one and made the result read as "structure fine, only the root wrong" — a picture that pointed at the climb when the fault was upstream
        //    of it. A suppressed failure is worse than a noisy one when the suppression is what decides the diagnosis.
        std::printf("  %-24s n=%-7u FAIL %s (%lld)\n", Label, InstanceCount, Reason, Detail);
        ++LocalFailures;
    };

    // ─── 1. THE SORT IS ACTUALLY SORTED ──────────────────────────────────────────────────────────────────────────────────────────────────────
    // 📝 Checked first and reported as itself: a tree built over unsorted keys fails every structural assertion below for a reason that has nothing
    //    to do with the tree, and the two must not be confused.
    for (uint32_t Index = 1; Index < InstanceCount; ++Index)
        if (SortedKeys[Index] < SortedKeys[Index - 1u]) { Fail("sorted keys are not ascending at", Index); break; }

    // ─── 2. EVERY NODE BUT THE ROOT HAS EXACTLY ONE PARENT ───────────────────────────────────────────────────────────────────────────────────
    std::vector<uint32_t> ParentTally(TotalNodes, 0u);
    bool ChildIndexValid = true;
    for (uint32_t Node = 0; Node < InternalCount; ++Node)
    {
        const uint32_t LeftChild  = NodeWords[(size_t)Node * WordsPerNode + 6u];
        const uint32_t RightChild = NodeWords[(size_t)Node * WordsPerNode + 7u];
        if (LeftChild >= TotalNodes || RightChild >= TotalNodes)
        {
            Fail("child index out of range at node", Node);
            ChildIndexValid = false;
            break;
        }
        ++ParentTally[LeftChild];
        ++ParentTally[RightChild];
    }

    if (ChildIndexValid)
    {
        uint32_t RootTally = 0, MultiParentTally = 0;
        for (uint32_t Node = 0; Node < TotalNodes; ++Node)
        {
            if (ParentTally[Node] == 0u) ++RootTally;
            else if (ParentTally[Node] != 1u) ++MultiParentTally;
        }
        if (RootTally != 1u)        Fail("nodes with no parent, expected exactly 1:", RootTally);
        if (MultiParentTally != 0u) Fail("nodes claimed by more than one parent:", MultiParentTally);
        if (ParentTally[0] != 0u)   Fail("node 0 is not the root, parent tally", ParentTally[0]);

        // ─── 3. THE PARENT TABLE AGREES WITH THE CHILD POINTERS ──────────────────────────────────────────────────────────────────────────────
        // 🔴 The refit CLIMBS the parent table while the trace DESCENDS the child pointers, so a disagreement between them is a tree that bounds
        //    one shape and is traversed as another — and each half looks self-consistent on its own.
        uint32_t ParentDisagreement = 0;
        for (uint32_t Node = 0; Node < InternalCount; ++Node)
        {
            const uint32_t LeftChild  = NodeWords[(size_t)Node * WordsPerNode + 6u];
            const uint32_t RightChild = NodeWords[(size_t)Node * WordsPerNode + 7u];
            if (Parents[LeftChild] != Node)  ++ParentDisagreement;
            if (Parents[RightChild] != Node) ++ParentDisagreement;
        }
        if (ParentDisagreement != 0u) Fail("parent table disagrees with child pointers, entries:", ParentDisagreement);
        if (Parents[0] != GeometryTreeNoParent) Fail("the root's parent is not the sentinel:", (long long)Parents[0]);

        // ─── 4. CONNECTED, ACYCLIC, AND IN Z-CURVE ORDER ─────────────────────────────────────────────────────────────────────────────────────
        // 📝 Iterative rather than recursive: a two-cluster scene at 65,536 instances produces a tree deep enough to overflow the default stack, and
        //    a crash here would read as a tree bug rather than a harness one.
        std::vector<uint32_t> Stack;
        std::vector<uint32_t> LeafOrder;
        std::vector<uint8_t>  Visited(TotalNodes, 0u);
        Stack.push_back(0u);
        LeafOrder.reserve(InstanceCount);

        bool DescentValid = true;
        while (!Stack.empty())
        {
            const uint32_t Node = Stack.back();
            Stack.pop_back();

            if (Visited[Node] != 0u) { Fail("cycle: node reached twice at", Node); DescentValid = false; break; }
            Visited[Node] = 1u;

            if (Node >= InternalCount) { LeafOrder.push_back(Node - InternalCount); continue; }

            // Right first so the left subtree pops first, giving LeafOrder in ascending key position.
            Stack.push_back(NodeWords[(size_t)Node * WordsPerNode + 7u]);
            Stack.push_back(NodeWords[(size_t)Node * WordsPerNode + 6u]);
        }

        if (DescentValid)
        {
            if (LeafOrder.size() != InstanceCount)
                Fail("descent reached the wrong number of leaves:", (long long)LeafOrder.size());
            else
                for (uint32_t Index = 0; Index < InstanceCount; ++Index)
                    if (LeafOrder[Index] != Index) { Fail("leaf order breaks Z-curve order at", Index); break; }

            uint32_t Unreached = 0;
            for (uint32_t Node = 0; Node < TotalNodes; ++Node) if (Visited[Node] == 0u) ++Unreached;
            if (Unreached != 0u) Fail("nodes unreachable from the root:", Unreached);
        }
    }

    // ─── 5. EVERY LEAF CARRIES ITS INSTANCE AND ITS LEAF MARKER ──────────────────────────────────────────────────────────────────────────────
    for (uint32_t Leaf = 0; Leaf < InstanceCount; ++Leaf)
    {
        const uint32_t LeafNode = InternalCount + Leaf;
        const uint32_t Payload  = NodeWords[(size_t)LeafNode * WordsPerNode + 6u];
        const uint32_t Marker   = NodeWords[(size_t)LeafNode * WordsPerNode + 7u];
        if (Payload != SortedPayloads[Leaf]) { Fail("leaf names the wrong instance at leaf", Leaf); break; }
        if ((Marker >> 16u) != LeafFlag)     { Fail("leaf is missing its leaf flag at leaf", Leaf); break; }
        if ((Marker & 0xFFFFu) != 1u)        { Fail("leaf count is not 1 at leaf", Leaf); break; }
    }

    // ─── 6. THE LEAF BOXES ARE EXACTLY THE EIGHT-CORNER BOXES ────────────────────────────────────────────────────────────────────────────────
    // 🔴 THIS IS WHERE THE EIGHT-CORNER RULE FINALLY HAS A GATE. The bounds gate (#7) could not assert it: it observes CENTROIDS, and the two-corner
    //    and eight-corner rules provably share a centre under any affine map. Here the EXTENT is what is compared, which is exactly the quantity the
    //    two rules disagree on — and the rotated-ring shape supplies transforms where they genuinely differ.
    uint32_t LeafBoxMismatch = 0;
    for (uint32_t Leaf = 0; Leaf < InstanceCount; ++Leaf)
    {
        const uint32_t LeafNode  = InternalCount + Leaf;
        const WorldBox Result    = ReadNodeBox(NodeWords, LeafNode);
        const WorldBox Reference = ReferenceLeafBox(Scene, SortedPayloads[Leaf]);

        const bool Matches = ValuesMatchExactly(Result.MinimumX, Reference.MinimumX) &&
                             ValuesMatchExactly(Result.MinimumY, Reference.MinimumY) &&
                             ValuesMatchExactly(Result.MinimumZ, Reference.MinimumZ) &&
                             ValuesMatchExactly(Result.MaximumX, Reference.MaximumX) &&
                             ValuesMatchExactly(Result.MaximumY, Reference.MaximumY) &&
                             ValuesMatchExactly(Result.MaximumZ, Reference.MaximumZ);
        if (!Matches)
        {
            if (LeafBoxMismatch == 0u)
                std::printf("      leaf %u got [%g %g %g .. %g %g %g] expected [%g %g %g .. %g %g %g]\n", Leaf,
                            Result.MinimumX, Result.MinimumY, Result.MinimumZ, Result.MaximumX, Result.MaximumY, Result.MaximumZ,
                            Reference.MinimumX, Reference.MinimumY, Reference.MinimumZ,
                            Reference.MaximumX, Reference.MaximumY, Reference.MaximumZ);
            ++LeafBoxMismatch;
        }
    }
    if (LeafBoxMismatch != 0u) Fail("leaf boxes disagreeing with the eight-corner oracle:", LeafBoxMismatch);

    // ─── 7. EVERY INTERIOR BOX IS EXACTLY THE UNION OF ITS CHILDREN ──────────────────────────────────────────────────────────────────────────
    // 🔴 EXACTLY, NOT MERELY CONTAINING. A box that CONTAINS its children is what a short refit still produces most of the time — the union of a
    //    partial subtree contains what it did absorb. Only exact equality catches the arrival-counter race, because a box missing one subtree is a
    //    valid superset of everything it did see. min/max of exactly-representable values is exact, so this comparison is legitimate.
    if (ChildIndexValid)
    {
        uint32_t UnionMismatch = 0;
        for (uint32_t Node = 0; Node < InternalCount; ++Node)
        {
            const uint32_t LeftChild  = NodeWords[(size_t)Node * WordsPerNode + 6u];
            const uint32_t RightChild = NodeWords[(size_t)Node * WordsPerNode + 7u];

            const WorldBox Result = ReadNodeBox(NodeWords, Node);
            WorldBox Union = ReadNodeBox(NodeWords, LeftChild);
            const WorldBox Right = ReadNodeBox(NodeWords, RightChild);
            Union.MinimumX = std::min(Union.MinimumX, Right.MinimumX);
            Union.MinimumY = std::min(Union.MinimumY, Right.MinimumY);
            Union.MinimumZ = std::min(Union.MinimumZ, Right.MinimumZ);
            Union.MaximumX = std::max(Union.MaximumX, Right.MaximumX);
            Union.MaximumY = std::max(Union.MaximumY, Right.MaximumY);
            Union.MaximumZ = std::max(Union.MaximumZ, Right.MaximumZ);

            const bool Matches = ValuesMatchExactly(Result.MinimumX, Union.MinimumX) &&
                                 ValuesMatchExactly(Result.MinimumY, Union.MinimumY) &&
                                 ValuesMatchExactly(Result.MinimumZ, Union.MinimumZ) &&
                                 ValuesMatchExactly(Result.MaximumX, Union.MaximumX) &&
                                 ValuesMatchExactly(Result.MaximumY, Union.MaximumY) &&
                                 ValuesMatchExactly(Result.MaximumZ, Union.MaximumZ);
            if (!Matches)
            {
                if (UnionMismatch == 0u)
                    std::printf("      node %u got [%g %g %g .. %g %g %g] children union [%g %g %g .. %g %g %g]\n", Node,
                                Result.MinimumX, Result.MinimumY, Result.MinimumZ, Result.MaximumX, Result.MaximumY, Result.MaximumZ,
                                Union.MinimumX, Union.MinimumY, Union.MinimumZ, Union.MaximumX, Union.MaximumY, Union.MaximumZ);
                ++UnionMismatch;
            }
        }
        if (UnionMismatch != 0u) Fail("interior boxes that are not the exact union of their children:", UnionMismatch);

        // ─── 8. THE ROOT BOUNDS EVERY LIVE LEAF ──────────────────────────────────────────────────────────────────────────────────────────────
        // 📝 Implied by 6 + 7, but asserted directly because it is the one property the trace actually depends on, and an independent statement of it
        //    fails loudly if a future change breaks the chain of reasoning rather than the code.
        //
        // ⚠️ 6 AND 7 CAN BOTH PASS WHILE THIS FAILS, AND THAT COMBINATION IS DIAGNOSTIC RATHER THAN CONTRADICTORY. 7 compares the readback against
        //    ITSELF: if a lane read a stale sibling box and then stored the union of what it saw, the stored parent still equals the union of the two
        //    children AS READ BACK LATER only when the stale value happened to match. When it does not, 7 catches it — so 7 passing while 8 fails
        //    means the climb TERMINATED EARLY, leaving interior nodes never written at all. The tally below distinguishes the two.
        // 📝 An unwritten node reads as ZEROS, not as the inverted box — nothing seeds these words, which is the whole point of this tally. Testing
        //    for the inverted box instead would report a clean zero every time and hide the very failure being looked for.
        //
        // 🔴 AN INVERTED INTERIOR BOX IS NOT EVIDENCE OF ANYTHING ON ITS OWN, AND TREATING IT AS SUCH IS WHY THIS ASSERTION USED TO FAIL EVERY
        //    out-of-range CASE. A leaf whose ordinal was rejected keeps its inverted box deliberately — the refit's own comment calls it inert. An
        //    interior node whose entire subtree is such leaves is then inverted too, correctly, because min(+inf, +inf) is +inf. That is a faithful
        //    "this subtree bounds nothing", and the whole point of the out-of-range shape is to produce it. The tally has to ask whether the node was
        //    REACHED, and inversion does not answer that question — only all-zero does, since nothing else ever writes that pattern.
        //
        // ⚠️ SO INVERSION IS JUDGED AGAINST SUBTREE LIVENESS, NOT ON SIGHT. A node is only suspect when it reads empty while a live leaf sits beneath
        //    it, which is a climb that terminated early; that case the counter below still catches. Dropping the inversion arm entirely would be the
        //    other error — it would let a genuinely short climb hide behind an inverted box in any scene that has dropped instances at all.
        std::vector<uint8_t> SubtreeHasLiveLeaf((size_t)TotalNodes, 0u);
        for (uint32_t Leaf = 0; Leaf < InstanceCount; ++Leaf)
        {
            const WorldBox LeafBox = ReadNodeBox(NodeWords, InternalCount + Leaf);
            if (std::isinf(LeafBox.MinimumX))
                continue;   // dropped by the ordinal test, exactly as the reduce and the Morton pass dropped it
            for (uint32_t Node = InternalCount + Leaf; Node != GeometryTreeNoParent; Node = Parents[Node])
            {
                if (SubtreeHasLiveLeaf[Node]) break;   // an ancestor already marked: everything above it is marked too
                SubtreeHasLiveLeaf[Node] = 1u;
            }
        }

        uint32_t UnwrittenInterior = 0;
        for (uint32_t Node = 0; Node < InternalCount; ++Node)
        {
            const WorldBox Box = ReadNodeBox(NodeWords, Node);
            const bool AllZero = Box.MinimumX == 0.0f && Box.MinimumY == 0.0f && Box.MinimumZ == 0.0f &&
                                 Box.MaximumX == 0.0f && Box.MaximumY == 0.0f && Box.MaximumZ == 0.0f;
            const bool EmptyOverLiveSubtree = std::isinf(Box.MinimumX) && SubtreeHasLiveLeaf[Node] != 0u;
            if (AllZero || EmptyOverLiveSubtree) ++UnwrittenInterior;
        }
        if (UnwrittenInterior != 0u)
            Fail("interior nodes left unwritten (all-zero, or empty over a live subtree):", UnwrittenInterior);

        WorldBox Expected;
        for (uint32_t Index = 0; Index < InstanceCount; ++Index)
            Expected.Absorb(ReferenceLeafBox(Scene, Index));

        const WorldBox Root = ReadNodeBox(NodeWords, 0u);
        if (!Expected.EmptyCondition())
        {
            const bool Matches = ValuesMatchExactly(Root.MinimumX, Expected.MinimumX) &&
                                 ValuesMatchExactly(Root.MinimumY, Expected.MinimumY) &&
                                 ValuesMatchExactly(Root.MinimumZ, Expected.MinimumZ) &&
                                 ValuesMatchExactly(Root.MaximumX, Expected.MaximumX) &&
                                 ValuesMatchExactly(Root.MaximumY, Expected.MaximumY) &&
                                 ValuesMatchExactly(Root.MaximumZ, Expected.MaximumZ);
            if (!Matches)
            {
                Fail("the root box is not the union of every live leaf", 0);
                std::printf("      root [%g %g %g .. %g %g %g] expected [%g %g %g .. %g %g %g]\n",
                            Root.MinimumX, Root.MinimumY, Root.MinimumZ, Root.MaximumX, Root.MaximumY, Root.MaximumZ,
                            Expected.MinimumX, Expected.MinimumY, Expected.MinimumZ,
                            Expected.MaximumX, Expected.MaximumY, Expected.MaximumZ);
            }
        }
    }

    if (LocalFailures > 4u)
        std::printf("  %-24s n=%-7u ... and %u further failures\n", Label, InstanceCount, LocalFailures - 4u);

    FailureTally += LocalFailures;
}

// One full case: synthesise, run the chain, judge.
void ExecuteCase(VulkanHost& Host, VkCommandPool CommandPool, SceneShape Shape, uint32_t InstanceCount, uint32_t Seed, bool Verbose)
{
    ++CaseTally;

    // 📝 A single-instance tree has no internal nodes and no structure to judge; the build is a documented no-op and the refit writes the lone leaf.
    //    Excluded at the call sites rather than silently skipped here, so the suite's counts mean what they say.
    SyntheticScene Scene;
    BuildSyntheticScene(Shape, InstanceCount, Seed, Scene);

    std::vector<uint32_t> NodeWords, Parents, SortedKeys, SortedPayloads, MortonKeys;
    const char* FailureReason = nullptr;
    if (!ExecuteChain(Host, CommandPool, Scene, NodeWords, Parents, SortedKeys, SortedPayloads, MortonKeys, FailureReason))
    {
        std::printf("  %-24s n=%-7u FAIL %s\n", DescribeShape(Shape), InstanceCount, FailureReason ? FailureReason : "unknown");
        ++FailureTally;
        return;
    }

    const uint32_t Before = FailureTally;
    JudgeTree(DescribeShape(Shape), Scene, NodeWords, Parents, SortedKeys, SortedPayloads);
    if (Verbose && FailureTally == Before)
        std::printf("  %-24s n=%-7u PASS\n", DescribeShape(Shape), InstanceCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE DETERMINISM CHECK
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ THE ONLY CHECK THAT CAN SEE THE ARRIVAL-COUNTER RACE. A refit that lets both lanes climb, or that reads a sibling box before it lands, produces
//    the right answer in most runs. Running the SAME scene repeatedly and demanding byte-identical node words turns "usually right" into a failure:
//    a race that is right 90% of the time fails this at 12 runs with probability ~0.7, and the counter-reseed bug specifically shows up as a box
//    that differs run to run while every structural assertion still passes.
void ExecuteDeterminismCheck(VulkanHost& Host, VkCommandPool CommandPool, SceneShape Shape, uint32_t InstanceCount, uint32_t RunCount)
{
    ++CaseTally;

    SyntheticScene Scene;
    BuildSyntheticScene(Shape, InstanceCount, 777u, Scene);

    std::vector<uint32_t> ReferenceNodes, ReferenceParents, Keys, Payloads, Morton;
    const char* FailureReason = nullptr;
    if (!ExecuteChain(Host, CommandPool, Scene, ReferenceNodes, ReferenceParents, Keys, Payloads, Morton, FailureReason))
    {
        std::printf("  [determinism] %-14s n=%-7u FAIL first run: %s\n",
                    DescribeShape(Shape), InstanceCount, FailureReason ? FailureReason : "unknown");
        ++FailureTally;
        return;
    }

    for (uint32_t Run = 1; Run < RunCount; ++Run)
    {
        std::vector<uint32_t> Nodes, Parents, RunKeys, RunPayloads, RunMorton;
        if (!ExecuteChain(Host, CommandPool, Scene, Nodes, Parents, RunKeys, RunPayloads, RunMorton, FailureReason))
        {
            std::printf("  [determinism] %-14s n=%-7u FAIL run %u: %s\n",
                        DescribeShape(Shape), InstanceCount, Run, FailureReason ? FailureReason : "unknown");
            ++FailureTally;
            return;
        }

        // 🔴 THE CHAIN IS CHECKED IN ORDER, AND THAT ORDER IS THE WHOLE VALUE OF THIS BLOCK. Every stage downstream is a pure function of the stage
        //    above it, so "the tree differed" is not a finding on its own — it is a nondeterministic stage somewhere upstream, faithfully propagated.
        //    Reporting only the node difference conflates all four stages and sends the diagnosis into the tree when the tree is innocent, which is
        //    exactly the wrong turn this gate cost before the discrimination existed. Morton first, then the sort, then the tree.
        if (RunMorton != Morton)
        {
            size_t FirstDifference = 0;
            while (FirstDifference < RunMorton.size() && RunMorton[FirstDifference] == Morton[FirstDifference]) ++FirstDifference;
            std::printf("  [determinism] %-14s n=%-7u FAIL run %u MORTON CODES differ at %zu (reduce or Morton pass, upstream of the sort)\n",
                        DescribeShape(Shape), InstanceCount, Run, FirstDifference);
            ++FailureTally;
            return;
        }
        if (RunKeys != Keys)
        {
            size_t FirstDifference = 0;
            while (FirstDifference < RunKeys.size() && RunKeys[FirstDifference] == Keys[FirstDifference]) ++FirstDifference;
            std::printf("  [determinism] %-14s n=%-7u FAIL run %u SORTED KEYS differ at %zu (the SORT, on identical Morton input)\n",
                        DescribeShape(Shape), InstanceCount, Run, FirstDifference);
            ++FailureTally;
            return;
        }
        if (RunPayloads != Payloads)
        {
            size_t FirstDifference = 0;
            while (FirstDifference < RunPayloads.size() && RunPayloads[FirstDifference] == Payloads[FirstDifference]) ++FirstDifference;
            std::printf("  [determinism] %-14s n=%-7u FAIL run %u SORTED PAYLOADS differ at %zu (sort is unstable)\n",
                        DescribeShape(Shape), InstanceCount, Run, FirstDifference);
            ++FailureTally;
            return;
        }

        if (Nodes != ReferenceNodes)
        {
            size_t FirstDifference = 0;
            while (FirstDifference < Nodes.size() && Nodes[FirstDifference] == ReferenceNodes[FirstDifference]) ++FirstDifference;
            std::printf("  [determinism] %-14s n=%-7u FAIL run %u differs at node word %zu (node %zu) with an IDENTICAL sorted pair\n",
                        DescribeShape(Shape), InstanceCount, Run, FirstDifference, FirstDifference / WordsPerNode);
            ++FailureTally;
            return;
        }
        if (Parents != ReferenceParents)
        {
            std::printf("  [determinism] %-14s n=%-7u FAIL run %u parent table differs\n",
                        DescribeShape(Shape), InstanceCount, Run);
            ++FailureTally;
            return;
        }
    }

    std::printf("  [determinism] %-14s n=%-7u PASS (%u identical runs)\n", DescribeShape(Shape), InstanceCount, RunCount);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                             ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    bool StressEnabled = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
        if (std::strcmp(ArgumentValues[Index], "--stress") == 0) StressEnabled = true;

    std::printf("==== InstanceTreeBuild + InstanceTreeRefit validation ====\n\n");

    // A layout disagreement between this file and the shaders would make every case fail for one uninformative reason, so it is checked first and
    // reported as itself.
    std::printf("  sizeof(SuzanneSceneInstance) = %zu (expect 208)\n", sizeof(SuzanneSceneInstance));
    std::printf("  sizeof(GeometryArenaSlice)   = %zu (expect 32)\n\n", sizeof(GeometryArenaSlice));

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

    // 📝 Counts straddle every tile edge of the 256-wide workgroup, and 2 and 3 are the smallest trees that HAVE an internal node — the boundary where
    //    the range search has nowhere to overshoot to. Single-instance is excluded: it has no internal nodes and nothing structural to judge.
    const uint32_t InstanceCounts[] = { 2u, 3u, 255u, 256u, 257u, 511u, 512u, 513u, 1023u, 1024u, 1025u, 4096u, 65536u };
    const SceneShape Shapes[] = {
        SceneShape::ScatteredGrid,      SceneShape::RotatedRing, SceneShape::IdenticalCentroids,
        SceneShape::TwoClusters,        SceneShape::CoplanarRow, SceneShape::DenseCell,
        SceneShape::OutOfRangeOrdinals, SceneShape::NegativeOctant,
    };

    std::printf("---- correctness suite ");
    for (int Dash = 0; Dash < 67; ++Dash) std::printf("-");
    std::printf("\n");

    uint32_t Seed = 2000u;
    for (SceneShape Shape : Shapes)
        for (uint32_t Count : InstanceCounts)
            ExecuteCase(Host, CommandPool, Shape, Count, Seed++, Count >= 4096u);

    std::printf("\n---- determinism suite ");
    for (int Dash = 0; Dash < 66; ++Dash) std::printf("-");
    std::printf("\n");

    ExecuteDeterminismCheck(Host, CommandPool, SceneShape::TwoClusters,        16384u, 8u);
    ExecuteDeterminismCheck(Host, CommandPool, SceneShape::IdenticalCentroids, 16384u, 8u);
    ExecuteDeterminismCheck(Host, CommandPool, SceneShape::RotatedRing,        16384u, 8u);

    if (StressEnabled)
    {
        std::printf("\n---- stress suite ");
        for (int Dash = 0; Dash < 72; ++Dash) std::printf("-");
        std::printf("\n");

        std::printf("  [repetition] 12 runs, rotated ring, 200,000 instances\n");
        for (int Run = 0; Run < 12; ++Run)
            ExecuteCase(Host, CommandPool, SceneShape::RotatedRing, 200000u, 6000u + (uint32_t)Run, false);

        std::printf("  [randomized] 24 runs, random shape and count\n");
        std::mt19937 Generator(4242u);
        std::uniform_int_distribution<uint32_t> CountSpread(2u, 250000u);
        for (int Run = 0; Run < 24; ++Run)
        {
            const SceneShape Shape = Shapes[Run % (int)(sizeof(Shapes) / sizeof(Shapes[0]))];
            ExecuteCase(Host, CommandPool, Shape, CountSpread(Generator), 9000u + (uint32_t)Run, false);
        }

        ExecuteDeterminismCheck(Host, CommandPool, SceneShape::DenseCell, 200000u, 12u);
    }

    std::printf("\n==== %s : %u cases, %u failure%s ====\n",
                FailureTally == 0 ? "PASS" : "FAIL", CaseTally, FailureTally, FailureTally == 1 ? "" : "s");

    vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
    FinalizeVulkanHost(Host);
    return FailureTally == 0 ? 0 : 1;
}
