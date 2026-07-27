/*==============================================================================================================================================
                                                            GPUBAKEDISPATCH.CPP
==============================================================================================================================================*/
// 🧩 Vulkan implementation of the compute bake path. A GpuBakeContext owns a transient command pool + a descriptor pool and lazily builds
//    one compute pipeline per bake shader (loaded off disk as .comp.spv). Every Evaluate* entry follows the same synchronous shape: build
//    host-visible storage buffers for the inputs + one output buffer, memcpy the inputs in, bind the pipeline + descriptor set, record a
//    one-shot command buffer (host-write → compute-read barrier, dispatch, compute-write → host-read barrier), submit against a fresh fence,
//    wait it, then memcpy the output buffer straight into the caller's CPU result struct. No 3D storage image and no image-to-buffer copy —
//    the shaders write a flat voxel / pixel SSBO the host reads back directly. Manual vkAllocateMemory (no VMA), mirroring TerrainFieldVolume
//    (compute idiom) + GlobalDistanceField (one-shot submit + fence). Bounds / MaximumDistance are derived here to match DistanceVolumeBaker
//    byte-for-byte so the exact algorithm reproduces the CPU bake; the fast algorithm's Jump-Flooding result is near-exact.

#include "GpuBakeDispatch.h"

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Authoring/Geometry/Modeling/PolygonCluster.h"   // 📝 RenderVertexStream / RenderVertex — the triangle source the SDF bake uploads
#include "SignedDistanceVolume.h"  // 📝 the dense volume result the SDF bake fills
#include "SurfaceSampleField.h"    // 📝 the per-texel substrate the surface encoders read
#include "TriangleRayVolume.h"     // 📝 the ray volume the bent-normal / thickness encoders trace
#include "LinearAlgebra_Float64.h" // 📝 Vector3d — the volume boundary type

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL TYPES
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Which bake shader a compute pass runs. Indexes the lazily-built pipeline cache in the implementation, and picks the SPIR-V file
    //    name. The surface encoders branch on a push-constant identity inside a single shader, so one pass id covers several map identities.
    enum class BakePassIdentity
    {
        DistanceExact = 0,   // [-] - DistanceVolumeBakeExact.comp — exact per-voxel closest-point (matches CPU)
        DistanceNarrow,      // [-] - DistanceVolumeNarrow.comp     — int32 → packed int16 (Fix 6 approach a)
        DistanceSeed,        // [-] - DistanceVolumeSeed.comp       — Jump-Flooding seed splat
        DistanceFlood,       // [-] - DistanceVolumeFlood.comp      — Jump-Flooding propagation pass
        DistanceResolve,     // [-] - DistanceVolumeResolve.comp    — nearest-seed → signed R16_SNORM voxel
        DistanceBandExact,   // [-] - DistanceVolumeBandExact.comp  — exact refine only inside the narrow band (coarse-JFA + narrow-exact)
        SurfaceNormal,       // [-] - SurfaceNormalEncode.comp      — tangent + world normal
        SurfacePosition,     // [-] - SurfacePositionEncode.comp    — world position into bounds
        SurfaceOcclusion,    // [-] - SurfaceOcclusionEncode.comp   — bent normal + thickness (ray traced)
        SurfaceCurvature,    // [-] - SurfaceCurvatureEncode.comp   — neighbourhood normal divergence (+ cavity/convexity/concavity/pointiness remaps)
        SurfaceBevel,        // [-] - SurfaceBevelEncode.comp       — cone-traced rounded-corner normal (ray traced)
        SurfaceDust,         // [-] - SurfaceDustEncode.comp        — up-facing normal dust mask (no rays)
        SurfaceAmbientOcclusion, // [-] - SurfaceAmbientOcclusionEncode.comp — hemisphere occlusion scalar (ray traced)
        PassCount
    };

    constexpr uint32_t PassTotal = (uint32_t)BakePassIdentity::PassCount;

    const char* PassFileName(BakePassIdentity Pass)
    {
        switch (Pass)
        {
            case BakePassIdentity::DistanceExact:   return "DistanceVolumeBakeExact.comp.spv";
            case BakePassIdentity::DistanceNarrow:  return "DistanceVolumeNarrow.comp.spv";
            case BakePassIdentity::DistanceSeed:    return "DistanceVolumeSeed.comp.spv";
            case BakePassIdentity::DistanceFlood:   return "DistanceVolumeFlood.comp.spv";
            case BakePassIdentity::DistanceResolve: return "DistanceVolumeResolve.comp.spv";
            case BakePassIdentity::DistanceBandExact: return "DistanceVolumeBandExact.comp.spv";
            case BakePassIdentity::SurfaceNormal:   return "SurfaceNormalEncode.comp.spv";
            case BakePassIdentity::SurfacePosition: return "SurfacePositionEncode.comp.spv";
            case BakePassIdentity::SurfaceOcclusion:return "SurfaceOcclusionEncode.comp.spv";
            case BakePassIdentity::SurfaceCurvature:return "SurfaceCurvatureEncode.comp.spv";
            case BakePassIdentity::SurfaceBevel:    return "SurfaceBevelEncode.comp.spv";
            case BakePassIdentity::SurfaceDust:     return "SurfaceDustEncode.comp.spv";
            case BakePassIdentity::SurfaceAmbientOcclusion: return "SurfaceAmbientOcclusionEncode.comp.spv";
            default:                                return "";
        }
    }

    // 📝 Storage-buffer count per pass — the descriptor set layout binds this many STORAGE_BUFFERs at bindings 0..N-1. Every input and the
    //    single output are storage buffers (std430); the output is always the LAST binding. Distance exact: [triangles][output].
    //    Distance seed/flood/resolve: [seedNearest][seedValid] plus (exact triangles for seed) / (output for resolve). Surface: [field]
    //    (+[triangles][tree][ordered] for occlusion) [output].
    uint32_t PassBufferCount(BakePassIdentity Pass)
    {
        switch (Pass)
        {
            case BakePassIdentity::DistanceExact:    return 3;   // triangles, outVoxels (int32), packedVoxels (int16)
            case BakePassIdentity::DistanceNarrow:   return 2;   // srcVoxels (int32), packedVoxels (int16)
            case BakePassIdentity::DistanceSeed:     return 3;   // triangles, seedPoint, seedValid
            case BakePassIdentity::DistanceFlood:    return 4;   // seedPointSrc, seedValidSrc, seedPointDst, seedValidDst
            case BakePassIdentity::DistanceResolve:  return 4;   // seedPoint, seedValid, outVoxels, triangles (per-voxel sign)
            case BakePassIdentity::DistanceBandExact: return 2;  // triangles, voxels (int32, patched in place)
            case BakePassIdentity::SurfaceNormal:    return 2;   // texels, outPixels
            case BakePassIdentity::SurfacePosition:  return 2;   // texels, outPixels
            case BakePassIdentity::SurfaceOcclusion: return 5;   // texels, triangles, tree, ordered, outPixels
            case BakePassIdentity::SurfaceCurvature: return 2;   // texels, outPixels
            case BakePassIdentity::SurfaceBevel:     return 5;   // texels, triangles, tree, ordered, outPixels
            case BakePassIdentity::SurfaceDust:      return 2;   // texels, outPixels
            case BakePassIdentity::SurfaceAmbientOcclusion: return 5;   // texels, triangles, tree, ordered, outPixels
            default:                                 return 0;
        }
    }

    // 📝 A lazily-built compute pipeline: shader module + layout + pipeline + a descriptor set layout sized for the pass's buffer count.
    //    Built on first use of the pass; Built stays false (and the pass declines) if the SPIR-V was missing at Initialize.
    struct ComputePass
    {
        std::vector<char>     SpirV;
        VkShaderModule        Module              = VK_NULL_HANDLE;
        VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout      PipelineLayout      = VK_NULL_HANDLE;
        VkPipeline            Pipeline            = VK_NULL_HANDLE;
        uint32_t              BufferCount         = 0;
        bool                  SpirVLoaded         = false;
        bool                  Built               = false;
    };

    // 📝 A host-visible coherent storage buffer used both as an input (memcpy in before dispatch) and an output (memcpy out after the
    //    fence). Persistently mapped for the pass's lifetime; destroyed when the pass entry returns. Sized to the exact byte payload.
    struct HostBuffer
    {
        VkBuffer       Buffer   = VK_NULL_HANDLE;
        VkDeviceMemory Memory   = VK_NULL_HANDLE;
        void*          Mapped   = nullptr;
        VkDeviceSize   Capacity = 0;
    };

    // 📝 A device-local storage buffer the compute queue writes and the graphics queue copies to the resident 3D image (Fix 6): the packed
    //    int16 voxel store that never touches the CPU. Not host-visible (no Mapped) — its whole point is to stay on the GPU. Created
    //    VK_SHARING_MODE_CONCURRENT over {compute, graphics} so the copy-to-image needs no image queue-family-ownership transfer.
    struct DeviceBuffer
    {
        VkBuffer       Buffer   = VK_NULL_HANDLE;
        VkDeviceMemory Memory   = VK_NULL_HANDLE;
        VkDeviceSize   Capacity = 0;
    };

    struct GpuBakeContextImplementation
    {
        VkPhysicalDevice PhysicalDevice          = VK_NULL_HANDLE;
        VkDevice         Device                  = VK_NULL_HANDLE;
        VkQueue          Queue                   = VK_NULL_HANDLE;   // [-]   - dispatch queue (routed from VulkanHost.GraphicsQueue)
        uint32_t         QueueFamilyIndex        = 0;                // [idx] - dispatch family (the command pool + dispatch land here)
        uint32_t         GraphicsQueueFamilyIndex = 0;               // [idx] - graphics family — the second owner of the CONCURRENT packed buffer (Fix 6)

        VkCommandPool    CommandPool    = VK_NULL_HANDLE;
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;

        ComputePass Passes[PassTotal];
    };

    // ── push-constant blocks, byte-matched to each .comp (kept ≤ 128 bytes) ──

    struct DistanceExactPush
    {
        float    BoundaryMinimum[4];   // [cm] - xyz padded AABB lower corner (w pad)
        float    VoxelSize[4];         // [cm] - xyz per-voxel span (w pad)
        uint32_t ResolutionX;          // [-]  - per-axis grid voxel count X (three scalars keep the uvec4 BoxMinimum 16-aligned)
        uint32_t ResolutionY;          // [-]  - per-axis grid voxel count Y
        uint32_t ResolutionZ;          // [-]  - per-axis grid voxel count Z
        uint32_t TriangleCount;        // [-]  - triangles the closest-point loop walks
        float    MaximumDistance;      // [cm] - R16_SNORM decode scale
        uint32_t PackedMode;           // [-]  - 0 write int32 at binding 1, 1 pack int16 into binding 2 via atomicOr (Fix 6 approach b)
        uint32_t Pad0;                 // [-]  - pad to the next 16-byte boundary so uvec4 BoxMinimum aligns
        uint32_t Pad1;                 // [-]  - pad
        uint32_t BoxMinimum[4];        // [-]  - 🔍 xyz first dirty voxel (ABSOLUTE grid coords), w pad — sub-box dispatch offset
        uint32_t BoxExtent[4];         // [-]  - xyz dirty voxel count (whole grid = per-axis Resolution), w pad
    };

    // Fix 6 approach (a): the standalone narrow pass reads the int32 voxels and packs two int16 per uint. VoxelTotal guards the odd tail.
    struct DistanceNarrowPush
    {
        uint32_t VoxelTotal;   // [-] - voxel count; the shader packs ceil(VoxelTotal/2) uint words, guarding the last odd voxel
        uint32_t Pad0;
        uint32_t Pad1;
        uint32_t Pad2;
    };

    struct DistanceSeedPush
    {
        float    BoundaryMinimum[4];
        float    VoxelSize[4];
        uint32_t ResolutionX;
        uint32_t ResolutionY;
        uint32_t ResolutionZ;
        uint32_t TriangleCount;
        float    MaximumDistance;
        uint32_t Pad;
    };

    struct DistanceFloodPush
    {
        float    BoundaryMinimum[4];   // [cm] - xyz padded AABB lower corner (reconstruct the voxel world centre)
        float    VoxelSize[4];         // [cm] - xyz per-voxel span
        uint32_t ResolutionX;          // [-]  - per-axis grid voxel count X
        uint32_t ResolutionY;          // [-]  - per-axis grid voxel count Y
        uint32_t ResolutionZ;          // [-]  - per-axis grid voxel count Z
        int32_t  StepSize;             // [-]  - jump-flood step (N/2, N/4, … , 1)
        uint32_t Pad0;
        uint32_t Pad1;
    };

    struct DistanceResolvePush
    {
        float    BoundaryMinimum[4];
        float    VoxelSize[4];
        uint32_t ResolutionX;
        uint32_t ResolutionY;
        uint32_t ResolutionZ;
        float    MaximumDistance;
        uint32_t Pad0;
        uint32_t Pad1;
    };

    // Narrow-band exact refine (coarse-JFA + narrow-exact): reads the JFA int32 store in place, recomputes the exact distance only where
    // |jfaDistance| ≤ BandRadius. Byte-matched to DistanceVolumeBandExact.comp's BandConstant.
    struct DistanceBandExactPush
    {
        float    BoundaryMinimum[4];
        float    VoxelSize[4];
        uint32_t ResolutionX;
        uint32_t ResolutionY;
        uint32_t ResolutionZ;
        uint32_t TriangleCount;
        float    MaximumDistance;   // [cm] - R16_SNORM decode scale
        float    BandRadius;        // [cm] - refine exactly only where |jfaDistance| ≤ BandRadius
        uint32_t Pad0;
        uint32_t Pad1;
    };

    struct SurfacePush
    {
        uint32_t Edge;               // [-]   - map edge in texels
        uint32_t Identity;           // [-]   - SurfaceMapIdentity as uint (branch inside the shader)
        uint32_t TriangleCount;      // [-]   - ray volume triangles (occlusion pass only)
        uint32_t ElementCount;       // [-]   - ray volume tree elements (occlusion pass only)
        int32_t  SecondaryRays;      // [-]   - hemisphere fan sample count
        float    SpreadAngle;        // [°]   - hemisphere cone half-angle
        float    OcclusionMaximum;   // [cm]  - ray max distance / thickness normalization
        float    SamplingRadius;     // [px]  - curvature texel window
        float    Contrast;           // [-]   - curvature contrast about mid-grey
        uint32_t PositionNormalize;  // [-]   - 0 box, 1 sphere
        uint32_t AutoTonemapEnabled; // [-]   - curvature auto-remap
        uint32_t AlignmentPad;       // [-]   - 🔴 std430 pads the following vec4 to a 16-byte boundary (11 scalars = 44 → 48); this pad
                                     //         makes the host struct byte-match the shader's SurfaceConstant block. Without it BoundsMinimum /
                                     //         BoundsMaximum land 4 bytes early and the pushed range under-runs what the shader declares.
        float    BoundsMinimum[4];   // [cm]  - position-map normalization AABB lower corner (offset 48, 16-byte aligned)
        float    BoundsMaximum[4];   // [cm]  - position-map normalization AABB upper corner
    };

    // 📝 std430 triangle record uploaded for the SDF exact / seed bake. Corners + the pseudonormals the sign reads. Each vec3 padded to 16
    //    bytes to satisfy std430 array alignment; the shader mirrors this layout exactly.
    struct GpuBakeTriangle
    {
        float CornerA[4];
        float CornerB[4];
        float CornerC[4];
        float FaceNormal[4];
        float CornerNormalA[4];
        float CornerNormalB[4];
        float CornerNormalC[4];
        float EdgeNormalAB[4];
        float EdgeNormalBC[4];
        float EdgeNormalCA[4];
    };

    // 📝 std430 per-texel record uploaded for the surface encoders, mirroring TexelSample. Each vec3 padded to 16 bytes; CoverageMask +
    //    TriangleIndex packed into the tail uints. The shader reads WorldPosition / SurfaceNormal / the tangent frame per texel.
    struct GpuTexelSample
    {
        float    WorldPosition[4];   // xyz world position (w pad)
        float    SurfaceNormal[4];   // xyz shading normal (w pad)
        float    TangentBasisT[4];   // xyz tangent (w pad)
        float    TangentBasisB[4];   // xyz bitangent (w pad)
        int32_t  TriangleIndex;      // covering triangle (-1 uncovered)
        uint32_t CoverageMask;       // 1 covered, 0 gutter
        uint32_t Pad0;
        uint32_t Pad1;
    };

    // 📝 std430 ray-volume triangle for the occlusion pass, mirroring RayTriangle's corners / face normal / per-corner shading normal so a
    //    hit can barycentric-interpolate the frame. AABB carried for slab pruning against the flat tree.
    struct GpuRayTriangle
    {
        float CornerA[4];
        float CornerB[4];
        float CornerC[4];
        float FaceNormal[4];
        float VertexNormalA[4];
        float VertexNormalB[4];
        float VertexNormalC[4];
        float BoundaryMinimum[4];
        float BoundaryMaximum[4];
    };

    // 📝 std430 tree element for the occlusion pass, mirroring RayBoundaryElement (AABB + leaf span / down-links).
    struct GpuRayElement
    {
        float   BoundaryMinimum[4];
        float   BoundaryMaximum[4];
        int32_t FirstTriangle;
        int32_t TriangleCount;
        int32_t LeftDownLink;
        int32_t RightDownLink;
    };

    void ReportDispatch(const char* MessageText)
    {
        std::fprintf(stderr, "[GpuBakeDispatch] %s\n", MessageText);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  LOW-LEVEL HELPERS
    //--------------------------------------------------------------------------------------------------------------------

    bool LoadFileBytes(const char* FilePath, std::vector<char>& OutBytes)
    {
        std::FILE* HandleFile = nullptr;
        if (fopen_s(&HandleFile, FilePath, "rb") != 0 || !HandleFile) return false;
        std::fseek(HandleFile, 0, SEEK_END);
        long LengthBytes = std::ftell(HandleFile);
        std::fseek(HandleFile, 0, SEEK_SET);
        if (LengthBytes <= 0) { std::fclose(HandleFile); return false; }
        OutBytes.resize((size_t)LengthBytes);
        const size_t BytesRead = std::fread(OutBytes.data(), 1, (size_t)LengthBytes, HandleFile);
        std::fclose(HandleFile);
        return BytesRead == (size_t)LengthBytes;
    }

    uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDeviceHandle,
                                   uint32_t              CompatibleTypesBitmask,
                                   VkMemoryPropertyFlags RequiredProperties,
                                   bool&                 FoundEnabled)
    {
        VkPhysicalDeviceMemoryProperties MemoryProperties = {};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDeviceHandle, &MemoryProperties);
        for (uint32_t IndexIterator = 0; IndexIterator < MemoryProperties.memoryTypeCount; ++IndexIterator)
        {
            const bool TypeCompatible = (CompatibleTypesBitmask & (1u << IndexIterator)) != 0;
            const bool PropertyMatch  = (MemoryProperties.memoryTypes[IndexIterator].propertyFlags & RequiredProperties) == RequiredProperties;
            if (TypeCompatible && PropertyMatch) { FoundEnabled = true; return IndexIterator; }
        }
        FoundEnabled = false;
        return 0;
    }

    // Build one host-visible coherent storage buffer of Capacity bytes, persistently mapped and zeroed. Returns false (and leaves the
    // HostBuffer safe to destroy) on any failure.
    bool ConstructHostBuffer(GpuBakeContextImplementation& Implementation, VkDeviceSize Capacity, HostBuffer& Out)
    {
        if (Capacity == 0) Capacity = 4;   // never allocate a zero-byte buffer

        VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        BufferInformation.size        = Capacity;
        BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Implementation.Device, &BufferInformation, nullptr, &Out.Buffer) != VK_SUCCESS) return false;

        VkMemoryRequirements MemoryRequirements = {};
        vkGetBufferMemoryRequirements(Implementation.Device, Out.Buffer, &MemoryRequirements);
        bool MemoryTypeFound = false;
        const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Implementation.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                               MemoryTypeFound);
        if (!MemoryTypeFound) return false;

        VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocateInformation.allocationSize  = MemoryRequirements.size;
        AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
        if (vkAllocateMemory(Implementation.Device, &AllocateInformation, nullptr, &Out.Memory) != VK_SUCCESS) return false;
        if (vkBindBufferMemory(Implementation.Device, Out.Buffer, Out.Memory, 0) != VK_SUCCESS) return false;
        if (vkMapMemory(Implementation.Device, Out.Memory, 0, VK_WHOLE_SIZE, 0, &Out.Mapped) != VK_SUCCESS) return false;

        Out.Capacity = Capacity;
        std::memset(Out.Mapped, 0, (size_t)Capacity);
        return true;
    }

    void DestroyHostBuffer(VkDevice Device, HostBuffer& Buffer)
    {
        if (Buffer.Mapped) vkUnmapMemory(Device, Buffer.Memory);
        if (Buffer.Buffer) vkDestroyBuffer(Device, Buffer.Buffer, nullptr);
        if (Buffer.Memory) vkFreeMemory(Device, Buffer.Memory, nullptr);
        Buffer = HostBuffer{};
    }

    // Build one DEVICE_LOCAL storage buffer of Capacity bytes for the packed int16 voxels (Fix 6). Usage carries STORAGE (compute writes it),
    // TRANSFER_SRC (the graphics queue copies it into the resident 3D image), and TRANSFER_DST (the vkCmdFillBuffer zero-clear approach b
    // needs before its accumulating atomicOr). Created VK_SHARING_MODE_CONCURRENT over {compute, graphics} so the copy-to-image needs no
    // image ownership transfer — the two families both own the buffer. When the two family indices coincide (single-queue GPU) it degrades
    // to EXCLUSIVE. Returns false (and leaves the buffer safe to destroy) on any failure.
    bool ConstructDeviceBuffer(GpuBakeContextImplementation& Implementation, VkDeviceSize Capacity, DeviceBuffer& Out)
    {
        if (Capacity == 0) Capacity = 4;   // never allocate a zero-byte buffer

        const uint32_t QueueFamilies[2] = { Implementation.QueueFamilyIndex, Implementation.GraphicsQueueFamilyIndex };
        const bool     DistinctFamilies = QueueFamilies[0] != QueueFamilies[1];

        VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        BufferInformation.size  = Capacity;
        BufferInformation.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (DistinctFamilies)
        {
            BufferInformation.sharingMode           = VK_SHARING_MODE_CONCURRENT;
            BufferInformation.queueFamilyIndexCount = 2;
            BufferInformation.pQueueFamilyIndices   = QueueFamilies;
        }
        else
        {
            BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        if (vkCreateBuffer(Implementation.Device, &BufferInformation, nullptr, &Out.Buffer) != VK_SUCCESS) return false;

        VkMemoryRequirements MemoryRequirements = {};
        vkGetBufferMemoryRequirements(Implementation.Device, Out.Buffer, &MemoryRequirements);
        bool MemoryTypeFound = false;
        const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Implementation.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                               MemoryTypeFound);
        if (!MemoryTypeFound) return false;

        VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocateInformation.allocationSize  = MemoryRequirements.size;
        AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
        if (vkAllocateMemory(Implementation.Device, &AllocateInformation, nullptr, &Out.Memory) != VK_SUCCESS) return false;
        if (vkBindBufferMemory(Implementation.Device, Out.Buffer, Out.Memory, 0) != VK_SUCCESS) return false;

        Out.Capacity = Capacity;
        return true;
    }

    void DestroyDeviceBuffer(VkDevice Device, DeviceBuffer& Buffer)
    {
        if (Buffer.Buffer) vkDestroyBuffer(Device, Buffer.Buffer, nullptr);
        if (Buffer.Memory) vkFreeMemory(Device, Buffer.Memory, nullptr);
        Buffer = DeviceBuffer{};
    }

    // Build the compute pass on first use: N STORAGE_BUFFER bindings (COMPUTE stage), a push-constant range of PushSize, the module, and
    // the pipeline. The SPIR-V was loaded at Initialize; a missing SPIR-V leaves Built false and the caller declines the GPU path.
    bool BuildComputePass(GpuBakeContextImplementation& Implementation, ComputePass& Pass, uint32_t PushSize)
    {
        if (Pass.Built) return true;
        if (!Pass.SpirVLoaded) return false;

        std::vector<VkDescriptorSetLayoutBinding> Bindings(Pass.BufferCount);
        for (uint32_t BindingIndex = 0; BindingIndex < Pass.BufferCount; ++BindingIndex)
        {
            Bindings[BindingIndex].binding         = BindingIndex;
            Bindings[BindingIndex].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Bindings[BindingIndex].descriptorCount = 1;
            Bindings[BindingIndex].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        LayoutInformation.bindingCount = Pass.BufferCount;
        LayoutInformation.pBindings    = Bindings.data();
        if (vkCreateDescriptorSetLayout(Implementation.Device, &LayoutInformation, nullptr, &Pass.DescriptorSetLayout) != VK_SUCCESS)
            return false;

        VkPushConstantRange PushRange = {};
        PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        PushRange.offset     = 0;
        PushRange.size       = PushSize;
        VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PipelineLayoutInformation.setLayoutCount         = 1;
        PipelineLayoutInformation.pSetLayouts            = &Pass.DescriptorSetLayout;
        PipelineLayoutInformation.pushConstantRangeCount = 1;
        PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
        if (vkCreatePipelineLayout(Implementation.Device, &PipelineLayoutInformation, nullptr, &Pass.PipelineLayout) != VK_SUCCESS)
            return false;

        VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ModuleInformation.codeSize = Pass.SpirV.size();
        ModuleInformation.pCode    = reinterpret_cast<const uint32_t*>(Pass.SpirV.data());
        if (vkCreateShaderModule(Implementation.Device, &ModuleInformation, nullptr, &Pass.Module) != VK_SUCCESS)
            return false;

        VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        PipelineInformation.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        PipelineInformation.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        PipelineInformation.stage.module = Pass.Module;
        PipelineInformation.stage.pName  = "main";
        PipelineInformation.layout       = Pass.PipelineLayout;
        if (vkCreateComputePipelines(Implementation.Device, VK_NULL_HANDLE, 1, &PipelineInformation, nullptr, &Pass.Pipeline) != VK_SUCCESS)
            return false;

        Pass.Built = true;
        return true;
    }

    // Allocate a descriptor set from the pool for this pass and point its bindings at the given host buffers (Buffers[i] → binding i).
    bool BindPassDescriptors(GpuBakeContextImplementation& Implementation,
                             ComputePass&                  Pass,
                             const HostBuffer* const*      Buffers,
                             VkDescriptorSet&              OutSet)
    {
        VkDescriptorSetAllocateInfo SetAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        SetAllocate.descriptorPool     = Implementation.DescriptorPool;
        SetAllocate.descriptorSetCount = 1;
        SetAllocate.pSetLayouts        = &Pass.DescriptorSetLayout;
        if (vkAllocateDescriptorSets(Implementation.Device, &SetAllocate, &OutSet) != VK_SUCCESS) return false;

        std::vector<VkDescriptorBufferInfo> BufferInfos(Pass.BufferCount);
        std::vector<VkWriteDescriptorSet>   Writes(Pass.BufferCount);
        for (uint32_t BindingIndex = 0; BindingIndex < Pass.BufferCount; ++BindingIndex)
        {
            BufferInfos[BindingIndex].buffer = Buffers[BindingIndex]->Buffer;
            BufferInfos[BindingIndex].offset = 0;
            BufferInfos[BindingIndex].range  = VK_WHOLE_SIZE;
            Writes[BindingIndex]             = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            Writes[BindingIndex].dstSet          = OutSet;
            Writes[BindingIndex].dstBinding      = BindingIndex;
            Writes[BindingIndex].descriptorCount = 1;
            Writes[BindingIndex].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[BindingIndex].pBufferInfo     = &BufferInfos[BindingIndex];
        }
        vkUpdateDescriptorSets(Implementation.Device, Pass.BufferCount, Writes.data(), 0, nullptr);
        return true;
    }

    // Bind a pass's descriptors from raw VkBuffer handles (Buffers[i] → binding i) — the mixed-buffer-type counterpart of BindPassDescriptors,
    // used where a pass mixes host-visible storage buffers with the device-local packed buffer (Fix 6). A VK_NULL_HANDLE entry is left
    // unwritten (the pass must not read that binding for the active PackedMode).
    bool BindPassDescriptorsRaw(GpuBakeContextImplementation& Implementation,
                                ComputePass&                  Pass,
                                const VkBuffer*               Buffers,
                                VkDescriptorSet&              OutSet)
    {
        VkDescriptorSetAllocateInfo SetAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        SetAllocate.descriptorPool     = Implementation.DescriptorPool;
        SetAllocate.descriptorSetCount = 1;
        SetAllocate.pSetLayouts        = &Pass.DescriptorSetLayout;
        if (vkAllocateDescriptorSets(Implementation.Device, &SetAllocate, &OutSet) != VK_SUCCESS) return false;

        std::vector<VkDescriptorBufferInfo> BufferInfos(Pass.BufferCount);
        std::vector<VkWriteDescriptorSet>   Writes;
        Writes.reserve(Pass.BufferCount);
        for (uint32_t BindingIndex = 0; BindingIndex < Pass.BufferCount; ++BindingIndex)
        {
            if (Buffers[BindingIndex] == VK_NULL_HANDLE) continue;
            BufferInfos[BindingIndex].buffer = Buffers[BindingIndex];
            BufferInfos[BindingIndex].offset = 0;
            BufferInfos[BindingIndex].range  = VK_WHOLE_SIZE;
            VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            Write.dstSet          = OutSet;
            Write.dstBinding      = BindingIndex;
            Write.descriptorCount = 1;
            Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Write.pBufferInfo     = &BufferInfos[BindingIndex];
            Writes.push_back(Write);
        }
        vkUpdateDescriptorSets(Implementation.Device, (uint32_t)Writes.size(), Writes.data(), 0, nullptr);
        return true;
    }

    uint32_t DispatchGroups(uint32_t Extent, uint32_t LocalSize)
    {
        return (Extent + LocalSize - 1) / LocalSize;
    }

    // Record + submit one compute dispatch synchronously: host-write → compute-read barrier, bind, push, dispatch, compute-write →
    // host-read barrier, submit against a fresh fence, wait it. The caller reads the output buffer's mapped memory after this returns.
    bool DispatchComputePass(GpuBakeContextImplementation& Implementation,
                             ComputePass&                  Pass,
                             VkDescriptorSet               DescriptorSet,
                             const void*                   PushData,
                             uint32_t                      PushSize,
                             uint32_t                      GroupsX,
                             uint32_t                      GroupsY,
                             uint32_t                      GroupsZ)
    {
        VkCommandBufferAllocateInfo CommandInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandInformation.commandPool        = Implementation.CommandPool;
        CommandInformation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandInformation.commandBufferCount = 1;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(Implementation.Device, &CommandInformation, &CommandBuffer) != VK_SUCCESS) return false;

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(CommandBuffer, &BeginInformation);

        // Order the host memcpy of the inputs before the compute read (buffers are HOST_COHERENT so no flush is needed, only ordering).
        VkMemoryBarrier HostToCompute = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        HostToCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        HostToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(CommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &HostToCompute, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pass.Pipeline);
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pass.PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
        if (PushData && PushSize > 0)
            vkCmdPushConstants(CommandBuffer, Pass.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, PushSize, PushData);
        vkCmdDispatch(CommandBuffer, GroupsX, GroupsY, GroupsZ);

        // Order the compute write before the host readback of the output buffer.
        VkMemoryBarrier ComputeToHost = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        ComputeToHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ComputeToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &ComputeToHost, 0, nullptr, 0, nullptr);

        vkEndCommandBuffer(CommandBuffer);

        VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence Fence = VK_NULL_HANDLE;
        vkCreateFence(Implementation.Device, &FenceInformation, nullptr, &Fence);

        VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        SubmitInformation.commandBufferCount = 1;
        SubmitInformation.pCommandBuffers    = &CommandBuffer;
        bool DispatchEnabled = true;
        if (vkQueueSubmit(Implementation.Queue, 1, &SubmitInformation, Fence) != VK_SUCCESS)
        {
            ReportDispatch("compute dispatch submit failed");
            DispatchEnabled = false;
        }
        else
        {
            vkWaitForFences(Implementation.Device, 1, &Fence, VK_TRUE, UINT64_MAX);
        }

        vkDestroyFence(Implementation.Device, Fence, nullptr);
        vkFreeCommandBuffers(Implementation.Device, Implementation.CommandPool, 1, &CommandBuffer);
        return DispatchEnabled;
    }

    // 📝 One in-flight compute submission: the command buffer + fence, submitted but NOT yet waited. DispatchRun distinguishes a genuine
    //    submit from a failed one (so the caller can fall back). The single exact dispatch fits one ticket; multi-pass floods do not.
    struct GpuDispatchTicket
    {
        VkFence         Fence         = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        bool            DispatchRun   = false;
    };

    // Record + submit one compute dispatch WITHOUT waiting — the async half of DispatchComputePass (same barriers / bind / push / dispatch).
    // Returns true and fills OutTicket with a submitted fence the caller polls; the host must not read the output buffer until the fence
    // signals. On any allocation / submit failure returns false with an empty ticket.
    bool DispatchComputeBegin(GpuBakeContextImplementation& Implementation,
                              ComputePass&                  Pass,
                              VkDescriptorSet               DescriptorSet,
                              const void*                   PushData,
                              uint32_t                      PushSize,
                              uint32_t                      GroupsX,
                              uint32_t                      GroupsY,
                              uint32_t                      GroupsZ,
                              GpuDispatchTicket&            OutTicket)
    {
        OutTicket = GpuDispatchTicket{};

        VkCommandBufferAllocateInfo CommandInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandInformation.commandPool        = Implementation.CommandPool;
        CommandInformation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandInformation.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(Implementation.Device, &CommandInformation, &OutTicket.CommandBuffer) != VK_SUCCESS) return false;

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(OutTicket.CommandBuffer, &BeginInformation);

        VkMemoryBarrier HostToCompute = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        HostToCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        HostToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(OutTicket.CommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &HostToCompute, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(OutTicket.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pass.Pipeline);
        vkCmdBindDescriptorSets(OutTicket.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pass.PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
        if (PushData && PushSize > 0)
            vkCmdPushConstants(OutTicket.CommandBuffer, Pass.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, PushSize, PushData);
        vkCmdDispatch(OutTicket.CommandBuffer, GroupsX, GroupsY, GroupsZ);

        VkMemoryBarrier ComputeToHost = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        ComputeToHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ComputeToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(OutTicket.CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &ComputeToHost, 0, nullptr, 0, nullptr);

        vkEndCommandBuffer(OutTicket.CommandBuffer);

        VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(Implementation.Device, &FenceInformation, nullptr, &OutTicket.Fence);

        VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        SubmitInformation.commandBufferCount = 1;
        SubmitInformation.pCommandBuffers    = &OutTicket.CommandBuffer;
        if (vkQueueSubmit(Implementation.Queue, 1, &SubmitInformation, OutTicket.Fence) != VK_SUCCESS)
        {
            ReportDispatch("async compute dispatch submit failed");
            vkDestroyFence(Implementation.Device, OutTicket.Fence, nullptr);
            vkFreeCommandBuffers(Implementation.Device, Implementation.CommandPool, 1, &OutTicket.CommandBuffer);
            OutTicket = GpuDispatchTicket{};
            return false;
        }
        OutTicket.DispatchRun = true;
        return true;
    }

    // Non-blocking: has the submitted dispatch finished? vkGetFenceStatus never waits (VK_SUCCESS signaled, VK_NOT_READY still running).
    bool DispatchComputeResolved(GpuBakeContextImplementation& Implementation, const GpuDispatchTicket& Ticket)
    {
        if (!Ticket.DispatchRun || Ticket.Fence == VK_NULL_HANDLE) return false;
        return vkGetFenceStatus(Implementation.Device, Ticket.Fence) == VK_SUCCESS;
    }

    // Destroy the ticket's fence + command buffer. The caller frees the host buffers separately (they may outlive a poll loop).
    void DispatchComputeFinalize(GpuBakeContextImplementation& Implementation, GpuDispatchTicket& Ticket)
    {
        if (Ticket.Fence)         vkDestroyFence(Implementation.Device, Ticket.Fence, nullptr);
        if (Ticket.CommandBuffer) vkFreeCommandBuffers(Implementation.Device, Implementation.CommandPool, 1, &Ticket.CommandBuffer);
        Ticket = GpuDispatchTicket{};
    }

    // 📝 The GPU-narrow (modes 1 & 2) async begin: record the exact bake, then narrow to the device-local packed buffer, all in ONE command
    //    buffer + ONE fence on the compute queue (no extra sync round-trip). Mode 2 has the exact shader pack int16 straight into the packed
    //    buffer (PackedMode 1) — a vkCmdFillBuffer zero-clears it first because the shader accumulates with atomicOr. Mode 1 has the exact
    //    shader write int32 (PackedMode 0), then a DistanceNarrow dispatch packs int32 → int16. The packed buffer ends in
    //    TRANSFER_READ-visible state so the graphics-queue copy-to-image can source it. The caller must NOT read voxels on the CPU for these
    //    modes — the whole point is the voxels never touch the host. Returns false (empty ticket) on any allocation / submit failure.
    bool DispatchExactPackedBegin(GpuBakeContextImplementation& Implementation,
                                  ComputePass&                  ExactPass,
                                  VkDescriptorSet               ExactSet,
                                  const DistanceExactPush&      ExactPush,
                                  ComputePass*                  NarrowPass,      // mode 1 only (nullptr for mode 2)
                                  VkDescriptorSet               NarrowSet,       // mode 1 only
                                  const DistanceNarrowPush&     NarrowPush,      // mode 1 only
                                  VkBuffer                      PackedBuffer,
                                  VkDeviceSize                  PackedBytes,
                                  uint32_t                      GroupsX,
                                  uint32_t                      GroupsY,
                                  uint32_t                      GroupsZ,
                                  uint32_t                      VoxelTotal,
                                  bool                          ExactPacksDirect,   // mode 2: exact shader writes packed (PackedMode 1)
                                  GpuDispatchTicket&            OutTicket)
    {
        OutTicket = GpuDispatchTicket{};

        VkCommandBufferAllocateInfo CommandInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandInformation.commandPool        = Implementation.CommandPool;
        CommandInformation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandInformation.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(Implementation.Device, &CommandInformation, &OutTicket.CommandBuffer) != VK_SUCCESS) return false;

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(OutTicket.CommandBuffer, &BeginInformation);

        // Mode 2: the exact shader accumulates into the packed buffer with atomicOr, so zero-clear it first, then order the fill before the
        // compute read/write of that buffer.
        if (ExactPacksDirect)
        {
            vkCmdFillBuffer(OutTicket.CommandBuffer, PackedBuffer, 0, PackedBytes, 0u);
            VkBufferMemoryBarrier FillToCompute = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            FillToCompute.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            FillToCompute.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            FillToCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            FillToCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            FillToCompute.buffer              = PackedBuffer;
            FillToCompute.offset              = 0;
            FillToCompute.size                = PackedBytes;
            vkCmdPipelineBarrier(OutTicket.CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 1, &FillToCompute, 0, nullptr);
        }

        // Order the host memcpy of the triangle input before the exact compute read.
        VkMemoryBarrier HostToCompute = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        HostToCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        HostToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(OutTicket.CommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &HostToCompute, 0, nullptr, 0, nullptr);

        // ── Exact pass ──
        vkCmdBindPipeline(OutTicket.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ExactPass.Pipeline);
        vkCmdBindDescriptorSets(OutTicket.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ExactPass.PipelineLayout, 0, 1, &ExactSet, 0, nullptr);
        vkCmdPushConstants(OutTicket.CommandBuffer, ExactPass.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(ExactPush), &ExactPush);
        vkCmdDispatch(OutTicket.CommandBuffer, GroupsX, GroupsY, GroupsZ);

        if (!ExactPacksDirect && NarrowPass)
        {
            // Mode 1: order the exact int32 write before the narrow read of the same VoxelBuffer, then dispatch the narrow pack pass.
            VkMemoryBarrier ExactToNarrow = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            ExactToNarrow.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            ExactToNarrow.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(OutTicket.CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &ExactToNarrow, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(OutTicket.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, NarrowPass->Pipeline);
            vkCmdBindDescriptorSets(OutTicket.CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, NarrowPass->PipelineLayout, 0, 1, &NarrowSet, 0, nullptr);
            vkCmdPushConstants(OutTicket.CommandBuffer, NarrowPass->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)sizeof(NarrowPush), &NarrowPush);
            vkCmdDispatch(OutTicket.CommandBuffer, DispatchGroups((VoxelTotal + 1u) / 2u, 64), 1, 1);
        }

        // Order the compute write of the packed buffer before the graphics-queue transfer read (copy-to-image). No queue-family transfer:
        // the packed buffer is CONCURRENT over {compute, graphics}, so both families access it directly.
        VkBufferMemoryBarrier ComputeToTransfer = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        ComputeToTransfer.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        ComputeToTransfer.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        ComputeToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ComputeToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ComputeToTransfer.buffer              = PackedBuffer;
        ComputeToTransfer.offset              = 0;
        ComputeToTransfer.size                = PackedBytes;
        vkCmdPipelineBarrier(OutTicket.CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 1, &ComputeToTransfer, 0, nullptr);

        vkEndCommandBuffer(OutTicket.CommandBuffer);

        VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(Implementation.Device, &FenceInformation, nullptr, &OutTicket.Fence);

        VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        SubmitInformation.commandBufferCount = 1;
        SubmitInformation.pCommandBuffers    = &OutTicket.CommandBuffer;
        if (vkQueueSubmit(Implementation.Queue, 1, &SubmitInformation, OutTicket.Fence) != VK_SUCCESS)
        {
            ReportDispatch("async packed compute dispatch submit failed");
            vkDestroyFence(Implementation.Device, OutTicket.Fence, nullptr);
            vkFreeCommandBuffers(Implementation.Device, Implementation.CommandPool, 1, &OutTicket.CommandBuffer);
            OutTicket = GpuDispatchTicket{};
            return false;
        }
        OutTicket.DispatchRun = true;
        return true;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  SDF INPUT PREPARATION
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The padded grid bounds, byte-identical to DistanceVolumeBaker's ResolveDistanceBounds: pad the geometry AABB by 5 %, MaximumDistance
    //    = 1.5 × box diagonal. Computed on the CPU so the GPU volume's boundary / decode scale match the CPU bake exactly.
    struct BakeBounds
    {
        float          BoundaryMinimum[3] = { 0.0f, 0.0f, 0.0f };
        float          BoundaryMaximum[3] = { 0.0f, 0.0f, 0.0f };
        float          VoxelSize[3]       = { 0.0f, 0.0f, 0.0f };
        float          MaximumDistance    = 0.0f;
        DistanceExtent Extent             = {};   // [-] - per-axis voxel count DeriveDistanceExtent resolved from the padded box + budget
    };

    // The per-axis voxel count of a BakeBounds as its DistanceExtent — the shared shape the dispatch group counts / flat indexing read.
    uint32_t ExtentAxis(const DistanceExtent& Extent, int Axis)
    {
        return Axis == 0 ? Extent.X : (Axis == 1 ? Extent.Y : Extent.Z);
    }

    // 📝 Per-axis voxel extent from a padded box at a fixed voxel density — inlined from the CPU baker's DeriveDistanceExtent /
    //    DeriveExtentFromBox so the GPU grid matches the CPU bake byte-for-byte (the CPU baker itself is unported this GPU-only slice).
    //    ResolutionBudget is the voxel count along the LONGEST axis; each shorter axis takes round(budget · axisLength / longestLength) so
    //    every axis shares one cm-per-voxel spacing (the grid is tight to the box, not a cube around the longest side). Every axis clamps to
    //    [FloorVoxels, budget] so a sliver still carries a usable band and no axis exceeds the budget. A cube box yields X==Y==Z==budget.
    DistanceExtent DeriveDistanceExtentLocal(const float PaddedMinimum[3], const float PaddedMaximum[3], uint32_t ResolutionBudget)
    {
        const uint32_t FloorVoxels = 32u;
        const uint32_t Budget      = std::max(FloorVoxels, std::min(ResolutionBudget, 256u));
        const float BoxSizeX = PaddedMaximum[0] - PaddedMinimum[0];
        const float BoxSizeY = PaddedMaximum[1] - PaddedMinimum[1];
        const float BoxSizeZ = PaddedMaximum[2] - PaddedMinimum[2];
        const float Longest  = std::max(BoxSizeX, std::max(BoxSizeY, BoxSizeZ));
        if (Longest <= 0.0f) return DistanceExtent{ Budget, Budget, Budget };

        auto AxisVoxels = [&](float AxisLength) -> uint32_t
        {
            const float Proportional = static_cast<float>(Budget) * (AxisLength / Longest);
            const int   Rounded      = static_cast<int>(std::lround(Proportional));
            return static_cast<uint32_t>(std::max<int>(static_cast<int>(FloorVoxels), std::min<int>(static_cast<int>(Budget), Rounded)));
        };
        return DistanceExtent{ AxisVoxels(BoxSizeX), AxisVoxels(BoxSizeY), AxisVoxels(BoxSizeZ) };
    }

    // Build the flat GPU triangle list + the padded bounds from a render stream (dropping degenerate triangles), reproducing the CPU
    // baker's angle-weighted vertex / mean-edge pseudonormals so the GPU sign matches. Returns false when no non-degenerate triangle
    // survives.
    bool PrepareBakeTriangles(const RenderVertexStream&     Stream,
                              uint32_t                      Resolution,
                              std::vector<GpuBakeTriangle>& OutTriangles,
                              BakeBounds&                   OutBounds)
    {
        const size_t IndexTotal = Stream.Indices.size();
        if (IndexTotal < 3) return false;

        // First pass: face normals + AABB, and accumulate angle-weighted vertex pseudonormals keyed by rounded position.
        struct RawTriangle { float A[3]; float B[3]; float C[3]; float FaceNormal[3]; };
        std::vector<RawTriangle> Raw;
        Raw.reserve(IndexTotal / 3);

        float GeometryMinimum[3] = {  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
        float GeometryMaximum[3] = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };

        for (size_t IndexIterator = 0; IndexIterator + 2 < IndexTotal; IndexIterator += 3)
        {
            const uint32_t IndexA = Stream.Indices[IndexIterator + 0];
            const uint32_t IndexB = Stream.Indices[IndexIterator + 1];
            const uint32_t IndexC = Stream.Indices[IndexIterator + 2];
            if (IndexA >= Stream.Vertices.size() || IndexB >= Stream.Vertices.size() || IndexC >= Stream.Vertices.size()) continue;

            const RenderVertex& VertexA = Stream.Vertices[IndexA];
            const RenderVertex& VertexB = Stream.Vertices[IndexB];
            const RenderVertex& VertexC = Stream.Vertices[IndexC];

            const float EdgeAB[3] = { VertexB.Position[0] - VertexA.Position[0], VertexB.Position[1] - VertexA.Position[1], VertexB.Position[2] - VertexA.Position[2] };
            const float EdgeAC[3] = { VertexC.Position[0] - VertexA.Position[0], VertexC.Position[1] - VertexA.Position[1], VertexC.Position[2] - VertexA.Position[2] };
            float Cross[3] = { EdgeAB[1] * EdgeAC[2] - EdgeAB[2] * EdgeAC[1],
                               EdgeAB[2] * EdgeAC[0] - EdgeAB[0] * EdgeAC[2],
                               EdgeAB[0] * EdgeAC[1] - EdgeAB[1] * EdgeAC[0] };
            const float CrossLength = std::sqrt(Cross[0] * Cross[0] + Cross[1] * Cross[1] + Cross[2] * Cross[2]);
            if (CrossLength <= 1e-12f) continue;   // degenerate, drop

            RawTriangle Triangle;
            Triangle.A[0] = VertexA.Position[0]; Triangle.A[1] = VertexA.Position[1]; Triangle.A[2] = VertexA.Position[2];
            Triangle.B[0] = VertexB.Position[0]; Triangle.B[1] = VertexB.Position[1]; Triangle.B[2] = VertexB.Position[2];
            Triangle.C[0] = VertexC.Position[0]; Triangle.C[1] = VertexC.Position[1]; Triangle.C[2] = VertexC.Position[2];
            Triangle.FaceNormal[0] = Cross[0] / CrossLength;
            Triangle.FaceNormal[1] = Cross[1] / CrossLength;
            Triangle.FaceNormal[2] = Cross[2] / CrossLength;
            Raw.push_back(Triangle);

            for (int Corner = 0; Corner < 3; ++Corner)
            {
                const float* Position = Corner == 0 ? Triangle.A : (Corner == 1 ? Triangle.B : Triangle.C);
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    GeometryMinimum[Axis] = std::min(GeometryMinimum[Axis], Position[Axis]);
                    GeometryMaximum[Axis] = std::max(GeometryMaximum[Axis], Position[Axis]);
                }
            }
        }
        if (Raw.empty()) return false;

        // Padded AABB — matches ResolvePaddedAabb exactly (5 % half-span pad + 1e-3 floor).
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Centre    = 0.5f * (GeometryMinimum[Axis] + GeometryMaximum[Axis]);
            const float HalfSpan  = 0.5f * (GeometryMaximum[Axis] - GeometryMinimum[Axis]);
            const float PaddedHalf = HalfSpan * 1.05f + 1e-3f;
            OutBounds.BoundaryMinimum[Axis] = Centre - PaddedHalf;
            OutBounds.BoundaryMaximum[Axis] = Centre + PaddedHalf;
        }

        // 📝 Per-axis extent + voxel size — the GPU path derives the SAME per-axis grid the CPU baker does by calling the canonical
        //    DeriveDistanceExtent on the SAME padded box + budget, so GPU / CPU extents (and thus VoxelSize) agree byte-for-byte. A cube
        //    box yields X==Y==Z==Resolution, reproducing the old scalar-cube VoxelSize = span / Resolution exactly.
        OutBounds.Extent = DeriveDistanceExtentLocal(OutBounds.BoundaryMinimum, OutBounds.BoundaryMaximum, Resolution);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const uint32_t AxisVoxels = std::max(1u, ExtentAxis(OutBounds.Extent, Axis));
            OutBounds.VoxelSize[Axis] = (OutBounds.BoundaryMaximum[Axis] - OutBounds.BoundaryMinimum[Axis]) / (float)AxisVoxels;
        }
        const float BoxSize[3] = { OutBounds.BoundaryMaximum[0] - OutBounds.BoundaryMinimum[0],
                                   OutBounds.BoundaryMaximum[1] - OutBounds.BoundaryMinimum[1],
                                   OutBounds.BoundaryMaximum[2] - OutBounds.BoundaryMinimum[2] };
        OutBounds.MaximumDistance = 1.5f * std::sqrt(BoxSize[0] * BoxSize[0] + BoxSize[1] * BoxSize[1] + BoxSize[2] * BoxSize[2]);

        // 📝 Angle-weighted vertex pseudonormals + mean-edge pseudonormals reproduce the EXACT inside/outside sign of the CPU baker
        //    (Baerentzen & Aanaes) — a faithful port of DistanceVolumeBaker.cpp's ConstructPseudonormals. Two adjacency passes over the
        //    welded corners: each vertex accumulates every incident face normal scaled by that face's interior angle at the shared
        //    corner; each edge accumulates the (≤2) face normals sharing it. Both are welded by rounded position so seam-split render
        //    corners at one 3D location agree. Signing by the resolved feature's pseudonormal — not the flat face normal — is what
        //    fixes the GPU bake looking worse than the CPU one at sharp low-poly corners.
        struct WeldKey
        {
            int64_t X; int64_t Y; int64_t Z;
            bool operator==(const WeldKey& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }
        };
        struct WeldKeyHash
        {
            size_t operator()(const WeldKey& Key) const
            {
                const uint64_t MixX = (uint64_t)Key.X * 0x9E3779B97F4A7C15ull;
                const uint64_t MixY = (uint64_t)Key.Y * 0xC2B2AE3D27D4EB4Full;
                const uint64_t MixZ = (uint64_t)Key.Z * 0x165667B19E3779F9ull;
                return (size_t)(MixX ^ MixY ^ MixZ);
            }
        };
        auto ResolveWeldKey = [](const float* Position) -> WeldKey
        {
            const double Quantum = 1.0 / 1e-4;   // 1e-4 cm weld grid — matches the CPU baker
            return WeldKey{ (int64_t)std::llround((double)Position[0] * Quantum),
                            (int64_t)std::llround((double)Position[1] * Quantum),
                            (int64_t)std::llround((double)Position[2] * Quantum) };
        };
        auto ResolveEdgeKey = [&](const float* First, const float* Second) -> WeldKey
        {
            const WeldKey Alpha = ResolveWeldKey(First);
            const WeldKey Beta  = ResolveWeldKey(Second);
            return WeldKey{ Alpha.X + Beta.X, Alpha.Y + Beta.Y, Alpha.Z + Beta.Z };   // order-independent
        };
        auto CornerAngle = [](const float* Apex, const float* Left, const float* Right) -> float
        {
            float ToLeft[3]  = { Left[0]  - Apex[0], Left[1]  - Apex[1], Left[2]  - Apex[2] };
            float ToRight[3] = { Right[0] - Apex[0], Right[1] - Apex[1], Right[2] - Apex[2] };
            const float LeftLength  = std::sqrt(ToLeft[0]*ToLeft[0]   + ToLeft[1]*ToLeft[1]   + ToLeft[2]*ToLeft[2]);
            const float RightLength = std::sqrt(ToRight[0]*ToRight[0] + ToRight[1]*ToRight[1] + ToRight[2]*ToRight[2]);
            if (LeftLength < 1e-20f || RightLength < 1e-20f) return 0.0f;
            const float Cosine = (ToLeft[0]*ToRight[0] + ToLeft[1]*ToRight[1] + ToLeft[2]*ToRight[2]) / (LeftLength * RightLength);
            return std::acos(std::max(-1.0f, std::min(1.0f, Cosine)));
        };
        auto Normalize = [](float* Target)
        {
            const float Length = std::sqrt(Target[0]*Target[0] + Target[1]*Target[1] + Target[2]*Target[2]);
            if (Length < 1e-20f) { Target[0] = Target[1] = Target[2] = 0.0f; return; }
            Target[0] /= Length; Target[1] /= Length; Target[2] /= Length;
        };

        std::unordered_map<WeldKey, std::array<float, 3>, WeldKeyHash> VertexAccumulator;
        std::unordered_map<WeldKey, std::array<float, 3>, WeldKeyHash> EdgeAccumulator;
        VertexAccumulator.reserve(Raw.size() * 2);
        EdgeAccumulator.reserve(Raw.size() * 3);
        for (const RawTriangle& Triangle : Raw)
        {
            const float* Corners[3] = { Triangle.A, Triangle.B, Triangle.C };
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                const float Angle = CornerAngle(Corners[Corner], Corners[(Corner + 1) % 3], Corners[(Corner + 2) % 3]);
                std::array<float, 3>& Slot = VertexAccumulator[ResolveWeldKey(Corners[Corner])];
                for (int Axis = 0; Axis < 3; ++Axis) Slot[Axis] += Triangle.FaceNormal[Axis] * Angle;
            }
            for (int Edge = 0; Edge < 3; ++Edge)
            {
                std::array<float, 3>& Slot = EdgeAccumulator[ResolveEdgeKey(Corners[Edge], Corners[(Edge + 1) % 3])];
                for (int Axis = 0; Axis < 3; ++Axis) Slot[Axis] += Triangle.FaceNormal[Axis];
            }
        }

        OutTriangles.resize(Raw.size());
        for (size_t TriangleIndex = 0; TriangleIndex < Raw.size(); ++TriangleIndex)
        {
            const RawTriangle& Source = Raw[TriangleIndex];
            GpuBakeTriangle& Destination = OutTriangles[TriangleIndex];
            std::memset(&Destination, 0, sizeof(Destination));
            const float* Corners[3] = { Source.A, Source.B, Source.C };
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Destination.CornerA[Axis]    = Source.A[Axis];
                Destination.CornerB[Axis]    = Source.B[Axis];
                Destination.CornerC[Axis]    = Source.C[Axis];
                Destination.FaceNormal[Axis] = Source.FaceNormal[Axis];
            }
            float* CornerNormals[3] = { Destination.CornerNormalA, Destination.CornerNormalB, Destination.CornerNormalC };
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                const std::array<float, 3>& Slot = VertexAccumulator[ResolveWeldKey(Corners[Corner])];
                for (int Axis = 0; Axis < 3; ++Axis) CornerNormals[Corner][Axis] = Slot[Axis];
                Normalize(CornerNormals[Corner]);
            }
            float* EdgeNormals[3] = { Destination.EdgeNormalAB, Destination.EdgeNormalBC, Destination.EdgeNormalCA };
            for (int Edge = 0; Edge < 3; ++Edge)
            {
                const std::array<float, 3>& Slot = EdgeAccumulator[ResolveEdgeKey(Corners[Edge], Corners[(Edge + 1) % 3])];
                for (int Axis = 0; Axis < 3; ++Axis) EdgeNormals[Edge][Axis] = Slot[Axis];
                Normalize(EdgeNormals[Edge]);
            }
        }
        return true;
    }

    // Write the finished R16_SNORM voxels + bounds / resolution / decode scale into the caller's volume (the shape the CPU baker produces).
    void PublishDistanceVolume(const std::vector<int16_t>& Voxels,
                               const BakeBounds&           Bounds,
                               SignedDistanceVolume&       OutVolume)
    {
        OutVolume.BoundaryMinimum = Vector3d{ Bounds.BoundaryMinimum[0], Bounds.BoundaryMinimum[1], Bounds.BoundaryMinimum[2] };
        OutVolume.BoundaryMaximum = Vector3d{ Bounds.BoundaryMaximum[0], Bounds.BoundaryMaximum[1], Bounds.BoundaryMaximum[2] };
        OutVolume.Resolution      = Bounds.Extent;
        OutVolume.MaximumDistance = Bounds.MaximumDistance;
        OutVolume.Voxels          = Voxels;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 ASYNC EXACT DISPATCH STATE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The full definition of the opaque GpuBakeAsyncRun the header forward-declares. It holds everything the in-flight exact dispatch
    //    needs to stay alive between Begin and Resolve: the submitted ticket, the two host storage buffers (triangles in, full-grid voxels
    //    out) kept mapped until the fence signals, the resolved bounds / resolution to publish on a whole-grid run, and the readback sub-box.
    //    RegionRun selects the narrow-back shape in Resolve; a whole-grid run overwrites OutVolume wholesale, a region run patches the box.
    struct GpuBakeAsyncRunImplementation
    {
        GpuDispatchTicket Ticket;
        HostBuffer        TriangleBuffer;
        HostBuffer        VoxelBuffer;                 // [-] - int32 host voxels (CPU narrow mode 0, and the source for GPU narrow mode 1)
        DeviceBuffer      VoxelPackedBuffer;           // [-] - packed int16 device voxels (GPU modes 1 & 2); Buffer == VK_NULL_HANDLE when unused
        BakeBounds        Bounds;
        DistanceExtent    Extent        = {};          // [-] - per-axis voxel count of the run (whole-grid publish + box clamp read it)
        bool              RegionRun     = false;
        uint32_t          BoxMinimum[3] = { 0u, 0u, 0u };
        uint32_t          BoxExtent[3]  = { 0u, 0u, 0u };
        int32_t           NarrowMode    = 0;           // [-] - 0 CPU narrow, 1 GPU narrow shader (a), 2 shader-packs-int16 (b)
    };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    SDF COMPUTE DISPATCH
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Fill the DistanceExactPush from the resolved bounds + triangle count. Shared by the exact bake + the resolve pass's decode.
    //    🔍 BoxMinimum / BoxExtent optionally bound the dispatch to a dirty sub-box (both nullptr → whole grid). The shader offsets its
    //    invocation by BoxMinimum and writes the ABSOLUTE voxel index, so only BoxExtent voxels are computed while the output buffer stays
    //    full-grid sized. Whole-grid callers pass {0,0,0} + {Resolution,...} so the byte layout is identical on both paths.
    void FillExactPush(const BakeBounds&     Bounds,
                       const DistanceExtent& Extent,
                       uint32_t              TriangleCount,
                       const uint32_t*       BoxMinimum,
                       const uint32_t*       BoxExtent,
                       uint32_t              PackedMode,
                       DistanceExactPush&    Push)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Push.BoundaryMinimum[Axis] = Bounds.BoundaryMinimum[Axis];
            Push.VoxelSize[Axis]       = Bounds.VoxelSize[Axis];
        }
        Push.BoundaryMinimum[3] = 0.0f;
        Push.VoxelSize[3]       = 0.0f;
        Push.ResolutionX        = Extent.X;
        Push.ResolutionY        = Extent.Y;
        Push.ResolutionZ        = Extent.Z;
        Push.TriangleCount      = TriangleCount;
        Push.MaximumDistance    = Bounds.MaximumDistance;
        Push.PackedMode         = PackedMode;
        Push.Pad0               = 0u;
        Push.Pad1               = 0u;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Push.BoxMinimum[Axis] = BoxMinimum ? BoxMinimum[Axis] : 0u;
            Push.BoxExtent[Axis]  = BoxExtent  ? BoxExtent[Axis]  : ExtentAxis(Extent, Axis);
        }
        Push.BoxMinimum[3] = 0u;
        Push.BoxExtent[3]  = 0u;
    }

    // Exact per-voxel closest-point bake: one invocation per voxel walks the whole triangle list. Matches the CPU ExactBoundingVolume.
    // 🔍 BoxMinimum / BoxExtent optionally bound the dispatch to a dirty sub-box (nullptr → whole grid). The output buffer is ALWAYS
    //    full-grid sized because the shader writes absolute voxel indices; only BoxExtent voxels are computed, and only that sub-box is
    //    narrowed back into OutVoxels — the rest of OutVoxels is left untouched (the region caller pre-seeds it with the retained field).
    bool EvaluateDistanceExactInternal(GpuBakeContextImplementation& Implementation,
                                       std::vector<GpuBakeTriangle>& Triangles,
                                       const BakeBounds&             Bounds,
                                       const DistanceExtent&         Extent,
                                       std::vector<int16_t>&         OutVoxels,
                                       const uint32_t*               BoxMinimum = nullptr,
                                       const uint32_t*               BoxExtent  = nullptr)
    {
        ComputePass& Pass = Implementation.Passes[(uint32_t)BakePassIdentity::DistanceExact];
        if (!BuildComputePass(Implementation, Pass, (uint32_t)sizeof(DistanceExactPush))) return false;

        // Whole-grid extent when no box is supplied — used for both the dispatch group count and the box-scoped readback.
        const uint32_t DispatchMinimum[3] = { BoxMinimum ? BoxMinimum[0] : 0u, BoxMinimum ? BoxMinimum[1] : 0u, BoxMinimum ? BoxMinimum[2] : 0u };
        const uint32_t DispatchExtent[3]  = { BoxExtent  ? BoxExtent[0]  : Extent.X,
                                              BoxExtent  ? BoxExtent[1]  : Extent.Y,
                                              BoxExtent  ? BoxExtent[2]  : Extent.Z };

        // The R16_SNORM voxels are packed one int16 per voxel; the SSBO stores them as uint (two voxels per uint) — pad the output to an
        // even voxel count so the shader can write 16-bit lanes into 32-bit words. Simpler: store one int32 per voxel, narrow on readback.
        HostBuffer TriangleBuffer;
        HostBuffer VoxelBuffer;
        const VkDeviceSize TriangleBytes = (VkDeviceSize)Triangles.size() * sizeof(GpuBakeTriangle);
        const VkDeviceSize VoxelBytes    = (VkDeviceSize)OutVoxels.size() * sizeof(int32_t);
        bool BuildEnabled = ConstructHostBuffer(Implementation, TriangleBytes, TriangleBuffer) &&
                            ConstructHostBuffer(Implementation, VoxelBytes, VoxelBuffer);
        if (!BuildEnabled)
        {
            DestroyHostBuffer(Implementation.Device, TriangleBuffer);
            DestroyHostBuffer(Implementation.Device, VoxelBuffer);
            return false;
        }
        std::memcpy(TriangleBuffer.Mapped, Triangles.data(), (size_t)TriangleBytes);

        // Binding 2 (packed int16) is unused on this synchronous CPU-narrow path (PackedMode 0 writes only binding 1); alias the int32
        // VoxelBuffer into it so every declared binding is populated (no null-descriptor validation fault). The shader never writes it.
        const HostBuffer* Buffers[3] = { &TriangleBuffer, &VoxelBuffer, &VoxelBuffer };
        VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
        DistanceExactPush Push; FillExactPush(Bounds, Extent, (uint32_t)Triangles.size(), DispatchMinimum, DispatchExtent, 0u, Push);

        bool DispatchEnabled = BindPassDescriptors(Implementation, Pass, Buffers, DescriptorSet) &&
                               DispatchComputePass(Implementation, Pass, DescriptorSet, &Push, (uint32_t)sizeof(Push),
                                                   DispatchGroups(DispatchExtent[0], 4), DispatchGroups(DispatchExtent[1], 4), DispatchGroups(DispatchExtent[2], 4));
        if (DispatchEnabled)
        {
            const int32_t* Source = static_cast<const int32_t*>(VoxelBuffer.Mapped);
            // Box-scoped narrow-back: iterate only the computed sub-box (absolute voxel indices), leaving the rest of OutVoxels intact.
            const uint32_t ClampMaxX = std::min(DispatchMinimum[0] + DispatchExtent[0], Extent.X);
            const uint32_t ClampMaxY = std::min(DispatchMinimum[1] + DispatchExtent[1], Extent.Y);
            const uint32_t ClampMaxZ = std::min(DispatchMinimum[2] + DispatchExtent[2], Extent.Z);
            for (uint32_t Z = DispatchMinimum[2]; Z < ClampMaxZ; ++Z)
                for (uint32_t Y = DispatchMinimum[1]; Y < ClampMaxY; ++Y)
                    for (uint32_t X = DispatchMinimum[0]; X < ClampMaxX; ++X)
                    {
                        const size_t Index = VoxelIndex(Extent, X, Y, Z);
                        OutVoxels[Index] = (int16_t)Source[Index];
                    }
        }

        DestroyHostBuffer(Implementation.Device, TriangleBuffer);
        DestroyHostBuffer(Implementation.Device, VoxelBuffer);
        return DispatchEnabled;
    }

    // Jump-Flooding bake (fast / realtime): seed voxels straddling a triangle with the nearest surface point, propagate the nearest seed
    // with a log₂(N) sequence of flood passes (ping-pong buffers), then resolve each voxel's nearest seed into a signed R16_SNORM voxel.
    // 📝 BandRefine (coarse-JFA + narrow-exact composition): when true, after the whole-grid JFA resolve a band-exact pass walks the triangle
    //    list and OVERWRITES the exact distance for every voxel within BandRadius of the surface (far voxels keep their JFA value, the
    //    triangle loop never runs for them). BandRadius ≤ 0 auto-selects a tight ~4-voxel-diagonal band. The whole grid is resolved by cheap
    //    JFA; only the thin surface shell pays the exact triangle loop, so the bake is exact where shadows/GI read the field at a fraction of
    //    a full exact bake's cost. BandRefine == false is the plain fast JFA bake (unchanged).
    bool EvaluateDistanceFloodInternal(GpuBakeContextImplementation& Implementation,
                                       std::vector<GpuBakeTriangle>& Triangles,
                                       const BakeBounds&             Bounds,
                                       const DistanceExtent&         Extent,
                                       std::vector<int16_t>&         OutVoxels,
                                       bool                          BandRefine = false,
                                       float                         BandRadius = 0.0f)
    {
        ComputePass& SeedPass    = Implementation.Passes[(uint32_t)BakePassIdentity::DistanceSeed];
        ComputePass& FloodPass   = Implementation.Passes[(uint32_t)BakePassIdentity::DistanceFlood];
        ComputePass& ResolvePass = Implementation.Passes[(uint32_t)BakePassIdentity::DistanceResolve];
        if (!BuildComputePass(Implementation, SeedPass,    (uint32_t)sizeof(DistanceSeedPush))   ||
            !BuildComputePass(Implementation, FloodPass,   (uint32_t)sizeof(DistanceFloodPush))  ||
            !BuildComputePass(Implementation, ResolvePass, (uint32_t)sizeof(DistanceResolvePush)))
            return false;

        const uint32_t VoxelCount = (uint32_t)VoxelTotal(Extent);

        // Ping-pong seed buffers: SeedPoint is the nearest surface point per voxel (vec4, w = signed flag), SeedValid a uint per voxel.
        HostBuffer TriangleBuffer;
        HostBuffer SeedPointA, SeedValidA, SeedPointB, SeedValidB;
        HostBuffer VoxelBuffer;
        const VkDeviceSize TriangleBytes = (VkDeviceSize)Triangles.size() * sizeof(GpuBakeTriangle);
        const VkDeviceSize PointBytes    = (VkDeviceSize)VoxelCount * sizeof(float) * 4;
        const VkDeviceSize ValidBytes    = (VkDeviceSize)VoxelCount * sizeof(uint32_t);
        const VkDeviceSize VoxelBytes    = (VkDeviceSize)VoxelCount * sizeof(int32_t);
        bool BuildEnabled = ConstructHostBuffer(Implementation, TriangleBytes, TriangleBuffer) &&
                            ConstructHostBuffer(Implementation, PointBytes,    SeedPointA)     &&
                            ConstructHostBuffer(Implementation, ValidBytes,    SeedValidA)     &&
                            ConstructHostBuffer(Implementation, PointBytes,    SeedPointB)     &&
                            ConstructHostBuffer(Implementation, ValidBytes,    SeedValidB)     &&
                            ConstructHostBuffer(Implementation, VoxelBytes,    VoxelBuffer);
        auto Cleanup = [&]()
        {
            DestroyHostBuffer(Implementation.Device, TriangleBuffer);
            DestroyHostBuffer(Implementation.Device, SeedPointA);
            DestroyHostBuffer(Implementation.Device, SeedValidA);
            DestroyHostBuffer(Implementation.Device, SeedPointB);
            DestroyHostBuffer(Implementation.Device, SeedValidB);
            DestroyHostBuffer(Implementation.Device, VoxelBuffer);
        };
        if (!BuildEnabled) { Cleanup(); return false; }
        std::memcpy(TriangleBuffer.Mapped, Triangles.data(), (size_t)TriangleBytes);

        // ① Seed pass — [triangles][seedPoint A][seedValid A].
        {
            const HostBuffer* SeedBuffers[3] = { &TriangleBuffer, &SeedPointA, &SeedValidA };
            VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
            DistanceSeedPush Push;
            for (int Axis = 0; Axis < 3; ++Axis) { Push.BoundaryMinimum[Axis] = Bounds.BoundaryMinimum[Axis]; Push.VoxelSize[Axis] = Bounds.VoxelSize[Axis]; }
            Push.BoundaryMinimum[3] = 0.0f; Push.VoxelSize[3] = 0.0f;
            Push.ResolutionX = Extent.X; Push.ResolutionY = Extent.Y; Push.ResolutionZ = Extent.Z; Push.TriangleCount = (uint32_t)Triangles.size();
            Push.MaximumDistance = Bounds.MaximumDistance; Push.Pad = 0;
            if (!BindPassDescriptors(Implementation, SeedPass, SeedBuffers, DescriptorSet) ||
                !DispatchComputePass(Implementation, SeedPass, DescriptorSet, &Push, (uint32_t)sizeof(Push),
                                     DispatchGroups(Extent.X, 4), DispatchGroups(Extent.Y, 4), DispatchGroups(Extent.Z, 4)))
            { Cleanup(); return false; }
        }

        // ② Flood passes — ping-pong src → dst, halving the step from N/2 down to 1.
        HostBuffer* PointSrc = &SeedPointA; HostBuffer* ValidSrc = &SeedValidA;
        HostBuffer* PointDst = &SeedPointB; HostBuffer* ValidDst = &SeedValidB;
        // 📝 The flood is isotropic in voxel space, so one scalar step over the LONGEST axis covers every axis. Start at half the longest
        //    axis and halve to 1 — a cube grid (X==Y==Z) reduces to the old Resolution/2 sequence exactly.
        const uint32_t LongestAxis = std::max({ Extent.X, Extent.Y, Extent.Z });
        for (uint32_t Step = LongestAxis / 2; Step >= 1; Step /= 2)
        {
            const HostBuffer* FloodBuffers[4] = { PointSrc, ValidSrc, PointDst, ValidDst };
            VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
            DistanceFloodPush Push;
            for (int Axis = 0; Axis < 3; ++Axis) { Push.BoundaryMinimum[Axis] = Bounds.BoundaryMinimum[Axis]; Push.VoxelSize[Axis] = Bounds.VoxelSize[Axis]; }
            Push.BoundaryMinimum[3] = 0.0f; Push.VoxelSize[3] = 0.0f;
            Push.ResolutionX = Extent.X; Push.ResolutionY = Extent.Y; Push.ResolutionZ = Extent.Z; Push.StepSize = (int32_t)Step; Push.Pad0 = 0; Push.Pad1 = 0;
            if (!BindPassDescriptors(Implementation, FloodPass, FloodBuffers, DescriptorSet) ||
                !DispatchComputePass(Implementation, FloodPass, DescriptorSet, &Push, (uint32_t)sizeof(Push),
                                     DispatchGroups(Extent.X, 4), DispatchGroups(Extent.Y, 4), DispatchGroups(Extent.Z, 4)))
            { Cleanup(); return false; }
            HostBuffer* SwapPoint = PointSrc; PointSrc = PointDst; PointDst = SwapPoint;
            HostBuffer* SwapValid = ValidSrc; ValidSrc = ValidDst; ValidDst = SwapValid;
            if (Step == 1) break;   // avoid the unsigned wrap when Step/2 would underflow past 0
        }

        // ③ Resolve pass — [seedPoint src][seedValid src][outVoxels]: nearest surface point → signed R16_SNORM voxel.
        {
            const HostBuffer* ResolveBuffers[4] = { PointSrc, ValidSrc, &VoxelBuffer, &TriangleBuffer };   // binding 3 = triangles for the per-voxel sign
            VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
            DistanceResolvePush Push;
            for (int Axis = 0; Axis < 3; ++Axis) { Push.BoundaryMinimum[Axis] = Bounds.BoundaryMinimum[Axis]; Push.VoxelSize[Axis] = Bounds.VoxelSize[Axis]; }
            Push.BoundaryMinimum[3] = 0.0f; Push.VoxelSize[3] = 0.0f;
            Push.ResolutionX = Extent.X; Push.ResolutionY = Extent.Y; Push.ResolutionZ = Extent.Z; Push.MaximumDistance = Bounds.MaximumDistance; Push.Pad0 = 0; Push.Pad1 = 0;
            if (!BindPassDescriptors(Implementation, ResolvePass, ResolveBuffers, DescriptorSet) ||
                !DispatchComputePass(Implementation, ResolvePass, DescriptorSet, &Push, (uint32_t)sizeof(Push),
                                     DispatchGroups(Extent.X, 4), DispatchGroups(Extent.Y, 4), DispatchGroups(Extent.Z, 4)))
            { Cleanup(); return false; }
        }

        // ④ Band-exact refine (coarse-JFA + narrow-exact) — [triangles][voxels]: recompute the EXACT distance in place for band voxels only.
        //    Each DispatchComputePass is its own fence-waited submit, so the resolve's writes to VoxelBuffer are fully visible here. The band
        //    pass reads the JFA int32 value, and where |distance| ≤ BandRadius walks the triangle list and overwrites the voxel exactly.
        if (BandRefine)
        {
            ComputePass& BandPass = Implementation.Passes[(uint32_t)BakePassIdentity::DistanceBandExact];
            if (!BuildComputePass(Implementation, BandPass, (uint32_t)sizeof(DistanceBandExactPush))) { Cleanup(); return false; }

            // Auto-band: a tight shell of ~4 voxel diagonals keeps the exact triangle loop to a thin surface layer while covering the
            //    contact / shadow / GI band. The diagonal (not one axis) so an oblong voxel still bands symmetrically in world space.
            float ResolvedBand = BandRadius;
            if (ResolvedBand <= 0.0f)
            {
                const float VoxelDiagonal = std::sqrt(Bounds.VoxelSize[0] * Bounds.VoxelSize[0] +
                                                      Bounds.VoxelSize[1] * Bounds.VoxelSize[1] +
                                                      Bounds.VoxelSize[2] * Bounds.VoxelSize[2]);
                ResolvedBand = 4.0f * VoxelDiagonal;
            }

            const HostBuffer* BandBuffers[2] = { &TriangleBuffer, &VoxelBuffer };
            VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
            DistanceBandExactPush BandPush;
            for (int Axis = 0; Axis < 3; ++Axis) { BandPush.BoundaryMinimum[Axis] = Bounds.BoundaryMinimum[Axis]; BandPush.VoxelSize[Axis] = Bounds.VoxelSize[Axis]; }
            BandPush.BoundaryMinimum[3] = 0.0f; BandPush.VoxelSize[3] = 0.0f;
            BandPush.ResolutionX = Extent.X; BandPush.ResolutionY = Extent.Y; BandPush.ResolutionZ = Extent.Z;
            BandPush.TriangleCount = (uint32_t)Triangles.size(); BandPush.MaximumDistance = Bounds.MaximumDistance;
            BandPush.BandRadius = ResolvedBand; BandPush.Pad0 = 0; BandPush.Pad1 = 0;
            if (!BindPassDescriptors(Implementation, BandPass, BandBuffers, DescriptorSet) ||
                !DispatchComputePass(Implementation, BandPass, DescriptorSet, &BandPush, (uint32_t)sizeof(BandPush),
                                     DispatchGroups(Extent.X, 4), DispatchGroups(Extent.Y, 4), DispatchGroups(Extent.Z, 4)))
            { Cleanup(); return false; }
        }

        // ⑤ Narrow the (band-refined) int32 store to the int16 result.
        {
            const int32_t* Source = static_cast<const int32_t*>(VoxelBuffer.Mapped);
            for (size_t VoxelIndex = 0; VoxelIndex < OutVoxels.size(); ++VoxelIndex)
                OutVoxels[VoxelIndex] = (int16_t)Source[VoxelIndex];
        }

        Cleanup();
        return true;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SURFACE INPUT PREPARATION
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Which pass id encodes a given surface map. World + tangent share SurfaceNormalEncode (branched by push Identity); bent + thickness
    //    share SurfaceOcclusionEncode (the ray-traced pass); position + curvature have their own. Mirrors the CPU encoder dispatch.
    BakePassIdentity SurfacePassFor(SurfaceMapIdentity Identity)
    {
        switch (Identity)
        {
            case SurfaceMapIdentity::WorldNormal:
            case SurfaceMapIdentity::TangentNormal: return BakePassIdentity::SurfaceNormal;
            case SurfaceMapIdentity::Position:      return BakePassIdentity::SurfacePosition;
            case SurfaceMapIdentity::BentNormal:
            case SurfaceMapIdentity::Thickness:     return BakePassIdentity::SurfaceOcclusion;
            case SurfaceMapIdentity::Curvature:
            case SurfaceMapIdentity::Cavity:
            case SurfaceMapIdentity::Convexity:
            case SurfaceMapIdentity::Concavity:
            case SurfaceMapIdentity::Pointiness:    return BakePassIdentity::SurfaceCurvature;
            case SurfaceMapIdentity::Bevel:         return BakePassIdentity::SurfaceBevel;
            case SurfaceMapIdentity::Dust:          return BakePassIdentity::SurfaceDust;
            case SurfaceMapIdentity::AmbientOcclusion: return BakePassIdentity::SurfaceAmbientOcclusion;
            default:                                return BakePassIdentity::SurfaceNormal;
        }
    }

    // Whether an identity needs the ray volume uploaded (the traced occlusion pass and the cone-traced bevel pass do).
    bool SurfaceTracesRays(SurfaceMapIdentity Identity)
    {
        return Identity == SurfaceMapIdentity::BentNormal ||
               Identity == SurfaceMapIdentity::Thickness  ||
               Identity == SurfaceMapIdentity::Bevel      ||
               Identity == SurfaceMapIdentity::AmbientOcclusion;
    }

    // Flatten the CPU SurfaceSampleField into the std430 texel records the encoders read.
    void PrepareTexelSamples(const SurfaceSampleField& Field, std::vector<GpuTexelSample>& OutTexels)
    {
        const size_t TexelTotal = Field.Texels.size();
        OutTexels.resize(TexelTotal);
        for (size_t TexelIndex = 0; TexelIndex < TexelTotal; ++TexelIndex)
        {
            const TexelSample& Source = Field.Texels[TexelIndex];
            GpuTexelSample& Destination = OutTexels[TexelIndex];
            std::memset(&Destination, 0, sizeof(Destination));
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Destination.WorldPosition[Axis] = Source.WorldPosition[Axis];
                Destination.SurfaceNormal[Axis] = Source.SurfaceNormal[Axis];
                Destination.TangentBasisT[Axis] = Source.TangentBasisT[Axis];
                Destination.TangentBasisB[Axis] = Source.TangentBasisB[Axis];
            }
            Destination.TriangleIndex = Source.TriangleIndex;
            Destination.CoverageMask  = Source.CoverageMask ? 1u : 0u;
        }
    }

    // Flatten the ray volume's triangles + tree + ordered index list into their std430 upload records (occlusion pass only).
    void PrepareRayVolume(const TriangleRayVolume&      Volume,
                          std::vector<GpuRayTriangle>&  OutTriangles,
                          std::vector<GpuRayElement>&   OutElements,
                          std::vector<int32_t>&         OutOrdered)
    {
        OutTriangles.resize(Volume.Triangles.size());
        for (size_t TriangleIndex = 0; TriangleIndex < Volume.Triangles.size(); ++TriangleIndex)
        {
            const RayTriangle& Source = Volume.Triangles[TriangleIndex];
            GpuRayTriangle& Destination = OutTriangles[TriangleIndex];
            std::memset(&Destination, 0, sizeof(Destination));
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Destination.CornerA[Axis]         = Source.CornerA[Axis];
                Destination.CornerB[Axis]         = Source.CornerB[Axis];
                Destination.CornerC[Axis]         = Source.CornerC[Axis];
                Destination.FaceNormal[Axis]      = Source.FaceNormal[Axis];
                Destination.VertexNormalA[Axis]   = Source.VertexNormal[0][Axis];
                Destination.VertexNormalB[Axis]   = Source.VertexNormal[1][Axis];
                Destination.VertexNormalC[Axis]   = Source.VertexNormal[2][Axis];
                Destination.BoundaryMinimum[Axis] = Source.BoundaryMinimum[Axis];
                Destination.BoundaryMaximum[Axis] = Source.BoundaryMaximum[Axis];
            }
        }
        OutElements.resize(Volume.Elements.size());
        for (size_t ElementIndex = 0; ElementIndex < Volume.Elements.size(); ++ElementIndex)
        {
            const RayBoundaryElement& Source = Volume.Elements[ElementIndex];
            GpuRayElement& Destination = OutElements[ElementIndex];
            std::memset(&Destination, 0, sizeof(Destination));
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Destination.BoundaryMinimum[Axis] = Source.BoundaryMinimum[Axis];
                Destination.BoundaryMaximum[Axis] = Source.BoundaryMaximum[Axis];
            }
            Destination.FirstTriangle = Source.FirstTriangle;
            Destination.TriangleCount = Source.TriangleCount;
            Destination.LeftDownLink  = Source.LeftDownLink;
            Destination.RightDownLink = Source.RightDownLink;
        }
        OutOrdered = Volume.OrderedIndices;
    }

    // 📝 Fill the SurfacePush from the parameters + identity + bounds. The occlusion / curvature / position controls all live here so a
    //    single push block serves every surface pass (fields a pass does not read are ignored by its shader).
    void FillSurfacePush(uint32_t                     Edge,
                         SurfaceMapIdentity           Identity,
                         uint32_t                     TriangleCount,
                         uint32_t                     ElementCount,
                         const SurfaceBakeParameters& Parameters,
                         const float                  BoundsMinimum[3],
                         const float                  BoundsMaximum[3],
                         SurfacePush&                 Push)
    {
        Push.Edge               = Edge;
        Push.Identity           = (uint32_t)Identity;
        Push.TriangleCount      = TriangleCount;
        Push.ElementCount       = ElementCount;
        Push.SecondaryRays      = Parameters.SecondaryRays;
        Push.SpreadAngle        = (float)Parameters.SpreadAngle;
        Push.OcclusionMaximum   = Parameters.OcclusionMaximum;
        Push.SamplingRadius     = Parameters.SamplingRadius;
        Push.Contrast           = Parameters.Contrast;
        Push.PositionNormalize  = Parameters.PositionNormalize ? 1u : 0u;
        Push.AutoTonemapEnabled = Parameters.AutoTonemapEnabled ? 1u : 0u;

        // 📝 The procedural maps reuse the existing scalar slots by semantic mapping so the std430 push block stays byte-identical (no new
        //    fields, no re-pad). Each procedural shader reads the SAME slots this packs — keep the two in lock-step.
        //    • Bevel (SurfaceBevel pass): SecondaryRays ← BevelSamples, OcclusionMaximum ← BevelRadius (the cone-trace reach).
        //    • Dust (SurfaceDust pass): SamplingRadius ← DustUpBias, Contrast ← DustFalloff, SpreadAngle ← DustPower.
        //    • Cavity / Convexity / Concavity / Pointiness (SurfaceCurvature pass): Identity selects the remap; PositionNormalize ← InvertMask.
        if (Identity == SurfaceMapIdentity::Bevel)
        {
            Push.SecondaryRays    = Parameters.BevelSamples > 0 ? Parameters.BevelSamples : 16;
            Push.OcclusionMaximum = Parameters.BevelRadius > 1e-5f ? Parameters.BevelRadius : 0.05f;
        }
        else if (Identity == SurfaceMapIdentity::Dust)
        {
            Push.SamplingRadius    = Parameters.DustUpBias;
            Push.Contrast          = Parameters.DustFalloff > 1e-4f ? Parameters.DustFalloff : 1e-4f;
            Push.SpreadAngle       = Parameters.DustPower > 1e-4f ? Parameters.DustPower : 1.0f;
            Push.PositionNormalize = Parameters.InvertMaskEnabled ? 1u : 0u;
        }
        else if (Identity == SurfaceMapIdentity::Cavity   || Identity == SurfaceMapIdentity::Convexity ||
                 Identity == SurfaceMapIdentity::Concavity || Identity == SurfaceMapIdentity::Pointiness)
        {
            Push.PositionNormalize = Parameters.InvertMaskEnabled ? 1u : 0u;
        }
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Push.BoundsMinimum[Axis] = BoundsMinimum[Axis];
            Push.BoundsMaximum[Axis] = BoundsMaximum[Axis];
        }
        Push.BoundsMinimum[3] = 0.0f;
        Push.BoundsMaximum[3] = 0.0f;

        // 📝 Ambient occlusion packs its three extra controls into the otherwise-zeroed vec4 tail lanes so the std430 push block keeps
        //    its exact size (existing passes never read these lanes): BoundsMinimum.w ← OcclusionMinimum (near-range floor), and the two
        //    toggles into BoundsMaximum.w (self-occlusion) + a bit spare. SecondaryRays / SpreadAngle / OcclusionMaximum are already the
        //    shared trace slots the AO shader reads directly — no remap needed. Ground-plane min-Z (Z-up) arrives via BoundsMinimum.z (unused
        //    otherwise for AO), and the ground toggle is folded into BoundsMaximum.w alongside self-occlusion as two packed flag bits.
        if (Identity == SurfaceMapIdentity::AmbientOcclusion)
        {
            Push.BoundsMinimum[3] = Parameters.OcclusionMinimum;
            const uint32_t SelfBit   = Parameters.SelfOcclusionEnabled ? 1u : 0u;
            const uint32_t GroundBit = Parameters.GroundPlaneEnabled   ? 2u : 0u;
            // Pack the two flag bits as a small float payload the shader decodes with a >= comparison (0,1,2,3).
            Push.BoundsMaximum[3] = (float)(SelfBit | GroundBit);
            // BoundsMinimum.z carries the ground-plane world-Z floor (Z-up); BoundsMinimum.x/.y stay 0 (AO reads neither).
            Push.BoundsMinimum[2] = BoundsMinimum[2];
        }
    }

    // Resolve the covered-texel world-position bounds the position map normalizes into (matches EncodePositionMap's first pass, including
    // the sphere-mode uniform half-extent). Leaves a unit box when nothing is covered.
    void ResolvePositionBounds(const SurfaceSampleField&    Field,
                               const SurfaceBakeParameters& Parameters,
                               float                        OutMinimum[3],
                               float                        OutMaximum[3])
    {
        bool  BoundsPresent = false;
        float Minimum[3] = { 0.0f, 0.0f, 0.0f };
        float Maximum[3] = { 0.0f, 0.0f, 0.0f };
        for (const TexelSample& Texel : Field.Texels)
        {
            if (!Texel.CoverageMask) continue;
            if (!BoundsPresent)
            {
                for (int Axis = 0; Axis < 3; ++Axis) { Minimum[Axis] = Maximum[Axis] = Texel.WorldPosition[Axis]; }
                BoundsPresent = true;
                continue;
            }
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                if (Texel.WorldPosition[Axis] < Minimum[Axis]) Minimum[Axis] = Texel.WorldPosition[Axis];
                if (Texel.WorldPosition[Axis] > Maximum[Axis]) Maximum[Axis] = Texel.WorldPosition[Axis];
            }
        }
        if (!BoundsPresent)
        {
            for (int Axis = 0; Axis < 3; ++Axis) { OutMinimum[Axis] = 0.0f; OutMaximum[Axis] = 1.0f; }
            return;
        }
        // Carry the centre ± extent through so sphere mode can equalize the half-extent; the shader recomputes centre / extent from these.
        if (Parameters.PositionNormalize)
        {
            float Radius = 0.0f;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const float Extent = 0.5f * (Maximum[Axis] - Minimum[Axis]);
                if (Extent > Radius) Radius = Extent;
            }
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const float Centre = 0.5f * (Minimum[Axis] + Maximum[Axis]);
                Minimum[Axis] = Centre - Radius;
                Maximum[Axis] = Centre + Radius;
            }
        }
        for (int Axis = 0; Axis < 3; ++Axis) { OutMinimum[Axis] = Minimum[Axis]; OutMaximum[Axis] = Maximum[Axis]; }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  SURFACE COMPUTE DISPATCH
    //--------------------------------------------------------------------------------------------------------------------

    // Encode one surface map: build the texel buffer (+ ray-volume buffers for the traced pass) + the R8G8B8A8 output buffer, dispatch the
    // selected encoder one invocation per texel (local_size 8×8), and read the pixels back into Out. Matches the CPU encoder byte-for-byte.
    bool EvaluateSurfaceMapInternal(GpuBakeContextImplementation& Implementation,
                                    const SurfaceSampleField&     Field,
                                    const TriangleRayVolume&      Volume,
                                    const SurfaceBakeParameters&  Parameters,
                                    SurfaceMapIdentity            Identity,
                                    BakedImageBuffer&             Out)
    {
        const uint32_t Edge = Field.Edge;
        if (Edge == 0 || Field.Texels.size() != (size_t)Edge * (size_t)Edge)
        { ReportDispatch("surface: edge/texel-count gate failed"); return false; }

        const BakePassIdentity PassId = SurfacePassFor(Identity);
        ComputePass& Pass = Implementation.Passes[(uint32_t)PassId];
        if (!Pass.SpirVLoaded) ReportDispatch("surface: pass SPIR-V not loaded (shader missing at init)");
        if (!BuildComputePass(Implementation, Pass, (uint32_t)sizeof(SurfacePush)))
        { ReportDispatch("surface: BuildComputePass failed (pipeline creation)"); return false; }

        const bool TracesRays = SurfaceTracesRays(Identity);
        if (TracesRays && Volume.Triangles.empty())
        { ReportDispatch("surface: traced pass but ray volume empty"); return false; }

        std::vector<GpuTexelSample> Texels;
        PrepareTexelSamples(Field, Texels);

        float BoundsMinimum[3] = { 0.0f, 0.0f, 0.0f };
        float BoundsMaximum[3] = { 1.0f, 1.0f, 1.0f };
        if (Identity == SurfaceMapIdentity::Position)
            ResolvePositionBounds(Field, Parameters, BoundsMinimum, BoundsMaximum);
        // 📝 AO with a virtual floor needs the specimen's lowest covered-texel world Z (Z-up); resolve it into BoundsMinimum[2] so
        //    FillSurfacePush can forward it (the GPU AO shader reads the floor plane from that lane, mirroring the CPU encoder's GroundZ sweep).
        if (Identity == SurfaceMapIdentity::AmbientOcclusion && Parameters.GroundPlaneEnabled)
        {
            bool GroundPresent = false;
            for (const TexelSample& Texel : Field.Texels)
            {
                if (!Texel.CoverageMask) continue;
                if (!GroundPresent || Texel.WorldPosition[2] < BoundsMinimum[2])
                { BoundsMinimum[2] = Texel.WorldPosition[2]; GroundPresent = true; }
            }
        }

        // The texel buffer + the output pixel buffer are always present; the traced pass adds three ray-volume buffers between them.
        HostBuffer TexelBuffer, TriangleBuffer, ElementBuffer, OrderedBuffer, PixelBuffer;
        const VkDeviceSize TexelBytes = (VkDeviceSize)Texels.size() * sizeof(GpuTexelSample);
        const VkDeviceSize PixelBytes = (VkDeviceSize)Edge * (VkDeviceSize)Edge * 4;

        std::vector<GpuRayTriangle> RayTriangles;
        std::vector<GpuRayElement>  RayElements;
        std::vector<int32_t>        RayOrdered;
        if (TracesRays) PrepareRayVolume(Volume, RayTriangles, RayElements, RayOrdered);
        const VkDeviceSize TriangleBytes = (VkDeviceSize)RayTriangles.size() * sizeof(GpuRayTriangle);
        const VkDeviceSize ElementBytes  = (VkDeviceSize)RayElements.size()  * sizeof(GpuRayElement);
        const VkDeviceSize OrderedBytes   = (VkDeviceSize)RayOrdered.size()  * sizeof(int32_t);

        auto Cleanup = [&]()
        {
            DestroyHostBuffer(Implementation.Device, TexelBuffer);
            DestroyHostBuffer(Implementation.Device, TriangleBuffer);
            DestroyHostBuffer(Implementation.Device, ElementBuffer);
            DestroyHostBuffer(Implementation.Device, OrderedBuffer);
            DestroyHostBuffer(Implementation.Device, PixelBuffer);
        };

        bool BuildEnabled = ConstructHostBuffer(Implementation, TexelBytes, TexelBuffer) &&
                            ConstructHostBuffer(Implementation, PixelBytes, PixelBuffer);
        if (TracesRays)
            BuildEnabled = BuildEnabled &&
                           ConstructHostBuffer(Implementation, TriangleBytes, TriangleBuffer) &&
                           ConstructHostBuffer(Implementation, ElementBytes,  ElementBuffer)  &&
                           ConstructHostBuffer(Implementation, OrderedBytes,  OrderedBuffer);
        if (!BuildEnabled) { ReportDispatch("surface: host buffer allocation failed"); Cleanup(); return false; }

        std::memcpy(TexelBuffer.Mapped, Texels.data(), (size_t)TexelBytes);
        if (TracesRays)
        {
            if (TriangleBytes > 0) std::memcpy(TriangleBuffer.Mapped, RayTriangles.data(), (size_t)TriangleBytes);
            if (ElementBytes  > 0) std::memcpy(ElementBuffer.Mapped,  RayElements.data(),  (size_t)ElementBytes);
            if (OrderedBytes  > 0) std::memcpy(OrderedBuffer.Mapped,  RayOrdered.data(),   (size_t)OrderedBytes);
        }

        // Binding order matches PassBufferCount: [texels]([triangles][tree][ordered])[outPixels] — output always last.
        const HostBuffer* SurfaceBuffers[2] = { &TexelBuffer, &PixelBuffer };
        const HostBuffer* OcclusionBuffers[5] = { &TexelBuffer, &TriangleBuffer, &ElementBuffer, &OrderedBuffer, &PixelBuffer };
        const HostBuffer* const* Buffers = TracesRays ? OcclusionBuffers : SurfaceBuffers;

        SurfacePush Push;
        FillSurfacePush(Edge, Identity, (uint32_t)RayTriangles.size(), (uint32_t)RayElements.size(),
                        Parameters, BoundsMinimum, BoundsMaximum, Push);

        VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
        const bool DispatchEnabled = BindPassDescriptors(Implementation, Pass, Buffers, DescriptorSet) &&
                                     DispatchComputePass(Implementation, Pass, DescriptorSet, &Push, (uint32_t)sizeof(Push),
                                                         DispatchGroups(Edge, 8), DispatchGroups(Edge, 8), 1);
        if (DispatchEnabled)
        {
            Out.Edge = Edge;
            Out.Pixels.resize((size_t)PixelBytes);
            std::memcpy(Out.Pixels.data(), PixelBuffer.Mapped, (size_t)PixelBytes);
        }
        else ReportDispatch("surface: BindPassDescriptors/DispatchComputePass failed");

        Cleanup();
        return DispatchEnabled;
    }
}

