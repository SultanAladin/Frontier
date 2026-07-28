/*==============================================================================================================================================
                                                                VULKANHOST.CPP
==============================================================================================================================================*/
// 🧩 One-time Vulkan bring-up. Chooses a discrete GPU when present, a graphics-capable queue family, and creates the swapchain-capable
//    logical device plus an ImGui-sized descriptor pool. The validation layer is enabled only under FRONTIER_VULKAN_VALIDATION so release
//    builds stay lean. Mirrors the setup in the official ImGui example, restructured into the struct + free-function style.

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#include <cstring>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportVulkanResult(const char* Where, VkResult Outcome)
{
    if (Outcome != VK_SUCCESS)
        ISSUE_FAULT("vulkan", "%s failed (VkResult %d)", Where, (int)Outcome);
}

#ifdef FRONTIER_VULKAN_VALIDATION
// 📝 The validation layer hands us a formatted payload per message; we classify it by its severity bits and route it into the
//    diagnostic archive. Errors become faults, warnings become cautions, everything else a notice. Returns VK_FALSE per spec so
//    the offending call is not aborted.
VKAPI_ATTR VkBool32 VKAPI_CALL DecodeValidationPayload(VkDebugUtilsMessageSeverityFlagBitsEXT      Severity,
                                                       VkDebugUtilsMessageTypeFlagsEXT             MessageType,
                                                       const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
                                                       void*                                       UserContext)
{
    (void)MessageType;
    (void)UserContext;
    const char* PayloadText = (CallbackData && CallbackData->pMessage) ? CallbackData->pMessage : "(no message)";

    if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ISSUE_FAULT("vk-validation", "%s", PayloadText);
    else if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        ISSUE_CAUTION("vk-validation", "%s", PayloadText);
    else
        ISSUE_NOTICE("vk-validation", "%s", PayloadText);

    return VK_FALSE;
}

// Fill a messenger create-info describing which severities and types we want routed to DecodeValidationPayload.
void DescribeValidationMessenger(VkDebugUtilsMessengerCreateInfoEXT& MessengerInfo)
{
    MessengerInfo = {};
    MessengerInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    MessengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                  | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    MessengerInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                  | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                  | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    MessengerInfo.pfnUserCallback = DecodeValidationPayload;
}
#endif

