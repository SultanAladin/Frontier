/*==========================================================================================================================================
                                                             MESHBAKEGPU.CPP
==========================================================================================================================================*/
#include "MeshBakeGpu.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Frontier
{
MeshBakeGpuChannelIndex MeshBakeGpuChannelForKey(const std::string &Key)
{
    static const std::array<std::pair<const char *, MeshBakeGpuChannelIndex>, MeshBakeGpuChannelCount> Table{{
        {"normal", MeshBakeGpuChannelIndex::Normal}, {"world-normal", MeshBakeGpuChannelIndex::WorldNormal},
        {"object-normal", MeshBakeGpuChannelIndex::ObjectNormal}, {"bent-normal", MeshBakeGpuChannelIndex::BentNormal},
        {"bent-world-normal", MeshBakeGpuChannelIndex::BentWorldNormal},
        {"bent-object-normal", MeshBakeGpuChannelIndex::BentObjectNormal}, {"bevel-normal", MeshBakeGpuChannelIndex::BevelNormal},
        {"occlusion", MeshBakeGpuChannelIndex::Occlusion}, {"occlusion-secondary", MeshBakeGpuChannelIndex::OcclusionSecondary},
        {"curvature", MeshBakeGpuChannelIndex::Curvature}, {"convexity", MeshBakeGpuChannelIndex::Convexity},
        {"cavity", MeshBakeGpuChannelIndex::Cavity}, {"thickness", MeshBakeGpuChannelIndex::Thickness},
        {"height", MeshBakeGpuChannelIndex::Height}, {"vector-displacement", MeshBakeGpuChannelIndex::VectorDisplacement},
        {"position", MeshBakeGpuChannelIndex::Position}, {"id", MeshBakeGpuChannelIndex::Id},
        {"uv-island", MeshBakeGpuChannelIndex::UvIsland}, {"uv", MeshBakeGpuChannelIndex::Uv},
        {"wireframe", MeshBakeGpuChannelIndex::Wireframe}, {"coverage", MeshBakeGpuChannelIndex::Coverage},
        {"alpha", MeshBakeGpuChannelIndex::Alpha}, {"bevel-mask", MeshBakeGpuChannelIndex::BevelMask},
        {"dust", MeshBakeGpuChannelIndex::Dust}, {"dirt", MeshBakeGpuChannelIndex::Dirt},
        {"edge-wear", MeshBakeGpuChannelIndex::EdgeWear}, {"base-color", MeshBakeGpuChannelIndex::BaseColor},
    }};
    for (const auto &Entry : Table)
        if (Key == Entry.first) return Entry.second;
    return MeshBakeGpuChannelIndex::Count;
}

namespace
{
GpuVec4 ToVec4(const float V[3], float W = 0.f) noexcept { return {V[0], V[1], V[2], W}; }
}

MeshBakeGpuPlan BuildMeshBakeGpuPlan(const BakePart &Target, const BakeMeshSet &High, const BakeSettings &Settings)
{
    MeshBakeGpuPlan Plan;

    // 📝 Low mesh buffers - straight flatten, same vertex/index order as the CPU reference reads them in.
    Plan.LowVertices.reserve(Target.Vertices.size());
    for (const auto &Vertex : Target.Vertices)
        Plan.LowVertices.push_back(GpuLowVertex{ToVec4(Vertex.Position), ToVec4(Vertex.Normal),
            {Vertex.Uv[0], Vertex.Uv[1], float(Vertex.ChartId), 0.f}});
    Plan.LowIndices = Target.Indices;

    // 📝 High mesh BVH - reuse the exact CPU builder (median-split, leaf size 4) so GPU traversal explores the
    // identical tree the CPU path would have, then flatten its already-POD Triangle/Node/Order arrays verbatim.
    BakeTriangleBvh Bvh;
    Bvh.Build(High);
    Plan.HighTriangles.reserve(Bvh.Triangles.size());
    for (const auto &Triangle : Bvh.Triangles)
    {
        GpuTriangle Gpu;
        Gpu.A = ToVec4(Triangle.A); Gpu.B = ToVec4(Triangle.B); Gpu.C = ToVec4(Triangle.C);
        Gpu.NormalA = ToVec4(Triangle.NormalA); Gpu.NormalB = ToVec4(Triangle.NormalB); Gpu.NormalC = ToVec4(Triangle.NormalC);
        Gpu.Meta[0] = Triangle.PartIndex;
        Plan.HighTriangles.push_back(Gpu);
    }
    Plan.BvhNodes.reserve(Bvh.Nodes.size());
    for (const auto &Node : Bvh.Nodes)
    {
        GpuBvhNode Gpu;
        Gpu.Minimum = ToVec4(Node.Minimum); Gpu.Maximum = ToVec4(Node.Maximum);
        Gpu.LeftRightStartCount[0] = Node.Left; Gpu.LeftRightStartCount[1] = Node.Right;
        Gpu.LeftRightStartCount[2] = int32_t(Node.Start); Gpu.LeftRightStartCount[3] = int32_t(Node.Count);
        Plan.BvhNodes.push_back(Gpu);
    }
    Plan.BvhOrder = Bvh.Order;
    if (Plan.BvhNodes.empty()) Plan.BvhNodes.push_back(GpuBvhNode{}); // keep binding 3 non-empty for an empty high mesh

    // 📝 Channel selection - identical filter/order semantics to RunMeshBake's own Channels construction.
    Plan.ChannelSlots.fill(MeshBakeGpuInvalidSlot);
    for (const auto &Key : Settings.Channels)
    {
        if (!IsMeshMapImplemented(Key)) continue;
        const auto Index = uint32_t(MeshBakeGpuChannelForKey(Key));
        if (Index >= MeshBakeGpuChannelCount || Plan.ChannelSlots[Index] != MeshBakeGpuInvalidSlot) continue;
        Plan.ChannelSlots[Index] = uint32_t(Plan.RequestedChannelKeys.size());
        Plan.RequestedChannelKeys.push_back(Key);
    }
    const auto Needs = [&](const char *Key)
    { return std::find(Plan.RequestedChannelKeys.begin(), Plan.RequestedChannelKeys.end(), std::string(Key)) != Plan.RequestedChannelKeys.end(); };
    const bool NeedHemisphereSampling = Needs("occlusion") || Needs("occlusion-secondary") || Needs("bent-normal") ||
        Needs("bent-world-normal") || Needs("bent-object-normal") || Needs("dust") || Needs("dirt") || Needs("edge-wear");
    const bool NeedCurvatureSampling = Needs("curvature") || Needs("convexity") || Needs("cavity") || Needs("dirt") || Needs("edge-wear");
    const bool NeedThickness = Needs("thickness");
    const bool NeedBevel = Needs("bevel-normal") || Needs("bevel-mask");

    // 📝 High mesh bounds/epsilon - identical formula to RunMeshBake's own bounds pass.
    float BoundsMinimum[3]{3.402823e38f, 3.402823e38f, 3.402823e38f}, BoundsMaximum[3]{-3.402823e38f, -3.402823e38f, -3.402823e38f};
    for (const auto &Part : High.Parts)
        for (const auto &Vertex : Part.Vertices)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                BoundsMinimum[Axis] = std::min(BoundsMinimum[Axis], Vertex.Position[Axis]);
                BoundsMaximum[Axis] = std::max(BoundsMaximum[Axis], Vertex.Position[Axis]);
            }
    const float Diagonal[3]{BoundsMaximum[0] - BoundsMinimum[0], BoundsMaximum[1] - BoundsMinimum[1], BoundsMaximum[2] - BoundsMinimum[2]};
    const float DiagonalLength = std::sqrt(Diagonal[0] * Diagonal[0] + Diagonal[1] * Diagonal[1] + Diagonal[2] * Diagonal[2]);
    const float Epsilon = std::max(DiagonalLength * 1e-5f, 1e-7f);

    // 📝 Dust axis - resolved once here (constant per bake) instead of re-parsed every texel like the CPU path.
    float DustUp[3]{0, 0, 0};
    if (Settings.DustAxis.size() == 2)
    {
        const int Axis = Settings.DustAxis[1] == 'X' ? 0 : Settings.DustAxis[1] == 'Y' ? 1 : Settings.DustAxis[1] == 'Z' ? 2 : 1;
        DustUp[Axis] = Settings.DustAxis[0] == '-' ? -1.f : 1.f;
    }

    auto &S = Plan.Settings;
    S.ObjectToWorldRow0 = {Target.ObjectToWorld[0], Target.ObjectToWorld[1], Target.ObjectToWorld[2], Target.ObjectToWorld[3]};
    S.ObjectToWorldRow1 = {Target.ObjectToWorld[4], Target.ObjectToWorld[5], Target.ObjectToWorld[6], Target.ObjectToWorld[7]};
    S.ObjectToWorldRow2 = {Target.ObjectToWorld[8], Target.ObjectToWorld[9], Target.ObjectToWorld[10], Target.ObjectToWorld[11]};
    S.BoundsMin = ToVec4(BoundsMinimum);
    S.BoundsMax = ToVec4(BoundsMaximum);
    S.DiagonalEpsilon = {Diagonal[0], Diagonal[1], Diagonal[2], Epsilon};
    S.FrontBackAo = {Settings.Front, Settings.Back, Settings.AoDistance, Settings.SecondaryAoDistance};
    S.ThicknessCurvWireMask = {Settings.ThicknessRange, Settings.CurvatureRadius, Settings.WireWidth, Settings.MaskStrength};
    S.BevelRadiusStrength = {Settings.BevelRadius, Settings.BevelStrength, 0, 0};
    S.DustUpFalloff = {DustUp[0], DustUp[1], DustUp[2], Settings.DustFalloff};
    const uint32_t Resolution = std::max(8u, std::min(1024u, Settings.Resolution));
    S.SampleCounts[0] = std::max(1u, Settings.Samples);
    S.SampleCounts[1] = std::max(1u, Settings.BevelSamples);
    S.SampleCounts[2] = Resolution;
    S.SampleCounts[3] = uint32_t(Plan.LowIndices.size());
    uint32_t NeedsMask = 0;
    if (NeedHemisphereSampling) NeedsMask |= MeshBakeGpuNeedHemisphereSampling;
    if (NeedCurvatureSampling) NeedsMask |= MeshBakeGpuNeedCurvatureSampling;
    if (NeedThickness) NeedsMask |= MeshBakeGpuNeedThickness;
    if (NeedBevel) NeedsMask |= MeshBakeGpuNeedBevel;
    S.Flags[0] = NeedsMask;
    S.Flags[1] = Settings.NormalPlusY ? 1u : 0u;
    return Plan;
}