// 📝 The header forward-declares GpuBakeAsyncRun; its whole body is the anon-namespace impl above, held opaquely so nothing outside this
//    TU sees the Vulkan handles. Mirrors GpuBakeContext::OpaqueImplementation. Deleted by ResolveDistanceExactGpu / DiscardDistanceExactGpu.
struct GpuBakeAsyncRun
{
    GpuBakeAsyncRunImplementation Implementation;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeGpuBakeContext(GpuBakeContext& Context, VulkanHost& Device, const char* ShaderDirectory)
{
    Context.OpaqueImplementation = nullptr;
    Context.InitializeEnabled    = false;
    if (!Device.Device || !ShaderDirectory) return false;

    GpuBakeContextImplementation* Implementation = new GpuBakeContextImplementation();
    Implementation->PhysicalDevice   = Device.PhysicalDevice;
    Implementation->Device           = Device.Device;
    // 📝 Submit the bake compute to the graphics queue: VulkanHost exposes no separate compute queue, and a graphics-capable family is
    //    always compute-capable per the Vulkan spec, so the single graphics queue records + runs the dispatch. The command pool below
    //    builds from GraphicsQueueFamily, so it lands on the matching family automatically. The compute is buffers-only (host-coherent +
    //    poll fence), so no image queue-family-ownership transfer is needed even when a later copy runs on the same family.
    Implementation->Queue                    = Device.GraphicsQueue;
    Implementation->QueueFamilyIndex         = Device.GraphicsQueueFamily;
    Implementation->GraphicsQueueFamilyIndex = Device.GraphicsQueueFamily;
    Context.OpaqueImplementation = Implementation;

    // Transient command pool for the one-shot dispatch command buffers.
    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInformation.queueFamilyIndex = Implementation->QueueFamilyIndex;
    if (vkCreateCommandPool(Implementation->Device, &PoolInformation, nullptr, &Implementation->CommandPool) != VK_SUCCESS)
    {
        ReportDispatch("failed to create transient command pool");
        FinalizeGpuBakeContext(Context, Device);
        return false;
    }

    // 📝 A descriptor pool large enough for the multi-pass jump-flood run (seed + several flood passes + resolve, each its own set). The
    //    pool is reset between Evaluate* calls so its capacity bounds per-bake sets, not lifetime sets.
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 256;
    VkDescriptorPoolCreateInfo DescriptorPoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    DescriptorPoolInformation.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    DescriptorPoolInformation.maxSets       = 64;
    DescriptorPoolInformation.poolSizeCount = 1;
    DescriptorPoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Implementation->Device, &DescriptorPoolInformation, nullptr, &Implementation->DescriptorPool) != VK_SUCCESS)
    {
        ReportDispatch("failed to create descriptor pool");
        FinalizeGpuBakeContext(Context, Device);
        return false;
    }

    // Load each pass's SPIR-V off disk. A missing shader leaves that pass's SpirVLoaded false — that pass declines at dispatch time, but
    // the context still initializes so other passes work. Set the per-pass buffer counts here too.
    for (uint32_t PassIndex = 0; PassIndex < PassTotal; ++PassIndex)
    {
        ComputePass& Pass = Implementation->Passes[PassIndex];
        Pass.BufferCount  = PassBufferCount((BakePassIdentity)PassIndex);
        const std::string Path = std::string(ShaderDirectory) + "/" + PassFileName((BakePassIdentity)PassIndex);
        Pass.SpirVLoaded = LoadFileBytes(Path.c_str(), Pass.SpirV);
        if (!Pass.SpirVLoaded)
            ReportDispatch((std::string("missing bake shader: ") + Path).c_str());
    }

    Context.InitializeEnabled = true;
    return true;
}

