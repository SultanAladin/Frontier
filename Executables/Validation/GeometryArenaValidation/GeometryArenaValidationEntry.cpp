/*==============================================================================================================================================
                                                    GEOMETRYARENAVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for the bottom-level tree arena: brings up a HEADLESS Vulkan device, builds several meshes of differing sizes with
//    BuildGeometryTree, appends them to a GeometryArenaSubmission, uploads, and then READS THE DEVICE BUFFERS BACK to judge them against the host
//    trees byte for byte. Nothing before this proved the upload works — GeometryTreeValidation proved the BUILDER on the CPU and executes no
//    Vulkan, so the concatenation offsets, the staged copy and the slice table are all untested until this runs.
//
//    🔴 THE ROUND TRIP IS JUDGED BIT-EXACT. Node words are opaque bit patterns — packed floats and packed indices in the same array — so there is
//       no meaningful tolerance to apply, and any epsilon would forgive exactly the failure this exists to catch: a concatenation off-by-one that
//       shifts one mesh's blob by a word. A shifted blob is still well-formed, still traverses, and names the wrong triangles.
//
//    🔴 SEVERAL ASSERTIONS HERE ARE ABOUT THE OFFSETS, NOT THE BYTES, AND THAT IS DELIBERATE. A single-mesh arena passes a byte-for-byte round trip
//       trivially, because offset 0 is correct by accident. The multi-mesh disjointness and root-addressability checks are what actually exercise
//       the arena's one real job.
//
//    📝 Headless on purpose, same as the bounds gate: InitializeVulkanHost never creates or queries a surface, so a zero extension count yields a
//       compute-capable device with no window. No shader is loaded — the arena owns buffers only and dispatches nothing.

#define _CRT_SECURE_NO_WARNINGS

#include "Graphics/Acceleration/GeometryArenaSubmission.h"
#include "Graphics/Acceleration/GeometryTreeBuild.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Scene/SuzanneScene.h"
#include "Graphics/Scene/WorkspaceDocumentDecoder.h"

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        REPORTING
//------------------------------------------------------------------------------------------------------------------------

uint32_t CheckTally   = 0;
uint32_t FailureTally = 0;

void Judge(bool Condition, const char* Description)
{
    ++CheckTally;
    if (!Condition) ++FailureTally;
    std::printf("  [%s] %s\n", Condition ? "ok" : "FAIL", Description);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TEST GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 One generated mesh: a cloud of triangles in LOCAL space, which is the only thing the builder is allowed to see. The shapes differ in triangle
//    count so the appended blobs differ in size — equal-sized blobs would let a stride bug in the concatenation cancel out.
struct TestMesh
{
    std::vector<float>    Positions;   // [cm] - tightly packed XYZ, local space
    std::vector<uint32_t> Indices;     // [-]  - three per triangle
    uint32_t              TriangleCount = 0;
};

TestMesh BuildTestMesh(uint32_t TriangleCount, uint32_t Seed, float Extent)
{
    TestMesh Mesh;
    Mesh.TriangleCount = TriangleCount;
    Mesh.Positions.reserve((size_t)TriangleCount * 9u);
    Mesh.Indices.reserve((size_t)TriangleCount * 3u);

    std::mt19937 Generator(Seed);
    std::uniform_real_distribution<float> Spread(-Extent, Extent);
    std::uniform_real_distribution<float> Jitter(-Extent * 0.05f, Extent * 0.05f);

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const float CentreX = Spread(Generator);
        const float CentreY = Spread(Generator);
        const float CentreZ = Spread(Generator);
        for (uint32_t Corner = 0; Corner < 3u; ++Corner)
        {
            Mesh.Positions.push_back(CentreX + Jitter(Generator));
            Mesh.Positions.push_back(CentreY + Jitter(Generator));
            Mesh.Positions.push_back(CentreZ + Jitter(Generator));
            Mesh.Indices.push_back(Triangle * 3u + Corner);
        }
    }
    return Mesh;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         READBACK
//------------------------------------------------------------------------------------------------------------------------

// Copy a device buffer's leading WordCount words back to the host. Returns false on any failure, which is judged as a failed assertion by the
// caller rather than swallowed — a readback that quietly returns zeros would make a wrong arena look empty rather than wrong.
bool ReadDeviceWords(VulkanHost&            Host,
                     VkCommandPool          CommandPool,
                     VkBuffer               Source,
                     size_t                 WordCount,
                     std::vector<uint32_t>& OutWords)
{
    OutWords.clear();
    if (Source == VK_NULL_HANDLE || WordCount == 0)
        return false;

    const VkDeviceSize ByteSize = (VkDeviceSize)WordCount * sizeof(uint32_t);

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer StagingBuffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &StagingBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, StagingBuffer, &MemoryRequirements);

    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(Host.PhysicalDevice, &MemoryProperties);
    const VkMemoryPropertyFlags Required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t MemoryTypeIndex = UINT32_MAX;
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
    {
        const bool Compatible = (MemoryRequirements.memoryTypeBits & (1u << Index)) != 0;
        const bool Matches    = (MemoryProperties.memoryTypes[Index].propertyFlags & Required) == Required;
        if (Compatible && Matches) { MemoryTypeIndex = Index; break; }
    }
    if (MemoryTypeIndex == UINT32_MAX)
    {
        vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &StagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, StagingBuffer, StagingMemory, 0) != VK_SUCCESS)
    {
        if (StagingMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
        return false;
    }

    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer TransferCommand = VK_NULL_HANDLE;
    bool Succeeded = false;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &TransferCommand) == VK_SUCCESS)
    {
        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(TransferCommand, &BeginInformation) == VK_SUCCESS)
        {
            VkBufferCopy Region = {};
            Region.size = ByteSize;
            vkCmdCopyBuffer(TransferCommand, Source, StagingBuffer, 1, &Region);
            if (vkEndCommandBuffer(TransferCommand) == VK_SUCCESS)
            {
                VkFence Fence = VK_NULL_HANDLE;
                VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                if (vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence) == VK_SUCCESS)
                {
                    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                    SubmitInformation.commandBufferCount = 1;
                    SubmitInformation.pCommandBuffers    = &TransferCommand;
                    if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence) == VK_SUCCESS &&
                        vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
                    {
                        void* Mapped = nullptr;
                        if (vkMapMemory(Host.Device, StagingMemory, 0, ByteSize, 0, &Mapped) == VK_SUCCESS && Mapped != nullptr)
                        {
                            OutWords.resize(WordCount);
                            std::memcpy(OutWords.data(), Mapped, (size_t)ByteSize);
                            vkUnmapMemory(Host.Device, StagingMemory);
                            Succeeded = true;
                        }
                    }
                    vkDestroyFence(Host.Device, Fence, Host.Allocator);
                }
            }
        }
        vkFreeCommandBuffers(Host.Device, CommandPool, 1, &TransferCommand);
    }

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Succeeded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       NODE DECODING
//------------------------------------------------------------------------------------------------------------------------

// 📝 A packed node's six box floats live in words 0..5 as raw bit patterns; word 7's upper half is the leaf marker. Decoded here rather than
//    compared as bits so assertion 3 can report a box a reader recognises when it fails.
struct DecodedNode
{
    float    Bounds[6]     = { 0,0,0,0,0,0 };
    uint32_t TailWord      = 0;
    bool     LeafCondition = false;
};

DecodedNode DecodeNode(const std::vector<uint32_t>& Words, size_t WordOffset)
{
    DecodedNode Node;
    for (size_t Index = 0; Index < 6; ++Index)
    {
        const uint32_t Word = Words[WordOffset + Index];
        std::memcpy(&Node.Bounds[Index], &Word, sizeof(float));
    }
    Node.TailWord      = Words[WordOffset + 7];
    Node.LeafCondition = (Node.TailWord >> 16) == GeometryTreeLeafFlag;
    return Node;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFORM ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

// Column-major 4x4 multiply, element index Column*4 + Row — the convention SuzanneSceneInstance and the shaders use.
void MultiplyMatrices(const float Left[16], const float Right[16], float OutProduct[16])
{
    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
        {
            double Sum = 0.0;
            for (int Step = 0; Step < 4; ++Step)
                Sum += (double)Left[Step * 4 + Row] * (double)Right[Column * 4 + Step];
            OutProduct[Column * 4 + Row] = (float)Sum;
        }
}

// 📝 Tolerance scales with the OPERANDS, not with the result — the lesson GeometryTreeValidationEntry's WithinRoundingTolerance already records.
//    Rounding error in Model * InverseModel is proportional to the magnitudes multiplied together, so a translation of 5000 cm produces absolute
//    error in the identity's off-diagonal that a fixed epsilon would flag while a genuine sign error at the origin would slip through.
bool IdentityWithinTolerance(const float Product[16], float OperandMagnitude)
{
    const float Tolerance = 1.0e-4f * (OperandMagnitude > 1.0f ? OperandMagnitude : 1.0f);
    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
        {
            const float Expected = (Column == Row) ? 1.0f : 0.0f;
            const float Value    = Product[Column * 4 + Row];
            if (!std::isfinite(Value) || std::fabs(Value - Expected) > Tolerance)
                return false;
        }
    return true;
}

bool AllFinite(const float Values[16])
{
    for (int Index = 0; Index < 16; ++Index)
        if (!std::isfinite(Values[Index])) return false;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("==== GeometryArena validation ====\n\n");

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

    // ─── build the meshes ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 📝 Deliberately different triangle counts, so the appended blobs differ in size. Equal-sized blobs let a stride error in the concatenation
    //    cancel out and pass. The last mesh is DYNAMIC, which is the only one that contributes a parent table — assertion 6 needs both kinds
    //    present at once to prove a static mesh's GeometryTreeNoParent does not disturb a dynamic mesh's offset.
    struct MeshPlan { uint32_t TriangleCount; uint32_t Seed; float Extent; bool DynamicCondition; const char* Name; };
    const MeshPlan Plans[] = {
        {   37u, 11u,  50.0f, false, "small-static"  },
        {  968u, 22u, 120.0f, false, "suzanne-sized" },
        { 4111u, 33u, 800.0f, false, "large-static"  },
        {  512u, 44u,  75.0f, true,  "dynamic"       },
    };
    const uint32_t MeshCount = (uint32_t)(sizeof(Plans) / sizeof(Plans[0]));

    std::vector<GeometryTree> Trees(MeshCount);
    bool EveryTreeBuilt = true;
    for (uint32_t Index = 0; Index < MeshCount; ++Index)
    {
        const TestMesh Mesh = BuildTestMesh(Plans[Index].TriangleCount, Plans[Index].Seed, Plans[Index].Extent);
        GeometryTreeOptions Options;
        Options.DynamicCondition = Plans[Index].DynamicCondition;
        const bool Built = BuildGeometryTree(Mesh.Positions.data(),
                                             (uint32_t)(Mesh.Positions.size() / 3u),
                                             Mesh.Indices.data(),
                                             (uint32_t)Mesh.Indices.size(),
                                             Options,
                                             Trees[Index]);
        if (!Built) EveryTreeBuilt = false;
        std::printf("  built %-14s : %6u tri -> %6u nodes, %5u leaves, depth %2u%s\n",
                    Plans[Index].Name, Plans[Index].TriangleCount, Trees[Index].NodeCount,
                    Trees[Index].LeafCount, Trees[Index].MaximumDepth,
                    Plans[Index].DynamicCondition ? "  [dynamic]" : "");
    }
    std::printf("\n");
    Judge(EveryTreeBuilt, "every test mesh built a tree");

    // ─── append + upload ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    GeometryArenaSubmission Arena;
    Judge(InitializeGeometryArenaSubmission(Arena, Host), "arena initialized against the headless host");

    std::vector<uint32_t> Ordinals(MeshCount, UINT32_MAX);
    bool EveryAppendAccepted = true;
    uint32_t RunningVertexOffset = 0;
    uint32_t RunningIndexOffset  = 0;
    for (uint32_t Index = 0; Index < MeshCount; ++Index)
    {
        if (!AppendGeometryTreeToArena(Arena, Trees[Index], RunningVertexOffset, RunningIndexOffset, Ordinals[Index]))
            EveryAppendAccepted = false;
        RunningVertexOffset += Plans[Index].TriangleCount * 3u;
        RunningIndexOffset  += Plans[Index].TriangleCount * 3u;
    }
    Judge(EveryAppendAccepted, "every tree appended to the arena");

    bool OrdinalsSequential = true;
    for (uint32_t Index = 0; Index < MeshCount; ++Index)
        if (Ordinals[Index] != Index) OrdinalsSequential = false;
    Judge(OrdinalsSequential, "append hands back sequential mesh ordinals");

    // Snapshot the host accumulation BEFORE the upload releases it, so the round-trip comparison has an oracle.
    const size_t ExpectedNodeWords      = Arena.PendingNodeWords.size();
    const size_t ExpectedPrimitiveWords = Arena.PendingPrimitiveOrder.size();
    const size_t ExpectedParentWords    = Arena.PendingParentTable.size();

    Judge(UploadGeometryArena(Arena, CommandPool), "arena uploaded to the device");
    Judge(Arena.UploadedCondition, "UploadedCondition set after upload");

    // 🔴 The append-after-upload refusal is a correctness assertion, not a nicety: a silently-accepted late append would point a slice past the end
    //    of a buffer sized without it, and the trace would read whatever follows.
    uint32_t LateOrdinal = UINT32_MAX;
    Judge(!AppendGeometryTreeToArena(Arena, Trees[0], 0, 0, LateOrdinal), "append after upload is refused");

    VkBuffer NodeBuffer = VK_NULL_HANDLE, PrimitiveBuffer = VK_NULL_HANDLE, SliceBuffer = VK_NULL_HANDLE, ParentBuffer = VK_NULL_HANDLE;
    RetrieveGeometryArenaBuffers(Arena, NodeBuffer, PrimitiveBuffer, SliceBuffer, ParentBuffer);
    Judge(NodeBuffer != VK_NULL_HANDLE && PrimitiveBuffer != VK_NULL_HANDLE && SliceBuffer != VK_NULL_HANDLE,
          "node / primitive / slice buffers are live after upload");
    Judge(ParentBuffer != VK_NULL_HANDLE, "parent buffer allocated (one mesh is dynamic)");

    // ─── read the device back ────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    std::vector<uint32_t> DeviceNodeWords, DevicePrimitiveWords, DeviceParentWords;
    const bool NodesRead      = ReadDeviceWords(Host, CommandPool, NodeBuffer,      ExpectedNodeWords,      DeviceNodeWords);
    const bool PrimitivesRead = ReadDeviceWords(Host, CommandPool, PrimitiveBuffer, ExpectedPrimitiveWords, DevicePrimitiveWords);
    const bool ParentsRead    = ReadDeviceWords(Host, CommandPool, ParentBuffer,    ExpectedParentWords,    DeviceParentWords);
    Judge(NodesRead && PrimitivesRead && ParentsRead, "device buffers read back");

    // ─── assertion 1: byte-for-byte round trip, per mesh ─────────────────────────────────────────────────────────────────────────────────────
    std::printf("\n-- assertion 1: round trip --\n");
    bool EveryMeshRoundTripped = true;
    for (uint32_t Index = 0; Index < MeshCount && NodesRead && PrimitivesRead; ++Index)
    {
        const GeometryArenaSlice Slice = RetrieveGeometryArenaSlice(Arena, Index);
        const GeometryTree&      Tree  = Trees[Index];

        bool NodesMatch = ((size_t)Slice.NodeOffset + Tree.NodeWords.size()) <= DeviceNodeWords.size();
        for (size_t Word = 0; NodesMatch && Word < Tree.NodeWords.size(); ++Word)
            if (DeviceNodeWords[Slice.NodeOffset + Word] != Tree.NodeWords[Word]) NodesMatch = false;

        bool PrimitivesMatch = ((size_t)Slice.PrimitiveOffset + Tree.PrimitiveOrder.size()) <= DevicePrimitiveWords.size();
        for (size_t Entry = 0; PrimitivesMatch && Entry < Tree.PrimitiveOrder.size(); ++Entry)
            if (DevicePrimitiveWords[Slice.PrimitiveOffset + Entry] != Tree.PrimitiveOrder[Entry]) PrimitivesMatch = false;

        if (!NodesMatch || !PrimitivesMatch) EveryMeshRoundTripped = false;
        std::printf("  %-14s nodes %s, primitive order %s\n", Plans[Index].Name,
                    NodesMatch ? "match" : "DIFFER", PrimitivesMatch ? "match" : "DIFFER");
    }
    Judge(EveryMeshRoundTripped, "every mesh's node words and primitive order round trip byte for byte");

    // ─── assertion 2: slice ranges are disjoint and in bounds ────────────────────────────────────────────────────────────────────────────────
    std::printf("\n-- assertion 2: slice offsets --\n");
    bool RangesWellFormed = true;
    for (uint32_t Index = 0; Index < MeshCount; ++Index)
    {
        const GeometryArenaSlice Outer = RetrieveGeometryArenaSlice(Arena, Index);
        const size_t OuterStart = Outer.NodeOffset;
        const size_t OuterEnd   = OuterStart + (size_t)Outer.NodeCount * GeometryTreeWordsPerNode;
        if (OuterEnd > ExpectedNodeWords) { RangesWellFormed = false; std::printf("  %-14s runs past the arena end\n", Plans[Index].Name); }
        if (Outer.NodeCount != Trees[Index].NodeCount) { RangesWellFormed = false; std::printf("  %-14s node count disagrees\n", Plans[Index].Name); }

        for (uint32_t Other = Index + 1u; Other < MeshCount; ++Other)
        {
            const GeometryArenaSlice Inner = RetrieveGeometryArenaSlice(Arena, Other);
            const size_t InnerStart = Inner.NodeOffset;
            const size_t InnerEnd   = InnerStart + (size_t)Inner.NodeCount * GeometryTreeWordsPerNode;
            if (OuterStart < InnerEnd && InnerStart < OuterEnd)
            {
                RangesWellFormed = false;
                std::printf("  %s overlaps %s\n", Plans[Index].Name, Plans[Other].Name);
            }
        }
    }
    Judge(RangesWellFormed, "node ranges are disjoint, correctly sized and within the arena");

    // ─── assertion 3: each mesh's root node is addressable at its offset ─────────────────────────────────────────────────────────────────────
    std::printf("\n-- assertion 3: root addressability --\n");
    bool EveryRootAddressable = true;
    for (uint32_t Index = 0; Index < MeshCount && NodesRead; ++Index)
    {
        const GeometryArenaSlice Slice = RetrieveGeometryArenaSlice(Arena, Index);
        const DecodedNode DeviceRoot = DecodeNode(DeviceNodeWords,      Slice.NodeOffset);
        const DecodedNode HostRoot   = DecodeNode(Trees[Index].NodeWords, 0);

        bool BoxMatches = true;
        for (int Component = 0; Component < 6; ++Component)
            if (std::memcmp(&DeviceRoot.Bounds[Component], &HostRoot.Bounds[Component], sizeof(float)) != 0) BoxMatches = false;
        const bool FlagMatches = DeviceRoot.LeafCondition == HostRoot.LeafCondition;

        if (!BoxMatches || !FlagMatches) EveryRootAddressable = false;
        std::printf("  %-14s root box [%.2f %.2f %.2f]..[%.2f %.2f %.2f] %s\n", Plans[Index].Name,
                    DeviceRoot.Bounds[0], DeviceRoot.Bounds[1], DeviceRoot.Bounds[2],
                    DeviceRoot.Bounds[3], DeviceRoot.Bounds[4], DeviceRoot.Bounds[5],
                    (BoxMatches && FlagMatches) ? "matches host" : "DIFFERS FROM HOST");
    }
    Judge(EveryRootAddressable, "every mesh's root decodes at NodeOffset and matches the host root");

    // ─── assertion 6: the parent table ───────────────────────────────────────────────────────────────────────────────────────────────────────
    // 📝 Out of numeric order because it is the last arena-side check; 4 and 5 belong to the transform and follow.
    std::printf("\n-- assertion 6: parent table --\n");
    bool StaticMeshesHaveNoParents = true;
    for (uint32_t Index = 0; Index < MeshCount; ++Index)
    {
        const GeometryArenaSlice Slice = RetrieveGeometryArenaSlice(Arena, Index);
        const bool ExpectDynamic = Plans[Index].DynamicCondition;
        const bool HasParents    = Slice.ParentOffset != GeometryTreeNoParent;
        if (HasParents != ExpectDynamic) StaticMeshesHaveNoParents = false;
    }
    Judge(StaticMeshesHaveNoParents, "only the dynamic mesh carries a parent offset; static meshes hold the sentinel");

    bool ParentTableWellFormed = ParentsRead && !DeviceParentWords.empty();
    uint32_t RootTally = 0;
    if (ParentTableWellFormed)
    {
        const uint32_t DynamicIndex = MeshCount - 1u;
        const GeometryArenaSlice Slice = RetrieveGeometryArenaSlice(Arena, DynamicIndex);
        Judge(DeviceParentWords.size() == (size_t)Slice.NodeCount, "parent table holds exactly one entry per node of the dynamic mesh");

        for (size_t Entry = 0; Entry < DeviceParentWords.size(); ++Entry)
        {
            const uint32_t Parent = DeviceParentWords[Entry];
            if (Parent == GeometryTreeNoParent) { ++RootTally; continue; }
            if (Parent >= Slice.NodeCount) { ParentTableWellFormed = false; break; }
        }
    }
    Judge(ParentTableWellFormed, "every parent entry is a valid node index or the no-parent sentinel");
    Judge(RootTally == 1u, "exactly one node carries the no-parent sentinel");

    // ─── assertions 4 + 5: the inverse transform ─────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 This calls the SHIPPED ComposeInverseModelMatrix through the decoder header, not a transcription of it. A copy here would pass forever
    //    while the load path drifted — the failure mode "probe transcription goes stale" already burned once in this project.
    std::printf("\n-- assertions 4 + 5: inverse transform --\n");
    struct PlacementCase { LocalPlacement Placement; float Magnitude; const char* Name; };
    PlacementCase Cases[] = {
        { LocalPlacement{ {   0.0f,   0.0f,   0.0f }, {  0.0f,  0.0f,   0.0f }, { 1.0f, 1.0f, 1.0f } },    1.0f, "identity"          },
        { LocalPlacement{ {  12.5f,  -7.25f, 3.0f }, {  0.0f,  0.0f,  35.0f }, { 1.0f, 1.0f, 1.0f } },   13.0f, "rotate-translate"  },
        { LocalPlacement{ { 500.0f, 250.0f, -80.0f}, { 15.0f, 40.0f, -65.0f }, { 2.5f, 0.5f, 3.0f } },  600.0f, "full trs"          },
        { LocalPlacement{ {5000.0f,-3000.0f,900.0f}, { 90.0f,-45.0f, 180.0f }, { 0.01f,120.0f,7.5f } }, 6000.0f, "extreme trs"      },
        { LocalPlacement{ {  30.0f,  10.0f,  -5.0f }, { 20.0f, 10.0f,   5.0f }, { 1.0f, 0.0f, 1.0f } },   35.0f, "zero-scale-y"      },
        { LocalPlacement{ {  -8.0f,   4.0f,   2.0f }, {  0.0f,  0.0f,   0.0f }, { 0.0f, 0.0f, 0.0f } },   10.0f, "all-zero-scale"    },
    };
    const uint32_t CaseCount = (uint32_t)(sizeof(Cases) / sizeof(Cases[0]));

    bool EveryInverseFinite = true;
    bool EveryWellPosedInverseExact = true;
    for (uint32_t Index = 0; Index < CaseCount; ++Index)
    {
        SuzanneSceneInstance Instance;
        ComposeInverseModelMatrix(Cases[Index].Placement, Instance.InverseModel);

        const bool Finite = AllFinite(Instance.InverseModel);
        if (!Finite) EveryInverseFinite = false;

        // 📝 A degenerate axis is clamped, not inverted, so its product is deliberately NOT identity — asserting otherwise would demand the
        //    impossible. Those cases are judged on finiteness alone (assertion 5); the well-posed ones carry assertion 4.
        const LocalPlacement& Placement = Cases[Index].Placement;
        const bool WellPosed = std::fabs(Placement.Scale[0]) > 1.0e-8f &&
                               std::fabs(Placement.Scale[1]) > 1.0e-8f &&
                               std::fabs(Placement.Scale[2]) > 1.0e-8f;

        bool IdentityHolds = true;
        if (WellPosed)
        {
            // 📝 The FORWARD matrix is rebuilt inline rather than called, because ComposeModelMatrix is private to the decoder and is already
            //    proven by the raster drawing correctly. Only the INVERSE is under test here, and that one is called, not copied.
            //    Forward = T * Rz * Ry * Rx * S in the decoder's column-major convention.
            float Forward[16] = {};
            const double ToRadians = 0.017453292519943295769236907684886;
            const double RadiansX = Placement.Rotation[0] * ToRadians;
            const double RadiansY = Placement.Rotation[1] * ToRadians;
            const double RadiansZ = Placement.Rotation[2] * ToRadians;
            const double CosX = std::cos(RadiansX), SinX = std::sin(RadiansX);
            const double CosY = std::cos(RadiansY), SinY = std::sin(RadiansY);
            const double CosZ = std::cos(RadiansZ), SinZ = std::sin(RadiansZ);
            const double R00 =  CosZ * CosY,                        R10 =  SinZ * CosY,                        R20 = -SinY;
            const double R01 =  CosZ * SinY * SinX - SinZ * CosX,   R11 =  SinZ * SinY * SinX + CosZ * CosX,   R21 =  CosY * SinX;
            const double R02 =  CosZ * SinY * CosX + SinZ * SinX,   R12 =  SinZ * SinY * CosX - CosZ * SinX,   R22 =  CosY * CosX;
            Forward[0]  = (float)(R00 * Placement.Scale[0]); Forward[1]  = (float)(R10 * Placement.Scale[0]); Forward[2]  = (float)(R20 * Placement.Scale[0]); Forward[3]  = 0.0f;
            Forward[4]  = (float)(R01 * Placement.Scale[1]); Forward[5]  = (float)(R11 * Placement.Scale[1]); Forward[6]  = (float)(R21 * Placement.Scale[1]); Forward[7]  = 0.0f;
            Forward[8]  = (float)(R02 * Placement.Scale[2]); Forward[9]  = (float)(R12 * Placement.Scale[2]); Forward[10] = (float)(R22 * Placement.Scale[2]); Forward[11] = 0.0f;
            Forward[12] = Placement.Location[0];             Forward[13] = Placement.Location[1];             Forward[14] = Placement.Location[2];             Forward[15] = 1.0f;

            float Product[16] = {};
            MultiplyMatrices(Forward, Instance.InverseModel, Product);
            IdentityHolds = IdentityWithinTolerance(Product, Cases[Index].Magnitude);
            if (!IdentityHolds) EveryWellPosedInverseExact = false;
        }

        std::printf("  %-18s finite %-3s  %s\n", Cases[Index].Name, Finite ? "yes" : "NO",
                    WellPosed ? (IdentityHolds ? "Model * InverseModel = I" : "IDENTITY FAILED") : "degenerate (finiteness only)");
    }
    Judge(EveryWellPosedInverseExact, "assertion 4: Model * InverseModel is identity for every well-posed placement");
    Judge(EveryInverseFinite, "assertion 5: a degenerate scale axis produces no inf/NaN");

    // ─── assertion 7: the real load path reaches the arena ───────────────────────────────────────────────────────────────────────────────────
    // 🔴 Everything above this point runs on SYNTHETIC meshes this file generated, so it proves the arena works and says nothing about whether
    //    anything actually FEEDS it. That gap is what let the builder and the arena both ship correct and both stay unreachable — no caller
    //    outside these gates ever built a tree. This assertion closes it by going through the shipped LoadWorkspaceScene against a real .wsdoc.
    std::printf("\n-- assertion 7: .wsdoc load path --\n");
    {
        const char* ScenePath = "Binaries/Validation/Assets/SuzanneRadial.wsdoc";

        RenderVertexStream                LoadedGeometry;
        std::vector<SuzanneSceneInstance> LoadedInstances;
        GeometryTree                      LoadedTree;
        const bool SceneLoaded = LoadWorkspaceScene(ScenePath, LoadedGeometry, LoadedInstances,
                                                    nullptr, nullptr, 0u, nullptr, &LoadedTree);
        Judge(SceneLoaded, "a real .wsdoc scene loads");

        if (SceneLoaded)
        {
            const uint32_t TriangleCount = (uint32_t)(LoadedGeometry.Indices.size() / 3);
            std::printf("     %s: %u vertices, %u triangles, %u instances\n",
                        ScenePath, (uint32_t)LoadedGeometry.Vertices.size(), TriangleCount, (uint32_t)LoadedInstances.size());

            Judge(LoadedTree.ReadyCondition && LoadedTree.NodeCount > 0,
                  "the load path built a bottom-level tree over the loaded mesh");
            std::printf("     tree: %u nodes, %u leaves, depth %u, %u primitives ordered\n",
                        LoadedTree.NodeCount, LoadedTree.LeafCount, LoadedTree.MaximumDepth,
                        (uint32_t)LoadedTree.PrimitiveOrder.size());

            // 📝 Every triangle must appear in the permutation exactly once. A tree whose leaves collectively name fewer triangles than the mesh
            //    has is not a malformed tree — it is a well-formed tree over a subset, and it traces cleanly while silently missing geometry.
            Judge(LoadedTree.PrimitiveOrder.size() == (size_t)TriangleCount,
                  "the permutation covers every triangle of the loaded mesh exactly once");

            // The real test: does a tree from the real load path survive the real arena? A fresh arena, because the one above is sealed.
            GeometryArenaSubmission LoadArena;
            const bool LoadArenaReady = InitializeGeometryArenaSubmission(LoadArena, Host);
            Judge(LoadArenaReady, "a fresh arena accepts the loaded scene");
            if (LoadArenaReady)
            {
                uint32_t LoadedOrdinal = 0xFFFFFFFFu;
                Judge(AppendGeometryTreeToArena(LoadArena, LoadedTree, 0, 0, LoadedOrdinal),
                      "the loaded tree appends to the arena");
                Judge(UploadGeometryArena(LoadArena, CommandPool), "the loaded tree uploads to the device");

                // 🔴 The mesh ordinal is assigned by the ARENA and must be written back over the instances, which leave the decoder at 0. This is
                //    the step the decoder header warns about: skip it and every instance traces against arena slot 0 while rendering correctly.
                for (SuzanneSceneInstance& Instance : LoadedInstances)
                    Instance.MeshOrdinal = LoadedOrdinal;

                bool EveryInstanceNamesTheMesh = true;
                for (const SuzanneSceneInstance& Instance : LoadedInstances)
                    if (Instance.MeshOrdinal != LoadedOrdinal) EveryInstanceNamesTheMesh = false;
                Judge(EveryInstanceNamesTheMesh && LoadedOrdinal != 0xFFFFFFFFu,
                      "every loaded instance names the arena slot its mesh landed in");

                // The root box must contain the mesh. Checked against the stream's own extent rather than a recomputed tree bound, so a tree
                // built over the WRONG vertices (a transformed copy, say) fails here instead of agreeing with itself.
                float MeshMinimum[3] = {  1e30f,  1e30f,  1e30f };
                float MeshMaximum[3] = { -1e30f, -1e30f, -1e30f };
                for (const RenderVertex& Vertex : LoadedGeometry.Vertices)
                    for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    {
                        if (Vertex.Position[Axis] < MeshMinimum[Axis]) MeshMinimum[Axis] = Vertex.Position[Axis];
                        if (Vertex.Position[Axis] > MeshMaximum[Axis]) MeshMaximum[Axis] = Vertex.Position[Axis];
                    }

                const GeometryArenaSlice LoadedSlice = RetrieveGeometryArenaSlice(LoadArena, LoadedOrdinal);
                std::vector<uint32_t> LoadedNodeWords;
                const bool LoadedNodesRead = ReadDeviceWords(Host, CommandPool, LoadArena.NodeBuffer,
                                                             (size_t)LoadedTree.NodeCount * GeometryTreeWordsPerNode, LoadedNodeWords);
                Judge(LoadedNodesRead, "the loaded tree reads back from the device");

                if (LoadedNodesRead)
                {
                    const DecodedNode LoadedRoot = DecodeNode(LoadedNodeWords, LoadedSlice.NodeOffset);

                    bool RootEnclosesMesh = true;
                    for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    {
                        // Tolerance is a relative slack on the extent: the builder stores float bounds over float positions, so the root should
                        // match to rounding, not be loose. Scaled by the operand per the WithinRoundingTolerance lesson.
                        const float Extent = MeshMaximum[Axis] - MeshMinimum[Axis];
                        const float Slack  = (Extent > 0.0f ? Extent : 1.0f) * 1.0e-4f;
                        if (LoadedRoot.Bounds[Axis]     > MeshMinimum[Axis] + Slack) RootEnclosesMesh = false;
                        if (LoadedRoot.Bounds[Axis + 3] < MeshMaximum[Axis] - Slack) RootEnclosesMesh = false;
                    }
                    Judge(RootEnclosesMesh, "the root box encloses the loaded mesh's own local-space extent");
                    std::printf("     mesh  [%.3f %.3f %.3f] .. [%.3f %.3f %.3f]\n",
                                MeshMinimum[0], MeshMinimum[1], MeshMinimum[2], MeshMaximum[0], MeshMaximum[1], MeshMaximum[2]);
                    std::printf("     root  [%.3f %.3f %.3f] .. [%.3f %.3f %.3f]\n",
                                LoadedRoot.Bounds[0], LoadedRoot.Bounds[1], LoadedRoot.Bounds[2],
                                LoadedRoot.Bounds[3], LoadedRoot.Bounds[4], LoadedRoot.Bounds[5]);
                }
            }
            FinalizeGeometryArenaSubmission(LoadArena);
        }
    }

    // ─── teardown ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    FinalizeGeometryArenaSubmission(Arena);
    Judge(Arena.NodeBuffer == VK_NULL_HANDLE && !Arena.ReadyCondition, "finalize released the arena");

    std::printf("\n==== %s : %u checks, %u failure%s ====\n",
                FailureTally == 0 ? "PASS" : "FAIL", CheckTally, FailureTally, FailureTally == 1 ? "" : "s");

    vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
    FinalizeVulkanHost(Host);
    return FailureTally == 0 ? 0 : 1;
}
