/*==============================================================================================================================================
                                                            GPUPAINTDISPATCH.CPP
==============================================================================================================================================*/
// 🧩 Vulkan implementation of the UV-space paint stamp. A GpuPaintContext owns a transient command pool, one render pass over an
//    R8G8B8A8_UNORM colour attachment (loadOp LOAD so stamps accumulate), one graphics pipeline built from PaintUvRaster.vert/.frag,
//    and a framebuffer cache keyed by image view. RecordPaintStamp records a one-shot command buffer: an image barrier
//    SHADER_READ_ONLY -> COLOUR_ATTACHMENT, begin the render pass on the layer image, bind the pipeline + the model's vertex/index
//    buffers, push the brush block, draw indexed (the vertex stage lands each triangle at its UV; the fragment reprojects to the
//    brush circle and blends over), end the pass, barrier back to SHADER_READ_ONLY, then submit on the graphics queue under a fresh
//    fence and wait it. Manual vkAllocateMemory-free path — the paint target is the caller's already-resident layer image, so no
//    memory is owned here. Mirrors GpuBakeDispatch's opaque-struct discipline; graphics-based rather than compute.

#include "GpuPaintDispatch.h"

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
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
    // 📝 The push-constant block, byte-matched to PaintStampBlock in PaintUvRaster.vert/.frag. A GLSL push-constant vec4 is
    //    16-byte aligned (default block layout, no scalar qualifier), so the shader places BrushColour at byte 96 — the first
    //    16-byte boundary after the six scalars that follow the mat4 (which end at 88, NOT a 16-byte boundary: 88/16 = 5.5).
    //    ColourAlignment fills bytes 88-95 so the host struct matches the shader: BrushColour @96, TargetExtent @112. Without it
    //    the shader read TargetExtent one slot past the payload (garbage), the reprojection landed off-screen, and every fragment
    //    discarded — the stamp recorded but wrote nothing. Total 120 B, under the 128-byte guaranteed push-constant minimum.
    struct PaintStampPush
    {
        float ModelViewProjection[16];   // [-]  - world -> clip                                        @0
        float BrushCentre[2];            // [px] - stroke centre in canvas pixels                       @64
        float BrushRadius;               // [px] - footprint radius                                     @72
        float Hardness;                  // [0-1]- inner solid fraction                                 @76
        float Opacity;                   // [0-1]- overall strength                                     @80
        float Flow;                      // [0-1]- per-stamp deposit                                    @84
        float ColourAlignment[2];        // [-]  - pad so BrushColour lands on the vec4 16-byte boundary @88
        float BrushColour[4];            // [-]  - straight albedo rgba                                 @96
        float TargetExtent[2];           // [px] - canvas width/height                                  @112
    };

    // 📝 One VkFormat's cached graphics objects: a render pass + pipeline built for that attachment format. The eight PBR channels
    //    span only four distinct formats (RGBA8, R8, R16, RGBA16F), so at most four entries ever live. The pipeline layout + shader
    //    modules are shared across formats (the push contract + shaders are identical — only the attachment format + its implicit
    //    blend precision differ), so they stay on the implementation, not here.
    struct PaintFormatObjects
    {
        VkFormat     Format     = VK_FORMAT_UNDEFINED;
        VkRenderPass RenderPass = VK_NULL_HANDLE;
        VkPipeline   Pipeline   = VK_NULL_HANDLE;
    };

    // 📝 The owned graphics objects. PipelineLayout + the shader modules are built once at Initialize (fixed); a render pass +
    //    pipeline are built LAZILY per target VkFormat on first stamp into it and cached in FormatObjects. Framebuffers are built +
    //    destroyed per stamp. The command pool is transient — one command buffer per stamp, reset each time.
    struct GpuPaintContextImplementation
    {
        VkPhysicalDevice PhysicalDevice   = VK_NULL_HANDLE;
        VkDevice         Device           = VK_NULL_HANDLE;
        VkQueue          Queue            = VK_NULL_HANDLE;   // [-]   - graphics queue (the colour-attachment write lands here)
        uint32_t         QueueFamilyIndex = 0;                // [idx] - graphics family (the transient command pool builds here)

        VkCommandPool    CommandPool    = VK_NULL_HANDLE;

        VkShaderModule   VertexModule   = VK_NULL_HANDLE;
        VkShaderModule   FragmentModule = VK_NULL_HANDLE;
        VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;

        std::vector<PaintFormatObjects> FormatObjects;   // [-] - render pass + pipeline per target VkFormat (lazily built, <=4 live)
    };

    void ReportPaint(const char* MessageText)
    {
        std::fprintf(stderr, "[GpuPaintDispatch] %s\n", MessageText);
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

    VkShaderModule ConstructShaderModule(VkDevice Device, const std::vector<char>& SpirV)
    {
        VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ModuleInformation.codeSize = SpirV.size();
        ModuleInformation.pCode    = reinterpret_cast<const uint32_t*>(SpirV.data());
        VkShaderModule Module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &ModuleInformation, nullptr, &Module) != VK_SUCCESS) return VK_NULL_HANDLE;
        return Module;
    }

    // Build a render pass over a single colour attachment of TargetFormat that LOADs its prior contents (so stamps accumulate) and
    // keeps COLOUR_ATTACHMENT_OPTIMAL as both the initial and final layout — the caller's explicit barriers move it in and out of
    // SHADER_READ_ONLY around the render pass. The pass is otherwise format-agnostic; only the attachment format varies per channel.
    bool ConstructRenderPass(GpuPaintContextImplementation& Implementation, VkFormat TargetFormat, VkRenderPass& OutRenderPass)
    {
        VkAttachmentDescription ColourAttachment = {};
        ColourAttachment.format         = TargetFormat;
        ColourAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        ColourAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        ColourAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        ColourAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ColourAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ColourAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ColourAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ColourReference = {};
        ColourReference.attachment = 0;
        ColourReference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription Subpass = {};
        Subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subpass.colorAttachmentCount = 1;
        Subpass.pColorAttachments    = &ColourReference;

        // 📝 The external barriers the caller records handle the SHADER_READ <-> COLOUR_ATTACHMENT moves; the subpass dependency
        //    here only orders the colour write against itself so overlapping stamps into the same image serialize correctly.
        VkSubpassDependency Dependencies[2] = {};
        Dependencies[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        Dependencies[0].dstSubpass    = 0;
        Dependencies[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        Dependencies[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        Dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Dependencies[1].srcSubpass    = 0;
        Dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        Dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        Dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        Dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo RenderPassInformation = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        RenderPassInformation.attachmentCount = 1;
        RenderPassInformation.pAttachments    = &ColourAttachment;
        RenderPassInformation.subpassCount    = 1;
        RenderPassInformation.pSubpasses      = &Subpass;
        RenderPassInformation.dependencyCount = 2;
        RenderPassInformation.pDependencies   = Dependencies;
        return vkCreateRenderPass(Implementation.Device, &RenderPassInformation, nullptr, &OutRenderPass) == VK_SUCCESS;
    }

    // Build the shared pipeline layout (one push-constant range, VS+FS). Called once at Initialize — the layout is format-agnostic.
    bool ConstructPipelineLayout(GpuPaintContextImplementation& Implementation)
    {
        VkPushConstantRange PushRange = {};
        PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        PushRange.offset     = 0;
        PushRange.size       = sizeof(PaintStampPush);
        VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PipelineLayoutInformation.pushConstantRangeCount = 1;
        PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
        return vkCreatePipelineLayout(Implementation.Device, &PipelineLayoutInformation, nullptr, &Implementation.PipelineLayout) == VK_SUCCESS;
    }

    // Build the graphics pipeline for one target format's render pass: the PaintUvRaster vert/frag pair, the RenderVertex input
    // contract (position @0, normal @12, uv @24, stride 32), no depth, src-alpha over blending so stamps accumulate (the same
    // coverage-alpha blend converges scalar channels toward their target), dynamic viewport/scissor sized per target.
    bool ConstructPipeline(GpuPaintContextImplementation& Implementation, VkRenderPass RenderPass, VkPipeline& OutPipeline)
    {
        VkPipelineShaderStageCreateInfo Stages[2] = {};
        Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        Stages[0].module = Implementation.VertexModule;
        Stages[0].pName  = "main";
        Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        Stages[1].module = Implementation.FragmentModule;
        Stages[1].pName  = "main";

        VkVertexInputBindingDescription VertexBinding = {};
        VertexBinding.binding   = 0;
        VertexBinding.stride    = 32;                               // RenderVertex stride (3+3+2 floats)
        VertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription VertexAttributes[3] = {};
        VertexAttributes[0].location = 0;
        VertexAttributes[0].binding  = 0;
        VertexAttributes[0].format   = VK_FORMAT_R32G32B32_SFLOAT;   // position @0
        VertexAttributes[0].offset   = 0;
        VertexAttributes[1].location = 1;
        VertexAttributes[1].binding  = 0;
        VertexAttributes[1].format   = VK_FORMAT_R32G32B32_SFLOAT;   // normal @12
        VertexAttributes[1].offset   = 12;
        VertexAttributes[2].location = 2;
        VertexAttributes[2].binding  = 0;
        VertexAttributes[2].format   = VK_FORMAT_R32G32_SFLOAT;      // uv @24
        VertexAttributes[2].offset   = 24;

        VkPipelineVertexInputStateCreateInfo VertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VertexInput.vertexBindingDescriptionCount   = 1;
        VertexInput.pVertexBindingDescriptions      = &VertexBinding;
        VertexInput.vertexAttributeDescriptionCount = 3;
        VertexInput.pVertexAttributeDescriptions    = VertexAttributes;

        VkPipelineInputAssemblyStateCreateInfo InputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // 📝 No back-face cull: the UV-space triangle winding is unrelated to on-screen facing, and the fragment already discards
        //    behind-eye texels. Culling here would drop texels whose UV winding differs, leaving holes in the stamp.
        VkPipelineRasterizationStateCreateInfo Rasterization = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        Rasterization.cullMode    = VK_CULL_MODE_NONE;
        Rasterization.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        Rasterization.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo Multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 📝 Src-alpha over (matches the reference painter's stamp blend): the colour channel converges the stored RGB toward the
        //    straight brush colour as coverage builds within a layer (dst -> Colour, not Colour·Alpha), and alpha accumulates over.
        //    The composite pass and surface read the layer straight (mix by coverage alpha). A single low-opacity dab is faint by
        //    design — that is the brush Opacity/Flow working, not a bug. The real fault was the push-constant misalignment above.
        VkPipelineColorBlendAttachmentState BlendAttachment = {};
        BlendAttachment.blendEnable         = VK_TRUE;
        BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
        BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        BlendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo ColourBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        ColourBlend.attachmentCount = 1;
        ColourBlend.pAttachments    = &BlendAttachment;

        VkPipelineViewportStateCreateInfo ViewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        ViewportState.viewportCount = 1;
        ViewportState.scissorCount  = 1;

        VkDynamicState DynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo DynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        DynamicState.dynamicStateCount = 2;
        DynamicState.pDynamicStates    = DynamicStates;

        VkGraphicsPipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        PipelineInformation.stageCount          = 2;
        PipelineInformation.pStages             = Stages;
        PipelineInformation.pVertexInputState   = &VertexInput;
        PipelineInformation.pInputAssemblyState = &InputAssembly;
        PipelineInformation.pViewportState      = &ViewportState;
        PipelineInformation.pRasterizationState = &Rasterization;
        PipelineInformation.pMultisampleState   = &Multisample;
        PipelineInformation.pColorBlendState    = &ColourBlend;
        PipelineInformation.pDynamicState       = &DynamicState;
        PipelineInformation.layout              = Implementation.PipelineLayout;
        PipelineInformation.renderPass          = RenderPass;
        PipelineInformation.subpass             = 0;
        return vkCreateGraphicsPipelines(Implementation.Device, VK_NULL_HANDLE, 1, &PipelineInformation, nullptr,
                                         &OutPipeline) == VK_SUCCESS;
    }

    // 📝 Resolve (lazily build + cache) the render pass + pipeline for TargetFormat. Returns nullptr Objects on a build failure.
    //    At most four distinct channel formats ever appear, so the linear scan is trivially small.
    const PaintFormatObjects* ResolveFormatObjects(GpuPaintContextImplementation& Implementation, VkFormat TargetFormat)
    {
        for (const PaintFormatObjects& Existing : Implementation.FormatObjects)
            if (Existing.Format == TargetFormat)
                return &Existing;

        PaintFormatObjects Built;
        Built.Format = TargetFormat;
        if (!ConstructRenderPass(Implementation, TargetFormat, Built.RenderPass))
        {
            ReportPaint("failed to build paint render pass for a channel format");
            return nullptr;
        }
        if (!ConstructPipeline(Implementation, Built.RenderPass, Built.Pipeline))
        {
            ReportPaint("failed to build paint pipeline for a channel format");
            vkDestroyRenderPass(Implementation.Device, Built.RenderPass, nullptr);
            return nullptr;
        }
        Implementation.FormatObjects.push_back(Built);
        return &Implementation.FormatObjects.back();
    }

    // Construct a fresh framebuffer over a layer image view at the given extent. 📝 Built + destroyed per stamp (not cached): the
    //    layer views are minted / freed by LayerTextureAllocation on resize / remove, so a cached framebuffer keyed by view could
    //    outlive its attachment. One framebuffer create per stamp is cheap at stroke cadence and removes that stale-view hazard.
    VkFramebuffer ConstructFramebuffer(GpuPaintContextImplementation& Implementation,
                                       VkRenderPass                   RenderPass,
                                       VkImageView                    AttachmentView,
                                       uint32_t                       Width,
                                       uint32_t                       Height)
    {
        VkFramebufferCreateInfo FramebufferInformation = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        FramebufferInformation.renderPass      = RenderPass;
        FramebufferInformation.attachmentCount = 1;
        FramebufferInformation.pAttachments    = &AttachmentView;
        FramebufferInformation.width           = Width;
        FramebufferInformation.height          = Height;
        FramebufferInformation.layers          = 1;
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(Implementation.Device, &FramebufferInformation, nullptr, &Framebuffer) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return Framebuffer;
    }

    // Record one image memory barrier moving Image between the two layouts on the graphics queue's colour/fragment stages.
    void RecordImageBarrier(VkCommandBuffer CommandBuffer,
                            VkImage         Image,
                            VkImageLayout   OldLayout,
                            VkImageLayout   NewLayout,
                            VkAccessFlags   SourceAccess,
                            VkAccessFlags   DestinationAccess,
                            VkPipelineStageFlags SourceStage,
                            VkPipelineStageFlags DestinationStage)
    {
        VkImageMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        Barrier.oldLayout                       = OldLayout;
        Barrier.newLayout                       = NewLayout;
        Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image                           = Image;
        Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.baseMipLevel   = 0;
        Barrier.subresourceRange.levelCount     = 1;
        Barrier.subresourceRange.baseArrayLayer = 0;
        Barrier.subresourceRange.layerCount     = 1;
        Barrier.srcAccessMask                   = SourceAccess;
        Barrier.dstAccessMask                   = DestinationAccess;
        vkCmdPipelineBarrier(CommandBuffer, SourceStage, DestinationStage, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeGpuPaintContext(GpuPaintContext& Context, VulkanHost& Device, const char* ShaderDirectory)
{
    Context.OpaqueImplementation = nullptr;
    Context.InitializeEnabled    = false;
    if (!Device.Device || !ShaderDirectory) return false;

    GpuPaintContextImplementation* Implementation = new GpuPaintContextImplementation();
    // 📝 The paint stamp writes a colour attachment, so it must run on the GRAPHICS queue/family (unlike the bake compute path).
    Implementation->PhysicalDevice   = Device.PhysicalDevice;
    Implementation->Device           = Device.Device;
    Implementation->Queue            = Device.GraphicsQueue;
    Implementation->QueueFamilyIndex = Device.GraphicsQueueFamily;
    Context.OpaqueImplementation = Implementation;

    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInformation.queueFamilyIndex = Implementation->QueueFamilyIndex;
    if (vkCreateCommandPool(Implementation->Device, &PoolInformation, nullptr, &Implementation->CommandPool) != VK_SUCCESS)
    {
        ReportPaint("failed to create transient command pool");
        FinalizeGpuPaintContext(Context, Device);
        return false;
    }

    std::vector<char> VertexSpirV;
    std::vector<char> FragmentSpirV;
    const std::string VertexPath   = std::string(ShaderDirectory) + "/PaintUvRaster.vert.spv";
    const std::string FragmentPath = std::string(ShaderDirectory) + "/PaintUvRaster.frag.spv";
    if (!LoadFileBytes(VertexPath.c_str(), VertexSpirV) || !LoadFileBytes(FragmentPath.c_str(), FragmentSpirV))
    {
        ReportPaint("missing PaintUvRaster SPIR-V");
        FinalizeGpuPaintContext(Context, Device);
        return false;
    }
    Implementation->VertexModule   = ConstructShaderModule(Implementation->Device, VertexSpirV);
    Implementation->FragmentModule = ConstructShaderModule(Implementation->Device, FragmentSpirV);
    if (!Implementation->VertexModule || !Implementation->FragmentModule)
    {
        ReportPaint("failed to create PaintUvRaster shader modules");
        FinalizeGpuPaintContext(Context, Device);
        return false;
    }

    if (!ConstructPipelineLayout(*Implementation))
    {
        ReportPaint("failed to create paint pipeline layout");
        FinalizeGpuPaintContext(Context, Device);
        return false;
    }

    // 📝 Render passes + pipelines are built lazily per target VkFormat on first stamp (ResolveFormatObjects) — nothing format-
    //    specific is created here. Warm the Albedo (RGBA8) path up front so the common first stroke never pays the build inline.
    if (ResolveFormatObjects(*Implementation, VK_FORMAT_R8G8B8A8_UNORM) == nullptr)
    {
        ReportPaint("failed to warm the Albedo paint pipeline");
        FinalizeGpuPaintContext(Context, Device);
        return false;
    }

    Context.InitializeEnabled = true;
    return true;
}

bool RecordPaintStamp(GpuPaintContext& Context, VulkanHost& Device, const PaintStampInputs& Inputs)
{
    (void)Device;
    if (!Context.OpaqueImplementation || !Context.InitializeEnabled)
    {
        ReportPaint("RecordPaintStamp declined: context not initialized");
        return false;
    }
    GpuPaintContextImplementation* Implementation = static_cast<GpuPaintContextImplementation*>(Context.OpaqueImplementation);

    VkImage       TargetImage  = reinterpret_cast<VkImage>(Inputs.TargetImage);
    VkImageView   TargetView   = reinterpret_cast<VkImageView>(Inputs.TargetView);
    VkBuffer      VertexBuffer = reinterpret_cast<VkBuffer>(Inputs.VertexBuffer);
    VkBuffer      IndexBuffer  = reinterpret_cast<VkBuffer>(Inputs.IndexBuffer);
    if (TargetImage == VK_NULL_HANDLE || TargetView == VK_NULL_HANDLE || VertexBuffer == VK_NULL_HANDLE ||
        IndexBuffer == VK_NULL_HANDLE || Inputs.IndexCount == 0 ||
        Inputs.TextureExtent[0] == 0 || Inputs.TextureExtent[1] == 0)
    {
        fprintf(stderr, "[paint] RecordPaintStamp declined: null input — img=%p view=%p vb=%p ib=%p idx=%u tex=%ux%u\n",
                (void*)TargetImage, (void*)TargetView, (void*)VertexBuffer, (void*)IndexBuffer,
                Inputs.IndexCount, Inputs.TextureExtent[0], Inputs.TextureExtent[1]);
        return false;
    }

    const PaintFormatObjects* Objects = ResolveFormatObjects(*Implementation, static_cast<VkFormat>(Inputs.TargetFormatCode));
    if (Objects == nullptr) { ReportPaint("no paint pipeline for the target channel format"); return false; }

    VkFramebuffer Framebuffer = ConstructFramebuffer(*Implementation, Objects->RenderPass, TargetView,
                                                     Inputs.TextureExtent[0], Inputs.TextureExtent[1]);
    if (Framebuffer == VK_NULL_HANDLE) { ReportPaint("failed to construct paint framebuffer"); return false; }

    VkCommandBufferAllocateInfo CommandInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandInformation.commandPool        = Implementation->CommandPool;
    CommandInformation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandInformation.commandBufferCount = 1;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Implementation->Device, &CommandInformation, &CommandBuffer) != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(CommandBuffer, &BeginInformation);

    // (1) Move the layer image SHADER_READ_ONLY -> COLOUR_ATTACHMENT so the render pass may write it.
    RecordImageBarrier(CommandBuffer, TargetImage,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    // (2) Begin the paint pass (LOAD preserves prior stamps), bind pipeline + geometry, push the brush, draw the model in UV space.
    VkRenderPassBeginInfo RenderPassBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    RenderPassBegin.renderPass        = Objects->RenderPass;
    RenderPassBegin.framebuffer       = Framebuffer;
    RenderPassBegin.renderArea.offset = { 0, 0 };
    RenderPassBegin.renderArea.extent = { Inputs.TextureExtent[0], Inputs.TextureExtent[1] };
    RenderPassBegin.clearValueCount   = 0;
    vkCmdBeginRenderPass(CommandBuffer, &RenderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport ViewportRect = {};
    ViewportRect.x        = 0.0f;
    ViewportRect.y        = 0.0f;
    ViewportRect.width    = (float)Inputs.TextureExtent[0];
    ViewportRect.height   = (float)Inputs.TextureExtent[1];
    ViewportRect.minDepth = 0.0f;
    ViewportRect.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRect);

    VkRect2D ScissorRect = {};
    ScissorRect.offset = { 0, 0 };
    ScissorRect.extent = { Inputs.TextureExtent[0], Inputs.TextureExtent[1] };
    vkCmdSetScissor(CommandBuffer, 0, 1, &ScissorRect);

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Objects->Pipeline);

    PaintStampPush Push = {};
    std::memcpy(Push.ModelViewProjection, Inputs.ModelViewProjection, sizeof(Push.ModelViewProjection));
    Push.BrushCentre[0] = Inputs.BrushCentre[0];
    Push.BrushCentre[1] = Inputs.BrushCentre[1];
    Push.BrushRadius    = Inputs.BrushRadius;
    Push.Hardness       = Inputs.Hardness;
    Push.Opacity        = Inputs.Opacity;
    Push.Flow           = Inputs.Flow;
    std::memcpy(Push.BrushColour, Inputs.TargetColour, sizeof(Push.BrushColour));
    Push.TargetExtent[0] = Inputs.TargetExtent[0];
    Push.TargetExtent[1] = Inputs.TargetExtent[1];
    vkCmdPushConstants(CommandBuffer, Implementation->PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PaintStampPush), &Push);

    const VkDeviceSize VertexOffset = 0;
    vkCmdBindVertexBuffers(CommandBuffer, 0, 1, &VertexBuffer, &VertexOffset);
    vkCmdBindIndexBuffer(CommandBuffer, IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(CommandBuffer, Inputs.IndexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(CommandBuffer);

    // (3) Move the layer image back to SHADER_READ_ONLY so the surface pass samples it this frame.
    RecordImageBarrier(CommandBuffer, TargetImage,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(CommandBuffer);

    VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence Fence = VK_NULL_HANDLE;
    vkCreateFence(Implementation->Device, &FenceInformation, nullptr, &Fence);

    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    SubmitInformation.commandBufferCount = 1;
    SubmitInformation.pCommandBuffers    = &CommandBuffer;
    bool StampEnabled = true;
    if (vkQueueSubmit(Implementation->Queue, 1, &SubmitInformation, Fence) != VK_SUCCESS)
    {
        ReportPaint("paint stamp submit failed");
        StampEnabled = false;
    }
    else
    {
        vkWaitForFences(Implementation->Device, 1, &Fence, VK_TRUE, UINT64_MAX);
    }

    vkDestroyFence(Implementation->Device, Fence, nullptr);
    vkDestroyFramebuffer(Implementation->Device, Framebuffer, nullptr);
    vkFreeCommandBuffers(Implementation->Device, Implementation->CommandPool, 1, &CommandBuffer);
    return StampEnabled;
}

void FinalizeGpuPaintContext(GpuPaintContext& Context, VulkanHost& Device)
{
    (void)Device;
    if (!Context.OpaqueImplementation) return;
    GpuPaintContextImplementation* Implementation = static_cast<GpuPaintContextImplementation*>(Context.OpaqueImplementation);

    if (Implementation->Device) vkDeviceWaitIdle(Implementation->Device);
    VkDevice Dev = Implementation->Device;

    // 📝 Framebuffers are built + destroyed per stamp (see ConstructFramebuffer), so none outlive a RecordPaintStamp call —
    //    release the per-format render passes + pipelines, then the shared layout / modules / pool.
    for (PaintFormatObjects& Objects : Implementation->FormatObjects)
    {
        if (Objects.Pipeline)   vkDestroyPipeline(Dev, Objects.Pipeline, nullptr);
        if (Objects.RenderPass) vkDestroyRenderPass(Dev, Objects.RenderPass, nullptr);
    }
    Implementation->FormatObjects.clear();
    if (Implementation->PipelineLayout) vkDestroyPipelineLayout(Dev, Implementation->PipelineLayout, nullptr);
    if (Implementation->FragmentModule) vkDestroyShaderModule(Dev, Implementation->FragmentModule, nullptr);
    if (Implementation->VertexModule)   vkDestroyShaderModule(Dev, Implementation->VertexModule, nullptr);
    if (Implementation->CommandPool)    vkDestroyCommandPool(Dev, Implementation->CommandPool, nullptr);

    delete Implementation;
    Context.OpaqueImplementation = nullptr;
    Context.InitializeEnabled    = false;
}

} // namespace Frontier