void FinalizeGpuBakeContext(GpuBakeContext& Context, VulkanHost& Device)
{
    (void)Device;
    if (!Context.OpaqueImplementation) return;
    GpuBakeContextImplementation* Implementation = static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);

    if (Implementation->Device) vkDeviceWaitIdle(Implementation->Device);
    VkDevice Dev = Implementation->Device;

    for (uint32_t PassIndex = 0; PassIndex < PassTotal; ++PassIndex)
    {
        ComputePass& Pass = Implementation->Passes[PassIndex];
        if (Pass.Pipeline)            vkDestroyPipeline(Dev, Pass.Pipeline, nullptr);
        if (Pass.PipelineLayout)      vkDestroyPipelineLayout(Dev, Pass.PipelineLayout, nullptr);
        if (Pass.Module)              vkDestroyShaderModule(Dev, Pass.Module, nullptr);
        if (Pass.DescriptorSetLayout) vkDestroyDescriptorSetLayout(Dev, Pass.DescriptorSetLayout, nullptr);
    }
    if (Implementation->DescriptorPool) vkDestroyDescriptorPool(Dev, Implementation->DescriptorPool, nullptr);
    if (Implementation->CommandPool)    vkDestroyCommandPool(Dev, Implementation->CommandPool, nullptr);

    delete Implementation;
    Context.OpaqueImplementation = nullptr;
    Context.InitializeEnabled    = false;
}

