/*==========================================================================================================================================
                                             VULKAN COMPUTE SUBSTRATE DEFINITION
==========================================================================================================================================*/
// 🧩 Dynamic-loaded Vulkan 1.3 offscreen compute substrate for multi-instance Environment & Celestial GPU submissions.

#include "Engine/Rendering/VulkanComputeSubstrate.h"

#include <algorithm>
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

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            FP16 <-> FP32 CONVERSION
//------------------------------------------------------------------------------------------------------------------------

uint16_t PackHalfFloat(float Value) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Value, sizeof(uint32_t));
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
            uint32_t M = Mant;
            int32_t  E = -14;
            while ((M & 0x0400u) == 0u)
            {
                M <<= 1u;
                --E;
            }
            M &= 0x03FFu;
            OutBits = Sign | (static_cast<uint32_t>(E + 127) << 23u) | (M << 13u);
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

VulkanComputeSubstrate::~VulkanComputeSubstrate()
{
    Shutdown();
}

bool VulkanComputeSubstrate::Initialize(const std::string& ExplicitDriverLibraryPath, std::string* OutDiagnostic)
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
    Candidates.push_back("libvulkan.so.1");
#endif

    std::string ChosenPath;
    for (const std::string& Cand : Candidates)
    {
        // Configure SwiftShader ICD automatically when a co-located vk_swiftshader_icd.json exists
#if !defined(_WIN32)
        std::error_code Ec;
        const std::filesystem::path LibPath = std::filesystem::absolute(Cand, Ec);
        if (!Ec && std::filesystem::exists(LibPath, Ec))
        {
            const std::filesystem::path IcdJson = LibPath.parent_path() / "vk_swiftshader_icd.json";
            if (std::filesystem::exists(IcdJson, Ec))
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
        LibraryHandle_ = OpenDynamicLibrary(Cand.c_str());
        if (LibraryHandle_)
        {
            ChosenPath = Cand;
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

    VkApplicationInfo AppInfo{};
    AppInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName   = "FrontierEnvironmentCompute";
    AppInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    AppInfo.pEngineName        = "Frontier";
    AppInfo.engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0);
    AppInfo.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo InstInfo{};
    InstInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstInfo.pApplicationInfo = &AppInfo;

    if (pfnCreateInstance_(&InstInfo, nullptr, &Instance_) != VK_SUCCESS || Instance_ == VK_NULL_HANDLE)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateInstance failed";
        }
        Shutdown();
        return false;
    }