namespace
{
void ConfirmBakeResult(VkResult Result)
{
    if (Result != VK_SUCCESS) throw std::runtime_error("Vulkan mesh bake failed: " + std::to_string(int(Result)));
}
}

struct MeshBakeGpuRasterization::BakeAllocation
{
    VulkanRasterAccess Access;
    VkPhysicalDeviceMemoryProperties Memory{};
    VkBuffer LowVertices = VK_NULL_HANDLE, LowIndices = VK_NULL_HANDLE, HighTriangles = VK_NULL_HANDLE,
        BvhNodes = VK_NULL_HANDLE, BvhOrder = VK_NULL_HANDLE, Settings = VK_NULL_HANDLE, ChannelSlots = VK_NULL_HANDLE,
        Output = VK_NULL_HANDLE;
    VkDeviceMemory LowVertexMemory = VK_NULL_HANDLE, LowIndexMemory = VK_NULL_HANDLE, HighTriangleMemory = VK_NULL_HANDLE,
        BvhNodeMemory = VK_NULL_HANDLE, BvhOrderMemory = VK_NULL_HANDLE, SettingsMemory = VK_NULL_HANDLE,
        ChannelSlotMemory = VK_NULL_HANDLE, OutputMemory = VK_NULL_HANDLE;
    VkDeviceSize OutputBytes = 0;
    uint32_t Resolution = 0, RequestedChannels = 0;
    std::vector<std::string> RequestedChannelKeys;
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Descriptor = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    uint64_t LastSubmission = 0, PendingSubmission = 0, Dispatches = 0;
    bool Dirty = false, Recorded = false, ReadbackRecorded = false, ReadbackAvailable = false;
#define FRONTIER_MESHBAKE_FUNCTIONS(Function) \
    Function(CreateBuffer) Function(DestroyBuffer) Function(GetBufferMemoryRequirements) Function(AllocateMemory) \
    Function(FreeMemory) Function(BindBufferMemory) Function(MapMemory) Function(UnmapMemory) \
    Function(CreateDescriptorSetLayout) Function(DestroyDescriptorSetLayout) Function(CreateDescriptorPool) \
    Function(DestroyDescriptorPool) Function(AllocateDescriptorSets) Function(UpdateDescriptorSets) \
    Function(CreatePipelineLayout) Function(DestroyPipelineLayout) Function(CreateShaderModule) Function(DestroyShaderModule) \
    Function(CreateComputePipelines) Function(DestroyPipeline) Function(CmdPipelineBarrier) Function(CmdBindPipeline) \
    Function(CmdBindDescriptorSets) Function(CmdDispatch)
#define FRONTIER_MESHBAKE_POINTER(Name) PFN_vk##Name Name = nullptr;
    FRONTIER_MESHBAKE_FUNCTIONS(FRONTIER_MESHBAKE_POINTER)
#undef FRONTIER_MESHBAKE_POINTER
    ~BakeAllocation()
    {
        if (!Access.Device) return;
        if (Pipeline) DestroyPipeline(Access.Device, Pipeline, nullptr);
        if (Layout) DestroyPipelineLayout(Access.Device, Layout, nullptr);
        if (Pool) DestroyDescriptorPool(Access.Device, Pool, nullptr);
        if (DescriptorLayout) DestroyDescriptorSetLayout(Access.Device, DescriptorLayout, nullptr);
        for (auto Buffer : {LowVertices, LowIndices, HighTriangles, BvhNodes, BvhOrder, Settings, ChannelSlots, Output})
            if (Buffer) DestroyBuffer(Access.Device, Buffer, nullptr);
        for (auto Storage : {LowVertexMemory, LowIndexMemory, HighTriangleMemory, BvhNodeMemory, BvhOrderMemory, SettingsMemory,
                 ChannelSlotMemory, OutputMemory})
            if (Storage) FreeMemory(Access.Device, Storage, nullptr);
    }
    void Allocate(VkMemoryRequirements Requirements, VkMemoryPropertyFlags Properties, VkDeviceMemory &Storage)
    {
        uint32_t Selection = UINT32_MAX;
        for (uint32_t Index = 0; Index < Memory.memoryTypeCount; ++Index)
            if ((Requirements.memoryTypeBits & (1u << Index)) && (Memory.memoryTypes[Index].propertyFlags & Properties) == Properties)
                { Selection = Index; break; }
        if (Selection == UINT32_MAX) throw std::runtime_error("Vulkan mesh bake memory category is unavailable.");
        VkMemoryAllocateInfo Creation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Creation.allocationSize = Requirements.size; Creation.memoryTypeIndex = Selection;
        ConfirmBakeResult(AllocateMemory(Access.Device, &Creation, nullptr, &Storage));
    }
    // 📝 Every buffer here is host-visible/coherent and written or read directly by name, the same established
    // pattern VectorCoverageRasterization uses for its Instructions/Segments/TileDirectory/TileIndices buffers -
    // there is no device-local staging step because this dispatch is a one-shot batch bake, not a per-frame draw.
    void AllocateBuffer(VkDeviceSize Bytes, VkBuffer &Buffer, VkDeviceMemory &Storage)
    {
        VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Creation.size = std::max<VkDeviceSize>(Bytes, 4); Creation.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ConfirmBakeResult(CreateBuffer(Access.Device, &Creation, nullptr, &Buffer));
        VkMemoryRequirements Requirements; GetBufferMemoryRequirements(Access.Device, Buffer, &Requirements);
        Allocate(Requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Storage);
        ConfirmBakeResult(BindBufferMemory(Access.Device, Buffer, Storage, 0));
    }
    void Upload(VkDeviceMemory Storage, const void *Source, size_t Bytes)
    {
        if (!Bytes) return;
        void *Destination = nullptr;
        ConfirmBakeResult(MapMemory(Access.Device, Storage, 0, Bytes, 0, &Destination));
        std::memcpy(Destination, Source, Bytes);
        UnmapMemory(Access.Device, Storage);
    }
};