bool EvaluateDistanceVolumeGpu(GpuBakeContext&           Context,
                               VulkanHost&               Device,
                               const RenderVertexStream& Stream,
                               uint32_t                  Resolution,
                               DistanceBakeAlgorithm     Algorithm,
                               SignedDistanceVolume&     OutVolume)
{
    (void)Device;
    if (!Context.InitializeEnabled || !Context.OpaqueImplementation) return false;
    if (Resolution == 0 || Resolution > 256) return false;
    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);

    std::vector<GpuBakeTriangle> Triangles;
    BakeBounds Bounds;
    if (!PrepareBakeTriangles(Stream, Resolution, Triangles, Bounds)) return false;

    std::vector<int16_t> Voxels(VoxelTotal(Bounds.Extent), 0);

    vkResetDescriptorPool(Implementation.Device, Implementation.DescriptorPool, 0);

    if (Algorithm == DistanceBakeAlgorithm::ExactBoundingVolume)
    {
        if (!EvaluateDistanceExactInternal(Implementation, Triangles, Bounds, Bounds.Extent, Voxels)) return false;
    }
    else if (Algorithm == DistanceBakeAlgorithm::CoarseJumpNarrowExact)
    {
        // Whole-grid jump-flood, then exact refine only inside the auto-selected narrow band (BandRadius 0 → ~4 voxel diagonals).
        if (!EvaluateDistanceFloodInternal(Implementation, Triangles, Bounds, Bounds.Extent, Voxels, true, 0.0f)) return false;
    }
    else
    {
        if (!EvaluateDistanceFloodInternal(Implementation, Triangles, Bounds, Bounds.Extent, Voxels)) return false;
    }

    PublishDistanceVolume(Voxels, Bounds, OutVolume);
    return true;
}

