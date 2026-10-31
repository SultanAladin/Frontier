/*==========================================================================================================================================
                                                 VULKAN COMPUTE SUBSTRATE
                                     ------------------------------------------------------------------
                 Dynamic-loaded Vulkan 1.3 offscreen compute, RGBA16F / sRGB 2D-array images,
                         storage buffers, SPIR-V program compilation, and host readback.
==========================================================================================================================================*/
// 🧩 Portable VK_NO_PROTOTYPES compute execution substrate for Environment & Celestial GPU submissions.

#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                        FP16 <-> FP32 CONVERSION FOR RGBA16F READBACK
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint16_t PackHalfFloat(float Value) noexcept;
[[nodiscard]] float    UnpackHalfFloat(uint16_t Bits) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                           GPU STORAGE BUFFER & 2D-ARRAY IMAGE SLOTS
//------------------------------------------------------------------------------------------------------------------------

struct VulkanBufferSlot
{
    VkBuffer       Buffer      = VK_NULL_HANDLE;
    VkDeviceMemory Memory      = VK_NULL_HANDLE;
    VkDeviceSize   Capacity    = 0;
    void*          MappedBytes = nullptr;
};

struct VulkanImageSlot
{
    VkImage       Image         = VK_NULL_HANDLE;
    VkDeviceMemory Memory       = VK_NULL_HANDLE;
    VkImageView   ArrayView     = VK_NULL_HANDLE;
    VkFormat      Format        = VK_FORMAT_UNDEFINED;
    uint32_t      Width         = 0;
    uint32_t      Height        = 0;
    uint32_t      ArrayLayers   = 0;
    VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct VulkanComputeProgram
{
    VkShaderModule                ShaderModule  = VK_NULL_HANDLE;
    VkDescriptorSetLayout         SetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout              Layout        = VK_NULL_HANDLE;
    VkPipeline                    Pipeline      = VK_NULL_HANDLE;
    std::vector<VkDescriptorType> BindingTypes{};
};

struct VulkanDescriptorArgument
{
    VkDescriptorType       Type  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    const VulkanBufferSlot* Buffer = nullptr;
    const VulkanImageSlot*  Image  = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VULKAN COMPUTE SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

class VulkanComputeSubstrate
{
  public:
    VulkanComputeSubstrate() = default;
    ~VulkanComputeSubstrate();

    VulkanComputeSubstrate(const VulkanComputeSubstrate&)            = delete;
    VulkanComputeSubstrate& operator=(const VulkanComputeSubstrate&) = delete;

    // Initializes Vulkan instance, physical device, and compute queue from explicit or discovered libvulkan.so.1.
    bool Initialize(const std::string& ExplicitDriverLibraryPath = "", std::string* OutDiagnostic = nullptr);
    void Shutdown() noexcept;

    [[nodiscard]] bool               Ready() const noexcept { return Device_ != VK_NULL_HANDLE; }
    [[nodiscard]] const std::string& DeviceName() const noexcept { return DeviceName_; }

    // Storage buffer management
    bool AllocateBuffer(VkDeviceSize ByteCount, VulkanBufferSlot& OutSlot);
    void ReleaseBuffer(VulkanBufferSlot& Slot) noexcept;
    bool UploadBuffer(VulkanBufferSlot& Slot, const void* SourceBytes, VkDeviceSize ByteCount);
    bool DownloadBuffer(const VulkanBufferSlot& Slot, void* DestinationBytes, VkDeviceSize ByteCount) const;

    // 2D-Array image management (VK_FORMAT_R16G16B16A16_SFLOAT storage+sampled or VK_FORMAT_R8G8B8A8_SRGB sampled)
    bool AllocateRgba16fArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, VulkanImageSlot& OutSlot);
    bool AllocateSrgb8Array(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, VulkanImageSlot& OutSlot);
    void ReleaseImage(VulkanImageSlot& Slot) noexcept;

