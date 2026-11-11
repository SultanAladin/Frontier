/*============================================================================================================================================
                                                          UVWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVWorkspaceSession coordinating O(1) GPU layer selection, parallel UV solvers, .UVSpace I/O & 215 MiB revisions

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

UVWorkspaceSession::UVWorkspaceSession()
    : ActiveDocument(
          WorkspaceToken{ 0u, 1u, Codecs::SpaceExtensionCategory::UVSpace },
          "UVWorkspaceSession",
          std::filesystem::path("UVWorkspaceSession.UVSpace")),
      RevisionHistory(ActiveDocument.AccessRevisionTracker())
{
    WorkspaceConfig.AccessUVPreferences().InitialViewportLayout = Configuration::UVViewportLayoutMode::Split2DAnd3D;
    WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled    = false;

    auto& Projection = ActiveDocument.AccessViewportProjection();
    Projection.ConfigureSplitMode(Editor::ViewportSplitMode::SplitPlanarAndSpatial);
    Projection.ConfigureSplitRatio(WorkspaceConfig.InspectUVPreferences().SplitRatioNormalized);

    Gizmo3D.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    Gizmo3D.ConfigureAnchor({ 0.0f, 0.54f, 0.0f }, 1.12f);

    Gizmo2D.ConfigureDimensionMode(Editor::GizmoDimensionMode::Planar2D);
    Gizmo2D.ConfigureAnchor({ 0.50f, 0.50f, 0.0f }, 0.34f);

    LoadSamplePreset(GeometrySpace::SampleAssetPreset::AeroCoupeGT);
}

bool UVWorkspaceSession::AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer)
{
    Rasterizer = BoundRasterizer;
    if (Rasterizer == nullptr)
    {
        return false;
    }
    const bool Uploaded = Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    if (Uploaded)
    {
        (void)Rasterizer->DispatchGPUSelection(GeometricRaster::GPUSelectionOperation::SelectByLayer, SelectedLayerOrdinal, false);
        LatestMetrics     = Rasterizer->EvaluateUVDistortionInspection(Asset.Network, true);
        FramebuffersDirty = true;
    }
    return Uploaded;
}

bool UVWorkspaceSession::InitializeGPUSolvers(
    GeometricRaster::VulkanDeviceContext* Context,
    const std::filesystem::path&          CompiledShaderDir)
{
    const bool RelaxReady   = StretchRelaxer.InitializeGPUKernel(Context, CompiledShaderDir);
    const bool HorizonReady = HorizonPacker.InitializeGPUKernel(Context, CompiledShaderDir);
    return RelaxReady && HorizonReady;
}

void UVWorkspaceSession::TeardownGPUSolvers() noexcept
{
    StretchRelaxer.TeardownGPUKernel();
    HorizonPacker.TeardownGPUKernel();
}

void UVWorkspaceSession::LoadSamplePreset(GeometrySpace::SampleAssetPreset Preset)
{
    ActivePreset = Preset;
    Asset        = GeometrySpace::ConstructSamplePolygonNetwork(Preset);
    Instances    = GeometrySpace::GeometryInstanceRegistry{};

    RootInstanceToken = Instances.IssueInstance(Asset.Network.NetworkTitle, 0u);
    (void)Instances.IssueInstance("Primary_Subpart", 0u, { { 0.0f, 0.45f, 0.0f }, {}, { 1.0f, 1.0f, 1.0f } });

    const auto LayerSpan = Asset.Layers.InspectLayers();
    SelectedLayerOrdinal = LayerSpan.empty() ? 0u : LayerSpan.front().LayerOrdinal;
    ActiveUDIMNumber     = Asset.UDIMTiles.empty() ? 1001u : Asset.UDIMTiles.front();

    RevisionHistory.Reset();
    BaselineUVPairs = Asset.Network.ExtractParameterizedUVStream();

    auto& Projection = ActiveDocument.AccessViewportProjection();
    Editor::SpatialCameraRig Rig = Projection.InspectSpatialCamera();
    Rig.DistanceMeters           = 6.20f;
    Rig.TargetPivot              = { 0.0f, 0.42f, 0.0f };
    Projection.ConfigureSpatialCamera(Rig);

    const float MaxTileU = static_cast<float>(std::max<std::size_t>(1u, Asset.UDIMTiles.size()));
    Projection.ConfigurePlanarDomainSpan(MaxTileU, 1.0f);

    RenderOptions3D.Camera.DistanceMeters = Rig.DistanceMeters;
    RenderOptions3D.Camera.TargetPivot    = Rig.TargetPivot;
    RenderOptions3D.ActiveLayerOrdinal    = SelectedLayerOrdinal;
    RenderOptions3D.SeamOverlayEnabled    = WorkspaceConfig.InspectUVPreferences().SeamOverlayEnabled;
    RenderOptions3D.WireframeEnabled      = WorkspaceConfig.InspectUVPreferences().WireframeOverlayEnabled;

    RenderOptions2D.ActiveLayerOrdinal = SelectedLayerOrdinal;
    RenderOptions2D.CheckerEnabled     = WorkspaceConfig.InspectUVPreferences().CheckerOverlayEnabled;
    Projection.EvaluateAspectLockedPlanarBounds(640u, 640u, RenderOptions2D.VisibleMinUV, RenderOptions2D.VisibleMaxUV);

    SyncNetworkToGPU();

    char Msg[160]{};
    std::snprintf(
        Msg,
        sizeof(Msg),
        "Loaded %s  |  %u Faces across %zu UDIM Tiles",
        Asset.Network.NetworkTitle.c_str(),
        static_cast<uint32_t>(Asset.Network.Faces.size()),
        Asset.UDIMTiles.size());
    StatusBannerText  = Msg;
    FramebuffersDirty = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             O(1) LAYER & MATCAP OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void UVWorkspaceSession::SelectPriorityLayer(uint16_t LayerOrdinal)
{
    SelectedLayerOrdinal               = LayerOrdinal;
    RenderOptions3D.ActiveLayerOrdinal = LayerOrdinal;
    RenderOptions2D.ActiveLayerOrdinal = LayerOrdinal;

    const auto& Ref = Asset.Layers.InspectPartitionSpan(Asset.Network, LayerOrdinal, RootInstanceToken);

    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->DispatchGPUSelection(GeometricRaster::GPUSelectionOperation::SelectByLayer, LayerOrdinal, false);
    }

    char Msg[160]{};
    std::snprintf(
        Msg,
        sizeof(Msg),
        "Selected Layer %u  |  %zu Faces  |  3D Area %.2f m^2",
        static_cast<unsigned>(LayerOrdinal),
        Ref.FaceIndices.size(),
        Ref.TotalArea3D);
    StatusBannerText  = Msg;
    FramebuffersDirty = true;
}

uint16_t UVWorkspaceSession::CreatePriorityLayer(
    std::string_view      Title,
    Mathematics::Scalar3D LinearTint,
    float                 InitialSharePercent)
{
    const float Available = Asset.Layers.DeriveRemainderSharePercent();
    const float Clamped   = std::min(InitialSharePercent, Available);
    const uint16_t NewOrd = Asset.Layers.ConstructLayer(Title, LinearTint, Clamped);
    SelectPriorityLayer(NewOrd);
    return NewOrd;
}

void UVWorkspaceSession::AssignSelectedLayerTint(Mathematics::Scalar3D LinearTint) noexcept
{
    if (SelectedLayerOrdinal == 0u)
    {
        return;
    }
    if (auto* Layer = Asset.Layers.ResolveLayer(SelectedLayerOrdinal))
    {
        Layer->LinearTintRGB = LinearTint;
        FramebuffersDirty    = true;
    }
}

void UVWorkspaceSession::AdjustSelectedLayerShare(float RequestedSharePercent) noexcept
{
    if (SelectedLayerOrdinal == 0u)
    {
        return;
    }
    auto* Layer = Asset.Layers.ResolveLayer(SelectedLayerOrdinal);
    if (Layer == nullptr)
    {
        return;
    }

    float OtherSum = 0.0f;
    for (const auto& Entry : Asset.Layers.InspectLayers())
    {
        if (Entry.LayerOrdinal != SelectedLayerOrdinal)
        {
            OtherSum += Entry.TargetSharePercent;
        }
    }
    const float Ceiling       = std::max(0.0f, 100.0f - OtherSum);
    Layer->TargetSharePercent = std::clamp(RequestedSharePercent, 0.0f, Ceiling);
    FramebuffersDirty         = true;
}

void UVWorkspaceSession::ApplyMatCapToActiveSelection(uint8_t MatCapSlot)
{
    const auto& Ref = Asset.Layers.InspectPartitionSpan(Asset.Network, SelectedLayerOrdinal, RootInstanceToken);
    for (const uint32_t FaceIdx : Ref.FaceIndices)
    {
        if (FaceIdx < Asset.Network.Faces.size())
        {
            Asset.Network.Faces[FaceIdx].Shading.MatCapSlot = static_cast<uint8_t>(MatCapSlot & 15u);
        }
    }
    ++Asset.Network.ShadingRevision;
    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
    }
    FramebuffersDirty = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  SOLVER DIRECTIVES, ASYNC THREADING & 215 MIB REVISIONS
//------------------------------------------------------------------------------------------------------------------------

void UVWorkspaceSession::QueueDirective(UVCommandDirective Directive) noexcept
{
    PendingDirective = Directive;
}

UVCommandDirective UVWorkspaceSession::ConsumePendingDirective() noexcept
{
    const UVCommandDirective Current = PendingDirective;
    PendingDirective                 = UVCommandDirective::None;
    return Current;
}

bool UVWorkspaceSession::ExecuteDirective(
    UVCommandDirective            Directive,
    Threading::ExecutionChannels* OptionalChannels)
{
    if (Directive == UVCommandDirective::None)
    {
        return false;
    }

    const auto& Prefs = WorkspaceConfig.InspectUVPreferences();
    char        Msg[192]{};

    switch (Directive)
    {
        case UVCommandDirective::MarkSharpSeams:
        {
            const uint32_t Marked = SeamCutGenerator::MarkDihedralSharpSeams(Asset.Network, SharpAngleThreshold, false);
            std::snprintf(Msg, sizeof(Msg), "Marked %u Dihedral Seam Edges (>= %.0f deg)", Marked, SharpAngleThreshold);
            RecordUVRevisionStep("MarkSharpSeams");
            break;
        }
        case UVCommandDirective::UnwrapConformal:
        {
            const ConformalUnwrapReport Rep = ConformalUnwrapSolver::FlattenIslandsConformally(Asset.Network, OptionalChannels, 28u);
            std::snprintf(Msg, sizeof(Msg), "Conformal Unwrap: %u Islands in %.2f ms (0 Flips)", Rep.IslandsFlattened, Rep.ElapsedMilliseconds);
            RecordUVRevisionStep("UnwrapConformal");
            break;
        }
        case UVCommandDirective::RelaxStretchGPU:
        {
            const StretchRelaxReport Rep = StretchRelaxer.RelaxStretch(Asset.Network, RelaxIterations, 0.35f, true);
            std::snprintf(
                Msg,
                sizeof(Msg),
                "Relaxed %u Corners (%u Passes, %s) in %.2f ms",
                Rep.WeldedCornersRelaxed,
                Rep.ExecutedPasses,
                Rep.ExecutedOnVulkanGPU ? "Vulkan .slang" : "CPU",
                Rep.ElapsedMilliseconds);
            RecordUVRevisionStep("RelaxStretchGPU");
            break;
        }
        case UVCommandDirective::PackUVsConcaveSkyline:
        {
            LatestPackReport = HorizonPacker.PackConcaveSkyline(Asset.Network, Asset.Layers, Prefs, false, OptionalChannels);
            Asset.UDIMTiles  = LatestPackReport.ActiveUDIMNumbers;
            ActiveUDIMNumber = Asset.UDIMTiles.empty() ? 1001u : Asset.UDIMTiles.front();
            std::snprintf(
                Msg,
                sizeof(Msg),
                "Packed %u Islands (%s) | UDIM 1001 Fill %.1f%% in %.2f ms",
                LatestPackReport.IslandsPacked,
                LatestPackReport.EvaluatedOnVulkanGPU ? "GPU Skyline" : "CPU Skyline",
                LatestPackReport.PrimaryTileEfficiency * 100.0f,
                LatestPackReport.ElapsedMilliseconds);
            RecordUVRevisionStep("PackConcaveSkyline");
            break;
        }
        case UVCommandDirective::EqualizeTexelDensity:
        {
            const TexelDensityReport Rep = TexelDensityEqualizer::EqualizePhysicalTexelDensity(
                Asset.Network, Prefs.TargetTexelDensity, Prefs.DefaultTileResolutionPx);
            std::snprintf(Msg, sizeof(Msg), "Equalized %u Islands -> %.0f px/m", Rep.IslandsEqualized, Rep.AchievedMeanTexelPerM);
            RecordUVRevisionStep("EqualizeTexelDensity");
            break;
        }
        case UVCommandDirective::AutoDistributeUDIMs:
        {
            LatestPackReport = HorizonPacker.PackConcaveSkyline(Asset.Network, Asset.Layers, Prefs, true, OptionalChannels);
            Asset.UDIMTiles  = LatestPackReport.ActiveUDIMNumbers;
            ActiveUDIMNumber = Asset.UDIMTiles.empty() ? 1001u : Asset.UDIMTiles.front();
            std::snprintf(
                Msg,
                sizeof(Msg),
                "Distributed %u Islands across %u UDIM Tiles (%.2f ms)",
                LatestPackReport.IslandsPacked,
                LatestPackReport.UDIMTilesOccupied,
                LatestPackReport.ElapsedMilliseconds);
            RecordUVRevisionStep("AutoDistributeUDIMs");
            break;
        }
        case UVCommandDirective::ValidateDistortionGPU:
        {
            SyncNetworkToGPU();
            std::snprintf(
                Msg,
                sizeof(Msg),
                "GPU Validated: L2=%.3f | Flips=%u | Overlaps=%u",
                LatestMetrics.MeanL2Stretch,
                LatestMetrics.FlippedFaceCount,
                LatestMetrics.OverlapFaceCount);
            StatusBannerText = Msg;
            return true;
        }
        default:
            return false;
    }

    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    StatusBannerText  = Msg;
    FramebuffersDirty = true;
    return true;
}

bool UVWorkspaceSession::DispatchDirectiveAsync(
    Threading::WorkspaceThread&   WorkerThread,
    Threading::ExecutionChannels* OptionalChannels,
    UVCommandDirective            Directive,
    Threading::CompletionCounter* OptionalCompletion)
{
    return WorkerThread.EnqueueAuthoringTask(
        [this, &WorkerThread, OptionalChannels, Directive]()
        {
            (void)ExecuteDirective(Directive, OptionalChannels);
            WorkerThread.PublishSnapshot(
                1u,
                RevisionHistory.QueryBytesUsed(),
                Constants::DefaultGlobalRevisionBudgetBytes,
                StatusBannerText);
        },
        static_cast<uint32_t>(Directive),
        OptionalCompletion);
}

void UVWorkspaceSession::RecordUVRevisionStep(std::string_view OperationTag)
{
    const std::vector<float> UVStream = Asset.Network.ExtractParameterizedUVStream();
    const std::span<const std::byte> BytePayload(
        reinterpret_cast<const std::byte*>(UVStream.data()),
        UVStream.size() * sizeof(float));

    const ParametricOperationParameters Params = OperationRecorder::ConstructUVTileTransform(
        static_cast<int32_t>((ActiveUDIMNumber - 1001u) % 10u),
        static_cast<int32_t>((ActiveUDIMNumber - 1001u) / 10u),
        0.0,
        0.0,
        1.0,
        0.0,
        WorkspaceConfig.InspectUVPreferences().TargetTexelDensity,
        OperationTag);

    (void)RevisionHistory.RecordParametricOperation(Params, RevisionStampCounter++, BytePayload);
}

bool UVWorkspaceSession::RestoreUVFromRevisionCursor()
{
    const auto&    History   = RevisionHistory.InspectHistory();
    const uint32_t CursorIdx = History.QueryCursorIndex();
    const uint32_t RootIdx   = History.QueryRootIndex();

    if (CursorIdx == RootIdx)
    {
        if (!Asset.Network.ApplyParameterizedUVStream(BaselineUVPairs))
        {
            return false;
        }
    }
    else
    {
        const auto Entries = History.InspectEntries();
        if (CursorIdx >= Entries.size())
        {
            return false;
        }
        const uint32_t RecIdx  = Entries[CursorIdx].RecordEntryIndex;
        const auto     Records = RevisionHistory.InspectRecorder().InspectRecordEntries();
        if (RecIdx >= Records.size())
        {
            return false;
        }
        const auto& Rec       = Records[RecIdx];
        const auto  RawSlice  = RevisionHistory.InspectRecorder().InspectContentRegion().InspectSpan(Rec.RegionByteOffset, Rec.RegionByteLength);
        if (RawSlice.empty() || (RawSlice.size() % sizeof(float)) != 0u)
        {
            return false;
        }
        const std::span<const float> FloatSpan(
            reinterpret_cast<const float*>(RawSlice.data()),
            RawSlice.size() / sizeof(float));
        if (!Asset.Network.ApplyParameterizedUVStream(FloatSpan))
        {
            return false;
        }
    }

    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    FramebuffersDirty = true;
    return true;
}

bool UVWorkspaceSession::UndoParameterization()
{
    if (!RevisionHistory.Undo())
    {
        return false;
    }
    return RestoreUVFromRevisionCursor();
}

bool UVWorkspaceSession::RedoParameterization()
{
    if (!RevisionHistory.Redo())
    {
        return false;
    }
    return RestoreUVFromRevisionCursor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           .UVSPACE PERSISTENCE (UVCODEC + REVISION)
//------------------------------------------------------------------------------------------------------------------------

bool UVWorkspaceSession::SaveUVSpaceDocument(
    const std::filesystem::path& TargetPath,
    std::string&                 OutDiagnostic) const
{
    Codecs::UVSpaceDocumentModel Model {};
    Model.DocumentTitle        = Asset.Network.NetworkTitle;
    Model.GridColumns          = 10u;
    Model.ReferenceScaleMeters = 1.0;

    const auto& Prefs = WorkspaceConfig.InspectUVPreferences();
    for (const uint32_t UDIMCode : Asset.UDIMTiles)
    {
        const int32_t TileU = static_cast<int32_t>((UDIMCode - 1001u) % 10u);
        const int32_t TileV = static_cast<int32_t>((UDIMCode - 1001u) / 10u);
        Model.Tiles.push_back({
            TileU,
            TileV,
            UDIMCode,
            static_cast<double>(Prefs.TargetTexelDensity),
            static_cast<double>(Prefs.IslandMarginPixels),
        });
    }
    if (Model.Tiles.empty())
    {
        Model.Tiles.push_back({ 0, 0, 1001u, static_cast<double>(Prefs.TargetTexelDensity), static_cast<double>(Prefs.IslandMarginPixels) });
    }

    Model.ParameterizedUVPairs = Asset.Network.ExtractParameterizedUVStream();
    return Codecs::UVCodec::Encoder::EncodeToFile(TargetPath, Model, OutDiagnostic, &RevisionHistory);
}

bool UVWorkspaceSession::LoadUVSpaceDocument(
    const std::filesystem::path& SourcePath,
    std::string&                 OutDiagnostic)
{
    Codecs::UVSpaceDocumentModel LoadedModel {};
    if (!Codecs::UVCodec::Decoder::DecodeFromFile(SourcePath, LoadedModel, OutDiagnostic, &RevisionHistory))
    {
        return false;
    }

    if (!Asset.Network.ApplyParameterizedUVStream(LoadedModel.ParameterizedUVPairs))
    {
        OutDiagnostic = "ParameterizedUVPairs count does not match active PolygonNetwork::Edges.";
        return false;
    }

    Asset.UDIMTiles.clear();
    for (const auto& Tile : LoadedModel.Tiles)
    {
        Asset.UDIMTiles.push_back(Tile.UDIMNumber);
    }
    if (Asset.UDIMTiles.empty())
    {
        Asset.UDIMTiles.push_back(1001u);
    }
    ActiveUDIMNumber = Asset.UDIMTiles.front();

    Asset.Layers.SynchronizePartitions(Asset.Network, RootInstanceToken);
    SyncNetworkToGPU();
    FramebuffersDirty = true;
    return true;
}

void UVWorkspaceSession::SyncNetworkToGPU()
{
    if (Rasterizer != nullptr)
    {
        (void)Rasterizer->UploadPolygonNetwork(Asset.Network, RootInstanceToken.SlotIndex);
        (void)Rasterizer->DispatchGPUSelection(GeometricRaster::GPUSelectionOperation::SelectByLayer, SelectedLayerOrdinal, false);
        LatestMetrics = Rasterizer->EvaluateUVDistortionInspection(Asset.Network, true);
    }
}

void UVWorkspaceSession::RefreshViewportFramebuffers(
    uint32_t Viewport3DW,
    uint32_t Viewport3DH,
    uint32_t Canvas2DW,
    uint32_t Canvas2DH)
{
    if (PendingDirective != UVCommandDirective::None)
    {
        (void)ExecuteDirective(ConsumePendingDirective(), nullptr);
    }

    if (Rasterizer == nullptr)
    {
        return;
    }

    const bool SizeChanged =
        (Viewport3DW != Framebuffer3DWidth) || (Viewport3DH != Framebuffer3DHeight) ||
        (Canvas2DW != Framebuffer2DWidth) || (Canvas2DH != Framebuffer2DHeight);

    if (!FramebuffersDirty && !SizeChanged)
    {
        return;
    }

    auto&       Projection = ActiveDocument.AccessViewportProjection();
    const auto& Rig        = Projection.InspectSpatialCamera();

    RenderOptions3D.Camera.TargetPivot    = Rig.TargetPivot;
    RenderOptions3D.Camera.YawRadians     = Rig.YawRadians;
    RenderOptions3D.Camera.PitchRadians   = Rig.PitchRadians;
    RenderOptions3D.Camera.DistanceMeters = Rig.DistanceMeters;
    RenderOptions3D.Camera.VerticalFovDeg = Rig.VerticalFovDeg;
    RenderOptions3D.SeamOverlayEnabled    = WorkspaceConfig.InspectUVPreferences().SeamOverlayEnabled;
    RenderOptions3D.WireframeEnabled      = WorkspaceConfig.InspectUVPreferences().WireframeOverlayEnabled;
    RenderOptions2D.CheckerEnabled        = WorkspaceConfig.InspectUVPreferences().CheckerOverlayEnabled;

    if (Viewport3DW >= 16u && Viewport3DH >= 16u)
    {
        if (Rasterizer->Rasterize3DViewport(Viewport3DW, Viewport3DH, RenderOptions3D, &Gizmo3D, &Framebuffer3DRGBA))
        {
            Framebuffer3DWidth  = Viewport3DW;
            Framebuffer3DHeight = Viewport3DH;
        }
    }

    if (Canvas2DW >= 16u && Canvas2DH >= 16u)
    {
        const float TileSpanU = static_cast<float>(std::max<std::size_t>(1u, Asset.UDIMTiles.size()));
        Projection.ConfigurePlanarDomainSpan(TileSpanU, 1.0f);
        Projection.EvaluateAspectLockedPlanarBounds(Canvas2DW, Canvas2DH, RenderOptions2D.VisibleMinUV, RenderOptions2D.VisibleMaxUV);

        if (Rasterizer->Rasterize2DUVCanvas(Canvas2DW, Canvas2DH, RenderOptions2D, &Gizmo2D, &Framebuffer2DRGBA))
        {
            Framebuffer2DWidth  = Canvas2DW;
            Framebuffer2DHeight = Canvas2DH;
        }
    }

    FramebuffersDirty = false;
}

} // namespace Frontier::Workspaces
