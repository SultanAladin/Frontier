/*============================================================================================================================================
                                                       PROGRESSIVESTRETCHRELAXER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ProgressiveStretchRelaxer dispatching StretchRelaxCompute.comp.spv on Vulkan 1.2 Timeline Semaphores

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/ProgressiveStretchRelaxer.h"

#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Frontier::Workspaces
{

namespace
{

struct GPURelaxCornerRecord
{
    float    CurrentUV[2]         {};
    float    NeighborAvgUV[2]     {};
    float    IsometricTargetUV[2] {};
    uint32_t PinnedOrSeamFlag     = 0u;
    float    DampingWeight        = 1.0f;
};

struct GPURelaxDispatchParams
{
    uint32_t CornerCount   = 0u;
    float    StepScale     = 0.35f;
    uint32_t PreserveSeams = 1u;
    uint32_t ReservedPad   = 0u;
};

} // namespace

ProgressiveStretchRelaxer::~ProgressiveStretchRelaxer()
{
    TeardownGPUKernel();
}

bool ProgressiveStretchRelaxer::InitializeGPUKernel(
    GeometricRaster::VulkanDeviceContext* Context,
    const std::filesystem::path&          CompiledShaderDir)
{
    TeardownGPUKernel();
    if (Context == nullptr || !Context->Ready())
    {
        return false;
    }
    VkContext = Context;
    if (!Encoder.InitializeEncoder(VkContext, 64u))
    {
        VkContext = nullptr;
        return false;
    }

    const std::array<VkDescriptorType, 3u> Bindings {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    RelaxProgramHandle = Encoder.RegisterProgram(CompiledShaderDir / "StretchRelaxCompute.comp.spv", Bindings, "ComputeEntry");
    return RelaxProgramHandle != UINT32_MAX;
}

void ProgressiveStretchRelaxer::TeardownGPUKernel() noexcept
{
    Encoder.TeardownEncoder();
    RelaxProgramHandle = UINT32_MAX;
    VkContext          = nullptr;
}

StretchRelaxReport ProgressiveStretchRelaxer::RelaxStretch(
    GeometrySpace::PolygonNetwork& Network,
    uint32_t                       IterationPasses,
    float                          StepScale,
    bool                           PreserveSeams)
{
    const auto StartClock = std::chrono::steady_clock::now();
    StretchRelaxReport Report {};

    const std::vector<UVIslandPartition> Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    if (Islands.empty() || IterationPasses == 0u)
    {
        return Report;
    }

    // Group welded UV corners per (IslandOrdinal, OriginVertexIndex) so welded edges move coherently without tearing
    struct WeldedNode
    {
        uint32_t              VertexIndex      = 0u;
        uint32_t              PinnedOrSeamFlag = 0u;
        Mathematics::UVAxes   CurrentUV        {};
        std::vector<uint32_t> MemberEdges      {};
        std::vector<uint32_t> NeighborNodes    {};
        std::vector<float>    RestLengths3D    {};
        float                 UVPerMeter       = 0.25f;
    };

    std::vector<WeldedNode> Nodes;
    for (const auto& Island : Islands)
    {
        const float IslandScale = (Island.TotalArea3D > 1.0e-6f && Island.TotalAreaUV > 1.0e-8f)
            ? std::sqrt(Island.TotalAreaUV / Island.TotalArea3D)
            : 0.25f;

        std::unordered_map<uint32_t, uint32_t> VertToNode;
        for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
        {
            const auto&    Edge = Network.Edges[EdgeIdx];
            const uint32_t VIdx = Edge.OriginVertexIndex;
            auto [It, Inserted] = VertToNode.try_emplace(VIdx, static_cast<uint32_t>(Nodes.size()));
            if (Inserted)
            {
                WeldedNode Node {};
                Node.VertexIndex = VIdx;
                Node.CurrentUV   = Edge.ParameterUV;
                Node.UVPerMeter  = IslandScale;
                Nodes.push_back(std::move(Node));
            }
            Nodes[It->second].MemberEdges.push_back(EdgeIdx);
        }

        for (const uint32_t BVert : Island.OrderedBoundaryVertices)
        {
            const auto It = VertToNode.find(BVert);
            if (It != VertToNode.end())
            {
                Nodes[It->second].PinnedOrSeamFlag = PreserveSeams ? 1u : 2u;
            }
        }

        for (const uint32_t EdgeIdx : Island.CornerEdgeIndices)
        {
            const auto&    Edge  = Network.Edges[EdgeIdx];
            const uint32_t V0    = Edge.OriginVertexIndex;
            const uint32_t V1    = Network.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
            const uint32_t N0    = VertToNode[V0];
            const uint32_t N1    = VertToNode[V1];
            const auto&    P0    = Network.Vertices[V0].Axes;
            const auto&    P1    = Network.Vertices[V1].Axes;
            const float    Len3D = Mathematics::EvaluateDistance(P0, P1);

            Nodes[N0].NeighborNodes.push_back(N1);
            Nodes[N0].RestLengths3D.push_back(Len3D);
            Nodes[N1].NeighborNodes.push_back(N0);
            Nodes[N1].RestLengths3D.push_back(Len3D);
        }
    }

    const uint32_t NodeCount = static_cast<uint32_t>(Nodes.size());
    if (NodeCount == 0u)
    {
        return Report;
    }

    const bool UseGPU = (VkContext != nullptr && VkContext->Ready() && RelaxProgramHandle != UINT32_MAX);
    GeometricRaster::DeviceBufferResource ParamBuf {};
    GeometricRaster::DeviceBufferResource InputBuf {};
    GeometricRaster::DeviceBufferResource OutputBuf {};

    if (UseGPU)
    {
        ParamBuf  = Encoder.AllocateHostWritableBuffer(sizeof(GPURelaxDispatchParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        InputBuf  = Encoder.AllocateHostWritableBuffer(sizeof(GPURelaxCornerRecord) * NodeCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        OutputBuf = Encoder.AllocateHostWritableBuffer(sizeof(float) * 2u * NodeCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    std::vector<GPURelaxCornerRecord> Records(NodeCount);
    const uint32_t ClampedPasses = std::clamp(IterationPasses, 1u, 64u);

    for (uint32_t Pass = 0u; Pass < ClampedPasses; ++Pass)
    {
        for (uint32_t Idx = 0u; Idx < NodeCount; ++Idx)
        {
            const auto& Node = Nodes[Idx];
            GPURelaxCornerRecord Rec {};
            Rec.CurrentUV[0]     = Node.CurrentUV.U;
            Rec.CurrentUV[1]     = Node.CurrentUV.V;
            Rec.PinnedOrSeamFlag = Node.PinnedOrSeamFlag;
            Rec.DampingWeight    = 0.75f;

            if (Node.NeighborNodes.empty())
            {
                Rec.NeighborAvgUV[0]     = Node.CurrentUV.U;
                Rec.NeighborAvgUV[1]     = Node.CurrentUV.V;
                Rec.IsometricTargetUV[0] = Node.CurrentUV.U;
                Rec.IsometricTargetUV[1] = Node.CurrentUV.V;
            }
            else
            {
                float AvgU = 0.0f, AvgV = 0.0f;
                float IsoU = 0.0f, IsoV = 0.0f;
                for (std::size_t K = 0u; K < Node.NeighborNodes.size(); ++K)
                {
                    const auto& NbUV  = Nodes[Node.NeighborNodes[K]].CurrentUV;
                    AvgU += NbUV.U;
                    AvgV += NbUV.V;

                    const float dU   = Node.CurrentUV.U - NbUV.U;
                    const float dV   = Node.CurrentUV.V - NbUV.V;
                    const float CurL = std::hypot(dU, dV);
                    const float Rest = std::max(1.0e-5f, Node.RestLengths3D[K] * Node.UVPerMeter);
                    if (CurL > 1.0e-6f)
                    {
                        IsoU += NbUV.U + (dU / CurL) * Rest;
                        IsoV += NbUV.V + (dV / CurL) * Rest;
                    }
                    else
                    {
                        IsoU += Node.CurrentUV.U;
                        IsoV += Node.CurrentUV.V;
                    }
                }
                const float InvK = 1.0f / static_cast<float>(Node.NeighborNodes.size());
                Rec.NeighborAvgUV[0]     = AvgU * InvK;
                Rec.NeighborAvgUV[1]     = AvgV * InvK;
                Rec.IsometricTargetUV[0] = IsoU * InvK;
                Rec.IsometricTargetUV[1] = IsoV * InvK;
            }
            Records[Idx] = Rec;
        }

        if (UseGPU)
        {
            Encoder.ResetTransientDescriptors();
            const GPURelaxDispatchParams Params { NodeCount, StepScale, PreserveSeams ? 1u : 0u, 0u };
            const std::array<GPURelaxDispatchParams, 1u> PSpan {{ Params }};
            Encoder.UploadSpanToBuffer<GPURelaxDispatchParams>(ParamBuf, PSpan);
            Encoder.UploadSpanToBuffer<GPURelaxCornerRecord>(InputBuf, Records);

            VkCommandBuffer Cmd = VkContext->BeginSingleTimeCommands();
            const std::array<GeometricRaster::ComputeBindingDescriptor, 3u> Binds {{
                { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ParamBuf.BufferHandle, sizeof(GPURelaxDispatchParams) },
                { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InputBuf.BufferHandle, VK_WHOLE_SIZE },
                { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputBuf.BufferHandle, VK_WHOLE_SIZE },
            }};
            (void)Encoder.EncodeComputeDispatch(Cmd, RelaxProgramHandle, Binds, (NodeCount + 63u) / 64u);
            const uint64_t Signal = VkContext->DispatchCommandBufferAsync(Cmd);
            (void)VkContext->AwaitTimelineValue(Signal);
            vkFreeCommandBuffers(VkContext->AccessDevice(), VkContext->AccessCommandPool(), 1u, &Cmd);

            const auto* OutPairs = static_cast<const float*>(OutputBuf.MappedPtr);
            for (uint32_t Idx = 0u; Idx < NodeCount; ++Idx)
            {
                Nodes[Idx].CurrentUV = { OutPairs[Idx * 2u + 0u], OutPairs[Idx * 2u + 1u] };
            }
        }
        else
        {
            for (uint32_t Idx = 0u; Idx < NodeCount; ++Idx)
            {
                const auto& Rec = Records[Idx];
                if ((Rec.PinnedOrSeamFlag & 1u) != 0u)
                {
                    continue;
                }
                const float BlendU = Rec.NeighborAvgUV[0] * 0.45f + Rec.IsometricTargetUV[0] * 0.55f;
                const float BlendV = Rec.NeighborAvgUV[1] * 0.45f + Rec.IsometricTargetUV[1] * 0.55f;
                const float T      = std::clamp(StepScale * Rec.DampingWeight, 0.0f, 1.0f);
                Nodes[Idx].CurrentUV.U += (BlendU - Nodes[Idx].CurrentUV.U) * T;
                Nodes[Idx].CurrentUV.V += (BlendV - Nodes[Idx].CurrentUV.V) * T;
            }
        }
    }

    if (UseGPU)
    {
        VkContext->ReleaseBuffer(ParamBuf);
        VkContext->ReleaseBuffer(InputBuf);
        VkContext->ReleaseBuffer(OutputBuf);
    }

    // Commit relaxed coordinates with strict positive CCW signed-area verification (zero flips guaranteed)
    for (const auto& Node : Nodes)
    {
        std::vector<Mathematics::UVAxes> PreviousUVs;
        PreviousUVs.reserve(Node.MemberEdges.size());
        for (const uint32_t EdgeIdx : Node.MemberEdges)
        {
            PreviousUVs.push_back(Network.Edges[EdgeIdx].ParameterUV);
            Network.Edges[EdgeIdx].ParameterUV = Node.CurrentUV;
        }

        bool InvertedAnyFace = false;
        for (const uint32_t EdgeIdx : Node.MemberEdges)
        {
            if (Network.EvaluateFaceSignedAreaUV(Network.Edges[EdgeIdx].EnclosingFaceIndex) <= 1.0e-7f)
            {
                InvertedAnyFace = true;
                break;
            }
        }
        if (InvertedAnyFace)
        {
            for (std::size_t K = 0u; K < Node.MemberEdges.size(); ++K)
            {
                Network.Edges[Node.MemberEdges[K]].ParameterUV = PreviousUVs[K];
            }
        }
    }

    ++Network.UVRevision;

    Report.WeldedCornersRelaxed = NodeCount;
    Report.ExecutedPasses       = ClampedPasses;
    Report.ExecutedOnVulkanGPU  = UseGPU;
    Report.ElapsedMilliseconds  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartClock).count();
    return Report;
}

} // namespace Frontier::Workspaces
