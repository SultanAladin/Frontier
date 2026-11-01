/*==========================================================================================================================================
                                             VULKAN COMPUTE EXCHANGE DEFINITION
==========================================================================================================================================*/
// 🧩 Dynamic-loaded Vulkan 1.3 offscreen & shared-device compute exchange with atomic pipeline archive checkpointing,
//    2D RGBA8_UNORM presentation views, external command-stream recording, and hardware GPU timestamp queries.

#include "Engine/DeviceExchange/VulkanComputeExchange.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Frontier
{
namespace
{

constexpr VkDeviceSize MaximumArchiveLoadBytes = 64ull * 1024ull * 1024ull;
constexpr uint32_t     StandaloneQueryOffset   = VulkanComputeExchange::ExternalRingCount *
                                             VulkanComputeExchange::MaxTimedDispatchesSlot * 2u;
constexpr uint32_t TotalTimestampQueries = StandaloneQueryOffset + 2u;

void* OpenDynamicLibrary(const char* Path) noexcept
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(Path));
#else
    return dlopen(Path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
#endif
}

void CloseDynamicLibrary(void* Handle) noexcept
{
    if (!Handle)
    {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(Handle));
#else
    dlclose(Handle);
#endif
}

void* LookupDynamicSymbol(void* Handle, const char* Name) noexcept
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(Handle), Name));
#else
    return dlsym(Handle, Name);
#endif
}

uint64_t ComputeFnv1a64(const uint8_t* Bytes, size_t ByteCount) noexcept
{
    uint64_t Hash = 14695981039346656037ull;
    for (size_t Index = 0u; Index < ByteCount; ++Index)
    {
        Hash ^= static_cast<uint64_t>(Bytes[Index]);
        Hash *= 1099511628211ull;
    }
    return Hash;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            FP16 <-> FP32 CONVERSION
//------------------------------------------------------------------------------------------------------------------------

uint16_t PackHalfFloat(float Scalar) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Scalar, sizeof(uint32_t));
    const uint32_t Sign = (Bits >> 16u) & 0x8000u;
    int32_t        Exp  = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t       Mant = Bits & 0x007FFFFFu;

    if (Exp <= 0)
    {
        if (Exp < -10)
        {
            return static_cast<uint16_t>(Sign);
        }
        Mant = (Mant | 0x00800000u) >> static_cast<uint32_t>(1 - Exp);
        return static_cast<uint16_t>(Sign | ((Mant + 0x00001000u) >> 13u));
    }
    if (Exp >= 31)
    {
        return static_cast<uint16_t>(Sign | 0x7C00u);
    }
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exp) << 10u) | ((Mant + 0x00001000u) >> 13u));
}

float UnpackHalfFloat(uint16_t Bits) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(Bits & 0x8000u) << 16u;
    const uint32_t Exp  = (Bits >> 10u) & 0x1Fu;
    const uint32_t Mant = Bits & 0x03FFu;

    uint32_t OutBits = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u)
        {
            OutBits = Sign;
        }
        else
        {
            uint32_t Subnormal = Mant;
            int32_t  ShiftExp  = -14;
            while ((Subnormal & 0x0400u) == 0u)
            {
                Subnormal <<= 1u;
                --ShiftExp;
            }
            Subnormal &= 0x03FFu;
            OutBits = Sign | (static_cast<uint32_t>(ShiftExp + 127) << 23u) | (Subnormal << 13u);
        }
    }
    else if (Exp == 31u)
    {
        OutBits = Sign | 0x7F800000u | (Mant << 13u);
    }
    else
    {
        OutBits = Sign | ((Exp + 127u - 15u) << 23u) | (Mant << 13u);
    }
    float Result = 0.0f;
    std::memcpy(&Result, &OutBits, sizeof(float));
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             LIFECYCLE & INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

VulkanComputeExchange::~VulkanComputeExchange()
{
    Shutdown();
}

bool VulkanComputeExchange::Initialize(const std::string& ExplicitDriverLibraryPath,
                                       std::string*       OutDiagnostic,
                                       const std::string& PipelineArchivePath)
{
    Shutdown();

    std::vector<std::string> Candidates;
    if (!ExplicitDriverLibraryPath.empty())
    {
        Candidates.push_back(ExplicitDriverLibraryPath);
    }
    if (const char* EnvLib = std::getenv("FRONTIER_VULKAN_DRIVER"))
    {
        if (EnvLib[0] != '\0')
        {
            Candidates.push_back(EnvLib);
        }
    }
#if defined(_WIN32)
    Candidates.push_back("vulkan-1.dll");
#else
    Candidates.push_back("ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1");
    Candidates.push_back("../ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1");
    Candidates.push_back("../../ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1");
    Candidates.push_back("libvulkan.so.1");
#endif

    std::string ChosenPath;
    for (const std::string& Candidate : Candidates)
    {
#if !defined(_WIN32)
        std::error_code             ErrorCode;
        const std::filesystem::path LibPath = std::filesystem::absolute(Candidate, ErrorCode);
        if (!ErrorCode && std::filesystem::exists(LibPath, ErrorCode))
        {
            const std::filesystem::path IcdJson = LibPath.parent_path() / "vk_swiftshader_icd.json";
            if (std::filesystem::exists(IcdJson, ErrorCode))
            {
                setenv("VK_ICD_FILENAMES", IcdJson.string().c_str(), 1);
            }
            LibraryHandle_ = OpenDynamicLibrary(LibPath.string().c_str());
            if (LibraryHandle_)
            {
                ChosenPath = LibPath.string();
                break;
            }
        }
#endif
        LibraryHandle_ = OpenDynamicLibrary(Candidate.c_str());
        if (LibraryHandle_)
        {
            ChosenPath = Candidate;
            break;
        }
    }

    if (!LibraryHandle_)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Could not open Vulkan loader library";
        }
        return false;
    }

    pfnGetInstanceProcAddr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        LookupDynamicSymbol(LibraryHandle_, "vkGetInstanceProcAddr"));
    if (!pfnGetInstanceProcAddr_)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkGetInstanceProcAddr missing in " + ChosenPath;
        }
        Shutdown();
        return false;
    }

    pfnCreateInstance_ = reinterpret_cast<PFN_vkCreateInstance>(
        pfnGetInstanceProcAddr_(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!pfnCreateInstance_)
    {
        Shutdown();
        return false;
    }

    VkApplicationInfo AppSpec{};
    AppSpec.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppSpec.pApplicationName   = "FrontierPlanetaryCompute";
    AppSpec.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    AppSpec.pEngineName        = "Frontier";
    AppSpec.engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0);
    AppSpec.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo InstanceSpec{};
    InstanceSpec.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceSpec.pApplicationInfo = &AppSpec;

    if (pfnCreateInstance_(&InstanceSpec, nullptr, &Instance_) != VK_SUCCESS || Instance_ == VK_NULL_HANDLE)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateInstance failed";
        }
        Shutdown();
        return false;
    }

#define RESOLVE_VK_INST(Field, Symbol)                                                       \
    Field = reinterpret_cast<decltype(Field)>(pfnGetInstanceProcAddr_(Instance_, Symbol));   \
    if (!Field)                                                                              \
    {                                                                                        \
        if (OutDiagnostic) *OutDiagnostic = std::string("Missing Vulkan symbol: ") + Symbol; \
        Shutdown();                                                                          \
        return false;                                                                        \
    }

    RESOLVE_VK_INST(pfnDestroyInstance_, "vkDestroyInstance");
    RESOLVE_VK_INST(pfnEnumeratePhysicalDevices_, "vkEnumeratePhysicalDevices");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceProperties_, "vkGetPhysicalDeviceProperties");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceMemoryProperties_, "vkGetPhysicalDeviceMemoryProperties");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceQueueFamilyProperties_, "vkGetPhysicalDeviceQueueFamilyProperties");
    RESOLVE_VK_INST(pfnCreateDevice_, "vkCreateDevice");

#undef RESOLVE_VK_INST

    uint32_t DeviceCount = 0u;
    if (pfnEnumeratePhysicalDevices_(Instance_, &DeviceCount, nullptr) != VK_SUCCESS || DeviceCount == 0u)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "No Vulkan physical devices enumerated";
        }
        Shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> Devices(DeviceCount);
    pfnEnumeratePhysicalDevices_(Instance_, &DeviceCount, Devices.data());
    PhysicalDevice_ = Devices[0];

    VkPhysicalDeviceProperties Props{};
    pfnGetPhysicalDeviceProperties_(PhysicalDevice_, &Props);
    DeviceName_                 = Props.deviceName;
    VendorId_                   = Props.vendorID;
    DeviceId_                   = Props.deviceID;
    DriverVersion_              = Props.driverVersion;
    ApiVersion_                 = Props.apiVersion;
    TimestampPeriodNanoseconds_ = Props.limits.timestampPeriod;
    pfnGetPhysicalDeviceMemoryProperties_(PhysicalDevice_, &MemoryProperties_);

    uint32_t FamilyCount = 0u;
    pfnGetPhysicalDeviceQueueFamilyProperties_(PhysicalDevice_, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    pfnGetPhysicalDeviceQueueFamilyProperties_(PhysicalDevice_, &FamilyCount, Families.data());

    bool     FoundCompute      = false;
    bool     FoundGraphicsBoth = false;
    uint32_t ChosenFamily      = 0u;
    for (uint32_t FamilyIndex = 0u; FamilyIndex < FamilyCount; ++FamilyIndex)
    {
        const VkQueueFlags Flags = Families[FamilyIndex].queueFlags;
        if ((Flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        {
            ChosenFamily      = FamilyIndex;
            FoundCompute      = true;
            FoundGraphicsBoth = (Props.limits.maxColorAttachments >= 6u);
            break;
        }
        if (!FoundCompute && (Flags & VK_QUEUE_COMPUTE_BIT) != 0u)
        {
            ChosenFamily = FamilyIndex;
            FoundCompute = true;
        }
    }
    if (!FoundCompute)
    {
        Shutdown();
        return false;
    }
    QueueFamily_         = ChosenFamily;
    GraphicsSupported_   = FoundGraphicsBoth;
    TimestampsSupported_ = (Families[ChosenFamily].timestampValidBits > 0u) && (TimestampPeriodNanoseconds_ > 0.0f);

    const float             Priority = 1.0f;
    VkDeviceQueueCreateInfo QueueSpec{};
    QueueSpec.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueSpec.queueFamilyIndex = QueueFamily_;
    QueueSpec.queueCount       = 1u;
    QueueSpec.pQueuePriorities = &Priority;

    VkDeviceCreateInfo DeviceSpec{};
    DeviceSpec.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceSpec.queueCreateInfoCount = 1u;
    DeviceSpec.pQueueCreateInfos    = &QueueSpec;

    if (pfnCreateDevice_(PhysicalDevice_, &DeviceSpec, nullptr, &Device_) != VK_SUCCESS || Device_ == VK_NULL_HANDLE)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateDevice failed";
        }
        Shutdown();
        return false;
    }
    BorrowedDeviceEnabled_ = false;

    auto GetDeviceProc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(pfnGetInstanceProcAddr_(Instance_, "vkGetDeviceProcAddr"));
    if (!GetDeviceProc)
    {
        Shutdown();
        return false;
    }

    return ResolveDeviceFunctionsAndPools(GetDeviceProc, PipelineArchivePath, OutDiagnostic);
}

bool VulkanComputeExchange::AttachBorrowedDevice(const BorrowedVulkanDeviceSpec& BorrowedSpec,
                                                 const std::string&              PipelineArchivePath,
                                                 std::string*                    OutDiagnostic)
{
    Shutdown();
    if (BorrowedSpec.Device == VK_NULL_HANDLE || BorrowedSpec.PhysicalDevice == VK_NULL_HANDLE ||
        BorrowedSpec.Queue == VK_NULL_HANDLE || !BorrowedSpec.ResolveDevice || !BorrowedSpec.QueryMemory)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Borrowed Vulkan device specification incomplete";
        }
        return false;
    }

    BorrowedDeviceEnabled_ = true;
    GraphicsSupported_     = true;
    PhysicalDevice_        = BorrowedSpec.PhysicalDevice;
    Device_                = BorrowedSpec.Device;
    Queue_                 = BorrowedSpec.Queue;
    QueueFamily_           = BorrowedSpec.QueueFamily;

    BorrowedSpec.QueryMemory(PhysicalDevice_, &MemoryProperties_);
    if (BorrowedSpec.QueryLimits)
    {
        VkPhysicalDeviceProperties Props{};
        BorrowedSpec.QueryLimits(PhysicalDevice_, &Props);
        DeviceName_                 = Props.deviceName;
        VendorId_                   = Props.vendorID;
        DeviceId_                   = Props.deviceID;
        DriverVersion_              = Props.driverVersion;
        ApiVersion_                 = Props.apiVersion;
        TimestampPeriodNanoseconds_ = Props.limits.timestampPeriod;
        TimestampsSupported_        = (Props.limits.timestampComputeAndGraphics != 0u) && (TimestampPeriodNanoseconds_ > 0.0f);
        GraphicsSupported_          = (Props.limits.maxColorAttachments >= 6u);
    }
    else
    {
        DeviceName_ = "Borrowed Vulkan Presentation Device";
    }

    return ResolveDeviceFunctionsAndPools(BorrowedSpec.ResolveDevice, PipelineArchivePath, OutDiagnostic);
}