MeshBakeGpuRasterization::MeshBakeGpuRasterization() = default;
MeshBakeGpuRasterization::~MeshBakeGpuRasterization() = default;

void MeshBakeGpuRasterization::Initialize(const VulkanRasterAccess &Access, RasterShader Shader)
{
    if (Allocation || !Access.Device || !Access.PhysicalDevice || !Access.ResolveDevice || !Access.QueryMemory || !Access.QueryLimits ||
        Shader.Words.size() < 5 || Shader.Words.size() > 4 * 1024 * 1024 || Shader.Words[0] != 0x07230203 || !Shader.Entry)
        throw std::runtime_error("Invalid Vulkan mesh bake initialization.");
    if (!(Access.QueueCapabilities & VK_QUEUE_COMPUTE_BIT))
        throw std::runtime_error("Mesh bake requires an explicitly verified compute-capable command queue.");
    auto Resident = std::make_unique<BakeAllocation>();
    Resident->Access = Access;
#define FRONTIER_MESHBAKE_RESOLVE(Name) \
    Resident->Name = reinterpret_cast<PFN_vk##Name>(Access.ResolveDevice(Access.Device, "vk" #Name)); \
    if (!Resident->Name) throw std::runtime_error("Missing Vulkan mesh bake function: vk" #Name);
    FRONTIER_MESHBAKE_FUNCTIONS(FRONTIER_MESHBAKE_RESOLVE)
#undef FRONTIER_MESHBAKE_RESOLVE
    Access.QueryMemory(Access.PhysicalDevice, &Resident->Memory);
    VkPhysicalDeviceProperties Limits{}; Access.QueryLimits(Access.PhysicalDevice, &Limits);
    if (Limits.limits.maxPerStageDescriptorStorageBuffers < 8 || Limits.limits.maxDescriptorSetStorageBuffers < 8 ||
        Limits.limits.maxBoundDescriptorSets < 1 || Limits.limits.maxComputeWorkGroupInvocations < 64 ||
        Limits.limits.maxComputeWorkGroupSize[0] < 8 || Limits.limits.maxComputeWorkGroupSize[1] < 8)
        throw std::runtime_error("Vulkan device cannot support bounded mesh baking.");

    const VkDescriptorSetLayoutBinding Bindings[8]{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo Descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    Descriptor.bindingCount = 8; Descriptor.pBindings = Bindings;
    ConfirmBakeResult(Resident->CreateDescriptorSetLayout(Access.Device, &Descriptor, nullptr, &Resident->DescriptorLayout));
    const VkDescriptorPoolSize PoolSizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}};
    VkDescriptorPoolCreateInfo Pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    Pool.maxSets = 1; Pool.poolSizeCount = 1; Pool.pPoolSizes = PoolSizes;
    ConfirmBakeResult(Resident->CreateDescriptorPool(Access.Device, &Pool, nullptr, &Resident->Pool));
    VkDescriptorSetAllocateInfo Selection{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    Selection.descriptorPool = Resident->Pool; Selection.descriptorSetCount = 1; Selection.pSetLayouts = &Resident->DescriptorLayout;
    ConfirmBakeResult(Resident->AllocateDescriptorSets(Access.Device, &Selection, &Resident->Descriptor));
    VkPipelineLayoutCreateInfo Layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    Layout.setLayoutCount = 1; Layout.pSetLayouts = &Resident->DescriptorLayout;
    ConfirmBakeResult(Resident->CreatePipelineLayout(Access.Device, &Layout, nullptr, &Resident->Layout));
    VkShaderModule ShaderLibrary = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo Source{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    Source.codeSize = Shader.Words.size_bytes(); Source.pCode = Shader.Words.data();
    ConfirmBakeResult(Resident->CreateShaderModule(Access.Device, &Source, nullptr, &ShaderLibrary));
    VkComputePipelineCreateInfo Pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    Pipeline.layout = Resident->Layout;
    Pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, ShaderLibrary,
        Shader.Entry, nullptr};
    const auto Result = Resident->CreateComputePipelines(Access.Device, VK_NULL_HANDLE, 1, &Pipeline, nullptr, &Resident->Pipeline);
    Resident->DestroyShaderModule(Access.Device, ShaderLibrary, nullptr);
    ConfirmBakeResult(Result);
    Allocation = std::move(Resident);
}

void MeshBakeGpuRasterization::ConfigureRetiredPlan(const MeshBakeGpuPlan &Plan, uint64_t CompletedSerial)
{
    if (!Allocation || Allocation->LastSubmission > CompletedSerial || Allocation->Recorded || Allocation->ReadbackRecorded)
        throw std::runtime_error("Mesh bake descriptors are not retired; defer the upload without waiting.");
    if (Plan.LowVertices.empty() || Plan.LowIndices.empty() || Plan.RequestedChannelKeys.empty())
        throw std::runtime_error("Mesh bake plan has no geometry or no requested channels.");
    auto &Resident = *Allocation;
    for (auto Buffer : {&Resident.LowVertices, &Resident.LowIndices, &Resident.HighTriangles, &Resident.BvhNodes, &Resident.BvhOrder,
             &Resident.Settings, &Resident.ChannelSlots, &Resident.Output})
        if (*Buffer) { Resident.DestroyBuffer(Resident.Access.Device, *Buffer, nullptr); *Buffer = VK_NULL_HANDLE; }
    for (auto Storage : {&Resident.LowVertexMemory, &Resident.LowIndexMemory, &Resident.HighTriangleMemory, &Resident.BvhNodeMemory,
             &Resident.BvhOrderMemory, &Resident.SettingsMemory, &Resident.ChannelSlotMemory, &Resident.OutputMemory})
        if (*Storage) { Resident.FreeMemory(Resident.Access.Device, *Storage, nullptr); *Storage = VK_NULL_HANDLE; }

    Resident.AllocateBuffer(Plan.LowVertices.size() * sizeof(GpuLowVertex), Resident.LowVertices, Resident.LowVertexMemory);
    Resident.Upload(Resident.LowVertexMemory, Plan.LowVertices.data(), Plan.LowVertices.size() * sizeof(GpuLowVertex));
    Resident.AllocateBuffer(Plan.LowIndices.size() * sizeof(uint32_t), Resident.LowIndices, Resident.LowIndexMemory);
    Resident.Upload(Resident.LowIndexMemory, Plan.LowIndices.data(), Plan.LowIndices.size() * sizeof(uint32_t));
    Resident.AllocateBuffer(Plan.HighTriangles.size() * sizeof(GpuTriangle), Resident.HighTriangles, Resident.HighTriangleMemory);
    Resident.Upload(Resident.HighTriangleMemory, Plan.HighTriangles.data(), Plan.HighTriangles.size() * sizeof(GpuTriangle));
    Resident.AllocateBuffer(Plan.BvhNodes.size() * sizeof(GpuBvhNode), Resident.BvhNodes, Resident.BvhNodeMemory);
    Resident.Upload(Resident.BvhNodeMemory, Plan.BvhNodes.data(), Plan.BvhNodes.size() * sizeof(GpuBvhNode));
    const std::vector<uint32_t> OrderOrPlaceholder = Plan.BvhOrder.empty() ? std::vector<uint32_t>{0} : Plan.BvhOrder;
    Resident.AllocateBuffer(OrderOrPlaceholder.size() * sizeof(uint32_t), Resident.BvhOrder, Resident.BvhOrderMemory);
    Resident.Upload(Resident.BvhOrderMemory, OrderOrPlaceholder.data(), OrderOrPlaceholder.size() * sizeof(uint32_t));
    Resident.AllocateBuffer(sizeof(GpuBakeSettings), Resident.Settings, Resident.SettingsMemory);
    Resident.Upload(Resident.SettingsMemory, &Plan.Settings, sizeof(GpuBakeSettings));
    Resident.AllocateBuffer(Plan.ChannelSlots.size() * sizeof(uint32_t), Resident.ChannelSlots, Resident.ChannelSlotMemory);
    Resident.Upload(Resident.ChannelSlotMemory, Plan.ChannelSlots.data(), Plan.ChannelSlots.size() * sizeof(uint32_t));

    const uint32_t Resolution = Plan.Settings.SampleCounts[2];
    Resident.OutputBytes = VkDeviceSize(Resolution) * Resolution * Plan.RequestedChannelKeys.size() * sizeof(uint32_t);
    Resident.AllocateBuffer(Resident.OutputBytes, Resident.Output, Resident.OutputMemory);
    Resident.Resolution = Resolution;
    Resident.RequestedChannels = uint32_t(Plan.RequestedChannelKeys.size());
    Resident.RequestedChannelKeys = Plan.RequestedChannelKeys;

    const VkDescriptorBufferInfo BufferInfos[8]{{Resident.LowVertices, 0, VK_WHOLE_SIZE}, {Resident.LowIndices, 0, VK_WHOLE_SIZE},
        {Resident.HighTriangles, 0, VK_WHOLE_SIZE}, {Resident.BvhNodes, 0, VK_WHOLE_SIZE}, {Resident.BvhOrder, 0, VK_WHOLE_SIZE},
        {Resident.Settings, 0, VK_WHOLE_SIZE}, {Resident.ChannelSlots, 0, VK_WHOLE_SIZE}, {Resident.Output, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet Writes[8]{};
    for (uint32_t Index = 0; Index < 8; ++Index)
    {
        Writes[Index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet = Resident.Descriptor; Writes[Index].dstBinding = Index; Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; Writes[Index].pBufferInfo = &BufferInfos[Index];
    }
    Resident.UpdateDescriptorSets(Resident.Access.Device, 8, Writes, 0, nullptr);
    Resident.Dirty = true;
}

bool MeshBakeGpuRasterization::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation || !Allocation->Dirty) return false;
    auto &Resident = *Allocation;
    if (!Commands || Resident.LastSubmission > CompletedSerial || SubmissionSerial <= CompletedSerial || Resident.Recorded ||
        Resident.ReadbackRecorded)
        throw std::runtime_error("Mesh bake output is not retired or recording serial is invalid.");
    Resident.CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Resident.Pipeline);
    Resident.CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Resident.Layout, 0, 1, &Resident.Descriptor, 0, nullptr);
    Resident.CmdDispatch(Commands, (Resident.Resolution + 7) / 8, (Resident.Resolution + 7) / 8, 1);
    Resident.Recorded = true; Resident.PendingSubmission = SubmissionSerial;
    return true;
}