#define RESOLVE_VK_INST(Field, Symbol)                                                        \
    Field = reinterpret_cast<decltype(Field)>(pfnGetInstanceProcAddr_(Instance_, Symbol));    \
    if (!Field)                                                                               \
    {                                                                                         \
        if (OutDiagnostic) *OutDiagnostic = std::string("Missing Vulkan symbol: ") + Symbol;  \
        Shutdown();                                                                           \
        return false;                                                                         \
    }

    RESOLVE_VK_INST(pfnDestroyInstance_, "vkDestroyInstance");
    RESOLVE_VK_INST(pfnEnumeratePhysicalDevices_, "vkEnumeratePhysicalDevices");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceProperties_, "vkGetPhysicalDeviceProperties");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceMemoryProperties_, "vkGetPhysicalDeviceMemoryProperties");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceQueueFamilyProperties_, "vkGetPhysicalDeviceQueueFamilyProperties");
    RESOLVE_VK_INST(pfnCreateDevice_, "vkCreateDevice");

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
    DeviceName_ = Props.deviceName;
    pfnGetPhysicalDeviceMemoryProperties_(PhysicalDevice_, &MemoryProperties_);

    uint32_t FamilyCount = 0u;
    pfnGetPhysicalDeviceQueueFamilyProperties_(PhysicalDevice_, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    pfnGetPhysicalDeviceQueueFamilyProperties_(PhysicalDevice_, &FamilyCount, Families.data());

    bool FoundCompute = false;
    for (uint32_t I = 0u; I < FamilyCount; ++I)
    {
        if ((Families[I].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
        {
            QueueFamily_ = I;
            FoundCompute = true;
            break;
        }
    }
    if (!FoundCompute)
    {
        Shutdown();
        return false;
    }

    const float             Priority = 1.0f;
    VkDeviceQueueCreateInfo QueueInfo{};
    QueueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueInfo.queueFamilyIndex = QueueFamily_;
    QueueInfo.queueCount       = 1u;
    QueueInfo.pQueuePriorities = &Priority;

    VkDeviceCreateInfo DevInfo{};
    DevInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DevInfo.queueCreateInfoCount = 1u;
    DevInfo.pQueueCreateInfos    = &QueueInfo;

    if (pfnCreateDevice_(PhysicalDevice_, &DevInfo, nullptr, &Device_) != VK_SUCCESS || Device_ == VK_NULL_HANDLE)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateDevice failed";
        }
        Shutdown();
        return false;
    }

    auto GetDev = reinterpret_cast<PFN_vkGetDeviceProcAddr>(pfnGetInstanceProcAddr_(Instance_, "vkGetDeviceProcAddr"));
    if (!GetDev)
    {
        Shutdown();
        return false;
    }

#define RESOLVE_VK_DEV(Field, Symbol)                                                        \
    Field = reinterpret_cast<decltype(Field)>(GetDev(Device_, Symbol));                      \
    if (!Field)                                                                              \
    {                                                                                        \
        if (OutDiagnostic) *OutDiagnostic = std::string("Missing device symbol: ") + Symbol; \
        Shutdown();                                                                          \
        return false;                                                                        \
    }

    RESOLVE_VK_DEV(pfnDestroyDevice_, "vkDestroyDevice");
    RESOLVE_VK_DEV(pfnGetDeviceQueue_, "vkGetDeviceQueue");
    RESOLVE_VK_DEV(pfnCreateCommandPool_, "vkCreateCommandPool");
    RESOLVE_VK_DEV(pfnDestroyCommandPool_, "vkDestroyCommandPool");
    RESOLVE_VK_DEV(pfnAllocateCommandBuffers_, "vkAllocateCommandBuffers");
    RESOLVE_VK_DEV(pfnFreeCommandBuffers_, "vkFreeCommandBuffers");
    RESOLVE_VK_DEV(pfnBeginCommandBuffer_, "vkBeginCommandBuffer");
    RESOLVE_VK_DEV(pfnEndCommandBuffer_, "vkEndCommandBuffer");
    RESOLVE_VK_DEV(pfnResetCommandBuffer_, "vkResetCommandBuffer");
    RESOLVE_VK_DEV(pfnQueueSubmit_, "vkQueueSubmit");
    RESOLVE_VK_DEV(pfnQueueWaitIdle_, "vkQueueWaitIdle");
    RESOLVE_VK_DEV(pfnCreateFence_, "vkCreateFence");
    RESOLVE_VK_DEV(pfnDestroyFence_, "vkDestroyFence");
    RESOLVE_VK_DEV(pfnWaitForFences_, "vkWaitForFences");
    RESOLVE_VK_DEV(pfnResetFences_, "vkResetFences");
    RESOLVE_VK_DEV(pfnCreateBuffer_, "vkCreateBuffer");
    RESOLVE_VK_DEV(pfnDestroyBuffer_, "vkDestroyBuffer");
    RESOLVE_VK_DEV(pfnGetBufferMemoryRequirements_, "vkGetBufferMemoryRequirements");
    RESOLVE_VK_DEV(pfnAllocateMemory_, "vkAllocateMemory");
    RESOLVE_VK_DEV(pfnFreeMemory_, "vkFreeMemory");
    RESOLVE_VK_DEV(pfnBindBufferMemory_, "vkBindBufferMemory");
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
    RESOLVE_VK_DEV(pfnCreateShaderModule_, "vkCreateShaderModule");
    RESOLVE_VK_DEV(pfnDestroyShaderModule_, "vkDestroyShaderModule");
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
    RESOLVE_VK_DEV(pfnCmdCopyBufferToImage_, "vkCmdCopyBufferToImage");
    RESOLVE_VK_DEV(pfnCmdCopyImageToBuffer_, "vkCmdCopyImageToBuffer");

#undef RESOLVE_VK_INST
#undef RESOLVE_VK_DEV

    pfnGetDeviceQueue_(Device_, QueueFamily_, 0u, &Queue_);

    VkCommandPoolCreateInfo PoolInfo{};
    PoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInfo.queueFamilyIndex = QueueFamily_;
    if (pfnCreateCommandPool_(Device_, &PoolInfo, nullptr, &CommandPool_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    const std::array<VkDescriptorPoolSize, 3> Sizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256u},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256u},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256u},
    }};
    VkDescriptorPoolCreateInfo DescPool{};
    DescPool.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DescPool.maxSets       = 64u;
    DescPool.poolSizeCount = static_cast<uint32_t>(Sizes.size());
    DescPool.pPoolSizes    = Sizes.data();
    if (pfnCreateDescriptorPool_(Device_, &DescPool, nullptr, &DescriptorPool_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    VkSamplerCreateInfo Samp{};
    Samp.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    Samp.magFilter    = VK_FILTER_LINEAR;
    Samp.minFilter    = VK_FILTER_LINEAR;
    Samp.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    Samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Samp.maxLod       = 0.0f;
    if (pfnCreateSampler_(Device_, &Samp, nullptr, &LinearClampSampler_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    Samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (pfnCreateSampler_(Device_, &Samp, nullptr, &LinearRepeatSampler_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    VkFenceCreateInfo FenceInfo{};
    FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (pfnCreateFence_(Device_, &FenceInfo, nullptr, &SubmitFence_) != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }

    return true;
}

void VulkanComputeSubstrate::Shutdown() noexcept
{
    if (Device_ != VK_NULL_HANDLE)
    {
        if (pfnQueueWaitIdle_ && Queue_ != VK_NULL_HANDLE)
        {
            pfnQueueWaitIdle_(Queue_);
        }
        if (SubmitFence_ != VK_NULL_HANDLE && pfnDestroyFence_)
        {
            pfnDestroyFence_(Device_, SubmitFence_, nullptr);
        }
        if (LinearRepeatSampler_ != VK_NULL_HANDLE && pfnDestroySampler_)
        {
            pfnDestroySampler_(Device_, LinearRepeatSampler_, nullptr);
        }
        if (LinearClampSampler_ != VK_NULL_HANDLE && pfnDestroySampler_)
        {
            pfnDestroySampler_(Device_, LinearClampSampler_, nullptr);
        }
        if (DescriptorPool_ != VK_NULL_HANDLE && pfnDestroyDescriptorPool_)
        {
            pfnDestroyDescriptorPool_(Device_, DescriptorPool_, nullptr);
        }
        if (CommandPool_ != VK_NULL_HANDLE && pfnDestroyCommandPool_)
        {
            pfnDestroyCommandPool_(Device_, CommandPool_, nullptr);
        }
        if (pfnDestroyDevice_)
        {
            pfnDestroyDevice_(Device_, nullptr);
        }
    }
    if (Instance_ != VK_NULL_HANDLE && pfnDestroyInstance_)
    {
        pfnDestroyInstance_(Instance_, nullptr);
    }
    CloseDynamicLibrary(LibraryHandle_);

    LibraryHandle_       = nullptr;
    Instance_            = VK_NULL_HANDLE;
    PhysicalDevice_      = VK_NULL_HANDLE;
    Device_              = VK_NULL_HANDLE;
    Queue_               = VK_NULL_HANDLE;
    CommandPool_         = VK_NULL_HANDLE;
    DescriptorPool_      = VK_NULL_HANDLE;
    LinearClampSampler_  = VK_NULL_HANDLE;
    LinearRepeatSampler_ = VK_NULL_HANDLE;
    SubmitFence_         = VK_NULL_HANDLE;
    DeviceName_.clear();
}

uint32_t VulkanComputeSubstrate::SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredFlags) const
{
    for (uint32_t I = 0u; I < MemoryProperties_.memoryTypeCount; ++I)
    {
        if ((TypeBits & (1u << I)) != 0u && (MemoryProperties_.memoryTypes[I].propertyFlags & RequiredFlags) == RequiredFlags)
        {
            return I;
        }
    }
    return UINT32_MAX;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           BUFFER & IMAGE ALLOCATION / IO
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeSubstrate::AllocateBuffer(VkDeviceSize ByteCount, VulkanBufferSlot& OutSlot)
{
    ReleaseBuffer(OutSlot);
    if (!Ready() || ByteCount == 0)
    {
        return false;
    }

    VkBufferCreateInfo Info{};
    Info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Info.size        = std::max<VkDeviceSize>(ByteCount, 16u);
    Info.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (pfnCreateBuffer_(Device_, &Info, nullptr, &OutSlot.Buffer) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Req{};
    pfnGetBufferMemoryRequirements_(Device_, OutSlot.Buffer, &Req);

    const uint32_t MemType = SelectMemoryType(
        Req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (MemType == UINT32_MAX)
    {
        ReleaseBuffer(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo Alloc{};
    Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Alloc.allocationSize  = Req.size;
    Alloc.memoryTypeIndex = MemType;

    if (pfnAllocateMemory_(Device_, &Alloc, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindBufferMemory_(Device_, OutSlot.Buffer, OutSlot.Memory, 0) != VK_SUCCESS ||
        pfnMapMemory_(Device_, OutSlot.Memory, 0, Info.size, 0, &OutSlot.MappedBytes) != VK_SUCCESS)
    {
        ReleaseBuffer(OutSlot);
        return false;
    }
    OutSlot.Capacity = Info.size;
    std::memset(OutSlot.MappedBytes, 0, static_cast<size_t>(Info.size));
    return true;
}

void VulkanComputeSubstrate::ReleaseBuffer(VulkanBufferSlot& Slot) noexcept
{
    if (!Ready())
    {
        Slot = VulkanBufferSlot{};
        return;
    }
    if (Slot.MappedBytes && Slot.Memory != VK_NULL_HANDLE)
    {
        pfnUnmapMemory_(Device_, Slot.Memory);
    }
    if (Slot.Buffer != VK_NULL_HANDLE)
    {
        pfnDestroyBuffer_(Device_, Slot.Buffer, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = VulkanBufferSlot{};
}

bool VulkanComputeSubstrate::UploadBuffer(VulkanBufferSlot& Slot, const void* SourceBytes, VkDeviceSize ByteCount)
{
    if (ByteCount == 0)
    {
        return true;
    }
    if (Slot.Buffer == VK_NULL_HANDLE || Slot.Capacity < ByteCount)
    {
        if (!AllocateBuffer(ByteCount, Slot))
        {
            return false;
        }
    }
    std::memcpy(Slot.MappedBytes, SourceBytes, static_cast<size_t>(ByteCount));
    return true;
}

bool VulkanComputeSubstrate::DownloadBuffer(const VulkanBufferSlot& Slot, void* DestinationBytes, VkDeviceSize ByteCount) const
{
    if (!Slot.MappedBytes || Slot.Capacity < ByteCount)
    {
        return false;
    }
    std::memcpy(DestinationBytes, Slot.MappedBytes, static_cast<size_t>(ByteCount));
    return true;
}

bool VulkanComputeSubstrate::AllocateRgba16fArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, VulkanImageSlot& OutSlot)
{
    ReleaseImage(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || ArrayLayers == 0u)
    {
        return false;
    }

    VkImageCreateInfo Img{};
    Img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    Img.imageType     = VK_IMAGE_TYPE_2D;
    Img.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
    Img.extent        = {Width, Height, 1u};
    Img.mipLevels     = 1u;
    Img.arrayLayers   = ArrayLayers;
    Img.samples       = VK_SAMPLE_COUNT_1_BIT;
    Img.tiling        = VK_IMAGE_TILING_OPTIMAL;
    Img.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    Img.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    Img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &Img, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Req{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Req);
    const uint32_t MemType = SelectMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemType == UINT32_MAX)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo Alloc{};
    Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Alloc.allocationSize  = Req.size;
    Alloc.memoryTypeIndex = MemType;

    if (pfnAllocateMemory_(Device_, &Alloc, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkImageViewCreateInfo View{};
    View.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    View.image                           = OutSlot.Image;
    View.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    View.format                          = Img.format;
    View.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    View.subresourceRange.baseMipLevel   = 0u;
    View.subresourceRange.levelCount     = 1u;
    View.subresourceRange.baseArrayLayer = 0u;
    View.subresourceRange.layerCount     = ArrayLayers;

    if (pfnCreateImageView_(Device_, &View, nullptr, &OutSlot.ArrayView) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    OutSlot.Format        = Img.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.ArrayLayers   = ArrayLayers;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return ClearRgba16fArray(OutSlot);
}

bool VulkanComputeSubstrate::AllocateSrgb8Array(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, VulkanImageSlot& OutSlot)
{
    ReleaseImage(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || ArrayLayers == 0u)
    {
        return false;
    }

    VkImageCreateInfo Img{};
    Img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    Img.imageType     = VK_IMAGE_TYPE_2D;
    Img.format        = VK_FORMAT_R8G8B8A8_SRGB;
    Img.extent        = {Width, Height, 1u};
    Img.mipLevels     = 1u;
    Img.arrayLayers   = ArrayLayers;
    Img.samples       = VK_SAMPLE_COUNT_1_BIT;
    Img.tiling        = VK_IMAGE_TILING_OPTIMAL;
    Img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Img.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    Img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &Img, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Req{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Req);
    const uint32_t MemType = SelectMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemType == UINT32_MAX)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo Alloc{};
    Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Alloc.allocationSize  = Req.size;
    Alloc.memoryTypeIndex = MemType;

    if (pfnAllocateMemory_(Device_, &Alloc, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkImageViewCreateInfo View{};
    View.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    View.image                           = OutSlot.Image;
    View.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    View.format                          = Img.format;
    View.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    View.subresourceRange.baseMipLevel   = 0u;
    View.subresourceRange.levelCount     = 1u;
    View.subresourceRange.baseArrayLayer = 0u;
    View.subresourceRange.layerCount     = ArrayLayers;

    if (pfnCreateImageView_(Device_, &View, nullptr, &OutSlot.ArrayView) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    OutSlot.Format        = Img.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.ArrayLayers   = ArrayLayers;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void VulkanComputeSubstrate::ReleaseImage(VulkanImageSlot& Slot) noexcept
{
    if (!Ready())
    {
        Slot = VulkanImageSlot{};
        return;
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
    Slot = VulkanImageSlot{};
}

bool VulkanComputeSubstrate::TransitionImageLayout(VkCommandBuffer CommandBuffer, VulkanImageSlot& Slot, VkImageLayout TargetLayout)
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
    Barrier.subresourceRange.layerCount     = Slot.ArrayLayers;
    Barrier.srcAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    Barrier.dstAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    pfnCmdPipelineBarrier_(CommandBuffer,
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

bool VulkanComputeSubstrate::ClearRgba16fArray(VulkanImageSlot& Slot, std::array<float, 4> ClearColour)
{
    if (!Ready() || Slot.Image == VK_NULL_HANDLE)
    {
        return false;
    }
    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = CommandPool_;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    VkCommandBuffer Cmd         = VK_NULL_HANDLE;
    if (pfnAllocateCommandBuffers_(Device_, &CmdAlloc, &Cmd) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBufferBeginInfo Begin{};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandBuffer_(Cmd, &Begin);
    TransitionImageLayout(Cmd, Slot, VK_IMAGE_LAYOUT_GENERAL);

    VkClearColorValue Val{};
    Val.float32[0] = ClearColour[0];
    Val.float32[1] = ClearColour[1];
    Val.float32[2] = ClearColour[2];
    Val.float32[3] = ClearColour[3];

    VkImageSubresourceRange Range{};
    Range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Range.baseMipLevel   = 0u;
    Range.levelCount     = 1u;
    Range.baseArrayLayer = 0u;
    Range.layerCount     = Slot.ArrayLayers;

    pfnCmdClearColorImage_(Cmd, Slot.Image, VK_IMAGE_LAYOUT_GENERAL, &Val, 1u, &Range);
    pfnEndCommandBuffer_(Cmd);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo Submit{};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &Cmd;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &Submit, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandBuffers_(Device_, CommandPool_, 1u, &Cmd);
    return Ok;
}

bool VulkanComputeSubstrate::UploadSrgb8Array(VulkanImageSlot& Slot, std::span<const uint8_t> RgbaBytes)
{
    const size_t Expected = static_cast<size_t>(Slot.Width) * Slot.Height * 4u * Slot.ArrayLayers;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || RgbaBytes.size() != Expected)
    {
        return false;
    }
    VulkanBufferSlot TransferSlot;
    if (!UploadBuffer(TransferSlot, RgbaBytes.data(), Expected))
    {
        return false;
    }

    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = CommandPool_;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    VkCommandBuffer Cmd         = VK_NULL_HANDLE;
    pfnAllocateCommandBuffers_(Device_, &CmdAlloc, &Cmd);

    VkCommandBufferBeginInfo Begin{};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandBuffer_(Cmd, &Begin);
    TransitionImageLayout(Cmd, Slot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy Copy{};
    Copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Copy.imageSubresource.mipLevel       = 0u;
    Copy.imageSubresource.baseArrayLayer = 0u;
    Copy.imageSubresource.layerCount     = Slot.ArrayLayers;
    Copy.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopyBufferToImage_(Cmd, TransferSlot.Buffer, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &Copy);
    TransitionImageLayout(Cmd, Slot, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    pfnEndCommandBuffer_(Cmd);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo Submit{};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &Cmd;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &Submit, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandBuffers_(Device_, CommandPool_, 1u, &Cmd);
    ReleaseBuffer(TransferSlot);
    return Ok;
}

bool VulkanComputeSubstrate::UploadRgba16fLayer(VulkanImageSlot& Slot, uint32_t LayerIndex, std::span<const float> RgbaFloats)
{
    const size_t FloatCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || LayerIndex >= Slot.ArrayLayers || RgbaFloats.size() != FloatCount)
    {
        return false;
    }
    std::vector<uint16_t> HalfTexels(FloatCount);
    for (size_t I = 0u; I < FloatCount; ++I)
    {
        HalfTexels[I] = PackHalfFloat(RgbaFloats[I]);
    }
    VulkanBufferSlot TransferSlot;
    if (!UploadBuffer(TransferSlot, HalfTexels.data(), HalfTexels.size() * sizeof(uint16_t)))
    {
        return false;
    }

    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = CommandPool_;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    VkCommandBuffer Cmd         = VK_NULL_HANDLE;
    pfnAllocateCommandBuffers_(Device_, &CmdAlloc, &Cmd);

    VkCommandBufferBeginInfo Begin{};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandBuffer_(Cmd, &Begin);
    TransitionImageLayout(Cmd, Slot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy Copy{};
    Copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Copy.imageSubresource.mipLevel       = 0u;
    Copy.imageSubresource.baseArrayLayer = LayerIndex;
    Copy.imageSubresource.layerCount     = 1u;
    Copy.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopyBufferToImage_(Cmd, TransferSlot.Buffer, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &Copy);
    TransitionImageLayout(Cmd, Slot, VK_IMAGE_LAYOUT_GENERAL);
    pfnEndCommandBuffer_(Cmd);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo Submit{};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &Cmd;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &Submit, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandBuffers_(Device_, CommandPool_, 1u, &Cmd);
    ReleaseBuffer(TransferSlot);
    return Ok;
}

bool VulkanComputeSubstrate::DownloadRgba16fLayer(VulkanImageSlot& Slot, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats)
{
    const size_t FloatCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || LayerIndex >= Slot.ArrayLayers)
    {
        return false;
    }
    VulkanBufferSlot TransferSlot;
    if (!AllocateBuffer(FloatCount * sizeof(uint16_t), TransferSlot))
    {
        return false;
    }

    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = CommandPool_;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    VkCommandBuffer Cmd         = VK_NULL_HANDLE;
    pfnAllocateCommandBuffers_(Device_, &CmdAlloc, &Cmd);

    VkCommandBufferBeginInfo Begin{};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandBuffer_(Cmd, &Begin);
    TransitionImageLayout(Cmd, Slot, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy Copy{};
    Copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Copy.imageSubresource.mipLevel       = 0u;
    Copy.imageSubresource.baseArrayLayer = LayerIndex;
    Copy.imageSubresource.layerCount     = 1u;
    Copy.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopyImageToBuffer_(Cmd, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, TransferSlot.Buffer, 1u, &Copy);
    TransitionImageLayout(Cmd, Slot, VK_IMAGE_LAYOUT_GENERAL);
    pfnEndCommandBuffer_(Cmd);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo Submit{};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &Cmd;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &Submit, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandBuffers_(Device_, CommandPool_, 1u, &Cmd);
    if (!Ok)
    {
        ReleaseBuffer(TransferSlot);
        return false;
    }

    const auto* HalfPtr = static_cast<const uint16_t*>(TransferSlot.MappedBytes);
    OutRgbaFloats.resize(FloatCount);
    for (size_t I = 0u; I < FloatCount; ++I)
    {
        OutRgbaFloats[I] = UnpackHalfFloat(HalfPtr[I]);
    }
    ReleaseBuffer(TransferSlot);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPUTE PROGRAM & DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeSubstrate::LoadComputeProgram(const std::string&                SpvFilePath,
                                                const char*                       EntryPointName,
                                                std::span<const VkDescriptorType> BindingTypes,
                                                uint32_t                          PushConstantBytes,
                                                VulkanComputeProgram&             OutProgram,
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
    const std::streamsize Size = File.tellg();
    if (Size <= 0 || (Size % 4) != 0)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Invalid SPIR-V byte size: " + SpvFilePath;
        }
        return false;
    }
    File.seekg(0, std::ios::beg);
    std::vector<uint32_t> Words(static_cast<size_t>(Size) / 4u);
    File.read(reinterpret_cast<char*>(Words.data()), Size);

    VkShaderModuleCreateInfo ModInfo{};
    ModInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ModInfo.codeSize = static_cast<size_t>(Size);
    ModInfo.pCode    = Words.data();
    if (pfnCreateShaderModule_(Device_, &ModInfo, nullptr, &OutProgram.ShaderModule) != VK_SUCCESS)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateShaderModule failed for " + SpvFilePath;
        }
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingTypes.size());
    for (size_t I = 0u; I < BindingTypes.size(); ++I)
    {
        Bindings[I].binding         = static_cast<uint32_t>(I);
        Bindings[I].descriptorType  = BindingTypes[I];
        Bindings[I].descriptorCount = 1u;
        Bindings[I].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInfo{};
    LayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutInfo.bindingCount = static_cast<uint32_t>(Bindings.size());
    LayoutInfo.pBindings    = Bindings.data();
    if (pfnCreateDescriptorSetLayout_(Device_, &LayoutInfo, nullptr, &OutProgram.SetLayout) != VK_SUCCESS)
    {
        ReleaseProgram(OutProgram);
        return false;
    }

    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = PushConstantBytes;

    VkPipelineLayoutCreateInfo PipeLayout{};
    PipeLayout.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayout.setLayoutCount         = 1u;
    PipeLayout.pSetLayouts            = &OutProgram.SetLayout;
    PipeLayout.pushConstantRangeCount = PushConstantBytes > 0u ? 1u : 0u;
    PipeLayout.pPushConstantRanges    = PushConstantBytes > 0u ? &PushRange : nullptr;

    if (pfnCreatePipelineLayout_(Device_, &PipeLayout, nullptr, &OutProgram.Layout) != VK_SUCCESS)
    {
        ReleaseProgram(OutProgram);
        return false;
    }

    std::string ResolvedEntryName = (EntryPointName && EntryPointName[0] != '\0') ? EntryPointName : "main";
    {
        std::vector<std::string> ModuleComputeEntries;
        size_t                   WordIndex = 5u;
        while (WordIndex < Words.size())
        {
            const uint32_t WordCount = Words[WordIndex] >> 16u;
            const uint32_t Opcode    = Words[WordIndex] & 0xFFFFu;
            if (WordCount == 0u || WordIndex + WordCount > Words.size())
            {
                break;
            }
            if (Opcode == 15u && WordCount >= 4u && Words[WordIndex + 1u] == 5u) // OpEntryPoint GLCompute(5)
            {
                const char* RawChars = reinterpret_cast<const char*>(&Words[WordIndex + 3u]);
                const size_t MaxBytes = (WordCount - 3u) * sizeof(uint32_t);
                size_t       Len      = 0u;
                while (Len < MaxBytes && RawChars[Len] != '\0')
                {
                    ++Len;
                }
                ModuleComputeEntries.emplace_back(RawChars, Len);
            }
            WordIndex += WordCount;
        }
        if (!ModuleComputeEntries.empty() &&
            std::find(ModuleComputeEntries.begin(), ModuleComputeEntries.end(), ResolvedEntryName) == ModuleComputeEntries.end())
        {
            ResolvedEntryName = ModuleComputeEntries.front();
        }
    }

    VkComputePipelineCreateInfo PipeInfo{};
    PipeInfo.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    PipeInfo.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipeInfo.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
    PipeInfo.stage.module       = OutProgram.ShaderModule;
    PipeInfo.stage.pName        = ResolvedEntryName.c_str();
    PipeInfo.layout             = OutProgram.Layout;

    if (pfnCreateComputePipelines_(Device_, VK_NULL_HANDLE, 1u, &PipeInfo, nullptr, &OutProgram.Pipeline) != VK_SUCCESS)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = std::string("vkCreateComputePipelines failed for entry ") + EntryPointName;
        }
        ReleaseProgram(OutProgram);
        return false;
    }

    OutProgram.BindingTypes.assign(BindingTypes.begin(), BindingTypes.end());
    return true;
}

void VulkanComputeSubstrate::ReleaseProgram(VulkanComputeProgram& Program) noexcept
{
    if (!Ready())
    {
        Program = VulkanComputeProgram{};
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
    if (Program.ShaderModule != VK_NULL_HANDLE)
    {
        pfnDestroyShaderModule_(Device_, Program.ShaderModule, nullptr);
    }
    Program = VulkanComputeProgram{};
}

bool VulkanComputeSubstrate::DispatchCompute(const VulkanComputeProgram&               Program,
                                             std::span<const VulkanDescriptorArgument> Arguments,
                                             const void*                               PushConstants,
                                             uint32_t                                  PushConstantBytes,
                                             uint32_t                                  GroupCountX,
                                             uint32_t                                  GroupCountY,
                                             uint32_t                                  GroupCountZ)
{
    if (!Ready() || Program.Pipeline == VK_NULL_HANDLE || Arguments.size() != Program.BindingTypes.size())
    {
        return false;
    }
    if (GroupCountX == 0u || GroupCountY == 0u || GroupCountZ == 0u)
    {
        return true;
    }

    pfnResetDescriptorPool_(Device_, DescriptorPool_, 0u);
    VkDescriptorSetAllocateInfo SetAlloc{};
    SetAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAlloc.descriptorPool     = DescriptorPool_;
    SetAlloc.descriptorSetCount = 1u;
    SetAlloc.pSetLayouts        = &Program.SetLayout;
    VkDescriptorSet DescSet     = VK_NULL_HANDLE;
    if (pfnAllocateDescriptorSets_(Device_, &SetAlloc, &DescSet) != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkDescriptorBufferInfo> BufferInfos(Arguments.size());
    std::vector<VkDescriptorImageInfo>  ImageInfos(Arguments.size());
    std::vector<VkWriteDescriptorSet>   Writes(Arguments.size());

    for (size_t I = 0u; I < Arguments.size(); ++I)
    {
        Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[I].dstSet          = DescSet;
        Writes[I].dstBinding      = static_cast<uint32_t>(I);
        Writes[I].descriptorCount = 1u;
        Writes[I].descriptorType  = Program.BindingTypes[I];

        if (Program.BindingTypes[I] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            if (!Arguments[I].Buffer || Arguments[I].Buffer->Buffer == VK_NULL_HANDLE)
            {
                return false;
            }
            BufferInfos[I].buffer = Arguments[I].Buffer->Buffer;
            BufferInfos[I].offset = 0u;
            BufferInfos[I].range  = VK_WHOLE_SIZE;
            Writes[I].pBufferInfo = &BufferInfos[I];
        }
        else
        {
            if (!Arguments[I].Image || Arguments[I].Image->ArrayView == VK_NULL_HANDLE)
            {
                return false;
            }
            ImageInfos[I].imageView = Arguments[I].Image->ArrayView;
            ImageInfos[I].sampler   = (Program.BindingTypes[I] == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                                          ? (Arguments[I].Image->Format == VK_FORMAT_R8G8B8A8_SRGB
                                                 ? LinearRepeatSampler_
                                                 : LinearClampSampler_)
                                          : VK_NULL_HANDLE;
            ImageInfos[I].imageLayout = (Arguments[I].Image->Format == VK_FORMAT_R8G8B8A8_SRGB)
                                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_GENERAL;
            Writes[I].pImageInfo = &ImageInfos[I];
        }
    }
    pfnUpdateDescriptorSets_(Device_, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = CommandPool_;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    VkCommandBuffer Cmd         = VK_NULL_HANDLE;
    if (pfnAllocateCommandBuffers_(Device_, &CmdAlloc, &Cmd) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBufferBeginInfo Begin{};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandBuffer_(Cmd, &Begin);

    pfnCmdBindPipeline_(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Program.Pipeline);
    pfnCmdBindDescriptorSets_(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Program.Layout, 0u, 1u, &DescSet, 0u, nullptr);
    if (PushConstants && PushConstantBytes > 0u)
    {
        pfnCmdPushConstants_(Cmd, Program.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, PushConstantBytes, PushConstants);
    }
    pfnCmdDispatch_(Cmd, GroupCountX, GroupCountY, GroupCountZ);

    VkMemoryBarrier Barrier{};
    Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    pfnCmdPipelineBarrier_(Cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0u,
                           1u,
                           &Barrier,
                           0u,
                           nullptr,
                           0u,
                           nullptr);

    pfnEndCommandBuffer_(Cmd);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo Submit{};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &Cmd;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &Submit, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandBuffers_(Device_, CommandPool_, 1u, &Cmd);
    return Ok;
}

} // namespace Frontier