bool EvaluateDistanceRegionGpu(GpuBakeContext&           Context,
                               VulkanHost&               Device,
                               SignedDistanceVolume&     OutVolume,
                               const RenderVertexStream& Stream,
                               uint32_t                  MinX,
                               uint32_t                  MinY,
                               uint32_t                  MinZ,
                               uint32_t                  MaxX,
                               uint32_t                  MaxY,
                               uint32_t                  MaxZ)
{
    (void)Device;
    if (!Context.InitializeEnabled || !Context.OpaqueImplementation) return false;
    if (!ExtentPopulated(OutVolume.Resolution) || OutVolume.Voxels.empty()) return false;
    if (MinX > MaxX || MinY > MaxY || MinZ > MaxZ) return false;
    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);

    // 📝 The region bake reuses OutVolume's FIXED bounds / per-axis extent / decode scale (never re-derives). 🔍 The exact per-voxel shader
    //    is now dispatched BOUNDED to the dirty sub-box only (BoxMinimum + BoxExtent) and writes straight into OutVolume.Voxels — no whole-grid
    //    scratch pass, no copy-back. Only the escaped/moved voxels recompute; every untouched voxel keeps its retained value.
    const DistanceExtent Extent = OutVolume.Resolution;   // 🔍 the existing grid's extent drives dispatch + indexing (not the re-derived one)

    // The triangle list is all PrepareBakeTriangles is used for on this path; the budget (longest axis) keeps ConstructTriangles happy but
    // its ScratchBounds / re-derived extent are discarded — the Fixed bounds below (from the volume) drive the dispatch.
    std::vector<GpuBakeTriangle> Triangles;
    BakeBounds ScratchBounds;
    const uint32_t Budget = std::max({ Extent.X, Extent.Y, Extent.Z });
    if (!PrepareBakeTriangles(Stream, Budget, Triangles, ScratchBounds)) return false;

    BakeBounds Fixed;
    Fixed.BoundaryMinimum[0] = (float)OutVolume.BoundaryMinimum.XCoord;
    Fixed.BoundaryMinimum[1] = (float)OutVolume.BoundaryMinimum.YCoord;
    Fixed.BoundaryMinimum[2] = (float)OutVolume.BoundaryMinimum.ZCoord;
    Fixed.BoundaryMaximum[0] = (float)OutVolume.BoundaryMaximum.XCoord;
    Fixed.BoundaryMaximum[1] = (float)OutVolume.BoundaryMaximum.YCoord;
    Fixed.BoundaryMaximum[2] = (float)OutVolume.BoundaryMaximum.ZCoord;
    for (int Axis = 0; Axis < 3; ++Axis)
        Fixed.VoxelSize[Axis] = (Fixed.BoundaryMaximum[Axis] - Fixed.BoundaryMinimum[Axis]) / (float)std::max(1u, ExtentAxis(Extent, Axis));
    Fixed.MaximumDistance = OutVolume.MaximumDistance;
    Fixed.Extent          = Extent;

    // Inclusive dirty box → half-open [Minimum, Minimum+Extent) in absolute voxel coords, clamped per axis to the grid.
    const uint32_t ClampMaxX = MaxX < Extent.X ? MaxX : Extent.X - 1;
    const uint32_t ClampMaxY = MaxY < Extent.Y ? MaxY : Extent.Y - 1;
    const uint32_t ClampMaxZ = MaxZ < Extent.Z ? MaxZ : Extent.Z - 1;
    const uint32_t BoxMinimum[3] = { MinX, MinY, MinZ };
    const uint32_t BoxExtent[3]  = { ClampMaxX - MinX + 1u, ClampMaxY - MinY + 1u, ClampMaxZ - MinZ + 1u };

    // 🔍 Rebake trace — the bounded-region win. Compare BoxVoxels against the full grid so the dispatch reduction is provable in the log.
    const uint64_t BoxVoxels  = (uint64_t)BoxExtent[0] * BoxExtent[1] * BoxExtent[2];
    const uint64_t GridVoxels = (uint64_t)VoxelTotal(Extent);
    fprintf(stderr, "[bake] REGION box=[%u %u %u]->[%u %u %u] extent=%ux%ux%u voxels=%llu/%llu (%.1f%%)\n",
            MinX, MinY, MinZ, ClampMaxX, ClampMaxY, ClampMaxZ, BoxExtent[0], BoxExtent[1], BoxExtent[2],
            (unsigned long long)BoxVoxels, (unsigned long long)GridVoxels,
            GridVoxels ? 100.0 * (double)BoxVoxels / (double)GridVoxels : 0.0);

    vkResetDescriptorPool(Implementation.Device, Implementation.DescriptorPool, 0);
    if (!EvaluateDistanceExactInternal(Implementation, Triangles, Fixed, Extent, OutVolume.Voxels, BoxMinimum, BoxExtent)) return false;
    return true;
}

