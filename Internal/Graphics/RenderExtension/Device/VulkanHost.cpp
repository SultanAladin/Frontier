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

    // 📝 int64 shader atomics for the software micro-raster (Phase 4). The path packs a 64-bit (depth|id) word and resolves it
    //    with atomicMax — image atomics are the primary route, buffer atomics the fallback. Detection alone (InspectHardwareFeatures)
    //    is not enough: the feature must be ENABLED at device creation or SPIR-V using OpCapability Int64Atomics is rejected. We
    //    probe the same two feature structs the inspector uses, add VK_EXT_shader_image_atomic_int64 when advertised, and chain the
    //    structs into pNext with only the supported bits set. A device without either bit leaves both flags false and the software
    //    path gates itself off, falling back to the hardware raster (Phase 4 gate, PLAN §12).
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT ImageAtomicProbe = {};
    ImageAtomicProbe.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
    VkPhysicalDeviceShaderAtomicInt64Features BufferAtomicProbe = {};
    BufferAtomicProbe.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
    BufferAtomicProbe.pNext = &ImageAtomicProbe;
    VkPhysicalDeviceFeatures2 AtomicProbe = {};
    AtomicProbe.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    AtomicProbe.pNext = &BufferAtomicProbe;
    vkGetPhysicalDeviceFeatures2(Host.PhysicalDevice, &AtomicProbe);

    const bool ImageAtomicExtensionAvailable =
        DeviceExtensionAvailable(Host.PhysicalDevice, VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME);
    Host.ShaderImageInt64AtomicsEnabled  = ImageAtomicProbe.shaderImageInt64Atomics == VK_TRUE && ImageAtomicExtensionAvailable;
    Host.ShaderBufferInt64AtomicsEnabled = BufferAtomicProbe.shaderBufferInt64Atomics == VK_TRUE;
    if (Host.ShaderImageInt64AtomicsEnabled)
        DeviceExtensions.push_back(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME);

    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT ImageAtomicFeatures = {};
    ImageAtomicFeatures.sType                   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
    ImageAtomicFeatures.shaderImageInt64Atomics = Host.ShaderImageInt64AtomicsEnabled ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceShaderAtomicInt64Features BufferAtomicFeatures = {};
    BufferAtomicFeatures.sType                    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
    BufferAtomicFeatures.shaderBufferInt64Atomics = Host.ShaderBufferInt64AtomicsEnabled ? VK_TRUE : VK_FALSE;

    // 📝 The visibility raster's fragment stage reads gl_PrimitiveID to write the packed surface identity; glslang lowers that
    //    read to SPIR-V OpCapability Geometry, which vkCreateShaderModule rejects unless the geometryShader feature is enabled at
    //    device creation. Every Pascal-and-newer part (GTX-1060 floor) advertises it, so we turn it on unconditionally here.
    VkPhysicalDeviceFeatures EnabledFeatures = {};
    EnabledFeatures.geometryShader = VK_TRUE;

    // 📝 fragmentStoresAndAtomics is a CORE VkPhysicalDeviceFeatures bit, unrelated to the int64 atomic chain above: that chain widens
    //    the atomic OPERAND to 64 bits, whereas this bit grants the fragment stage permission to WRITE to storage buffers and storage
    //    images at all. The sun-shadow chain needs it twice — tile marking tags visible pages from a fragment shader, and the page
    //    atlas raster resolves caster depth with imageAtomicMin. One bit covers both stores and atomics; vertexPipelineStoresAndAtomics
    //    is deliberately NOT requested, because no vertex stage in the engine writes.
    // 🔴 Queried, never assumed. Every Pascal-and-newer part advertises it, so the false branch is not expected to be taken on any
    //    supported GPU — but requesting an unsupported feature makes vkCreateDevice fail outright with FEATURE_NOT_PRESENT, which would
    //    take the WHOLE renderer down rather than only the shadows. Recording the verdict on the host lets the shadow passes gate
    //    themselves off and everything else keep working.
    VkPhysicalDeviceFeatures SupportedFeatures = {};
    vkGetPhysicalDeviceFeatures(Host.PhysicalDevice, &SupportedFeatures);
    Host.FragmentStoresAndAtomicsEnabled = SupportedFeatures.fragmentStoresAndAtomics == VK_TRUE;
    if (Host.FragmentStoresAndAtomicsEnabled)
        EnabledFeatures.fragmentStoresAndAtomics = VK_TRUE;

    // 📝 The software micro-raster packs a 64-bit (depth|id) word and does atomicMax on it. The atomic capability structs enabled
    //    above cover the atomic OP, but the packed uint64_t arithmetic itself makes glslang emit the BASE OpCapability Int64 — a
    //    SEPARATE capability from Int64Atomics, gated on VkPhysicalDeviceFeatures::shaderInt64. Without this the software-raster and
    //    resolve shader modules are rejected at creation (Int64 declared but shaderInt64 not enabled) and the whole P4 path silently
    //    goes dark. Enable it whenever either atomics route is live — every device that advertises the atomics also advertises int64.
    if (Host.ShaderImageInt64AtomicsEnabled || Host.ShaderBufferInt64AtomicsEnabled)
        EnabledFeatures.shaderInt64 = VK_TRUE;

    // Assemble the pNext feature chain in a fixed order, splicing in only the structs whose feature is being enabled: dynamic
    // rendering (if present) → buffer int64 atomics (if enabled) → image int64 atomics (if enabled). FeatureChainHead walks to the
    // current tail so each link appends without assuming the previous one is present.
    void** FeatureChainTail = nullptr;
    const void* FeatureChainHead = nullptr;
    auto AppendFeature = [&](void* Structure, void** StructureNext)
    {
        if (FeatureChainTail == nullptr)
            FeatureChainHead = Structure;
        else
            *FeatureChainTail = Structure;
        FeatureChainTail = StructureNext;
        *StructureNext = nullptr;
    };
    if (Host.DynamicRenderingEnabled)
        AppendFeature(&DynamicRenderingFeatures, (void**)&DynamicRenderingFeatures.pNext);
    if (Host.ShaderBufferInt64AtomicsEnabled)
        AppendFeature(&BufferAtomicFeatures, (void**)&BufferAtomicFeatures.pNext);
    if (Host.ShaderImageInt64AtomicsEnabled)
        AppendFeature(&ImageAtomicFeatures, (void**)&ImageAtomicFeatures.pNext);

    VkDeviceCreateInfo DeviceInfo = {};
    DeviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceInfo.pNext                   = FeatureChainHead;
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
    ISSUE_NOTICE("vulkan", "int64 atomics enabled — image: %s, buffer: %s (software micro-raster %s)",
                 Host.ShaderImageInt64AtomicsEnabled  ? "yes" : "no",
                 Host.ShaderBufferInt64AtomicsEnabled ? "yes" : "no",
                 (Host.ShaderImageInt64AtomicsEnabled || Host.ShaderBufferInt64AtomicsEnabled)
                     ? "available" : "gated off — hardware raster only");

    // -- ImGui descriptor pool ------------------------------------------------------------------------------------------
    // 📝 Sized generously enough for the ImGui font atlas + any AddTexture calls a workspace makes. This ImGui version's
    //    Vulkan backend (1.92.x texture-management path) allocates sets with SEPARATE sampler + sampled-image bindings, not
    //    only the classic combined-image-sampler, so the pool must advertise all three types — otherwise the driver logs a
    //    validation CAUTION that the pool has no matching pool size for the SAMPLER / SAMPLED_IMAGE bindings it hands out.
    //    FREE_DESCRIPTOR_SET_BIT is required by the backend.
    // 🔴 maxSets is sized against the ICON REGISTRY, not the font atlas: every distinct icon content-hash costs one descriptor
    //    set, and an icon pack that carries a glyph in two inks (live + gated, because a rasterized SVG cannot be recoloured
    //    after upload) costs two. The ToolMenu pack alone is 291 textures — at the former ceiling of 64 the pool ran dry partway
    //    through registration and every later upload returned VK_ERROR_OUT_OF_POOL_MEMORY, which surfaced as a card drawn with
    //    NO glyphs plus a per-frame null-ImTextureID assertion. 1024 leaves room for the CAD + texture-paint packs beside it.
    VkDescriptorPoolSize PoolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1024 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1024 },
    };
    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    PoolInfo.maxSets       = 1024;
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