bool VulkanComputeExchange::ResolveDeviceFunctionsAndPools(PFN_vkGetDeviceProcAddr GetDeviceProc,
                                                           const std::string&      PipelineArchivePath,
                                                           std::string*            OutDiagnostic)
{
#define RESOLVE_VK_DEV(Field, Symbol)                                                        \
    Field = reinterpret_cast<decltype(Field)>(GetDeviceProc(Device_, Symbol));               \
    if (!Field)                                                                              \
    {                                                                                        \
        if (OutDiagnostic) *OutDiagnostic = std::string("Missing device symbol: ") + Symbol; \
        Shutdown();                                                                          \
        return false;                                                                        \
    }

    RESOLVE_VK_DEV(pfnDestroyDevice_, "vkDestroyDevice");
    RESOLVE_VK_DEV(pfnDeviceWaitIdle_, "vkDeviceWaitIdle");
    RESOLVE_VK_DEV(pfnGetDeviceQueue_, "vkGetDeviceQueue");
    RESOLVE_VK_DEV(pfnCreateCommandPool_, "vkCreateCommandPool");
    RESOLVE_VK_DEV(pfnDestroyCommandPool_, "vkDestroyCommandPool");
    RESOLVE_VK_DEV(pfnAllocateCommandStreams_, "vkAllocateCommandBuffers");
    RESOLVE_VK_DEV(pfnFreeCommandStreams_, "vkFreeCommandBuffers");
    RESOLVE_VK_DEV(pfnBeginCommandStream_, "vkBeginCommandBuffer");
    RESOLVE_VK_DEV(pfnEndCommandStream_, "vkEndCommandBuffer");
    RESOLVE_VK_DEV(pfnResetCommandStream_, "vkResetCommandBuffer");
    RESOLVE_VK_DEV(pfnQueueSubmit_, "vkQueueSubmit");
    RESOLVE_VK_DEV(pfnQueueWaitIdle_, "vkQueueWaitIdle");
    RESOLVE_VK_DEV(pfnCreateFence_, "vkCreateFence");
    RESOLVE_VK_DEV(pfnDestroyFence_, "vkDestroyFence");
    RESOLVE_VK_DEV(pfnWaitForFences_, "vkWaitForFences");
    RESOLVE_VK_DEV(pfnResetFences_, "vkResetFences");
    RESOLVE_VK_DEV(pfnCreateMemorySpan_, "vkCreateBuffer");
    RESOLVE_VK_DEV(pfnDestroyMemorySpan_, "vkDestroyBuffer");
    RESOLVE_VK_DEV(pfnGetSpanMemoryRequirements_, "vkGetBufferMemoryRequirements");
    RESOLVE_VK_DEV(pfnAllocateMemory_, "vkAllocateMemory");
    RESOLVE_VK_DEV(pfnFreeMemory_, "vkFreeMemory");
    RESOLVE_VK_DEV(pfnBindSpanMemory_, "vkBindBufferMemory");
    RESOLVE_VK_DEV(pfnMapMemory_, "vkMapMemory");
    RESOLVE_VK_DEV(pfnUnmapMemory_, "vkUnmapMemory");
    RESOLVE_VK_DEV(pfnCreateImage_, "vkCreateImage");
    RESOLVE_VK_DEV(pfnDestroyImage_, "vkDestroyImage");
    RESOLVE_VK_DEV(pfnGetImageMemoryRequirements_, "vkGetImageMemoryRequirements");
    RESOLVE_VK_DEV(pfnBindImageMemory_, "vkBindImageMemory");
    RESOLVE_VK_DEV(pfnCreateImageView_, "vkCreateImageView");
    RESOLVE_VK_DEV(pfnDestroyImageView_, "vkDestroyImageView");
    RESOLVE_VK_DEV(pfnCreateSampler_, "vkCreateSampler");
    RESOLVE_VK_DEV(pfnDestroySampler_, "vkDestroySampler");
    RESOLVE_VK_DEV(pfnCreateShaderUnit_, "vkCreateShaderModule");
    RESOLVE_VK_DEV(pfnDestroyShaderUnit_, "vkDestroyShaderModule");
    RESOLVE_VK_DEV(pfnCreateDescriptorSetLayout_, "vkCreateDescriptorSetLayout");
    RESOLVE_VK_DEV(pfnDestroyDescriptorSetLayout_, "vkDestroyDescriptorSetLayout");
    RESOLVE_VK_DEV(pfnCreatePipelineLayout_, "vkCreatePipelineLayout");
    RESOLVE_VK_DEV(pfnDestroyPipelineLayout_, "vkDestroyPipelineLayout");
    RESOLVE_VK_DEV(pfnCreateComputePipelines_, "vkCreateComputePipelines");
    RESOLVE_VK_DEV(pfnDestroyPipeline_, "vkDestroyPipeline");
    RESOLVE_VK_DEV(pfnCreateDescriptorPool_, "vkCreateDescriptorPool");
    RESOLVE_VK_DEV(pfnDestroyDescriptorPool_, "vkDestroyDescriptorPool");
    RESOLVE_VK_DEV(pfnResetDescriptorPool_, "vkResetDescriptorPool");
    RESOLVE_VK_DEV(pfnAllocateDescriptorSets_, "vkAllocateDescriptorSets");
    RESOLVE_VK_DEV(pfnUpdateDescriptorSets_, "vkUpdateDescriptorSets");
    RESOLVE_VK_DEV(pfnCmdPipelineBarrier_, "vkCmdPipelineBarrier");
    RESOLVE_VK_DEV(pfnCmdBindPipeline_, "vkCmdBindPipeline");
    RESOLVE_VK_DEV(pfnCmdBindDescriptorSets_, "vkCmdBindDescriptorSets");
    RESOLVE_VK_DEV(pfnCmdPushConstants_, "vkCmdPushConstants");
    RESOLVE_VK_DEV(pfnCmdDispatch_, "vkCmdDispatch");
    RESOLVE_VK_DEV(pfnCmdClearColorImage_, "vkCmdClearColorImage");
    RESOLVE_VK_DEV(pfnCmdCopySpanToImage_, "vkCmdCopyBufferToImage");
    RESOLVE_VK_DEV(pfnCmdCopyImageToSpan_, "vkCmdCopyImageToBuffer");

#undef RESOLVE_VK_DEV

    // Optional pipeline archive, hardware graphics rasterization & timestamp query symbols
    pfnCreateRenderPass_        = reinterpret_cast<PFN_vkCreateRenderPass>(GetDeviceProc(Device_, "vkCreateRenderPass"));
    pfnDestroyRenderPass_       = reinterpret_cast<PFN_vkDestroyRenderPass>(GetDeviceProc(Device_, "vkDestroyRenderPass"));
    pfnCreateFramebuffer_       = reinterpret_cast<PFN_vkCreateFramebuffer>(GetDeviceProc(Device_, "vkCreateFramebuffer"));
    pfnDestroyFramebuffer_      = reinterpret_cast<PFN_vkDestroyFramebuffer>(GetDeviceProc(Device_, "vkDestroyFramebuffer"));
    pfnCreateGraphicsPipelines_ = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(GetDeviceProc(Device_, "vkCreateGraphicsPipelines"));
    pfnCmdBeginRenderPass_      = reinterpret_cast<PFN_vkCmdBeginRenderPass>(GetDeviceProc(Device_, "vkCmdBeginRenderPass"));
    pfnCmdEndRenderPass_        = reinterpret_cast<PFN_vkCmdEndRenderPass>(GetDeviceProc(Device_, "vkCmdEndRenderPass"));
    pfnCmdSetViewport_          = reinterpret_cast<PFN_vkCmdSetViewport>(GetDeviceProc(Device_, "vkCmdSetViewport"));
    pfnCmdSetScissor_           = reinterpret_cast<PFN_vkCmdSetScissor>(GetDeviceProc(Device_, "vkCmdSetScissor"));
    pfnCmdSetDepthBias_         = reinterpret_cast<PFN_vkCmdSetDepthBias>(GetDeviceProc(Device_, "vkCmdSetDepthBias"));
    pfnCmdDraw_                 = reinterpret_cast<PFN_vkCmdDraw>(GetDeviceProc(Device_, "vkCmdDraw"));
    if (!pfnCreateRenderPass_ || !pfnDestroyRenderPass_ || !pfnCreateFramebuffer_ || !pfnDestroyFramebuffer_ ||
        !pfnCreateGraphicsPipelines_ || !pfnCmdBeginRenderPass_ || !pfnCmdEndRenderPass_ || !pfnCmdSetViewport_ ||
        !pfnCmdSetScissor_ || !pfnCmdSetDepthBias_ || !pfnCmdDraw_)
    {
        GraphicsSupported_ = false;
    }

    pfnCreatePipelineArchive_  = reinterpret_cast<PFN_vkCreatePipelineCache>(GetDeviceProc(Device_, "vkCreatePipelineCache"));
    pfnDestroyPipelineArchive_ = reinterpret_cast<PFN_vkDestroyPipelineCache>(GetDeviceProc(Device_, "vkDestroyPipelineCache"));
    pfnGetPipelineArchiveBytes_ =
        reinterpret_cast<PFN_vkGetPipelineCacheData>(GetDeviceProc(Device_, "vkGetPipelineCacheData"));
    pfnCreateQueryPool_     = reinterpret_cast<PFN_vkCreateQueryPool>(GetDeviceProc(Device_, "vkCreateQueryPool"));
    pfnDestroyQueryPool_    = reinterpret_cast<PFN_vkDestroyQueryPool>(GetDeviceProc(Device_, "vkDestroyQueryPool"));
    pfnGetQueryPoolResults_ = reinterpret_cast<PFN_vkGetQueryPoolResults>(GetDeviceProc(Device_, "vkGetQueryPoolResults"));
    pfnCmdResetQueryPool_   = reinterpret_cast<PFN_vkCmdResetQueryPool>(GetDeviceProc(Device_, "vkCmdResetQueryPool"));
    pfnCmdWriteTimestamp_   = reinterpret_cast<PFN_vkCmdWriteTimestamp>(GetDeviceProc(Device_, "vkCmdWriteTimestamp"));

    if (!BorrowedDeviceEnabled_)
    {
        pfnGetDeviceQueue_(Device_, QueueFamily_, 0u, &Queue_);
    }

    VkCommandPoolCreateInfo PoolSpec{};
    PoolSpec.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolSpec.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolSpec.queueFamilyIndex = QueueFamily_;
    if (pfnCreateCommandPool_(Device_, &PoolSpec, nullptr, &CommandPool_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    const std::array<VkDescriptorPoolSize, 3> PoolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 768u},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512u},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512u},
    }};
    VkDescriptorPoolCreateInfo DescPoolSpec{};
    DescPoolSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DescPoolSpec.maxSets       = 128u;
    DescPoolSpec.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    DescPoolSpec.pPoolSizes    = PoolSizes.data();
    if (pfnCreateDescriptorPool_(Device_, &DescPoolSpec, nullptr, &DescriptorPool_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    for (uint32_t RingIndex = 0u; RingIndex < ExternalRingCount; ++RingIndex)
    {
        if (pfnCreateDescriptorPool_(Device_, &DescPoolSpec, nullptr, &ExternalDescriptorPools_[RingIndex]) != VK_SUCCESS)
        {
            Shutdown();
            return false;
        }
    }

    VkSamplerCreateInfo SamplerSpec{};
    SamplerSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerSpec.magFilter    = VK_FILTER_LINEAR;
    SamplerSpec.minFilter    = VK_FILTER_LINEAR;
    SamplerSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.maxLod       = 0.0f;
    if (pfnCreateSampler_(Device_, &SamplerSpec, nullptr, &LinearClampSampler_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    // Trilinear hardware mipmap repeat sampler for ddx/ddy fragment material maps
    VkSamplerCreateInfo RepeatMipSpec = SamplerSpec;
    RepeatMipSpec.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    RepeatMipSpec.addressModeU        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    RepeatMipSpec.addressModeV        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    RepeatMipSpec.addressModeW        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    RepeatMipSpec.minLod              = 0.0f;
    RepeatMipSpec.maxLod              = 12.0f;
    if (pfnCreateSampler_(Device_, &RepeatMipSpec, nullptr, &LinearRepeatSampler_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    // Exact nearest clamp sampler for D32_SFLOAT cascaded shadow maps (portable across all Vulkan 1.0+ ICDs)
    VkSamplerCreateInfo NearestSpec = SamplerSpec;
    NearestSpec.magFilter           = VK_FILTER_NEAREST;
    NearestSpec.minFilter           = VK_FILTER_NEAREST;
    if (pfnCreateSampler_(Device_, &NearestSpec, nullptr, &NearestClampSampler_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    VkFenceCreateInfo FenceSpec{};
    FenceSpec.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (pfnCreateFence_(Device_, &FenceSpec, nullptr, &SubmitFence_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    if (TimestampsSupported_ && pfnCreateQueryPool_ && pfnDestroyQueryPool_ && pfnGetQueryPoolResults_ &&
        pfnCmdResetQueryPool_ && pfnCmdWriteTimestamp_)
    {
        VkQueryPoolCreateInfo QuerySpec{};
        QuerySpec.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        QuerySpec.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        QuerySpec.queryCount = TotalTimestampQueries;
        if (pfnCreateQueryPool_(Device_, &QuerySpec, nullptr, &TimestampQueryPool_) != VK_SUCCESS)
        {
            TimestampQueryPool_  = VK_NULL_HANDLE;
            TimestampsSupported_ = false;
        }
    }
    else
    {
        TimestampsSupported_ = false;
    }

    InitializePipelineArchive(PipelineArchivePath);
    return true;
}

bool VulkanComputeExchange::InitializePipelineArchive(const std::string& ArchivePath)
{
    PipelineArchivePath_        = ArchivePath;
    PipelineArchiveLoadedBytes_ = 0u;
    if (!pfnCreatePipelineArchive_ || Device_ == VK_NULL_HANDLE)
    {
        return false;
    }

    std::vector<uint8_t> InitialBytes;
    if (!PipelineArchivePath_.empty())
    {
        std::error_code ErrorCode;
        if (std::filesystem::exists(PipelineArchivePath_, ErrorCode))
        {
            const auto FileBytes = std::filesystem::file_size(PipelineArchivePath_, ErrorCode);
            if (!ErrorCode && FileBytes > 0u && FileBytes <= MaximumArchiveLoadBytes)
            {
                std::ifstream File(PipelineArchivePath_, std::ios::binary);
                if (File.is_open())
                {
                    InitialBytes.resize(static_cast<size_t>(FileBytes));
                    File.read(reinterpret_cast<char*>(InitialBytes.data()), static_cast<std::streamsize>(FileBytes));
                    if (!File)
                    {
                        InitialBytes.clear();
                    }
                    else
                    {
                        PipelineArchiveLoadedBytes_ = InitialBytes.size();
                    }
                }
            }
        }
    }

    VkPipelineCacheCreateInfo ArchiveSpec{};
    ArchiveSpec.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    ArchiveSpec.initialDataSize = InitialBytes.size();
    ArchiveSpec.pInitialData    = InitialBytes.empty() ? nullptr : InitialBytes.data();
    if (pfnCreatePipelineArchive_(Device_, &ArchiveSpec, nullptr, &PipelineArchiveHandle_) != VK_SUCCESS)
    {
        // Corrupted or driver-mismatched archive fallback: create an empty archive cleanly.
        ArchiveSpec.initialDataSize = 0u;
        ArchiveSpec.pInitialData    = nullptr;
        PipelineArchiveLoadedBytes_ = 0u;
        return pfnCreatePipelineArchive_(Device_, &ArchiveSpec, nullptr, &PipelineArchiveHandle_) == VK_SUCCESS;
    }
    return true;
}

void VulkanComputeExchange::ConfigurePipelineArchivePath(const std::string& ArchivePath)
{
    if (PipelineArchivePath_ == ArchivePath)
    {
        return;
    }
    if (PipelineArchiveHandle_ != VK_NULL_HANDLE && pfnDestroyPipelineArchive_ && Device_ != VK_NULL_HANDLE)
    {
        pfnDestroyPipelineArchive_(Device_, PipelineArchiveHandle_, nullptr);
        PipelineArchiveHandle_ = VK_NULL_HANDLE;
    }
    InitializePipelineArchive(ArchivePath);
}

bool VulkanComputeExchange::PersistPipelineArchive() noexcept
{
    if (PipelineArchivePath_.empty() || PipelineArchiveHandle_ == VK_NULL_HANDLE || !pfnGetPipelineArchiveBytes_ ||
        Device_ == VK_NULL_HANDLE)
    {
        return false;
    }

    size_t ByteCount = 0u;
    if (pfnGetPipelineArchiveBytes_(Device_, PipelineArchiveHandle_, &ByteCount, nullptr) != VK_SUCCESS || ByteCount == 0u ||
        ByteCount > MaximumArchiveLoadBytes)
    {
        return false;
    }
    std::vector<uint8_t> ArchiveBytes(ByteCount);
    if (pfnGetPipelineArchiveBytes_(Device_, PipelineArchiveHandle_, &ByteCount, ArchiveBytes.data()) != VK_SUCCESS)
    {
        return false;
    }

    std::error_code             ErrorCode;
    const std::filesystem::path FinalPath(PipelineArchivePath_);
    if (FinalPath.has_parent_path())
    {
        std::filesystem::create_directories(FinalPath.parent_path(), ErrorCode);
    }
    const std::filesystem::path TempPath = FinalPath.string() + ".tmp";
    {
        std::ofstream Out(TempPath, std::ios::binary | std::ios::trunc);
        if (!Out.is_open())
        {
            return false;
        }
        Out.write(reinterpret_cast<const char*>(ArchiveBytes.data()), static_cast<std::streamsize>(ByteCount));
        Out.flush();
        if (!Out)
        {
            return false;
        }
    }
    std::filesystem::rename(TempPath, FinalPath, ErrorCode);
    if (ErrorCode)
    {
        std::filesystem::remove(FinalPath, ErrorCode);
        ErrorCode.clear();
        std::filesystem::rename(TempPath, FinalPath, ErrorCode);
    }
    if (!ErrorCode)
    {
        PipelineArchiveSavedBytes_ = ByteCount;
        return true;
    }
    return false;
}

void VulkanComputeExchange::Shutdown() noexcept
{
    ActiveExternalStream_ = VK_NULL_HANDLE;
    if (Device_ != VK_NULL_HANDLE)
    {
        PersistPipelineArchive();
        if (pfnDeviceWaitIdle_)
        {
            pfnDeviceWaitIdle_(Device_);
        }
        else if (pfnQueueWaitIdle_ && Queue_ != VK_NULL_HANDLE)
        {
            pfnQueueWaitIdle_(Queue_);
        }
        if (TimestampQueryPool_ != VK_NULL_HANDLE && pfnDestroyQueryPool_)
        {
            pfnDestroyQueryPool_(Device_, TimestampQueryPool_, nullptr);
        }
        if (PipelineArchiveHandle_ != VK_NULL_HANDLE && pfnDestroyPipelineArchive_)
        {
            pfnDestroyPipelineArchive_(Device_, PipelineArchiveHandle_, nullptr);
        }
        if (SubmitFence_ != VK_NULL_HANDLE && pfnDestroyFence_)
        {
            pfnDestroyFence_(Device_, SubmitFence_, nullptr);
        }
        if (NearestClampSampler_ != VK_NULL_HANDLE && pfnDestroySampler_)
        {
            pfnDestroySampler_(Device_, NearestClampSampler_, nullptr);
        }
        if (LinearRepeatSampler_ != VK_NULL_HANDLE && pfnDestroySampler_)
        {
            pfnDestroySampler_(Device_, LinearRepeatSampler_, nullptr);
        }
        if (LinearClampSampler_ != VK_NULL_HANDLE && pfnDestroySampler_)
        {
            pfnDestroySampler_(Device_, LinearClampSampler_, nullptr);
        }
        for (VkDescriptorPool& Pool : ExternalDescriptorPools_)
        {
            if (Pool != VK_NULL_HANDLE && pfnDestroyDescriptorPool_)
            {
                pfnDestroyDescriptorPool_(Device_, Pool, nullptr);
            }
            Pool = VK_NULL_HANDLE;
        }
        if (DescriptorPool_ != VK_NULL_HANDLE && pfnDestroyDescriptorPool_)
        {
            pfnDestroyDescriptorPool_(Device_, DescriptorPool_, nullptr);
        }
        if (CommandPool_ != VK_NULL_HANDLE && pfnDestroyCommandPool_)
        {
            pfnDestroyCommandPool_(Device_, CommandPool_, nullptr);
        }
        if (!BorrowedDeviceEnabled_ && pfnDestroyDevice_)
        {
            pfnDestroyDevice_(Device_, nullptr);
        }
    }
    if (!BorrowedDeviceEnabled_ && Instance_ != VK_NULL_HANDLE && pfnDestroyInstance_)
    {
        pfnDestroyInstance_(Instance_, nullptr);
    }
    if (!BorrowedDeviceEnabled_)
    {
        CloseDynamicLibrary(LibraryHandle_);
    }

    LibraryHandle_               = nullptr;
    Instance_                    = VK_NULL_HANDLE;
    PhysicalDevice_              = VK_NULL_HANDLE;
    Device_                      = VK_NULL_HANDLE;
    Queue_                       = VK_NULL_HANDLE;
    BorrowedDeviceEnabled_       = false;
    GraphicsSupported_           = false;
    CommandPool_                 = VK_NULL_HANDLE;
    DescriptorPool_              = VK_NULL_HANDLE;
    LinearClampSampler_          = VK_NULL_HANDLE;
    LinearRepeatSampler_         = VK_NULL_HANDLE;
    NearestClampSampler_         = VK_NULL_HANDLE;
    SubmitFence_                 = VK_NULL_HANDLE;
    PipelineArchiveHandle_       = VK_NULL_HANDLE;
    TimestampQueryPool_          = VK_NULL_HANDLE;
    TimestampsSupported_         = false;
    ExternalSlotDispatchCounts_  = {};
    ExternalSlotRecorded_        = {};
    AllocatedDeviceBytes_        = 0u;
    LastDispatchGpuMilliseconds_ = 0.0f;
    LastDispatchCpuMilliseconds_ = 0.0f;
    DeviceName_.clear();
}

void VulkanComputeExchange::BeginExternalCommandStream(VkCommandBuffer ExternalStream, uint32_t RingSlot) noexcept
{
    ActiveExternalStream_ = ExternalStream;
    ActiveExternalSlot_   = RingSlot % ExternalRingCount;
    ExternalSlotDispatchCounts_[ActiveExternalSlot_] = 0u;
    ExternalSlotRecorded_[ActiveExternalSlot_]       = false;
    if (Ready() && ExternalStream != VK_NULL_HANDLE)
    {
        if (ExternalDescriptorPools_[ActiveExternalSlot_] != VK_NULL_HANDLE)
        {
            pfnResetDescriptorPool_(Device_, ExternalDescriptorPools_[ActiveExternalSlot_], 0u);
        }
        if (TimestampsSupported_ && TimestampQueryPool_ != VK_NULL_HANDLE)
        {
            const uint32_t FirstQuery = ActiveExternalSlot_ * MaxTimedDispatchesSlot * 2u;
            pfnCmdResetQueryPool_(ExternalStream, TimestampQueryPool_, FirstQuery, MaxTimedDispatchesSlot * 2u);
        }
    }
}

void VulkanComputeExchange::EndExternalCommandStream() noexcept
{
    ActiveExternalStream_ = VK_NULL_HANDLE;
}

bool VulkanComputeExchange::HarvestExternalSlotTimings(uint32_t RingSlot, std::span<float> OutDispatchMilliseconds) noexcept
{
    const uint32_t Slot = RingSlot % ExternalRingCount;
    for (float& Value : OutDispatchMilliseconds)
    {
        Value = 0.0f;
    }
    if (!Ready() || !TimestampsSupported_ || TimestampQueryPool_ == VK_NULL_HANDLE || !ExternalSlotRecorded_[Slot])
    {
        return false;
    }
    const uint32_t Count = std::min<uint32_t>(ExternalSlotDispatchCounts_[Slot],
                                              static_cast<uint32_t>(OutDispatchMilliseconds.size()));
    if (Count == 0u)
    {
        return false;
    }

    std::array<uint64_t, MaxTimedDispatchesSlot * 2u> Ticks{};
    const uint32_t                                    FirstQuery = Slot * MaxTimedDispatchesSlot * 2u;
    const VkResult Result = pfnGetQueryPoolResults_(Device_,
                                                    TimestampQueryPool_,
                                                    FirstQuery,
                                                    Count * 2u,
                                                    Count * 2u * sizeof(uint64_t),
                                                    Ticks.data(),
                                                    sizeof(uint64_t),
                                                    VK_QUERY_RESULT_64_BIT);
    if (Result != VK_SUCCESS)
    {
        return false;
    }
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const uint64_t StartTick = Ticks[Index * 2u + 0u];
        const uint64_t EndTick   = Ticks[Index * 2u + 1u];
        OutDispatchMilliseconds[Index] =
            (EndTick >= StartTick) ? static_cast<float>(static_cast<double>(EndTick - StartTick) *
                                                        static_cast<double>(TimestampPeriodNanoseconds_) * 1.0e-6)
                                   : 0.0f;
    }
    return true;
}

uint32_t VulkanComputeExchange::SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredFlags) const
{
    for (uint32_t TypeIndex = 0u; TypeIndex < MemoryProperties_.memoryTypeCount; ++TypeIndex)
    {
        if ((TypeBits & (1u << TypeIndex)) != 0u &&
            (MemoryProperties_.memoryTypes[TypeIndex].propertyFlags & RequiredFlags) == RequiredFlags)
        {
            return TypeIndex;
        }
    }
    return UINT32_MAX;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      MEMORY SPAN & IMAGE ARRAY ALLOCATION / IO
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeExchange::AllocateMemorySpan(VkDeviceSize ByteCount, DeviceMemorySpan& OutSlot)
{
    ReleaseMemorySpan(OutSlot);
    if (!Ready() || ByteCount == 0)
    {
        return false;
    }

    VkBufferCreateInfo SpanSpec{};
    SpanSpec.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    SpanSpec.size        = std::max<VkDeviceSize>(ByteCount, 16u);
    SpanSpec.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    SpanSpec.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (pfnCreateMemorySpan_(Device_, &SpanSpec, nullptr, &OutSlot.Handle) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetSpanMemoryRequirements_(Device_, OutSlot.Handle, &Requirements);

    const uint32_t MemoryType = SelectMemoryType(
        Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseMemorySpan(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindSpanMemory_(Device_, OutSlot.Handle, OutSlot.Memory, 0) != VK_SUCCESS ||
        pfnMapMemory_(Device_, OutSlot.Memory, 0, SpanSpec.size, 0, &OutSlot.MappedBytes) != VK_SUCCESS)
    {
        ReleaseMemorySpan(OutSlot);
        return false;
    }
    OutSlot.Capacity      = SpanSpec.size;
    OutSlot.AllocatedSize = Requirements.size;
    AllocatedDeviceBytes_ += OutSlot.AllocatedSize;
    std::memset(OutSlot.MappedBytes, 0, static_cast<size_t>(SpanSpec.size));
    return true;
}

void VulkanComputeExchange::ReleaseMemorySpan(DeviceMemorySpan& Slot) noexcept
{
    if (!Ready())
    {
        Slot = DeviceMemorySpan{};
        return;
    }
    if (Slot.AllocatedSize > 0u && AllocatedDeviceBytes_ >= Slot.AllocatedSize)
    {
        AllocatedDeviceBytes_ -= Slot.AllocatedSize;
    }
    if (Slot.MappedBytes && Slot.Memory != VK_NULL_HANDLE)
    {
        pfnUnmapMemory_(Device_, Slot.Memory);
    }
    if (Slot.Handle != VK_NULL_HANDLE)
    {
        pfnDestroyMemorySpan_(Device_, Slot.Handle, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = DeviceMemorySpan{};
}

bool VulkanComputeExchange::UploadMemorySpan(DeviceMemorySpan& Slot, const void* SourceBytes, VkDeviceSize ByteCount)
{
    if (ByteCount == 0)
    {
        return true;
    }
    if (Slot.Handle == VK_NULL_HANDLE || Slot.Capacity < ByteCount)
    {
        if (!AllocateMemorySpan(ByteCount, Slot))
        {
            return false;
        }
    }
    std::memcpy(Slot.MappedBytes, SourceBytes, static_cast<size_t>(ByteCount));
    return true;
}

bool VulkanComputeExchange::DownloadMemorySpan(const DeviceMemorySpan& Slot, void* DestinationBytes, VkDeviceSize ByteCount) const
{
    if (!Slot.MappedBytes || Slot.Capacity < ByteCount)
    {
        return false;
    }
    std::memcpy(DestinationBytes, Slot.MappedBytes, static_cast<size_t>(ByteCount));
    return true;
}

bool VulkanComputeExchange::AllocateRgba16fArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot)
{
    ReleaseImage(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || ArrayLayers == 0u)
    {
        return false;
    }

    VkImageCreateInfo ImageSpec{};
    ImageSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImageSpec.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
    ImageSpec.extent        = {Width, Height, 1u};
    ImageSpec.mipLevels     = 1u;
    ImageSpec.arrayLayers   = ArrayLayers;
    ImageSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageSpec.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ImageSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &ImageSpec, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Requirements);
    const uint32_t MemoryType = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }
    OutSlot.AllocatedSize = Requirements.size;
    AllocatedDeviceBytes_ += OutSlot.AllocatedSize;

    VkImageViewCreateInfo ViewSpec{};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutSlot.Image;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ViewSpec.format                          = ImageSpec.format;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = ArrayLayers;

    if (pfnCreateImageView_(Device_, &ViewSpec, nullptr, &OutSlot.ArrayView) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    if (ArrayLayers >= 6u)
    {
        OutSlot.LayerViews2D.resize(6u, VK_NULL_HANDLE);
        for (uint32_t LayerIdx = 0u; LayerIdx < 6u; ++LayerIdx)
        {
            VkImageViewCreateInfo LayerViewSpec           = ViewSpec;
            LayerViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            LayerViewSpec.subresourceRange.baseArrayLayer = LayerIdx;
            LayerViewSpec.subresourceRange.layerCount     = 1u;
            if (pfnCreateImageView_(Device_, &LayerViewSpec, nullptr, &OutSlot.LayerViews2D[LayerIdx]) != VK_SUCCESS)
            {
                ReleaseImage(OutSlot);
                return false;
            }
        }
    }

    OutSlot.Format        = ImageSpec.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.ArrayLayers   = ArrayLayers;
    OutSlot.MipLevels     = 1u;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return ClearRgba16fArray(OutSlot);
}

bool VulkanComputeExchange::AllocateSrgb8Array(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot)
{
    ReleaseImage(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || ArrayLayers == 0u)
    {
        return false;
    }

    uint32_t MipLevels = 1u;
    for (uint32_t Dim = std::max(Width, Height); Dim > 1u; Dim >>= 1u)
    {
        ++MipLevels;
    }

    VkImageCreateInfo ImageSpec{};
    ImageSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImageSpec.format        = VK_FORMAT_R8G8B8A8_SRGB;
    ImageSpec.extent        = {Width, Height, 1u};
    ImageSpec.mipLevels     = MipLevels;
    ImageSpec.arrayLayers   = ArrayLayers;
    ImageSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageSpec.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ImageSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &ImageSpec, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Requirements);
    const uint32_t MemoryType = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }
    OutSlot.AllocatedSize = Requirements.size;
    AllocatedDeviceBytes_ += OutSlot.AllocatedSize;

    VkImageViewCreateInfo ViewSpec{};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutSlot.Image;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ViewSpec.format                          = ImageSpec.format;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = MipLevels;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = ArrayLayers;

    if (pfnCreateImageView_(Device_, &ViewSpec, nullptr, &OutSlot.ArrayView) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    OutSlot.Format        = ImageSpec.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.ArrayLayers   = ArrayLayers;
    OutSlot.MipLevels     = MipLevels;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void VulkanComputeExchange::ReleaseImage(DeviceImageArraySlot& Slot) noexcept
{
    if (!Ready())
    {
        Slot = DeviceImageArraySlot{};
        return;
    }
    if (Slot.AllocatedSize > 0u && AllocatedDeviceBytes_ >= Slot.AllocatedSize)
    {
        AllocatedDeviceBytes_ -= Slot.AllocatedSize;
    }
    for (VkImageView View : Slot.LayerViews2D)
    {
        if (View != VK_NULL_HANDLE)
        {
            pfnDestroyImageView_(Device_, View, nullptr);
        }
    }
    if (Slot.ArrayView != VK_NULL_HANDLE)
    {
        pfnDestroyImageView_(Device_, Slot.ArrayView, nullptr);
    }
    if (Slot.Image != VK_NULL_HANDLE)
    {
        pfnDestroyImage_(Device_, Slot.Image, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = DeviceImageArraySlot{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                  2D RGBA8_UNORM VIEWPORT PRESENTATION TARGET
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeExchange::AllocateViewportImage2D(uint32_t Width, uint32_t Height, DeviceViewportImageSlot& OutSlot)
{
    ReleaseViewportImage2D(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u)
    {
        return false;
    }

    VkImageCreateInfo ImageSpec{};
    ImageSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImageSpec.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ImageSpec.extent        = {Width, Height, 1u};
    ImageSpec.mipLevels     = 1u;
    ImageSpec.arrayLayers   = 1u;
    ImageSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageSpec.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ImageSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &ImageSpec, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Requirements);
    const uint32_t MemoryType = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseViewportImage2D(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseViewportImage2D(OutSlot);
        return false;
    }
    OutSlot.AllocatedSize = Requirements.size;
    AllocatedDeviceBytes_ += OutSlot.AllocatedSize;

    VkImageViewCreateInfo ViewSpec{};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutSlot.Image;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ViewSpec.format                          = ImageSpec.format;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = 1u;

    if (pfnCreateImageView_(Device_, &ViewSpec, nullptr, &OutSlot.View2D) != VK_SUCCESS)
    {
        ReleaseViewportImage2D(OutSlot);
        return false;
    }

    OutSlot.Format        = ImageSpec.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Initialize into SHADER_READ_ONLY_OPTIMAL with dark neutral clear so first ImGui bind is valid immediately.
    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionViewportLayout(CommandStream,
                             OutSlot,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0u,
                             VK_ACCESS_TRANSFER_WRITE_BIT);

    VkClearColorValue ClearColor{};
    ClearColor.float32[0] = 0.04f;
    ClearColor.float32[1] = 0.045f;
    ClearColor.float32[2] = 0.055f;
    ClearColor.float32[3] = 1.0f;

    VkImageSubresourceRange Subresource{};
    Subresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Subresource.baseMipLevel   = 0u;
    Subresource.levelCount     = 1u;
    Subresource.baseArrayLayer = 0u;
    Subresource.layerCount     = 1u;
    pfnCmdClearColorImage_(CommandStream, OutSlot.Image, VK_IMAGE_LAYOUT_GENERAL, &ClearColor, 1u, &Subresource);

    TransitionViewportLayout(CommandStream,
                             OutSlot,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_SHADER_READ_BIT);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    return Ok;
}

void VulkanComputeExchange::ReleaseViewportImage2D(DeviceViewportImageSlot& Slot) noexcept
{
    if (!Ready())
    {
        Slot = DeviceViewportImageSlot{};
        return;
    }
    if (Slot.AllocatedSize > 0u && AllocatedDeviceBytes_ >= Slot.AllocatedSize)
    {
        AllocatedDeviceBytes_ -= Slot.AllocatedSize;
    }
    if (Slot.View2D != VK_NULL_HANDLE)
    {
        pfnDestroyImageView_(Device_, Slot.View2D, nullptr);
    }
    if (Slot.Image != VK_NULL_HANDLE)
    {
        pfnDestroyImage_(Device_, Slot.Image, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = DeviceViewportImageSlot{};
}

bool VulkanComputeExchange::TransitionViewportLayout(VkCommandBuffer          CommandStream,
                                                     DeviceViewportImageSlot& Slot,
                                                     VkImageLayout            TargetLayout,
                                                     VkPipelineStageFlags     SrcStage,
                                                     VkPipelineStageFlags     DstStage,
                                                     VkAccessFlags            SrcAccess,
                                                     VkAccessFlags            DstAccess)
{
    if (Slot.CurrentLayout == TargetLayout)
    {
        return true;
    }
    VkImageMemoryBarrier Barrier{};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout                       = Slot.CurrentLayout;
    Barrier.newLayout                       = TargetLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Slot.Image;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = 1u;
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = 1u;
    Barrier.srcAccessMask                   = SrcAccess;
    Barrier.dstAccessMask                   = DstAccess;

    pfnCmdPipelineBarrier_(CommandStream, SrcStage, DstStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
    Slot.CurrentLayout = TargetLayout;
    return true;
}

bool VulkanComputeExchange::PrepareViewportImageForCompute(DeviceViewportImageSlot& Slot)
{
    if (!Ready() || Slot.Image == VK_NULL_HANDLE)
    {
        return false;
    }
    if (Slot.CurrentLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        return true;
    }
    if (ActiveExternalStream_ != VK_NULL_HANDLE)
    {
        return TransitionViewportLayout(ActiveExternalStream_,
                                        Slot,
                                        VK_IMAGE_LAYOUT_GENERAL,
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_SHADER_READ_BIT,
                                        VK_ACCESS_SHADER_WRITE_BIT);
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);
    VkCommandBufferBeginInfo BeginSpec{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionViewportLayout(CommandStream,
                             Slot,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_ACCESS_MEMORY_READ_BIT,
                             VK_ACCESS_SHADER_WRITE_BIT);
    pfnEndCommandStream_(CommandStream);
    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok                 = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    return Ok;
}

bool VulkanComputeExchange::PrepareViewportImageForSampling(DeviceViewportImageSlot& Slot)
{
    if (!Ready() || Slot.Image == VK_NULL_HANDLE)
    {
        return false;
    }
    if (Slot.CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        return true;
    }
    if (ActiveExternalStream_ != VK_NULL_HANDLE)
    {
        return TransitionViewportLayout(ActiveExternalStream_,
                                        Slot,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_SHADER_WRITE_BIT,
                                        VK_ACCESS_SHADER_READ_BIT);
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);
    VkCommandBufferBeginInfo BeginSpec{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionViewportLayout(CommandStream,
                             Slot,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_ACCESS_SHADER_WRITE_BIT,
                             VK_ACCESS_SHADER_READ_BIT);
    pfnEndCommandStream_(CommandStream);
    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok                 = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    return Ok;
}

bool VulkanComputeExchange::DownloadViewportImageRgba8(DeviceViewportImageSlot& Slot, std::vector<uint8_t>& OutRgbaBytes)
{
    const size_t ByteCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || ByteCount == 0u)
    {
        return false;
    }
    DeviceMemorySpan TransferSpan;
    if (!AllocateMemorySpan(ByteCount, TransferSpan))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    const VkImageLayout PreviousLayout = (Slot.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                             ? VK_IMAGE_LAYOUT_GENERAL
                                             : Slot.CurrentLayout;
    TransitionViewportLayout(CommandStream,
                             Slot,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_MEMORY_WRITE_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = 0u;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopyImageToSpan_(CommandStream, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, TransferSpan.Handle, 1u, &CopyRegion);
    TransitionViewportLayout(CommandStream,
                             Slot,
                             PreviousLayout,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT,
                             VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    if (!Ok)
    {
        ReleaseMemorySpan(TransferSpan);
        return false;
    }

    OutRgbaBytes.resize(ByteCount);
    std::memcpy(OutRgbaBytes.data(), TransferSpan.MappedBytes, ByteCount);
    ReleaseMemorySpan(TransferSpan);
    return true;
}

void VulkanComputeExchange::WaitDeviceIdle() noexcept
{
    if (Ready() && Device_ != VK_NULL_HANDLE && pfnDeviceWaitIdle_ != nullptr)
    {
        pfnDeviceWaitIdle_(Device_);
    }
}

bool VulkanComputeExchange::UploadViewportImageRgba8(DeviceViewportImageSlot& Slot, std::span<const uint8_t> SourceRgbaBytes)
{
    const size_t ByteCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || ByteCount == 0u || SourceRgbaBytes.size() < ByteCount)
    {
        return false;
    }
    DeviceMemorySpan TransferSpan;
    if (!AllocateMemorySpan(ByteCount, TransferSpan))
    {
        return false;
    }
    std::memcpy(TransferSpan.MappedBytes, SourceRgbaBytes.data(), ByteCount);

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionViewportLayout(CommandStream,
                             Slot,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = 0u;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopySpanToImage_(CommandStream, TransferSpan.Handle, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);
    TransitionViewportLayout(CommandStream,
                             Slot,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_SHADER_READ_BIT);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    ReleaseMemorySpan(TransferSpan);
    return Ok;
}

bool VulkanComputeExchange::TransitionImageLayout(VkCommandBuffer       CommandStream,
                                                  DeviceImageArraySlot& Slot,
                                                  VkImageLayout         TargetLayout)
{
    if (Slot.CurrentLayout == TargetLayout)
    {
        return true;
    }
    VkImageMemoryBarrier Barrier{};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout                       = Slot.CurrentLayout;
    Barrier.newLayout                       = TargetLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Slot.Image;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = std::max(1u, Slot.MipLevels);
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = Slot.ArrayLayers;
    Barrier.srcAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    Barrier.dstAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           0u,
                           0u,
                           nullptr,
                           0u,
                           nullptr,
                           1u,
                           &Barrier);
    Slot.CurrentLayout = TargetLayout;
    return true;
}

bool VulkanComputeExchange::ClearRgba16fArray(DeviceImageArraySlot& Slot, std::array<float, 4> ClearColour)
{
    if (!Ready() || Slot.Image == VK_NULL_HANDLE)
    {
        return false;
    }
    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_GENERAL);

    VkClearColorValue ClearColor{};
    ClearColor.float32[0] = ClearColour[0];
    ClearColor.float32[1] = ClearColour[1];
    ClearColor.float32[2] = ClearColour[2];
    ClearColor.float32[3] = ClearColour[3];

    VkImageSubresourceRange Subresource{};
    Subresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Subresource.baseMipLevel   = 0u;
    Subresource.levelCount     = 1u;
    Subresource.baseArrayLayer = 0u;
    Subresource.layerCount     = Slot.ArrayLayers;

    pfnCmdClearColorImage_(CommandStream, Slot.Image, VK_IMAGE_LAYOUT_GENERAL, &ClearColor, 1u, &Subresource);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    return Ok;
}

bool VulkanComputeExchange::UploadSrgb8Array(DeviceImageArraySlot& Slot, std::span<const uint8_t> RgbaBytes)
{
    const size_t Expected = static_cast<size_t>(Slot.Width) * Slot.Height * 4u * Slot.ArrayLayers;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || RgbaBytes.size() != Expected)
    {
        return false;
    }

    auto SrgbByteToLinear = [](uint8_t Byte) noexcept -> float
    {
        const float S = static_cast<float>(Byte) * (1.0f / 255.0f);
        return (S <= 0.04045f) ? (S / 12.92f) : std::pow((S + 0.055f) / 1.055f, 2.4f);
    };
    auto LinearToSrgbByte = [](float L) noexcept -> uint8_t
    {
        const float Clamped = std::clamp(L, 0.0f, 1.0f);
        const float S       = (Clamped <= 0.0031308f) ? (Clamped * 12.92f) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
    };

    const uint32_t                MipLevels = std::max(1u, Slot.MipLevels);
    std::vector<VkBufferImageCopy> CopyRegions;
    CopyRegions.reserve(MipLevels);

    std::vector<uint8_t> AllMipBytes;
    AllMipBytes.reserve(Expected + Expected / 2u + 1024u);

    // Mip 0: authored base resolution across all layers
    {
        VkBufferImageCopy Region{};
        Region.bufferOffset                    = 0u;
        Region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Region.imageSubresource.mipLevel       = 0u;
        Region.imageSubresource.baseArrayLayer = 0u;
        Region.imageSubresource.layerCount     = Slot.ArrayLayers;
        Region.imageExtent                     = {Slot.Width, Slot.Height, 1u};
        CopyRegions.push_back(Region);
        AllMipBytes.insert(AllMipBytes.end(), RgbaBytes.begin(), RgbaBytes.end());
    }

    uint32_t             PrevW = Slot.Width;
    uint32_t             PrevH = Slot.Height;
    std::vector<uint8_t> PrevMip(RgbaBytes.begin(), RgbaBytes.end());

    for (uint32_t Mip = 1u; Mip < MipLevels; ++Mip)
    {
        const uint32_t NextW = std::max(1u, PrevW >> 1u);
        const uint32_t NextH = std::max(1u, PrevH >> 1u);
        while ((AllMipBytes.size() & 3u) != 0u)
        {
            AllMipBytes.push_back(0u);
        }

        const VkDeviceSize MipOffset = AllMipBytes.size();
        std::vector<uint8_t> NextMip(static_cast<size_t>(NextW) * NextH * 4u * Slot.ArrayLayers, 255u);

        for (uint32_t Layer = 0u; Layer < Slot.ArrayLayers; ++Layer)
        {
            const uint8_t* SrcLayer = PrevMip.data() + static_cast<size_t>(Layer) * PrevW * PrevH * 4u;
            uint8_t*       DstLayer = NextMip.data() + static_cast<size_t>(Layer) * NextW * NextH * 4u;

            for (uint32_t Y = 0u; Y < NextH; ++Y)
            {
                const uint32_t Sy0 = std::min(Y * 2u, PrevH - 1u);
                const uint32_t Sy1 = std::min(Y * 2u + 1u, PrevH - 1u);
                for (uint32_t X = 0u; X < NextW; ++X)
                {
                    const uint32_t Sx0 = std::min(X * 2u, PrevW - 1u);
                    const uint32_t Sx1 = std::min(X * 2u + 1u, PrevW - 1u);

                    const uint8_t* P00 = SrcLayer + (static_cast<size_t>(Sy0) * PrevW + Sx0) * 4u;
                    const uint8_t* P10 = SrcLayer + (static_cast<size_t>(Sy0) * PrevW + Sx1) * 4u;
                    const uint8_t* P01 = SrcLayer + (static_cast<size_t>(Sy1) * PrevW + Sx0) * 4u;
                    const uint8_t* P11 = SrcLayer + (static_cast<size_t>(Sy1) * PrevW + Sx1) * 4u;

                    uint8_t* DstPx = DstLayer + (static_cast<size_t>(Y) * NextW + X) * 4u;
                    for (uint32_t C = 0u; C < 3u; ++C)
                    {
                        const float AvgLin = 0.25f * (SrgbByteToLinear(P00[C]) + SrgbByteToLinear(P10[C]) +
                                                      SrgbByteToLinear(P01[C]) + SrgbByteToLinear(P11[C]));
                        DstPx[C]           = LinearToSrgbByte(AvgLin);
                    }
                    DstPx[3] = static_cast<uint8_t>((uint32_t(P00[3]) + P10[3] + P01[3] + P11[3] + 2u) >> 2u);
                }
            }
        }

        VkBufferImageCopy Region{};
        Region.bufferOffset                    = MipOffset;
        Region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Region.imageSubresource.mipLevel       = Mip;
        Region.imageSubresource.baseArrayLayer = 0u;
        Region.imageSubresource.layerCount     = Slot.ArrayLayers;
        Region.imageExtent                     = {NextW, NextH, 1u};
        CopyRegions.push_back(Region);

        AllMipBytes.insert(AllMipBytes.end(), NextMip.begin(), NextMip.end());
        PrevMip = std::move(NextMip);
        PrevW   = NextW;
        PrevH   = NextH;
    }

    DeviceMemorySpan TransferSpan;
    if (!UploadMemorySpan(TransferSpan, AllMipBytes.data(), AllMipBytes.size()))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    pfnCmdCopySpanToImage_(CommandStream,
                           TransferSpan.Handle,
                           Slot.Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(CopyRegions.size()),
                           CopyRegions.data());
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    ReleaseMemorySpan(TransferSpan);
    return Ok;
}

bool VulkanComputeExchange::UploadRgba16fLayer(DeviceImageArraySlot&  Slot,
                                               uint32_t               LayerIndex,
                                               std::span<const float> RgbaFloats)
{
    const size_t FloatCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || LayerIndex >= Slot.ArrayLayers || RgbaFloats.size() != FloatCount)
    {
        return false;
    }
    std::vector<uint16_t> HalfTexels(FloatCount);
    for (size_t Index = 0u; Index < FloatCount; ++Index)
    {
        HalfTexels[Index] = PackHalfFloat(RgbaFloats[Index]);
    }
    DeviceMemorySpan TransferSpan;
    if (!UploadMemorySpan(TransferSpan, HalfTexels.data(), HalfTexels.size() * sizeof(uint16_t)))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = LayerIndex;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopySpanToImage_(CommandStream, TransferSpan.Handle, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_GENERAL);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    ReleaseMemorySpan(TransferSpan);
    return Ok;
}

bool VulkanComputeExchange::DownloadRgba16fLayer(DeviceImageArraySlot& Slot,
                                                 uint32_t              LayerIndex,
                                                 std::vector<float>&   OutRgbaFloats)
{
    const size_t FloatCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || LayerIndex >= Slot.ArrayLayers)
    {
        return false;
    }
    DeviceMemorySpan TransferSpan;
    if (!AllocateMemorySpan(FloatCount * sizeof(uint16_t), TransferSpan))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = LayerIndex;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopyImageToSpan_(CommandStream, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, TransferSpan.Handle, 1u, &CopyRegion);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_GENERAL);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    if (!Ok)
    {
        ReleaseMemorySpan(TransferSpan);
        return false;
    }

    const auto* HalfPtr = static_cast<const uint16_t*>(TransferSpan.MappedBytes);
    OutRgbaFloats.resize(FloatCount);
    for (size_t Index = 0u; Index < FloatCount; ++Index)
    {
        OutRgbaFloats[Index] = UnpackHalfFloat(HalfPtr[Index]);
    }
    ReleaseMemorySpan(TransferSpan);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                      3D HARDWARE SDF VOLUME TEXTURE & D32_SFLOAT DEPTH / SHADOW ARRAYS
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeExchange::AllocateR16fVolume3D(uint32_t Width, uint32_t Height, uint32_t Depth, DeviceVolumeTextureSlot& OutSlot)
{
    ReleaseVolume3D(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || Depth == 0u)
    {
        return false;
    }

    VkImageCreateInfo ImageSpec{};
    ImageSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageSpec.imageType     = VK_IMAGE_TYPE_3D;
    ImageSpec.format        = VK_FORMAT_R16_SFLOAT;
    ImageSpec.extent        = {Width, Height, Depth};
    ImageSpec.mipLevels     = 1u;
    ImageSpec.arrayLayers   = 1u;
    ImageSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageSpec.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ImageSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &ImageSpec, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Requirements);
    const uint32_t MemoryType = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseVolume3D(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseVolume3D(OutSlot);
        return false;
    }
    OutSlot.AllocatedSize = Requirements.size;
    AllocatedDeviceBytes_ += OutSlot.AllocatedSize;

    VkImageViewCreateInfo ViewSpec{};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutSlot.Image;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_3D;
    ViewSpec.format                          = ImageSpec.format;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = 1u;

    if (pfnCreateImageView_(Device_, &ViewSpec, nullptr, &OutSlot.View3D) != VK_SUCCESS)
    {
        ReleaseVolume3D(OutSlot);
        return false;
    }

    OutSlot.Format        = ImageSpec.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.Depth         = Depth;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    std::vector<float> InitialFar(static_cast<size_t>(Width) * Height * Depth, 100.0f);
    return UploadR16fVolume3D(OutSlot, InitialFar);
}

bool VulkanComputeExchange::UploadR16fVolume3D(DeviceVolumeTextureSlot& Slot, std::span<const float> ScalarDistances)
{
    const size_t VoxelCount = static_cast<size_t>(Slot.Width) * Slot.Height * Slot.Depth;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || ScalarDistances.size() != VoxelCount)
    {
        return false;
    }

    std::vector<uint16_t> HalfTexels(VoxelCount);
    for (size_t Index = 0u; Index < VoxelCount; ++Index)
    {
        HalfTexels[Index] = PackHalfFloat(ScalarDistances[Index]);
    }
    DeviceMemorySpan TransferSpan;
    if (!UploadMemorySpan(TransferSpan, HalfTexels.data(), HalfTexels.size() * sizeof(uint16_t)))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
    {
        ReleaseMemorySpan(TransferSpan);
        return false;
    }

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);

    VkImageMemoryBarrier ToDst{};
    ToDst.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToDst.oldLayout                       = Slot.CurrentLayout;
    ToDst.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToDst.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToDst.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToDst.image                           = Slot.Image;
    ToDst.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ToDst.subresourceRange.levelCount     = 1u;
    ToDst.subresourceRange.layerCount     = 1u;
    ToDst.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
    ToDst.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0u,
                           0u,
                           nullptr,
                           0u,
                           nullptr,
                           1u,
                           &ToDst);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.layerCount = 1u;
    CopyRegion.imageExtent                 = {Slot.Width, Slot.Height, Slot.Depth};
    pfnCmdCopySpanToImage_(CommandStream, TransferSpan.Handle, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);

    VkImageMemoryBarrier ToRead          = ToDst;
    ToRead.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToRead.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToRead.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToRead.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;
    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0u,
                           0u,
                           nullptr,
                           0u,
                           nullptr,
                           1u,
                           &ToRead);
    Slot.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    pfnEndCommandStream_(CommandStream);
    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    ReleaseMemorySpan(TransferSpan);
    return Ok;
}

void VulkanComputeExchange::ReleaseVolume3D(DeviceVolumeTextureSlot& Slot) noexcept
{
    if (!Ready())
    {
        Slot = DeviceVolumeTextureSlot{};
        return;
    }
    if (Slot.AllocatedSize > 0u && AllocatedDeviceBytes_ >= Slot.AllocatedSize)
    {
        AllocatedDeviceBytes_ -= Slot.AllocatedSize;
    }
    if (Slot.View3D != VK_NULL_HANDLE)
    {
        pfnDestroyImageView_(Device_, Slot.View3D, nullptr);
    }
    if (Slot.Image != VK_NULL_HANDLE)
    {
        pfnDestroyImage_(Device_, Slot.Image, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = DeviceVolumeTextureSlot{};
}

bool VulkanComputeExchange::AllocateDepthTargetArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceDepthTargetSlot& OutSlot)
{
    ReleaseDepthTarget(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || ArrayLayers == 0u)
    {
        return false;
    }

    VkImageCreateInfo ImageSpec{};
    ImageSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImageSpec.format        = VK_FORMAT_D32_SFLOAT;
    ImageSpec.extent        = {Width, Height, 1u};
    ImageSpec.mipLevels     = 1u;
    ImageSpec.arrayLayers   = ArrayLayers;
    ImageSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageSpec.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &ImageSpec, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Requirements);
    const uint32_t MemoryType = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseDepthTarget(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseDepthTarget(OutSlot);
        return false;
    }
    OutSlot.AllocatedSize = Requirements.size;
    AllocatedDeviceBytes_ += OutSlot.AllocatedSize;

    VkImageViewCreateInfo ViewSpec{};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutSlot.Image;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ViewSpec.format                          = ImageSpec.format;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = ArrayLayers;

    if (pfnCreateImageView_(Device_, &ViewSpec, nullptr, &OutSlot.ArrayView) != VK_SUCCESS)
    {
        ReleaseDepthTarget(OutSlot);
        return false;
    }

    OutSlot.SliceViews2D.resize(ArrayLayers, VK_NULL_HANDLE);
    for (uint32_t Layer = 0u; Layer < ArrayLayers; ++Layer)
    {
        VkImageViewCreateInfo SliceSpec           = ViewSpec;
        SliceSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        SliceSpec.subresourceRange.baseArrayLayer = Layer;
        SliceSpec.subresourceRange.layerCount     = 1u;
        if (pfnCreateImageView_(Device_, &SliceSpec, nullptr, &OutSlot.SliceViews2D[Layer]) != VK_SUCCESS)
        {
            ReleaseDepthTarget(OutSlot);
            return false;
        }
    }

    OutSlot.Format        = ImageSpec.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.ArrayLayers   = ArrayLayers;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Transition into SHADER_READ_ONLY_OPTIMAL initially so compute descriptors are valid even before first shadow pass.
    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
    {
        return false;
    }
    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);

    VkImageMemoryBarrier Barrier{};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    Barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = OutSlot.Image;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    Barrier.subresourceRange.levelCount     = 1u;
    Barrier.subresourceRange.layerCount     = ArrayLayers;
    Barrier.srcAccessMask                   = 0u;
    Barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0u,
                           0u,
                           nullptr,
                           0u,
                           nullptr,
                           1u,
                           &Barrier);
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    return Ok;
}

void VulkanComputeExchange::ReleaseDepthTarget(DeviceDepthTargetSlot& Slot) noexcept
{
    if (!Ready())
    {
        Slot = DeviceDepthTargetSlot{};
        return;
    }
    if (Slot.AllocatedSize > 0u && AllocatedDeviceBytes_ >= Slot.AllocatedSize)
    {
        AllocatedDeviceBytes_ -= Slot.AllocatedSize;
    }
    for (VkImageView View : Slot.SliceViews2D)
    {
        if (View != VK_NULL_HANDLE)
        {
            pfnDestroyImageView_(Device_, View, nullptr);
        }
    }
    if (Slot.ArrayView != VK_NULL_HANDLE)
    {
        pfnDestroyImageView_(Device_, Slot.ArrayView, nullptr);
    }
    if (Slot.Image != VK_NULL_HANDLE)
    {
        pfnDestroyImage_(Device_, Slot.Image, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = DeviceDepthTargetSlot{};
}

//------------------------------------------------------------------------------------------------------------------------
//                      HARDWARE GRAPHICS RENDER PROCEDURES, FRAMEBUFFERS & RASTER PIPELINES
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeExchange::CreateSurfaceProcedureOnly(VkRenderPass& OutProcedure)
{
    ReleaseRenderProcedure(OutProcedure);
    if (!Ready() || !GraphicsSupported_)
    {
        return false;
    }

    std::array<VkAttachmentDescription, 7> Attachments{};
    std::array<VkAttachmentReference, 6>   ColorRefs{};
    for (uint32_t Idx = 0u; Idx < 6u; ++Idx)
    {
        Attachments[Idx].format         = VK_FORMAT_R16G16B16A16_SFLOAT;
        Attachments[Idx].samples        = VK_SAMPLE_COUNT_1_BIT;
        Attachments[Idx].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Attachments[Idx].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Attachments[Idx].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[Idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[Idx].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Attachments[Idx].finalLayout    = VK_IMAGE_LAYOUT_GENERAL;

        ColorRefs[Idx].attachment = Idx;
        ColorRefs[Idx].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    Attachments[6].format         = VK_FORMAT_D32_SFLOAT;
    Attachments[6].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[6].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[6].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[6].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[6].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[6].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[6].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference DepthRef{};
    DepthRef.attachment = 6u;
    DepthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription Subpass{};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = static_cast<uint32_t>(ColorRefs.size());
    Subpass.pColorAttachments       = ColorRefs.data();
    Subpass.pDepthStencilAttachment = &DepthRef;

    std::array<VkSubpassDependency, 2> Dependencies{};
    Dependencies[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    Dependencies[0].dstSubpass    = 0u;
    Dependencies[0].srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    Dependencies[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    Dependencies[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    Dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    Dependencies[1].srcSubpass    = 0u;
    Dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    Dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    Dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    Dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    Dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    VkRenderPassCreateInfo PassSpec{};
    PassSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    PassSpec.attachmentCount = static_cast<uint32_t>(Attachments.size());
    PassSpec.pAttachments    = Attachments.data();
    PassSpec.subpassCount    = 1u;
    PassSpec.pSubpasses      = &Subpass;
    PassSpec.dependencyCount = static_cast<uint32_t>(Dependencies.size());
    PassSpec.pDependencies   = Dependencies.data();

    return pfnCreateRenderPass_(Device_, &PassSpec, nullptr, &OutProcedure) == VK_SUCCESS;
}

bool VulkanComputeExchange::CreateShadowProcedureOnly(VkRenderPass& OutProcedure)
{
    ReleaseRenderProcedure(OutProcedure);
    if (!Ready() || !GraphicsSupported_)
    {
        return false;
    }

    VkAttachmentDescription DepthAttachment{};
    DepthAttachment.format         = VK_FORMAT_D32_SFLOAT;
    DepthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    DepthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    DepthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    DepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    DepthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    DepthAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference DepthRef{};
    DepthRef.attachment = 0u;
    DepthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription Subpass{};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = 0u;
    Subpass.pDepthStencilAttachment = &DepthRef;

    std::array<VkSubpassDependency, 2> Dependencies{};
    Dependencies[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    Dependencies[0].dstSubpass    = 0u;
    Dependencies[0].srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    Dependencies[0].dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    Dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    Dependencies[1].srcSubpass    = 0u;
    Dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    Dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    Dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    Dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    Dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo PassSpec{};
    PassSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    PassSpec.attachmentCount = 1u;
    PassSpec.pAttachments    = &DepthAttachment;
    PassSpec.subpassCount    = 1u;
    PassSpec.pSubpasses      = &Subpass;
    PassSpec.dependencyCount = static_cast<uint32_t>(Dependencies.size());
    PassSpec.pDependencies   = Dependencies.data();

    return pfnCreateRenderPass_(Device_, &PassSpec, nullptr, &OutProcedure) == VK_SUCCESS;
}

void VulkanComputeExchange::ReleaseRenderProcedure(VkRenderPass& Procedure) noexcept
{
    if (Ready() && Procedure != VK_NULL_HANDLE && pfnDestroyRenderPass_)
    {
        pfnDestroyRenderPass_(Device_, Procedure, nullptr);
    }
    Procedure = VK_NULL_HANDLE;
}

bool VulkanComputeExchange::CreateSurfaceFramebufferSet(VkRenderPass                 CompatibleProcedure,
                                                        const DeviceImageArraySlot&  TargetAtlas,
                                                        const DeviceDepthTargetSlot& DepthTarget,
                                                        SurfaceFramebufferSet&       OutSet)
{
    ReleaseSurfaceFramebufferSet(OutSet);
    if (!Ready() || !GraphicsSupported_ || CompatibleProcedure == VK_NULL_HANDLE || TargetAtlas.LayerViews2D.size() < 6u ||
        DepthTarget.SliceViews2D.empty() || TargetAtlas.Width != DepthTarget.Width || TargetAtlas.Height != DepthTarget.Height)
    {
        return false;
    }

    std::array<VkImageView, 7> Views{};
    for (uint32_t Idx = 0u; Idx < 6u; ++Idx)
    {
        Views[Idx] = TargetAtlas.LayerViews2D[Idx];
    }
    Views[6] = DepthTarget.SliceViews2D[0];

    VkFramebufferCreateInfo FbSpec{};
    FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbSpec.renderPass      = CompatibleProcedure;
    FbSpec.attachmentCount = static_cast<uint32_t>(Views.size());
    FbSpec.pAttachments    = Views.data();
    FbSpec.width           = TargetAtlas.Width;
    FbSpec.height          = TargetAtlas.Height;
    FbSpec.layers          = 1u;

    if (pfnCreateFramebuffer_(Device_, &FbSpec, nullptr, &OutSet.SurfaceTarget) != VK_SUCCESS)
    {
        return false;
    }
    OutSet.SurfaceProcedure = CompatibleProcedure;
    OutSet.Width            = TargetAtlas.Width;
    OutSet.Height           = TargetAtlas.Height;
    return true;
}

void VulkanComputeExchange::ReleaseSurfaceFramebufferSet(SurfaceFramebufferSet& Set) noexcept
{
    if (Ready() && Set.SurfaceTarget != VK_NULL_HANDLE && pfnDestroyFramebuffer_)
    {
        pfnDestroyFramebuffer_(Device_, Set.SurfaceTarget, nullptr);
    }
    Set = SurfaceFramebufferSet{};
}

bool VulkanComputeExchange::CreateShadowFramebufferSet(VkRenderPass                 CompatibleProcedure,
                                                       const DeviceDepthTargetSlot& ShadowDepthArray,
                                                       ShadowFramebufferSet&        OutSet)
{
    ReleaseShadowFramebufferSet(OutSet);
    if (!Ready() || !GraphicsSupported_ || CompatibleProcedure == VK_NULL_HANDLE || ShadowDepthArray.SliceViews2D.empty())
    {
        return false;
    }

    OutSet.CascadeTargets.resize(ShadowDepthArray.SliceViews2D.size(), VK_NULL_HANDLE);
    for (size_t Idx = 0u; Idx < ShadowDepthArray.SliceViews2D.size(); ++Idx)
    {
        VkImageView View = ShadowDepthArray.SliceViews2D[Idx];
        VkFramebufferCreateInfo FbSpec{};
        FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        FbSpec.renderPass      = CompatibleProcedure;
        FbSpec.attachmentCount = 1u;
        FbSpec.pAttachments    = &View;
        FbSpec.width           = ShadowDepthArray.Width;
        FbSpec.height          = ShadowDepthArray.Height;
        FbSpec.layers          = 1u;
        if (pfnCreateFramebuffer_(Device_, &FbSpec, nullptr, &OutSet.CascadeTargets[Idx]) != VK_SUCCESS)
        {
            ReleaseShadowFramebufferSet(OutSet);
            return false;
        }
    }
    OutSet.ShadowProcedure = CompatibleProcedure;
    OutSet.Resolution      = ShadowDepthArray.Width;
    OutSet.CascadeCount    = static_cast<uint32_t>(ShadowDepthArray.SliceViews2D.size());
    return true;
}

void VulkanComputeExchange::ReleaseShadowFramebufferSet(ShadowFramebufferSet& Set) noexcept
{
    if (Ready() && pfnDestroyFramebuffer_)
    {
        for (VkFramebuffer Fb : Set.CascadeTargets)
        {
            if (Fb != VK_NULL_HANDLE)
            {
                pfnDestroyFramebuffer_(Device_, Fb, nullptr);
            }
        }
    }
    Set = ShadowFramebufferSet{};
}

bool VulkanComputeExchange::LoadGraphicsProgram(const std::string&                VertSpvPath,
                                                const char*                       VertEntryName,
                                                const std::string&                FragSpvPath,
                                                const char*                       FragEntryName,
                                                std::span<const VkDescriptorType> BindingTypes,
                                                uint32_t                          PushConstantBytes,
                                                VkRenderPass                      CompatibleProcedure,
                                                uint32_t                          ColorAttachmentCount,
                                                VkCompareOp                       DepthCompare,
                                                bool                              DepthWrite,
                                                bool                              EnableDepthBias,
                                                GraphicsRasterProgramSlot&        OutProgram,
                                                std::string*                      OutDiagnostic)
{
    ReleaseGraphicsProgram(OutProgram);
    if (!Ready() || !GraphicsSupported_ || CompatibleProcedure == VK_NULL_HANDLE)
    {
        return false;
    }

    auto ReadSpirvModule = [&](const std::string& Path,
                               uint32_t           ExpectedExecModel,
                               const char*        PreferredEntry,
                               VkShaderModule&    OutModule,
                               std::string&       OutResolvedEntry,
                               uint32_t&          OutBytes) -> bool
    {
        std::ifstream File(Path, std::ios::binary | std::ios::ate);
        if (!File.is_open())
        {
            if (OutDiagnostic) *OutDiagnostic = "Could not open SPIR-V file: " + Path;
            return false;
        }
        const std::streamsize Size = File.tellg();
        if (Size <= 0 || (Size % 4) != 0)
        {
            if (OutDiagnostic) *OutDiagnostic = "Invalid SPIR-V size: " + Path;
            return false;
        }
        File.seekg(0, std::ios::beg);
        std::vector<uint32_t> Words(static_cast<size_t>(Size) / 4u);
        File.read(reinterpret_cast<char*>(Words.data()), Size);

        OutResolvedEntry = (PreferredEntry && PreferredEntry[0] != '\0') ? PreferredEntry : "main";
        std::vector<std::string> StageEntries;
        size_t                   WordIndex = 5u;
        while (WordIndex < Words.size())
        {
            const uint32_t WordCount = Words[WordIndex] >> 16u;
            const uint32_t Opcode    = Words[WordIndex] & 0xFFFFu;
            if (WordCount == 0u || WordIndex + WordCount > Words.size())
            {
                break;
            }
            if (Opcode == 15u && WordCount >= 4u && Words[WordIndex + 1u] == ExpectedExecModel)
            {
                const char*  RawChars = reinterpret_cast<const char*>(&Words[WordIndex + 3u]);
                const size_t MaxBytes = (WordCount - 3u) * sizeof(uint32_t);
                size_t       Length   = 0u;
                while (Length < MaxBytes && RawChars[Length] != '\0')
                {
                    ++Length;
                }
                StageEntries.emplace_back(RawChars, Length);
            }
            WordIndex += WordCount;
        }
        if (!StageEntries.empty() &&
            std::find(StageEntries.begin(), StageEntries.end(), OutResolvedEntry) == StageEntries.end())
        {
            OutResolvedEntry = StageEntries.front();
        }

        VkShaderModuleCreateInfo UnitSpec{};
        UnitSpec.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        UnitSpec.codeSize = static_cast<size_t>(Size);
        UnitSpec.pCode    = Words.data();
        if (pfnCreateShaderUnit_(Device_, &UnitSpec, nullptr, &OutModule) != VK_SUCCESS)
        {
            if (OutDiagnostic) *OutDiagnostic = "vkCreateShaderModule failed: " + Path;
            return false;
        }
        OutBytes += static_cast<uint32_t>(Size);
        return true;
    };

    std::string ResolvedVertEntry = "main";
    std::string ResolvedFragEntry = "main";
    if (!ReadSpirvModule(VertSpvPath, 0u, VertEntryName, OutProgram.VertexUnit, ResolvedVertEntry, OutProgram.SpirvByteCount))
    {
        ReleaseGraphicsProgram(OutProgram);
        return false;
    }
    const bool HasFragment = !FragSpvPath.empty();
    if (HasFragment &&
        !ReadSpirvModule(FragSpvPath, 4u, FragEntryName, OutProgram.FragmentUnit, ResolvedFragEntry, OutProgram.SpirvByteCount))
    {
        ReleaseGraphicsProgram(OutProgram);
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingTypes.size());
    for (size_t Idx = 0u; Idx < Bindings.size(); ++Idx)
    {
        Bindings[Idx].binding         = static_cast<uint32_t>(Idx);
        Bindings[Idx].descriptorType  = BindingTypes[Idx];
        Bindings[Idx].descriptorCount = 1u;
        Bindings[Idx].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo SetLayoutSpec{};
    SetLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutSpec.bindingCount = static_cast<uint32_t>(Bindings.size());
    SetLayoutSpec.pBindings    = Bindings.data();
    if (pfnCreateDescriptorSetLayout_(Device_, &SetLayoutSpec, nullptr, &OutProgram.SetLayout) != VK_SUCCESS)
    {
        ReleaseGraphicsProgram(OutProgram);
        return false;
    }

    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = PushConstantBytes;

    VkPipelineLayoutCreateInfo LayoutSpec{};
    LayoutSpec.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutSpec.setLayoutCount         = 1u;
    LayoutSpec.pSetLayouts            = &OutProgram.SetLayout;
    LayoutSpec.pushConstantRangeCount = PushConstantBytes > 0u ? 1u : 0u;
    LayoutSpec.pPushConstantRanges    = PushConstantBytes > 0u ? &PushRange : nullptr;
    if (pfnCreatePipelineLayout_(Device_, &LayoutSpec, nullptr, &OutProgram.Layout) != VK_SUCCESS)
    {
        ReleaseGraphicsProgram(OutProgram);
        return false;
    }

    std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
    {
        VkPipelineShaderStageCreateInfo Vs{};
        Vs.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Vs.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        Vs.module = OutProgram.VertexUnit;
        Vs.pName  = ResolvedVertEntry.c_str();
        ShaderStages.push_back(Vs);
    }
    if (HasFragment)
    {
        VkPipelineShaderStageCreateInfo Fs{};
        Fs.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Fs.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        Fs.module = OutProgram.FragmentUnit;
        Fs.pName  = ResolvedFragEntry.c_str();
        ShaderStages.push_back(Fs);
    }

    VkPipelineVertexInputStateCreateInfo VertexInput{};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    InputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportSpec{};
    ViewportSpec.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportSpec.viewportCount = 1u;
    ViewportSpec.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo RasterSpec{};
    RasterSpec.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterSpec.polygonMode      = VK_POLYGON_MODE_FILL;
    RasterSpec.cullMode         = VK_CULL_MODE_NONE;
    RasterSpec.frontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RasterSpec.depthBiasEnable  = EnableDepthBias ? VK_TRUE : VK_FALSE;
    RasterSpec.lineWidth        = 1.0f;

    VkPipelineMultisampleStateCreateInfo MultisampleSpec{};
    MultisampleSpec.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MultisampleSpec.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DepthSpec{};
    DepthSpec.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthSpec.depthTestEnable  = VK_TRUE;
    DepthSpec.depthWriteEnable = DepthWrite ? VK_TRUE : VK_FALSE;
    DepthSpec.depthCompareOp   = DepthCompare;

    std::vector<VkPipelineColorBlendAttachmentState> BlendAttachments(ColorAttachmentCount);
    for (auto& Blend : BlendAttachments)
    {
        Blend.blendEnable    = VK_FALSE;
        Blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo BlendSpec{};
    BlendSpec.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    BlendSpec.attachmentCount = ColorAttachmentCount;
    BlendSpec.pAttachments    = BlendAttachments.empty() ? nullptr : BlendAttachments.data();

    std::vector<VkDynamicState> DynamicItems = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    if (EnableDepthBias)
    {
        DynamicItems.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }
    VkPipelineDynamicStateCreateInfo DynamicSpec{};
    DynamicSpec.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicSpec.dynamicStateCount = static_cast<uint32_t>(DynamicItems.size());
    DynamicSpec.pDynamicStates    = DynamicItems.data();

    VkGraphicsPipelineCreateInfo PipeSpec{};
    PipeSpec.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeSpec.stageCount          = static_cast<uint32_t>(ShaderStages.size());
    PipeSpec.pStages             = ShaderStages.data();
    PipeSpec.pVertexInputState   = &VertexInput;
    PipeSpec.pInputAssemblyState = &InputAssembly;
    PipeSpec.pViewportState      = &ViewportSpec;
    PipeSpec.pRasterizationState = &RasterSpec;
    PipeSpec.pMultisampleState   = &MultisampleSpec;
    PipeSpec.pDepthStencilState  = &DepthSpec;
    PipeSpec.pColorBlendState    = &BlendSpec;
    PipeSpec.pDynamicState       = &DynamicSpec;
    PipeSpec.layout              = OutProgram.Layout;
    PipeSpec.renderPass          = CompatibleProcedure;
    PipeSpec.subpass             = 0u;

    const auto CompileStart = std::chrono::steady_clock::now();
    if (pfnCreateGraphicsPipelines_(Device_, PipelineArchiveHandle_, 1u, &PipeSpec, nullptr, &OutProgram.Pipeline) != VK_SUCCESS)
    {
        if (OutDiagnostic) *OutDiagnostic = "vkCreateGraphicsPipelines failed for " + VertSpvPath;
        ReleaseGraphicsProgram(OutProgram);
        return false;
    }
    OutProgram.PipelineCompileMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CompileStart).count();
    OutProgram.BindingTypes.assign(BindingTypes.begin(), BindingTypes.end());
    PersistPipelineArchive();
    return true;
}

void VulkanComputeExchange::ReleaseGraphicsProgram(GraphicsRasterProgramSlot& Program) noexcept
{
    if (!Ready())
    {
        Program = GraphicsRasterProgramSlot{};
        return;
    }
    if (Program.Pipeline != VK_NULL_HANDLE)
    {
        pfnDestroyPipeline_(Device_, Program.Pipeline, nullptr);
    }
    if (Program.Layout != VK_NULL_HANDLE)
    {
        pfnDestroyPipelineLayout_(Device_, Program.Layout, nullptr);
    }
    if (Program.SetLayout != VK_NULL_HANDLE)
    {
        pfnDestroyDescriptorSetLayout_(Device_, Program.SetLayout, nullptr);
    }
    if (Program.FragmentUnit != VK_NULL_HANDLE)
    {
        pfnDestroyShaderUnit_(Device_, Program.FragmentUnit, nullptr);
    }
    if (Program.VertexUnit != VK_NULL_HANDLE)
    {
        pfnDestroyShaderUnit_(Device_, Program.VertexUnit, nullptr);
    }
    Program = GraphicsRasterProgramSlot{};
}

bool VulkanComputeExchange::AllocateUpdatedDescriptorSet(VkDescriptorSetLayout                      SetLayout,
                                                         std::span<const VkDescriptorType>          BindingTypes,
                                                         std::span<const ComputeDescriptorArgument> Arguments,
                                                         VkDescriptorSet&                           OutSet)
{
    OutSet = VK_NULL_HANDLE;
    if (!Ready() || SetLayout == VK_NULL_HANDLE || Arguments.size() != BindingTypes.size())
    {
        return false;
    }

    const bool       ExternalActive = (ActiveExternalStream_ != VK_NULL_HANDLE);
    VkDescriptorPool TargetPool     = ExternalActive ? ExternalDescriptorPools_[ActiveExternalSlot_] : DescriptorPool_;
    if (!ExternalActive)
    {
        pfnResetDescriptorPool_(Device_, TargetPool, 0u);
    }

    VkDescriptorSetAllocateInfo SetAlloc{};
    SetAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAlloc.descriptorPool     = TargetPool;
    SetAlloc.descriptorSetCount = 1u;
    SetAlloc.pSetLayouts        = &SetLayout;
    if (pfnAllocateDescriptorSets_(Device_, &SetAlloc, &OutSet) != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkDescriptorBufferInfo> SpanDescriptors(Arguments.size());
    std::vector<VkDescriptorImageInfo>  ImageDescriptors(Arguments.size());
    std::vector<VkWriteDescriptorSet>   Writes(Arguments.size());

    for (size_t SlotIndex = 0u; SlotIndex < Arguments.size(); ++SlotIndex)
    {
        Writes[SlotIndex].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[SlotIndex].dstSet          = OutSet;
        Writes[SlotIndex].dstBinding      = static_cast<uint32_t>(SlotIndex);
        Writes[SlotIndex].descriptorCount = 1u;
        Writes[SlotIndex].descriptorType  = BindingTypes[SlotIndex];

        if (BindingTypes[SlotIndex] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            if (!Arguments[SlotIndex].MemorySpan || Arguments[SlotIndex].MemorySpan->Handle == VK_NULL_HANDLE)
            {
                return false;
            }
            SpanDescriptors[SlotIndex].buffer = Arguments[SlotIndex].MemorySpan->Handle;
            SpanDescriptors[SlotIndex].offset = 0u;
            SpanDescriptors[SlotIndex].range  = VK_WHOLE_SIZE;
            Writes[SlotIndex].pBufferInfo     = &SpanDescriptors[SlotIndex];
        }
        else if (Arguments[SlotIndex].VolumeTexture != nullptr)
        {
            if (Arguments[SlotIndex].VolumeTexture->View3D == VK_NULL_HANDLE)
            {
                return false;
            }
            ImageDescriptors[SlotIndex].imageView   = Arguments[SlotIndex].VolumeTexture->View3D;
            ImageDescriptors[SlotIndex].sampler     = LinearClampSampler_;
            ImageDescriptors[SlotIndex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Writes[SlotIndex].pImageInfo            = &ImageDescriptors[SlotIndex];
        }
        else if (Arguments[SlotIndex].DepthTarget != nullptr)
        {
            if (Arguments[SlotIndex].DepthTarget->ArrayView == VK_NULL_HANDLE)
            {
                return false;
            }
            ImageDescriptors[SlotIndex].imageView   = Arguments[SlotIndex].DepthTarget->ArrayView;
            ImageDescriptors[SlotIndex].sampler     = NearestClampSampler_;
            ImageDescriptors[SlotIndex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Writes[SlotIndex].pImageInfo            = &ImageDescriptors[SlotIndex];
        }
        else if (Arguments[SlotIndex].ViewportImage != nullptr)
        {
            if (Arguments[SlotIndex].ViewportImage->View2D == VK_NULL_HANDLE)
            {
                return false;
            }
            ImageDescriptors[SlotIndex].imageView = Arguments[SlotIndex].ViewportImage->View2D;
            ImageDescriptors[SlotIndex].sampler =
                (BindingTypes[SlotIndex] == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? LinearClampSampler_ : VK_NULL_HANDLE;
            ImageDescriptors[SlotIndex].imageLayout = (BindingTypes[SlotIndex] == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                                          ? VK_IMAGE_LAYOUT_GENERAL
                                                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Writes[SlotIndex].pImageInfo            = &ImageDescriptors[SlotIndex];
        }
        else
        {
            if (!Arguments[SlotIndex].ImageArray || Arguments[SlotIndex].ImageArray->ArrayView == VK_NULL_HANDLE)
            {
                return false;
            }
            ImageDescriptors[SlotIndex].imageView = Arguments[SlotIndex].ImageArray->ArrayView;
            ImageDescriptors[SlotIndex].sampler   = (BindingTypes[SlotIndex] == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                                                        ? (Arguments[SlotIndex].ImageArray->Format == VK_FORMAT_R8G8B8A8_SRGB
                                                               ? LinearRepeatSampler_
                                                               : LinearClampSampler_)
                                                        : VK_NULL_HANDLE;
            ImageDescriptors[SlotIndex].imageLayout = (Arguments[SlotIndex].ImageArray->Format == VK_FORMAT_R8G8B8A8_SRGB)
                                                          ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                          : VK_IMAGE_LAYOUT_GENERAL;
            Writes[SlotIndex].pImageInfo            = &ImageDescriptors[SlotIndex];
        }
    }
    pfnUpdateDescriptorSets_(Device_, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    return true;
}

bool VulkanComputeExchange::ExecuteShadowCascadeDraws(const GraphicsRasterProgramSlot&           ShadowProgram,
                                                      const ShadowFramebufferSet&                ShadowTargets,
                                                      DeviceDepthTargetSlot&                     ShadowDepthSlot,
                                                      std::span<const ComputeDescriptorArgument> Arguments,
                                                      uint32_t                                   CascadeIndex,
                                                      std::span<const RasterDrawItem>            Draws)
{
    LastDispatchGpuMilliseconds_ = 0.0f;
    LastDispatchCpuMilliseconds_ = 0.0f;
    if (!Ready() || !GraphicsSupported_ || ShadowProgram.Pipeline == VK_NULL_HANDLE ||
        CascadeIndex >= ShadowTargets.CascadeTargets.size())
    {
        return false;
    }

    const auto      CpuStart       = std::chrono::steady_clock::now();
    const bool      ExternalActive = (ActiveExternalStream_ != VK_NULL_HANDLE);
    VkDescriptorSet DescSet        = VK_NULL_HANDLE;
    if (!AllocateUpdatedDescriptorSet(ShadowProgram.SetLayout, ShadowProgram.BindingTypes, Arguments, DescSet))
    {
        return false;
    }

    VkCommandBuffer CommandStream = ActiveExternalStream_;
    if (!ExternalActive)
    {
        VkCommandBufferAllocateInfo StreamAlloc{};
        StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        StreamAlloc.commandPool        = CommandPool_;
        StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        StreamAlloc.commandBufferCount = 1u;
        if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
        {
            return false;
        }
        VkCommandBufferBeginInfo BeginSpec{};
        BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        pfnBeginCommandStream_(CommandStream, &BeginSpec);
    }

    uint32_t   QueryStartIndex = UINT32_MAX;
    const bool UseTimestamps   = TimestampsSupported_ && TimestampQueryPool_ != VK_NULL_HANDLE;
    if (UseTimestamps)
    {
        if (ExternalActive)
        {
            const uint32_t LocalIndex = ExternalSlotDispatchCounts_[ActiveExternalSlot_];
            if (LocalIndex < MaxTimedDispatchesSlot)
            {
                QueryStartIndex = ActiveExternalSlot_ * MaxTimedDispatchesSlot * 2u + LocalIndex * 2u;
                ++ExternalSlotDispatchCounts_[ActiveExternalSlot_];
                ExternalSlotRecorded_[ActiveExternalSlot_] = true;
            }
        }
        else
        {
            QueryStartIndex = StandaloneQueryOffset;
            pfnCmdResetQueryPool_(CommandStream, TimestampQueryPool_, StandaloneQueryOffset, 2u);
        }
    }

    if (QueryStartIndex != UINT32_MAX)
    {
        pfnCmdWriteTimestamp_(CommandStream, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, TimestampQueryPool_, QueryStartIndex);
    }

    VkClearValue ClearDepth{};
    ClearDepth.depthStencil = {1.0f, 0u};

    VkRenderPassBeginInfo RpBegin{};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = ShadowTargets.ShadowProcedure;
    RpBegin.framebuffer       = ShadowTargets.CascadeTargets[CascadeIndex];
    RpBegin.renderArea.offset = {0, 0};
    RpBegin.renderArea.extent = {ShadowTargets.Resolution, ShadowTargets.Resolution};
    RpBegin.clearValueCount   = 1u;
    RpBegin.pClearValues      = &ClearDepth;

    pfnCmdBeginRenderPass_(CommandStream, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport Viewport{0.0f, 0.0f, static_cast<float>(ShadowTargets.Resolution), static_cast<float>(ShadowTargets.Resolution), 0.0f, 1.0f};
    VkRect2D   Scissor{{0, 0}, {ShadowTargets.Resolution, ShadowTargets.Resolution}};
    pfnCmdSetViewport_(CommandStream, 0u, 1u, &Viewport);
    pfnCmdSetScissor_(CommandStream, 0u, 1u, &Scissor);
    pfnCmdSetDepthBias_(CommandStream, 1.35f, 0.0f, 1.85f);

    pfnCmdBindPipeline_(CommandStream, VK_PIPELINE_BIND_POINT_GRAPHICS, ShadowProgram.Pipeline);
    pfnCmdBindDescriptorSets_(CommandStream, VK_PIPELINE_BIND_POINT_GRAPHICS, ShadowProgram.Layout, 0u, 1u, &DescSet, 0u, nullptr);

    for (const RasterDrawItem& Item : Draws)
    {
        if (Item.VertexCount == 0u)
        {
            continue;
        }
        pfnCmdPushConstants_(CommandStream,
                             ShadowProgram.Layout,
                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0u,
                             static_cast<uint32_t>(sizeof(Item.PushWords)),
                             Item.PushWords.data());
        pfnCmdDraw_(CommandStream, Item.VertexCount, 1u, 0u, 0u);
    }

    pfnCmdEndRenderPass_(CommandStream);
    ShadowDepthSlot.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (QueryStartIndex != UINT32_MAX)
    {
        pfnCmdWriteTimestamp_(CommandStream, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, TimestampQueryPool_, QueryStartIndex + 1u);
    }

    if (ExternalActive)
    {
        LastDispatchCpuMilliseconds_ =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuStart).count();
        return true;
    }

    pfnEndCommandStream_(CommandStream);
    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);

    if (Ok && QueryStartIndex != UINT32_MAX)
    {
        uint64_t Ticks[2] = {0u, 0u};
        if (pfnGetQueryPoolResults_(Device_,
                                    TimestampQueryPool_,
                                    StandaloneQueryOffset,
                                    2u,
                                    sizeof(Ticks),
                                    Ticks,
                                    sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS &&
            Ticks[1] >= Ticks[0])
        {
            LastDispatchGpuMilliseconds_ = static_cast<float>(static_cast<double>(Ticks[1] - Ticks[0]) *
                                                              static_cast<double>(TimestampPeriodNanoseconds_) * 1.0e-6);
        }
    }
    LastDispatchCpuMilliseconds_ =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuStart).count();
    return Ok;
}

bool VulkanComputeExchange::ExecuteSurfaceAndForwardDraws(const GraphicsRasterProgramSlot&           OpaqueProgram,
                                                          const GraphicsRasterProgramSlot&           ForwardGlassProgram,
                                                          const SurfaceFramebufferSet&               SurfaceTargets,
                                                          DeviceImageArraySlot&                      TargetAtlas,
                                                          DeviceDepthTargetSlot&                     DepthTarget,
                                                          std::span<const ComputeDescriptorArgument> Arguments,
                                                          std::span<const RasterDrawItem>            OpaqueDraws,
                                                          std::span<const RasterDrawItem>            ForwardGlassDraws)
{
    LastDispatchGpuMilliseconds_ = 0.0f;
    LastDispatchCpuMilliseconds_ = 0.0f;
    if (!Ready() || !GraphicsSupported_ || OpaqueProgram.Pipeline == VK_NULL_HANDLE || SurfaceTargets.SurfaceTarget == VK_NULL_HANDLE)
    {
        return false;
    }

    const auto      CpuStart       = std::chrono::steady_clock::now();
    const bool      ExternalActive = (ActiveExternalStream_ != VK_NULL_HANDLE);
    VkDescriptorSet DescSet        = VK_NULL_HANDLE;
    if (!AllocateUpdatedDescriptorSet(OpaqueProgram.SetLayout, OpaqueProgram.BindingTypes, Arguments, DescSet))
    {
        return false;
    }

    VkDescriptorSet GlassDescSet = DescSet;
    if (!ForwardGlassDraws.empty() && ForwardGlassProgram.Pipeline != VK_NULL_HANDLE && !ExternalActive)
    {
        // Note: in standalone mode AllocateUpdatedDescriptorSet resets DescriptorPool_; allocate second set without reset!
        VkDescriptorSetAllocateInfo SetAlloc{};
        SetAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        SetAlloc.descriptorPool     = DescriptorPool_;
        SetAlloc.descriptorSetCount = 1u;
        SetAlloc.pSetLayouts        = &ForwardGlassProgram.SetLayout;
        if (pfnAllocateDescriptorSets_(Device_, &SetAlloc, &GlassDescSet) == VK_SUCCESS)
        {
            std::vector<VkDescriptorBufferInfo> SpanDesc(Arguments.size());
            std::vector<VkDescriptorImageInfo>  ImgDesc(Arguments.size());
            std::vector<VkWriteDescriptorSet>   Writes(Arguments.size());
            for (size_t SlotIndex = 0u; SlotIndex < Arguments.size(); ++SlotIndex)
            {
                Writes[SlotIndex].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                Writes[SlotIndex].dstSet          = GlassDescSet;
                Writes[SlotIndex].dstBinding      = static_cast<uint32_t>(SlotIndex);
                Writes[SlotIndex].descriptorCount = 1u;
                Writes[SlotIndex].descriptorType  = ForwardGlassProgram.BindingTypes[SlotIndex];
                if (ForwardGlassProgram.BindingTypes[SlotIndex] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                {
                    SpanDesc[SlotIndex]           = {Arguments[SlotIndex].MemorySpan->Handle, 0u, VK_WHOLE_SIZE};
                    Writes[SlotIndex].pBufferInfo = &SpanDesc[SlotIndex];
                }
                else
                {
                    ImgDesc[SlotIndex]           = {LinearRepeatSampler_, Arguments[SlotIndex].ImageArray->ArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    Writes[SlotIndex].pImageInfo = &ImgDesc[SlotIndex];
                }
            }
            pfnUpdateDescriptorSets_(Device_, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
        }
    }
    else if (!ForwardGlassDraws.empty() && ForwardGlassProgram.Pipeline != VK_NULL_HANDLE && ExternalActive)
    {
        (void)AllocateUpdatedDescriptorSet(ForwardGlassProgram.SetLayout, ForwardGlassProgram.BindingTypes, Arguments, GlassDescSet);
    }

    VkCommandBuffer CommandStream = ActiveExternalStream_;
    if (!ExternalActive)
    {
        VkCommandBufferAllocateInfo StreamAlloc{};
        StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        StreamAlloc.commandPool        = CommandPool_;
        StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        StreamAlloc.commandBufferCount = 1u;
        if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
        {
            return false;
        }
        VkCommandBufferBeginInfo BeginSpec{};
        BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        pfnBeginCommandStream_(CommandStream, &BeginSpec);
    }

    uint32_t   QueryStartIndex = UINT32_MAX;
    const bool UseTimestamps   = TimestampsSupported_ && TimestampQueryPool_ != VK_NULL_HANDLE;
    if (UseTimestamps)
    {
        if (ExternalActive)
        {
            const uint32_t LocalIndex = ExternalSlotDispatchCounts_[ActiveExternalSlot_];
            if (LocalIndex < MaxTimedDispatchesSlot)
            {
                QueryStartIndex = ActiveExternalSlot_ * MaxTimedDispatchesSlot * 2u + LocalIndex * 2u;
                ++ExternalSlotDispatchCounts_[ActiveExternalSlot_];
                ExternalSlotRecorded_[ActiveExternalSlot_] = true;
            }
        }
        else
        {
            QueryStartIndex = StandaloneQueryOffset;
            pfnCmdResetQueryPool_(CommandStream, TimestampQueryPool_, StandaloneQueryOffset, 2u);
        }
    }

    if (QueryStartIndex != UINT32_MAX)
    {
        pfnCmdWriteTimestamp_(CommandStream, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, TimestampQueryPool_, QueryStartIndex);
    }

    std::array<VkClearValue, 7> ClearValues{};
    for (uint32_t Idx = 0u; Idx < 6u; ++Idx)
    {
        ClearValues[Idx].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    }
    ClearValues[6].depthStencil = {0.0f, 0u}; // Reversed-Z far plane = 0.0

    VkRenderPassBeginInfo RpBegin{};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = SurfaceTargets.SurfaceProcedure;
    RpBegin.framebuffer       = SurfaceTargets.SurfaceTarget;
    RpBegin.renderArea.offset = {0, 0};
    RpBegin.renderArea.extent = {SurfaceTargets.Width, SurfaceTargets.Height};
    RpBegin.clearValueCount   = static_cast<uint32_t>(ClearValues.size());
    RpBegin.pClearValues      = ClearValues.data();

    pfnCmdBeginRenderPass_(CommandStream, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport Viewport{0.0f, 0.0f, static_cast<float>(SurfaceTargets.Width), static_cast<float>(SurfaceTargets.Height), 0.0f, 1.0f};
    VkRect2D   Scissor{{0, 0}, {SurfaceTargets.Width, SurfaceTargets.Height}};
    pfnCmdSetViewport_(CommandStream, 0u, 1u, &Viewport);
    pfnCmdSetScissor_(CommandStream, 0u, 1u, &Scissor);

    pfnCmdBindPipeline_(CommandStream, VK_PIPELINE_BIND_POINT_GRAPHICS, OpaqueProgram.Pipeline);
    pfnCmdBindDescriptorSets_(CommandStream, VK_PIPELINE_BIND_POINT_GRAPHICS, OpaqueProgram.Layout, 0u, 1u, &DescSet, 0u, nullptr);

    for (const RasterDrawItem& Item : OpaqueDraws)
    {
        if (Item.VertexCount == 0u)
        {
            continue;
        }
        pfnCmdPushConstants_(CommandStream,
                             OpaqueProgram.Layout,
                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0u,
                             static_cast<uint32_t>(sizeof(Item.PushWords)),
                             Item.PushWords.data());
        pfnCmdDraw_(CommandStream, Item.VertexCount, 1u, 0u, 0u);
    }

    if (!ForwardGlassDraws.empty() && ForwardGlassProgram.Pipeline != VK_NULL_HANDLE)
    {
        pfnCmdBindPipeline_(CommandStream, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardGlassProgram.Pipeline);
        pfnCmdBindDescriptorSets_(CommandStream,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  ForwardGlassProgram.Layout,
                                  0u,
                                  1u,
                                  &GlassDescSet,
                                  0u,
                                  nullptr);
        for (const RasterDrawItem& Item : ForwardGlassDraws)
        {
            if (Item.VertexCount == 0u)
            {
                continue;
            }
            pfnCmdPushConstants_(CommandStream,
                                 ForwardGlassProgram.Layout,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                 0u,
                                 static_cast<uint32_t>(sizeof(Item.PushWords)),
                                 Item.PushWords.data());
            pfnCmdDraw_(CommandStream, Item.VertexCount, 1u, 0u, 0u);
        }
    }

    pfnCmdEndRenderPass_(CommandStream);
    TargetAtlas.CurrentLayout = VK_IMAGE_LAYOUT_GENERAL;
    DepthTarget.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (QueryStartIndex != UINT32_MAX)
    {
        pfnCmdWriteTimestamp_(CommandStream, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, TimestampQueryPool_, QueryStartIndex + 1u);
    }

    VkMemoryBarrier Barrier{};
    Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0u,
                           1u,
                           &Barrier,
                           0u,
                           nullptr,
                           0u,
                           nullptr);

    if (ExternalActive)
    {
        LastDispatchCpuMilliseconds_ =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuStart).count();
        return true;
    }

    pfnEndCommandStream_(CommandStream);
    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);

    if (Ok && QueryStartIndex != UINT32_MAX)
    {
        uint64_t Ticks[2] = {0u, 0u};
        if (pfnGetQueryPoolResults_(Device_,
                                    TimestampQueryPool_,
                                    StandaloneQueryOffset,
                                    2u,
                                    sizeof(Ticks),
                                    Ticks,
                                    sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS &&
            Ticks[1] >= Ticks[0])
        {
            LastDispatchGpuMilliseconds_ = static_cast<float>(static_cast<double>(Ticks[1] - Ticks[0]) *
                                                              static_cast<double>(TimestampPeriodNanoseconds_) * 1.0e-6);
        }
    }
    LastDispatchCpuMilliseconds_ =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuStart).count();
    return Ok;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPUTE PROGRAM & DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeExchange::LoadComputeProgram(const std::string&                SpvFilePath,
                                               const char*                       EntryPointName,
                                               std::span<const VkDescriptorType> BindingTypes,
                                               uint32_t                          PushConstantBytes,
                                               ComputeProgramSlot&               OutProgram,
                                               std::string*                      OutDiagnostic)
{
    ReleaseProgram(OutProgram);
    if (!Ready())
    {
        return false;
    }

    std::ifstream File(SpvFilePath, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Could not open SPIR-V file: " + SpvFilePath;
        }
        return false;
    }
    const std::streamsize ByteSize = File.tellg();
    if (ByteSize <= 0 || (ByteSize % 4) != 0)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Invalid SPIR-V byte size: " + SpvFilePath;
        }
        return false;
    }
    File.seekg(0, std::ios::beg);
    std::vector<uint32_t> Words(static_cast<size_t>(ByteSize) / 4u);
    File.read(reinterpret_cast<char*>(Words.data()), ByteSize);

    OutProgram.SpirvByteCount        = static_cast<uint32_t>(ByteSize);
    OutProgram.SpirvFingerprintFnv64 = ComputeFnv1a64(reinterpret_cast<const uint8_t*>(Words.data()), static_cast<size_t>(ByteSize));

    const auto               ModuleStart = std::chrono::steady_clock::now();
    VkShaderModuleCreateInfo UnitSpec{};
    UnitSpec.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    UnitSpec.codeSize = static_cast<size_t>(ByteSize);
    UnitSpec.pCode    = Words.data();
    if (pfnCreateShaderUnit_(Device_, &UnitSpec, nullptr, &OutProgram.ShaderUnit) != VK_SUCCESS)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateShaderModule failed for " + SpvFilePath;
        }
        return false;
    }
    OutProgram.ShaderModuleMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - ModuleStart).count();

    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingTypes.size());
    for (size_t SlotIndex = 0u; SlotIndex < Bindings.size(); ++SlotIndex)
    {
        Bindings[SlotIndex].binding         = static_cast<uint32_t>(SlotIndex);
        Bindings[SlotIndex].descriptorType  = BindingTypes[SlotIndex];
        Bindings[SlotIndex].descriptorCount = 1u;
        Bindings[SlotIndex].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo SetLayoutSpec{};
    SetLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutSpec.bindingCount = static_cast<uint32_t>(Bindings.size());
    SetLayoutSpec.pBindings    = Bindings.data();
    if (pfnCreateDescriptorSetLayout_(Device_, &SetLayoutSpec, nullptr, &OutProgram.SetLayout) != VK_SUCCESS)
    {
        ReleaseProgram(OutProgram);
        return false;
    }

    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = PushConstantBytes;

    VkPipelineLayoutCreateInfo PipeLayoutSpec{};
    PipeLayoutSpec.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayoutSpec.setLayoutCount         = 1u;
    PipeLayoutSpec.pSetLayouts            = &OutProgram.SetLayout;
    PipeLayoutSpec.pushConstantRangeCount = PushConstantBytes > 0u ? 1u : 0u;
    PipeLayoutSpec.pPushConstantRanges    = PushConstantBytes > 0u ? &PushRange : nullptr;

    if (pfnCreatePipelineLayout_(Device_, &PipeLayoutSpec, nullptr, &OutProgram.Layout) != VK_SUCCESS)
    {
        ReleaseProgram(OutProgram);
        return false;
    }

    std::string ResolvedEntryName = (EntryPointName && EntryPointName[0] != '\0') ? EntryPointName : "main";
    {
        std::vector<std::string> ComputeEntries;
        size_t                   WordIndex = 5u;
        while (WordIndex < Words.size())
        {
            const uint32_t WordCount = Words[WordIndex] >> 16u;
            const uint32_t Opcode    = Words[WordIndex] & 0xFFFFu;
            if (WordCount == 0u || WordIndex + WordCount > Words.size())
            {
                break;
            }
            if (Opcode == 15u && WordCount >= 4u && Words[WordIndex + 1u] == 5u)
            {
                const char*  RawChars = reinterpret_cast<const char*>(&Words[WordIndex + 3u]);
                const size_t MaxBytes = (WordCount - 3u) * sizeof(uint32_t);
                size_t       Length   = 0u;
                while (Length < MaxBytes && RawChars[Length] != '\0')
                {
                    ++Length;
                }
                ComputeEntries.emplace_back(RawChars, Length);
            }
            WordIndex += WordCount;
        }
        if (!ComputeEntries.empty() &&
            std::find(ComputeEntries.begin(), ComputeEntries.end(), ResolvedEntryName) == ComputeEntries.end())
        {
            ResolvedEntryName = ComputeEntries.front();
        }
    }
    OutProgram.EntryName = ResolvedEntryName;

    VkComputePipelineCreateInfo PipeSpec{};
    PipeSpec.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    PipeSpec.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipeSpec.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    PipeSpec.stage.module = OutProgram.ShaderUnit;
    PipeSpec.stage.pName  = ResolvedEntryName.c_str();
    PipeSpec.layout       = OutProgram.Layout;

    const auto CompileStart = std::chrono::steady_clock::now();
    if (pfnCreateComputePipelines_(Device_, PipelineArchiveHandle_, 1u, &PipeSpec, nullptr, &OutProgram.Pipeline) != VK_SUCCESS)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = std::string("vkCreateComputePipelines failed for entry ") + ResolvedEntryName;
        }
        ReleaseProgram(OutProgram);
        return false;
    }
    OutProgram.PipelineCompileMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CompileStart).count();

    OutProgram.BindingTypes.assign(BindingTypes.begin(), BindingTypes.end());
    // Atomic per-pipeline checkpointing prevents cold-JIT repetition on GTX Pascal/Turing GPUs.
    PersistPipelineArchive();
    return true;
}

void VulkanComputeExchange::ReleaseProgram(ComputeProgramSlot& Program) noexcept
{
    if (!Ready())
    {
        Program = ComputeProgramSlot{};
        return;
    }
    if (Program.Pipeline != VK_NULL_HANDLE)
    {
        pfnDestroyPipeline_(Device_, Program.Pipeline, nullptr);
    }
    if (Program.Layout != VK_NULL_HANDLE)
    {
        pfnDestroyPipelineLayout_(Device_, Program.Layout, nullptr);
    }
    if (Program.SetLayout != VK_NULL_HANDLE)
    {
        pfnDestroyDescriptorSetLayout_(Device_, Program.SetLayout, nullptr);
    }
    if (Program.ShaderUnit != VK_NULL_HANDLE)
    {
        pfnDestroyShaderUnit_(Device_, Program.ShaderUnit, nullptr);
    }
    Program = ComputeProgramSlot{};
}

bool VulkanComputeExchange::DispatchCompute(const ComputeProgramSlot&                  Program,
                                            std::span<const ComputeDescriptorArgument> Arguments,
                                            const void*                                PushConstants,
                                            uint32_t                                   PushConstantBytes,
                                            uint32_t                                   GroupCountX,
                                            uint32_t                                   GroupCountY,
                                            uint32_t                                   GroupCountZ)
{
    LastDispatchGpuMilliseconds_ = 0.0f;
    LastDispatchCpuMilliseconds_ = 0.0f;
    if (!Ready() || Program.Pipeline == VK_NULL_HANDLE || Arguments.size() != Program.BindingTypes.size())
    {
        return false;
    }
    if (GroupCountX == 0u || GroupCountY == 0u || GroupCountZ == 0u)
    {
        return true;
    }

    const auto      CpuStart       = std::chrono::steady_clock::now();
    const bool      ExternalActive = (ActiveExternalStream_ != VK_NULL_HANDLE);
    VkDescriptorSet DescSet        = VK_NULL_HANDLE;
    if (!AllocateUpdatedDescriptorSet(Program.SetLayout, Program.BindingTypes, Arguments, DescSet))
    {
        return false;
    }

    VkCommandBuffer CommandStream = ActiveExternalStream_;
    if (!ExternalActive)
    {
        VkCommandBufferAllocateInfo StreamAlloc{};
        StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        StreamAlloc.commandPool        = CommandPool_;
        StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        StreamAlloc.commandBufferCount = 1u;
        if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
        {
            return false;
        }
        VkCommandBufferBeginInfo BeginSpec{};
        BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        pfnBeginCommandStream_(CommandStream, &BeginSpec);
    }

    uint32_t QueryStartIndex  = UINT32_MAX;
    const bool UseTimestamps = TimestampsSupported_ && TimestampQueryPool_ != VK_NULL_HANDLE;
    if (UseTimestamps)
    {
        if (ExternalActive)
        {
            const uint32_t LocalIndex = ExternalSlotDispatchCounts_[ActiveExternalSlot_];
            if (LocalIndex < MaxTimedDispatchesSlot)
            {
                QueryStartIndex = ActiveExternalSlot_ * MaxTimedDispatchesSlot * 2u + LocalIndex * 2u;
                ++ExternalSlotDispatchCounts_[ActiveExternalSlot_];
                ExternalSlotRecorded_[ActiveExternalSlot_] = true;
            }
        }
        else
        {
            QueryStartIndex = StandaloneQueryOffset;
            pfnCmdResetQueryPool_(CommandStream, TimestampQueryPool_, StandaloneQueryOffset, 2u);
        }
    }

    if (QueryStartIndex != UINT32_MAX)
    {
        pfnCmdWriteTimestamp_(CommandStream, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, TimestampQueryPool_, QueryStartIndex);
    }

    pfnCmdBindPipeline_(CommandStream, VK_PIPELINE_BIND_POINT_COMPUTE, Program.Pipeline);
    pfnCmdBindDescriptorSets_(CommandStream, VK_PIPELINE_BIND_POINT_COMPUTE, Program.Layout, 0u, 1u, &DescSet, 0u, nullptr);
    if (PushConstants && PushConstantBytes > 0u)
    {
        pfnCmdPushConstants_(CommandStream, Program.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, PushConstantBytes, PushConstants);
    }
    pfnCmdDispatch_(CommandStream, GroupCountX, GroupCountY, GroupCountZ);

    if (QueryStartIndex != UINT32_MAX)
    {
        pfnCmdWriteTimestamp_(CommandStream, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, TimestampQueryPool_, QueryStartIndex + 1u);
    }

    VkMemoryBarrier Barrier{};
    Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0u,
                           1u,
                           &Barrier,
                           0u,
                           nullptr,
                           0u,
                           nullptr);

    if (ExternalActive)
    {
        LastDispatchCpuMilliseconds_ =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuStart).count();
        return true;
    }

    pfnEndCommandStream_(CommandStream);
    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);

    if (Ok && QueryStartIndex != UINT32_MAX)
    {
        uint64_t Ticks[2] = {0u, 0u};
        if (pfnGetQueryPoolResults_(Device_,
                                    TimestampQueryPool_,
                                    StandaloneQueryOffset,
                                    2u,
                                    sizeof(Ticks),
                                    Ticks,
                                    sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS &&
            Ticks[1] >= Ticks[0])
        {
            LastDispatchGpuMilliseconds_ = static_cast<float>(static_cast<double>(Ticks[1] - Ticks[0]) *
                                                              static_cast<double>(TimestampPeriodNanoseconds_) * 1.0e-6);
        }
    }
    LastDispatchCpuMilliseconds_ =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - CpuStart).count();
    return Ok;
}

} // namespace Frontier
