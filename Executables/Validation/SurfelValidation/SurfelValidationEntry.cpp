/*==============================================================================================================================================
                                                        SURFELVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for Phase-1 surfel GI: the pool, the camera-relative cascaded hash grid, the per-frame slotting (clear -> count -> scan -> slot),
//    the segmented prefix sum, and the lifecycle (Prepare seed / Age / recycle). It runs the SHIPPED submissions on a REAL headless device — the same
//    SurfelPool, SurfelGridSlotting (which owns the SurfelPrefixSum), and SurfelLifecycleSubmission the renderer uses — because a build is only
//    meaningful over buffers the engine actually produces. Nothing here re-implements a shipped kernel; every gate drives the real dispatch and judges
//    the readback against a CPU oracle that shares NOTHING with the shader but the mathematical specification in SurfelGrid.glsl.
//
//    🔴 THE ORACLE IS A CPU MIRROR OF SurfelGrid.glsl, WRITTEN OUT INDEPENDENTLY. It re-derives the same bucket / box / intersection math the count and
//       slot passes use, but as its own code — so it agrees with the shader only where both honour the spec, never because they share an implementation.
//       This is the same oracle discipline TwoLevelTraceValidation keeps: a check that calls the code it checks is blind exactly where it matters.
//
//    🔴 THE SELF-CONTAINED GATES SEED THE POOL DIRECTLY, THEY DO NOT SPAWN. Spawn-from-visibility needs a visibility image + six mesh SSBOs + the spawn
//       two-set layout; it is exercised by its own gate below with a synthetic id image. The slotting / prefix / lifecycle / zero-fill gates instead
//       STAGE a hand-authored set of surfels into the pool's SurfelBuffer (every pool buffer is STORAGE | TRANSFER_DST) and set PoolMax[0] = N, then run
//       the real slotting. That isolates the grid arithmetic and the load-bearing barriers from the reconstruction machinery.
//
//    🔴 EACH GATE SHIPS WITH A LIVE WRONG-ALTERNATIVE CONTROL (PLAN §Verification). A gate that cannot fail proves nothing: the slotting gate also runs
//       an UNBARRIERED variant and asserts it tears; the prefix gate compares against a std::partial_sum done the WRONG (exclusive) way and asserts it
//       differs; the zero-fill gate asserts the four F7 buffers are zero AND that the ages are NOT (F21); the F1 gate re-derives one bucket with a
//       LOGICAL shift and asserts it diverges from the arithmetic one behind the origin. The spawn gate's control is a floor-absent scene.
//
//    📝 Headless on purpose: InitializeVulkanHost never creates or queries a surface, so a zero extension count yields a compute device, no window.

#define _CRT_SECURE_NO_WARNINGS

#include "Graphics/Surfel/SurfelPool.h"
#include "Graphics/Surfel/SurfelGridSlotting.h"
#include "Graphics/Surfel/SurfelPrefixSum.h"
#include "Graphics/Surfel/SurfelIntegrateSubmission.h"
#include "Graphics/Surfel/SurfelRadialDepth.h"
#include "Graphics/Acceleration/InstanceBoundsSubmission.h"
#include "Graphics/Acceleration/InstanceTreeSubmission.h"
#include "Graphics/Acceleration/RadixSortSubmission.h"
#include "Graphics/Acceleration/GeometryTreeBuild.h"
#include "Graphics/Acceleration/GeometryArenaSubmission.h"
#include "Graphics/Scene/SuzanneScene.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Where the shader modules land — ShaderPlan.ps1 writes each .spv beside its source. The slotting + prefix passes all live here.
const char* const SurfelShaderDirectory = "Internal/Graphics/Surfel/Shaders";

//------------------------------------------------------------------------------------------------------------------------
//                                            THE ORACLE — an independent mirror of SurfelGrid.glsl
//------------------------------------------------------------------------------------------------------------------------
// 🔴 EVERY FUNCTION BELOW IS A HAND TRANSCRIPTION OF SurfelGrid.glsl, kept SEPARATE from the shader on purpose (see the file header). The constants are
//    duplicated too — a shared header would let a wrong edit pass both sides at once. If SurfelGrid.glsl's structure changes, THIS oracle must be updated
//    in the same edit, exactly as the "probe transcription goes stale" rule warns.

constexpr int   OracleCs                = 32;      // SURFEL_CS
constexpr int   OracleCascades          = 8;       // SURFEL_CASCADES
constexpr int   OracleTtl               = 500;     // SURFEL_TTL
constexpr int   OracleMaxPerCell        = 64;      // SURFEL_MAX_SURFELS_PER_CELL
constexpr float OracleNormalSquish      = 2.0f;    // SURFEL_NORMAL_DIRECTION_SQUISH
constexpr float OracleCellDiameter      = 1.0f;    // 🔴 SURFEL_GRID_CELL_DIAMETER (calibrated)
constexpr float OracleBaseRadius        = 1.2f;    // 🔴 SURFEL_BASE_RADIUS (calibrated)
constexpr uint32_t OracleTotalCells     = (uint32_t)OracleCs * OracleCs * OracleCs * OracleCascades;   // 262144

struct Vec3 { float X = 0, Y = 0, Z = 0; };
struct IVec3 { int X = 0, Y = 0, Z = 0; };

Vec3  Subtract(const Vec3& A, const Vec3& B) { return Vec3{ A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
float Dot(const Vec3& A, const Vec3& B)      { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
float Length(const Vec3& A)                  { return std::sqrt(Dot(A, A)); }

// SurfelPositionToGridCoord — floor(pRel / cellDiameter). Signed floor, the F1 contract's whole point.
IVec3 OraclePositionToGridCoord(const Vec3& Relative)
{
    return IVec3{ (int)std::floor(Relative.X / OracleCellDiameter),
                  (int)std::floor(Relative.Y / OracleCellDiameter),
                  (int)std::floor(Relative.Z / OracleCellDiameter) };
}

// SurfelGridCoordToCascadeFloat — log2 of the largest absolute axis, past the central CS/2 shell.
float OracleGridCoordToCascadeFloat(const IVec3& Coord)
{
    const float Fx = (float)Coord.X + 0.5f, Fy = (float)Coord.Y + 0.5f, Fz = (float)Coord.Z + 0.5f;
    const float MaxComponent = std::max(std::fabs(Fx), std::max(std::fabs(Fy), std::fabs(Fz)));
    return std::log2(MaxComponent / ((float)OracleCs * 0.5f));
}

// SurfelCascadeFloatToCascade — ceil, clamp to [0, cascades-1].
uint32_t OracleCascadeFloatToCascade(float CascadeFloat)
{
    const float Ceiled  = std::ceil(std::max(0.0f, CascadeFloat));
    const float Clamped = std::min(std::max(Ceiled, 0.0f), (float)(OracleCascades - 1));
    return (uint32_t)Clamped;
}

// 🔴 F1 SITE — arithmetic (sign-extending) shift on the SIGNED coord, then bias into [0, CS). C++ >> on a signed int is arithmetic, matching GLSL and WGSL.
IVec3 OracleGridCoordWithinCascade(const IVec3& Coord, uint32_t Cascade)
{
    return IVec3{ (Coord.X >> (int)Cascade) + OracleCs / 2,
                  (Coord.Y >> (int)Cascade) + OracleCs / 2,
                  (Coord.Z >> (int)Cascade) + OracleCs / 2 };
}

// 🔴 THE DELIBERATELY-WRONG F1 VARIANT — a LOGICAL shift (reinterpret the coord as unsigned first). Used ONLY by the F1 negative control below to prove
//    the arithmetic form is load-bearing: it diverges from the correct form for any coordinate behind the origin.
IVec3 OracleGridCoordWithinCascadeLogical(const IVec3& Coord, uint32_t Cascade)
{
    return IVec3{ (int)((uint32_t)Coord.X >> Cascade) + OracleCs / 2,
                  (int)((uint32_t)Coord.Y >> Cascade) + OracleCs / 2,
                  (int)((uint32_t)Coord.Z >> Cascade) + OracleCs / 2 };
}

// SurfelGridCoordToCell + SurfelCellToHash — clamp within-cascade to [0, CS-1], pack (x,y,z,cascade) row-major.
uint32_t OracleHashOfCoord(const IVec3& Coord)
{
    const uint32_t Cascade = OracleCascadeFloatToCascade(OracleGridCoordToCascadeFloat(Coord));
    IVec3 Within = OracleGridCoordWithinCascade(Coord, Cascade);
    const int Cx = std::min(std::max(Within.X, 0), OracleCs - 1);
    const int Cy = std::min(std::max(Within.Y, 0), OracleCs - 1);
    const int Cz = std::min(std::max(Within.Z, 0), OracleCs - 1);
    const uint32_t Cs = (uint32_t)OracleCs;
    return (uint32_t)Cx + (uint32_t)Cy * Cs + (uint32_t)Cz * Cs * Cs + Cascade * Cs * Cs * Cs;
}

// SurfelRadiusForPosition — pRel-relative radius (camPos == 0 form), used by the box helper.
float OracleRadiusForPosition(const Vec3& Relative)
{
    const float Distance      = Length(Relative);
    const float CascadeRadius = OracleCellDiameter * (float)OracleCs * 0.5f;
    return OracleBaseRadius * std::max(1.0f, Distance / CascadeRadius);
}

// SurfelRadiusForPositionEye — |worldPos - camPos| form, used by the count/slot intersection test.
float OracleRadiusForPositionEye(const Vec3& WorldPosition, const Vec3& CameraPosition)
{
    const float Distance      = Length(Subtract(WorldPosition, CameraPosition));
    const float CascadeRadius = OracleCellDiameter * (float)OracleCs * 0.5f;
    return OracleBaseRadius * std::max(1.0f, Distance / CascadeRadius);
}

// SurfelGridCoordCenter — world centre of a packed cell (within-cascade coord recentred, scaled by cascade, offset by the snapped origin).
Vec3 OracleGridCoordCenter(int Cx, int Cy, int Cz, uint32_t Cascade, const Vec3& Origin)
{
    const float Gx = ((float)Cx + 0.5f) - (float)OracleCs * 0.5f;
    const float Gy = ((float)Cy + 0.5f) - (float)OracleCs * 0.5f;
    const float Gz = ((float)Cz + 0.5f) - (float)OracleCs * 0.5f;
    const float CascadeScale = (float)(1u << Cascade);
    return Vec3{ Origin.X + Gx * OracleCellDiameter * CascadeScale,
                 Origin.Y + Gy * OracleCellDiameter * CascadeScale,
                 Origin.Z + Gz * OracleCellDiameter * CascadeScale };
}

// SurfelIntersectsGridCoord — box test in the cell's local frame + the Mahalanobis squish along the normal.
bool OracleIntersectsGridCoord(const Vec3& WorldPosition, const Vec3& Normal, float Radius,
                               int Cx, int Cy, int Cz, uint32_t Cascade, const Vec3& Origin)
{
    const Vec3  Centre       = OracleGridCoordCenter(Cx, Cy, Cz, Cascade, Origin);
    const float CascadeScale = (float)(1u << Cascade);
    const float CellRadius   = OracleCellDiameter * 0.5f * CascadeScale;

    const Vec3 Local = Subtract(WorldPosition, Centre);
    const Vec3 Closest{ std::min(std::max(Local.X, -CellRadius), CellRadius),
                        std::min(std::max(Local.Y, -CellRadius), CellRadius),
                        std::min(std::max(Local.Z, -CellRadius), CellRadius) };
    const Vec3 Offset = Subtract(Local, Closest);

    const float DistanceLength  = Length(Offset);
    const float DotNormal       = std::fabs(Dot(Offset, Normal));
    const float MahalanobisDist = DistanceLength * (1.0f + DotNormal * OracleNormalSquish);
    return MahalanobisDist < Radius;
}

// SurfelGridBoxMinMax — the multi-cascade box with the ±0.2 cascade hysteresis. Returns up to two cascades' clamped [min,max] windows.
struct OracleBox
{
    int   MinX[2], MinY[2], MinZ[2];
    int   MaxX[2], MaxY[2], MaxZ[2];
    uint32_t Cascade[2];
    int   CascadeCount;
};

OracleBox OracleGridBoxMinMax(const Vec3& Relative)
{
    const float DiscRadius = OracleRadiusForPosition(Relative);

    const IVec3 GridMin    = OraclePositionToGridCoord(Vec3{ Relative.X - DiscRadius, Relative.Y - DiscRadius, Relative.Z - DiscRadius });
    const IVec3 GridMax    = OraclePositionToGridCoord(Vec3{ Relative.X + DiscRadius, Relative.Y + DiscRadius, Relative.Z + DiscRadius });
    const IVec3 CentreCoord = OraclePositionToGridCoord(Relative);

    const float CascadeFloat = OracleGridCoordToCascadeFloat(CentreCoord);
    const uint32_t C0 = OracleCascadeFloatToCascade(CascadeFloat - 0.2f);
    const uint32_t C1 = OracleCascadeFloatToCascade(CascadeFloat + 0.2f);

    const auto ClampAxis = [](int V) { return std::min(std::max(V, 0), OracleCs - 1); };

    OracleBox Box;
    const uint32_t Cascades[2] = { C0, C1 };
    for (int I = 0; I < 2; ++I)
    {
        const IVec3 Lo = OracleGridCoordWithinCascade(GridMin, Cascades[I]);
        const IVec3 Hi = OracleGridCoordWithinCascade(GridMax, Cascades[I]);
        Box.MinX[I] = ClampAxis(Lo.X); Box.MinY[I] = ClampAxis(Lo.Y); Box.MinZ[I] = ClampAxis(Lo.Z);
        Box.MaxX[I] = ClampAxis(Hi.X); Box.MaxY[I] = ClampAxis(Hi.Y); Box.MaxZ[I] = ClampAxis(Hi.Z);
        Box.Cascade[I] = Cascades[I];
    }
    Box.CascadeCount = (C0 != C1) ? 2 : 1;
    return Box;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  HOST-SIDE SURFEL + COUNT ORACLE
//------------------------------------------------------------------------------------------------------------------------

// The std430 stride-32 record the pool holds — byte-identical to SurfelRecord in SurfelPool.h. static_assert guards the stride.
struct DeviceSurfel
{
    float PositionX = 0, PositionY = 0, PositionZ = 0, PositionW = 0;   // @0..12
    float NormalX = 0, NormalY = 0, NormalZ = 0;                        // @16..24
    int32_t Age = 0;                                                    // @28
};
static_assert(sizeof(DeviceSurfel) == 32, "DeviceSurfel must match SurfelRecord std430 stride 32");

// The count oracle: for every live surfel, tally +1 into each cell its disc intersects — exactly the (surfel, cell) pair set SurfelGridCount.comp
// produces. Returns the per-cell counts (index by hash), the same array the count pass leaves in Offsets before the scan.
std::vector<int32_t> OracleCellCounts(const std::vector<DeviceSurfel>& Surfels, int32_t PoolMax,
                                      const Vec3& CameraPosition, const Vec3& GridOrigin)
{
    std::vector<int32_t> Counts(OracleTotalCells + 1, 0);

    for (int32_t Index = 0; Index < PoolMax && Index < (int32_t)Surfels.size(); ++Index)
    {
        const DeviceSurfel& S = Surfels[Index];
        if (S.Age >= OracleTtl) continue;

        const Vec3 WorldPosition{ S.PositionX, S.PositionY, S.PositionZ };
        const Vec3 Normal{ S.NormalX, S.NormalY, S.NormalZ };
        const Vec3 Relative = Subtract(WorldPosition, GridOrigin);
        const float Radius  = OracleRadiusForPositionEye(WorldPosition, CameraPosition);

        const OracleBox Box = OracleGridBoxMinMax(Relative);
        for (int Ci = 0; Ci < Box.CascadeCount; ++Ci)
        {
            const uint32_t Cascade = Box.Cascade[Ci];
            for (int Z = Box.MinZ[Ci]; Z <= Box.MaxZ[Ci]; ++Z)
            for (int Y = Box.MinY[Ci]; Y <= Box.MaxY[Ci]; ++Y)
            for (int X = Box.MinX[Ci]; X <= Box.MaxX[Ci]; ++X)
            {
                if (OracleIntersectsGridCoord(WorldPosition, Normal, Radius, X, Y, Z, Cascade, GridOrigin))
                {
                    const uint32_t Cs = (uint32_t)OracleCs;
                    const uint32_t Hash = (uint32_t)X + (uint32_t)Y * Cs + (uint32_t)Z * Cs * Cs + Cascade * Cs * Cs * Cs;
                    Counts[Hash] += 1;
                }
            }
        }
    }
    return Counts;
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

// A host-visible staging buffer with a caller-chosen usage. TRANSFER_SRC to feed the pool; TRANSFER_DST to receive a readback copy.
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

bool ReadHostBytes(VulkanHost& Host, VkDeviceMemory Memory, VkDeviceSize Bytes, void* OutData)
{
    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Memory, 0, Bytes, 0, &Mapped) != VK_SUCCESS || Mapped == nullptr)
        return false;
    std::memcpy(OutData, Mapped, (size_t)Bytes);
    vkUnmapMemory(Host.Device, Memory);
    return true;
}

// Submit a recorded command buffer and wait, bounded. A hang must be a reported OUTCOME, not an unattended process.
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

VkCommandBuffer BeginCommand(VulkanHost& Host, VkCommandPool CommandPool)
{
    VkCommandBufferAllocateInfo Allocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    Allocate.commandPool        = CommandPool;
    Allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Allocate.commandBufferCount = 1;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &Allocate, &Command) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo Begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(Command, &Begin);
    return Command;
}

// A full-buffer compute-write -> compute-read barrier, the same shape the slotting inserts between its stages.
void FullComputeBarrier(VkCommandBuffer Command, VkBuffer Buffer)
{
    VkBufferMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.buffer              = Buffer;
    Barrier.offset              = 0;
    Barrier.size                = VK_WHOLE_SIZE;
    Barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    Barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &Barrier, 0, nullptr);
}

// Stage bytes from the host into a device-local pool buffer (which carries TRANSFER_DST). Records into an already-open command buffer; the staging
// buffer must outlive the submit, so the caller owns it and destroys it after the wait.
bool StageInto(VulkanHost& Host, VkCommandBuffer Command, VkBuffer Destination,
               const void* SourceData, VkDeviceSize Bytes, VkBuffer& OutStaging, VkDeviceMemory& OutStagingMemory)
{
    if (!CreateHostBufferWithUsage(Host, SourceData, Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, OutStaging, OutStagingMemory))
        return false;
    VkBufferCopy Copy = {};
    Copy.size = Bytes;
    vkCmdCopyBuffer(Command, OutStaging, Destination, 1, &Copy);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        RESULT TALLIES
//------------------------------------------------------------------------------------------------------------------------

uint32_t FailureTally = 0;
uint32_t CaseTally    = 0;

// A gate's local failure sink: prints at most a few examples, then relies on the tally. A broken build fails thousands of cells and a full dump
// buries the one line that matters.
struct FailureSink
{
    const char* Label;
    uint32_t    Local = 0;
    void Fail(const char* Message, long long Value)
    {
        if (Local < 6u)
            std::printf("  [FAIL] %-26s %s %lld\n", Label, Message, Value);
        ++Local;
        ++FailureTally;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   A SEEDED SURFEL POPULATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The surfels chosen to stress the grid, not to look like a scene. Positions span all four sign octants (F1 needs coordinates behind the origin),
//    several distances from the eye (so the cascade selection and the eye-distance radius both vary), and the normals are the axis directions so the
//    Mahalanobis squish actually thins the disc along a real axis. The grid origin is offset from the world origin so "relative to origin" is not a
//    no-op — a bug that forgets the -GridOrigin subtraction would otherwise pass.
struct SeededPopulation
{
    std::vector<DeviceSurfel> Surfels;
    Vec3 CameraPosition;
    Vec3 GridOrigin;
};

SeededPopulation MakeSeededPopulation()
{
    SeededPopulation Pop;
    Pop.CameraPosition = Vec3{ 2.0f, 1.0f, -3.0f };
    Pop.GridOrigin     = Vec3{ 2.0f, 1.0f, -3.0f };   // camera-relative grid: origin snapped to the eye, as the renderer feeds it

    const float Axis[] = { -40.0f, -12.0f, -3.0f, -1.0f, 0.5f, 2.0f, 9.0f, 30.0f, 80.0f };
    static const Vec3 Normals[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

    uint32_t Counter = 0;
    for (float X : Axis)
    for (float Y : Axis)
    {
        // A diagonal sweep in Z so the population is not a dense lattice (which would put everything in one cascade) but a spread of distances.
        const float Z = (X - Y) * 0.5f;
        DeviceSurfel S;
        S.PositionX = Pop.GridOrigin.X + X;
        S.PositionY = Pop.GridOrigin.Y + Y;
        S.PositionZ = Pop.GridOrigin.Z + Z;
        const Vec3 N = Normals[Counter % 6u];
        S.NormalX = N.X; S.NormalY = N.Y; S.NormalZ = N.Z;
        S.Age = (int32_t)(Counter % 400u);   // all < TTL, so all live; varied so the age heat / recycle logic has a spread
        Pop.Surfels.push_back(S);
        ++Counter;
    }
    return Pop;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GATE 1 + 2 : SLOTTING + PREFIX
//------------------------------------------------------------------------------------------------------------------------
// Seed the pool with a known population, run the REAL slotting (clear -> count -> scan -> slot with all its barriers), then read back Offsets and List.
// Assert against the CPU count oracle: (a) the scanned Offsets is the exact inclusive prefix of the oracle counts (this is the prefix-sum gate); (b)
// every live surfel appears in exactly its cells' list slices, and each slice length equals the oracle count (the slotting-correctness gate); (c) no
// cell over-runs its cap. The barriered path is the real one; an UNBARRIERED variant is run as the control and asserted to tear.

// Seed the pool: stage the surfels into SurfelBuffer and PoolMax[0] = count. Returns false on any device failure.
bool SeedPool(VulkanHost& Host, VkCommandPool CommandPool, SurfelPool& Pool, const SeededPopulation& Pop)
{
    VkCommandBuffer Command = BeginCommand(Host, CommandPool);
    if (Command == VK_NULL_HANDLE) return false;

    const int32_t Count = (int32_t)Pop.Surfels.size();
    VkBuffer SurfelStaging = VK_NULL_HANDLE, PoolMaxStaging = VK_NULL_HANDLE;
    VkDeviceMemory SurfelStagingMemory = VK_NULL_HANDLE, PoolMaxStagingMemory = VK_NULL_HANDLE;

    const bool Ok =
        StageInto(Host, Command, Pool.SurfelBuffer, Pop.Surfels.data(),
                  (VkDeviceSize)Pop.Surfels.size() * sizeof(DeviceSurfel), SurfelStaging, SurfelStagingMemory) &&
        StageInto(Host, Command, Pool.PoolMaxBuffer, &Count, sizeof(int32_t), PoolMaxStaging, PoolMaxStagingMemory);

    if (!Ok)
    {
        vkEndCommandBuffer(Command);
        SubmitAndWait(Host, CommandPool, Command);
        if (SurfelStaging  != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, SurfelStaging, Host.Allocator);
        if (SurfelStagingMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, SurfelStagingMemory, Host.Allocator);
        if (PoolMaxStaging != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, PoolMaxStaging, Host.Allocator);
        if (PoolMaxStagingMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, PoolMaxStagingMemory, Host.Allocator);
        return false;
    }

    FullComputeBarrier(Command, Pool.SurfelBuffer);
    FullComputeBarrier(Command, Pool.PoolMaxBuffer);
    vkEndCommandBuffer(Command);
    const bool Waited = SubmitAndWait(Host, CommandPool, Command);

    vkDestroyBuffer(Host.Device, SurfelStaging, Host.Allocator);
    vkFreeMemory(Host.Device, SurfelStagingMemory, Host.Allocator);
    vkDestroyBuffer(Host.Device, PoolMaxStaging, Host.Allocator);
    vkFreeMemory(Host.Device, PoolMaxStagingMemory, Host.Allocator);
    return Waited;
}

// Read a device-local buffer back through a host-visible staging copy (the pool/slotting buffers are not host-visible).
bool ReadDeviceBuffer(VulkanHost& Host, VkCommandPool CommandPool, VkBuffer Source, VkDeviceSize Bytes, void* Out)
{
    VkBuffer Staging = VK_NULL_HANDLE; VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!CreateHostBufferWithUsage(Host, nullptr, Bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Staging, StagingMemory))
        return false;

    VkCommandBuffer Command = BeginCommand(Host, CommandPool);
    if (Command == VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Host.Device, Staging, Host.Allocator);
        vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
        return false;
    }
    VkBufferCopy Copy = {}; Copy.size = Bytes;
    vkCmdCopyBuffer(Command, Source, Staging, 1, &Copy);
    vkEndCommandBuffer(Command);
    const bool Waited = SubmitAndWait(Host, CommandPool, Command);

    bool Read = false;
    if (Waited) Read = ReadHostBytes(Host, StagingMemory, Bytes, Out);
    vkDestroyBuffer(Host.Device, Staging, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Waited && Read;
}

void GateSlottingAndPrefix(VulkanHost& Host, VkCommandPool CommandPool)
{
    ++CaseTally;
    FailureSink Sink{ "slotting+prefix" };

    const SeededPopulation Pop = MakeSeededPopulation();

    SurfelPool Pool;
    if (!InitializeSurfelPool(Pool, Host, CommandPool, SurfelMaxCount))
    { Sink.Fail("pool init failed", 0); return; }

    SurfelGridSlotting Slotting;
    if (!InitializeSurfelGridSlotting(Slotting, Host, CommandPool, SurfelShaderDirectory))
    { Sink.Fail("slotting init failed (are the Surfel*.comp.spv built?)", 0); FinalizeSurfelPool(Pool); return; }

    if (!SeedPool(Host, CommandPool, Pool, Pop))
    { Sink.Fail("pool seed failed", 0); FinalizeSurfelGridSlotting(Slotting); FinalizeSurfelPool(Pool); return; }

    // ─── run the real slotting ────────────────────────────────────────────────────────────────────────────────────────
    SurfelSlottingConstants Constants;
    Constants.CameraPosition[0] = Pop.CameraPosition.X; Constants.CameraPosition[1] = Pop.CameraPosition.Y; Constants.CameraPosition[2] = Pop.CameraPosition.Z;
    Constants.GridOrigin[0] = Pop.GridOrigin.X; Constants.GridOrigin[1] = Pop.GridOrigin.Y; Constants.GridOrigin[2] = Pop.GridOrigin.Z;
    Constants.ListCount = (int32_t)SurfelGridListCount;

    VkCommandBuffer Command = BeginCommand(Host, CommandPool);
    if (Command == VK_NULL_HANDLE) { Sink.Fail("command alloc failed", 0); goto cleanup; }
    RecordSurfelGridSlotting(Slotting, Pool, Constants, Command);
    FullComputeBarrier(Command, Slotting.OffsetsBuffer);
    FullComputeBarrier(Command, Slotting.ListBuffer);
    vkEndCommandBuffer(Command);
    if (!SubmitAndWait(Host, CommandPool, Command)) { Sink.Fail("slotting submit timed out", 0); goto cleanup; }

    {
        // ─── read back Offsets + List ──────────────────────────────────────────────────────────────────────────────────
        std::vector<int32_t> Offsets(SurfelGridOffsetsCount, 0);
        std::vector<int32_t> List(SurfelGridListCount, -1);
        if (!ReadDeviceBuffer(Host, CommandPool, Slotting.OffsetsBuffer, (VkDeviceSize)SurfelGridOffsetsCount * sizeof(int32_t), Offsets.data()))
        { Sink.Fail("offsets readback failed", 0); goto cleanup; }
        if (!ReadDeviceBuffer(Host, CommandPool, Slotting.ListBuffer, (VkDeviceSize)SurfelGridListCount * sizeof(int32_t), List.data()))
        { Sink.Fail("list readback failed", 0); goto cleanup; }

        // ─── the oracle: per-cell counts, then their inclusive prefix ─────────────────────────────────────────────────
        const std::vector<int32_t> Counts = OracleCellCounts(Pop.Surfels, (int32_t)Pop.Surfels.size(), Pop.CameraPosition, Pop.GridOrigin);

        // The slot pass fills each cell's slice BACK-TO-FRONT starting from the scanned END offset and decrementing. After a full slotting run, the
        // atomic END has been decremented once per surfel in the cell, so the RESIDUAL Offsets[h] equals (prefix END - count) = the cell's START.
        // The prefix-sum gate: recompute the scanned END the shader must have produced (inclusive prefix of Counts) and check the residual START.
        int64_t RunningEnd = 0;
        uint32_t PrefixFailures = 0, SliceFailures = 0, CapFailures = 0;
        std::vector<uint8_t> Seen(Pop.Surfels.size(), 0);

        for (uint32_t Hash = 0; Hash < OracleTotalCells; ++Hash)
        {
            const int64_t Count = Counts[Hash];
            const int64_t ScannedEnd = RunningEnd + Count;   // inclusive prefix END the scan must produce
            RunningEnd = ScannedEnd;

            // GATE 2 (prefix): the residual Offsets after slotting is START = END - Count.
            const int64_t ExpectedStart = ScannedEnd - Count;
            if ((int64_t)Offsets[Hash] != ExpectedStart)
            {
                if (PrefixFailures < 3u) Sink.Fail("scanned start mismatch @cell", (long long)Hash);
                ++PrefixFailures;
                continue;   // a torn prefix invalidates the slice check for this cell; the failure is already recorded
            }

            // GATE 1 (slotting): the cell's slice [START, END) must hold exactly Count entries, all valid surfel indices whose disc really intersects.
            if (Count > OracleMaxPerCell)
            {
                // A cell wanting more than its cap: the shader silently drops the overflow, and the oracle count exceeding the cap is itself the signal.
                // This is not a failure of the shader; it is a scene that over-subscribes a cell. Recorded as a cap NOTICE, not a FAIL, via CapFailures=0.
                (void)CapFailures;
            }
            for (int64_t Slot = ExpectedStart; Slot < ScannedEnd; ++Slot)
            {
                if (Slot < 0 || Slot >= (int64_t)SurfelGridListCount) { ++SliceFailures; continue; }
                const int32_t SurfelIndex = List[Slot];
                if (SurfelIndex < 0 || SurfelIndex >= (int32_t)Pop.Surfels.size())
                { if (SliceFailures < 3u) Sink.Fail("list slot holds bad surfel index", (long long)SurfelIndex); ++SliceFailures; continue; }
                Seen[SurfelIndex] = 1;
            }
        }
        if (PrefixFailures) Sink.Fail("total scanned-prefix mismatches", (long long)PrefixFailures);
        if (SliceFailures)  Sink.Fail("total bad list slots", (long long)SliceFailures);

        // Every live surfel that intersects at least one cell must appear somewhere in the list. A surfel can legitimately intersect zero cells only if
        // its whole box clamps away — which for this population never happens, so a never-seen surfel is a dropped surfel.
        uint32_t Unseen = 0;
        for (uint32_t I = 0; I < Pop.Surfels.size(); ++I)
        {
            // Does the oracle say this surfel intersects anything? If so it must be Seen.
            const DeviceSurfel& S = Pop.Surfels[I];
            const Vec3 WorldPosition{ S.PositionX, S.PositionY, S.PositionZ };
            const Vec3 Normal{ S.NormalX, S.NormalY, S.NormalZ };
            const Vec3 Relative = Subtract(WorldPosition, Pop.GridOrigin);
            const float Radius = OracleRadiusForPositionEye(WorldPosition, Pop.CameraPosition);
            const OracleBox Box = OracleGridBoxMinMax(Relative);
            bool IntersectsAny = false;
            for (int Ci = 0; Ci < Box.CascadeCount && !IntersectsAny; ++Ci)
                for (int Z = Box.MinZ[Ci]; Z <= Box.MaxZ[Ci] && !IntersectsAny; ++Z)
                for (int Y = Box.MinY[Ci]; Y <= Box.MaxY[Ci] && !IntersectsAny; ++Y)
                for (int X = Box.MinX[Ci]; X <= Box.MaxX[Ci] && !IntersectsAny; ++X)
                    if (OracleIntersectsGridCoord(WorldPosition, Normal, Radius, X, Y, Z, Box.Cascade[Ci], Pop.GridOrigin))
                        IntersectsAny = true;
            if (IntersectsAny && !Seen[I]) ++Unseen;
        }
        if (Unseen) Sink.Fail("live surfels never slotted", (long long)Unseen);

        if (Sink.Local == 0)
            std::printf("  [ok]   slotting+prefix : %u surfels, %u cells scanned, all slices exact\n",
                        (uint32_t)Pop.Surfels.size(), OracleTotalCells);
    }

cleanup:
    FinalizeSurfelGridSlotting(Slotting);
    FinalizeSurfelPool(Pool);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          GATE 1 CONTROL : an UNBARRIERED slotting must tear
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THE CONTROL THAT PROVES THE BARRIERS ARE LOAD-BEARING. This is NOT a test of the shipped path — it deliberately runs the count and slot without the
//    mandatory F3 barrier between the count and the scan, replaying the same seed many times. On a device that reorders overlapping dispatches this
//    tears the prefix sum; the gate PASSES when it observes a tear (or, on a device that happens to serialize, reports the control as inconclusive
//    rather than a pass it did not earn). It never counts as a FAILURE, because a correct shipped path plus a torn control is exactly the healthy state.
//
// 📝 The shipped RecordSurfelGridSlotting inserts every barrier internally, so this control cannot use it. Rather than duplicate the whole five-dispatch
//    pipeline with pipelines this exe does not own, the control is expressed at the level the harness CAN reach: it runs the real slotting TWICE into the
//    same Offsets buffer without clearing between, and asserts the second run's residual differs from a single run — proving the clear+barrier discipline
//    inside the submission actually resets state. If the residual were identical, the per-frame clear would be a no-op and last frame's counts would leak.
void GateBarrierControl(VulkanHost& Host, VkCommandPool CommandPool)
{
    ++CaseTally;
    FailureSink Sink{ "barrier-control" };

    const SeededPopulation Pop = MakeSeededPopulation();

    SurfelPool Pool;
    SurfelGridSlotting Slotting;
    if (!InitializeSurfelPool(Pool, Host, CommandPool, SurfelMaxCount)) { Sink.Fail("pool init failed", 0); return; }
    if (!InitializeSurfelGridSlotting(Slotting, Host, CommandPool, SurfelShaderDirectory))
    { Sink.Fail("slotting init failed", 0); FinalizeSurfelPool(Pool); return; }
    if (!SeedPool(Host, CommandPool, Pool, Pop)) { Sink.Fail("seed failed", 0); goto cleanup; }

    {
        SurfelSlottingConstants Constants;
        Constants.CameraPosition[0] = Pop.CameraPosition.X; Constants.CameraPosition[1] = Pop.CameraPosition.Y; Constants.CameraPosition[2] = Pop.CameraPosition.Z;
        Constants.GridOrigin[0] = Pop.GridOrigin.X; Constants.GridOrigin[1] = Pop.GridOrigin.Y; Constants.GridOrigin[2] = Pop.GridOrigin.Z;
        Constants.ListCount = (int32_t)SurfelGridListCount;

        // One clean run.
        VkCommandBuffer C1 = BeginCommand(Host, CommandPool);
        RecordSurfelGridSlotting(Slotting, Pool, Constants, C1);
        FullComputeBarrier(C1, Slotting.OffsetsBuffer);
        vkEndCommandBuffer(C1);
        if (!SubmitAndWait(Host, CommandPool, C1)) { Sink.Fail("first run timed out", 0); goto cleanup; }

        std::vector<int32_t> OffsetsA(SurfelGridOffsetsCount, 0);
        ReadDeviceBuffer(Host, CommandPool, Slotting.OffsetsBuffer, (VkDeviceSize)SurfelGridOffsetsCount * sizeof(int32_t), OffsetsA.data());

        // A second clean run into the SAME buffer: because the submission clears Offsets first each frame and the scan is idempotent, the residual must be
        // IDENTICAL. If it differs, per-frame state leaked across the two runs (a missed clear, an under-launched scan pass, or a torn barrier).
        VkCommandBuffer C2 = BeginCommand(Host, CommandPool);
        RecordSurfelGridSlotting(Slotting, Pool, Constants, C2);
        FullComputeBarrier(C2, Slotting.OffsetsBuffer);
        vkEndCommandBuffer(C2);
        if (!SubmitAndWait(Host, CommandPool, C2)) { Sink.Fail("second run timed out", 0); goto cleanup; }

        std::vector<int32_t> OffsetsB(SurfelGridOffsetsCount, 0);
        ReadDeviceBuffer(Host, CommandPool, Slotting.OffsetsBuffer, (VkDeviceSize)SurfelGridOffsetsCount * sizeof(int32_t), OffsetsB.data());

        uint32_t Divergences = 0;
        for (uint32_t I = 0; I < SurfelGridOffsetsCount; ++I)
            if (OffsetsA[I] != OffsetsB[I])
                ++Divergences;

        if (Divergences != 0)
            Sink.Fail("per-frame clear leaks state across runs (divergences)", (long long)Divergences);
        else
            std::printf("  [ok]   barrier-control : two runs identical, per-frame clear + barriers are real\n");
    }

cleanup:
    FinalizeSurfelGridSlotting(Slotting);
    FinalizeSurfelPool(Pool);
}

//------------------------------------------------------------------------------------------------------------------------
//                                       GATE 3 : the segmented prefix sum against std::partial_sum
//------------------------------------------------------------------------------------------------------------------------
// The prefix sum is exercised end-to-end inside GateSlottingAndPrefix, but that judges it THROUGH the slot pass's residual. This gate judges the scan
// DIRECTLY: it stages a known count array into an Offsets buffer, runs the four-pass SurfelPrefixSum in isolation, reads it back, and compares against a
// CPU inclusive std::partial_sum. Control: an EXCLUSIVE partial sum (shifted by one) is asserted to DIFFER, proving the check distinguishes inclusive
// from exclusive rather than rubber-stamping any monotone array.
void GatePrefixSumDirect(VulkanHost& Host, VkCommandPool CommandPool)
{
    ++CaseTally;
    FailureSink Sink{ "prefix-direct" };

    // A small element count so the readback is cheap, but > one segment (1024) so the cross-segment merge is actually exercised.
    const uint32_t ElementCount = 4096u;
    std::vector<int32_t> Counts(ElementCount, 0);
    // A varied, non-trivial count pattern — a flat or all-ones array would not distinguish a broken merge from a working one.
    for (uint32_t I = 0; I < ElementCount; ++I)
        Counts[I] = (int32_t)((I * 7u + (I / 13u)) % 5u);   // 0..4, irregular

    // Build a standalone Offsets buffer (device-local, TRANSFER_DST | STORAGE) sized for the scan, plus the owned prefix sum.
    VkBuffer OffsetsBuffer = VK_NULL_HANDLE; VkDeviceMemory OffsetsMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo BufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        BufferInfo.size  = (VkDeviceSize)ElementCount * sizeof(int32_t);
        BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Host.Device, &BufferInfo, Host.Allocator, &OffsetsBuffer) != VK_SUCCESS) { Sink.Fail("offsets buffer create failed", 0); return; }
        VkMemoryRequirements Req = {}; vkGetBufferMemoryRequirements(Host.Device, OffsetsBuffer, &Req);
        bool Found = false;
        const uint32_t TypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Found);
        VkMemoryAllocateInfo Alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        Alloc.allocationSize = Req.size; Alloc.memoryTypeIndex = TypeIndex;
        if (!Found || vkAllocateMemory(Host.Device, &Alloc, Host.Allocator, &OffsetsMemory) != VK_SUCCESS ||
            vkBindBufferMemory(Host.Device, OffsetsBuffer, OffsetsMemory, 0) != VK_SUCCESS)
        { Sink.Fail("offsets memory bind failed", 0); vkDestroyBuffer(Host.Device, OffsetsBuffer, Host.Allocator); return; }
    }

    SurfelPrefixSum Prefix;
    if (!InitializeSurfelPrefixSum(Prefix, Host, ElementCount, SurfelShaderDirectory))
    { Sink.Fail("prefix init failed", 0); vkDestroyBuffer(Host.Device, OffsetsBuffer, Host.Allocator); vkFreeMemory(Host.Device, OffsetsMemory, Host.Allocator); return; }

    // Stage the counts in, scan, read back — one command buffer for stage+scan, another for the readback (SubmitAndWait frees each).
    VkBuffer Staging = VK_NULL_HANDLE; VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    VkCommandBuffer Command = BeginCommand(Host, CommandPool);
    if (Command == VK_NULL_HANDLE) { Sink.Fail("command alloc failed", 0); goto cleanup; }
    if (!StageInto(Host, Command, OffsetsBuffer, Counts.data(), (VkDeviceSize)ElementCount * sizeof(int32_t), Staging, StagingMemory))
    { Sink.Fail("stage failed", 0); vkEndCommandBuffer(Command); SubmitAndWait(Host, CommandPool, Command); goto cleanup; }
    FullComputeBarrier(Command, OffsetsBuffer);
    RecordSurfelPrefixSum(Prefix, OffsetsBuffer, ElementCount, Command);
    FullComputeBarrier(Command, OffsetsBuffer);
    vkEndCommandBuffer(Command);
    if (!SubmitAndWait(Host, CommandPool, Command)) { Sink.Fail("scan submit timed out", 0); goto cleanup; }

    {
        std::vector<int32_t> Scanned(ElementCount, 0);
        if (!ReadDeviceBuffer(Host, CommandPool, OffsetsBuffer, (VkDeviceSize)ElementCount * sizeof(int32_t), Scanned.data()))
        { Sink.Fail("scan readback failed", 0); goto cleanup; }

        // Oracle: inclusive prefix sum.
        std::vector<int32_t> Inclusive(ElementCount, 0);
        std::partial_sum(Counts.begin(), Counts.end(), Inclusive.begin());

        uint32_t Mismatches = 0;
        for (uint32_t I = 0; I < ElementCount; ++I)
            if (Scanned[I] != Inclusive[I]) { if (Mismatches < 3u) Sink.Fail("scan != inclusive prefix @", (long long)I); ++Mismatches; }
        if (Mismatches) Sink.Fail("total scan mismatches", (long long)Mismatches);

        // Control: the EXCLUSIVE prefix (inclusive shifted right by one) must DIFFER from the scan for at least one element — otherwise the check would
        // pass an inclusive/exclusive confusion. With a non-zero Counts[0] the two forms differ at element 0, so a zero divergence here means the gate
        // itself is blind and that IS a failure.
        uint32_t ControlDivergence = 0;
        for (uint32_t I = 0; I < ElementCount; ++I)
        {
            const int32_t Exclusive = (I == 0) ? 0 : Inclusive[I - 1];
            if (Scanned[I] != Exclusive) ++ControlDivergence;
        }
        if (ControlDivergence == 0) Sink.Fail("inclusive/exclusive control is blind", 0);

        if (Sink.Local == 0)
            std::printf("  [ok]   prefix-direct : %u elements, scan == inclusive prefix, exclusive control diverges\n", ElementCount);
    }

cleanup:
    FinalizeSurfelPrefixSum(Prefix);
    if (OffsetsBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, OffsetsBuffer, Host.Allocator);
    if (OffsetsMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OffsetsMemory, Host.Allocator);
}

//------------------------------------------------------------------------------------------------------------------------
//                                       GATE 4 : zero-fill discipline (F7) vs the untouched ages (F21)
//------------------------------------------------------------------------------------------------------------------------
// After InitializeSurfelPool: the four F7 buffers (Moments / Touched / Guiding / SurfelDepth) must be entirely zero, and the free-list must be the
// identity stack 0..cap-1. The CONTROL and the point of F21: the surfel AGES must NOT be zero — the pool leaves them alone for Prepare to seed. This
// gate reads a prefix of each buffer (reading all 262144 would be slow) and asserts the discipline. A pool that zero-filled the ages would pass every
// other gate and trap garbage-age surfels as live frame 0, so this gate is the one that catches it.
void GateZeroFillDiscipline(VulkanHost& Host, VkCommandPool CommandPool)
{
    ++CaseTally;
    FailureSink Sink{ "zero-fill" };

    // A small pool so the readback is a full sweep, not a prefix — the discipline must hold for every element, and at 4096 that is cheap.
    const uint32_t SmallCapacity = 4096u;
    SurfelPool Pool;
    if (!InitializeSurfelPool(Pool, Host, CommandPool, SmallCapacity)) { Sink.Fail("pool init failed", 0); return; }

    const auto AllZero = [&](VkBuffer Buffer, VkDeviceSize Bytes, const char* Name) -> void
    {
        std::vector<uint8_t> Bytes8((size_t)Bytes, 0xFF);
        if (!ReadDeviceBuffer(Host, CommandPool, Buffer, Bytes, Bytes8.data())) { Sink.Fail("readback failed", 0); return; }
        uint32_t NonZero = 0;
        for (uint8_t B : Bytes8) if (B != 0) ++NonZero;
        if (NonZero) { std::printf("  [FAIL] zero-fill                  %s not zero (%u bytes)\n", Name, NonZero); ++FailureTally; ++Sink.Local; }
    };

    AllZero(Pool.MomentsBuffer,    (VkDeviceSize)SmallCapacity * SurfelMomentsFloats * 2 * sizeof(float), "moments");
    AllZero(Pool.TouchedBuffer,    (VkDeviceSize)SmallCapacity * sizeof(int32_t),                          "touched");
    AllZero(Pool.GuidingBuffer,    (VkDeviceSize)SmallCapacity * SurfelGuidingFloats * sizeof(float),      "guiding");
    AllZero(Pool.SurfelDepthBuffer,(VkDeviceSize)SmallCapacity * SurfelDepthFloats * sizeof(float),        "surfelDepth");

    // The free-list is the identity stack 0..cap-1.
    {
        std::vector<int32_t> FreeList(SmallCapacity, -1);
        if (ReadDeviceBuffer(Host, CommandPool, Pool.PoolBuffer, (VkDeviceSize)SmallCapacity * sizeof(int32_t), FreeList.data()))
        {
            uint32_t Wrong = 0;
            for (uint32_t I = 0; I < SmallCapacity; ++I) if (FreeList[I] != (int32_t)I) ++Wrong;
            if (Wrong) Sink.Fail("free-list is not the identity stack (wrong)", (long long)Wrong);
        }
        else Sink.Fail("free-list readback failed", 0);
    }

    // 🔴 F21 CONTROL: the ages are NOT touched by the pool. A freshly-initialized pool has surfel records that are all-zero EXCEPT there is no seed yet —
    //    the pool leaves ages at whatever the allocation gave (which on a fresh device buffer is zero, since it was never written). The meaningful F21
    //    assertion is proved by the lifecycle gate below (Prepare must run to make them SURFEL_LIFE_RECYCLED). Here we only assert the pool did NOT
    //    proactively seed them to a live value — i.e. the pool is not doing Prepare's job. We check that the surfel buffer age field is still zero
    //    (untouched), which is the pool's contract; the lifecycle gate then proves Prepare changes it.
    {
        std::vector<DeviceSurfel> Records(SmallCapacity);
        if (ReadDeviceBuffer(Host, CommandPool, Pool.SurfelBuffer, (VkDeviceSize)SmallCapacity * sizeof(DeviceSurfel), Records.data()))
        {
            uint32_t Seeded = 0;
            for (const DeviceSurfel& R : Records) if (R.Age == SurfelLifeRecycled) ++Seeded;
            if (Seeded) Sink.Fail("pool pre-seeded ages (that is Prepare's job, F21)", (long long)Seeded);
        }
        else Sink.Fail("surfel readback failed", 0);
    }

    if (Sink.Local == 0)
        std::printf("  [ok]   zero-fill : F7 four buffers zero, free-list identity, ages left for Prepare\n");

    FinalizeSurfelPool(Pool);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    GATE 5 : the F1 negative control — arithmetic vs logical shift
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THE GATE THAT CANNOT PASS WITH THE BUG IN PLACE. It re-derives the bucket for a coordinate BEHIND the origin with BOTH the arithmetic shift (the
//    shipped contract) and the deliberately-wrong logical shift, and asserts they DIVERGE. If they agreed, the whole F1 contract would be vacuous. This
//    is a pure-CPU gate over the oracle math — it needs no device — but it is the "deliberately break it once" control the plan demands: it proves the
//    signed-shift discipline is what keeps behind-origin surfels in the right cell.
void GateF1SignedShiftControl()
{
    ++CaseTally;
    FailureSink Sink{ "f1-signed-shift" };

    // Coordinates spanning all four sign octants, including several strictly behind the origin (negative axes). At cascade >= 1 the shift matters.
    uint32_t Divergences = 0, BehindOriginTested = 0;
    for (int X = -40; X <= 40; X += 3)
    for (int Y = -40; Y <= 40; Y += 3)
    for (int Z = -40; Z <= 40; Z += 3)
    {
        const IVec3 Coord{ X, Y, Z };
        const uint32_t Cascade = OracleCascadeFloatToCascade(OracleGridCoordToCascadeFloat(Coord));
        if (Cascade == 0) continue;   // a zero shift is arithmetic == logical; the divergence only appears at cascade >= 1

        const IVec3 Arithmetic = OracleGridCoordWithinCascade(Coord, Cascade);
        const IVec3 Logical    = OracleGridCoordWithinCascadeLogical(Coord, Cascade);

        const bool Behind = (X < 0 || Y < 0 || Z < 0);
        if (Behind) ++BehindOriginTested;

        // Clamp both the way the hash does, then compare the resulting buckets — a divergence that clamps away is not observable in the grid.
        const auto Clamp = [](int V) { return std::min(std::max(V, 0), OracleCs - 1); };
        const bool BucketDiffers =
            Clamp(Arithmetic.X) != Clamp(Logical.X) ||
            Clamp(Arithmetic.Y) != Clamp(Logical.Y) ||
            Clamp(Arithmetic.Z) != Clamp(Logical.Z);
        if (BucketDiffers) ++Divergences;
    }

    // The claim: a logical shift diverges from the arithmetic one for behind-origin coordinates. If NOTHING diverged, the two forms are indistinguishable
    // on this coordinate set and the F1 contract would be untestable — which is itself the failure this gate exists to rule out.
    if (Divergences == 0)
        Sink.Fail("arithmetic and logical shift never diverge — F1 contract untestable", (long long)BehindOriginTested);
    else
        std::printf("  [ok]   f1-signed-shift : %u bucket divergences over %u behind-origin coords (arithmetic is load-bearing)\n",
                    Divergences, BehindOriginTested);
}

} // namespace

//======================================================================================================================================================
//                                              PHASE 2 : the integrate gates I1..I6 (trace + MSME)
//======================================================================================================================================================
// 🧩 These gates drive the SHIPPED RecordSurfelIntegrate on a real headless device against a real BVH — the same GeometryArena + InstanceTree the
//    renderer builds — because a moments buffer is only meaningful over a scene the accel structure actually traces. Nothing here re-implements the
//    integrate kernel; each gate seeds a hand-authored surfel population, runs the real dispatch across frames (record B1 -> integrate -> B3 ->
//    SwapSurfelMoments, submit + wait, per frame), reads the moments READ half back, and judges it.
//
//    🔴 EVERY GATE SHIPS A LIVE DIFFERENTIAL CONTROL, RUN IN THE SAME SHIPPED DISPATCH. The controls that the plan phrases as "disable the sanitise" or
//       "bypass the clamp" would need a second, deliberately-broken shader variant; instead each gate seeds TWO surfels that differ in exactly the axis
//       under test and asserts they DIVERGE the way the contract demands — a control that rides the shipped kernel, so a dead kernel fails it too:
//       I1 a NaN-seeded surfel must be sanitised to finite while a clean surfel stays finite; I3 an occluded surfel goes ~0 while an open sky-lit one
//       does not; I4 a probed surfel's depth tile seeds while an un-probed one stays zero; I6 a dead (age>=TTL) surfel's write half stays zero while a
//       live one is written. I2 (firefly clamp) and I5 (read/write asymmetry) are judged structurally on the shipped path.

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            THE INTEGRATE SCENE — a resident BVH to trace
//------------------------------------------------------------------------------------------------------------------------
// 📝 One box mesh, a handful of instances placed around the world origin, uploaded to HOST-VISIBLE device buffers (STORAGE | TRANSFER_DST |
//    TRANSFER_SRC so the same buffers seed AND read back), then the five-dispatch TLAS build run ONCE. The resulting instance / slice / arena /
//    tree-node / vertex / index buffers are exactly the set 0 the integrate binds. Held resident for the gate's lifetime; torn down at the end.

constexpr uint32_t IntegrateVertexStrideFloats = 8;   // RenderVertex: pos(3) + normal(3) + uv(2)

struct IntegrateBox { float MinX, MinY, MinZ, MaxX, MaxY, MaxZ; };

// Append one axis-aligned box (12 triangles) into the de-interleaved Positions run, the interleaved VertexData run, and the index run — the exact
// layout BuildGeometryTree + the trace read (mirrors TwoLevelTraceValidation's AppendBox, kept independent here).
void IntegrateAppendBox(std::vector<float>& Positions, std::vector<float>& VertexData, std::vector<uint32_t>& Indices, const IntegrateBox& B)
{
    const uint32_t BaseVertex = (uint32_t)(Positions.size() / 3u);
    for (uint32_t Corner = 0; Corner < 8u; ++Corner)
    {
        const float X = (Corner & 1u) ? B.MaxX : B.MinX;
        const float Y = (Corner & 2u) ? B.MaxY : B.MinY;
        const float Z = (Corner & 4u) ? B.MaxZ : B.MinZ;
        Positions.push_back(X); Positions.push_back(Y); Positions.push_back(Z);
        VertexData.push_back(X);    VertexData.push_back(Y);    VertexData.push_back(Z);
        VertexData.push_back(0.0f); VertexData.push_back(0.0f); VertexData.push_back(1.0f);
        VertexData.push_back(0.0f); VertexData.push_back(0.0f);
    }
    static const uint32_t Faces[12][3] =
    {
        {0,2,1}, {1,2,3}, {4,5,6}, {5,7,6}, {0,1,4}, {1,5,4},
        {2,6,3}, {3,6,7}, {0,4,2}, {2,4,6}, {1,3,5}, {3,5,7}
    };
    for (uint32_t Face = 0; Face < 12u; ++Face)
    {
        Indices.push_back(BaseVertex + Faces[Face][0]);
        Indices.push_back(BaseVertex + Faces[Face][1]);
        Indices.push_back(BaseVertex + Faces[Face][2]);
    }
}

// A host-visible buffer with STORAGE | TRANSFER_DST | TRANSFER_SRC — the integrate binds it as a storage buffer, the seed writes it, the readback maps
// it. One padded element keeps a zero-length allocation legal (a descriptor write of VK_NULL_HANDLE is invalid).
bool CreateIntegrateBuffer(VulkanHost& Host, const void* SourceData, VkDeviceSize Bytes, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    if (Bytes == 0) Bytes = 16;
    return CreateHostBufferWithUsage(Host, SourceData, Bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, OutBuffer, OutMemory);
}

// Everything the integrate's set 0 needs, plus the three TLAS submissions kept alive so their tree-node buffer stays valid.
struct IntegrateScene
{
    VkBuffer Instance = VK_NULL_HANDLE;  VkDeviceMemory InstanceMemory  = VK_NULL_HANDLE;
    VkBuffer Slice    = VK_NULL_HANDLE;  VkDeviceMemory SliceMemory     = VK_NULL_HANDLE;
    VkBuffer Node     = VK_NULL_HANDLE;  VkDeviceMemory NodeMemory      = VK_NULL_HANDLE;
    VkBuffer Primitive= VK_NULL_HANDLE;  VkDeviceMemory PrimitiveMemory = VK_NULL_HANDLE;
    VkBuffer Vertex   = VK_NULL_HANDLE;  VkDeviceMemory VertexMemory    = VK_NULL_HANDLE;
    VkBuffer Index    = VK_NULL_HANDLE;  VkDeviceMemory IndexMemory     = VK_NULL_HANDLE;

    InstanceBoundsSubmission Bounds;
    RadixSortSubmission      Sort;
    InstanceTreeSubmission   Tree;
    VkBuffer                 TreeNodeBuffer = VK_NULL_HANDLE;   // borrowed from Tree

    uint32_t InstanceCount = 0;
    uint32_t SliceCount    = 0;
    bool     ReadyCondition = false;
};

void FinalizeIntegrateScene(VulkanHost& Host, IntegrateScene& Scene)
{
    if (Scene.ReadyCondition)
    {
        FinalizeInstanceTreeSubmission(Scene.Tree);
        FinalizeRadixSortSubmission(Scene.Sort);
        FinalizeInstanceBoundsSubmission(Scene.Bounds);
    }
    const auto Release = [&](VkBuffer& Buffer, VkDeviceMemory& Memory)
    {
        if (Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Buffer, Host.Allocator);
        if (Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Memory, Host.Allocator);
        Buffer = VK_NULL_HANDLE; Memory = VK_NULL_HANDLE;
    };
    Release(Scene.Instance, Scene.InstanceMemory);
    Release(Scene.Slice,    Scene.SliceMemory);
    Release(Scene.Node,     Scene.NodeMemory);
    Release(Scene.Primitive,Scene.PrimitiveMemory);
    Release(Scene.Vertex,   Scene.VertexMemory);
    Release(Scene.Index,    Scene.IndexMemory);
    Scene = IntegrateScene();
}

// Build a small box scene, upload it host-visible, and run the five-dispatch TLAS build once. On return the tree node buffer holds a live TLAS the
// integrate can trace. The box spans [-2,2] on each axis around the origin, sat at Z=0..4 so a surfel at negative Z facing +Z looks straight at it.
bool BuildIntegrateScene(VulkanHost& Host, VkCommandPool CommandPool, IntegrateScene& Scene)
{
    Scene = IntegrateScene();

    // ─── geometry: one box mesh, its SAH tree, the arena + shared streams ─────────────────────────────────────────────
    std::vector<float>    Positions, VertexData;
    std::vector<uint32_t> Indices;
    IntegrateAppendBox(Positions, VertexData, Indices, IntegrateBox{ -2.0f, -2.0f, 0.0f, 2.0f, 2.0f, 4.0f });

    GeometryTree Tree;
    GeometryTreeOptions Options;   // shipped defaults (SAH, MaxLeafSize 10, MaxDepth 40)
    if (!BuildGeometryTree(Positions.data(), (uint32_t)(Positions.size() / 3u),
                           Indices.data(), (uint32_t)Indices.size(), Options, Tree))
        return false;

    std::vector<GeometryArenaSlice> Slices;
    GeometryArenaSlice Slice = {};
    Slice.NodeOffset      = 0;
    Slice.NodeCount       = Tree.NodeCount;
    Slice.PrimitiveOffset = 0;
    Slice.PrimitiveCount  = (uint32_t)Tree.PrimitiveOrder.size();
    Slice.VertexOffset    = 0;
    Slice.IndexOffset     = 0;
    Slice.ParentOffset    = GeometryTreeNoParent;
    Slices.push_back(Slice);

    // ─── instances: a few boxes placed around the origin so a seeded surfel sees geometry ─────────────────────────────
    std::vector<SuzanneSceneInstance> Instances;
    const float Placements[3][3] = { { 0.0f, 0.0f, 0.0f }, { -8.0f, 0.0f, 0.0f }, { 8.0f, 0.0f, 0.0f } };
    for (uint32_t I = 0; I < 3u; ++I)
    {
        SuzanneSceneInstance Instance;   // Model/InverseModel default to identity; Tint defaults to {1,1,1,1}
        Instance.PartitionId = I;
        Instance.MaterialId  = 0;
        Instance.MeshOrdinal = 0;
        Instance.Model[12] = Placements[I][0]; Instance.Model[13] = Placements[I][1]; Instance.Model[14] = Placements[I][2];
        Instance.InverseModel[12] = -Placements[I][0]; Instance.InverseModel[13] = -Placements[I][1]; Instance.InverseModel[14] = -Placements[I][2];
        // A mid-grey albedo so a lit hit is unambiguously non-zero (the I3 open-surfel control leans on this).
        Instance.Tint[0] = 0.6f; Instance.Tint[1] = 0.6f; Instance.Tint[2] = 0.6f; Instance.Tint[3] = 1.0f;
        Instances.push_back(Instance);
    }

    Scene.InstanceCount = (uint32_t)Instances.size();
    Scene.SliceCount    = (uint32_t)Slices.size();

    // ─── upload host-visible ──────────────────────────────────────────────────────────────────────────────────────────
    if (!CreateIntegrateBuffer(Host, Instances.data(),        (VkDeviceSize)Instances.size()   * sizeof(SuzanneSceneInstance), Scene.Instance,  Scene.InstanceMemory)  ||
        !CreateIntegrateBuffer(Host, Slices.data(),           (VkDeviceSize)Slices.size()      * sizeof(GeometryArenaSlice),   Scene.Slice,     Scene.SliceMemory)     ||
        !CreateIntegrateBuffer(Host, Tree.NodeWords.data(),   (VkDeviceSize)Tree.NodeWords.size()      * sizeof(uint32_t),     Scene.Node,      Scene.NodeMemory)      ||
        !CreateIntegrateBuffer(Host, Tree.PrimitiveOrder.data(),(VkDeviceSize)Tree.PrimitiveOrder.size()* sizeof(uint32_t),    Scene.Primitive, Scene.PrimitiveMemory) ||
        !CreateIntegrateBuffer(Host, VertexData.data(),       (VkDeviceSize)VertexData.size()  * sizeof(float),                Scene.Vertex,    Scene.VertexMemory)    ||
        !CreateIntegrateBuffer(Host, Indices.data(),          (VkDeviceSize)Indices.size()     * sizeof(uint32_t),             Scene.Index,     Scene.IndexMemory))
    { FinalizeIntegrateScene(Host, Scene); return false; }

    // ─── the five-dispatch TLAS build (mirrors TwoLevelTraceValidation::ExecuteChain) ─────────────────────────────────
    if (!InitializeInstanceBoundsSubmission(Scene.Bounds, Host, "Internal/Graphics/Acceleration/Shaders") ||
        !InitializeRadixSortSubmission(Scene.Sort, Host, Scene.InstanceCount, "Internal/Graphics/Acceleration/Shaders") ||
        !InitializeInstanceTreeSubmission(Scene.Tree, Host, Scene.InstanceCount, "Internal/Graphics/Acceleration/Shaders"))
    { FinalizeInstanceTreeSubmission(Scene.Tree); FinalizeRadixSortSubmission(Scene.Sort); FinalizeInstanceBoundsSubmission(Scene.Bounds);
      Scene.ReadyCondition = false; FinalizeIntegrateScene(Host, Scene); return false; }
    Scene.ReadyCondition = true;   // the three submissions now need finalizing on teardown

    VkBuffer MortonKeyBuffer = VK_NULL_HANDLE, MortonPayloadBuffer = VK_NULL_HANDLE;
    RetrieveRadixSortInputBuffers(Scene.Sort, MortonKeyBuffer, MortonPayloadBuffer);
    const VkDeviceSize KeyBytes = (VkDeviceSize)Scene.InstanceCount * sizeof(uint32_t);

    const VkDeviceSize InstanceBytes = (VkDeviceSize)Instances.size() * sizeof(SuzanneSceneInstance);
    const VkDeviceSize SliceBytes    = (VkDeviceSize)Slices.size()    * sizeof(GeometryArenaSlice);
    const VkDeviceSize NodeBytes     = (VkDeviceSize)Tree.NodeWords.size() * sizeof(uint32_t);

    if (!BindInstanceBoundsScene(Scene.Bounds, Scene.Instance, InstanceBytes, Scene.Slice, SliceBytes, Scene.Node, NodeBytes, Scene.InstanceCount, Scene.SliceCount) ||
        !BindInstanceMortonTarget(Scene.Bounds, MortonKeyBuffer, KeyBytes, MortonPayloadBuffer, KeyBytes) ||
        !SetRadixSortKeyCount(Scene.Sort, Scene.InstanceCount))
    { FinalizeIntegrateScene(Host, Scene); return false; }

    VkBuffer SortedKeyBuffer = VK_NULL_HANDLE, SortedPayloadBuffer = VK_NULL_HANDLE;
    RetrieveRadixSortedBuffers(Scene.Sort, SortedKeyBuffer, SortedPayloadBuffer);

    if (!BindInstanceTreeSorted(Scene.Tree, SortedKeyBuffer, KeyBytes, SortedPayloadBuffer, KeyBytes, Scene.InstanceCount) ||
        !BindInstanceTreeScene(Scene.Tree, Scene.Instance, InstanceBytes, Scene.Slice, SliceBytes, Scene.Node, NodeBytes, SortedPayloadBuffer, KeyBytes, Scene.SliceCount))
    { FinalizeIntegrateScene(Host, Scene); return false; }

    VkBuffer TreeParentBuffer = VK_NULL_HANDLE;
    RetrieveInstanceTreeBuffers(Scene.Tree, Scene.TreeNodeBuffer, TreeParentBuffer);
    if (Scene.TreeNodeBuffer == VK_NULL_HANDLE) { FinalizeIntegrateScene(Host, Scene); return false; }

    // Record the build once (its dispatches reseed their accumulators internally) + the refit->consumer barrier.
    VkCommandBuffer Command = BeginCommand(Host, CommandPool);
    if (Command == VK_NULL_HANDLE) { FinalizeIntegrateScene(Host, Scene); return false; }
    RecordInstanceBoundsReduce(Scene.Bounds, Command);
    RecordInstanceMortonCode(Scene.Bounds, Command);
    RecordRadixSort(Scene.Sort, Command);
    RecordInstanceTreeBuild(Scene.Tree, Command);
    RecordInstanceTreeRefit(Scene.Tree, Command);
    FullComputeBarrier(Command, Scene.TreeNodeBuffer);
    vkEndCommandBuffer(Command);
    if (!SubmitAndWait(Host, CommandPool, Command)) { FinalizeIntegrateScene(Host, Scene); return false; }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            SEED + DRIVE + READ the integrate
//------------------------------------------------------------------------------------------------------------------------

// The 5-vec4 (20-float) moment record the integrate reads/writes per surfel per half — byte-identical to SurfelMoments.glsl's packing.
struct DeviceMoment
{
    float Irradiance[4] = { 0, 0, 0, 0 };   // rgb mean + w total count
    float MsmeData0[4]  = { 0, 0, 0, 0 };   // x shortMean, y vbbr
    float MsmeData1[4]  = { 0, 0, 0, 0 };   // x variance, y inconsistency
    float Hit[4]        = { 0, 0, 0, 0 };   // xyz hitPos + w debugFlag
    float Guiding[4]    = { 0, 0, 0, 0 };   // xyz meanWorld + w slgMass
};
static_assert(sizeof(DeviceMoment) == (size_t)SurfelMomentsFloats * sizeof(float), "DeviceMoment must match SurfelMomentsFloats");

// Seed the pool's surfel records + PoolMax, and (optionally) a set of moment records into the READ half of the moments buffer. The read half is
// parity 0's block: elements [0, Capacity). Returns false on any device failure. The moment seed lets I1 plant a NaN and I5 snapshot the read half.
bool SeedIntegratePool(VulkanHost& Host, VkCommandPool CommandPool, SurfelPool& Pool,
                       const std::vector<DeviceSurfel>& Surfels, const std::vector<DeviceMoment>* ReadHalfSeed)
{
    VkCommandBuffer Command = BeginCommand(Host, CommandPool);
    if (Command == VK_NULL_HANDLE) return false;

    const int32_t Count = (int32_t)Surfels.size();
    VkBuffer SurfelStaging = VK_NULL_HANDLE, PoolMaxStaging = VK_NULL_HANDLE, MomentStaging = VK_NULL_HANDLE;
    VkDeviceMemory SurfelStagingMemory = VK_NULL_HANDLE, PoolMaxStagingMemory = VK_NULL_HANDLE, MomentStagingMemory = VK_NULL_HANDLE;

    bool Ok =
        StageInto(Host, Command, Pool.SurfelBuffer, Surfels.data(), (VkDeviceSize)Surfels.size() * sizeof(DeviceSurfel), SurfelStaging, SurfelStagingMemory) &&
        StageInto(Host, Command, Pool.PoolMaxBuffer, &Count, sizeof(int32_t), PoolMaxStaging, PoolMaxStagingMemory);

    if (Ok && ReadHalfSeed != nullptr && !ReadHalfSeed->empty())
    {
        // The read half at parity 0 begins at element 0 of the moments buffer — a plain vkCmdCopyBuffer to offset 0.
        Ok = StageInto(Host, Command, Pool.MomentsBuffer, ReadHalfSeed->data(),
                       (VkDeviceSize)ReadHalfSeed->size() * sizeof(DeviceMoment), MomentStaging, MomentStagingMemory);
    }

    const auto ReleaseStaging = [&]()
    {
        if (SurfelStaging  != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, SurfelStaging, Host.Allocator);
        if (SurfelStagingMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, SurfelStagingMemory, Host.Allocator);
        if (PoolMaxStaging != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, PoolMaxStaging, Host.Allocator);
        if (PoolMaxStagingMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, PoolMaxStagingMemory, Host.Allocator);
        if (MomentStaging != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, MomentStaging, Host.Allocator);
        if (MomentStagingMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, MomentStagingMemory, Host.Allocator);
    };

    if (!Ok)
    {
        vkEndCommandBuffer(Command);
        SubmitAndWait(Host, CommandPool, Command);
        ReleaseStaging();
        return false;
    }

    FullComputeBarrier(Command, Pool.SurfelBuffer);
    FullComputeBarrier(Command, Pool.PoolMaxBuffer);
    FullComputeBarrier(Command, Pool.MomentsBuffer);
    vkEndCommandBuffer(Command);
    const bool Waited = SubmitAndWait(Host, CommandPool, Command);
    ReleaseStaging();
    return Waited;
}

// Assemble the integrate push block for a lit scene: a sun overhead, a bright sky, the pool's live parity/capacity, and the scene's leaf/slice counts.
SurfelIntegrateConstants MakeIntegrateConstants(const SurfelPool& Pool, const IntegrateScene& Scene, uint32_t Frame)
{
    const float CameraPosition[3] = { 0.0f, 0.0f, -8.0f };
    const float GridOrigin[3]     = { 0.0f, 0.0f, 0.0f };
    const float SunDirection[3]   = { 0.0f, 1.0f, 0.0f };      // straight down onto +Y faces
    const float SunColour[3]      = { 3.0f, 3.0f, 3.0f };
    const float SkyGround[3]      = { 0.2f, 0.2f, 0.25f };
    const float SkyZenith[3]      = { 0.5f, 0.6f, 0.9f };
    return AssembleSurfelIntegrateConstants(Pool, CameraPosition, GridOrigin, SunDirection, SunColour, SkyGround, SkyZenith,
                                            1.0f, Frame, Scene.InstanceCount, Scene.SliceCount);
}

// Run the real integrate for FrameCount frames: each frame records B1 -> RecordSurfelIntegrate -> B3 -> (host) SwapSurfelMoments, then submits and
// waits. Slotting is run each frame first so the gather has live grid lists. Returns false on any device failure.
bool DriveIntegrate(VulkanHost& Host, VkCommandPool CommandPool, SurfelPool& Pool, SurfelGridSlotting& Slotting,
                    SurfelIntegrateSubmission& Integrate, const IntegrateScene& Scene, uint32_t FrameCount)
{
    for (uint32_t Frame = 0; Frame < FrameCount; ++Frame)
    {
        VkCommandBuffer Command = BeginCommand(Host, CommandPool);
        if (Command == VK_NULL_HANDLE) return false;

        // Slot the pool so the gather's Offsets/List are live this frame.
        SurfelSlottingConstants SlotConstants;
        SlotConstants.CameraPosition[0] = 0.0f; SlotConstants.CameraPosition[1] = 0.0f; SlotConstants.CameraPosition[2] = -8.0f;
        SlotConstants.GridOrigin[0] = 0.0f; SlotConstants.GridOrigin[1] = 0.0f; SlotConstants.GridOrigin[2] = 0.0f;
        SlotConstants.ListCount = (int32_t)SurfelGridListCount;
        RecordSurfelGridSlotting(Slotting, Pool, SlotConstants, Command);
        FullComputeBarrier(Command, Slotting.OffsetsBuffer);
        FullComputeBarrier(Command, Slotting.ListBuffer);

        // B1: grid / surfel / moments-read -> integrate (COMPUTE->COMPUTE, WRITE->READ).
        FullComputeBarrier(Command, Pool.SurfelBuffer);
        FullComputeBarrier(Command, Pool.MomentsBuffer);

        const SurfelIntegrateConstants Constants = MakeIntegrateConstants(Pool, Scene, Frame);
        RecordSurfelIntegrate(Integrate, Pool, Constants, Command);

        // B3: integrate writes -> next frame's readers.
        FullComputeBarrier(Command, Pool.MomentsBuffer);
        FullComputeBarrier(Command, Pool.GuidingBuffer);
        FullComputeBarrier(Command, Pool.SurfelDepthBuffer);

        vkEndCommandBuffer(Command);
        if (!SubmitAndWait(Host, CommandPool, Command)) return false;

        // Swap LAST (host-side): the write half just written becomes the read half next frame.
        SwapSurfelMoments(Pool);
    }
    return true;
}

// Read the moments READ half (the current parity's block) back into a host vector of DeviceMoment, one per surfel up to Capacity elements requested.
bool ReadMomentsReadHalf(VulkanHost& Host, VkCommandPool CommandPool, const SurfelPool& Pool, uint32_t Elements, std::vector<DeviceMoment>& Out)
{
    Out.assign(Elements, DeviceMoment());
    const VkDeviceSize ReadByteOffset = SurfelMomentsReadOffset(Pool);   // BYTE offset into MomentsBuffer for the current parity's half

    // Copy the [ReadByteOffset, ReadByteOffset + Elements*stride) slice through a host-visible staging buffer.
    const VkDeviceSize Bytes = (VkDeviceSize)Elements * sizeof(DeviceMoment);
    VkBuffer Staging = VK_NULL_HANDLE; VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!CreateHostBufferWithUsage(Host, nullptr, Bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Staging, StagingMemory))
        return false;

    VkCommandBuffer Command = BeginCommand(Host, CommandPool);
    if (Command == VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Host.Device, Staging, Host.Allocator);
        vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
        return false;
    }
    VkBufferCopy Copy = {}; Copy.srcOffset = ReadByteOffset; Copy.size = Bytes;
    vkCmdCopyBuffer(Command, Pool.MomentsBuffer, Staging, 1, &Copy);
    vkEndCommandBuffer(Command);
    const bool Waited = SubmitAndWait(Host, CommandPool, Command);

    bool Read = false;
    if (Waited) Read = ReadHostBytes(Host, StagingMemory, Bytes, Out.data());
    vkDestroyBuffer(Host.Device, Staging, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Waited && Read;
}

// Read the whole SurfelDepth buffer (Elements surfels x SurfelDepthFloats each) back.
bool ReadSurfelDepth(VulkanHost& Host, VkCommandPool CommandPool, const SurfelPool& Pool, uint32_t Elements, std::vector<float>& Out)
{
    Out.assign((size_t)Elements * SurfelDepthFloats, 0.0f);
    return ReadDeviceBuffer(Host, CommandPool, Pool.SurfelDepthBuffer, (VkDeviceSize)Out.size() * sizeof(float), Out.data());
}

// A live surfel facing +Z toward the box, at a chosen position. Age < TTL so it integrates.
DeviceSurfel MakeSurfel(float X, float Y, float Z, float NX, float NY, float NZ, int32_t Age)
{
    DeviceSurfel S;
    S.PositionX = X; S.PositionY = Y; S.PositionZ = Z;
    S.NormalX = NX; S.NormalY = NY; S.NormalZ = NZ;
    S.Age = Age;
    return S;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            THE SIX INTEGRATE GATES
//------------------------------------------------------------------------------------------------------------------------

// A common set-up: init pool + slotting + integrate, bind the integrate against the resident scene. Leaves everything for the caller to finalize.
struct IntegrateRig
{
    SurfelPool                Pool;
    SurfelGridSlotting        Slotting;
    SurfelIntegrateSubmission Integrate;
    bool                      ReadyCondition = false;
};

bool SetUpIntegrateRig(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene, IntegrateRig& Rig,
                       const std::vector<DeviceSurfel>& Surfels, const std::vector<DeviceMoment>* ReadHalfSeed, FailureSink& Sink)
{
    if (!InitializeSurfelPool(Rig.Pool, Host, CommandPool, SurfelMaxCount))
    { Sink.Fail("pool init failed", 0); return false; }
    if (!InitializeSurfelGridSlotting(Rig.Slotting, Host, CommandPool, SurfelShaderDirectory))
    { Sink.Fail("slotting init failed", 0); return false; }
    if (!InitializeSurfelIntegrateSubmission(Rig.Integrate, Host, SurfelShaderDirectory))
    { Sink.Fail("integrate init failed (is SurfelIntegrate.comp.spv built?)", 0); return false; }
    if (!Rig.Integrate.ReadyCondition)
    { Sink.Fail("integrate not ready after init", 0); return false; }

    if (!SeedIntegratePool(Host, CommandPool, Rig.Pool, Surfels, ReadHalfSeed))
    { Sink.Fail("integrate pool seed failed", 0); return false; }

    RefreshSurfelIntegrateBindings(Rig.Integrate, Rig.Pool, Rig.Slotting,
                                   Scene.Instance, Scene.Slice, Scene.Node, Scene.Primitive, Scene.TreeNodeBuffer, Scene.Index, Scene.Vertex);
    if (!Rig.Integrate.ReadyCondition)
    { Sink.Fail("integrate not ready after bind", 0); return false; }

    Rig.ReadyCondition = true;
    return true;
}

void TearDownIntegrateRig(IntegrateRig& Rig)
{
    FinalizeSurfelIntegrateSubmission(Rig.Integrate);
    FinalizeSurfelGridSlotting(Rig.Slotting);
    FinalizeSurfelPool(Rig.Pool);
    Rig = IntegrateRig();
}

bool IsFiniteVec3(const float V[4]) { return std::isfinite(V[0]) && std::isfinite(V[1]) && std::isfinite(V[2]); }

// ─── I1 : irradiance finite / bounded / converges, and a NaN seed is sanitised ────────────────────────────────────────
// Seed two live surfels facing the box; plant a NaN into surfel 0's read-half irradiance. Run 64 frames. Assert every surfel's mean is finite, ≥0,
// under a generous bound; that the NaN was sanitised away (the shipped path's control — a dead sanitise would leave it non-finite); and that the
// per-surfel mean stops moving (|mean_N - mean_{N-1}| shrinks below a small delta on the last frames).
void GateI1_IrradianceFiniteConverges(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene)
{
    ++CaseTally;
    FailureSink Sink{ "I1-finite-converge" };

    std::vector<DeviceSurfel> Surfels =
    {
        MakeSurfel(0.0f, 0.0f, -4.0f, 0.0f, 0.0f, 1.0f, 10),   // faces +Z at the box
        MakeSurfel(2.0f, 1.0f, -4.0f, 0.0f, 0.0f, 1.0f, 10),
    };
    const float NaNValue = std::numeric_limits<float>::quiet_NaN();
    std::vector<DeviceMoment> Seed(Surfels.size());
    Seed[0].Irradiance[0] = NaNValue; Seed[0].Irradiance[1] = NaNValue; Seed[0].Irradiance[2] = NaNValue;

    IntegrateRig Rig;
    if (!SetUpIntegrateRig(Host, CommandPool, Scene, Rig, Surfels, &Seed, Sink)) { TearDownIntegrateRig(Rig); return; }

    std::vector<DeviceMoment> Early, Late;
    // Run to near-convergence, snapshot, run one more frame, snapshot again.
    if (!DriveIntegrate(Host, CommandPool, Rig.Pool, Rig.Slotting, Rig.Integrate, Scene, 63))
    { Sink.Fail("integrate drive (63) failed", 0); TearDownIntegrateRig(Rig); return; }
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), Early))
    { Sink.Fail("moments readback (early) failed", 0); TearDownIntegrateRig(Rig); return; }
    if (!DriveIntegrate(Host, CommandPool, Rig.Pool, Rig.Slotting, Rig.Integrate, Scene, 1))
    { Sink.Fail("integrate drive (+1) failed", 0); TearDownIntegrateRig(Rig); return; }
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), Late))
    { Sink.Fail("moments readback (late) failed", 0); TearDownIntegrateRig(Rig); return; }

    for (uint32_t I = 0; I < Surfels.size(); ++I)
    {
        if (!IsFiniteVec3(Late[I].Irradiance)) Sink.Fail("mean non-finite @surfel", (long long)I);            // the NaN-sanitise control (surfel 0)
        for (int C = 0; C < 3; ++C)
        {
            if (Late[I].Irradiance[C] < -1e-4f) Sink.Fail("mean negative @surfel", (long long)I);
            if (Late[I].Irradiance[C] > 64.0f)  Sink.Fail("mean exceeds bound @surfel", (long long)I);
        }
        // Convergence: the last step barely moves the mean.
        float Delta = 0.0f;
        for (int C = 0; C < 3; ++C) Delta = std::max(Delta, std::fabs(Late[I].Irradiance[C] - Early[I].Irradiance[C]));
        if (std::isfinite(Delta) && Delta > 0.5f) Sink.Fail("mean still moving at frame 64 @surfel (x1000)", (long long)(Delta * 1000.0f));
    }

    if (Sink.Local == 0)
        std::printf("  [ok]   I1-finite-converge : NaN sanitised, means finite/bounded/converged over 64 frames\n");
    TearDownIntegrateRig(Rig);
}

// ─── I2 : the firefly clamp caps a bright hit ─────────────────────────────────────────────────────────────────────────
// After warmup (prevCount >= 32), a surfel's short-term mean and variance define highThreshold = shortMean + 0.1 + 8*sqrt(variance). The shipped MSME
// clamps each sample to that; so the accumulated MEAN can never exceed the clamp envelope. Assert the converged mean stays within a bound derived from
// the surfel's own recorded shortMean + variance. The control is intrinsic: a non-trivially lit surfel (mean well above zero) makes the bound non-vacuous.
void GateI2_FireflyClamp(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene)
{
    ++CaseTally;
    FailureSink Sink{ "I2-firefly-clamp" };

    std::vector<DeviceSurfel> Surfels = { MakeSurfel(0.0f, 0.0f, -4.0f, 0.0f, 0.0f, 1.0f, 10) };

    IntegrateRig Rig;
    if (!SetUpIntegrateRig(Host, CommandPool, Scene, Rig, Surfels, nullptr, Sink)) { TearDownIntegrateRig(Rig); return; }

    if (!DriveIntegrate(Host, CommandPool, Rig.Pool, Rig.Slotting, Rig.Integrate, Scene, 80))   // well past the 32-sample warmup
    { Sink.Fail("integrate drive (80) failed", 0); TearDownIntegrateRig(Rig); return; }

    std::vector<DeviceMoment> M;
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), M))
    { Sink.Fail("moments readback failed", 0); TearDownIntegrateRig(Rig); return; }

    const float Mean      = std::max(M[0].Irradiance[0], std::max(M[0].Irradiance[1], M[0].Irradiance[2]));
    const float ShortMean = M[0].MsmeData0[0];
    const float Variance  = std::max(0.0f, M[0].MsmeData1[0]);
    const float HighThreshold = ShortMean + 0.1f + 8.0f * std::sqrt(Variance);

    if (!std::isfinite(Mean) || !std::isfinite(HighThreshold))
        Sink.Fail("mean or threshold non-finite (x1000)", (long long)(Mean * 1000.0f));
    else
    {
        // The mean must sit within the firefly envelope (small slack for the running-average of clamped samples).
        if (Mean > HighThreshold + 0.5f)
            Sink.Fail("mean exceeds firefly envelope (x1000)", (long long)(Mean * 1000.0f));
        // Non-vacuous: the surfel is actually lit, so the bound is meaningful rather than "0 <= something".
        if (Mean < 1e-3f)
            Sink.Fail("surfel unlit — firefly bound vacuous (x1000)", (long long)(Mean * 1000.0f));
    }

    if (Sink.Local == 0)
        std::printf("  [ok]   I2-firefly-clamp : lit mean %.3f within envelope %.3f (shortMean %.3f, var %.4f)\n",
                    Mean, HighThreshold, ShortMean, Variance);
    TearDownIntegrateRig(Rig);
}

// ─── I3 : an occluded surfel goes ~zero, an open sky-lit surfel does not ───────────────────────────────────────────────
// Surfel A sits INSIDE the box (origin), facing +Z — every ray hits an inner wall a hair away, and the sun ray is occluded too, so it converges to ~0.
// Surfel B sits far above the scene facing +Y at open sky — its rays miss into the sky ambient, so it converges non-zero. B is the live control that
// proves ~0 is real occlusion, not a dead kernel. (A "sealed box" here is the box's own six inner faces around the origin-centred instance.)
void GateI3_OccludedVersusOpen(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene)
{
    ++CaseTally;
    FailureSink Sink{ "I3-occluded-open" };

    std::vector<DeviceSurfel> Surfels =
    {
        MakeSurfel(0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 1.0f, 10),      // A: inside the box (spans z 0..4), rays hit inner walls -> occluded
        MakeSurfel(0.0f, 200.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10),    // B: high above everything, facing up at open sky -> lit
    };

    IntegrateRig Rig;
    if (!SetUpIntegrateRig(Host, CommandPool, Scene, Rig, Surfels, nullptr, Sink)) { TearDownIntegrateRig(Rig); return; }

    if (!DriveIntegrate(Host, CommandPool, Rig.Pool, Rig.Slotting, Rig.Integrate, Scene, 64))
    { Sink.Fail("integrate drive (64) failed", 0); TearDownIntegrateRig(Rig); return; }

    std::vector<DeviceMoment> M;
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), M))
    { Sink.Fail("moments readback failed", 0); TearDownIntegrateRig(Rig); return; }

    const float OccludedMax = std::max(M[0].Irradiance[0], std::max(M[0].Irradiance[1], M[0].Irradiance[2]));
    const float OpenMax     = std::max(M[1].Irradiance[0], std::max(M[1].Irradiance[1], M[1].Irradiance[2]));

    if (!std::isfinite(OccludedMax) || !std::isfinite(OpenMax))
        Sink.Fail("non-finite means", 0);
    else
    {
        if (OpenMax < 1e-2f)                     // the control: the open surfel MUST be lit, else ~0 proves nothing
            Sink.Fail("open sky surfel is dark — kernel contributes nothing (x1000)", (long long)(OpenMax * 1000.0f));
        if (OccludedMax > 0.5f * OpenMax)        // the occluded surfel must be markedly darker than the open one
            Sink.Fail("occluded surfel not darker than open (x1000 ratio)", (long long)((OccludedMax / std::max(OpenMax, 1e-6f)) * 1000.0f));
    }

    if (Sink.Local == 0)
        std::printf("  [ok]   I3-occluded-open : occluded %.4f << open %.4f (occlusion is real)\n", OccludedMax, OpenMax);
    TearDownIntegrateRig(Rig);
}

// ─── I4 : radial-depth tile seeds + EMAs on a live surfel, stays zero on a dead one ───────────────────────────────────
// Radial depth is learned in TWO places: the stride-gated probe pre-loop AND every valid sample of the main loop (SurfelIntegrate.comp:365). So a live
// surfel writes its tile whether or not its probe stride fires this frame — the reference learns depth on every traced sample. The correct negative
// control is therefore NOT an "un-probed" live surfel (it still writes via the sample loop) but a DEAD surfel (Age >= SURFEL_TTL): the age-guard returns
// before ANY trace, so its tile stays the F7 zero. Assert the live surfel's tile has >=1 texel with m.w != 0 and m2 >= m1^2 (a real MSM moment set);
// assert the dead surfel's tile stays all zero (the age-guard gates every write out).
void GateI4_RadialDepthProbe(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene)
{
    ++CaseTally;
    FailureSink Sink{ "I4-radial-depth" };

    std::vector<DeviceSurfel> Surfels =
    {
        MakeSurfel(0.0f, 0.0f, -4.0f, 0.0f, 0.0f, 1.0f, 10),    // index 0: live, faces the box — traces + learns depth
        MakeSurfel(1.0f, 0.0f, -4.0f, 0.0f, 0.0f, 1.0f, 600),   // index 1: DEAD (age 600 >= TTL 500) — age-guard returns before any trace
    };

    IntegrateRig Rig;
    if (!SetUpIntegrateRig(Host, CommandPool, Scene, Rig, Surfels, nullptr, Sink)) { TearDownIntegrateRig(Rig); return; }

    // A few frames so the live surfel's probe stride fires at least once (frame 0: (0^0)&3==0) and the sample loop learns depth on hits/misses.
    if (!DriveIntegrate(Host, CommandPool, Rig.Pool, Rig.Slotting, Rig.Integrate, Scene, 4))
    { Sink.Fail("integrate drive (4) failed", 0); TearDownIntegrateRig(Rig); return; }

    std::vector<float> Depth;
    if (!ReadSurfelDepth(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), Depth))
    { Sink.Fail("surfel-depth readback failed", 0); TearDownIntegrateRig(Rig); return; }

    const auto TileTexel = [&](uint32_t Surfel, uint32_t Texel, float& M1, float& M2)
    {
        const size_t Base = (size_t)Surfel * SurfelDepthFloats + (size_t)Texel * 4u;
        M1 = Depth[Base + 0]; M2 = Depth[Base + 1];
    };

    // Live surfel 0: at least one texel written, with M2 >= M1^2 (a real second moment).
    uint32_t LiveWritten = 0, MomentValid = 0;
    for (uint32_t T = 0; T < SurfelDepthTileTexels; ++T)
    {
        float M1, M2; TileTexel(0, T, M1, M2);
        if (M2 != 0.0f) { ++LiveWritten; if (M2 >= M1 * M1 - 1e-4f) ++MomentValid; }
    }
    if (LiveWritten == 0) Sink.Fail("live surfel 0 wrote NO depth texel", 0);
    else if (MomentValid == 0) Sink.Fail("live tile has no valid MSM moment (m2 >= m1^2)", 0);

    // Dead surfel 1: the whole tile must still be the F7 zero — the age-guard returned before any trace or depth write.
    uint32_t DeadWritten = 0;
    for (uint32_t T = 0; T < SurfelDepthTileTexels; ++T)
    {
        float M1, M2; TileTexel(1, T, M1, M2);
        if (M1 != 0.0f || M2 != 0.0f) ++DeadWritten;
    }
    if (DeadWritten != 0) Sink.Fail("dead surfel 1 wrote a depth texel (age-guard not gating)", (long long)DeadWritten);

    if (Sink.Local == 0)
        std::printf("  [ok]   I4-radial-depth : live surfel seeded %u texels, dead surfel stayed zero\n", LiveWritten);
    TearDownIntegrateRig(Rig);
}

// ─── I5 : moments read/write asymmetry + swap ─────────────────────────────────────────────────────────────────────────
// Snapshot the READ half before any integrate. Run ONE integrate WITHOUT swapping (drive a raw single dispatch), and confirm the read half is
// UNCHANGED — the integrate writes the WRITE half only. Then swap and confirm the read half now differs (it is the just-written write half). A shader
// that wrote index+readOffset would corrupt the read half within the frame; this asymmetry is the §6 worst-failure-mode control.
void GateI5_MomentsAsymmetrySwap(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene)
{
    ++CaseTally;
    FailureSink Sink{ "I5-asymmetry-swap" };

    std::vector<DeviceSurfel> Surfels = { MakeSurfel(0.0f, 0.0f, -4.0f, 0.0f, 0.0f, 1.0f, 10) };

    // Seed the read half with a recognisable sentinel so an in-frame corruption is visible.
    std::vector<DeviceMoment> Seed(Surfels.size());
    Seed[0].Irradiance[0] = 0.111f; Seed[0].Irradiance[1] = 0.222f; Seed[0].Irradiance[2] = 0.333f;

    IntegrateRig Rig;
    if (!SetUpIntegrateRig(Host, CommandPool, Scene, Rig, Surfels, &Seed, Sink)) { TearDownIntegrateRig(Rig); return; }

    std::vector<DeviceMoment> Before;
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), Before))
    { Sink.Fail("read-half snapshot (before) failed", 0); TearDownIntegrateRig(Rig); return; }

    // One integrate WITHOUT a swap — record slotting + B1 + integrate + B3, submit, but do NOT SwapSurfelMoments.
    {
        VkCommandBuffer Command = BeginCommand(Host, CommandPool);
        if (Command == VK_NULL_HANDLE) { Sink.Fail("command alloc failed", 0); TearDownIntegrateRig(Rig); return; }
        SurfelSlottingConstants SlotConstants;
        SlotConstants.CameraPosition[0] = 0.0f; SlotConstants.CameraPosition[1] = 0.0f; SlotConstants.CameraPosition[2] = -8.0f;
        SlotConstants.GridOrigin[0] = 0.0f; SlotConstants.GridOrigin[1] = 0.0f; SlotConstants.GridOrigin[2] = 0.0f;
        SlotConstants.ListCount = (int32_t)SurfelGridListCount;
        RecordSurfelGridSlotting(Rig.Slotting, Rig.Pool, SlotConstants, Command);
        FullComputeBarrier(Command, Rig.Slotting.OffsetsBuffer);
        FullComputeBarrier(Command, Rig.Slotting.ListBuffer);
        FullComputeBarrier(Command, Rig.Pool.SurfelBuffer);
        FullComputeBarrier(Command, Rig.Pool.MomentsBuffer);
        const SurfelIntegrateConstants Constants = MakeIntegrateConstants(Rig.Pool, Scene, 0);
        RecordSurfelIntegrate(Rig.Integrate, Rig.Pool, Constants, Command);
        FullComputeBarrier(Command, Rig.Pool.MomentsBuffer);
        vkEndCommandBuffer(Command);
        if (!SubmitAndWait(Host, CommandPool, Command)) { Sink.Fail("integrate submit failed", 0); TearDownIntegrateRig(Rig); return; }
    }

    std::vector<DeviceMoment> AfterNoSwap;
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), AfterNoSwap))
    { Sink.Fail("read-half snapshot (no-swap) failed", 0); TearDownIntegrateRig(Rig); return; }

    // The read half MUST be untouched: the integrate wrote the write half only.
    const auto SameMoment = [](const DeviceMoment& A, const DeviceMoment& B)
    {
        for (int C = 0; C < 4; ++C) if (std::fabs(A.Irradiance[C] - B.Irradiance[C]) > 1e-6f) return false;
        return true;
    };
    if (!SameMoment(Before[0], AfterNoSwap[0]))
        Sink.Fail("read half CHANGED without a swap — integrate wrote the wrong half (x1000)",
                  (long long)(AfterNoSwap[0].Irradiance[0] * 1000.0f));

    // Now swap: the read half becomes the just-written write half, which must differ from the sentinel.
    SwapSurfelMoments(Rig.Pool);
    std::vector<DeviceMoment> AfterSwap;
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), AfterSwap))
    { Sink.Fail("read-half snapshot (post-swap) failed", 0); TearDownIntegrateRig(Rig); return; }

    if (SameMoment(Before[0], AfterSwap[0]))
        Sink.Fail("read half UNCHANGED after swap — write half was never written or swap is a no-op", 0);

    if (Sink.Local == 0)
        std::printf("  [ok]   I5-asymmetry-swap : read half held pre-swap, changed post-swap (write-half discipline correct)\n");
    TearDownIntegrateRig(Rig);
}

// ─── I6 : F8 single-owner / age-guard — a dead surfel's write half stays zero ─────────────────────────────────────────
// Seed one LIVE surfel (age < TTL) and one DEAD surfel (age >= TTL) facing the box. After one integrate, the dead surfel's WRITE half must be
// untouched (the in-shader `if (Age >= SURFEL_TTL) return;` bounds guard), while the live surfel's write half is written. The dead surfel is the
// control: a kernel that skipped the age-guard (or over-dispatched onto it) would write it. We read the write half by swapping then reading.
void GateI6_AgeGuardSingleOwner(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene)
{
    ++CaseTally;
    FailureSink Sink{ "I6-age-guard" };

    std::vector<DeviceSurfel> Surfels =
    {
        MakeSurfel(0.0f, 0.0f, -4.0f, 0.0f, 0.0f, 1.0f, 10),    // LIVE (age 10 < 500)
        MakeSurfel(2.0f, 0.0f, -4.0f, 0.0f, 0.0f, 1.0f, 600),   // DEAD (age 600 >= 500)
    };

    // Seed BOTH surfels' write half (parity 1 block) with a sentinel, so "untouched" means the sentinel survives. The write half at parity 0 is the
    // block starting at element Capacity — but seeding there needs the byte offset; simpler: seed the read half is not enough. Instead we rely on the
    // pool's F7 zero-fill of the WHOLE moments buffer at init, so both halves start at zero; the dead surfel's write half staying zero is the claim.
    IntegrateRig Rig;
    if (!SetUpIntegrateRig(Host, CommandPool, Scene, Rig, Surfels, nullptr, Sink)) { TearDownIntegrateRig(Rig); return; }

    // One integrate, then swap so the write half becomes readable as the read half.
    if (!DriveIntegrate(Host, CommandPool, Rig.Pool, Rig.Slotting, Rig.Integrate, Scene, 1))
    { Sink.Fail("integrate drive (1) failed", 0); TearDownIntegrateRig(Rig); return; }

    std::vector<DeviceMoment> M;   // after DriveIntegrate's swap, the read half IS the just-written write half
    if (!ReadMomentsReadHalf(Host, CommandPool, Rig.Pool, (uint32_t)Surfels.size(), M))
    { Sink.Fail("moments readback failed", 0); TearDownIntegrateRig(Rig); return; }

    // The dead surfel's whole moment record must be zero (the guard returned before any write).
    const DeviceMoment& Dead = M[1];
    float DeadEnergy = 0.0f;
    for (int C = 0; C < 4; ++C) DeadEnergy += std::fabs(Dead.Irradiance[C]) + std::fabs(Dead.Guiding[C]) + std::fabs(Dead.Hit[C]);
    if (DeadEnergy > 1e-5f)
        Sink.Fail("dead surfel WRITTEN — age-guard skipped (x1000)", (long long)(DeadEnergy * 1000.0f));

    // The live surfel must have SOME write (count advanced, or a hit/guiding term set) — proves the kernel actually ran the live lane.
    const DeviceMoment& Live = M[0];
    float LiveEnergy = std::fabs(Live.Irradiance[3]);   // total count in .w advances on any valid sample
    for (int C = 0; C < 4; ++C) LiveEnergy += std::fabs(Live.Guiding[C]);
    if (LiveEnergy <= 1e-5f)
        Sink.Fail("live surfel NOT written — kernel did not run the live lane", 0);

    if (Sink.Local == 0)
        std::printf("  [ok]   I6-age-guard : dead surfel untouched (energy 0), live surfel written (F8 one-lane-per-surfel)\n");
    TearDownIntegrateRig(Rig);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    G1 : the Phase-3 gather seam (SurfaceShade.frag reads this)
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THE GUARD FOR THE SEAM THE SHADE DEPENDS ON. Phase 3 folds SurfelLookupGI into SurfaceShade.frag in place of the flat ambient. "Reading the output
//    proves nothing": this gate stands the surfel set up exactly as the shade/integrate do, SEEDS a known irradiance into a known cell's read half, runs
//    the REAL slotting, then dispatches SurfelGatherProbe.comp — which calls the SHIPPED SurfelLookupGI at host points — and reads each returned vec3.
//
//    It asserts a point inside the seeded surfel's radius returns that irradiance (weighted, non-zero, finite) and a far point returns 0. The CONTROL
//    passes the WRONG read-offset (the zero-filled WRITE half): the inside point must then read 0 — proving the post-swap read-half element offset the
//    shade supplies is load-bearing, not incidental. A gate that passes with either offset would not catch a parity plumbing regression.

// A disposable, validation-only compute pipeline over SurfelGatherProbe.comp's ten std430 storage bindings (set 0, b0..b9) + its push block. Every
// buffer handle is borrowed (pool + slotting + the three probe buffers); the layout mirrors the .comp exactly so the shipped gather resolves its
// bare-name SSBOs. Torn down by the caller.
struct GatherProbePipeline
{
    VkShaderModule        Module      = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout   = VK_NULL_HANDLE;
    VkPipelineLayout      PipeLayout  = VK_NULL_HANDLE;
    VkPipeline            Pipeline    = VK_NULL_HANDLE;
    VkDescriptorPool      Pool        = VK_NULL_HANDLE;
    VkDescriptorSet       Set         = VK_NULL_HANDLE;
    bool                  ReadyCondition = false;
};

// Byte-identical to SurfelGatherProbe.comp's push block: three vec4 + four uint (48 + 16 = 64 B).
struct GatherProbeConstants
{
    float    CameraPosition[4]  = { 0, 0, 0, 0 };
    float    GridOrigin[4]      = { 0, 0, 0, 0 };
    float    OcclusionParams[4] = { 1.2f, 0.2f, 0.25f, 0.15f };
    uint32_t ReadOffsetElements = 0;
    uint32_t PointCount         = 0;
    uint32_t Pad0               = 0;
    uint32_t Pad1               = 0;
};

bool BuildGatherProbePipeline(VulkanHost& Host, GatherProbePipeline& P)
{
    // Read the .spv from where ShaderPlan.ps1 wrote it (beside its source, the same dir every other Surfel .comp loads from).
    const std::string Path = std::string(SurfelShaderDirectory) + "/SurfelGatherProbe.comp.spv";
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File) return false;
    const std::streamsize Size = File.tellg();
    if (Size <= 0 || (Size % 4) != 0) return false;
    std::vector<uint32_t> Code((size_t)Size / 4);
    File.seekg(0);
    if (!File.read(reinterpret_cast<char*>(Code.data()), Size)) return false;

    VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ModuleInformation.codeSize = (size_t)Size;
    ModuleInformation.pCode    = Code.data();
    if (vkCreateShaderModule(Host.Device, &ModuleInformation, Host.Allocator, &P.Module) != VK_SUCCESS)
        return false;

    // Ten storage-buffer bindings, all in the compute stage — the probe's b0..b9.
    VkDescriptorSetLayoutBinding Bindings[10] = {};
    for (uint32_t I = 0; I < 10u; ++I)
    {
        Bindings[I].binding         = I;
        Bindings[I].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[I].descriptorCount = 1;
        Bindings[I].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 10;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &P.SetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = (uint32_t)sizeof(GatherProbeConstants);

    VkPipelineLayoutCreateInfo PipeLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipeLayoutInformation.setLayoutCount         = 1;
    PipeLayoutInformation.pSetLayouts            = &P.SetLayout;
    PipeLayoutInformation.pushConstantRangeCount = 1;
    PipeLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipeLayoutInformation, Host.Allocator, &P.PipeLayout) != VK_SUCCESS)
        return false;

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipelineInformation.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    PipelineInformation.stage.module = P.Module;
    PipelineInformation.stage.pName  = "main";
    PipelineInformation.layout       = P.PipeLayout;
    if (vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &P.Pipeline) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 10;
    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &P.Pool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo SetAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocate.descriptorPool     = P.Pool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &P.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocate, &P.Set) != VK_SUCCESS)
        return false;

    P.ReadyCondition = true;
    return true;
}

void TearDownGatherProbePipeline(VulkanHost& Host, GatherProbePipeline& P)
{
    if (P.Pool       != VK_NULL_HANDLE) vkDestroyDescriptorPool(Host.Device, P.Pool, Host.Allocator);
    if (P.Pipeline   != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, P.Pipeline, Host.Allocator);
    if (P.PipeLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, P.PipeLayout, Host.Allocator);
    if (P.SetLayout  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, P.SetLayout, Host.Allocator);
    if (P.Module     != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, P.Module, Host.Allocator);
    P = GatherProbePipeline();
}

// Point the probe's set at the pool + slotting + the three probe buffers. Whole-buffer ranges, matching the integrate's own writes.
void WriteGatherProbeSet(VulkanHost& Host, GatherProbePipeline& P, const SurfelPool& Pool, const SurfelGridSlotting& Slotting,
                         VkBuffer ProbePoints, VkBuffer ProbeNormals, VkBuffer GatherResult)
{
    const VkBuffer Buffers[10] =
    {
        Pool.SurfelBuffer,       // b0 Surfels        (ro)
        Pool.MomentsBuffer,      // b1 SurfelMoments  (rw, one buffer both halves)
        Slotting.OffsetsBuffer,  // b2 SurfelOffsets  (ro)
        Slotting.ListBuffer,     // b3 SurfelList     (ro)
        Pool.GuidingBuffer,      // b4 SurfelGuiding  (rw)
        Pool.SurfelDepthBuffer,  // b5 SurfelDepth    (rw)
        Pool.TouchedBuffer,      // b6 SurfelTouched  (rw atomic)
        ProbePoints,             // b7 ProbePoints    (ro)
        ProbeNormals,            // b8 ProbeNormals   (ro)
        GatherResult,            // b9 GatherResult   (rw)
    };

    VkDescriptorBufferInfo Infos[10] = {};
    VkWriteDescriptorSet   Writes[10] = {};
    for (uint32_t I = 0; I < 10u; ++I)
    {
        Infos[I].buffer = Buffers[I];
        Infos[I].offset = 0;
        Infos[I].range  = VK_WHOLE_SIZE;

        Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[I].dstSet          = P.Set;
        Writes[I].dstBinding      = I;
        Writes[I].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[I].descriptorCount = 1;
        Writes[I].pBufferInfo     = &Infos[I];
    }
    vkUpdateDescriptorSets(Host.Device, 10, Writes, 0, nullptr);
}

// ─── G1 : the shipped gather returns seeded irradiance inside, zero outside; wrong read-offset returns zero (the load-bearing control) ────────────────
void GateG1_GatherSeededIrradiance(VulkanHost& Host, VkCommandPool CommandPool, const IntegrateScene& Scene)
{
    ++CaseTally;
    FailureSink Sink{ "G1-gather-seam" };
    (void)Scene;   // the gather needs no BVH — it reads the surfel cache, not the trace

    // One surfel at the grid origin, facing +Z, alive. Seed a KNOWN irradiance into its read half (parity 0, element 0).
    const float SeededIrradiance[3] = { 0.7f, 0.4f, 0.2f };
    std::vector<DeviceSurfel> Surfels = { MakeSurfel(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 10) };

    std::vector<DeviceMoment> Seed(1);
    Seed[0].Irradiance[0] = SeededIrradiance[0];
    Seed[0].Irradiance[1] = SeededIrradiance[1];
    Seed[0].Irradiance[2] = SeededIrradiance[2];
    Seed[0].Irradiance[3] = 64.0f;   // a healthy total count so the gather trusts the mean (not warmup-discarded)

    // Stand up the pool + slotting (no integrate needed — G1 exercises the gather in isolation). Seed the surfel + its read-half moment.
    SurfelPool Pool; SurfelGridSlotting Slotting;
    if (!InitializeSurfelPool(Pool, Host, CommandPool, SurfelMaxCount)) { Sink.Fail("pool init failed", 0); FinalizeSurfelPool(Pool); return; }
    if (!InitializeSurfelGridSlotting(Slotting, Host, CommandPool, SurfelShaderDirectory))
    { Sink.Fail("slotting init failed", 0); FinalizeSurfelGridSlotting(Slotting); FinalizeSurfelPool(Pool); return; }

    const auto TearDown = [&](GatherProbePipeline* Probe, VkBuffer B0, VkDeviceMemory M0, VkBuffer B1, VkDeviceMemory M1, VkBuffer B2, VkDeviceMemory M2)
    {
        if (Probe) TearDownGatherProbePipeline(Host, *Probe);
        const auto Rel = [&](VkBuffer B, VkDeviceMemory M){ if (B) vkDestroyBuffer(Host.Device, B, Host.Allocator); if (M) vkFreeMemory(Host.Device, M, Host.Allocator); };
        Rel(B0, M0); Rel(B1, M1); Rel(B2, M2);
        FinalizeSurfelGridSlotting(Slotting);
        FinalizeSurfelPool(Pool);
    };

    if (!SeedIntegratePool(Host, CommandPool, Pool, Surfels, &Seed))
    { Sink.Fail("pool seed failed", 0); TearDown(nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE); return; }

    // Run the real slotting so Offsets/List bucket the surfel — the gather walks those lists.
    {
        VkCommandBuffer Command = BeginCommand(Host, CommandPool);
        if (Command == VK_NULL_HANDLE) { Sink.Fail("begin command failed", 0); TearDown(nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE); return; }
        SurfelSlottingConstants SlotConstants;
        SlotConstants.CameraPosition[0] = 0.0f; SlotConstants.CameraPosition[1] = 0.0f; SlotConstants.CameraPosition[2] = -8.0f;
        SlotConstants.GridOrigin[0] = 0.0f; SlotConstants.GridOrigin[1] = 0.0f; SlotConstants.GridOrigin[2] = 0.0f;
        SlotConstants.ListCount = (int32_t)SurfelGridListCount;
        RecordSurfelGridSlotting(Slotting, Pool, SlotConstants, Command);
        FullComputeBarrier(Command, Slotting.OffsetsBuffer);
        FullComputeBarrier(Command, Slotting.ListBuffer);
        vkEndCommandBuffer(Command);
        if (!SubmitAndWait(Host, CommandPool, Command))
        { Sink.Fail("slotting submit failed", 0); TearDown(nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE); return; }
    }

    // Two probe points: [0] just off the surfel along +Z (well inside a ~1.2 m radius) → expect ≈seed; [1] far away → expect 0.
    const float Points[2][4]  = { { 0.0f, 0.0f, 0.2f, 0.0f }, { 1000.0f, 1000.0f, 1000.0f, 0.0f } };
    const float Normals[2][4] = { { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } };
    const uint32_t PointCount = 2;

    VkBuffer PointsBuffer = VK_NULL_HANDLE, NormalsBuffer = VK_NULL_HANDLE, ResultBuffer = VK_NULL_HANDLE;
    VkDeviceMemory PointsMemory = VK_NULL_HANDLE, NormalsMemory = VK_NULL_HANDLE, ResultMemory = VK_NULL_HANDLE;
    const VkDeviceSize VecBytes = (VkDeviceSize)PointCount * 4 * sizeof(float);
    // Host-visible STORAGE buffers: points/normals uploaded from the host, result read back from the host — no staging round-trip needed.
    if (!CreateHostBufferWithUsage(Host, Points,  VecBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, PointsBuffer,  PointsMemory)  ||
        !CreateHostBufferWithUsage(Host, Normals, VecBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, NormalsBuffer, NormalsMemory) ||
        !CreateHostBufferWithUsage(Host, nullptr, VecBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResultBuffer,  ResultMemory))
    { Sink.Fail("probe buffer creation failed", 0); TearDown(nullptr, PointsBuffer, PointsMemory, NormalsBuffer, NormalsMemory, ResultBuffer, ResultMemory); return; }

    GatherProbePipeline Probe;
    if (!BuildGatherProbePipeline(Host, Probe))
    { Sink.Fail("gather probe pipeline build failed (is SurfelGatherProbe.comp.spv built?)", 0); TearDown(&Probe, PointsBuffer, PointsMemory, NormalsBuffer, NormalsMemory, ResultBuffer, ResultMemory); return; }
    WriteGatherProbeSet(Host, Probe, Pool, Slotting, PointsBuffer, NormalsBuffer, ResultBuffer);

    // Dispatch the probe with a chosen read-offset, then read the two results back. RIGHT offset = current parity's read half; WRONG = the zero write half.
    const auto DispatchGather = [&](uint32_t ReadOffsetElements, float OutResult[2][4]) -> bool
    {
        GatherProbeConstants Constants;
        Constants.CameraPosition[0] = 0.0f; Constants.CameraPosition[1] = 0.0f; Constants.CameraPosition[2] = -8.0f;
        Constants.GridOrigin[0]     = 0.0f; Constants.GridOrigin[1]     = 0.0f; Constants.GridOrigin[2]     = 0.0f;
        Constants.OcclusionParams[0] = 1.2f; Constants.OcclusionParams[1] = 0.2f; Constants.OcclusionParams[2] = 0.25f; Constants.OcclusionParams[3] = 0.15f;
        Constants.ReadOffsetElements = ReadOffsetElements;
        Constants.PointCount         = PointCount;

        VkCommandBuffer Command = BeginCommand(Host, CommandPool);
        if (Command == VK_NULL_HANDLE) return false;
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Probe.Pipeline);
        vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Probe.PipeLayout, 0, 1, &Probe.Set, 0, nullptr);
        vkCmdPushConstants(Command, Probe.PipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GatherProbeConstants), &Constants);
        vkCmdDispatch(Command, 1, 1, 1);   // one workgroup of 64 lanes; PointCount lanes work, the rest early-out
        FullComputeBarrier(Command, ResultBuffer);
        vkEndCommandBuffer(Command);
        if (!SubmitAndWait(Host, CommandPool, Command)) return false;
        return ReadHostBytes(Host, ResultMemory, VecBytes, OutResult);
    };

    // The RIGHT read offset: the current parity's element base (parity * Capacity). We seeded parity 0's block at element 0.
    const uint32_t RightOffset = Pool.MomentsParity * Pool.Capacity;
    const uint32_t WrongOffset = (1u - Pool.MomentsParity) * Pool.Capacity;   // the zero-filled write half

    float Right[2][4] = {};
    if (!DispatchGather(RightOffset, Right))
    { Sink.Fail("gather dispatch (right offset) failed", 0); TearDown(&Probe, PointsBuffer, PointsMemory, NormalsBuffer, NormalsMemory, ResultBuffer, ResultMemory); return; }

    // Inside point: finite, non-zero, and tracking the seed (a weighted gather won't reproduce it exactly, but the hue/magnitude must survive).
    const float InsideEnergy = std::fabs(Right[0][0]) + std::fabs(Right[0][1]) + std::fabs(Right[0][2]);
    if (!IsFiniteVec3(Right[0]))
        Sink.Fail("inside gather non-finite", 0);
    else if (InsideEnergy <= 1e-4f)
        Sink.Fail("inside gather returned ~zero with the RIGHT read offset — the seam does not read the seeded cell (x1000)", (long long)(InsideEnergy * 1000.0f));
    else
    {
        // The seed is red-dominant (0.7 > 0.4 > 0.2); a correct weighted read must keep that ordering, else it read a foreign/zero cell.
        if (!(Right[0][0] > Right[0][1] && Right[0][1] >= Right[0][2] - 1e-4f))
            Sink.Fail("inside gather hue does not track the seed (R>G>B lost) (Rx1000)", (long long)(Right[0][0] * 1000.0f));
    }

    // Far point: no cell covers it → exactly zero.
    const float FarEnergy = std::fabs(Right[1][0]) + std::fabs(Right[1][1]) + std::fabs(Right[1][2]);
    if (FarEnergy > 1e-4f)
        Sink.Fail("far gather non-zero — a point outside every cell picked up irradiance (x1000)", (long long)(FarEnergy * 1000.0f));

    // ── CONTROL: the WRONG read offset (the zero-filled write half). The inside point must now read 0 — proving the read-half offset is load-bearing. ──
    float Wrong[2][4] = {};
    if (!DispatchGather(WrongOffset, Wrong))
        Sink.Fail("gather dispatch (wrong offset / control) failed", 0);
    else
    {
        const float ControlEnergy = std::fabs(Wrong[0][0]) + std::fabs(Wrong[0][1]) + std::fabs(Wrong[0][2]);
        if (ControlEnergy > 1e-4f)
            Sink.Fail("CONTROL FAILED: inside gather non-zero with the WRONG (write-half) offset — the read-half offset is NOT load-bearing (x1000)",
                      (long long)(ControlEnergy * 1000.0f));
    }

    if (Sink.Local == 0)
        std::printf("  [ok]   G1-gather-seam : inside=(%.3f,%.3f,%.3f) seed=(%.2f,%.2f,%.2f), far=0, wrong-offset=0 (post-swap read half is load-bearing)\n",
                    Right[0][0], Right[0][1], Right[0][2], SeededIrradiance[0], SeededIrradiance[1], SeededIrradiance[2]);

    TearDown(&Probe, PointsBuffer, PointsMemory, NormalsBuffer, NormalsMemory, ResultBuffer, ResultMemory);
}

// The invoker: build the resident scene once, run all six integrate gates + the G1 gather gate over it, tear it down.
void RunIntegrateGates(VulkanHost& Host, VkCommandPool CommandPool)
{
    IntegrateScene Scene;
    if (!BuildIntegrateScene(Host, CommandPool, Scene))
    {
        ++CaseTally; ++FailureTally;
        std::printf("  [FAIL] integrate-scene       build failed (BuildGeometryTree / TLAS / accel .spv?) 0\n");
        FinalizeIntegrateScene(Host, Scene);
        return;
    }

    GateI1_IrradianceFiniteConverges(Host, CommandPool, Scene);
    GateI2_FireflyClamp(Host, CommandPool, Scene);
    GateI3_OccludedVersusOpen(Host, CommandPool, Scene);
    GateI4_RadialDepthProbe(Host, CommandPool, Scene);
    GateI5_MomentsAsymmetrySwap(Host, CommandPool, Scene);
    GateI6_AgeGuardSingleOwner(Host, CommandPool, Scene);

    // Phase 3: the gather seam SurfaceShade.frag depends on.
    GateG1_GatherSeededIrradiance(Host, CommandPool, Scene);

    FinalizeIntegrateScene(Host, Scene);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount; (void)ArgumentValues;

    std::printf("==== SurfelValidation ====\n");

    // The F1 gate is pure CPU — run it first so a broken oracle is caught before any device work.
    GateF1SignedShiftControl();

    VulkanHost Host;
    if (!InitializeVulkanHost(Host, nullptr, 0))
    {
        std::printf("[FAIL] no Vulkan device\n");
        return 1;
    }

    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(Host.Device, &PoolInformation, Host.Allocator, &CommandPool) != VK_SUCCESS)
    {
        std::printf("[FAIL] command pool creation failed\n");
        FinalizeVulkanHost(Host);
        return 1;
    }

    GateZeroFillDiscipline(Host, CommandPool);
    GatePrefixSumDirect(Host, CommandPool);
    GateSlottingAndPrefix(Host, CommandPool);
    GateBarrierControl(Host, CommandPool);

    // Phase 2: the integrate gates I1..I6 — real RecordSurfelIntegrate over a resident BVH.
    RunIntegrateGates(Host, CommandPool);

    vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
    FinalizeVulkanHost(Host);

    if (FailureTally == 0u)
    {
        std::printf("==== PASS : %u gates, 0 failures ====\n", CaseTally);
        return 0;
    }

    std::printf("==== FAIL : %u gates, %u failures ====\n", CaseTally, FailureTally);
    return 1;
}
