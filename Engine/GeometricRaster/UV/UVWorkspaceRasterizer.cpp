/*============================================================================================================================================
                                                        UVWORKSPACERASTERIZER.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVWorkspaceRasterizer driving Vulkan .slang MRT pipelines, O(1) GPU selection, and 3D/2D GPU picking

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

struct alignas(16) GPUSurfaceVertex
{
    float    PosAndU[4];
    float    NormalAndV[4];
    float    AlbedoAndMetal[4];
    float    ShadingParams[4];
    uint32_t NetworkPickIDs[4];
    float    Barycentric[4];
};

struct alignas(16) GPUUVCanvasVertex
{
    float    UVAndBaryXY[4];
    float    BaryZAndStats[4];
    uint32_t NetworkPickIDs[4];
};

struct alignas(16) GPUViewport3DUniforms
{
    float    ViewProjection[4][4];
    float    CameraPosAndMode[4];
    float    CameraRightAndWire[4];
    float    CameraUpAndSeam[4];
    float    CameraForwardAndLayer[4];
    uint32_t PickHighlightIDs[4];
};

struct alignas(16) GPUCanvas2DUniforms
{
    float    CanvasMinMaxUV[4];
    uint32_t OverlayAndPick[4];
};

struct alignas(16) GPUGizmoUniforms
{
    float    ViewProjection[4][4];
    uint32_t HoverAndSpace[4];
};

struct alignas(16) GPUSelectionParams
{
    uint32_t FaceCount;
    uint32_t OperationCode;
    uint32_t TargetValue;
    uint32_t AdditiveMask;
};

struct alignas(16) GPUFaceMetricInput
{
    float Pos0AndU0[4];
    float Pos1AndV0[4];
    float Pos2AndU1[4];
    float UVRest[4];
    float UVBounds[4];
};

struct alignas(16) GPUFaceMetricOutput
{
    float    L2Stretch;
    float    SignedAreaUV;
    uint32_t StatusFlags;
    uint32_t UDIMNumber;
};

struct alignas(16) GPUMetricDispatchParams
{
    uint32_t FaceCount;
    float    ReferenceScale;
    uint32_t CheckOverlaps;
    uint32_t ReservedPad;
};

void ComputeOrbitBasis(
    const CameraOrbitState3D& Cam,
    Mathematics::Axis3D&      OutEye,
    Mathematics::Axis3D&      OutRight,
    Mathematics::Axis3D&      OutUp,
    Mathematics::Axis3D&      OutForward) noexcept
{
    const float CosP = std::cos(Cam.PitchRadians);
    const float SinP = std::sin(Cam.PitchRadians);
    const float CosY = std::cos(Cam.YawRadians);
    const float SinY = std::sin(Cam.YawRadians);

    const Mathematics::Axis3D Offset {
        Cam.DistanceMeters * CosP * SinY,
        Cam.DistanceMeters * SinP,
        Cam.DistanceMeters * CosP * CosY,
    };
    OutEye     = Cam.TargetPivot + Offset;
    OutForward = Mathematics::NormalizeAxis(Cam.TargetPivot - OutEye, { 0.0f, 0.0f, -1.0f });
    OutRight   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutForward, { 0.0f, 1.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
    OutUp      = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutRight, OutForward), { 0.0f, 1.0f, 0.0f });
}

// Row-major Vulkan clip matrix (Y-flipped for Vulkan NDC, Z in [0, 1])
void BuildRowMajorViewProjection(
    const CameraOrbitState3D& Cam,
    float                     AspectRatio,
    float                     OutVP[4][4]) noexcept
{
    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeOrbitBasis(Cam, Eye, Right, Up, Fwd);

    const float TanHalfFov = std::tan(Cam.VerticalFovDeg * Constants::DegToRadFloat * 0.5f);
    const float ScaleX     = 1.0f / std::max(AspectRatio * TanHalfFov, 1.0e-5f);
    const float ScaleY     = -1.0f / std::max(TanHalfFov, 1.0e-5f); // Flip Y for Vulkan top-down clip space
    const float NearZ      = 0.08f;
    const float FarZ       = 60.0f;
    const float ZRange     = FarZ / (NearZ - FarZ);

    // View matrix rows: R, U, -F
    const float V[4][4] = {
        { Right.X, Right.Y, Right.Z, -Mathematics::EvaluateDot(Right, Eye) },
        { Up.X,    Up.Y,    Up.Z,    -Mathematics::EvaluateDot(Up, Eye)    },
        { -Fwd.X,  -Fwd.Y,  -Fwd.Z,   Mathematics::EvaluateDot(Fwd, Eye)   },
        { 0.0f,    0.0f,    0.0f,     1.0f                                 },
    };
    const float P[4][4] = {
        { ScaleX, 0.0f,   0.0f,    0.0f            },
        { 0.0f,   ScaleY, 0.0f,    0.0f            },
        { 0.0f,   0.0f,   ZRange,  NearZ * ZRange  },
        { 0.0f,   0.0f,  -1.0f,    0.0f            },
    };

    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            OutVP[Row][Col] =
                P[Row][0] * V[0][Col] +
                P[Row][1] * V[1][Col] +
                P[Row][2] * V[2][Col] +
                P[Row][3] * V[3][Col];
        }
    }
}

void BuildRowMajorOrtho2D(
    Mathematics::UVAxes MinUV,
    Mathematics::UVAxes MaxUV,
    float               OutOrtho[4][4]) noexcept
{
    const float SpanU = std::max(MaxUV.U - MinUV.U, 1.0e-5f);
    const float SpanV = std::max(MaxUV.V - MinUV.V, 1.0e-5f);
    std::memset(OutOrtho, 0, sizeof(float) * 16u);
    OutOrtho[0][0] =  2.0f / SpanU;
    OutOrtho[0][3] = -1.0f - (2.0f * MinUV.U / SpanU);
    OutOrtho[1][1] = -2.0f / SpanV;
    OutOrtho[1][3] =  1.0f + (2.0f * MinUV.V / SpanV);
    OutOrtho[2][2] =  0.1f;
    OutOrtho[2][3] =  0.5f;
    OutOrtho[3][3] =  1.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

UVWorkspaceRasterizer::~UVWorkspaceRasterizer()
{
    TeardownRasterizer();
}

bool UVWorkspaceRasterizer::InitializeRasterizer(
    VulkanDeviceContext*         BoundContext,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownRasterizer();
    if (BoundContext == nullptr || !BoundContext->Ready())
    {
        return false;
    }

    Context = BoundContext;
    if (!ComputeEncoder.InitializeEncoder(Context, 256u) ||
        !TransferRegister.InitializeRegister(Context, 16ull * 1024ull * 1024ull))
    {
        TeardownRasterizer();
        return false;
    }

    if (!PickQuery.InitializePickQuery(
            Context,
            &ComputeEncoder,
            &TransferRegister,
            CompiledShaderDirectory / "ViewportPickReduction.comp.spv"))
    {
        TeardownRasterizer();
        return false;
    }

    const std::array<VkDescriptorType, 3u> SelBindings {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    SelectionComputeProgram = ComputeEncoder.RegisterProgram(CompiledShaderDirectory / "SelectionMaskCompute.comp.spv", SelBindings);

    const std::array<VkDescriptorType, 4u> StretchBindings {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    StretchComputeProgram = ComputeEncoder.RegisterProgram(CompiledShaderDirectory / "UVDistortionInspection.comp.spv", StretchBindings);

    if (SelectionComputeProgram == UINT32_MAX || StretchComputeProgram == UINT32_MAX)
    {
        TeardownRasterizer();
        return false;
    }

    // Create Dual-Attachment MRT + Depth RenderPass (Attachment 0: RGBA8, Attachment 1: R32G32B32A32_UINT, Attachment 2: D32_SFLOAT)
    std::array<VkAttachmentDescription, 3u> Attachments {};
    Attachments[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    Attachments[1].format         = VK_FORMAT_R32G32B32A32_UINT;
    Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    Attachments[2].format         = VK_FORMAT_D32_SFLOAT;
    Attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const std::array<VkAttachmentReference, 2u> ColorRefs {{
        { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 1u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    }};
    VkAttachmentReference DepthRef { 2u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription Subpass {};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = static_cast<uint32_t>(ColorRefs.size());
    Subpass.pColorAttachments       = ColorRefs.data();
    Subpass.pDepthStencilAttachment = &DepthRef;

    VkRenderPassCreateInfo PassInfo {};
    PassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    PassInfo.attachmentCount = static_cast<uint32_t>(Attachments.size());
    PassInfo.pAttachments    = Attachments.data();
    PassInfo.subpassCount    = 1u;
    PassInfo.pSubpasses      = &Subpass;

    if (vkCreateRenderPass(Context->AccessDevice(), &PassInfo, nullptr, &DualTargetRenderPass) != VK_SUCCESS)
    {
        TeardownRasterizer();
        return false;
    }

    const std::array<VkDescriptorPoolSize, 2u> GfxPoolSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256u },
    }};
    VkDescriptorPoolCreateInfo PoolInfo {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 128u;
    PoolInfo.poolSizeCount = static_cast<uint32_t>(GfxPoolSizes.size());
    PoolInfo.pPoolSizes    = GfxPoolSizes.data();
    if (vkCreateDescriptorPool(Context->AccessDevice(), &PoolInfo, nullptr, &GraphicsDescriptorPool) != VK_SUCCESS)
    {
        TeardownRasterizer();
        return false;
    }

    const std::array<VkDescriptorType, 3u> SurfaceSetTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const std::array<VkDescriptorType, 2u> GizmoSetTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};

    if (!CreateGraphicsPipeline(
            CompiledShaderDirectory / "SurfaceShading.vert.spv",
            CompiledShaderDirectory / "SurfaceShading.frag.spv",
            SurfaceSetTypes,
            true,
            SurfacePipeline) ||
        !CreateGraphicsPipeline(
            CompiledShaderDirectory / "UVTileRasterization.vert.spv",
            CompiledShaderDirectory / "UVTileRasterization.frag.spv",
            SurfaceSetTypes,
            false,
            UVCanvasPipeline) ||
        !CreateGraphicsPipeline(
            CompiledShaderDirectory / "GizmoRasterization.vert.spv",
            CompiledShaderDirectory / "GizmoRasterization.frag.spv",
            GizmoSetTypes,
            false,
            GizmoDepthOffPipeline))
    {
        TeardownRasterizer();
        return false;
    }

    Viewport3DUniformBuffer = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUViewport3DUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Canvas2DUniformBuffer   = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUCanvas2DUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Gizmo3DUniformBuffer    = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUGizmoUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Gizmo2DUniformBuffer    = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUGizmoUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    SelectionUniformBuffer  = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUSelectionParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    StretchUniformBuffer    = ComputeEncoder.AllocateHostWritableBuffer(sizeof(GPUMetricDispatchParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    StretchCountersBuffer   = ComputeEncoder.AllocateHostWritableBuffer(16u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    return true;
}

void UVWorkspaceRasterizer::TeardownRasterizer() noexcept
{
    if (Context != nullptr && Context->Ready())
    {
        VkDevice Dev = Context->AccessDevice();
        vkDeviceWaitIdle(Dev);

        ReleaseFramebufferTarget(Target3D);
        ReleaseFramebufferTarget(Target2D);

        DestroyGraphicsPipeline(SurfacePipeline);
        DestroyGraphicsPipeline(UVCanvasPipeline);
        DestroyGraphicsPipeline(GizmoDepthOffPipeline);

        Context->ReleaseBuffer(SurfaceVertexBuffer);
        Context->ReleaseBuffer(UVCanvasVertexBuffer);
        Context->ReleaseBuffer(Gizmo3DVertexBuffer);
        Context->ReleaseBuffer(Gizmo2DVertexBuffer);
        Context->ReleaseBuffer(FacePackedAttrBuffer);
        Context->ReleaseBuffer(SelectionBitmaskBuffer);
        Context->ReleaseBuffer(Viewport3DUniformBuffer);
        Context->ReleaseBuffer(Canvas2DUniformBuffer);
        Context->ReleaseBuffer(Gizmo3DUniformBuffer);
        Context->ReleaseBuffer(Gizmo2DUniformBuffer);
        Context->ReleaseBuffer(SelectionUniformBuffer);
        Context->ReleaseBuffer(StretchUniformBuffer);
        Context->ReleaseBuffer(StretchInputBuffer);
        Context->ReleaseBuffer(StretchOutputBuffer);
        Context->ReleaseBuffer(StretchCountersBuffer);

        if (GraphicsDescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Dev, GraphicsDescriptorPool, nullptr);
            GraphicsDescriptorPool = VK_NULL_HANDLE;
        }
        if (DualTargetRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(Dev, DualTargetRenderPass, nullptr);
            DualTargetRenderPass = VK_NULL_HANDLE;
        }
    }

    PickQuery.TeardownPickQuery();
    TransferRegister.TeardownRegister();
    ComputeEncoder.TeardownEncoder();
    Context = nullptr;
}

bool UVWorkspaceRasterizer::CreateGraphicsPipeline(
    const std::filesystem::path&      VertSpirv,
    const std::filesystem::path&      FragSpirv,
    std::span<const VkDescriptorType> BindingTypes,
    bool                              DepthTestAndWrite,
    GraphicsPipelineBundle&           OutBundle)
{
    VkDevice Dev         = Context->AccessDevice();
    OutBundle.VertModule = Context->LoadShaderModule(VertSpirv);
    OutBundle.FragModule = Context->LoadShaderModule(FragSpirv);
    if (OutBundle.VertModule == VK_NULL_HANDLE || OutBundle.FragModule == VK_NULL_HANDLE)
    {
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingTypes.size());
    for (std::size_t Idx = 0u; Idx < BindingTypes.size(); ++Idx)
    {
        Bindings[Idx].binding         = static_cast<uint32_t>(Idx);
        Bindings[Idx].descriptorType  = BindingTypes[Idx];
        Bindings[Idx].descriptorCount = 1u;
        Bindings[Idx].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo BindingInfo {};
    BindingInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    BindingInfo.bindingCount = static_cast<uint32_t>(Bindings.size());
    BindingInfo.pBindings    = Bindings.data();
    if (vkCreateDescriptorSetLayout(Dev, &BindingInfo, nullptr, &OutBundle.BindingLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo LayoutInfo {};
    LayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount = 1u;
    LayoutInfo.pSetLayouts    = &OutBundle.BindingLayout;
    if (vkCreatePipelineLayout(Dev, &LayoutInfo, nullptr, &OutBundle.PipelineLayout) != VK_SUCCESS)
    {
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2u> Stages {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = OutBundle.VertModule;
    Stages[0].pName  = "VertexEntry";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = OutBundle.FragModule;
    Stages[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo   VertexInput {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo PrimitiveInput {};
    PrimitiveInput.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    PrimitiveInput.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportState {};
    ViewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportState.viewportCount = 1u;
    ViewportState.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo Raster {};
    Raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.cullMode    = VK_CULL_MODE_NONE;
    Raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DepthStencil {};
    DepthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthStencil.depthTestEnable  = DepthTestAndWrite ? VK_TRUE : VK_FALSE;
    DepthStencil.depthWriteEnable = DepthTestAndWrite ? VK_TRUE : VK_FALSE;
    DepthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::array<VkPipelineColorBlendAttachmentState, 2u> BlendAtts {};
    BlendAtts[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[0].blendEnable    = VK_FALSE;
    BlendAtts[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    BlendAtts[1].blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo ColorBlend {};
    ColorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlend.attachmentCount = static_cast<uint32_t>(BlendAtts.size());
    ColorBlend.pAttachments    = BlendAtts.data();

    const std::array<VkDynamicState, 2u> DynStates {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
    VkPipelineDynamicStateCreateInfo DynInfo {};
    DynInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynInfo.dynamicStateCount = static_cast<uint32_t>(DynStates.size());
    DynInfo.pDynamicStates    = DynStates.data();

    VkGraphicsPipelineCreateInfo PipeInfo {};
    PipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeInfo.stageCount          = static_cast<uint32_t>(Stages.size());
    PipeInfo.pStages             = Stages.data();
    PipeInfo.pVertexInputState   = &VertexInput;
    PipeInfo.pInputAssemblyState = &PrimitiveInput;
    PipeInfo.pViewportState      = &ViewportState;
    PipeInfo.pRasterizationState = &Raster;
    PipeInfo.pMultisampleState   = &Multisample;
    PipeInfo.pDepthStencilState  = &DepthStencil;
    PipeInfo.pColorBlendState    = &ColorBlend;
    PipeInfo.pDynamicState       = &DynInfo;
    PipeInfo.layout              = OutBundle.PipelineLayout;
    PipeInfo.renderPass          = DualTargetRenderPass;
    PipeInfo.subpass             = 0u;

    return vkCreateGraphicsPipelines(Dev, VK_NULL_HANDLE, 1u, &PipeInfo, nullptr, &OutBundle.Pipeline) == VK_SUCCESS;
}

void UVWorkspaceRasterizer::DestroyGraphicsPipeline(GraphicsPipelineBundle& Bundle) noexcept
{
    if (Context == nullptr || !Context->Ready())
    {
        return;
    }
    VkDevice Dev = Context->AccessDevice();
    if (Bundle.Pipeline != VK_NULL_HANDLE)       { vkDestroyPipeline(Dev, Bundle.Pipeline, nullptr); Bundle.Pipeline = VK_NULL_HANDLE; }
    if (Bundle.PipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(Dev, Bundle.PipelineLayout, nullptr); Bundle.PipelineLayout = VK_NULL_HANDLE; }
    if (Bundle.BindingLayout != VK_NULL_HANDLE)      { vkDestroyDescriptorSetLayout(Dev, Bundle.BindingLayout, nullptr); Bundle.BindingLayout = VK_NULL_HANDLE; }
    if (Bundle.VertModule != VK_NULL_HANDLE)     { vkDestroyShaderModule(Dev, Bundle.VertModule, nullptr); Bundle.VertModule = VK_NULL_HANDLE; }
    if (Bundle.FragModule != VK_NULL_HANDLE)     { vkDestroyShaderModule(Dev, Bundle.FragModule, nullptr); Bundle.FragModule = VK_NULL_HANDLE; }
}

bool UVWorkspaceRasterizer::EnsureFramebufferTarget(MRTFramebufferTarget& Target, uint32_t WidthPx, uint32_t HeightPx)
{
    if (Target.Framebuffer != VK_NULL_HANDLE && Target.WidthPx == WidthPx && Target.HeightPx == HeightPx)
    {
        return true;
    }

    ReleaseFramebufferTarget(Target);

    Target.ColorImage = Context->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    Target.PickImage = Context->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    Target.DepthImage = Context->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    if (Target.ColorImage.ViewHandle == VK_NULL_HANDLE ||
        Target.PickImage.ViewHandle == VK_NULL_HANDLE ||
        Target.DepthImage.ViewHandle == VK_NULL_HANDLE)
    {
        ReleaseFramebufferTarget(Target);
        return false;
    }

    const std::array<VkImageView, 3u> Views {{
        Target.ColorImage.ViewHandle,
        Target.PickImage.ViewHandle,
        Target.DepthImage.ViewHandle,
    }};

    VkFramebufferCreateInfo FbInfo {};
    FbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbInfo.renderPass      = DualTargetRenderPass;
    FbInfo.attachmentCount = static_cast<uint32_t>(Views.size());
    FbInfo.pAttachments    = Views.data();
    FbInfo.width           = WidthPx;
    FbInfo.height          = HeightPx;
    FbInfo.layers          = 1u;

    if (vkCreateFramebuffer(Context->AccessDevice(), &FbInfo, nullptr, &Target.Framebuffer) != VK_SUCCESS)
    {
        ReleaseFramebufferTarget(Target);
        return false;
    }

    Target.WidthPx    = WidthPx;
    Target.HeightPx   = HeightPx;
    Target.PickLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void UVWorkspaceRasterizer::ReleaseFramebufferTarget(MRTFramebufferTarget& Target) noexcept
{
    if (Context != nullptr && Context->Ready())
    {
        if (Target.Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(Context->AccessDevice(), Target.Framebuffer, nullptr);
            Target.Framebuffer = VK_NULL_HANDLE;
        }
        Context->ReleaseImage2D(Target.ColorImage);
        Context->ReleaseImage2D(Target.PickImage);
        Context->ReleaseImage2D(Target.DepthImage);
    }
    Target.WidthPx  = 0u;
    Target.HeightPx = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      POLYGONNETWORK UPLOAD & GPU COMPUTE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool UVWorkspaceRasterizer::UploadPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             InstanceSlot)
{
    if (Context == nullptr || Network.Faces.empty())
    {
        return false;
    }

    UploadedFaceCount = static_cast<uint32_t>(Network.Faces.size());
    if (CachedFaceStretch.size() != UploadedFaceCount)
    {
        CachedFaceStretch.assign(UploadedFaceCount, 1.0f);
    }

    // 1. Pack FacePackedAttributes & SelectionBitmask
    std::vector<uint32_t> PackedFaceAttrs(UploadedFaceCount);
    for (uint32_t FaceIdx = 0u; FaceIdx < UploadedFaceCount; ++FaceIdx)
    {
        const uint32_t LayerOrd = Network.Faces[FaceIdx].PriorityLayerOrdinal;
        const uint32_t PartOrd  = Network.Faces[FaceIdx].PartGroupOrdinal;
        PackedFaceAttrs[FaceIdx] = (LayerOrd & 0xFFFFu) | ((PartOrd & 0xFFFFu) << 16u);
    }

    const uint32_t BitmaskWordCount = (UploadedFaceCount + 31u) / 32u;
    Context->ReleaseBuffer(FacePackedAttrBuffer);
    FacePackedAttrBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        PackedFaceAttrs.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<uint32_t>(FacePackedAttrBuffer, PackedFaceAttrs);

    if (SelectionBitmaskBuffer.ByteCapacity < BitmaskWordCount * sizeof(uint32_t))
    {
        Context->ReleaseBuffer(SelectionBitmaskBuffer);
        SelectionBitmaskBuffer = ComputeEncoder.AllocateHostWritableBuffer(
            std::max<uint64_t>(16ull, BitmaskWordCount * sizeof(uint32_t)),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::vector<uint32_t> ZeroWords(std::max(4u, BitmaskWordCount), 0u);
        ComputeEncoder.UploadSpanToBuffer<uint32_t>(SelectionBitmaskBuffer, ZeroWords);
    }

    // 2. Build 3D SurfaceVertexBuffer (starting with 2 ground-grid triangles at NetworkPickIDs.x = 0)
    std::vector<GPUSurfaceVertex> Verts3D;
    Verts3D.reserve(6u + UploadedFaceCount * 6u);

    constexpr float GridHalf = 4.2f;
    const std::array<Mathematics::Axis3D, 6u> GridQuad {{
        { -GridHalf, -0.02f, -GridHalf }, {  GridHalf, -0.02f, -GridHalf }, {  GridHalf, -0.02f,  GridHalf },
        { -GridHalf, -0.02f, -GridHalf }, {  GridHalf, -0.02f,  GridHalf }, { -GridHalf, -0.02f,  GridHalf },
    }};
    for (const Mathematics::Axis3D& GP : GridQuad)
    {
        GPUSurfaceVertex GV {};
        GV.PosAndU[0]        = GP.X; GV.PosAndU[1] = GP.Y; GV.PosAndU[2] = GP.Z;
        GV.NormalAndV[1]     = 1.0f;
        GV.NetworkPickIDs[0] = 0u; // Ground Grid marker
        GV.NetworkPickIDs[1] = GeometrySpace::InvalidNetworkIndex;
        Verts3D.push_back(GV);
    }
    GroundGridVertexCount = 6u;

    // 3. Build 2D UVCanvasVertexBuffer (starting with background quad at NetworkPickIDs.x = 0)
    std::vector<GPUUVCanvasVertex> Verts2D;
    Verts2D.reserve(6u + UploadedFaceCount * 6u);

    const std::array<Mathematics::UVAxes, 6u> BgQuad {{
        { -2.0f, -2.0f }, { 12.0f, -2.0f }, { 12.0f, 6.0f },
        { -2.0f, -2.0f }, { 12.0f,  6.0f }, { -2.0f, 6.0f },
    }};
    for (const Mathematics::UVAxes& BUV : BgQuad)
    {
        GPUUVCanvasVertex BV {};
        BV.UVAndBaryXY[0]    = BUV.U;
        BV.UVAndBaryXY[1]    = BUV.V;
        BV.NetworkPickIDs[0] = 0u;
        BV.NetworkPickIDs[1] = GeometrySpace::InvalidNetworkIndex;
        Verts2D.push_back(BV);
    }

    // Fan-triangulate each PolygonNetwork Face for both 3D and 2D streams
    for (uint32_t FaceIdx = 0u; FaceIdx < UploadedFaceCount; ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Network.Faces[FaceIdx];
        if (CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        std::vector<uint32_t> LoopEdges;
        LoopEdges.reserve(CurrentFace.CornerCount);
        uint32_t EdgeCur = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount && EdgeCur < Network.Edges.size(); ++Corner)
        {
            LoopEdges.push_back(EdgeCur);
            EdgeCur = Network.Edges[EdgeCur].NextEdgeIndex;
        }
        if (LoopEdges.size() < 3u)
        {
            continue;
        }

        const float SignedUV = Network.EvaluateFaceSignedAreaUV(FaceIdx);
        const float Stretch  = FaceIdx < CachedFaceStretch.size() ? CachedFaceStretch[FaceIdx] : 1.0f;

        for (std::size_t Tri = 1u; Tri + 1u < LoopEdges.size(); ++Tri)
        {
            const std::array<uint32_t, 3u> TriEdges {{ LoopEdges[0u], LoopEdges[Tri], LoopEdges[Tri + 1u] }};
            const std::array<std::array<float, 3u>, 3u> Barys {{
                {{ 1.0f, 0.0f, 0.0f }},
                {{ 0.0f, 1.0f, 0.0f }},
                {{ 0.0f, 0.0f, 1.0f }},
            }};

            const bool Pin0  = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[0u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock);
            const bool Pin1  = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[1u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock);
            const bool Pin2  = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[2u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnablePinLock);
            const bool Seam0 = GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[1u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);
            const bool Seam1 = (Tri + 2u == LoopEdges.size()) &&
                               GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[2u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);
            const bool Seam2 = (Tri == 1u) &&
                               GeometrySpace::QueryEdgeAttributeEnabled(Network.Edges[TriEdges[0u]].AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);

            const uint32_t Packed2DFlags =
                (static_cast<uint32_t>(CurrentFace.PriorityLayerOrdinal) & 0xFFFFu) |
                (Pin0  ? (1u << 16u) : 0u) |
                (Pin1  ? (1u << 17u) : 0u) |
                (Pin2  ? (1u << 18u) : 0u) |
                (Seam0 ? (1u << 19u) : 0u) |
                (Seam1 ? (1u << 20u) : 0u) |
                (Seam2 ? (1u << 21u) : 0u);

            for (std::size_t Corner = 0u; Corner < 3u; ++Corner)
            {
                const GeometrySpace::DirectedEdge& DE = Network.Edges[TriEdges[Corner]];
                const GeometrySpace::Vertex&       VX = Network.Vertices[DE.OriginVertexIndex];
                const bool SeamBit = GeometrySpace::QueryEdgeAttributeEnabled(DE.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut);

                const uint32_t PackedFlags =
                    (static_cast<uint32_t>(CurrentFace.Shading.MatCapSlot) & 0xFFu) |
                    ((static_cast<uint32_t>(CurrentFace.PriorityLayerOrdinal) & 0xFFFFu) << 8u) |
                    (SeamBit ? (1u << 24u) : 0u);

                GPUSurfaceVertex SV {};
                SV.PosAndU[0]        = VX.Axes.X;
                SV.PosAndU[1]        = VX.Axes.Y;
                SV.PosAndU[2]        = VX.Axes.Z;
                SV.PosAndU[3]        = DE.ParameterUV.U;
                SV.NormalAndV[0]     = CurrentFace.GeometricNormal.X;
                SV.NormalAndV[1]     = CurrentFace.GeometricNormal.Y;
                SV.NormalAndV[2]     = CurrentFace.GeometricNormal.Z;
                SV.NormalAndV[3]     = DE.ParameterUV.V;
                SV.AlbedoAndMetal[0] = CurrentFace.Shading.AlbedoLinearRGB.X;
                SV.AlbedoAndMetal[1] = CurrentFace.Shading.AlbedoLinearRGB.Y;
                SV.AlbedoAndMetal[2] = CurrentFace.Shading.AlbedoLinearRGB.Z;
                SV.AlbedoAndMetal[3] = CurrentFace.Shading.Metallic;
                SV.ShadingParams[0]  = CurrentFace.Shading.Roughness;
                SV.ShadingParams[1]  = CurrentFace.Shading.Specular;
                SV.ShadingParams[2]  = CurrentFace.Shading.MatCapBlend;
                SV.ShadingParams[3]  = Stretch;
                SV.NetworkPickIDs[0] = std::max(1u, InstanceSlot);
                SV.NetworkPickIDs[1] = FaceIdx;
                SV.NetworkPickIDs[2] = TriEdges[Corner];
                SV.NetworkPickIDs[3] = PackedFlags;
                SV.Barycentric[0]    = Barys[Corner][0];
                SV.Barycentric[1]    = Barys[Corner][1];
                SV.Barycentric[2]    = Barys[Corner][2];
                Verts3D.push_back(SV);

                GPUUVCanvasVertex UVV {};
                UVV.UVAndBaryXY[0]   = DE.ParameterUV.U;
                UVV.UVAndBaryXY[1]   = DE.ParameterUV.V;
                UVV.UVAndBaryXY[2]   = Barys[Corner][0];
                UVV.UVAndBaryXY[3]   = Barys[Corner][1];
                UVV.BaryZAndStats[0] = Barys[Corner][2];
                UVV.BaryZAndStats[1] = Stretch;
                UVV.BaryZAndStats[2] = SignedUV;
                UVV.BaryZAndStats[3] = 0.0f;
                UVV.NetworkPickIDs[0] = std::max(1u, InstanceSlot);
                UVV.NetworkPickIDs[1] = FaceIdx;
                UVV.NetworkPickIDs[2] = TriEdges[Corner];
                UVV.NetworkPickIDs[3] = Packed2DFlags;
                Verts2D.push_back(UVV);
            }
        }
    }

    SurfaceVertexCount  = static_cast<uint32_t>(Verts3D.size());
    UVCanvasVertexCount = static_cast<uint32_t>(Verts2D.size());

    Context->ReleaseBuffer(SurfaceVertexBuffer);
    SurfaceVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        Verts3D.size() * sizeof(GPUSurfaceVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<GPUSurfaceVertex>(SurfaceVertexBuffer, Verts3D);

    Context->ReleaseBuffer(UVCanvasVertexBuffer);
    UVCanvasVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(
        Verts2D.size() * sizeof(GPUUVCanvasVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<GPUUVCanvasVertex>(UVCanvasVertexBuffer, Verts2D);

    return true;
}

bool UVWorkspaceRasterizer::DispatchGPUSelection(
    GPUSelectionOperation Operation,
    uint32_t              TargetValue,
    bool                  AdditiveMask)
{
    if (Context == nullptr || UploadedFaceCount == 0u || SelectionBitmaskBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    GPUSelectionParams Params {};
    Params.FaceCount     = UploadedFaceCount;
    Params.OperationCode = static_cast<uint32_t>(Operation);
    Params.TargetValue   = TargetValue;
    Params.AdditiveMask  = AdditiveMask ? 1u : 0u;

    const std::array<GPUSelectionParams, 1u> ParamsArr {{ Params }};
    ComputeEncoder.UploadSpanToBuffer<GPUSelectionParams>(SelectionUniformBuffer, ParamsArr);

    const uint32_t WordCount = (UploadedFaceCount + 31u) / 32u;
    const uint32_t Groups    = (WordCount + 63u) / 64u;

    VkCommandBuffer Cmd = Context->BeginSingleTimeCommands();
    const std::array<ComputeBindingDescriptor, 3u> Bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SelectionUniformBuffer.BufferHandle, sizeof(GPUSelectionParams) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FacePackedAttrBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SelectionBitmaskBuffer.BufferHandle, VK_WHOLE_SIZE },
    }};
    (void)ComputeEncoder.EncodeComputeDispatch(Cmd, SelectionComputeProgram, Bindings, std::max(1u, Groups));
    const uint64_t Signal = Context->DispatchCommandBufferAsync(Cmd);
    (void)Context->AwaitTimelineValue(Signal);
    vkFreeCommandBuffers(Context->AccessDevice(), Context->AccessCommandPool(), 1u, &Cmd);
    return true;
}

void UVWorkspaceRasterizer::UploadSelectionBitmaskWords(std::span<const uint32_t> PackedWords)
{
    if (Context == nullptr || SelectionBitmaskBuffer.BufferHandle == VK_NULL_HANDLE || PackedWords.empty())
    {
        return;
    }
    const std::size_t MaxWords = static_cast<std::size_t>(SelectionBitmaskBuffer.ByteCapacity / sizeof(uint32_t));
    const std::size_t Count    = std::min(PackedWords.size(), MaxWords);
    ComputeEncoder.UploadSpanToBuffer<uint32_t>(SelectionBitmaskBuffer, PackedWords.subspan(0u, Count));
}

std::vector<uint32_t> UVWorkspaceRasterizer::ReadbackSelectionBitmaskWords()
{
    const uint32_t WordCount = (UploadedFaceCount + 31u) / 32u;
    std::vector<uint32_t> Output(WordCount, 0u);
    if (Context == nullptr || WordCount == 0u || SelectionBitmaskBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return Output;
    }

    VkCommandBuffer Cmd    = Context->BeginSingleTimeCommands();
    ReadbackTicket  Ticket = TransferRegister.EncodeBufferReadback(Cmd, SelectionBitmaskBuffer.BufferHandle, 0ull, WordCount * sizeof(uint32_t));
    const uint64_t  Signal = Context->DispatchCommandBufferAsync(Cmd);
    TransferRegister.BindTicketTimelineSignal(Ticket, Signal);
    (void)TransferRegister.AwaitAndCopyTyped<uint32_t>(Ticket, Output);
    vkFreeCommandBuffers(Context->AccessDevice(), Context->AccessCommandPool(), 1u, &Cmd);
    return Output;
}

GPUUVDistortionSummary UVWorkspaceRasterizer::EvaluateUVDistortionInspection(
    const GeometrySpace::PolygonNetwork& Network,
    bool                                 CheckOverlaps)
{
    GPUUVDistortionSummary Summary {};
    const uint32_t FaceCount = static_cast<uint32_t>(Network.Faces.size());
    if (Context == nullptr || FaceCount == 0u)
    {
        return Summary;
    }

    std::vector<GPUFaceMetricInput> Inputs(FaceCount);
    double Total3D = 0.0;
    double TotalUV = 0.0;

    for (uint32_t FaceIdx = 0u; FaceIdx < FaceCount; ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Network.Faces[FaceIdx];
        const uint32_t E0 = CurrentFace.BoundaryLoopEdgeIndex;
        const uint32_t E1 = Network.Edges[E0].NextEdgeIndex;
        const uint32_t E2 = Network.Edges[E1].NextEdgeIndex;

        const Mathematics::Axis3D P0 = Network.Vertices[Network.Edges[E0].OriginVertexIndex].Axes;
        const Mathematics::Axis3D P1 = Network.Vertices[Network.Edges[E1].OriginVertexIndex].Axes;
        const Mathematics::Axis3D P2 = Network.Vertices[Network.Edges[E2].OriginVertexIndex].Axes;

        const Mathematics::UVAxes T0 = Network.Edges[E0].ParameterUV;
        const Mathematics::UVAxes T1 = Network.Edges[E1].ParameterUV;
        const Mathematics::UVAxes T2 = Network.Edges[E2].ParameterUV;

        float MinU = T0.U, MaxU = T0.U, MinV = T0.V, MaxV = T0.V;
        uint32_t Walk = E0;
        for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount; ++Corner)
        {
            const Mathematics::UVAxes UV = Network.Edges[Walk].ParameterUV;
            MinU = std::min(MinU, UV.U); MaxU = std::max(MaxU, UV.U);
            MinV = std::min(MinV, UV.V); MaxV = std::max(MaxV, UV.V);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }

        const float Area3D = Network.EvaluateFaceArea3D(FaceIdx);
        const float AreaUV = std::abs(Network.EvaluateFaceSignedAreaUV(FaceIdx));
        Total3D += Area3D;
        TotalUV += AreaUV;

        GPUFaceMetricInput& In = Inputs[FaceIdx];
        In.Pos0AndU0[0] = P0.X; In.Pos0AndU0[1] = P0.Y; In.Pos0AndU0[2] = P0.Z; In.Pos0AndU0[3] = T0.U;
        In.Pos1AndV0[0] = P1.X; In.Pos1AndV0[1] = P1.Y; In.Pos1AndV0[2] = P1.Z; In.Pos1AndV0[3] = T0.V;
        In.Pos2AndU1[0] = P2.X; In.Pos2AndU1[1] = P2.Y; In.Pos2AndU1[2] = P2.Z; In.Pos2AndU1[3] = T1.U;
        In.UVRest[0]    = T1.V; In.UVRest[1]    = T2.U; In.UVRest[2]    = T2.V; In.UVRest[3]    = Area3D;
        In.UVBounds[0]  = MinU; In.UVBounds[1]  = MinV; In.UVBounds[2]  = MaxU; In.UVBounds[3]  = MaxV;
    }

    const float RefScale = Total3D > 1.0e-8 ? static_cast<float>(std::sqrt(TotalUV / Total3D)) : 1.0f;

    Context->ReleaseBuffer(StretchInputBuffer);
    Context->ReleaseBuffer(StretchOutputBuffer);
    StretchInputBuffer  = ComputeEncoder.AllocateHostWritableBuffer(Inputs.size() * sizeof(GPUFaceMetricInput), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    StretchOutputBuffer = ComputeEncoder.AllocateHostWritableBuffer(FaceCount * sizeof(GPUFaceMetricOutput), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ComputeEncoder.UploadSpanToBuffer<GPUFaceMetricInput>(StretchInputBuffer, Inputs);

    GPUMetricDispatchParams Params { FaceCount, RefScale, CheckOverlaps ? 1u : 0u, 0u };
    const std::array<GPUMetricDispatchParams, 1u> ParamsSpan {{ Params }};
    ComputeEncoder.UploadSpanToBuffer<GPUMetricDispatchParams>(StretchUniformBuffer, ParamsSpan);

    const std::array<uint32_t, 4u> ZeroCounters {{ 0u, 0u, 0u, 0u }};
    ComputeEncoder.UploadSpanToBuffer<uint32_t>(StretchCountersBuffer, ZeroCounters);

    VkCommandBuffer Cmd = Context->BeginSingleTimeCommands();
    const std::array<ComputeBindingDescriptor, 4u> Bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, StretchUniformBuffer.BufferHandle, sizeof(GPUMetricDispatchParams) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StretchInputBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StretchOutputBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StretchCountersBuffer.BufferHandle, 16u },
    }};
    (void)ComputeEncoder.EncodeComputeDispatch(Cmd, StretchComputeProgram, Bindings, (FaceCount + 63u) / 64u);

    ReadbackTicket CountsTicket  = TransferRegister.EncodeBufferReadback(Cmd, StretchCountersBuffer.BufferHandle, 0ull, 12ull);
    ReadbackTicket OutputsTicket = TransferRegister.EncodeBufferReadback(Cmd, StretchOutputBuffer.BufferHandle, 0ull, FaceCount * sizeof(GPUFaceMetricOutput));
    const uint64_t Signal        = Context->DispatchCommandBufferAsync(Cmd);
    TransferRegister.BindTicketTimelineSignal(CountsTicket, Signal);
    TransferRegister.BindTicketTimelineSignal(OutputsTicket, Signal);

    std::array<uint32_t, 3u> RawCounts {{ 0u, 0u, 0u }};
    std::vector<GPUFaceMetricOutput> Outputs(FaceCount);
    (void)TransferRegister.AwaitAndCopyTyped<uint32_t>(CountsTicket, RawCounts);
    (void)TransferRegister.AwaitAndCopyTyped<GPUFaceMetricOutput>(OutputsTicket, Outputs);
    vkFreeCommandBuffers(Context->AccessDevice(), Context->AccessCommandPool(), 1u, &Cmd);

    Summary.FlippedFaceCount = RawCounts[0u];
    Summary.OverlapFaceCount = RawCounts[1u];
    Summary.MeanL2Stretch    = static_cast<float>(RawCounts[2u]) / (1000.0f * static_cast<float>(FaceCount));
    Summary.PerFaceStretch.resize(FaceCount);
    for (uint32_t Idx = 0u; Idx < FaceCount; ++Idx)
    {
        Summary.PerFaceStretch[Idx] = Outputs[Idx].L2Stretch;
    }
    CachedFaceStretch = Summary.PerFaceStretch;
    return Summary;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3D & 2D VIEWPORT MRT RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

bool UVWorkspaceRasterizer::Rasterize3DViewport(
    uint32_t                       WidthPx,
    uint32_t                       HeightPx,
    const Viewport3DRenderOptions& Options,
    const Editor::TransformGizmo*  OptionalGizmo3D,
    std::vector<uint8_t>*          OptionalOutRGBA)
{
    if (Context == nullptr || SurfaceVertexCount == 0u || !EnsureFramebufferTarget(Target3D, WidthPx, HeightPx))
    {
        return false;
    }

    vkResetDescriptorPool(Context->AccessDevice(), GraphicsDescriptorPool, 0u);

    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeOrbitBasis(Options.Camera, Eye, Right, Up, Fwd);

    GPUViewport3DUniforms Uniforms {};
    BuildRowMajorViewProjection(Options.Camera, static_cast<float>(WidthPx) / static_cast<float>(HeightPx), Uniforms.ViewProjection);
    Uniforms.CameraPosAndMode[0]      = Eye.X;
    Uniforms.CameraPosAndMode[1]      = Eye.Y;
    Uniforms.CameraPosAndMode[2]      = Eye.Z;
    Uniforms.CameraPosAndMode[3]      = static_cast<float>(static_cast<uint32_t>(Options.DisplayMode));
    Uniforms.CameraRightAndWire[0]    = Right.X;
    Uniforms.CameraRightAndWire[1]    = Right.Y;
    Uniforms.CameraRightAndWire[2]    = Right.Z;
    Uniforms.CameraRightAndWire[3]    = Options.WireframeEnabled ? 1.0f : 0.0f;
    Uniforms.CameraUpAndSeam[0]       = Up.X;
    Uniforms.CameraUpAndSeam[1]       = Up.Y;
    Uniforms.CameraUpAndSeam[2]       = Up.Z;
    Uniforms.CameraUpAndSeam[3]       = Options.SeamOverlayEnabled ? 1.0f : 0.0f;
    Uniforms.CameraForwardAndLayer[0] = Fwd.X;
    Uniforms.CameraForwardAndLayer[1] = Fwd.Y;
    Uniforms.CameraForwardAndLayer[2] = Fwd.Z;
    Uniforms.CameraForwardAndLayer[3] = static_cast<float>(Options.ActiveLayerOrdinal);
    Uniforms.PickHighlightIDs[0]      = Options.HoveredFaceIndex;

    const std::array<GPUViewport3DUniforms, 1u> UniSpan {{ Uniforms }};
    ComputeEncoder.UploadSpanToBuffer<GPUViewport3DUniforms>(Viewport3DUniformBuffer, UniSpan);

    uint32_t GizmoVertCount = 0u;
    if (Options.RenderGizmo && OptionalGizmo3D != nullptr)
    {
        const std::vector<Editor::GizmoGPUVertex> GizmoVerts = OptionalGizmo3D->BuildTriangleStream(Right, Up, Fwd);
        GizmoVertCount = static_cast<uint32_t>(GizmoVerts.size());
        if (GizmoVertCount > 0u)
        {
            const uint64_t ReqBytes = GizmoVerts.size() * sizeof(Editor::GizmoGPUVertex);
            if (Gizmo3DVertexBuffer.ByteCapacity < ReqBytes)
            {
                Context->ReleaseBuffer(Gizmo3DVertexBuffer);
                Gizmo3DVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(ReqBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }
            ComputeEncoder.UploadSpanToBuffer<Editor::GizmoGPUVertex>(Gizmo3DVertexBuffer, GizmoVerts);

            GPUGizmoUniforms GizUni {};
            std::memcpy(GizUni.ViewProjection, Uniforms.ViewProjection, sizeof(Uniforms.ViewProjection));
            GizUni.HoverAndSpace[0] = static_cast<uint32_t>(OptionalGizmo3D->QueryHoveredHandle());
            GizUni.HoverAndSpace[1] = static_cast<uint32_t>(OptionalGizmo3D->QueryActiveHandle());
            GizUni.HoverAndSpace[2] = 0u;
            const std::array<GPUGizmoUniforms, 1u> GizSpan {{ GizUni }};
            ComputeEncoder.UploadSpanToBuffer<GPUGizmoUniforms>(Gizmo3DUniformBuffer, GizSpan);
        }
    }

    VkDevice Dev = Context->AccessDevice();

    // Allocate descriptor sets
    VkDescriptorSet SurfaceSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo SurfAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorPool, 1u, &SurfacePipeline.BindingLayout };
    vkAllocateDescriptorSets(Dev, &SurfAlloc, &SurfaceSet);

    const std::array<VkDescriptorBufferInfo, 3u> SurfBufs {{
        { Viewport3DUniformBuffer.BufferHandle, 0u, sizeof(GPUViewport3DUniforms) },
        { SurfaceVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        { SelectionBitmaskBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
    }};
    std::array<VkWriteDescriptorSet, 3u> SurfWrites {};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        SurfWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        SurfWrites[Idx].dstSet          = SurfaceSet;
        SurfWrites[Idx].dstBinding      = Idx;
        SurfWrites[Idx].descriptorCount = 1u;
        SurfWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        SurfWrites[Idx].pBufferInfo     = &SurfBufs[Idx];
    }
    vkUpdateDescriptorSets(Dev, 3u, SurfWrites.data(), 0u, nullptr);

    VkDescriptorSet GizmoSet = VK_NULL_HANDLE;
    if (GizmoVertCount > 0u)
    {
        VkDescriptorSetAllocateInfo GizAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorPool, 1u, &GizmoDepthOffPipeline.BindingLayout };
        vkAllocateDescriptorSets(Dev, &GizAlloc, &GizmoSet);
        const std::array<VkDescriptorBufferInfo, 2u> GizBufs {{
            { Gizmo3DUniformBuffer.BufferHandle, 0u, sizeof(GPUGizmoUniforms) },
            { Gizmo3DVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        }};
        std::array<VkWriteDescriptorSet, 2u> GizWrites {};
        for (uint32_t Idx = 0u; Idx < 2u; ++Idx)
        {
            GizWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            GizWrites[Idx].dstSet          = GizmoSet;
            GizWrites[Idx].dstBinding      = Idx;
            GizWrites[Idx].descriptorCount = 1u;
            GizWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            GizWrites[Idx].pBufferInfo     = &GizBufs[Idx];
        }
        vkUpdateDescriptorSets(Dev, 2u, GizWrites.data(), 0u, nullptr);
    }

    VkCommandBuffer Cmd = Context->BeginSingleTimeCommands();

    std::array<VkClearValue, 3u> Clears {};
    Clears[0].color        = { { 0.068f, 0.072f, 0.084f, 1.0f } };
    Clears[1].color.uint32[0] = 0u;
    Clears[1].color.uint32[1] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[2] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[3] = 0u;
    Clears[2].depthStencil = { 1.0f, 0u };

    VkRenderPassBeginInfo PassBegin {};
    PassBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    PassBegin.renderPass        = DualTargetRenderPass;
    PassBegin.framebuffer       = Target3D.Framebuffer;
    PassBegin.renderArea.extent = { WidthPx, HeightPx };
    PassBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    PassBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport VP { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx), 0.0f, 1.0f };
    VkRect2D   Scissor { { 0, 0 }, { WidthPx, HeightPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &VP);
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SurfacePipeline.Pipeline);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SurfacePipeline.PipelineLayout, 0u, 1u, &SurfaceSet, 0u, nullptr);
    const uint32_t FirstVert = Options.GroundGridEnabled ? 0u : GroundGridVertexCount;
    vkCmdDraw(Cmd, SurfaceVertexCount - FirstVert, 1u, FirstVert, 0u);

    if (GizmoVertCount > 0u)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffPipeline.Pipeline);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffPipeline.PipelineLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
        vkCmdDraw(Cmd, GizmoVertCount, 1u, 0u, 0u);
    }

    vkCmdEndRenderPass(Cmd);
    Target3D.PickLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    ReadbackTicket ColorTicket {};
    if (OptionalOutRGBA != nullptr)
    {
        ColorTicket = TransferRegister.EncodeImageRegionReadback(
            Cmd,
            Target3D.ColorImage.ImageHandle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0,
            0,
            WidthPx,
            HeightPx,
            4u);
    }

    const uint64_t Signal = Context->DispatchCommandBufferAsync(Cmd);
    if (OptionalOutRGBA != nullptr)
    {
        TransferRegister.BindTicketTimelineSignal(ColorTicket, Signal);
        OptionalOutRGBA->resize(static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u);
        (void)TransferRegister.AwaitAndCopyBytes(ColorTicket, *OptionalOutRGBA);
    }
    else
    {
        (void)Context->AwaitTimelineValue(Signal);
    }

    vkFreeCommandBuffers(Dev, Context->AccessCommandPool(), 1u, &Cmd);
    return true;
}

bool UVWorkspaceRasterizer::Rasterize2DUVCanvas(
    uint32_t                      WidthPx,
    uint32_t                      HeightPx,
    const Canvas2DRenderOptions&  Options,
    const Editor::TransformGizmo* OptionalGizmo2D,
    std::vector<uint8_t>*         OptionalOutRGBA)
{
    if (Context == nullptr || UVCanvasVertexCount == 0u || !EnsureFramebufferTarget(Target2D, WidthPx, HeightPx))
    {
        return false;
    }

    vkResetDescriptorPool(Context->AccessDevice(), GraphicsDescriptorPool, 0u);

    GPUCanvas2DUniforms Uniforms {};
    Uniforms.CanvasMinMaxUV[0] = Options.VisibleMinUV.U;
    Uniforms.CanvasMinMaxUV[1] = Options.VisibleMinUV.V;
    Uniforms.CanvasMinMaxUV[2] = Options.VisibleMaxUV.U;
    Uniforms.CanvasMinMaxUV[3] = Options.VisibleMaxUV.V;
    const uint32_t OverlayBits =
        (Options.CheckerEnabled     ? 1u : 0u) |
        (Options.SeamOverlayEnabled ? (1u << 1u) : 0u) |
        (Options.PinOverlayEnabled  ? (1u << 2u) : 0u) |
        (static_cast<uint32_t>(Options.BackdropPreset) << 8u);

    Uniforms.OverlayAndPick[0] = static_cast<uint32_t>(Options.DisplayMode);
    Uniforms.OverlayAndPick[1] = OverlayBits;
    Uniforms.OverlayAndPick[2] = Options.ActiveLayerOrdinal;
    Uniforms.OverlayAndPick[3] = Options.HoveredFaceIndex;

    const std::array<GPUCanvas2DUniforms, 1u> UniSpan {{ Uniforms }};
    ComputeEncoder.UploadSpanToBuffer<GPUCanvas2DUniforms>(Canvas2DUniformBuffer, UniSpan);

    uint32_t GizmoVertCount = 0u;
    if (Options.RenderGizmo && OptionalGizmo2D != nullptr)
    {
        const std::vector<Editor::GizmoGPUVertex> GizmoVerts = OptionalGizmo2D->BuildTriangleStream();
        GizmoVertCount = static_cast<uint32_t>(GizmoVerts.size());
        if (GizmoVertCount > 0u)
        {
            const uint64_t ReqBytes = GizmoVerts.size() * sizeof(Editor::GizmoGPUVertex);
            if (Gizmo2DVertexBuffer.ByteCapacity < ReqBytes)
            {
                Context->ReleaseBuffer(Gizmo2DVertexBuffer);
                Gizmo2DVertexBuffer = ComputeEncoder.AllocateHostWritableBuffer(ReqBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }
            ComputeEncoder.UploadSpanToBuffer<Editor::GizmoGPUVertex>(Gizmo2DVertexBuffer, GizmoVerts);

            GPUGizmoUniforms GizUni {};
            BuildRowMajorOrtho2D(Options.VisibleMinUV, Options.VisibleMaxUV, GizUni.ViewProjection);
            GizUni.HoverAndSpace[0] = static_cast<uint32_t>(OptionalGizmo2D->QueryHoveredHandle());
            GizUni.HoverAndSpace[1] = static_cast<uint32_t>(OptionalGizmo2D->QueryActiveHandle());
            GizUni.HoverAndSpace[2] = 0u;
            const std::array<GPUGizmoUniforms, 1u> GizSpan {{ GizUni }};
            ComputeEncoder.UploadSpanToBuffer<GPUGizmoUniforms>(Gizmo2DUniformBuffer, GizSpan);
        }
    }

    VkDevice Dev = Context->AccessDevice();

    VkDescriptorSet CanvasSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo CanvAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorPool, 1u, &UVCanvasPipeline.BindingLayout };
    vkAllocateDescriptorSets(Dev, &CanvAlloc, &CanvasSet);

    const std::array<VkDescriptorBufferInfo, 3u> CanvBufs {{
        { Canvas2DUniformBuffer.BufferHandle, 0u, sizeof(GPUCanvas2DUniforms) },
        { UVCanvasVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        { SelectionBitmaskBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
    }};
    std::array<VkWriteDescriptorSet, 3u> CanvWrites {};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        CanvWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        CanvWrites[Idx].dstSet          = CanvasSet;
        CanvWrites[Idx].dstBinding      = Idx;
        CanvWrites[Idx].descriptorCount = 1u;
        CanvWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        CanvWrites[Idx].pBufferInfo     = &CanvBufs[Idx];
    }
    vkUpdateDescriptorSets(Dev, 3u, CanvWrites.data(), 0u, nullptr);

    VkDescriptorSet GizmoSet = VK_NULL_HANDLE;
    if (GizmoVertCount > 0u)
    {
        VkDescriptorSetAllocateInfo GizAlloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, GraphicsDescriptorPool, 1u, &GizmoDepthOffPipeline.BindingLayout };
        vkAllocateDescriptorSets(Dev, &GizAlloc, &GizmoSet);
        const std::array<VkDescriptorBufferInfo, 2u> GizBufs {{
            { Gizmo2DUniformBuffer.BufferHandle, 0u, sizeof(GPUGizmoUniforms) },
            { Gizmo2DVertexBuffer.BufferHandle, 0u, VK_WHOLE_SIZE },
        }};
        std::array<VkWriteDescriptorSet, 2u> GizWrites {};
        for (uint32_t Idx = 0u; Idx < 2u; ++Idx)
        {
            GizWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            GizWrites[Idx].dstSet          = GizmoSet;
            GizWrites[Idx].dstBinding      = Idx;
            GizWrites[Idx].descriptorCount = 1u;
            GizWrites[Idx].descriptorType  = (Idx == 0u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            GizWrites[Idx].pBufferInfo     = &GizBufs[Idx];
        }
        vkUpdateDescriptorSets(Dev, 2u, GizWrites.data(), 0u, nullptr);
    }

    VkCommandBuffer Cmd = Context->BeginSingleTimeCommands();

    std::array<VkClearValue, 3u> Clears {};
    Clears[0].color           = { { 0.050f, 0.052f, 0.060f, 1.0f } };
    Clears[1].color.uint32[0] = 0u;
    Clears[1].color.uint32[1] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[2] = GeometrySpace::InvalidNetworkIndex;
    Clears[1].color.uint32[3] = 0u;
    Clears[2].depthStencil    = { 1.0f, 0u };

    VkRenderPassBeginInfo PassBegin {};
    PassBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    PassBegin.renderPass        = DualTargetRenderPass;
    PassBegin.framebuffer       = Target2D.Framebuffer;
    PassBegin.renderArea.extent = { WidthPx, HeightPx };
    PassBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    PassBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport VP { 0.0f, 0.0f, static_cast<float>(WidthPx), static_cast<float>(HeightPx), 0.0f, 1.0f };
    VkRect2D   Scissor { { 0, 0 }, { WidthPx, HeightPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &VP);
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UVCanvasPipeline.Pipeline);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UVCanvasPipeline.PipelineLayout, 0u, 1u, &CanvasSet, 0u, nullptr);
    vkCmdDraw(Cmd, UVCanvasVertexCount, 1u, 0u, 0u);

    if (GizmoVertCount > 0u)
    {
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffPipeline.Pipeline);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoDepthOffPipeline.PipelineLayout, 0u, 1u, &GizmoSet, 0u, nullptr);
        vkCmdDraw(Cmd, GizmoVertCount, 1u, 0u, 0u);
    }

    vkCmdEndRenderPass(Cmd);
    Target2D.PickLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    ReadbackTicket ColorTicket {};
    if (OptionalOutRGBA != nullptr)
    {
        ColorTicket = TransferRegister.EncodeImageRegionReadback(
            Cmd,
            Target2D.ColorImage.ImageHandle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0,
            0,
            WidthPx,
            HeightPx,
            4u);
    }

    const uint64_t Signal = Context->DispatchCommandBufferAsync(Cmd);
    if (OptionalOutRGBA != nullptr)
    {
        TransferRegister.BindTicketTimelineSignal(ColorTicket, Signal);
        OptionalOutRGBA->resize(static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u);
        (void)TransferRegister.AwaitAndCopyBytes(ColorTicket, *OptionalOutRGBA);
    }
    else
    {
        (void)Context->AwaitTimelineValue(Signal);
    }

    vkFreeCommandBuffers(Dev, Context->AccessCommandPool(), 1u, &Cmd);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3D & 2D GPU PICKING & CAMERA RAY
//------------------------------------------------------------------------------------------------------------------------

ViewportPickHit UVWorkspaceRasterizer::Pick3DPixel(int32_t CursorX, int32_t CursorY)
{
    ViewportPickHit Hit = PickQuery.QuerySinglePixel(
        Target3D.PickImage.ImageHandle,
        Target3D.PickLayout,
        Target3D.WidthPx,
        Target3D.HeightPx,
        CursorX,
        CursorY);
    Target3D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Hit;
}

ViewportPickHit UVWorkspaceRasterizer::Pick2DPixel(int32_t CursorX, int32_t CursorY)
{
    ViewportPickHit Hit = PickQuery.QuerySinglePixel(
        Target2D.PickImage.ImageHandle,
        Target2D.PickLayout,
        Target2D.WidthPx,
        Target2D.HeightPx,
        CursorX,
        CursorY);
    Target2D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Hit;
}

RegionPickSummary UVWorkspaceRasterizer::Reduce3DRegionPick(
    uint32_t MinX, uint32_t MinY, uint32_t MaxX, uint32_t MaxY,
    uint32_t BrushCenterX, uint32_t BrushCenterY, uint32_t BrushRadiusPx)
{
    RegionPickSummary Sum = PickQuery.ExecuteRegionReduction(
        Target3D.PickImage.ImageHandle,
        Target3D.PickLayout,
        Target3D.WidthPx,
        Target3D.HeightPx,
        MinX, MinY, MaxX, MaxY,
        BrushCenterX, BrushCenterY, BrushRadiusPx,
        SelectionBitmaskBuffer.BufferHandle,
        UploadedFaceCount);
    Target3D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Sum;
}

RegionPickSummary UVWorkspaceRasterizer::Reduce2DRegionPick(
    uint32_t MinX, uint32_t MinY, uint32_t MaxX, uint32_t MaxY,
    uint32_t BrushCenterX, uint32_t BrushCenterY, uint32_t BrushRadiusPx)
{
    RegionPickSummary Sum = PickQuery.ExecuteRegionReduction(
        Target2D.PickImage.ImageHandle,
        Target2D.PickLayout,
        Target2D.WidthPx,
        Target2D.HeightPx,
        MinX, MinY, MaxX, MaxY,
        BrushCenterX, BrushCenterY, BrushRadiusPx,
        SelectionBitmaskBuffer.BufferHandle,
        UploadedFaceCount);
    Target2D.PickLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return Sum;
}

Editor::ViewportRay3D UVWorkspaceRasterizer::ConstructCameraRay3D(
    const CameraOrbitState3D& Camera,
    uint32_t                  WidthPx,
    uint32_t                  HeightPx,
    float                     CursorX,
    float                     CursorY) noexcept
{
    Mathematics::Axis3D Eye {}, Right {}, Up {}, Fwd {};
    ComputeOrbitBasis(Camera, Eye, Right, Up, Fwd);

    const float Aspect     = static_cast<float>(std::max(1u, WidthPx)) / static_cast<float>(std::max(1u, HeightPx));
    const float TanHalfFov = std::tan(Camera.VerticalFovDeg * Constants::DegToRadFloat * 0.5f);
    const float NdcX       = (CursorX / static_cast<float>(std::max(1u, WidthPx))) * 2.0f - 1.0f;
    const float NdcY       = 1.0f - (CursorY / static_cast<float>(std::max(1u, HeightPx))) * 2.0f;

    const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis(
        Fwd + Right * (NdcX * Aspect * TanHalfFov) + Up * (NdcY * TanHalfFov));
    return Editor::ViewportRay3D{ Eye, Dir };
}

} // namespace Frontier::GeometricRaster
