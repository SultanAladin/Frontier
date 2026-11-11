/*============================================================================================================================================
                                                           UVWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Shared UV Workspace authoring state binding PolygonNetwork, GPU/Parallel Solvers, .UVSpace Codec, 215 MiB RevisionTracker & Viewports

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/UVCodec.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Configuration/WorkspaceConfiguration.h"
#include "Engine/Editor/Viewports/TransformGizmo.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/GeometryInstance.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Threads/Workspace/WorkspaceThread.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"
#include "Engine/Workspaces/WorkspaceDocument.h"
#include "Engine/Workspaces/UV/Solvers/ConcaveHorizonPacker.h"
#include "Engine/Workspaces/UV/Solvers/ConformalUnwrapSolver.h"
#include "Engine/Workspaces/UV/Solvers/ProgressiveStretchRelaxer.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"
#include "Engine/Workspaces/UV/Solvers/TexelDensityEqualizer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class UVInspectorTab : uint8_t
{
    Packing = 0u, // [-] - Layer share slider & Concave Packing Engine controls
    Unwrap  = 1u, // [-] - Seam cutting, Conformal Flatten & Progressive Stretch Relaxation
    UDIM    = 2u, // [-] - Multi-UDIM tile table, resolution & texel density
    Shading = 3u, // [-] - 16-slot MatCap palette & Surface Material PBR sliders
    Check   = 4u, // [-] - GPU Stretch/Overlap/Flip validator & viewport overlay switches
};

enum class UVSelectionMode : uint8_t
{
    Faces   = 0u,
    Edges   = 1u,
    Islands = 2u,
    Layers  = 3u,
};

enum class UVCommandDirective : uint8_t
{
    None                   = 0u,
    PackUVsConcaveSkyline  = 1u,
    UnwrapConformal        = 2u,
    RelaxStretchGPU        = 3u,
    MarkSharpSeams         = 4u,
    EqualizeTexelDensity   = 5u,
    AutoDistributeUDIMs    = 6u,
    ValidateDistortionGPU  = 7u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class UVWorkspaceSession
{
public:
    UVWorkspaceSession();

    [[nodiscard]] bool AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer);
    [[nodiscard]] bool InitializeGPUSolvers(
        GeometricRaster::VulkanDeviceContext* Context,
        const std::filesystem::path&          CompiledShaderDir);
    void TeardownGPUSolvers() noexcept;

    void LoadSamplePreset(GeometrySpace::SampleAssetPreset Preset);

    void     SelectPriorityLayer(uint16_t LayerOrdinal);
    uint16_t CreatePriorityLayer(std::string_view Title, Mathematics::Scalar3D LinearTint, float InitialSharePercent);
    void     AssignSelectedLayerTint(Mathematics::Scalar3D LinearTint) noexcept;
    void     AdjustSelectedLayerShare(float RequestedSharePercent) noexcept;
    void     ApplyMatCapToActiveSelection(uint8_t MatCapSlot);

    [[nodiscard]] WorkspaceDocument&                AccessDocument() noexcept { return ActiveDocument; }
    [[nodiscard]] const WorkspaceDocument&          InspectDocument() const noexcept { return ActiveDocument; }
    [[nodiscard]] Editor::ViewportProjection&       AccessViewportProjection() noexcept { return ActiveDocument.AccessViewportProjection(); }
    [[nodiscard]] const Editor::ViewportProjection& InspectViewportProjection() const noexcept { return ActiveDocument.InspectViewportProjection(); }

    void                             QueueDirective(UVCommandDirective Directive) noexcept;
    [[nodiscard]] UVCommandDirective ConsumePendingDirective() noexcept;

    bool ExecuteDirective(
        UVCommandDirective            Directive,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] bool DispatchDirectiveAsync(
        Threading::WorkspaceThread&   WorkerThread,
        Threading::ExecutionChannels* OptionalChannels,
        UVCommandDirective            Directive,
        Threading::CompletionCounter* OptionalCompletion = nullptr);

    bool UndoParameterization();
    bool RedoParameterization();

    [[nodiscard]] bool SaveUVSpaceDocument(
        const std::filesystem::path& TargetPath,
        std::string&                 OutDiagnostic) const;

    [[nodiscard]] bool LoadUVSpaceDocument(
        const std::filesystem::path& SourcePath,
        std::string&                 OutDiagnostic);

    void RefreshViewportFramebuffers(uint32_t Viewport3DW, uint32_t Viewport3DH, uint32_t Canvas2DW, uint32_t Canvas2DH);

    GeometrySpace::SampleAssetPreset         ActivePreset         = GeometrySpace::SampleAssetPreset::AeroCoupeGT;
    GeometrySpace::ConstructedSampleAsset    Asset                {};
    GeometrySpace::GeometryInstanceRegistry  Instances            {};
    GeometrySpace::GeometryInstanceToken     RootInstanceToken    {};
    Configuration::WorkspaceConfiguration    WorkspaceConfig      {};
    WorkspaceDocument                        ActiveDocument;
    RevisionTracker&                         RevisionHistory;
    std::vector<float>                       BaselineUVPairs      {};
    uint64_t                                 RevisionStampCounter = 1ull;

    ProgressiveStretchRelaxer                StretchRelaxer       {};
    ConcaveHorizonPacker                     HorizonPacker        {};
    ConcavePackReport                        LatestPackReport     {};

    UVInspectorTab                           ActiveInspectorTab   = UVInspectorTab::Packing;
    UVSelectionMode                          SelectionMode        = UVSelectionMode::Layers;
    uint16_t                                 SelectedLayerOrdinal = 1u;
    uint32_t                                 ActiveUDIMNumber     = 1001u;
    float                                    SharpAngleThreshold  = 52.0f;
    uint32_t                                 RelaxIterations      = 12u;

    Editor::TransformGizmo                   Gizmo3D              {};
    Editor::TransformGizmo                   Gizmo2D              {};
    GeometricRaster::Viewport3DRenderOptions RenderOptions3D      {};
    GeometricRaster::Canvas2DRenderOptions   RenderOptions2D      {};
    GeometricRaster::GPUUVDistortionSummary LatestMetrics       {};
    GeometricRaster::ViewportPickHit         LastHoverHit         {};
    std::string                              StatusBannerText     { "Ready  |  Zero Flips, Zero Overlaps" };

    std::vector<uint8_t>                     Framebuffer3DRGBA    {};
    uint32_t                                 Framebuffer3DWidth   = 0u;
    uint32_t                                 Framebuffer3DHeight  = 0u;

    std::vector<uint8_t>                     Framebuffer2DRGBA    {};
    uint32_t                                 Framebuffer2DWidth   = 0u;
    uint32_t                                 Framebuffer2DHeight  = 0u;
    bool                                     FramebuffersDirty    = true;

private:
    void RecordUVRevisionStep(std::string_view OperationTag);
    bool RestoreUVFromRevisionCursor();
    void SyncNetworkToGPU();

    GeometricRaster::UVWorkspaceRasterizer*  Rasterizer           = nullptr;
    UVCommandDirective                       PendingDirective     = UVCommandDirective::None;
};

} // namespace Frontier::Workspaces