GpuBakeAsyncRun* BeginDistanceExactGpu(GpuBakeContext&           Context,
                                       VulkanHost&               Device,
                                       const RenderVertexStream& Stream,
                                       SignedDistanceVolume&     OutVolume,
                                       bool                      RegionRun,
                                       int                       NarrowMode,
                                       uint32_t                  MinX,
                                       uint32_t                  MinY,
                                       uint32_t                  MinZ,
                                       uint32_t                  MaxX,
                                       uint32_t                  MaxY,
                                       uint32_t                  MaxZ)
{
    (void)Device;
    if (!Context.InitializeEnabled || !Context.OpaqueImplementation) return nullptr;
    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);

    // 🔴 The packed store is whole-grid only (the shader packs over ABSOLUTE voxel index; a region on an odd start voxel would share a
    //    word with an out-of-box voxel). A region run therefore always degrades to CPU narrow (mode 0), logged by the caller.
    if (RegionRun) NarrowMode = 0;

    // Resolve the triangle list + bounds. A region run reuses OutVolume's FIXED bounds / resolution (never re-derives); a whole-grid run
    // derives fresh bounds from the stream, sizes OutVolume.Voxels here, and publishes the bounds in Resolve. Fail early → caller falls back.
    std::unique_ptr<GpuBakeAsyncRun> Run(new GpuBakeAsyncRun());
    GpuBakeAsyncRunImplementation& AsyncRun = Run->Implementation;
    AsyncRun.RegionRun  = RegionRun;
    AsyncRun.NarrowMode = NarrowMode;

    std::vector<GpuBakeTriangle> Triangles;
    if (RegionRun)
    {
        if (!ExtentPopulated(OutVolume.Resolution) || OutVolume.Voxels.empty()) return nullptr;
        if (MinX > MaxX || MinY > MaxY || MinZ > MaxZ) return nullptr;
        const DistanceExtent Extent = OutVolume.Resolution;   // reuse the existing grid's per-axis extent (never re-derive on a region run)
        BakeBounds ScratchBounds;
        const uint32_t Budget = std::max({ Extent.X, Extent.Y, Extent.Z });
        if (!PrepareBakeTriangles(Stream, Budget, Triangles, ScratchBounds)) return nullptr;

        AsyncRun.Extent = Extent;
        AsyncRun.Bounds.BoundaryMinimum[0] = (float)OutVolume.BoundaryMinimum.XCoord;
        AsyncRun.Bounds.BoundaryMinimum[1] = (float)OutVolume.BoundaryMinimum.YCoord;
        AsyncRun.Bounds.BoundaryMinimum[2] = (float)OutVolume.BoundaryMinimum.ZCoord;
        AsyncRun.Bounds.BoundaryMaximum[0] = (float)OutVolume.BoundaryMaximum.XCoord;
        AsyncRun.Bounds.BoundaryMaximum[1] = (float)OutVolume.BoundaryMaximum.YCoord;
        AsyncRun.Bounds.BoundaryMaximum[2] = (float)OutVolume.BoundaryMaximum.ZCoord;
        for (int Axis = 0; Axis < 3; ++Axis)
            AsyncRun.Bounds.VoxelSize[Axis] = (AsyncRun.Bounds.BoundaryMaximum[Axis] - AsyncRun.Bounds.BoundaryMinimum[Axis]) / (float)std::max(1u, ExtentAxis(Extent, Axis));
        AsyncRun.Bounds.MaximumDistance = OutVolume.MaximumDistance;
        AsyncRun.Bounds.Extent          = Extent;

        const uint32_t ClampMaxX = MaxX < Extent.X ? MaxX : Extent.X - 1;
        const uint32_t ClampMaxY = MaxY < Extent.Y ? MaxY : Extent.Y - 1;
        const uint32_t ClampMaxZ = MaxZ < Extent.Z ? MaxZ : Extent.Z - 1;
        AsyncRun.BoxMinimum[0] = MinX; AsyncRun.BoxMinimum[1] = MinY; AsyncRun.BoxMinimum[2] = MinZ;
        AsyncRun.BoxExtent[0]  = ClampMaxX - MinX + 1u;
        AsyncRun.BoxExtent[1]  = ClampMaxY - MinY + 1u;
        AsyncRun.BoxExtent[2]  = ClampMaxZ - MinZ + 1u;
    }
    else
    {
        // 🔴 On a whole-grid run OutVolume.Resolution is the RESOLUTION BUDGET (longest-axis voxel count) the caller seeds; the per-axis
        //    extent is derived fresh from the stream by PrepareBakeTriangles. A budget of 0 or > 256 is out of range.
        const uint32_t Budget = std::max({ OutVolume.Resolution.X, OutVolume.Resolution.Y, OutVolume.Resolution.Z });
        if (Budget == 0 || Budget > 256) return nullptr;
        if (!PrepareBakeTriangles(Stream, Budget, Triangles, AsyncRun.Bounds)) return nullptr;
        AsyncRun.Extent = AsyncRun.Bounds.Extent;
        AsyncRun.BoxMinimum[0] = AsyncRun.BoxMinimum[1] = AsyncRun.BoxMinimum[2] = 0u;
        AsyncRun.BoxExtent[0]  = AsyncRun.Extent.X;
        AsyncRun.BoxExtent[1]  = AsyncRun.Extent.Y;
        AsyncRun.BoxExtent[2]  = AsyncRun.Extent.Z;
        // Whole-grid run owns its own output; size the voxel store so the shader's absolute writes land and Resolve narrows the whole grid.
        OutVolume.Voxels.assign(VoxelTotal(AsyncRun.Extent), (int16_t)0);
    }

    ComputePass& Pass = Implementation.Passes[(uint32_t)BakePassIdentity::DistanceExact];
    if (!BuildComputePass(Implementation, Pass, (uint32_t)sizeof(DistanceExactPush))) return nullptr;

    const uint32_t VoxelCount = (uint32_t)VoxelTotal(AsyncRun.Extent);

    // 🔴 No vkResetDescriptorPool here — a previous async run may still hold a live descriptor set on this shared pool. The caller's
    //    one-in-flight gate guarantees no other bake is pending; the pool is sized for many sets, so allocating one more is safe.
    const VkDeviceSize TriangleBytes = (VkDeviceSize)Triangles.size() * sizeof(GpuBakeTriangle);
    const VkDeviceSize VoxelBytes    = (VkDeviceSize)OutVolume.Voxels.size() * sizeof(int32_t);

    auto Cleanup = [&]()
    {
        DestroyHostBuffer(Implementation.Device, AsyncRun.TriangleBuffer);
        DestroyHostBuffer(Implementation.Device, AsyncRun.VoxelBuffer);
        DestroyDeviceBuffer(Implementation.Device, AsyncRun.VoxelPackedBuffer);
    };

    // Mode 2 needs no host-visible int32 store (the exact shader packs straight into the device buffer), but binding 1 is still declared,
    // so allocate a tiny placeholder int32 buffer to populate it. Modes 0/1 allocate the full-grid int32 store the exact shader writes.
    const VkDeviceSize IntBufferBytes = (NarrowMode == 2) ? (VkDeviceSize)sizeof(int32_t) : VoxelBytes;
    if (!ConstructHostBuffer(Implementation, TriangleBytes,  AsyncRun.TriangleBuffer) ||
        !ConstructHostBuffer(Implementation, IntBufferBytes, AsyncRun.VoxelBuffer))
    {
        Cleanup();
        return nullptr;
    }
    std::memcpy(AsyncRun.TriangleBuffer.Mapped, Triangles.data(), (size_t)TriangleBytes);

    // Modes 1/2 pack into a device-local buffer of ceil(N/2) uint words (two int16 voxels per word). Mode 0 leaves it VK_NULL_HANDLE.
    const VkDeviceSize PackedBytes = (VkDeviceSize)((VoxelCount + 1u) / 2u) * sizeof(uint32_t);
    if (NarrowMode == 1 || NarrowMode == 2)
    {
        if (!ConstructDeviceBuffer(Implementation, PackedBytes, AsyncRun.VoxelPackedBuffer))
        {
            Cleanup();
            return nullptr;
        }
    }

    VkDescriptorSet ExactSet = VK_NULL_HANDLE;
    DistanceExactPush Push;
    FillExactPush(AsyncRun.Bounds, AsyncRun.Extent, (uint32_t)Triangles.size(), AsyncRun.BoxMinimum, AsyncRun.BoxExtent,
                  NarrowMode == 2 ? 1u : 0u, Push);

    // Bind the exact pass's three storage buffers: [triangles][int32 voxels][packed voxels]. Mode 0 (no packed buffer) aliases the int32
    // buffer into binding 2 so every declared binding is populated (the shader writes only binding 1 under PackedMode 0).
    const VkBuffer PackedForExact = AsyncRun.VoxelPackedBuffer.Buffer != VK_NULL_HANDLE ? AsyncRun.VoxelPackedBuffer.Buffer
                                                                                        : AsyncRun.VoxelBuffer.Buffer;
    const VkBuffer ExactBuffers[3] = { AsyncRun.TriangleBuffer.Buffer, AsyncRun.VoxelBuffer.Buffer, PackedForExact };
    if (!BindPassDescriptorsRaw(Implementation, Pass, ExactBuffers, ExactSet)) { Cleanup(); return nullptr; }

    bool DispatchEnabled = false;
    if (NarrowMode == 0)
    {
        // CPU-narrow path: single exact dispatch, host reads the int32 buffer in Resolve. Reuse the plain single-dispatch begin.
        DispatchEnabled = DispatchComputeBegin(Implementation, Pass, ExactSet, &Push, (uint32_t)sizeof(Push),
                                               DispatchGroups(AsyncRun.BoxExtent[0], 4), DispatchGroups(AsyncRun.BoxExtent[1], 4),
                                               DispatchGroups(AsyncRun.BoxExtent[2], 4), AsyncRun.Ticket);
    }
    else if (NarrowMode == 2)
    {
        // Exact shader packs int16 directly into the device buffer (PackedMode 1); no narrow pass.
        DistanceNarrowPush Unused = {};
        DispatchEnabled = DispatchExactPackedBegin(Implementation, Pass, ExactSet, Push, nullptr, VK_NULL_HANDLE, Unused,
                                                   AsyncRun.VoxelPackedBuffer.Buffer, PackedBytes,
                                                   DispatchGroups(AsyncRun.BoxExtent[0], 4), DispatchGroups(AsyncRun.BoxExtent[1], 4),
                                                   DispatchGroups(AsyncRun.BoxExtent[2], 4), VoxelCount, /*ExactPacksDirect*/ true, AsyncRun.Ticket);
    }
    else   // NarrowMode == 1
    {
        // Exact shader writes int32 (PackedMode 0), then the DistanceNarrow pass packs int32 → int16 into the device buffer.
        ComputePass& NarrowPass = Implementation.Passes[(uint32_t)BakePassIdentity::DistanceNarrow];
        if (!BuildComputePass(Implementation, NarrowPass, (uint32_t)sizeof(DistanceNarrowPush))) { Cleanup(); return nullptr; }
        const VkBuffer NarrowBuffers[2] = { AsyncRun.VoxelBuffer.Buffer, AsyncRun.VoxelPackedBuffer.Buffer };
        VkDescriptorSet NarrowSet = VK_NULL_HANDLE;
        if (!BindPassDescriptorsRaw(Implementation, NarrowPass, NarrowBuffers, NarrowSet)) { Cleanup(); return nullptr; }
        DistanceNarrowPush NarrowPush = {};
        NarrowPush.VoxelTotal = VoxelCount;
        DispatchEnabled = DispatchExactPackedBegin(Implementation, Pass, ExactSet, Push, &NarrowPass, NarrowSet, NarrowPush,
                                                   AsyncRun.VoxelPackedBuffer.Buffer, PackedBytes,
                                                   DispatchGroups(AsyncRun.BoxExtent[0], 4), DispatchGroups(AsyncRun.BoxExtent[1], 4),
                                                   DispatchGroups(AsyncRun.BoxExtent[2], 4), VoxelCount, /*ExactPacksDirect*/ false, AsyncRun.Ticket);
    }

    if (!DispatchEnabled) { Cleanup(); return nullptr; }
    return Run.release();
}

