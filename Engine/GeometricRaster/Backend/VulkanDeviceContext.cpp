/*============================================================================================================================================
                                                         VULKANDEVICECONTEXT.CPP
============================================================================================================================================*/
// 🧩 Implementation of VulkanDeviceContext with volk dynamic loading, SwiftShader ICD auto-discovery, and Timeline Semaphore synchronization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#define VOLK_IMPLEMENTATION
#include "Engine/GeometricRaster/Backend/VulkanDeviceContext.h"

#include <cstdlib>
#include <cstring>
#include <fstream>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VulkanDeviceContext::~VulkanDeviceContext()
{
    TeardownContext();
}

bool VulkanDeviceContext::InitializeContext(
    const std::filesystem::path& RepositoryRootPath,
    std::string*                 OptionalDiagnostic)
{
    if (LogicalDevice != VK_NULL_HANDLE)
    {
        return true;
    }

    const std::filesystem::path SwiftShaderDir = RepositoryRootPath / "ExternalPackages/VulkanCompute/SwiftShader";
    const std::filesystem::path SwiftIcdJson   = SwiftShaderDir / "vk_swiftshader_icd.json";
    const std::filesystem::path SwiftLoaderSo  = SwiftShaderDir / "libvulkan.so.1";

#if defined(__unix__) || defined(__APPLE__)
    if (std::filesystem::exists(SwiftIcdJson) && std::getenv("VK_ICD_FILENAMES") == nullptr)
    {
        const std::string AbsoluteIcd = std::filesystem::weakly_canonical(SwiftIcdJson).string();
        ::setenv("VK_ICD_FILENAMES", AbsoluteIcd.c_str(), 0);
        ::setenv("VK_DRIVER_FILES", AbsoluteIcd.c_str(), 0);
    }
#endif

    VkResult InitStatus = volkInitialize();
#if defined(__unix__) || defined(__APPLE__)
    if (InitStatus != VK_SUCCESS && std::filesystem::exists(SwiftLoaderSo))
    {
        const std::string LoaderPath = std::filesystem::weakly_canonical(SwiftLoaderSo).string();
        DynamicLoaderLibrary = ::dlopen(LoaderPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (DynamicLoaderLibrary != nullptr)
        {
            auto ResolveInstanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                ::dlsym(DynamicLoaderLibrary, "vkGetInstanceProcAddr"));
            if (ResolveInstanceProc != nullptr)
            {
                volkInitializeCustom(ResolveInstanceProc);
                InitStatus = VK_SUCCESS;
            }
        }
    }
#endif

    if (InitStatus != VK_SUCCESS)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Failed to initialize Vulkan loader via volk or bundled SwiftShader.";
        }
        return false;
    }

    VkApplicationInfo AppInfo {};
    AppInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName   = "FrontierGeometricRaster";
    AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.pEngineName        = "Frontier";
    AppInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.apiVersion         = VK_API_VERSION_1_2;

    uint32_t InstExtCount = 0u;
    vkEnumerateInstanceExtensionProperties(nullptr, &InstExtCount, nullptr);
    std::vector<VkExtensionProperties> InstExts(InstExtCount);
    if (InstExtCount > 0u)
    {
        vkEnumerateInstanceExtensionProperties(nullptr, &InstExtCount, InstExts.data());
    }

    const char* DesiredInstanceExts[] = {
        "VK_KHR_surface",
        "VK_KHR_xcb_surface",
        "VK_KHR_xlib_surface",
        "VK_KHR_wayland_surface",
        "VK_KHR_win32_surface",
    };
    std::vector<const char*> EnabledInstanceExtensions {};
    for (const char* Desired : DesiredInstanceExts)
    {
        for (const auto& Available : InstExts)
        {
            if (std::strcmp(Available.extensionName, Desired) == 0)
            {
                EnabledInstanceExtensions.push_back(Desired);
                break;
            }
        }
    }

    VkInstanceCreateInfo InstanceInfo {};
    InstanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceInfo.pApplicationInfo        = &AppInfo;
    InstanceInfo.enabledExtensionCount   = static_cast<uint32_t>(EnabledInstanceExtensions.size());
    InstanceInfo.ppEnabledExtensionNames = EnabledInstanceExtensions.data();

    if (vkCreateInstance(&InstanceInfo, nullptr, &InstanceHandle) != VK_SUCCESS)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "vkCreateInstance failed.";
        }
        return false;
    }
    volkLoadInstance(InstanceHandle);

    uint32_t AdapterCount = 0u;
    vkEnumeratePhysicalDevices(InstanceHandle, &AdapterCount, nullptr);
    if (AdapterCount == 0u)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "No Vulkan physical devices enumerated.";
        }
        TeardownContext();
        return false;
    }

    std::vector<VkPhysicalDevice> Adapters(AdapterCount);
    vkEnumeratePhysicalDevices(InstanceHandle, &AdapterCount, Adapters.data());

    // Prefer discrete or integrated hardware GPU when present; otherwise select SwiftShader / CPU adapter
    PhysicalAdapter = Adapters.front();
    for (VkPhysicalDevice Candidate : Adapters)
    {
        VkPhysicalDeviceProperties Props {};
        vkGetPhysicalDeviceProperties(Candidate, &Props);
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            PhysicalAdapter     = Candidate;
            HardwareAccelerated = true;
            break;
        }
    }

    VkPhysicalDeviceProperties SelectedProps {};
    vkGetPhysicalDeviceProperties(PhysicalAdapter, &SelectedProps);
    AdapterName = SelectedProps.deviceName;

    uint32_t FamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalAdapter, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalAdapter, &FamilyCount, Families.data());

    bool FoundQueue = false;
    for (uint32_t FamIdx = 0u; FamIdx < FamilyCount; ++FamIdx)
    {
        const VkQueueFlags Flags = Families[FamIdx].queueFlags;
        if ((Flags & VK_QUEUE_GRAPHICS_BIT) != 0u && (Flags & VK_QUEUE_COMPUTE_BIT) != 0u)
        {
            QueueFamilyOrdinal = FamIdx;
            FoundQueue         = true;
            break;
        }
    }
    if (!FoundQueue)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "No unified Graphics+Compute Vulkan queue family found.";
        }
        TeardownContext();
        return false;
    }

    // Check extension list for VK_KHR_timeline_semaphore when adapter reports < 1.2 or KHR entry points
    uint32_t DevExtCount = 0u;
    vkEnumerateDeviceExtensionProperties(PhysicalAdapter, nullptr, &DevExtCount, nullptr);
    std::vector<VkExtensionProperties> DevExts(DevExtCount);
    vkEnumerateDeviceExtensionProperties(PhysicalAdapter, nullptr, &DevExtCount, DevExts.data());

    bool SupportsTimelineExt  = false;
    bool SupportsSwapchainExt = false;
    for (const VkExtensionProperties& Ext : DevExts)
    {
        if (std::strcmp(Ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0)
        {
            SupportsTimelineExt = true;
        }
        else if (std::strcmp(Ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        {
            SupportsSwapchainExt = true;
        }
    }

    std::vector<const char*> EnabledDeviceExtensions {};
    if (SupportsTimelineExt)
    {
        EnabledDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    }
    if (SupportsSwapchainExt)
    {
        EnabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures TimelineFeatures {};
    TimelineFeatures.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    TimelineFeatures.timelineSemaphore = VK_TRUE;

    const float QueuePriority = 1.0f;
    VkDeviceQueueCreateInfo QueueCreate {};
    QueueCreate.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueCreate.queueFamilyIndex = QueueFamilyOrdinal;
    QueueCreate.queueCount       = 1u;
    QueueCreate.pQueuePriorities = &QueuePriority;

    VkDeviceCreateInfo DeviceCreate {};
    DeviceCreate.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceCreate.pNext                   = &TimelineFeatures;
    DeviceCreate.queueCreateInfoCount    = 1u;
    DeviceCreate.pQueueCreateInfos       = &QueueCreate;
    DeviceCreate.enabledExtensionCount   = static_cast<uint32_t>(EnabledDeviceExtensions.size());
    DeviceCreate.ppEnabledExtensionNames = EnabledDeviceExtensions.data();

    if (vkCreateDevice(PhysicalAdapter, &DeviceCreate, nullptr, &LogicalDevice) != VK_SUCCESS)
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "vkCreateDevice with Timeline Semaphores failed.";
        }
        TeardownContext();
        return false;
    }
    volkLoadDevice(LogicalDevice);
    UseKhrTimelineEntry = (vkWaitSemaphores == nullptr && vkWaitSemaphoresKHR != nullptr);

    vkGetDeviceQueue(LogicalDevice, QueueFamilyOrdinal, 0u, &PrimaryQueue);

    VkCommandPoolCreateInfo PoolInfo {};
    PoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInfo.queueFamilyIndex = QueueFamilyOrdinal;
    if (vkCreateCommandPool(LogicalDevice, &PoolInfo, nullptr, &PrimaryCommandPool) != VK_SUCCESS)
    {
        TeardownContext();
        return false;
    }

    VkSemaphoreTypeCreateInfo TimelineTypeInfo {};
    TimelineTypeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    TimelineTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    TimelineTypeInfo.initialValue  = 0ull;

    VkSemaphoreCreateInfo SemInfo {};
    SemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    SemInfo.pNext = &TimelineTypeInfo;
    if (vkCreateSemaphore(LogicalDevice, &SemInfo, nullptr, &TimelineSyncHandle) != VK_SUCCESS)
    {
        TeardownContext();
        return false;
    }

    NextTimelineValue = 1ull;
    return true;
}

void VulkanDeviceContext::TeardownContext() noexcept
{
    if (LogicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(LogicalDevice);
        if (TimelineSyncHandle != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(LogicalDevice, TimelineSyncHandle, nullptr);
            TimelineSyncHandle = VK_NULL_HANDLE;
        }
        if (PrimaryCommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(LogicalDevice, PrimaryCommandPool, nullptr);
            PrimaryCommandPool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(LogicalDevice, nullptr);
        LogicalDevice = VK_NULL_HANDLE;
    }

    if (InstanceHandle != VK_NULL_HANDLE)
    {
        vkDestroyInstance(InstanceHandle, nullptr);
        InstanceHandle = VK_NULL_HANDLE;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (DynamicLoaderLibrary != nullptr)
    {
        ::dlclose(DynamicLoaderLibrary);
        DynamicLoaderLibrary = nullptr;
    }
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RESOURCE ALLOCATION & SHADERS
//------------------------------------------------------------------------------------------------------------------------

uint32_t VulkanDeviceContext::SelectMemoryTypeIndex(
    uint32_t              TypeFilterBits,
    VkMemoryPropertyFlags Properties) const noexcept
{
    VkPhysicalDeviceMemoryProperties MemProps {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalAdapter, &MemProps);
    for (uint32_t Idx = 0u; Idx < MemProps.memoryTypeCount; ++Idx)
    {
        if ((TypeFilterBits & (1u << Idx)) != 0u &&
            (MemProps.memoryTypes[Idx].propertyFlags & Properties) == Properties)
        {
            return Idx;
        }
    }
    return UINT32_MAX;
}

DeviceBufferResource VulkanDeviceContext::AllocateBuffer(
    uint64_t              ByteSize,
    VkBufferUsageFlags    UsageMask,
    VkMemoryPropertyFlags RequiredProperties,
    VkMemoryPropertyFlags PreferredProperties) const
{
    DeviceBufferResource Result {};
    if (LogicalDevice == VK_NULL_HANDLE || ByteSize == 0ull)
    {
        return Result;
    }

    VkBufferCreateInfo BufInfo {};
    BufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufInfo.size        = ByteSize;
    BufInfo.usage       = UsageMask;
    BufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(LogicalDevice, &BufInfo, nullptr, &Result.BufferHandle) != VK_SUCCESS)
    {
        return Result;
    }

    VkMemoryRequirements MemReq {};
    vkGetBufferMemoryRequirements(LogicalDevice, Result.BufferHandle, &MemReq);

    uint32_t MemIndex = UINT32_MAX;
    if (PreferredProperties != 0u)
    {
        MemIndex = SelectMemoryTypeIndex(MemReq.memoryTypeBits, RequiredProperties | PreferredProperties);
    }
    if (MemIndex == UINT32_MAX)
    {
        MemIndex = SelectMemoryTypeIndex(MemReq.memoryTypeBits, RequiredProperties);
    }
    if (MemIndex == UINT32_MAX)
    {
        vkDestroyBuffer(LogicalDevice, Result.BufferHandle, nullptr);
        Result.BufferHandle = VK_NULL_HANDLE;
        return Result;
    }

    VkMemoryAllocateInfo AllocInfo {};
    AllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocInfo.allocationSize  = MemReq.size;
    AllocInfo.memoryTypeIndex = MemIndex;

    if (vkAllocateMemory(LogicalDevice, &AllocInfo, nullptr, &Result.MemoryHandle) != VK_SUCCESS)
    {
        vkDestroyBuffer(LogicalDevice, Result.BufferHandle, nullptr);
        Result.BufferHandle = VK_NULL_HANDLE;
        return Result;
    }

    vkBindBufferMemory(LogicalDevice, Result.BufferHandle, Result.MemoryHandle, 0u);
    Result.ByteCapacity = ByteSize;

    if ((RequiredProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
    {
        vkMapMemory(LogicalDevice, Result.MemoryHandle, 0u, ByteSize, 0u, &Result.MappedPtr);
    }
    return Result;
}

void VulkanDeviceContext::ReleaseBuffer(DeviceBufferResource& Resource) const noexcept
{
    if (LogicalDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (Resource.MappedPtr != nullptr && Resource.MemoryHandle != VK_NULL_HANDLE)
    {
        vkUnmapMemory(LogicalDevice, Resource.MemoryHandle);
        Resource.MappedPtr = nullptr;
    }
    if (Resource.BufferHandle != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(LogicalDevice, Resource.BufferHandle, nullptr);
        Resource.BufferHandle = VK_NULL_HANDLE;
    }
    if (Resource.MemoryHandle != VK_NULL_HANDLE)
    {
        vkFreeMemory(LogicalDevice, Resource.MemoryHandle, nullptr);
        Resource.MemoryHandle = VK_NULL_HANDLE;
    }
    Resource.ByteCapacity = 0ull;
}

DeviceImageResource VulkanDeviceContext::AllocateImage2D(
    uint32_t           WidthPx,
    uint32_t           HeightPx,
    VkFormat           Format,
    VkImageUsageFlags  UsageMask,
    VkImageAspectFlags AspectMask) const
{
    DeviceImageResource Result {};
    if (LogicalDevice == VK_NULL_HANDLE || WidthPx == 0u || HeightPx == 0u)
    {
        return Result;
    }

    VkImageCreateInfo ImgInfo {};
    ImgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImgInfo.imageType     = VK_IMAGE_TYPE_2D;
    ImgInfo.format        = Format;
    ImgInfo.extent        = { WidthPx, HeightPx, 1u };
    ImgInfo.mipLevels     = 1u;
    ImgInfo.arrayLayers   = 1u;
    ImgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImgInfo.usage         = UsageMask;
    ImgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(LogicalDevice, &ImgInfo, nullptr, &Result.ImageHandle) != VK_SUCCESS)
    {
        return Result;
    }

    VkMemoryRequirements MemReq {};
    vkGetImageMemoryRequirements(LogicalDevice, Result.ImageHandle, &MemReq);

    const uint32_t MemIndex = SelectMemoryTypeIndex(MemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemIndex == UINT32_MAX)
    {
        vkDestroyImage(LogicalDevice, Result.ImageHandle, nullptr);
        Result.ImageHandle = VK_NULL_HANDLE;
        return Result;
    }

    VkMemoryAllocateInfo AllocInfo {};
    AllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocInfo.allocationSize  = MemReq.size;
    AllocInfo.memoryTypeIndex = MemIndex;
    if (vkAllocateMemory(LogicalDevice, &AllocInfo, nullptr, &Result.MemoryHandle) != VK_SUCCESS)
    {
        vkDestroyImage(LogicalDevice, Result.ImageHandle, nullptr);
        Result.ImageHandle = VK_NULL_HANDLE;
        return Result;
    }

    vkBindImageMemory(LogicalDevice, Result.ImageHandle, Result.MemoryHandle, 0u);

    VkImageViewCreateInfo ViewInfo {};
    ViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewInfo.image                           = Result.ImageHandle;
    ViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format                          = Format;
    ViewInfo.subresourceRange.aspectMask     = AspectMask;
    ViewInfo.subresourceRange.baseMipLevel   = 0u;
    ViewInfo.subresourceRange.levelCount     = 1u;
    ViewInfo.subresourceRange.baseArrayLayer = 0u;
    ViewInfo.subresourceRange.layerCount     = 1u;

    if (vkCreateImageView(LogicalDevice, &ViewInfo, nullptr, &Result.ViewHandle) != VK_SUCCESS)
    {
        ReleaseImage2D(Result);
        return Result;
    }

    Result.ImageFormat = Format;
    Result.WidthPx     = WidthPx;
    Result.HeightPx    = HeightPx;
    return Result;
}

void VulkanDeviceContext::ReleaseImage2D(DeviceImageResource& Resource) const noexcept
{
    if (LogicalDevice == VK_NULL_HANDLE)
    {
        return;
    }
    if (Resource.ViewHandle != VK_NULL_HANDLE)
    {
        vkDestroyImageView(LogicalDevice, Resource.ViewHandle, nullptr);
        Resource.ViewHandle = VK_NULL_HANDLE;
    }
    if (Resource.ImageHandle != VK_NULL_HANDLE)
    {
        vkDestroyImage(LogicalDevice, Resource.ImageHandle, nullptr);
        Resource.ImageHandle = VK_NULL_HANDLE;
    }
    if (Resource.MemoryHandle != VK_NULL_HANDLE)
    {
        vkFreeMemory(LogicalDevice, Resource.MemoryHandle, nullptr);
        Resource.MemoryHandle = VK_NULL_HANDLE;
    }
    Resource.WidthPx  = 0u;
    Resource.HeightPx = 0u;
}

VkShaderModule VulkanDeviceContext::LoadShaderModule(const std::filesystem::path& SpirvFilePath) const
{
    std::ifstream Stream(SpirvFilePath, std::ios::binary | std::ios::ate);
    if (!Stream.is_open())
    {
        return VK_NULL_HANDLE;
    }
    const std::streamsize ByteLen = Stream.tellg();
    if (ByteLen <= 0 || (ByteLen % 4) != 0)
    {
        return VK_NULL_HANDLE;
    }
    Stream.seekg(0, std::ios::beg);
    std::vector<uint32_t> Words(static_cast<std::size_t>(ByteLen) / 4u);
    if (!Stream.read(reinterpret_cast<char*>(Words.data()), ByteLen))
    {
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo ModInfo {};
    ModInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ModInfo.codeSize = static_cast<std::size_t>(ByteLen);
    ModInfo.pCode    = Words.data();

    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(LogicalDevice, &ModInfo, nullptr, &Module) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return Module;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         TIMELINE SEMAPHORE COMMAND DISPATCH
//------------------------------------------------------------------------------------------------------------------------

VkCommandBuffer VulkanDeviceContext::BeginSingleTimeCommands() const
{
    VkCommandBufferAllocateInfo AllocInfo {};
    AllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    AllocInfo.commandPool        = PrimaryCommandPool;
    AllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1u;

    VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(LogicalDevice, &AllocInfo, &CmdBuffer) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo BeginInfo {};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(CmdBuffer, &BeginInfo);
    return CmdBuffer;
}

uint64_t VulkanDeviceContext::DispatchCommandBufferAsync(VkCommandBuffer CmdBuffer)
{
    vkEndCommandBuffer(CmdBuffer);

    const uint64_t SignalValue = NextTimelineValue++;

    VkTimelineSemaphoreSubmitInfo TimelineInfo {};
    TimelineInfo.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    TimelineInfo.signalSemaphoreValueCount = 1u;
    TimelineInfo.pSignalSemaphoreValues    = &SignalValue;

    VkSubmitInfo QueueDispatch {};
    QueueDispatch.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    QueueDispatch.pNext                = &TimelineInfo;
    QueueDispatch.commandBufferCount   = 1u;
    QueueDispatch.pCommandBuffers      = &CmdBuffer;
    QueueDispatch.signalSemaphoreCount = 1u;
    QueueDispatch.pSignalSemaphores    = &TimelineSyncHandle;

    vkQueueSubmit(PrimaryQueue, 1u, &QueueDispatch, VK_NULL_HANDLE);
    return SignalValue;
}

bool VulkanDeviceContext::QueryTimelineReached(uint64_t TargetTimelineValue) const noexcept
{
    uint64_t CurrentCounter = 0ull;
    VkResult Status         = VK_ERROR_UNKNOWN;
    if (UseKhrTimelineEntry && vkGetSemaphoreCounterValueKHR != nullptr)
    {
        Status = vkGetSemaphoreCounterValueKHR(LogicalDevice, TimelineSyncHandle, &CurrentCounter);
    }
    else if (vkGetSemaphoreCounterValue != nullptr)
    {
        Status = vkGetSemaphoreCounterValue(LogicalDevice, TimelineSyncHandle, &CurrentCounter);
    }
    return Status == VK_SUCCESS && CurrentCounter >= TargetTimelineValue;
}

bool VulkanDeviceContext::AwaitTimelineValue(
    uint64_t TargetTimelineValue,
    uint64_t TimeoutNanoseconds) const noexcept
{
    VkSemaphoreWaitInfo WaitInfo {};
    WaitInfo.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    WaitInfo.semaphoreCount = 1u;
    WaitInfo.pSemaphores    = &TimelineSyncHandle;
    WaitInfo.pValues        = &TargetTimelineValue;

    VkResult Status = VK_ERROR_UNKNOWN;
    if (UseKhrTimelineEntry && vkWaitSemaphoresKHR != nullptr)
    {
        Status = vkWaitSemaphoresKHR(LogicalDevice, &WaitInfo, TimeoutNanoseconds);
    }
    else if (vkWaitSemaphores != nullptr)
    {
        Status = vkWaitSemaphores(LogicalDevice, &WaitInfo, TimeoutNanoseconds);
    }
    return Status == VK_SUCCESS;
}

} // namespace Frontier::GeometricRaster