void MeshBakeGpuRasterization::ConstructReadbackCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation || !Commands || !Allocation->Recorded || Allocation->ReadbackRecorded || Allocation->ReadbackAvailable ||
        SubmissionSerial <= CompletedSerial || Allocation->PendingSubmission != SubmissionSerial)
        throw std::runtime_error("Mesh bake output is not ready for a retired readback recording.");
    auto &Resident = *Allocation;
    // 📝 Output is host-visible/coherent and never sampled by another shader stage, so the only barrier the
    // Vulkan spec actually requires here is write-visibility to the host, not an image layout transition.
    VkMemoryBarrier Host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    Host.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; Host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    Resident.CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Host, 0, nullptr, 0,
        nullptr);
    Resident.ReadbackRecorded = true;
}

std::vector<BakeMapImage> MeshBakeGpuRasterization::RetrieveReadback(uint64_t CompletedSerial)
{
    if (!Allocation || !Allocation->ReadbackAvailable || Allocation->LastSubmission > CompletedSerial)
        throw std::runtime_error("Mesh bake readback has not retired.");
    auto &Resident = *Allocation;
    void *Mapped = nullptr;
    ConfirmBakeResult(Resident.MapMemory(Resident.Access.Device, Resident.OutputMemory, 0, Resident.OutputBytes, 0, &Mapped));
    const auto *Bytes = static_cast<const uint8_t *>(Mapped);
    std::vector<BakeMapImage> Results;
    const size_t PlaneBytes = size_t(Resident.Resolution) * Resident.Resolution * 4;
    for (uint32_t Channel = 0; Channel < Resident.RequestedChannels; ++Channel)
    {
        BakeMapImage Image{Resident.RequestedChannelKeys[Channel], std::vector<uint8_t>(PlaneBytes)};
        std::memcpy(Image.Rgba.data(), Bytes + size_t(Channel) * PlaneBytes, PlaneBytes);
        Results.push_back(std::move(Image));
    }
    Resident.UnmapMemory(Resident.Access.Device, Resident.OutputMemory);
    Resident.ReadbackAvailable = false;
    return Results;
}

void MeshBakeGpuRasterization::ConfirmSubmission() noexcept
{
    if (!Allocation) return;
    auto &Resident = *Allocation;
    if (Resident.Recorded || Resident.ReadbackRecorded)
    {
        Resident.LastSubmission = Resident.PendingSubmission;
        if (Resident.Recorded) { Resident.Recorded = false; Resident.Dirty = false; ++Resident.Dispatches; }
        if (Resident.ReadbackRecorded) { Resident.ReadbackRecorded = false; Resident.ReadbackAvailable = true; }
    }
}
void MeshBakeGpuRasterization::Finalize() noexcept { Allocation.reset(); }
uint64_t MeshBakeGpuRasterization::QueryDispatchCount() const noexcept { return Allocation ? Allocation->Dispatches : 0; }
} // namespace Frontier
