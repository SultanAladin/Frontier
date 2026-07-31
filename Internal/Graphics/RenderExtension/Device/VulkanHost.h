/*==============================================================================================================================================
                                                                VULKANHOST.H
==============================================================================================================================================*/
// 🧩 One-time Vulkan bring-up shared by every application: instance, physical device, logical device, graphics/present queue, and the ImGui
//    descriptor pool. Built once at start, finalized once at exit. Holds no per-frame or per-window state — the swapchain and frame lifecycle
//    live in VulkanImguiInterface. "Device" here is the Vulkan term (VkDevice), not a coordinator noun.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_DEVICE_VULKANHOST_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_DEVICE_VULKANHOST_H

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The shared Vulkan objects. One instance per application, held across the whole run. Allocator stays nullptr (default host
//    allocator); DebugReport is VK_NULL_HANDLE unless the validation layer is compiled in.
struct VulkanHost
{
    VkInstance               Instance             = VK_NULL_HANDLE;   // [-] - Vulkan instance
    VkPhysicalDevice         PhysicalDevice       = VK_NULL_HANDLE;   // [-] - Selected GPU (discrete preferred)
    VkDevice                 Device               = VK_NULL_HANDLE;   // [-] - Logical device
    uint32_t                 GraphicsQueueFamily  = 0xFFFFFFFFu;      // [-] - Graphics-capable queue family index
    VkQueue                  GraphicsQueue        = VK_NULL_HANDLE;   // [-] - Queue for submit + present
    VkDescriptorPool         ImguiDescriptorPool  = VK_NULL_HANDLE;   // [-] - Pool sized for the ImGui backend
    const VkAllocationCallbacks* Allocator        = nullptr;          // [-] - Default host allocator
    uint32_t                 ApiVersion           = 0;                // [-] - Instance API version passed to the ImGui backend
    bool                     DynamicRenderingEnabled = false;         // [-] - VK_KHR_dynamic_rendering active (grid pass renders without render-pass objects)
    bool                     ShaderImageInt64AtomicsEnabled  = false; // [-] - shaderImageInt64Atomics turned ON at device creation (software micro-raster depth+id atomicMax path)
    bool                     ShaderBufferInt64AtomicsEnabled = false; // [-] - shaderBufferInt64Atomics turned ON at device creation (fallback packing path for the software raster)
    bool                     FragmentStoresAndAtomicsEnabled = false; // [-] - fragmentStoresAndAtomics turned ON at device creation (fragment-stage SSBO / storage-image writes: shadow tile tag + page-atlas raster)
    PFN_vkCmdBeginRenderingKHR CmdBeginRendering    = nullptr;        // [-] - Loaded entry point (1.2 instance, KHR extension)
    PFN_vkCmdEndRenderingKHR   CmdEndRendering      = nullptr;        // [-] - Loaded entry point (1.2 instance, KHR extension)
    VkDebugUtilsMessengerEXT ValidationSignalBroadcaster = VK_NULL_HANDLE; // [-] - Validation-layer signal route (development profile only)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Create the instance (with the platform-required presentation extensions), select a GPU + graphics queue family, create the logical device
// and the ImGui descriptor pool. Returns false on any failure with the host left safely finalizable.
bool InitializeVulkanHost(VulkanHost&  Host,
                          const char** RequiredInstanceExtensions,
                          uint32_t     ExtensionCount);

// Destroy everything InitializeVulkanHost created. Safe to call on a partially-initialized host.
void FinalizeVulkanHost(VulkanHost& Host);

} // namespace Frontier

#endif