void* ResolvePackedBufferHandle(const GpuBakeAsyncRun* Run)
{
    if (!Run) return nullptr;
    return (void*)Run->Implementation.VoxelPackedBuffer.Buffer;
}

void FinalizePackedRunGpu(GpuBakeContext& Context, GpuBakeAsyncRun* Run)
{
    if (!Run) return;
    std::unique_ptr<GpuBakeAsyncRun> Owned(Run);
    if (!Context.InitializeEnabled || !Context.OpaqueImplementation) return;
    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);
    // The compute fence signaled (Poll) and the copy drained its transfer fence, so the packed buffer is no longer in use — free it flat.
    DestroyDeviceBuffer(Implementation.Device, Run->Implementation.VoxelPackedBuffer);
}

bool PollDistanceExactGpu(GpuBakeContext& Context, const GpuBakeAsyncRun* Run)
{
    if (!Run || !Context.InitializeEnabled || !Context.OpaqueImplementation) return false;
    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);
    return DispatchComputeResolved(Implementation, Run->Implementation.Ticket);
}

bool ResolveDistanceExactGpu(GpuBakeContext& Context, GpuBakeAsyncRun* Run, SignedDistanceVolume& OutVolume)
{
    if (!Run) return false;
    std::unique_ptr<GpuBakeAsyncRun> Owned(Run);   // always freed, success or not
    if (!Context.InitializeEnabled || !Context.OpaqueImplementation) return false;
    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);
    GpuBakeAsyncRunImplementation& AsyncRun = Run->Implementation;

    bool ResolveEnabled = AsyncRun.Ticket.DispatchRun;
    // Modes 1/2 narrowed on the GPU into the packed device buffer — the CPU never touches the voxels. Resolve only publishes the bounds /
    // resolution (whole-grid) so the volume metadata is correct; the packed buffer is copied to the resident image by CopyPackedBufferToResident,
    // and the run stays alive (freed by DiscardDistanceExactGpu) until that copy is recorded. There is no CPU narrow-back here.
    if (ResolveEnabled && AsyncRun.NarrowMode != 0)
    {
        if (!AsyncRun.RegionRun)
        {
            OutVolume.BoundaryMinimum = Vector3d{ AsyncRun.Bounds.BoundaryMinimum[0], AsyncRun.Bounds.BoundaryMinimum[1], AsyncRun.Bounds.BoundaryMinimum[2] };
            OutVolume.BoundaryMaximum = Vector3d{ AsyncRun.Bounds.BoundaryMaximum[0], AsyncRun.Bounds.BoundaryMaximum[1], AsyncRun.Bounds.BoundaryMaximum[2] };
            OutVolume.Resolution      = AsyncRun.Extent;
            OutVolume.MaximumDistance = AsyncRun.Bounds.MaximumDistance;
            // OutVolume.Voxels is left as-is (RAM residency): the packed GPU buffer is the source of truth uploaded to the image. The CPU
            // dense copy is not refreshed on the GPU-narrow path — the shadow samples the resident image, not OutVolume.Voxels.
        }
        DispatchComputeFinalize(Implementation, AsyncRun.Ticket);
        DestroyHostBuffer(Implementation.Device, AsyncRun.TriangleBuffer);
        DestroyHostBuffer(Implementation.Device, AsyncRun.VoxelBuffer);
        // 🔴 Keep VoxelPackedBuffer alive — the caller copies it to the image, then calls DiscardDistanceExactGpu to free it. Release
        //    ownership from the unique_ptr so the run (and its packed buffer) survives this return.
        Owned.release();
        return ResolveEnabled;
    }

    if (ResolveEnabled)
    {
        // 🔴 The mapped voxel buffer is host-visible WRITE-COMBINED memory: per-element reads off it are uncached and cost ~600-800ms to
        //    narrow a res-200+ grid on the render thread (this was the residual live-edit freeze). Bulk-copy in ONE memcpy per contiguous
        //    row (a single streaming read the hardware coalesces), then narrow int32→int16 from that cached RAM. The whole-grid path copies
        //    the entire buffer in one memcpy; the region path copies ONLY the dirty sub-box's rows — the readback then scales with the box,
        //    not the grid (a 0.6%-of-grid edit no longer pays the whole-grid WC read). Both narrow loops read Scratch, never Mapped.
        const DistanceExtent Extent = AsyncRun.Extent;
        const int32_t* Mapped       = static_cast<const int32_t*>(AsyncRun.VoxelBuffer.Mapped);
        if (!AsyncRun.RegionRun)
        {
            // Whole grid: one streaming memcpy of the full buffer, publish the derived bounds / extent / decode scale, narrow every voxel.
            const size_t VoxelCount = (size_t)(AsyncRun.VoxelBuffer.Capacity / sizeof(int32_t));
            std::vector<int32_t> Scratch(VoxelCount);
            std::memcpy(Scratch.data(), AsyncRun.VoxelBuffer.Mapped, (size_t)AsyncRun.VoxelBuffer.Capacity);
            const int32_t* Source = Scratch.data();
            OutVolume.BoundaryMinimum = Vector3d{ AsyncRun.Bounds.BoundaryMinimum[0], AsyncRun.Bounds.BoundaryMinimum[1], AsyncRun.Bounds.BoundaryMinimum[2] };
            OutVolume.BoundaryMaximum = Vector3d{ AsyncRun.Bounds.BoundaryMaximum[0], AsyncRun.Bounds.BoundaryMaximum[1], AsyncRun.Bounds.BoundaryMaximum[2] };
            OutVolume.Resolution      = Extent;
            OutVolume.MaximumDistance = AsyncRun.Bounds.MaximumDistance;
            if (OutVolume.Voxels.size() != VoxelTotal(Extent))
                OutVolume.Voxels.assign(VoxelTotal(Extent), (int16_t)0);
            for (size_t Index = 0; Index < OutVolume.Voxels.size(); ++Index)
                OutVolume.Voxels[Index] = (int16_t)Source[Index];
        }
        else
        {
            // Region: OutVolume is already the seeded volume; read + narrow only the computed sub-box, leaving the rest intact. The box is
            // contiguous in X, so each (Z,Y) span is one memcpy off write-combined memory into a small row buffer, then narrowed in place —
            // the WC readback touches BoxExtent voxels, never the whole grid.
            const uint32_t ClampMaxX = std::min(AsyncRun.BoxMinimum[0] + AsyncRun.BoxExtent[0], Extent.X);
            const uint32_t ClampMaxY = std::min(AsyncRun.BoxMinimum[1] + AsyncRun.BoxExtent[1], Extent.Y);
            const uint32_t ClampMaxZ = std::min(AsyncRun.BoxMinimum[2] + AsyncRun.BoxExtent[2], Extent.Z);
            const uint32_t RowStart  = AsyncRun.BoxMinimum[0];
            const uint32_t RowCount  = ClampMaxX > RowStart ? ClampMaxX - RowStart : 0u;
            std::vector<int32_t> Row(RowCount);
            for (uint32_t Z = AsyncRun.BoxMinimum[2]; Z < ClampMaxZ; ++Z)
                for (uint32_t Y = AsyncRun.BoxMinimum[1]; Y < ClampMaxY; ++Y)
                {
                    const size_t RowBase = VoxelIndex(Extent, RowStart, Y, Z);
                    std::memcpy(Row.data(), Mapped + RowBase, (size_t)RowCount * sizeof(int32_t));
                    for (uint32_t Column = 0; Column < RowCount; ++Column)
                        OutVolume.Voxels[RowBase + Column] = (int16_t)Row[Column];
                }
        }
    }

    DispatchComputeFinalize(Implementation, AsyncRun.Ticket);
    DestroyHostBuffer(Implementation.Device, AsyncRun.TriangleBuffer);
    DestroyHostBuffer(Implementation.Device, AsyncRun.VoxelBuffer);
    return ResolveEnabled;
}

