/*==========================================================================================================================================
                                                 VULKAN COMPUTE EXCHANGE
                                     ------------------------------------------------------------------
                 Dynamic-loaded Vulkan 1.3 offscreen & shared-device compute, RGBA16F / sRGB 2D-array images,
              2D viewport presentation targets, atomic pipeline archive persistence, and hardware GPU timestamps.
==========================================================================================================================================*/
// 🧩 Portable VK_NO_PROTOTYPES compute device exchange for multi-planet Environment, ReSTIR Illumination & Viewport submissions.

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

[[nodiscard]] uint16_t PackHalfFloat(float Scalar) noexcept;
[[nodiscard]] float    UnpackHalfFloat(uint16_t Bits) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                  BORROWED VULKAN DEVICE & PRESENTATION VIEWPORT SLOTS
//------------------------------------------------------------------------------------------------------------------------

struct BorrowedVulkanDeviceSpec
{
    VkDevice                                Device         = VK_NULL_HANDLE;
    VkPhysicalDevice                        PhysicalDevice = VK_NULL_HANDLE;
    VkQueue                                 Queue          = VK_NULL_HANDLE;
    uint32_t                                QueueFamily    = 0u;
    PFN_vkGetDeviceProcAddr                 ResolveDevice  = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties QueryMemory    = nullptr;
    PFN_vkGetPhysicalDeviceProperties       QueryLimits    = nullptr;
};

struct DeviceMemorySpan
{
    VkBuffer       Handle        = VK_NULL_HANDLE;
    VkDeviceMemory Memory        = VK_NULL_HANDLE;
    VkDeviceSize   Capacity      = 0;
    VkDeviceSize   AllocatedSize = 0;
    void*          MappedBytes   = nullptr;
};