    bool ClearRgba16fArray(VulkanImageSlot& Slot, std::array<float, 4> ClearColour = {0.0f, 0.0f, 0.0f, 0.0f});
    bool UploadSrgb8Array(VulkanImageSlot& Slot, std::span<const uint8_t> RgbaBytes);
    bool UploadRgba16fLayer(VulkanImageSlot& Slot, uint32_t LayerIndex, std::span<const float> RgbaFloats);
    bool DownloadRgba16fLayer(VulkanImageSlot& Slot, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats);

    // SPIR-V compute program compilation & synchronous/batched execution
    bool LoadComputeProgram(const std::string&                  SpvFilePath,
                            const char*                         EntryPointName,
                            std::span<const VkDescriptorType>   BindingTypes,
                            uint32_t                            PushConstantBytes,
                            VulkanComputeProgram&               OutProgram,
                            std::string*                        OutDiagnostic = nullptr);
    void ReleaseProgram(VulkanComputeProgram& Program) noexcept;

    bool DispatchCompute(const VulkanComputeProgram&                  Program,
                         std::span<const VulkanDescriptorArgument>    Arguments,
                         const void*                                  PushConstants,
                         uint32_t                                     PushConstantBytes,
                         uint32_t                                     GroupCountX,
                         uint32_t                                     GroupCountY = 1u,
                         uint32_t                                     GroupCountZ = 1u);

  private:
    [[nodiscard]] uint32_t SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredFlags) const;
    bool TransitionImageLayout(VkCommandBuffer CommandBuffer, VulkanImageSlot& Slot, VkImageLayout TargetLayout);

    void*                            LibraryHandle_ = nullptr;
    VkInstance                       Instance_      = VK_NULL_HANDLE;
    VkPhysicalDevice                 PhysicalDevice_ = VK_NULL_HANDLE;
    VkDevice                         Device_        = VK_NULL_HANDLE;
    VkQueue                          Queue_         = VK_NULL_HANDLE;
    uint32_t                         QueueFamily_   = 0u;
    VkCommandPool                    CommandPool_   = VK_NULL_HANDLE;
    VkDescriptorPool                 DescriptorPool_ = VK_NULL_HANDLE;
    VkSampler                        LinearClampSampler_  = VK_NULL_HANDLE;
    VkSampler                        LinearRepeatSampler_ = VK_NULL_HANDLE;
    VkFence                          SubmitFence_   = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties MemoryProperties_{};
    std::string                      DeviceName_{};

