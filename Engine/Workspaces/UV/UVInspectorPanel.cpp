/*============================================================================================================================================
                                                          UVINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of 5-tab UVInspectorPanel with Concave Packing, Full Projections, Seams/Pins, Per-UDIM Res/Backdrop, Shading & GPU Check

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVInspectorPanel.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

constexpr ColorRgba InspectorBg   { 20, 21, 25, 255 };
constexpr ColorRgba SectionCard   { 27, 29, 35, 255 };
constexpr ColorRgba ActivePill    { 40, 46, 58, 255 };
constexpr ColorRgba PrimaryBtnBg  { 52, 92, 162, 255 };
constexpr ColorRgba BorderStroke  { 48, 52, 62, 255 };
constexpr ColorRgba IvoryAccent   { 239, 244, 207, 255 };
constexpr ColorRgba SkyAccent     { 118, 182, 255, 255 };
constexpr ColorRgba EmeraldGood   { 92, 212, 148, 255 };
constexpr ColorRgba WarnCoral     { 238, 104, 98, 255 };
constexpr ColorRgba CrispWhite    { 230, 234, 242, 255 };
constexpr ColorRgba MutedSubtext  { 146, 152, 166, 255 };

bool RenderActionCapsule(
    PixelSpace&             Surface,
    Rectangle2D             Rect,
    const char*             Caption,
    bool                    PrimaryStyle,
    const PaneInputReading& Input) noexcept
{
    const bool Hovered = Rect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y });
    ColorRgba  Fill    = PrimaryStyle ? PrimaryBtnBg : ActivePill;
    if (Hovered)
    {
        Fill = ColorRgba::Blend(Fill, { 255, 255, 255, 255 }, 0.12f);
    }
    Surface.FillRectangle(Rect, Fill, Rect.Height * 0.5f);
    Surface.StrokeRectangle(Rect, PrimaryStyle ? SkyAccent : BorderStroke, Rect.Height * 0.5f, 1.1f);
    Surface.TextCentered(Rect, 11.5f, PrimaryStyle ? IvoryAccent : CrispWhite, Caption);
    return Input.PrimaryPointer.Pressed && Hovered;
}

float RenderSliderControlRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   Width,
    const char*             Title,
    const char*             NumberText,
    const char*             UnitText,
    float                   MinVal,
    float                   MaxVal,
    float                   CurrentVal,
    uint32_t                FieldId,
    uint32_t&               InOutDraggingField,
    const PaneInputReading& Input,
    float&                  OutNewValue) noexcept
{
    Surface.Text({ LeftX, TopY }, 11.5f, CrispWhite, Title);
    const Rectangle2D SliderRow { LeftX, TopY + 18.0f, Width, 28.0f };
    const bool        ActiveDrag = (InOutDraggingField == FieldId);

    OutNewValue = CurrentVal;
    const ControlContact Contact = ScalarSlider::ConstructWithReadout(
        Surface,
        SliderRow,
        NumberText,
        UnitText,
        MinVal,
        MaxVal,
        CurrentVal,
        ActiveDrag,
        Input.PrimaryPointer,
        OutNewValue);

    if (Contact.Pressed)
    {
        InOutDraggingField = FieldId;
    }
    else if (!Input.PrimaryPointer.Down && InOutDraggingField == FieldId)
    {
        InOutDraggingField = 0u;
    }
    return TopY + 52.0f;
}

} // namespace

void UVInspectorPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void UVInspectorPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    (void)Δt;
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, InspectorBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    const float LeftX  = Bounds.X + 12.0f;
    const float InnerW = std::max(140.0f, Bounds.Width - 24.0f);
    float       CurY   = Bounds.Y + 12.0f;

    Surface.Text({ LeftX, CurY }, 13.5f, IvoryAccent, "UV INSPECTOR");
    CurY += 24.0f;

    // 5-Tab Segmented Selector: Pack | Unwrap | UDIM | Shade | Check
    const std::array<const char*, 5u> TabLabels {{ "Pack", "Unwrap", "UDIM", "Shade", "Check" }};
    const float TabGap = 4.0f;
    const float TabW   = (InnerW - TabGap * 4.0f) / 5.0f;
    for (uint32_t TabIdx = 0u; TabIdx < 5u; ++TabIdx)
    {
        const Rectangle2D TRect { LeftX + static_cast<float>(TabIdx) * (TabW + TabGap), CurY, TabW, 28.0f };
        const bool        Selected = (static_cast<uint32_t>(Session->ActiveInspectorTab) == TabIdx);
        Surface.FillRectangle(TRect, Selected ? ActivePill : SectionCard, 6.0f);
        Surface.StrokeRectangle(TRect, Selected ? IvoryAccent : BorderStroke, 6.0f, Selected ? 1.3f : 1.0f);
        Surface.TextCentered(TRect, 11.5f, Selected ? IvoryAccent : CrispWhite, TabLabels[TabIdx]);
        if (Input.PrimaryPointer.Pressed && TRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
        {
            Session->ActiveInspectorTab = static_cast<UVInspectorTab>(TabIdx);
        }
    }
    CurY += 38.0f;

    const Rectangle2D BodyArea { LeftX, CurY, InnerW, std::max(40.0f, Bounds.Bottom() - CurY - 10.0f) };
    switch (Session->ActiveInspectorTab)
    {
        case UVInspectorTab::Packing: RenderPackingSection(Surface, BodyArea, Input); break;
        case UVInspectorTab::Unwrap:  RenderUnwrapSection(Surface, BodyArea, Input);  break;
        case UVInspectorTab::UDIM:    RenderUDIMSection(Surface, BodyArea, Input);    break;
        case UVInspectorTab::Shading: RenderShadingSection(Surface, BodyArea, Input); break;
        case UVInspectorTab::Check:   RenderCheckSection(Surface, BodyArea, Input);   break;
    }

    Surface.PopClip();
}

void UVInspectorPanel::RenderPackingSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    float CurY  = Area.Y;
    auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();

    // Selected Priority Layer Share Slider
    Surface.FillRectangle({ Area.X, CurY, Area.Width, 94.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, CurY, Area.Width, 94.0f }, BorderStroke, 6.0f, 1.0f);

    const auto* ActiveLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
    if (ActiveLayer != nullptr)
    {
        Surface.Text({ Area.X + 12.0f, CurY + 10.0f }, 12.0f, IvoryAccent, ActiveLayer->LayerTitle.c_str());
        char NumStr[24]{};
        std::snprintf(NumStr, sizeof(NumStr), "%.0f", ActiveLayer->TargetSharePercent);
        float RequestedShare = ActiveLayer->TargetSharePercent;
        (void)RenderSliderControlRow(
            Surface, Area.X + 12.0f, CurY + 30.0f, Area.Width - 24.0f,
            "Target Tile Share", NumStr, "%", 0.0f, 100.0f, ActiveLayer->TargetSharePercent,
            101u, DraggingField, Input, RequestedShare);
        if (std::abs(RequestedShare - ActiveLayer->TargetSharePercent) > 0.05f)
        {
            Session->AdjustSelectedLayerShare(RequestedShare);
        }
    }
    else
    {
        Surface.Text({ Area.X + 12.0f, CurY + 14.0f }, 12.0f, SkyAccent, "Unassigned Remainder Selected");
        char RemLine[96]{};
        std::snprintf(RemLine, sizeof(RemLine), "Auto-Receives %.0f%% Remaining UDIM Space", Session->Asset.Layers.DeriveRemainderSharePercent());
        Surface.Text({ Area.X + 12.0f, CurY + 42.0f }, 11.5f, MutedSubtext, RemLine);
    }
    CurY += 104.0f;

    // Concave Packing Engine + Island Stack/Pocket Controls
    Surface.FillRectangle({ Area.X, CurY, Area.Width, 304.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, CurY, Area.Width, 304.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, CurY + 10.0f }, 12.0f, SkyAccent, "CONCAVE PACKING ENGINE (GPU SKYLINE)");

    char MarginStr[24]{};
    std::snprintf(MarginStr, sizeof(MarginStr), "%u", Prefs.IslandMarginPixels);
    float MarginFloat = static_cast<float>(Prefs.IslandMarginPixels);
    float NewMargin   = MarginFloat;
    float RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, CurY + 32.0f, Area.Width - 24.0f,
        "Bleed Gutter Margin", MarginStr, "px", 2.0f, 32.0f, MarginFloat,
        102u, DraggingField, Input, NewMargin);
    Prefs.IslandMarginPixels = static_cast<uint32_t>(std::round(NewMargin));

    Surface.Text({ Area.X + 12.0f, RowY }, 11.5f, CrispWhite, "Rotation Candidates");
    const std::array<uint32_t, 3u> RotSteps {{ 0u, 90u, 45u }};
    const std::array<const char*, 3u> RotNames {{ "Lock 0", "90 deg", "45 deg" }};
    const float RChipW = (Area.Width - 36.0f) / 3.0f;
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        const Rectangle2D CRect { Area.X + 12.0f + static_cast<float>(Idx) * (RChipW + 6.0f), RowY + 18.0f, RChipW, 24.0f };
        const bool Active = (Prefs.RotationStepDegrees == RotSteps[Idx]);
        Surface.FillRectangle(CRect, Active ? ActivePill : InspectorBg, 5.0f);
        Surface.StrokeRectangle(CRect, Active ? SkyAccent : BorderStroke, 5.0f, 1.0f);
        Surface.TextCentered(CRect, 11.0f, Active ? IvoryAccent : CrispWhite, RotNames[Idx]);
        if (Input.PrimaryPointer.Pressed && CRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
        {
            Prefs.RotationStepDegrees = RotSteps[Idx];
        }
    }
    RowY += 50.0f;

    Surface.Text({ Area.X + 12.0f, RowY + 4.0f }, 11.5f, CrispWhite, "Equalize Intra-Layer Scale");
    if (ExclusiveSelector::ConstructCompact(
            Surface,
            { Area.X + Area.Width - 60.0f, RowY, 48.0f, 22.0f },
            Prefs.EqualizeIntraLayerScale,
            Input.PrimaryPointer).Clicked)
    {
        Prefs.EqualizeIntraLayerScale = !Prefs.EqualizeIntraLayerScale;
    }
    RowY += 28.0f;

    Surface.Text({ Area.X + 12.0f, RowY + 4.0f }, 11.5f, CrispWhite, "Fill Empty Pockets");
    if (ExclusiveSelector::ConstructCompact(
            Surface,
            { Area.X + Area.Width - 60.0f, RowY, 48.0f, 22.0f },
            Session->FillPocketsEnabled,
            Input.PrimaryPointer).Clicked)
    {
        Session->FillPocketsEnabled = !Session->FillPocketsEnabled;
    }
    RowY += 32.0f;

    const float HalfBtnW = (Area.Width - 30.0f) * 0.5f;
    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, HalfBtnW, 28.0f }, "Stack Similar", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::StackSimilarIslands);
    }
    if (RenderActionCapsule(Surface, { Area.X + 18.0f + HalfBtnW, RowY, HalfBtnW, 28.0f }, "Unstack", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::UnstackOverlapping);
    }
    RowY += 36.0f;

    if (RenderActionCapsule(
            Surface,
            { Area.X + 12.0f, RowY, Area.Width - 24.0f, 34.0f },
            "Pack UVs (Concave Skyline + GPU)",
            true,
            Input))
    {
        Session->QueueDirective(UVCommandDirective::PackUVsConcaveSkyline);
    }
}

void UVInspectorPanel::RenderUnwrapSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    float CurY = Area.Y;
    Surface.FillRectangle({ Area.X, CurY, Area.Width, 434.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, CurY, Area.Width, 434.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, CurY + 10.0f }, 12.0f, SkyAccent, "UNWRAP, PROJECTIONS, SEAMS, PINS & RELAX");

    // 7 Projection / Unwrap Method Chips (2 rows)
    const std::array<const char*, 7u> MethodNames {{ "LSCM", "ABF++", "Smart", "Box", "Cyl", "Sphere", "Quads" }};
    const std::array<UVCommandDirective, 7u> MethodDirs {{
        UVCommandDirective::UnwrapConformal,
        UVCommandDirective::FlattenAngleBasedABF,
        UVCommandDirective::ProjectSmartUV,
        UVCommandDirective::ProjectCubicBox,
        UVCommandDirective::ProjectCylindrical,
        UVCommandDirective::ProjectSpherical,
        UVCommandDirective::FollowActiveQuads,
    }};

    float RowY = CurY + 32.0f;
    const float ChipW4 = (Area.Width - 36.0f) / 4.0f;
    for (uint32_t MIdx = 0u; MIdx < 4u; ++MIdx)
    {
        const Rectangle2D MRect { Area.X + 12.0f + static_cast<float>(MIdx) * (ChipW4 + 4.0f), RowY, ChipW4, 24.0f };
        const bool Active = (static_cast<uint32_t>(Session->ActiveUnwrapMethod) == MIdx);
        Surface.FillRectangle(MRect, Active ? ActivePill : InspectorBg, 5.0f);
        Surface.StrokeRectangle(MRect, Active ? IvoryAccent : BorderStroke, 5.0f, 1.0f);
        Surface.TextCentered(MRect, 10.5f, Active ? IvoryAccent : CrispWhite, MethodNames[MIdx]);
        if (Input.PrimaryPointer.Pressed && MRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
        {
            Session->ActiveUnwrapMethod = static_cast<UVProjectionMethod>(MIdx);
            Session->QueueDirective(MethodDirs[MIdx]);
        }
    }
    RowY += 28.0f;

    const float ChipW3 = (Area.Width - 32.0f) / 3.0f;
    for (uint32_t MIdx = 4u; MIdx < 7u; ++MIdx)
    {
        const Rectangle2D MRect { Area.X + 12.0f + static_cast<float>(MIdx - 4u) * (ChipW3 + 4.0f), RowY, ChipW3, 24.0f };
        const bool Active = (static_cast<uint32_t>(Session->ActiveUnwrapMethod) == MIdx);
        Surface.FillRectangle(MRect, Active ? ActivePill : InspectorBg, 5.0f);
        Surface.StrokeRectangle(MRect, Active ? IvoryAccent : BorderStroke, 5.0f, 1.0f);
        Surface.TextCentered(MRect, 10.5f, Active ? IvoryAccent : CrispWhite, MethodNames[MIdx]);
        if (Input.PrimaryPointer.Pressed && MRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
        {
            Session->ActiveUnwrapMethod = static_cast<UVProjectionMethod>(MIdx);
            Session->QueueDirective(MethodDirs[MIdx]);
        }
    }
    RowY += 32.0f;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, Area.Width - 24.0f, 30.0f }, "Execute Selected Unwrap / Projection", true, Input))
    {
        const uint32_t Idx = std::min<uint32_t>(6u, static_cast<uint32_t>(Session->ActiveUnwrapMethod));
        Session->QueueDirective(MethodDirs[Idx]);
    }
    RowY += 38.0f;

    // Seam, Pin, Stitch & Island Authoring Grid
    const float HalfW = (Area.Width - 30.0f) * 0.5f;
    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, HalfW, 26.0f }, "Mark Seam", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::MarkSelectedSeams);
    }
    if (RenderActionCapsule(Surface, { Area.X + 18.0f + HalfW, RowY, HalfW, 26.0f }, "Clear + Weld", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::ClearSelectedSeams);
    }
    RowY += 30.0f;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, HalfW, 26.0f }, "Stitch [V]", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::StitchSelectedSeams);
    }
    if (RenderActionCapsule(Surface, { Area.X + 18.0f + HalfW, RowY, HalfW, 26.0f }, "Split Island [Y]", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::SplitSelectedIsland);
    }
    RowY += 30.0f;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, HalfW, 26.0f }, "Pin UVs [P]", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::PinSelectedUVs);
    }
    if (RenderActionCapsule(Surface, { Area.X + 18.0f + HalfW, RowY, HalfW, 26.0f }, "Rectify Quads", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::RectifySelectedQuads);
    }
    RowY += 34.0f;

    char AngleStr[24]{};
    std::snprintf(AngleStr, sizeof(AngleStr), "%.0f", Session->SharpAngleThreshold);
    float NewAngle = Session->SharpAngleThreshold;
    RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, RowY, Area.Width - 24.0f,
        "Sharp Dihedral Seam Angle", AngleStr, "deg", 15.0f, 89.0f, Session->SharpAngleThreshold,
        201u, DraggingField, Input, NewAngle);
    Session->SharpAngleThreshold = NewAngle;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, HalfW, 28.0f }, "Mark Sharp", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::MarkSharpSeams);
    }
    if (RenderActionCapsule(Surface, { Area.X + 18.0f + HalfW, RowY, HalfW, 28.0f }, "Relax GPU (.slang)", true, Input))
    {
        Session->QueueDirective(UVCommandDirective::RelaxStretchGPU);
    }
}

void UVInspectorPanel::RenderUDIMSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    auto& Prefs   = Session->WorkspaceConfig.AccessUVPreferences();
    auto& TileCfg = Session->AccessUDIMTileSetting(Session->ActiveUDIMNumber);

    Surface.FillRectangle({ Area.X, Area.Y, Area.Width, 344.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, Area.Y, Area.Width, 344.0f }, BorderStroke, 6.0f, 1.0f);

    char HeaderTxt[64]{};
    std::snprintf(HeaderTxt, sizeof(HeaderTxt), "UDIM %u  |  RESOLUTION & BACKDROP", Session->ActiveUDIMNumber);
    Surface.Text({ Area.X + 12.0f, Area.Y + 10.0f }, 12.0f, SkyAccent, HeaderTxt);

    float RowY = Area.Y + 34.0f;
    Surface.Text({ Area.X + 12.0f, RowY }, 11.5f, CrispWhite, "Per-UDIM Target Resolution");
    const std::array<uint32_t, 5u> ResChoices {{ 512u, 1024u, 2048u, 4096u, 8192u }};
    const std::array<const char*, 5u> ResLabels {{ "512", "1K", "2K", "4K", "8K" }};
    const float RChipW = (Area.Width - 40.0f) / 5.0f;
    for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
    {
        const Rectangle2D RRect { Area.X + 12.0f + static_cast<float>(Idx) * (RChipW + 4.0f), RowY + 18.0f, RChipW, 24.0f };
        const bool Active = (TileCfg.ResolutionPx == ResChoices[Idx]);
        Surface.FillRectangle(RRect, Active ? ActivePill : InspectorBg, 5.0f);
        Surface.StrokeRectangle(RRect, Active ? IvoryAccent : BorderStroke, 5.0f, 1.0f);
        Surface.TextCentered(RRect, 10.5f, Active ? IvoryAccent : CrispWhite, ResLabels[Idx]);
        if (Input.PrimaryPointer.Pressed && RRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
        {
            TileCfg.ResolutionPx          = ResChoices[Idx];
            Prefs.DefaultTileResolutionPx = ResChoices[Idx];
            Session->FramebuffersDirty    = true;
        }
    }
    RowY += 52.0f;

    Surface.Text({ Area.X + 12.0f, RowY }, 11.5f, CrispWhite, "Texture Backdrop Preview");
    const std::array<const char*, 4u> BgLabels {{ "None", "Checker", "Trim", "Aniso" }};
    const float BChipW = (Area.Width - 36.0f) / 4.0f;
    for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const Rectangle2D BRect { Area.X + 12.0f + static_cast<float>(Idx) * (BChipW + 4.0f), RowY + 18.0f, BChipW, 24.0f };
        const bool Active = (static_cast<uint32_t>(TileCfg.BackdropPreset) == Idx);
        Surface.FillRectangle(BRect, Active ? ActivePill : InspectorBg, 5.0f);
        Surface.StrokeRectangle(BRect, Active ? SkyAccent : BorderStroke, 5.0f, 1.0f);
        Surface.TextCentered(BRect, 10.5f, Active ? IvoryAccent : CrispWhite, BgLabels[Idx]);
        if (Input.PrimaryPointer.Pressed && BRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
        {
            TileCfg.BackdropPreset     = static_cast<GeometricRaster::UVBackdropPreset>(Idx);
            Session->FramebuffersDirty = true;
        }
    }
    RowY += 52.0f;

    char DensityStr[24]{};
    std::snprintf(DensityStr, sizeof(DensityStr), "%.0f", Prefs.TargetTexelDensity);
    float NewDensity = Prefs.TargetTexelDensity;
    RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, RowY, Area.Width - 24.0f,
        "Target Physical Texel Density", DensityStr, "px/m", 128.0f, 2048.0f, Prefs.TargetTexelDensity,
        301u, DraggingField, Input, NewDensity);
    Prefs.TargetTexelDensity = NewDensity;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY + 4.0f, Area.Width - 24.0f, 30.0f }, "Equalize Texel Density", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::EqualizeTexelDensity);
    }
    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY + 40.0f, Area.Width - 24.0f, 34.0f }, "Auto-Distribute Across UDIMs", true, Input))
    {
        Session->QueueDirective(UVCommandDirective::AutoDistributeUDIMs);
    }
}

void UVInspectorPanel::RenderShadingSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    Surface.FillRectangle({ Area.X, Area.Y, Area.Width, 356.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, Area.Y, Area.Width, 356.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, Area.Y + 10.0f }, 12.0f, SkyAccent, "16-SLOT STUDIO MATCAP & PBR (.SLANG)");

    const std::array<ColorRgba, 16u> SwatchPalette {{
        { 196, 204, 218, 255 }, { 188, 180, 168, 255 }, { 186, 158, 140, 255 }, { 222, 172,  82, 255 },
        { 232, 236, 244, 255 }, {  72, 138, 228, 255 }, { 118, 138, 108, 255 }, {  48,  52,  62, 255 },
        {  86, 122, 152, 255 }, { 202, 198, 186, 255 }, { 220, 224, 230, 255 }, { 224, 128,  86, 255 },
        { 232, 236, 208, 255 }, {  88,  92, 102, 255 }, { 138, 204, 242, 255 }, { 142, 196, 168, 255 },
    }};

    const float CellGap = 6.0f;
    const float CellW   = (Area.Width - 24.0f - CellGap * 3.0f) / 4.0f;
    for (uint8_t Slot = 0u; Slot < 16u; ++Slot)
    {
        const uint32_t Col = Slot % 4u;
        const uint32_t Row = Slot / 4u;
        const Rectangle2D Cell {
            Area.X + 12.0f + static_cast<float>(Col) * (CellW + CellGap),
            Area.Y + 34.0f + static_cast<float>(Row) * 44.0f,
            CellW,
            38.0f,
        };
        Surface.FillRectangle(Cell, SwatchPalette[Slot], 6.0f);
        Surface.StrokeRectangle(Cell, BorderStroke, 6.0f, 1.0f);

        char SlotTxt[16]{};
        std::snprintf(SlotTxt, sizeof(SlotTxt), "M%u", Slot);
        Surface.Text({ Cell.X + 6.0f, Cell.Y + 6.0f }, 10.5f, { 16, 18, 22, 255 }, SlotTxt);

        if (Input.PrimaryPointer.Pressed && Cell.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
        {
            Session->ApplyMatCapToActiveSelection(Slot);
        }
    }

    float RowY = Area.Y + 216.0f;
    char RoughStr[24]{}, MetalStr[24]{};
    std::snprintf(RoughStr, sizeof(RoughStr), "%.2f", SurfaceRoughness);
    std::snprintf(MetalStr, sizeof(MetalStr), "%.2f", SurfaceMetallic);

    float NewRough = SurfaceRoughness;
    RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, RowY, Area.Width - 24.0f,
        "Surface Roughness", RoughStr, "", 0.04f, 1.0f, SurfaceRoughness,
        401u, DraggingField, Input, NewRough);

    float NewMetal = SurfaceMetallic;
    (void)RenderSliderControlRow(
        Surface, Area.X + 12.0f, RowY, Area.Width - 24.0f,
        "Surface Metallic", MetalStr, "", 0.0f, 1.0f, SurfaceMetallic,
        402u, DraggingField, Input, NewMetal);

    if (std::abs(NewRough - SurfaceRoughness) > 0.005f || std::abs(NewMetal - SurfaceMetallic) > 0.005f)
    {
        SurfaceRoughness = NewRough;
        SurfaceMetallic  = NewMetal;
        Session->ApplySurfaceRoughnessMetallic(SurfaceRoughness, SurfaceMetallic, SurfaceMatBlend);
    }
}

void UVInspectorPanel::RenderCheckSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();
    Surface.FillRectangle({ Area.X, Area.Y, Area.Width, 356.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, Area.Y, Area.Width, 356.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, Area.Y + 10.0f }, 12.0f, SkyAccent, "GPU DISTORTION & OVERLAY DIAGNOSTICS");

    // Live GPU KPI Status Badge
    const bool CleanMetrics = (Session->LatestMetrics.FlippedFaceCount == 0u && Session->LatestMetrics.OverlapFaceCount == 0u);
    const Rectangle2D KpiBadge { Area.X + 12.0f, Area.Y + 34.0f, Area.Width - 24.0f, 48.0f };
    Surface.FillRectangle(KpiBadge, InspectorBg, 6.0f);
    Surface.StrokeRectangle(KpiBadge, CleanMetrics ? EmeraldGood : WarnCoral, 6.0f, 1.2f);

    float MaxStretch = Session->LatestMetrics.MeanL2Stretch;
    for (const float Val : Session->LatestMetrics.PerFaceStretch)
    {
        MaxStretch = std::max(MaxStretch, Val);
    }
    char Line1[96]{};
    std::snprintf(Line1, sizeof(Line1), "Mean L2 Stretch: %.3f  (Max %.3f)", Session->LatestMetrics.MeanL2Stretch, MaxStretch);
    Surface.Text({ KpiBadge.X + 10.0f, KpiBadge.Y + 7.0f }, 11.5f, CleanMetrics ? EmeraldGood : WarnCoral, Line1);

    char Line2[96]{};
    std::snprintf(
        Line2,
        sizeof(Line2),
        "Flipped Faces: %u   |   Overlap Faces: %u",
        Session->LatestMetrics.FlippedFaceCount,
        Session->LatestMetrics.OverlapFaceCount);
    Surface.Text({ KpiBadge.X + 10.0f, KpiBadge.Y + 26.0f }, 11.0f, CrispWhite, Line2);

    const float HalfW = (Area.Width - 30.0f) * 0.5f;
    if (RenderActionCapsule(Surface, { Area.X + 12.0f, Area.Y + 90.0f, HalfW, 26.0f }, "Select Flipped", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::SelectFlippedFaces);
    }
    if (RenderActionCapsule(Surface, { Area.X + 18.0f + HalfW, Area.Y + 90.0f, HalfW, 26.0f }, "Select Overlaps", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::SelectOverlappingFaces);
    }

    float ToggleY = Area.Y + 126.0f;
    Surface.Text({ Area.X + 12.0f, ToggleY + 4.0f }, 11.5f, CrispWhite, "Wireframe Overlay");
    if (ExclusiveSelector::ConstructCompact(
            Surface, { Area.X + Area.Width - 60.0f, ToggleY, 48.0f, 22.0f }, Prefs.WireframeOverlayEnabled, Input.PrimaryPointer).Clicked)
    {
        Prefs.WireframeOverlayEnabled = !Prefs.WireframeOverlayEnabled;
        Session->FramebuffersDirty    = true;
    }

    ToggleY += 30.0f;
    Surface.Text({ Area.X + 12.0f, ToggleY + 4.0f }, 11.5f, CrispWhite, "Seam Overlay (Default Off)");
    if (ExclusiveSelector::ConstructCompact(
            Surface, { Area.X + Area.Width - 60.0f, ToggleY, 48.0f, 22.0f }, Prefs.SeamOverlayEnabled, Input.PrimaryPointer).Clicked)
    {
        Prefs.SeamOverlayEnabled   = !Prefs.SeamOverlayEnabled;
        Session->FramebuffersDirty = true;
    }

    ToggleY += 30.0f;
    Surface.Text({ Area.X + 12.0f, ToggleY + 4.0f }, 11.5f, CrispWhite, "GPU Pin Beacons");
    if (ExclusiveSelector::ConstructCompact(
            Surface, { Area.X + Area.Width - 60.0f, ToggleY, 48.0f, 22.0f }, Session->RenderOptions2D.PinOverlayEnabled, Input.PrimaryPointer).Clicked)
    {
        Session->RenderOptions2D.PinOverlayEnabled = !Session->RenderOptions2D.PinOverlayEnabled;
        Session->FramebuffersDirty                 = true;
    }

    ToggleY += 30.0f;
    const bool HeatmapOn = (Session->RenderOptions2D.DisplayMode == GeometricRaster::UVCanvasDisplayMode::StretchHeatmap);
    Surface.Text({ Area.X + 12.0f, ToggleY + 4.0f }, 11.5f, CrispWhite, "L2 Stretch Heatmap");
    if (ExclusiveSelector::ConstructCompact(
            Surface, { Area.X + Area.Width - 60.0f, ToggleY, 48.0f, 22.0f }, HeatmapOn, Input.PrimaryPointer).Clicked)
    {
        const bool EnableHeatmap = !HeatmapOn;
        Session->RenderOptions2D.DisplayMode = EnableHeatmap
            ? GeometricRaster::UVCanvasDisplayMode::StretchHeatmap
            : GeometricRaster::UVCanvasDisplayMode::PriorityLayerFill;
        Session->FramebuffersDirty = true;
    }

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, Area.Y + 306.0f, Area.Width - 24.0f, 34.0f }, "Re-Evaluate Stretch & Overlaps (GPU)", true, Input))
    {
        Session->QueueDirective(UVCommandDirective::ValidateDistortionGPU);
    }
}

void UVInspectorPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton("##UVInspectorCanvas", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)));

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             WindowSurface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(WindowSurface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces
