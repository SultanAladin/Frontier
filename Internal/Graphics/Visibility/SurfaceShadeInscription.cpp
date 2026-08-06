/*==============================================================================================================================================
                                                       SURFACESHADEINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the surface shade. Initialize reads the two SPIR-V modules (the fullscreen-triangle vertex stage is VisibilityInscription's,
//    reused verbatim), builds an eight-binding descriptor set layout + pool + set, one nearest sampler, the owned material UBO, a pipeline layout
//    carrying that set and the ShadeConstants push range, and a graphics pipeline configured for dynamic rendering against the swapchain colour format
//    (alpha-over blend, no depth, no vertex input). Refresh points the set at the borrowed visibility image view and the mesh / instance buffers.
//    Record binds the pipeline + set, pushes the constants, and draws the three-vertex triangle inside the caller's open colour scope.
//
//    The structure deliberately mirrors VisibilityInscription.cpp — same helper shapes, same failure-unwind pattern, same idempotent-Refresh rule —
//    so the two units read as one family rather than two dialects. What differs is only the binding count and the owned UBO.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/SurfaceShadeInscription.h"
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// The descriptor bindings, named so the layout / pool / write code below cannot drift out of step with the shader's declarations.
constexpr uint32_t BindingVisibilityImage = 0;
constexpr uint32_t BindingMeshVertices    = 1;
constexpr uint32_t BindingMeshIndices     = 2;
constexpr uint32_t BindingInstances       = 3;
constexpr uint32_t BindingMaterials       = 4;
constexpr uint32_t BindingFloorVertices   = 5;
constexpr uint32_t BindingFloorIndices    = 6;
constexpr uint32_t BindingFloorInstances  = 7;
constexpr uint32_t BindingCount           = 8;

// The surfel-field set (set 1) — the W298 GI gather's read side. Three of SurfelStore's buffers plus its depth-moment atlas, numbered to match
// SurfaceShade.frag's set-1 declarations exactly. 🔴 THE ORDER IS THE CONTRACT AND IT HAS NO DIAGNOSTIC: b0..b2 are all storage buffers, so swapping two
// of them satisfies the layout, passes validation, and hands the gather the cell table as surfel records — a field of plausible nonsense.
constexpr uint32_t SurfelBindingRecords    = 0;   // b0 Surfel[] records, stride 100      (ro)
constexpr uint32_t SurfelBindingCellSpans  = 1;   // b1 SurfelCellSpan[] occupancy        (ro)
constexpr uint32_t SurfelBindingCellList   = 2;   // b2 cell -> surfel ordinal table      (ro)
constexpr uint32_t SurfelBindingDepthAtlas = 3;   // b3 R32G32_SFLOAT depth moments   (sampled)
constexpr uint32_t SurfelBindingCount      = 4;

// The BVH set (set 2) — the primary sun shadow's ray trace. Set 0 already carries the instance SSBO + merged vertex/index streams the BVH was built
// over (the trace's Instances/MeshIndices/PositionForVertex resolve to those in the frag), so set 2 carries ONLY the four acceleration buffers set 0
// lacks. Numbered to match SurfaceShade.frag's set-2 declarations exactly so the shader and this layout cannot drift apart.
constexpr uint32_t ShadowBindingSlices    = 0;   // b0 GeometryArena slice table       (ro)
constexpr uint32_t ShadowBindingArenaNode = 1;   // b1 GeometryArena bottom-level nodes (ro)
constexpr uint32_t ShadowBindingArenaPrim = 2;   // b2 GeometryArena primitive-order    (ro)
constexpr uint32_t ShadowBindingTreeNode  = 3;   // b3 InstanceTree top-level nodes     (ro)
constexpr uint32_t ShadowBindingCount     = 4;

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable), which the caller treats as "skip".
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("surface-shade", "shader module not found: %s", FilePath.c_str());
        return Bytes;
    }
    std::fseek(Handle, 0, SEEK_END);
    long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Bytes.resize((size_t)Size);
        size_t Read = std::fread(Bytes.data(), 1, (size_t)Size, Handle);
        if (Read != (size_t)Size)
            Bytes.clear();
    }
    std::fclose(Handle);
    return Bytes;
}

// First memory type satisfying the compatible-type bitmask and every required property flag. FoundEnabled is false when none matches. Mirrors the
// per-component helper the other visibility units carry (VisibilityImage / VisibilityInscription) — no shared home exists yet.
uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
                               uint32_t              CompatibleTypesBitmask,
                               VkMemoryPropertyFlags RequiredProperties,
                               bool&                 FoundEnabled)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    for (uint32_t IndexIterator = 0; IndexIterator < MemoryProperties.memoryTypeCount; ++IndexIterator)
    {
        const bool TypeCompatible = (CompatibleTypesBitmask & (1u << IndexIterator)) != 0;
        const bool PropertyMatch  = (MemoryProperties.memoryTypes[IndexIterator].propertyFlags & RequiredProperties) == RequiredProperties;
        if (TypeCompatible && PropertyMatch) { FoundEnabled = true; return IndexIterator; }
    }
    FoundEnabled = false;
    return 0;
}

// Allocate a host-visible + host-coherent buffer of ByteCapacity with the given usage. On any failure both out handles are null. The material table
// is 14 * 112 B and immutable after upload, so host-visible + mapped-once is the right shape — no staging transfer. Every size here derives from
// sizeof(SurfacePresetParameters), so a record growing a slot flows through without touching this allocation path.
bool ConstructHostBuffer(VulkanHost&        Host,
                         VkDeviceSize       ByteCapacity,
                         VkBufferUsageFlags Usage,
                         VkBuffer&          OutBuffer,
                         VkDeviceMemory&    OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteCapacity;
    BufferInformation.usage       = Usage;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Wrap a SPIR-V byte buffer in a VkShaderModule. VK_NULL_HANDLE on failure.
VkShaderModule ConstructShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
{
    if (Bytes.empty() || (Bytes.size() % 4) != 0)
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ModuleInfo = {};
    ModuleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ModuleInfo.codeSize = Bytes.size();
    ModuleInfo.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());

    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInfo, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Module;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfaceShadeInscription(SurfaceShadeInscription& Shade,
                                       VulkanHost&              Host,
                                       VkFormat                 ColourFormat,
                                       const char*              ShaderDirectory)
{
    Shade = SurfaceShadeInscription{};
    Shade.Host = &Host;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    // -- Shader modules. The fullscreen-triangle vertex stage is VisibilityInscription's, reused as-is: it synthesizes its three corners from
    //    gl_VertexIndex and carries no pass-specific state, so a second identical copy would only be a second thing to keep in sync. ------------
    const std::string Directory     = ShaderDirectory;
    VkShaderModule    VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityInscription.vert.spv"));
    VkShaderModule    FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/SurfaceShade.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("surface-shade", "pipeline not built — shader modules unavailable, surfaces will not shade");
        return false;
    }

    // A single unwind path for every failure after the modules exist, so no branch below can leak them.
    auto ReleaseModules = [&]()
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    };

    // -- Descriptor set layout: b0 = id sampler, b1/b2/b3 = head vertices / indices / instances, b4 = material table,
    //    b5/b6/b7 = the floor's own vertices / indices / instances (all fragment stage) ------------------------------------
    VkDescriptorSetLayoutBinding Bindings[BindingCount] = {};
    Bindings[0].binding         = BindingVisibilityImage;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[1].binding         = BindingMeshVertices;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[2].binding         = BindingMeshIndices;
    Bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[2].descriptorCount = 1;
    Bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[3].binding         = BindingInstances;
    Bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[3].descriptorCount = 1;
    Bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[4].binding         = BindingMaterials;
    Bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Bindings[4].descriptorCount = 1;
    Bindings[4].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[5].binding         = BindingFloorVertices;
    Bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[5].descriptorCount = 1;
    Bindings[5].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[6].binding         = BindingFloorIndices;
    Bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[6].descriptorCount = 1;
    Bindings[6].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[7].binding         = BindingFloorInstances;
    Bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[7].descriptorCount = 1;
    Bindings[7].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = BindingCount;
    SetLayoutInfo.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInfo, Host.Allocator, &Shade.SetLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("surface-shade", "descriptor set layout creation failed");
        return false;
    }

    // -- Set 1 (surfel field): three read-only storage buffers + one sampled atlas, all fragment stage. This slot was an EMPTY reserved layout while the
    //    W298 port was landing, held open only because vkCmdBindDescriptorSets binds by CONTIGUOUS index and the sun-shadow BVH below is declared at
    //    `set = 2` — a set at index 2 is illegal without a real layout at index 1. Filling it renumbered nothing, which is exactly what the empty layout
    //    was protecting. 🔴 IT MUST STILL BUILD EVEN WITH NO SurfelStore IN THE APPLICATION: the layout is a pipeline property, the WRITES are what wait
    //    for the store (RefreshSurfaceShadeSurfelBindings). A shade with no GI still binds a real-but-unpointed set 1 and pushes
    //    GlobalIlluminationEnabled = 0, so the gather is never reached. ------------------------------------------------------------------------------
    VkDescriptorSetLayoutBinding SurfelBindings[SurfelBindingCount] = {};
    for (uint32_t Index = 0; Index < SurfelBindingCount; ++Index)
    {
        SurfelBindings[Index].binding         = Index;
        SurfelBindings[Index].descriptorType  = (Index == SurfelBindingDepthAtlas) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        SurfelBindings[Index].descriptorCount = 1;
        SurfelBindings[Index].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo SurfelLayoutInfo = {};
    SurfelLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SurfelLayoutInfo.bindingCount = SurfelBindingCount;
    SurfelLayoutInfo.pBindings    = SurfelBindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SurfelLayoutInfo, Host.Allocator, &Shade.SurfelSetLayout) != VK_SUCCESS)
    {
        Shade.SurfelSetLayout = VK_NULL_HANDLE;
        ISSUE_CAUTION("surface-shade", "surfel field set-1 layout creation failed — index 2 becomes a gap, GI and direct shadows will drop");
    }

    // -- Set 2 (primary sun shadow): the BVH — four storage buffers, all fragment stage. Best-effort like set 1: if this layout fails the shade still
    //    runs unshadowed (the frag's ShadowEnabled toggle handles a never-pointed set), so a failure here caution-logs and leaves ShadowSetLayout null
    //    rather than aborting. 🔴 The set-2 layout MUST build whenever the pipeline expects three sets, so it is only added to the pipeline layout below
    //    when non-null — a set-1-only fallback stays valid because the frag never reaches a set-2 access with ShadowEnabled forced to 0. -----------
    VkDescriptorSetLayoutBinding ShadowBindings[ShadowBindingCount] = {};
    for (uint32_t Index = 0; Index < ShadowBindingCount; ++Index)
    {
        ShadowBindings[Index].binding         = Index;
        ShadowBindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ShadowBindings[Index].descriptorCount = 1;
        ShadowBindings[Index].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo ShadowLayoutInfo = {};
    ShadowLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ShadowLayoutInfo.bindingCount = ShadowBindingCount;
    ShadowLayoutInfo.pBindings    = ShadowBindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &ShadowLayoutInfo, Host.Allocator, &Shade.ShadowSetLayout) != VK_SUCCESS)
    {
        Shade.ShadowSetLayout = VK_NULL_HANDLE;
        ISSUE_CAUTION("surface-shade", "sun-shadow BVH set layout creation failed — direct shadows disabled");
    }

    // 🔴 The set-2 layout only makes sense atop the set-1 layout: the pipeline binds sets by contiguous index, so a set at index 2 requires a real
    //    layout at index 1. If the surfel layout failed but the shadow layout built, DROP the shadow set — a gap at index 1 is illegal.
    if (Shade.SurfelSetLayout == VK_NULL_HANDLE && Shade.ShadowSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Host.Device, Shade.ShadowSetLayout, Host.Allocator);
        Shade.ShadowSetLayout = VK_NULL_HANDLE;
        ISSUE_CAUTION("surface-shade", "sun-shadow set dropped — surfel set (index 1) is absent, so index 2 would be a gap");
    }

    // -- Descriptor pool + set. ONE sampler (the id image), SIX storage buffers (three head + three floor) and ONE uniform buffer (the material
    //    table). ⚠️ These counts must track the binding list above exactly: an undersized pool fails allocation outright rather than degrading, which
    //    is the good outcome, but it fails at bring-up far from the binding that caused it. -------------------------------------------------------
    // Set 0 needs 1 sampler + 6 storage + 1 uniform; set 1 (the surfel field) adds 3 storage + 1 sampler; set 2 (the sun-shadow BVH) adds 4 storage. Size
    // the pool for all three sets even if a later layout failed above — an over-sized pool is harmless, and this keeps the counts a simple sum rather than
    // a conditional. maxSets = 3 for the three sets.
    VkDescriptorPoolSize PoolSizes[3] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[0].descriptorCount = 2;                        // 1 (set 0 id image) + 1 (set 1 depth atlas)
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = 6 + 3 + ShadowBindingCount;   // 6 (set 0) + 3 (set 1 field) + 4 (set 2 BVH)
    PoolSizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    PoolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 3;
    PoolInfo.poolSizeCount = 3;
    PoolInfo.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Shade.DescriptorPool) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocate = {};
    SetAllocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocate.descriptorPool     = Shade.DescriptorPool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &Shade.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocate, &Shade.ShadeSet) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "descriptor set allocation failed");
        return false;
    }

    // Allocate the surfel-field set from the same pool. Left UNPOINTED until RefreshSurfaceShadeSurfelBindings runs against a live SurfelStore — and it
    // must still be a real allocated set even in an application that has no store at all, because the record binds index 1 to keep set 2 contiguous and
    // vkCmdBindDescriptorSets rejects VK_NULL_HANDLE in the array. A failure leaves SurfelSet null, which the record reads as "index 1 is empty" and
    // consequently drops BOTH the GI gather and the sun shadow — so it caution-logs rather than passing silently.
    if (Shade.SurfelSetLayout != VK_NULL_HANDLE)
    {
        VkDescriptorSetAllocateInfo SurfelSetAllocate = {};
        SurfelSetAllocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        SurfelSetAllocate.descriptorPool     = Shade.DescriptorPool;
        SurfelSetAllocate.descriptorSetCount = 1;
        SurfelSetAllocate.pSetLayouts        = &Shade.SurfelSetLayout;
        if (vkAllocateDescriptorSets(Host.Device, &SurfelSetAllocate, &Shade.SurfelSet) != VK_SUCCESS)
        {
            Shade.SurfelSet = VK_NULL_HANDLE;
            ISSUE_CAUTION("surface-shade", "surfel field set-1 allocation failed — index 2 becomes a gap, GI and direct shadows will drop");
        }
    }

    // Allocate the sun-shadow BVH set from the same pool (best-effort — only if the layout built). A failure leaves ShadowSet null; the record forces
    // ShadowEnabled off. Left unpointed until RefreshSurfaceShadeBvhBindings runs against the live GeometryArena + InstanceTree buffers.
    if (Shade.ShadowSetLayout != VK_NULL_HANDLE)
    {
        VkDescriptorSetAllocateInfo ShadowSetAllocate = {};
        ShadowSetAllocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ShadowSetAllocate.descriptorPool     = Shade.DescriptorPool;
        ShadowSetAllocate.descriptorSetCount = 1;
        ShadowSetAllocate.pSetLayouts        = &Shade.ShadowSetLayout;
        if (vkAllocateDescriptorSets(Host.Device, &ShadowSetAllocate, &Shade.ShadowSet) != VK_SUCCESS)
        {
            Shade.ShadowSet = VK_NULL_HANDLE;
            ISSUE_CAUTION("surface-shade", "sun-shadow BVH set allocation failed — direct shadows disabled");
        }
    }

    // -- Point sampler (nearest / clamp). A filtered identity is not a blend of two surfaces — it is a DIFFERENT, probably nonexistent triangle,
    //    so linear filtering here would fetch garbage geometry rather than soften an edge. -------------------------------------------------------
    VkSamplerCreateInfo SamplerInfo = {};
    SamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerInfo.magFilter    = VK_FILTER_NEAREST;
    SamplerInfo.minFilter    = VK_FILTER_NEAREST;
    SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Host.Device, &SamplerInfo, Host.Allocator, &Shade.PointSampler) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "point sampler creation failed");
        return false;
    }

    // -- Linear sampler (linear / clamp) — for the surfel DEPTH ATLAS only. 🔴 A SECOND SAMPLER RATHER THAN A REUSE OF THE POINT ONE, AND THE FILTER IS THE
    //    WHOLE REASON THE ATLAS HAS A BORDER. Each surfel's tile is a 5x5 payload inside a 7x7 tile, and the one-texel border exists precisely so a
    //    bilinear tap at the payload edge lands on a copy of the edge instead of the neighbouring surfel's data (SurfelAtlasAddressing.glsl). A NEAREST tap
    //    here would compile, run, and look roughly right while quantizing every occlusion test into 25 cells — stair-stepped contact shadows that read as an
    //    artefact of the gather rather than of the sampler. ⚠️ Best-effort: a failure leaves LinearSampler null, which the surfel Refresh refuses, so GI
    //    stays off and the shade keeps its flat fill. It must NOT fail the whole pass, since everything else here is independent of GI. ---------------
    VkSamplerCreateInfo LinearSamplerInfo = {};
    LinearSamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    LinearSamplerInfo.magFilter    = VK_FILTER_LINEAR;
    LinearSamplerInfo.minFilter    = VK_FILTER_LINEAR;
    LinearSamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    LinearSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    LinearSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    LinearSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Host.Device, &LinearSamplerInfo, Host.Allocator, &Shade.LinearSampler) != VK_SUCCESS)
    {
        Shade.LinearSampler = VK_NULL_HANDLE;
        ISSUE_CAUTION("surface-shade", "linear sampler creation failed — surfel GI cannot be pointed, the flat ambient fill stands");
    }

    // -- The owned material UBO (14 records; immutable after upload) -----------------------------------------------------
    const VkDeviceSize MaterialBytes = (VkDeviceSize)SurfacePresetCount * sizeof(SurfacePresetParameters);
    if (!ConstructHostBuffer(Host, MaterialBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Shade.MaterialBuffer, Shade.MaterialMemory))
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "material uniform buffer allocation failed");
        return false;
    }

    // -- Pipeline layout: the one set + the ShadeConstants push range (fragment stage) -----------------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SurfaceShadeConstants);

    // One to three sets, filled by contiguous index: set 0 shade always; set 1 the surfel field; set 2 sun-shadow BVH when ITS layout built (which the
    // block above guarantees implies set 1 exists too, so there is never a gap). The frag declares all three, so a shorter layout is only valid when the
    // trailing set genuinely failed — in which case the matching push toggle (GlobalIlluminationEnabled / ShadowEnabled) is forced off and no draw reaches
    // an access into the unbound set.
    VkDescriptorSetLayout SetLayouts[3] = { Shade.SetLayout, VK_NULL_HANDLE, VK_NULL_HANDLE };
    uint32_t              SetLayoutCount = 1u;
    if (Shade.SurfelSetLayout != VK_NULL_HANDLE)
        SetLayouts[SetLayoutCount++] = Shade.SurfelSetLayout;
    if (Shade.ShadowSetLayout != VK_NULL_HANDLE)
        SetLayouts[SetLayoutCount++] = Shade.ShadowSetLayout;

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = SetLayoutCount;
    LayoutInfo.pSetLayouts            = SetLayouts;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Shade.PipelineLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "pipeline layout creation failed");
        return false;
    }

    // -- Shader stages --------------------------------------------------------------------------------------------------
    VkPipelineShaderStageCreateInfo Stages[2] = {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = VertexModule;
    Stages[0].pName  = "main";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = FragmentModule;
    Stages[1].pName  = "main";

    // -- Fixed-function state: no vertex input, triangle list, dynamic viewport/scissor, alpha-over blend, no depth -----
    //    The alpha-over blend is not incidental: it is what makes the Glass preset transparent for free, since the shade writes a sub-1 alpha there
    //    and fixed-function blending composites it over the sky / grid already in the scope.
    VkPipelineVertexInputStateCreateInfo VertexInput = {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = {};
    InputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo Viewport = {};
    Viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    Viewport.viewportCount = 1;
    Viewport.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo Rasterization = {};
    Rasterization.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    Rasterization.cullMode    = VK_CULL_MODE_NONE;
    Rasterization.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rasterization.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState BlendAttachment = {};
    BlendAttachment.blendEnable         = VK_TRUE;
    BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    BlendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ColorBlend = {};
    ColorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlend.attachmentCount = 1;
    ColorBlend.pAttachments    = &BlendAttachment;

    VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo Dynamic = {};
    Dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    Dynamic.dynamicStateCount = 2;
    Dynamic.pDynamicStates    = DynamicStates;

    // -- Dynamic-rendering attachment format (replaces a VkRenderPass) --------------------------------------------------
    VkPipelineRenderingCreateInfoKHR RenderingInfo = {};
    RenderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    RenderingInfo.colorAttachmentCount    = 1;
    RenderingInfo.pColorAttachmentFormats = &ColourFormat;

    VkGraphicsPipelineCreateInfo PipelineInfo = {};
    PipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipelineInfo.pNext               = &RenderingInfo;
    PipelineInfo.stageCount          = 2;
    PipelineInfo.pStages             = Stages;
    PipelineInfo.pVertexInputState   = &VertexInput;
    PipelineInfo.pInputAssemblyState = &InputAssembly;
    PipelineInfo.pViewportState      = &Viewport;
    PipelineInfo.pRasterizationState = &Rasterization;
    PipelineInfo.pMultisampleState   = &Multisample;
    PipelineInfo.pColorBlendState    = &ColorBlend;
    PipelineInfo.pDynamicState       = &Dynamic;
    PipelineInfo.layout              = Shade.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Shade.Pipeline);

    ReleaseModules();

    if (Outcome != VK_SUCCESS)
    {
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Shade.ReadyCondition = true;
    ISSUE_NOTICE("surface-shade", "surface shade ready");
    return true;
}

void UploadSurfaceShadeMaterials(SurfaceShadeInscription& Shade, const SurfacePresetParameters* Table)
{
    if (!Shade.ReadyCondition || Shade.Host == nullptr || Shade.MaterialMemory == VK_NULL_HANDLE || Table == nullptr)
        return;

    VulkanHost&        Host          = *Shade.Host;
    const VkDeviceSize MaterialBytes = (VkDeviceSize)SurfacePresetCount * sizeof(SurfacePresetParameters);

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Shade.MaterialMemory, 0, MaterialBytes, 0, &Mapped) != VK_SUCCESS)
    {
        ISSUE_CAUTION("surface-shade", "material table map failed — surfaces will shade from an uninitialized table");
        return;
    }
    std::memcpy(Mapped, Table, (size_t)MaterialBytes);
    vkUnmapMemory(Host.Device, Shade.MaterialMemory);

    // Point b4 at the table. Uploaded once before the render loop, so no in-flight set is being rewritten here.
    VkDescriptorBufferInfo BufferInfo = {};
    BufferInfo.buffer = Shade.MaterialBuffer;
    BufferInfo.offset = 0;
    BufferInfo.range  = MaterialBytes;

    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Shade.ShadeSet;
    Write.dstBinding      = BindingMaterials;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Write.pBufferInfo     = &BufferInfo;
    vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
}

void RefreshSurfaceShadeInscription(SurfaceShadeInscription& Shade,
                                    const VisibilityImage&   Image,
                                    VkBuffer                 VertexBuffer,
                                    VkDeviceSize             VertexBytes,
                                    VkBuffer                 IndexBuffer,
                                    VkDeviceSize             IndexBytes,
                                    VkBuffer                 InstanceBuffer,
                                    VkDeviceSize             InstanceBytes,
                                    VkBuffer                 FloorVertexBuffer,
                                    VkDeviceSize             FloorVertexBytes,
                                    VkBuffer                 FloorIndexBuffer,
                                    VkDeviceSize             FloorIndexBytes,
                                    VkBuffer                 FloorInstanceBuffer,
                                    VkDeviceSize             FloorInstanceBytes)
{
    if (!Shade.ReadyCondition || Shade.ShadeSet == VK_NULL_HANDLE)
        return;
    if (!Image.ReadyCondition || Image.IdView == VK_NULL_HANDLE)
        return;
    if (VertexBuffer == VK_NULL_HANDLE || IndexBuffer == VK_NULL_HANDLE || InstanceBuffer == VK_NULL_HANDLE)
        return;

    // 🔴 All three floor handles or none: a partial set would leave one binding undefined while the other two looked real, and the shader has a single
    //    flag to branch on rather than three. Falling back to the head-buffer alias keeps every descriptor defined, and FloorGeometryBound records
    //    which of the two states b5-b7 are actually in so the caller cannot enable floor shading against the alias.
    const bool FloorPresent = FloorVertexBuffer   != VK_NULL_HANDLE
                           && FloorIndexBuffer    != VK_NULL_HANDLE
                           && FloorInstanceBuffer != VK_NULL_HANDLE;

    const VkBuffer     FloorVertexTarget   = FloorPresent ? FloorVertexBuffer   : VertexBuffer;
    const VkDeviceSize FloorVertexRange    = FloorPresent ? FloorVertexBytes    : VertexBytes;
    const VkBuffer     FloorIndexTarget    = FloorPresent ? FloorIndexBuffer    : IndexBuffer;
    const VkDeviceSize FloorIndexRange     = FloorPresent ? FloorIndexBytes     : IndexBytes;
    const VkBuffer     FloorInstanceTarget = FloorPresent ? FloorInstanceBuffer : InstanceBuffer;
    const VkDeviceSize FloorInstanceRange  = FloorPresent ? FloorInstanceBytes  : InstanceBytes;

    // Idempotent, for the same reason VisibilityInscription's Refresh is: this set is bound every frame the shade records, so rewriting it when
    // nothing changed would trip the "descriptor in use by a pending command buffer" rule for no benefit. Only bring-up and resize actually differ,
    // and the caller idles the device on those. Steady state issues zero vkUpdateDescriptorSets.
    const bool Unchanged = Shade.BoundIdView               == Image.IdView
                        && Shade.BoundVertexBuffer         == VertexBuffer
                        && Shade.BoundIndexBuffer          == IndexBuffer
                        && Shade.BoundInstanceBuffer       == InstanceBuffer
                        && Shade.BoundFloorVertexBuffer    == FloorVertexTarget
                        && Shade.BoundFloorIndexBuffer     == FloorIndexTarget
                        && Shade.BoundFloorInstanceBuffer  == FloorInstanceTarget
                        && Shade.FloorGeometryBound        == FloorPresent;
    if (Unchanged)
        return;

    VkDescriptorImageInfo ImageInfo = {};
    ImageInfo.sampler     = Shade.PointSampler;
    ImageInfo.imageView   = Image.IdView;
    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo VertexInfo   = { VertexBuffer,   0, VertexBytes };
    VkDescriptorBufferInfo IndexInfo    = { IndexBuffer,    0, IndexBytes };
    VkDescriptorBufferInfo InstanceInfo = { InstanceBuffer, 0, InstanceBytes };

    VkDescriptorBufferInfo FloorVertexInfo   = { FloorVertexTarget,   0, FloorVertexRange };
    VkDescriptorBufferInfo FloorIndexInfo    = { FloorIndexTarget,    0, FloorIndexRange };
    VkDescriptorBufferInfo FloorInstanceInfo = { FloorInstanceTarget, 0, FloorInstanceRange };

    VkWriteDescriptorSet Writes[7] = {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Shade.ShadeSet;
    Writes[0].dstBinding      = BindingVisibilityImage;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[0].pImageInfo      = &ImageInfo;

    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Shade.ShadeSet;
    Writes[1].dstBinding      = BindingMeshVertices;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[1].pBufferInfo     = &VertexInfo;

    Writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[2].dstSet          = Shade.ShadeSet;
    Writes[2].dstBinding      = BindingMeshIndices;
    Writes[2].descriptorCount = 1;
    Writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[2].pBufferInfo     = &IndexInfo;

    Writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[3].dstSet          = Shade.ShadeSet;
    Writes[3].dstBinding      = BindingInstances;
    Writes[3].descriptorCount = 1;
    Writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[3].pBufferInfo     = &InstanceInfo;

    Writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[4].dstSet          = Shade.ShadeSet;
    Writes[4].dstBinding      = BindingFloorVertices;
    Writes[4].descriptorCount = 1;
    Writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[4].pBufferInfo     = &FloorVertexInfo;

    Writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[5].dstSet          = Shade.ShadeSet;
    Writes[5].dstBinding      = BindingFloorIndices;
    Writes[5].descriptorCount = 1;
    Writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[5].pBufferInfo     = &FloorIndexInfo;

    Writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[6].dstSet          = Shade.ShadeSet;
    Writes[6].dstBinding      = BindingFloorInstances;
    Writes[6].descriptorCount = 1;
    Writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[6].pBufferInfo     = &FloorInstanceInfo;

    vkUpdateDescriptorSets(Shade.Host->Device, 7, Writes, 0, nullptr);

    Shade.BoundIdView                = Image.IdView;
    Shade.BoundVertexBuffer          = VertexBuffer;
    Shade.BoundIndexBuffer           = IndexBuffer;
    Shade.BoundInstanceBuffer        = InstanceBuffer;
    Shade.BoundFloorVertexBuffer     = FloorVertexTarget;
    Shade.BoundFloorIndexBuffer      = FloorIndexTarget;
    Shade.BoundFloorInstanceBuffer   = FloorInstanceTarget;
    Shade.FloorGeometryBound         = FloorPresent;
}

bool RefreshSurfaceShadeSurfelBindings(SurfaceShadeInscription& Shade,
                                       VkBuffer                 SurfelRecordBuffer,
                                       VkBuffer                 SurfelCellSpanBuffer,
                                       VkBuffer                 SurfelCellListBuffer,
                                       VkImageView              SurfelDepthView)
{
    // The field set must exist (Initialize built it) and every resource must be live, or there is nothing valid to point at. LinearSampler is part of
    // that precondition, not an afterthought: a combined-image-sampler write with a null sampler is invalid, and the filter is load-bearing anyway.
    if (!Shade.ReadyCondition || Shade.SurfelSet == VK_NULL_HANDLE || Shade.Host == nullptr)
        return false;
    if (SurfelRecordBuffer == VK_NULL_HANDLE || SurfelCellSpanBuffer == VK_NULL_HANDLE ||
        SurfelCellListBuffer == VK_NULL_HANDLE || SurfelDepthView == VK_NULL_HANDLE || Shade.LinearSampler == VK_NULL_HANDLE)
        return false;

    // Idempotent, same rule as the other Refreshes: rewriting a set bound by an in-flight command buffer is undefined, so only write on a change. 📝 For
    // this set that is the steady state — the store owns all four for its whole life — so after the first call this returns true having done nothing.
    const bool Unchanged = Shade.BoundSurfelRecordBuffer   == SurfelRecordBuffer
                        && Shade.BoundSurfelCellSpanBuffer == SurfelCellSpanBuffer
                        && Shade.BoundSurfelCellListBuffer == SurfelCellListBuffer
                        && Shade.BoundSurfelDepthView      == SurfelDepthView
                        && Shade.SurfelSetReady;
    if (Unchanged)
        return true;

    VkDescriptorBufferInfo RecordInfo   = { SurfelRecordBuffer,   0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo CellSpanInfo = { SurfelCellSpanBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo CellListInfo = { SurfelCellListBuffer, 0, VK_WHOLE_SIZE };

    // 🔴 VK_IMAGE_LAYOUT_GENERAL, NOT SHADER_READ_ONLY_OPTIMAL. Both surfel atlases stay in GENERAL for their whole life because the integrate holds them
    //    as storage images (the invariant SurfelIrradianceSubmission.h states). Declaring the read at GENERAL costs a possible optimal-tiling win and buys
    //    the absence of a per-frame GENERAL -> READ_ONLY -> GENERAL round trip that this pass would have to own BOTH halves of — the second half running
    //    after the shade but before the next frame's integrate, which is a seam no existing barrier sits on.
    VkDescriptorImageInfo DepthAtlasInfo = {};
    DepthAtlasInfo.sampler     = Shade.LinearSampler;
    DepthAtlasInfo.imageView   = SurfelDepthView;
    DepthAtlasInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    const VkDescriptorBufferInfo* Infos[3] = { &RecordInfo, &CellSpanInfo, &CellListInfo };
    const uint32_t Bindings[3] = { SurfelBindingRecords, SurfelBindingCellSpans, SurfelBindingCellList };

    VkWriteDescriptorSet Writes[SurfelBindingCount] = {};
    for (uint32_t Index = 0; Index < 3; ++Index)
    {
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Shade.SurfelSet;
        Writes[Index].dstBinding      = Bindings[Index];
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = Infos[Index];
    }
    Writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[3].dstSet          = Shade.SurfelSet;
    Writes[3].dstBinding      = SurfelBindingDepthAtlas;
    Writes[3].descriptorCount = 1;
    Writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[3].pImageInfo      = &DepthAtlasInfo;

    vkUpdateDescriptorSets(Shade.Host->Device, SurfelBindingCount, Writes, 0, nullptr);

    Shade.BoundSurfelRecordBuffer   = SurfelRecordBuffer;
    Shade.BoundSurfelCellSpanBuffer = SurfelCellSpanBuffer;
    Shade.BoundSurfelCellListBuffer = SurfelCellListBuffer;
    Shade.BoundSurfelDepthView      = SurfelDepthView;
    Shade.SurfelSetReady            = true;
    return true;
}

void RefreshSurfaceShadeBvhBindings(SurfaceShadeInscription& Shade,
                                    VkBuffer                 SliceBuffer,
                                    VkBuffer                 ArenaNodeBuffer,
                                    VkBuffer                 ArenaPrimitiveBuffer,
                                    VkBuffer                 TreeNodeBuffer)
{
    // The BVH set must exist (Initialize built it) and every buffer must be live, or there is nothing valid to point at.
    if (!Shade.ReadyCondition || Shade.ShadowSet == VK_NULL_HANDLE || Shade.Host == nullptr)
        return;
    if (SliceBuffer == VK_NULL_HANDLE || ArenaNodeBuffer == VK_NULL_HANDLE ||
        ArenaPrimitiveBuffer == VK_NULL_HANDLE || TreeNodeBuffer == VK_NULL_HANDLE)
        return;

    // Idempotent, same rule as the other two Refreshes: rewriting a set bound by an in-flight command buffer is undefined, so only write on a change.
    const bool Unchanged = Shade.BoundSliceBuffer     == SliceBuffer
                        && Shade.BoundArenaNodeBuffer == ArenaNodeBuffer
                        && Shade.BoundArenaPrimBuffer == ArenaPrimitiveBuffer
                        && Shade.BoundTreeNodeBuffer  == TreeNodeBuffer
                        && Shade.ShadowSetReady;
    if (Unchanged)
        return;

    VkDescriptorBufferInfo SliceInfo     = { SliceBuffer,          0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo ArenaNodeInfo = { ArenaNodeBuffer,      0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo ArenaPrimInfo = { ArenaPrimitiveBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo TreeNodeInfo  = { TreeNodeBuffer,       0, VK_WHOLE_SIZE };

    const VkDescriptorBufferInfo* Infos[ShadowBindingCount] =
        { &SliceInfo, &ArenaNodeInfo, &ArenaPrimInfo, &TreeNodeInfo };
    const uint32_t Bindings[ShadowBindingCount] =
        { ShadowBindingSlices, ShadowBindingArenaNode, ShadowBindingArenaPrim, ShadowBindingTreeNode };

    VkWriteDescriptorSet Writes[ShadowBindingCount] = {};
    for (uint32_t Index = 0; Index < ShadowBindingCount; ++Index)
    {
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Shade.ShadowSet;
        Writes[Index].dstBinding      = Bindings[Index];
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = Infos[Index];
    }
    vkUpdateDescriptorSets(Shade.Host->Device, ShadowBindingCount, Writes, 0, nullptr);

    Shade.BoundSliceBuffer     = SliceBuffer;
    Shade.BoundArenaNodeBuffer = ArenaNodeBuffer;
    Shade.BoundArenaPrimBuffer = ArenaPrimitiveBuffer;
    Shade.BoundTreeNodeBuffer  = TreeNodeBuffer;
    Shade.ShadowSetReady       = true;
}

void RecordSurfaceShadeInscription(const SurfaceShadeInscription& Shade,
                                   VkExtent2D                     Extent,
                                   const SurfaceShadeConstants&   Constants,
                                   VkCommandBuffer                CommandBuffer)
{
    if (!Shade.ReadyCondition || Shade.ShadeSet == VK_NULL_HANDLE)
        return;
    // Nothing has been bound yet (Refresh has not run, or ran before the mesh existed) — recording now would read undefined descriptors.
    if (Shade.BoundIdView == VK_NULL_HANDLE || Shade.BoundVertexBuffer == VK_NULL_HANDLE)
        return;

    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)Extent.width;
    ViewportRegion.height   = (float)Extent.height;
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);

    VkRect2D Scissor = {};
    Scissor.extent = Extent;
    vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Shade.Pipeline);

    // Bind sets by contiguous index, and force OFF the toggle for any set that is not live so the frag never touches an unbound access:
    //   set 0 (shade)       — always bound
    //   set 1 (surfel field)— bound whenever the set exists; GlobalIlluminationEnabled = 0 unless it is also POINTED (see below)
    //   set 2 (sun-shadow)  — bound when live AND index 1 is occupied, else ShadowEnabled = 0 (unshadowed fallback)
    SurfaceShadeConstants Pushed = Constants;
    VkDescriptorSet Sets[3] = { Shade.ShadeSet, VK_NULL_HANDLE, VK_NULL_HANDLE };
    uint32_t        SetCount = 1;

    // 🔴 THE SET IS BOUND ON EXISTENCE, THE GATHER IS ENABLED ON READINESS — TWO DIFFERENT CONDITIONS, AND CONFLATING THEM BREAKS THE SUN SHADOW.
    //    vkCmdBindDescriptorSets cannot skip an index, so set 2 is unreachable unless index 1 carries SOMETHING; an application with no SurfelStore at all
    //    must therefore still bind its unpointed set 1, exactly as the reserved empty set used to be bound. What that application must NOT do is READ it,
    //    which is what forcing GlobalIlluminationEnabled to 0 guarantees — an unpointed storage descriptor is undefined memory, not a safely empty buffer.
    if (Shade.SurfelSet != VK_NULL_HANDLE)
        Sets[SetCount++] = Shade.SurfelSet;
    if (!Shade.SurfelSetReady)
        Pushed.GlobalIlluminationEnabled = 0u;

    // Set 2 sits at index 2, so it can only be bound when index 1 is already occupied (SetCount == 2 here). Init guarantees the shadow layout is
    // dropped whenever the surfel layout is absent, so this condition simply mirrors that invariant at record time.
    if (SetCount == 2 && Shade.ShadowSet != VK_NULL_HANDLE && Shade.ShadowSetReady)
    {
        Sets[SetCount++] = Shade.ShadowSet;
    }
    else
    {
        Pushed.ShadowEnabled = 0u;
    }

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Shade.PipelineLayout,
                            0, SetCount, Sets, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Shade.PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SurfaceShadeConstants), &Pushed);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeSurfaceShadeInscription(SurfaceShadeInscription& Shade)
{
    if (Shade.Host == nullptr || Shade.Host->Device == VK_NULL_HANDLE)
    {
        Shade = SurfaceShadeInscription{};
        return;
    }
    VkDevice                     Device    = Shade.Host->Device;
    const VkAllocationCallbacks* Allocator = Shade.Host->Allocator;

    if (Shade.Pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(Device, Shade.Pipeline, Allocator);
    if (Shade.PipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, Shade.PipelineLayout, Allocator);
    if (Shade.PointSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, Shade.PointSampler, Allocator);
    if (Shade.LinearSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, Shade.LinearSampler, Allocator);           // set 1 b3 only; created best-effort, so it may legitimately be null
    if (Shade.DescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Shade.DescriptorPool, Allocator);   // frees ShadeSet + SurfelSet + ShadowSet
    if (Shade.SetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, Shade.SetLayout, Allocator);
    if (Shade.SurfelSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, Shade.SurfelSetLayout, Allocator);
    if (Shade.ShadowSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, Shade.ShadowSetLayout, Allocator);
    if (Shade.MaterialBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Device, Shade.MaterialBuffer, Allocator);
    if (Shade.MaterialMemory != VK_NULL_HANDLE)
        vkFreeMemory(Device, Shade.MaterialMemory, Allocator);

    Shade = SurfaceShadeInscription{};
}

} // namespace Frontier