    // Dynamically resolved Vulkan entry points
    PFN_vkGetInstanceProcAddr                pfnGetInstanceProcAddr_              = nullptr;
    PFN_vkCreateInstance                     pfnCreateInstance_                   = nullptr;
    PFN_vkDestroyInstance                    pfnDestroyInstance_                  = nullptr;
    PFN_vkEnumeratePhysicalDevices           pfnEnumeratePhysicalDevices_         = nullptr;
    PFN_vkGetPhysicalDeviceProperties        pfnGetPhysicalDeviceProperties_      = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties  pfnGetPhysicalDeviceMemoryProperties_ = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties pfnGetPhysicalDeviceQueueFamilyProperties_ = nullptr;
    PFN_vkCreateDevice                       pfnCreateDevice_                     = nullptr;
    PFN_vkDestroyDevice                      pfnDestroyDevice_                    = nullptr;
    PFN_vkGetDeviceQueue                     pfnGetDeviceQueue_                   = nullptr;
    PFN_vkCreateCommandPool                  pfnCreateCommandPool_                = nullptr;
    PFN_vkDestroyCommandPool                 pfnDestroyCommandPool_               = nullptr;
    PFN_vkAllocateCommandBuffers             pfnAllocateCommandBuffers_           = nullptr;
    PFN_vkFreeCommandBuffers                 pfnFreeCommandBuffers_               = nullptr;
    PFN_vkBeginCommandBuffer                 pfnBeginCommandBuffer_               = nullptr;
    PFN_vkEndCommandBuffer                   pfnEndCommandBuffer_                 = nullptr;
    PFN_vkResetCommandBuffer                 pfnResetCommandBuffer_               = nullptr;
    PFN_vkQueueSubmit                        pfnQueueSubmit_                      = nullptr;
    PFN_vkQueueWaitIdle                      pfnQueueWaitIdle_                    = nullptr;
    PFN_vkCreateFence                        pfnCreateFence_                      = nullptr;
    PFN_vkDestroyFence                       pfnDestroyFence_                     = nullptr;
    PFN_vkWaitForFences                      pfnWaitForFences_                    = nullptr;
    PFN_vkResetFences                        pfnResetFences_                      = nullptr;
    PFN_vkCreateBuffer                       pfnCreateBuffer_                     = nullptr;
    PFN_vkDestroyBuffer                      pfnDestroyBuffer_                    = nullptr;
    PFN_vkGetBufferMemoryRequirements        pfnGetBufferMemoryRequirements_      = nullptr;
    PFN_vkAllocateMemory                     pfnAllocateMemory_                   = nullptr;
    PFN_vkFreeMemory                         pfnFreeMemory_                       = nullptr;
    PFN_vkBindBufferMemory                   pfnBindBufferMemory_                 = nullptr;
    PFN_vkMapMemory                          pfnMapMemory_                        = nullptr;
    PFN_vkUnmapMemory                        pfnUnmapMemory_                      = nullptr;
    PFN_vkCreateImage                        pfnCreateImage_                      = nullptr;
    PFN_vkDestroyImage                       pfnDestroyImage_                     = nullptr;
    PFN_vkGetImageMemoryRequirements         pfnGetImageMemoryRequirements_       = nullptr;
    PFN_vkBindImageMemory                    pfnBindImageMemory_                  = nullptr;
    PFN_vkCreateImageView                    pfnCreateImageView_                  = nullptr;
    PFN_vkDestroyImageView                   pfnDestroyImageView_                 = nullptr;
    PFN_vkCreateSampler                      pfnCreateSampler_                    = nullptr;
    PFN_vkDestroySampler                     pfnDestroySampler_                   = nullptr;
    PFN_vkCreateShaderModule                 pfnCreateShaderModule_               = nullptr;
    PFN_vkDestroyShaderModule                pfnDestroyShaderModule_              = nullptr;
    PFN_vkCreateDescriptorSetLayout          pfnCreateDescriptorSetLayout_        = nullptr;
    PFN_vkDestroyDescriptorSetLayout         pfnDestroyDescriptorSetLayout_       = nullptr;
    PFN_vkCreatePipelineLayout               pfnCreatePipelineLayout_             = nullptr;
    PFN_vkDestroyPipelineLayout              pfnDestroyPipelineLayout_            = nullptr;
    PFN_vkCreateComputePipelines             pfnCreateComputePipelines_           = nullptr;
    PFN_vkDestroyPipeline                    pfnDestroyPipeline_                  = nullptr;
    PFN_vkCreateDescriptorPool               pfnCreateDescriptorPool_             = nullptr;
    PFN_vkDestroyDescriptorPool              pfnDestroyDescriptorPool_            = nullptr;
    PFN_vkResetDescriptorPool                pfnResetDescriptorPool_              = nullptr;
    PFN_vkAllocateDescriptorSets             pfnAllocateDescriptorSets_           = nullptr;
    PFN_vkUpdateDescriptorSets               pfnUpdateDescriptorSets_             = nullptr;
    PFN_vkCmdPipelineBarrier                 pfnCmdPipelineBarrier_               = nullptr;
    PFN_vkCmdBindPipeline                    pfnCmdBindPipeline_                  = nullptr;
    PFN_vkCmdBindDescriptorSets              pfnCmdBindDescriptorSets_            = nullptr;
    PFN_vkCmdPushConstants                   pfnCmdPushConstants_                 = nullptr;
    PFN_vkCmdDispatch                        pfnCmdDispatch_                      = nullptr;
    PFN_vkCmdClearColorImage                 pfnCmdClearColorImage_               = nullptr;
    PFN_vkCmdCopyBufferToImage               pfnCmdCopyBufferToImage_             = nullptr;
    PFN_vkCmdCopyImageToBuffer               pfnCmdCopyImageToBuffer_             = nullptr;
};

} // namespace Frontier