void DiscardDistanceExactGpu(GpuBakeContext& Context, GpuBakeAsyncRun* Run)
{
    if (!Run) return;
    std::unique_ptr<GpuBakeAsyncRun> Owned(Run);
    if (!Context.InitializeEnabled || !Context.OpaqueImplementation) return;
    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);
    GpuBakeAsyncRunImplementation& AsyncRun = Run->Implementation;

    // The fence may still be unsignaled; wait the device idle before tearing the command buffer / buffers down so nothing is in use.
    if (Implementation.Device) vkDeviceWaitIdle(Implementation.Device);
    DispatchComputeFinalize(Implementation, AsyncRun.Ticket);
    DestroyHostBuffer(Implementation.Device, AsyncRun.TriangleBuffer);
    DestroyHostBuffer(Implementation.Device, AsyncRun.VoxelBuffer);
    DestroyDeviceBuffer(Implementation.Device, AsyncRun.VoxelPackedBuffer);
}

bool EvaluateSurfaceMapGpu(GpuBakeContext&              Context,
                           VulkanHost&                  Device,
                           const SurfaceSampleField&    Field,
                           const TriangleRayVolume&     Volume,
                           const SurfaceBakeParameters& Parameters,
                           SurfaceMapIdentity           Identity,
                           BakedImageBuffer&            Out)
{
    (void)Device;
    if (!Context.InitializeEnabled || !Context.OpaqueImplementation) return false;
    if (Field.Edge == 0) return false;

    // 📝 The cheap data maps (height / wireframe / material-identity / vertex-colour / UV-island) have no GPU pass this slice — a serial
    //    flood-fill or a flat per-texel lookup is a poor compute fit and would need extra SSBOs the std430 push block does not carry.
    //    Decline them here so the driver's inline CPU fallback (BeginSurfaceBake) encodes them instead. Only the ray-traced AO / bevel /
    //    curvature / dust / normal / position / thickness maps run on the GPU.
    if (Identity == SurfaceMapIdentity::Height           || Identity == SurfaceMapIdentity::Wireframe ||
        Identity == SurfaceMapIdentity::MaterialIdentity || Identity == SurfaceMapIdentity::VertexColour ||
        Identity == SurfaceMapIdentity::IslandIdentity)
        return false;

    GpuBakeContextImplementation& Implementation = *static_cast<GpuBakeContextImplementation*>(Context.OpaqueImplementation);

    vkResetDescriptorPool(Implementation.Device, Implementation.DescriptorPool, 0);
    return EvaluateSurfaceMapInternal(Implementation, Field, Volume, Parameters, Identity, Out);
}

} // namespace Frontier