struct DeviceImageArraySlot
{
    VkImage                  Image         = VK_NULL_HANDLE;
    VkDeviceMemory           Memory        = VK_NULL_HANDLE;
    VkImageView              ArrayView     = VK_NULL_HANDLE;
    std::vector<VkImageView> LayerViews2D{};
    VkFormat                 Format        = VK_FORMAT_UNDEFINED;
    uint32_t                 Width         = 0;
    uint32_t                 Height        = 0;
    uint32_t                 ArrayLayers   = 0;
    uint32_t                 MipLevels     = 1;
    VkDeviceSize             AllocatedSize = 0;
    VkImageLayout            CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct DeviceVolumeTextureSlot
{
    VkImage        Image         = VK_NULL_HANDLE;
    VkDeviceMemory Memory        = VK_NULL_HANDLE;
    VkImageView    View3D        = VK_NULL_HANDLE;
    VkFormat       Format        = VK_FORMAT_UNDEFINED;
    uint32_t       Width         = 0;
    uint32_t       Height        = 0;
    uint32_t       Depth         = 0;
    VkDeviceSize   AllocatedSize = 0;
    VkImageLayout  CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct DeviceDepthTargetSlot
{
    VkImage                  Image         = VK_NULL_HANDLE;
    VkDeviceMemory           Memory        = VK_NULL_HANDLE;
    VkImageView              ArrayView     = VK_NULL_HANDLE;
    std::vector<VkImageView> SliceViews2D{};
    VkFormat                 Format        = VK_FORMAT_UNDEFINED;
    uint32_t                 Width         = 0;
    uint32_t                 Height        = 0;
    uint32_t                 ArrayLayers   = 0;
    VkDeviceSize             AllocatedSize = 0;
    VkImageLayout            CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct DeviceViewportImageSlot
{
    VkImage        Image         = VK_NULL_HANDLE;
    VkDeviceMemory Memory        = VK_NULL_HANDLE;
    VkImageView    View2D        = VK_NULL_HANDLE;
    VkFormat       Format        = VK_FORMAT_UNDEFINED;
    uint32_t       Width         = 0;
    uint32_t       Height        = 0;
    VkDeviceSize   AllocatedSize = 0;
    VkImageLayout  CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct ComputeProgramSlot
{
    VkShaderModule                ShaderUnit                  = VK_NULL_HANDLE;
    VkDescriptorSetLayout         SetLayout                   = VK_NULL_HANDLE;
    VkPipelineLayout              Layout                      = VK_NULL_HANDLE;
    VkPipeline                    Pipeline                    = VK_NULL_HANDLE;
    std::vector<VkDescriptorType> BindingTypes{};
    std::string                   EntryName{};
    uint64_t                      SpirvFingerprintFnv64       = 0u;
    uint32_t                      SpirvByteCount              = 0u;
    float                         ShaderModuleMilliseconds    = 0.0f;
    float                         PipelineCompileMilliseconds = 0.0f;
};

struct GraphicsRasterProgramSlot
{
    VkShaderModule                VertexUnit                  = VK_NULL_HANDLE;
    VkShaderModule                FragmentUnit                = VK_NULL_HANDLE;
    VkDescriptorSetLayout         SetLayout                   = VK_NULL_HANDLE;
    VkPipelineLayout              Layout                      = VK_NULL_HANDLE;
    VkPipeline                    Pipeline                    = VK_NULL_HANDLE;
    std::vector<VkDescriptorType> BindingTypes{};
    uint32_t                      SpirvByteCount              = 0u;
    float                         PipelineCompileMilliseconds = 0.0f;
};

struct SurfaceFramebufferSet
{
    VkRenderPass             SurfaceProcedure  = VK_NULL_HANDLE;
    VkFramebuffer            SurfaceTarget     = VK_NULL_HANDLE;
    uint32_t                 Width             = 0u;
    uint32_t                 Height            = 0u;
};

struct ShadowFramebufferSet
{
    VkRenderPass                 ShadowProcedure = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>   CascadeTargets{};
    uint32_t                     Resolution      = 0u;
    uint32_t                     CascadeCount    = 0u;
};

struct RasterDrawItem
{
    uint32_t             VertexCount = 0u;
    std::array<uint32_t, 8> PushWords{};
};

struct ComputeDescriptorArgument
{
    VkDescriptorType               Type          = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    const DeviceMemorySpan*        MemorySpan    = nullptr;
    const DeviceImageArraySlot*    ImageArray    = nullptr;
    const DeviceViewportImageSlot* ViewportImage = nullptr;
    const DeviceVolumeTextureSlot* VolumeTexture = nullptr;
    const DeviceDepthTargetSlot*   DepthTarget   = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VULKAN COMPUTE EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class VulkanComputeExchange
{
  public:
    static constexpr uint32_t ExternalRingCount      = 4u;
    static constexpr uint32_t MaxTimedDispatchesSlot = 16u;

    VulkanComputeExchange() = default;
    ~VulkanComputeExchange();

    VulkanComputeExchange(const VulkanComputeExchange&)            = delete;
    VulkanComputeExchange& operator=(const VulkanComputeExchange&) = delete;

    // Initializes standalone Vulkan instance, physical device, and compute queue from explicit or discovered Vulkan loader.
    bool Initialize(const std::string& ExplicitDriverLibraryPath = "",
                    std::string*       OutDiagnostic             = nullptr,
                    const std::string& PipelineArchivePath       = "");

    // Attaches to an existing presentation VkDevice + VkQueue for zero-copy ImGui viewport sharing.
    bool AttachBorrowedDevice(const BorrowedVulkanDeviceSpec& BorrowedSpec,
                              const std::string&              PipelineArchivePath = "",
                              std::string*                    OutDiagnostic       = nullptr);

    void Shutdown() noexcept;

    [[nodiscard]] bool               Ready() const noexcept { return Device_ != VK_NULL_HANDLE; }
    [[nodiscard]] bool               BorrowedDeviceActive() const noexcept { return BorrowedDeviceEnabled_; }
    [[nodiscard]] bool               GraphicsSupported() const noexcept { return GraphicsSupported_; }
    [[nodiscard]] const std::string& DeviceName() const noexcept { return DeviceName_; }
    [[nodiscard]] uint32_t           VendorIdentifier() const noexcept { return VendorId_; }
    [[nodiscard]] uint32_t           DeviceIdentifier() const noexcept { return DeviceId_; }
    [[nodiscard]] uint32_t           DriverVersion() const noexcept { return DriverVersion_; }
    [[nodiscard]] uint32_t           ApiVersion() const noexcept { return ApiVersion_; }
    [[nodiscard]] VkSampler          LinearClampSampler() const noexcept { return LinearClampSampler_; }
    [[nodiscard]] uint64_t           QueryAllocatedDeviceBytes() const noexcept { return AllocatedDeviceBytes_; }
    [[nodiscard]] uint64_t           QueryPipelineArchiveLoadedBytes() const noexcept { return PipelineArchiveLoadedBytes_; }
    [[nodiscard]] uint64_t           QueryPipelineArchiveSavedBytes() const noexcept { return PipelineArchiveSavedBytes_; }
    [[nodiscard]] float              QueryLastDispatchGpuMilliseconds() const noexcept { return LastDispatchGpuMilliseconds_; }
    [[nodiscard]] float              QueryLastDispatchCpuMilliseconds() const noexcept { return LastDispatchCpuMilliseconds_; }

    void ConfigurePipelineArchivePath(const std::string& ArchivePath);
    bool PersistPipelineArchive() noexcept;
    void WaitDeviceIdle() noexcept;

    // External command-stream recording inside VulkanPresentation::ConstructGpu (zero CPU-GPU stall)
    void BeginExternalCommandStream(VkCommandBuffer ExternalStream, uint32_t RingSlot) noexcept;
    void EndExternalCommandStream() noexcept;
    [[nodiscard]] bool HarvestExternalSlotTimings(uint32_t RingSlot, std::span<float> OutDispatchMilliseconds) noexcept;

    // Contiguous GPU memory span management
    bool AllocateMemorySpan(VkDeviceSize ByteCount, DeviceMemorySpan& OutSlot);
    void ReleaseMemorySpan(DeviceMemorySpan& Slot) noexcept;
    bool UploadMemorySpan(DeviceMemorySpan& Slot, const void* SourceBytes, VkDeviceSize ByteCount);
    bool DownloadMemorySpan(const DeviceMemorySpan& Slot, void* DestinationBytes, VkDeviceSize ByteCount) const;

    // 2D-Array image management (VK_FORMAT_R16G16B16A16_SFLOAT storage+sampled or VK_FORMAT_R8G8B8A8_SRGB sampled)
    bool AllocateRgba16fArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot);
    bool AllocateSrgb8Array(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot);
    void ReleaseImage(DeviceImageArraySlot& Slot) noexcept;

    bool ClearRgba16fArray(DeviceImageArraySlot& Slot, std::array<float, 4> ClearColour = {0.0f, 0.0f, 0.0f, 0.0f});
    bool UploadSrgb8Array(DeviceImageArraySlot& Slot, std::span<const uint8_t> RgbaBytes);
    bool UploadRgba16fLayer(DeviceImageArraySlot& Slot, uint32_t LayerIndex, std::span<const float> RgbaFloats);
    bool DownloadRgba16fLayer(DeviceImageArraySlot& Slot, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats);

    // 2D RGBA8_UNORM presentation target (storage write in compute + VK_IMAGE_VIEW_TYPE_2D sampled in ImGui)
    bool AllocateViewportImage2D(uint32_t Width, uint32_t Height, DeviceViewportImageSlot& OutSlot);
    void ReleaseViewportImage2D(DeviceViewportImageSlot& Slot) noexcept;
    bool PrepareViewportImageForCompute(DeviceViewportImageSlot& Slot);
    bool PrepareViewportImageForSampling(DeviceViewportImageSlot& Slot);
    bool UploadViewportImageRgba8(DeviceViewportImageSlot& Slot, std::span<const uint8_t> SourceRgbaBytes);
    bool DownloadViewportImageRgba8(DeviceViewportImageSlot& Slot, std::vector<uint8_t>& OutRgbaBytes);

    // 3D hardware trilinear SDF clipmap volume texture (VK_IMAGE_TYPE_3D, VK_FORMAT_R16_SFLOAT)
    bool AllocateR16fVolume3D(uint32_t Width, uint32_t Height, uint32_t Depth, DeviceVolumeTextureSlot& OutSlot);
    bool UploadR16fVolume3D(DeviceVolumeTextureSlot& Slot, std::span<const float> ScalarDistances);
    void ReleaseVolume3D(DeviceVolumeTextureSlot& Slot) noexcept;

    // Hardware D32_SFLOAT depth attachment & sampled cascade shadow array
    bool AllocateDepthTargetArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceDepthTargetSlot& OutSlot);
    void ReleaseDepthTarget(DeviceDepthTargetSlot& Slot) noexcept;

    // Hardware MRT surface/forward framebuffer & cascaded shadow framebuffer management
    bool CreateSurfaceProcedureOnly(VkRenderPass& OutProcedure);
    bool CreateShadowProcedureOnly(VkRenderPass& OutProcedure);
    void ReleaseRenderProcedure(VkRenderPass& Procedure) noexcept;

    bool CreateSurfaceFramebufferSet(VkRenderPass                 CompatibleProcedure,
                                     const DeviceImageArraySlot&  TargetAtlas,
                                     const DeviceDepthTargetSlot& DepthTarget,
                                     SurfaceFramebufferSet&       OutSet);
    void ReleaseSurfaceFramebufferSet(SurfaceFramebufferSet& Set) noexcept;

    bool CreateShadowFramebufferSet(VkRenderPass                 CompatibleProcedure,
                                    const DeviceDepthTargetSlot& ShadowDepthArray,
                                    ShadowFramebufferSet&        OutSet);
    void ReleaseShadowFramebufferSet(ShadowFramebufferSet& Set) noexcept;

    // Hardware SPIR-V vertex/fragment raster pipeline compilation & execution
    bool LoadGraphicsProgram(const std::string&                VertSpvPath,
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
                             std::string*                      OutDiagnostic = nullptr);
    void ReleaseGraphicsProgram(GraphicsRasterProgramSlot& Program) noexcept;

    bool ExecuteShadowCascadeDraws(const GraphicsRasterProgramSlot&           ShadowProgram,
                                   const ShadowFramebufferSet&                ShadowTargets,
                                   DeviceDepthTargetSlot&                     ShadowDepthSlot,
                                   std::span<const ComputeDescriptorArgument> Arguments,
                                   uint32_t                                   CascadeIndex,
                                   std::span<const RasterDrawItem>            Draws);

    bool ExecuteSurfaceAndForwardDraws(const GraphicsRasterProgramSlot&           OpaqueProgram,
                                       const GraphicsRasterProgramSlot&           ForwardGlassProgram,
                                       const SurfaceFramebufferSet&               SurfaceTargets,
                                       DeviceImageArraySlot&                      TargetAtlas,
                                       DeviceDepthTargetSlot&                     DepthTarget,
                                       std::span<const ComputeDescriptorArgument> Arguments,
                                       std::span<const RasterDrawItem>            OpaqueDraws,
                                       std::span<const RasterDrawItem>            ForwardGlassDraws);

    // SPIR-V compute program compilation & queue execution
    bool LoadComputeProgram(const std::string&                SpvFilePath,
                            const char*                       EntryPointName,
                            std::span<const VkDescriptorType> BindingTypes,
                            uint32_t                          PushConstantBytes,
                            ComputeProgramSlot&               OutProgram,
                            std::string*                      OutDiagnostic = nullptr);
    void ReleaseProgram(ComputeProgramSlot& Program) noexcept;

    bool DispatchCompute(const ComputeProgramSlot&                  Program,
                         std::span<const ComputeDescriptorArgument> Arguments,
                         const void*                                PushConstants,
                         uint32_t                                   PushConstantBytes,
                         uint32_t                                   GroupCountX,
                         uint32_t                                   GroupCountY = 1u,
                         uint32_t                                   GroupCountZ = 1u);

  private:
    bool ResolveDeviceFunctionsAndPools(PFN_vkGetDeviceProcAddr GetDeviceProc,
                                        const std::string&      PipelineArchivePath,
                                        std::string*            OutDiagnostic);
    bool InitializePipelineArchive(const std::string& ArchivePath);
    [[nodiscard]] bool AllocateUpdatedDescriptorSet(VkDescriptorSetLayout                      SetLayout,
                                                    std::span<const VkDescriptorType>          BindingTypes,
                                                    std::span<const ComputeDescriptorArgument> Arguments,
                                                    VkDescriptorSet&                           OutSet);
    [[nodiscard]] uint32_t SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredFlags) const;
    bool TransitionImageLayout(VkCommandBuffer CommandStream, DeviceImageArraySlot& Slot, VkImageLayout TargetLayout);
    bool TransitionViewportLayout(VkCommandBuffer          CommandStream,
                                  DeviceViewportImageSlot& Slot,
                                  VkImageLayout            TargetLayout,
                                  VkPipelineStageFlags     SrcStage,
                                  VkPipelineStageFlags     DstStage,
                                  VkAccessFlags            SrcAccess,
                                  VkAccessFlags            DstAccess);

    void*                                                 LibraryHandle_               = nullptr;
    VkInstance                                            Instance_                    = VK_NULL_HANDLE;
    VkPhysicalDevice                                      PhysicalDevice_              = VK_NULL_HANDLE;
    VkDevice                                              Device_                      = VK_NULL_HANDLE;
    VkQueue                                               Queue_                       = VK_NULL_HANDLE;
    uint32_t                                              QueueFamily_                 = 0u;
    bool                                                  BorrowedDeviceEnabled_       = false;
    bool                                                  GraphicsSupported_           = false;
    VkCommandPool                                         CommandPool_                 = VK_NULL_HANDLE;
    VkDescriptorPool                                      DescriptorPool_              = VK_NULL_HANDLE;
    std::array<VkDescriptorPool, ExternalRingCount>       ExternalDescriptorPools_{};
    VkSampler                                             LinearClampSampler_          = VK_NULL_HANDLE;
    VkSampler                                             LinearRepeatSampler_         = VK_NULL_HANDLE;
    VkSampler                                             NearestClampSampler_         = VK_NULL_HANDLE;
    VkFence                                               SubmitFence_                 = VK_NULL_HANDLE;
    VkPipelineCache                                       PipelineArchiveHandle_       = VK_NULL_HANDLE;
    std::string                                           PipelineArchivePath_{};
    VkQueryPool                                           TimestampQueryPool_          = VK_NULL_HANDLE;
    float                                                 TimestampPeriodNanoseconds_  = 0.0f;
    bool                                                  TimestampsSupported_         = false;
    VkCommandBuffer                                       ActiveExternalStream_        = VK_NULL_HANDLE;
    uint32_t                                              ActiveExternalSlot_          = 0u;
    std::array<uint32_t, ExternalRingCount>               ExternalSlotDispatchCounts_{};
    std::array<bool, ExternalRingCount>                   ExternalSlotRecorded_{};
    VkPhysicalDeviceMemoryProperties                      MemoryProperties_{};
    std::string                                           DeviceName_{};
    uint32_t                                              VendorId_                    = 0u;
    uint32_t                                              DeviceId_                    = 0u;
    uint32_t                                              DriverVersion_               = 0u;
    uint32_t                                              ApiVersion_                  = 0u;
    uint64_t                                              AllocatedDeviceBytes_        = 0u;
    uint64_t                                              PipelineArchiveLoadedBytes_  = 0u;
    uint64_t                                              PipelineArchiveSavedBytes_   = 0u;
    float                                                 LastDispatchGpuMilliseconds_ = 0.0f;
    float                                                 LastDispatchCpuMilliseconds_ = 0.0f;

    PFN_vkGetInstanceProcAddr                    pfnGetInstanceProcAddr_                    = nullptr;
    PFN_vkCreateInstance                         pfnCreateInstance_                         = nullptr;
    PFN_vkDestroyInstance                        pfnDestroyInstance_                        = nullptr;
    PFN_vkEnumeratePhysicalDevices               pfnEnumeratePhysicalDevices_               = nullptr;
    PFN_vkGetPhysicalDeviceProperties            pfnGetPhysicalDeviceProperties_            = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties      pfnGetPhysicalDeviceMemoryProperties_      = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties pfnGetPhysicalDeviceQueueFamilyProperties_ = nullptr;
    PFN_vkCreateDevice                           pfnCreateDevice_                           = nullptr;
    PFN_vkDestroyDevice                          pfnDestroyDevice_                          = nullptr;
    PFN_vkDeviceWaitIdle                         pfnDeviceWaitIdle_                         = nullptr;
    PFN_vkGetDeviceQueue                         pfnGetDeviceQueue_                         = nullptr;
    PFN_vkCreateCommandPool                      pfnCreateCommandPool_                      = nullptr;
    PFN_vkDestroyCommandPool                     pfnDestroyCommandPool_                     = nullptr;
    PFN_vkAllocateCommandBuffers                 pfnAllocateCommandStreams_                 = nullptr;
    PFN_vkFreeCommandBuffers                     pfnFreeCommandStreams_                     = nullptr;
    PFN_vkBeginCommandBuffer                     pfnBeginCommandStream_                     = nullptr;
    PFN_vkEndCommandBuffer                       pfnEndCommandStream_                       = nullptr;
    PFN_vkResetCommandBuffer                     pfnResetCommandStream_                     = nullptr;
    PFN_vkQueueSubmit                            pfnQueueSubmit_                            = nullptr;
    PFN_vkQueueWaitIdle                          pfnQueueWaitIdle_                          = nullptr;
    PFN_vkCreateFence                            pfnCreateFence_                            = nullptr;
    PFN_vkDestroyFence                           pfnDestroyFence_                           = nullptr;
    PFN_vkWaitForFences                          pfnWaitForFences_                          = nullptr;
    PFN_vkResetFences                            pfnResetFences_                            = nullptr;
    PFN_vkCreateBuffer                           pfnCreateMemorySpan_                       = nullptr;
    PFN_vkDestroyBuffer                          pfnDestroyMemorySpan_                      = nullptr;
    PFN_vkGetBufferMemoryRequirements            pfnGetSpanMemoryRequirements_              = nullptr;
    PFN_vkAllocateMemory                         pfnAllocateMemory_                         = nullptr;
    PFN_vkFreeMemory                             pfnFreeMemory_                             = nullptr;
    PFN_vkBindBufferMemory                       pfnBindSpanMemory_                         = nullptr;
    PFN_vkMapMemory                              pfnMapMemory_                              = nullptr;
    PFN_vkUnmapMemory                            pfnUnmapMemory_                            = nullptr;
    PFN_vkCreateImage                            pfnCreateImage_                            = nullptr;
    PFN_vkDestroyImage                           pfnDestroyImage_                           = nullptr;
    PFN_vkGetImageMemoryRequirements             pfnGetImageMemoryRequirements_             = nullptr;
    PFN_vkBindImageMemory                        pfnBindImageMemory_                        = nullptr;
    PFN_vkCreateImageView                        pfnCreateImageView_                        = nullptr;
    PFN_vkDestroyImageView                       pfnDestroyImageView_                       = nullptr;
    PFN_vkCreateSampler                          pfnCreateSampler_                          = nullptr;
    PFN_vkDestroySampler                         pfnDestroySampler_                         = nullptr;
    PFN_vkCreateShaderModule                     pfnCreateShaderUnit_                       = nullptr;
    PFN_vkDestroyShaderModule                    pfnDestroyShaderUnit_                      = nullptr;
    PFN_vkCreateDescriptorSetLayout              pfnCreateDescriptorSetLayout_              = nullptr;
    PFN_vkDestroyDescriptorSetLayout             pfnDestroyDescriptorSetLayout_             = nullptr;
    PFN_vkCreatePipelineLayout                   pfnCreatePipelineLayout_                   = nullptr;
    PFN_vkDestroyPipelineLayout                  pfnDestroyPipelineLayout_                  = nullptr;
    PFN_vkCreatePipelineCache                    pfnCreatePipelineArchive_                  = nullptr;
    PFN_vkDestroyPipelineCache                   pfnDestroyPipelineArchive_                 = nullptr;
    PFN_vkGetPipelineCacheData                   pfnGetPipelineArchiveBytes_                = nullptr;
    PFN_vkCreateComputePipelines                 pfnCreateComputePipelines_                 = nullptr;
    PFN_vkDestroyPipeline                        pfnDestroyPipeline_                        = nullptr;
    PFN_vkCreateDescriptorPool                   pfnCreateDescriptorPool_                   = nullptr;
    PFN_vkDestroyDescriptorPool                  pfnDestroyDescriptorPool_                  = nullptr;
    PFN_vkResetDescriptorPool                    pfnResetDescriptorPool_                    = nullptr;
    PFN_vkAllocateDescriptorSets                 pfnAllocateDescriptorSets_                 = nullptr;
    PFN_vkUpdateDescriptorSets                   pfnUpdateDescriptorSets_                   = nullptr;
    PFN_vkCreateQueryPool                        pfnCreateQueryPool_                        = nullptr;
    PFN_vkDestroyQueryPool                       pfnDestroyQueryPool_                       = nullptr;
    PFN_vkGetQueryPoolResults                    pfnGetQueryPoolResults_                    = nullptr;
    PFN_vkCmdResetQueryPool                      pfnCmdResetQueryPool_                      = nullptr;
    PFN_vkCmdWriteTimestamp                      pfnCmdWriteTimestamp_                      = nullptr;
    PFN_vkCmdPipelineBarrier                     pfnCmdPipelineBarrier_                     = nullptr;
    PFN_vkCmdBindPipeline                        pfnCmdBindPipeline_                        = nullptr;
    PFN_vkCmdBindDescriptorSets                  pfnCmdBindDescriptorSets_                  = nullptr;
    PFN_vkCmdPushConstants                       pfnCmdPushConstants_                       = nullptr;
    PFN_vkCmdDispatch                            pfnCmdDispatch_                            = nullptr;
    PFN_vkCmdClearColorImage                     pfnCmdClearColorImage_                     = nullptr;
    PFN_vkCmdCopyBufferToImage                   pfnCmdCopySpanToImage_                     = nullptr;
    PFN_vkCmdCopyImageToBuffer                   pfnCmdCopyImageToSpan_                     = nullptr;
    PFN_vkCreateRenderPass                       pfnCreateRenderPass_                       = nullptr;
    PFN_vkDestroyRenderPass                      pfnDestroyRenderPass_                      = nullptr;
    PFN_vkCreateFramebuffer                      pfnCreateFramebuffer_                      = nullptr;
    PFN_vkDestroyFramebuffer                     pfnDestroyFramebuffer_                     = nullptr;
    PFN_vkCreateGraphicsPipelines                pfnCreateGraphicsPipelines_                = nullptr;
    PFN_vkCmdBeginRenderPass                     pfnCmdBeginRenderPass_                     = nullptr;
    PFN_vkCmdEndRenderPass                       pfnCmdEndRenderPass_                       = nullptr;
    PFN_vkCmdSetViewport                         pfnCmdSetViewport_                         = nullptr;
    PFN_vkCmdSetScissor                          pfnCmdSetScissor_                          = nullptr;
    PFN_vkCmdSetDepthBias                        pfnCmdSetDepthBias_                        = nullptr;
    PFN_vkCmdDraw                                pfnCmdDraw_                                = nullptr;
};

} // namespace Frontier
