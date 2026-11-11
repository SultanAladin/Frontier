/*============================================================================================================================================
                                                          UVINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of 5-tab UVInspectorPanel with Concave Packing Engine, Conformal Unwrap, Multi-UDIM, 16-MatCap & GPU Check tabs

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
    Surface.TextCentered(Rect, 12.0f, PrimaryStyle ? IvoryAccent : CrispWhite, Caption);
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
    return TopY + 54.0f;
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
    float CurY = Area.Y;
    auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();

    // Selected Priority Layer Share Slider
    Surface.FillRectangle({ Area.X, CurY, Area.Width, 96.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, CurY, Area.Width, 96.0f }, BorderStroke, 6.0f, 1.0f);

    const auto* ActiveLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
    if (ActiveLayer != nullptr)
    {
        Surface.Text({ Area.X + 12.0f, CurY + 10.0f }, 12.0f, IvoryAccent, ActiveLayer->LayerTitle.c_str());
        char NumStr[24]{};
        std::snprintf(NumStr, sizeof(NumStr), "%.0f", ActiveLayer->TargetSharePercent);
        float RequestedShare = ActiveLayer->TargetSharePercent;
        (void)RenderSliderControlRow(
            Surface, Area.X + 12.0f, CurY + 32.0f, Area.Width - 24.0f,
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
    CurY += 108.0f;

    // Concave Packing Engine Section
    Surface.FillRectangle({ Area.X, CurY, Area.Width, 238.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, CurY, Area.Width, 238.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, CurY + 10.0f }, 12.0f, SkyAccent, "CONCAVE PACKING ENGINE (GPU SKYLINE)");

    char MarginStr[24]{};
    std::snprintf(MarginStr, sizeof(MarginStr), "%u", Prefs.IslandMarginPixels);
    float MarginFloat = static_cast<float>(Prefs.IslandMarginPixels);
    float NewMargin   = MarginFloat;
    float RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, CurY + 34.0f, Area.Width - 24.0f,
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
    RowY += 52.0f;

    Surface.Text({ Area.X + 12.0f, RowY + 4.0f }, 11.5f, CrispWhite, "Equalize Intra-Layer Scale");
    if (ExclusiveSelector::ConstructCompact(
            Surface,
            { Area.X + Area.Width - 60.0f, RowY, 48.0f, 24.0f },
            Prefs.EqualizeIntraLayerScale,
            Input.PrimaryPointer).Clicked)
    {
        Prefs.EqualizeIntraLayerScale = !Prefs.EqualizeIntraLayerScale;
    }

    if (RenderActionCapsule(
            Surface,
            { Area.X + 12.0f, CurY + 190.0f, Area.Width - 24.0f, 34.0f },
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
    Surface.FillRectangle({ Area.X, CurY, Area.Width, 260.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, CurY, Area.Width, 260.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, CurY + 10.0f }, 12.0f, SkyAccent, "SEAM CUT, CONFORMAL UNWRAP & GPU RELAX");

    char AngleStr[24]{};
    std::snprintf(AngleStr, sizeof(AngleStr), "%.0f", Session->SharpAngleThreshold);
    float NewAngle = Session->SharpAngleThreshold;
    float RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, CurY + 34.0f, Area.Width - 24.0f,
        "Sharp Dihedral Seam Angle", AngleStr, "deg", 15.0f, 89.0f, Session->SharpAngleThreshold,
        201u, DraggingField, Input, NewAngle);
    Session->SharpAngleThreshold = NewAngle;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, Area.Width - 24.0f, 30.0f }, "Mark Dihedral Sharp Seams", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::MarkSharpSeams);
    }
    RowY += 38.0f;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY, Area.Width - 24.0f, 32.0f }, "Conformal Flatten (Harmonic LSCM)", true, Input))
    {
        Session->QueueDirective(UVCommandDirective::UnwrapConformal);
    }
    RowY += 42.0f;

    char IterStr[24]{};
    std::snprintf(IterStr, sizeof(IterStr), "%u", Session->RelaxIterations);
    float NewIter = static_cast<float>(Session->RelaxIterations);
    RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, RowY, Area.Width - 24.0f,
        "GPU Jacobi Relax Passes", IterStr, "itr", 2.0f, 48.0f, static_cast<float>(Session->RelaxIterations),
        202u, DraggingField, Input, NewIter);
    Session->RelaxIterations = static_cast<uint32_t>(std::round(NewIter));

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, CurY + 218.0f, Area.Width - 24.0f, 30.0f }, "Relax Stretch on GPU (.slang)", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::RelaxStretchGPU);
    }
}

void UVInspectorPanel::RenderUDIMSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();
    Surface.FillRectangle({ Area.X, Area.Y, Area.Width, 232.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, Area.Y, Area.Width, 232.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, Area.Y + 10.0f }, 12.0f, SkyAccent, "MULTI-UDIM TEXEL DENSITY & TILES");

    char DensityStr[24]{};
    std::snprintf(DensityStr, sizeof(DensityStr), "%.0f", Prefs.TargetTexelDensity);
    float NewDensity = Prefs.TargetTexelDensity;
    float RowY = RenderSliderControlRow(
        Surface, Area.X + 12.0f, Area.Y + 34.0f, Area.Width - 24.0f,
        "Target Physical Texel Density", DensityStr, "px/m", 128.0f, 2048.0f, Prefs.TargetTexelDensity,
        301u, DraggingField, Input, NewDensity);
    Prefs.TargetTexelDensity = NewDensity;

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY + 4.0f, Area.Width - 24.0f, 32.0f }, "Equalize Texel Density", false, Input))
    {
        Session->QueueDirective(UVCommandDirective::EqualizeTexelDensity);
    }
    if (RenderActionCapsule(Surface, { Area.X + 12.0f, RowY + 44.0f, Area.Width - 24.0f, 34.0f }, "Auto-Distribute Across UDIMs", true, Input))
    {
        Session->QueueDirective(UVCommandDirective::AutoDistributeUDIMs);
    }
}

void UVInspectorPanel::RenderShadingSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    Surface.FillRectangle({ Area.X, Area.Y, Area.Width, 260.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, Area.Y, Area.Width, 260.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, Area.Y + 10.0f }, 12.0f, SkyAccent, "16-SLOT STUDIO MATCAP PALETTE (.SLANG)");

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
            Area.Y + 36.0f + static_cast<float>(Row) * 46.0f,
            CellW,
            40.0f,
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
}

void UVInspectorPanel::RenderCheckSection(
    PixelSpace&             Surface,
    Rectangle2D             Area,
    const PaneInputReading& Input)
{
    auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();
    Surface.FillRectangle({ Area.X, Area.Y, Area.Width, 248.0f }, SectionCard, 6.0f);
    Surface.StrokeRectangle({ Area.X, Area.Y, Area.Width, 248.0f }, BorderStroke, 6.0f, 1.0f);
    Surface.Text({ Area.X + 12.0f, Area.Y + 10.0f }, 12.0f, SkyAccent, "GPU DISTORTION & OVERLAY DIAGNOSTICS");

    char Line1[96]{};
    std::snprintf(Line1, sizeof(Line1), "Mean L2 Stretch: %.3f  (Ideal 1.000)", Session->LatestMetrics.MeanL2Stretch);
    Surface.Text({ Area.X + 12.0f, Area.Y + 38.0f }, 12.0f, EmeraldGood, Line1);

    char Line2[96]{};
    std::snprintf(
        Line2,
        sizeof(Line2),
        "Flipped Faces: %u   |   Overlap Faces: %u",
        Session->LatestMetrics.FlippedFaceCount,
        Session->LatestMetrics.OverlapFaceCount);
    Surface.Text({ Area.X + 12.0f, Area.Y + 62.0f }, 12.0f, CrispWhite, Line2);

    float ToggleY = Area.Y + 98.0f;
    Surface.Text({ Area.X + 12.0f, ToggleY + 4.0f }, 11.5f, CrispWhite, "Wireframe Overlay");
    if (ExclusiveSelector::ConstructCompact(
            Surface, { Area.X + Area.Width - 60.0f, ToggleY, 48.0f, 24.0f }, Prefs.WireframeOverlayEnabled, Input.PrimaryPointer).Clicked)
    {
        Prefs.WireframeOverlayEnabled = !Prefs.WireframeOverlayEnabled;
        Session->FramebuffersDirty    = true;
    }

    ToggleY += 34.0f;
    Surface.Text({ Area.X + 12.0f, ToggleY + 4.0f }, 11.5f, CrispWhite, "Seam Overlay (Default Off)");
    if (ExclusiveSelector::ConstructCompact(
            Surface, { Area.X + Area.Width - 60.0f, ToggleY, 48.0f, 24.0f }, Prefs.SeamOverlayEnabled, Input.PrimaryPointer).Clicked)
    {
        Prefs.SeamOverlayEnabled   = !Prefs.SeamOverlayEnabled;
        Session->FramebuffersDirty = true;
    }

    if (RenderActionCapsule(Surface, { Area.X + 12.0f, Area.Y + 194.0f, Area.Width - 24.0f, 34.0f }, "Re-Evaluate Stretch & Overlaps (GPU)", true, Input))
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