// Prefer a discrete GPU; fall back to the first device reported. Returns VK_NULL_HANDLE only when no device exists.
VkPhysicalDevice ResolvePhysicalDevice(VkInstance Instance)
{
    uint32_t DeviceCount = 0;
    vkEnumeratePhysicalDevices(Instance, &DeviceCount, nullptr);
    if (DeviceCount == 0)
        return VK_NULL_HANDLE;

    std::vector<VkPhysicalDevice> Devices(DeviceCount);
    vkEnumeratePhysicalDevices(Instance, &DeviceCount, Devices.data());

    for (VkPhysicalDevice Candidate : Devices)
    {
        VkPhysicalDeviceProperties Properties;
        vkGetPhysicalDeviceProperties(Candidate, &Properties);
        if (Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            return Candidate;
    }
    return Devices[0];
}

// First queue family advertising graphics support. 0xFFFFFFFF when none (should not happen on a real GPU).
uint32_t ResolveGraphicsQueueFamily(VkPhysicalDevice PhysicalDevice)
{
    uint32_t FamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &FamilyCount, Families.data());

    for (uint32_t Index = 0; Index < FamilyCount; ++Index)
    {
        if ((Families[Index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            return Index;
    }
    return 0xFFFFFFFFu;
}

bool ValidationLayerAvailable(const char* LayerName)
{
    uint32_t LayerCount = 0;
    vkEnumerateInstanceLayerProperties(&LayerCount, nullptr);
    std::vector<VkLayerProperties> Layers(LayerCount);
    vkEnumerateInstanceLayerProperties(&LayerCount, Layers.data());
    for (const VkLayerProperties& Layer : Layers)
    {
        if (strcmp(Layer.layerName, LayerName) == 0)
            return true;
    }
    return false;
}

// True when the physical device advertises a named device extension. Used to gate VK_KHR_dynamic_rendering so the grid pass
// can render straight into the swapchain image without VkRenderPass / VkFramebuffer objects.
bool DeviceExtensionAvailable(VkPhysicalDevice PhysicalDevice, const char* ExtensionName)
{
    uint32_t ExtensionCount = 0;
    vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &ExtensionCount, nullptr);
    std::vector<VkExtensionProperties> Extensions(ExtensionCount);
    vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &ExtensionCount, Extensions.data());
    for (const VkExtensionProperties& Extension : Extensions)
    {
        if (strcmp(Extension.extensionName, ExtensionName) == 0)
            return true;
    }
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeVulkanHost(VulkanHost&  Host,
                          const char** RequiredInstanceExtensions,
                          uint32_t     ExtensionCount)
{
    Host.ApiVersion = VK_API_VERSION_1_2;

    // -- Instance ------------------------------------------------------------------------------------------------------
    std::vector<const char*> Extensions;
    for (uint32_t Index = 0; Index < ExtensionCount; ++Index)
        Extensions.push_back(RequiredInstanceExtensions[Index]);

    std::vector<const char*> Layers;
#ifdef FRONTIER_VULKAN_VALIDATION
    const char* ValidationLayer = "VK_LAYER_KHRONOS_validation";
    const bool ValidationOn = ValidationLayerAvailable(ValidationLayer);
    if (ValidationOn)
    {
        Layers.push_back(ValidationLayer);
        Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        ISSUE_NOTICE("vulkan", "validation layer enabled (development profile)");
    }
    else
    {
        ISSUE_CAUTION("vulkan", "validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }
#else
    (void)&ValidationLayerAvailable;
#endif

    VkApplicationInfo ApplicationInfo = {};
    ApplicationInfo.sType       = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ApplicationInfo.pApplicationName = "Frontier";
    ApplicationInfo.apiVersion  = Host.ApiVersion;

    VkInstanceCreateInfo InstanceInfo = {};
    InstanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceInfo.pApplicationInfo        = &ApplicationInfo;
    InstanceInfo.enabledExtensionCount   = (uint32_t)Extensions.size();
    InstanceInfo.ppEnabledExtensionNames = Extensions.empty() ? nullptr : Extensions.data();
    InstanceInfo.enabledLayerCount       = (uint32_t)Layers.size();
    InstanceInfo.ppEnabledLayerNames     = Layers.empty() ? nullptr : Layers.data();

    VkResult Outcome = vkCreateInstance(&InstanceInfo, Host.Allocator, &Host.Instance);
    if (Outcome != VK_SUCCESS)
    {
        ReportVulkanResult("vkCreateInstance", Outcome);
        return false;
    }

    // -- Validation signal route (development profile) -------------------------------------------------------------------
#ifdef FRONTIER_VULKAN_VALIDATION
    if (ValidationOn)
    {
        auto ConstructMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(Host.Instance, "vkCreateDebugUtilsMessengerEXT");
        if (ConstructMessenger != nullptr)
        {
            VkDebugUtilsMessengerCreateInfoEXT MessengerInfo;
            DescribeValidationMessenger(MessengerInfo);
            Outcome = ConstructMessenger(Host.Instance, &MessengerInfo, Host.Allocator, &Host.ValidationSignalBroadcaster);
            if (Outcome != VK_SUCCESS)
                ISSUE_CAUTION("vulkan", "debug messenger creation failed (VkResult %d) — validation output unrouted", (int)Outcome);
        }
        else
        {
            ISSUE_CAUTION("vulkan", "vkCreateDebugUtilsMessengerEXT unavailable — validation output unrouted");
        }
    }
#endif

    // -- Physical device + queue family ---------------------------------------------------------------------------------
    Host.PhysicalDevice = ResolvePhysicalDevice(Host.Instance);
    if (Host.PhysicalDevice == VK_NULL_HANDLE)
    {
        ISSUE_FAULT("vulkan", "no physical device found");
        return false;
    }
    Host.GraphicsQueueFamily = ResolveGraphicsQueueFamily(Host.PhysicalDevice);
    if (Host.GraphicsQueueFamily == 0xFFFFFFFFu)
    {
        ISSUE_FAULT("vulkan", "no graphics queue family found");
        return false;
    }

    // -- Logical device -------------------------------------------------------------------------------------------------
    const float QueuePriority = 1.0f;
    VkDeviceQueueCreateInfo QueueInfo = {};
    QueueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueInfo.queueFamilyIndex = Host.GraphicsQueueFamily;
    QueueInfo.queueCount       = 1;
    QueueInfo.pQueuePriorities = &QueuePriority;

    // 📝 Dynamic rendering lets the grid pass draw into the acquired swapchain image with vkCmdBeginRendering — no VkRenderPass
    //    or VkFramebuffer objects. It is core in 1.3 and a KHR extension on 1.2 (Pascal / GTX-1060 drivers expose it). We enable
    //    it only when advertised; a device without it still presents the clear-only path, and the grid pass is skipped.
    std::vector<const char*> DeviceExtensions;
    DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    Host.DynamicRenderingEnabled = DeviceExtensionAvailable(Host.PhysicalDevice, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    if (Host.DynamicRenderingEnabled)
        DeviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    VkPhysicalDeviceDynamicRenderingFeaturesKHR DynamicRenderingFeatures = {};
    DynamicRenderingFeatures.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    DynamicRenderingFeatures.dynamicRendering = VK_TRUE;

    // 📝 The visibility raster's fragment stage reads gl_PrimitiveID to write the packed surface identity; glslang lowers that
    //    read to SPIR-V OpCapability Geometry, which vkCreateShaderModule rejects unless the geometryShader feature is enabled at
    //    device creation. Every Pascal-and-newer part (GTX-1060 floor) advertises it, so we turn it on unconditionally here.
    VkPhysicalDeviceFeatures EnabledFeatures = {};
    EnabledFeatures.geometryShader = VK_TRUE;

    VkDeviceCreateInfo DeviceInfo = {};
    DeviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceInfo.pNext                   = Host.DynamicRenderingEnabled ? &DynamicRenderingFeatures : nullptr;
    DeviceInfo.queueCreateInfoCount    = 1;
    DeviceInfo.pQueueCreateInfos       = &QueueInfo;
    DeviceInfo.enabledExtensionCount   = (uint32_t)DeviceExtensions.size();
    DeviceInfo.ppEnabledExtensionNames = DeviceExtensions.data();
    DeviceInfo.pEnabledFeatures        = &EnabledFeatures;

    Outcome = vkCreateDevice(Host.PhysicalDevice, &DeviceInfo, Host.Allocator, &Host.Device);
    if (Outcome != VK_SUCCESS)
    {
        ReportVulkanResult("vkCreateDevice", Outcome);
        return false;
    }
    vkGetDeviceQueue(Host.Device, Host.GraphicsQueueFamily, 0, &Host.GraphicsQueue);

    // Load the dynamic-rendering command entry points (the 1.2 loader does not expose the 1.3 core symbols statically).
    if (Host.DynamicRenderingEnabled)
    {
        Host.CmdBeginRendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(Host.Device, "vkCmdBeginRenderingKHR");
        Host.CmdEndRendering   = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(Host.Device, "vkCmdEndRenderingKHR");
        if (Host.CmdBeginRendering == nullptr || Host.CmdEndRendering == nullptr)
        {
            Host.DynamicRenderingEnabled = false;
            ISSUE_CAUTION("vulkan", "vkCmdBeginRenderingKHR unavailable despite extension — grid pass disabled");
        }
    }
    ISSUE_NOTICE("vulkan", "dynamic rendering: %s", Host.DynamicRenderingEnabled ? "enabled" : "unavailable (grid pass will be skipped)");

    // -- ImGui descriptor pool ------------------------------------------------------------------------------------------
    // 📝 Sized generously enough for the ImGui font atlas + any AddTexture calls a workspace makes. This ImGui version's
    //    Vulkan backend (1.92.x texture-management path) allocates sets with SEPARATE sampler + sampled-image bindings, not
    //    only the classic combined-image-sampler, so the pool must advertise all three types — otherwise the driver logs a
    //    validation CAUTION that the pool has no matching pool size for the SAMPLER / SAMPLED_IMAGE bindings it hands out.
    //    FREE_DESCRIPTOR_SET_BIT is required by the backend.
    VkDescriptorPoolSize PoolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                 64 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,           64 },
    };
    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    PoolInfo.maxSets       = 64;
    PoolInfo.poolSizeCount = (uint32_t)(sizeof(PoolSizes) / sizeof(PoolSizes[0]));
    PoolInfo.pPoolSizes    = PoolSizes;

    Outcome = vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Host.ImguiDescriptorPool);
    if (Outcome != VK_SUCCESS)
    {
        ReportVulkanResult("vkCreateDescriptorPool", Outcome);
        return false;
    }

    return true;
}

void FinalizeVulkanHost(VulkanHost& Host)
{
    if (Host.Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Host.Device);

    if (Host.ImguiDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Host.Device, Host.ImguiDescriptorPool, Host.Allocator);
        Host.ImguiDescriptorPool = VK_NULL_HANDLE;
    }
    if (Host.Device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(Host.Device, Host.Allocator);
        Host.Device = VK_NULL_HANDLE;
    }
#ifdef FRONTIER_VULKAN_VALIDATION
    if (Host.ValidationSignalBroadcaster != VK_NULL_HANDLE && Host.Instance != VK_NULL_HANDLE)
    {
        auto FinalizeMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(Host.Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (FinalizeMessenger != nullptr)
            FinalizeMessenger(Host.Instance, Host.ValidationSignalBroadcaster, Host.Allocator);
        Host.ValidationSignalBroadcaster = VK_NULL_HANDLE;
    }
#endif
    if (Host.Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(Host.Instance, Host.Allocator);
        Host.Instance = VK_NULL_HANDLE;
    }
}

} // namespace Frontier
