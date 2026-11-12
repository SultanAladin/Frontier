/*============================================================================================================================================
                                                          UVINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVInspectorPanel matching Experimental/UVEditor across Pack, Unwrap, UDIM, Shading & Check tabs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVInspectorPanel.h"

#include "Engine/Components/Controls/ControlTooltip.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Workspaces/UV/UVPaneAppearance.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Frontier::Workspaces
{

namespace
{

void ConstructMetricTrio(
    PixelSpace&                        Surface,
    Rectangle2D                        Box,
    const char*                        Val0, const char* Cap0,
    const char*                        Val1, const char* Cap1,
    const char*                        Val2, const char* Cap2,
    const UVAppearance::UVThemeColors& Theme)
{
    Surface.FillRectangle(Box, Theme.SectionBg, 16.0f);
    Surface.StrokeRectangle(Box, Theme.BorderSubtle, 16.0f, 1.0f);

    const float ColW = Box.Width / 3.0f;
    Surface.StrokeLine({ Box.X + ColW, Box.Y + 10.0f }, { Box.X + ColW, Box.Bottom() - 10.0f }, Theme.BorderSubtle, 1.0f);
    Surface.StrokeLine({ Box.X + ColW * 2.0f, Box.Y + 10.0f }, { Box.X + ColW * 2.0f, Box.Bottom() - 10.0f }, Theme.BorderSubtle, 1.0f);

    const std::array<std::pair<const char*, const char*>, 3u> Cols {{
        { Val0, Cap0 },
        { Val1, Cap1 },
        { Val2, Cap2 },
    }};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        const float CellX = Box.X + ColW * static_cast<float>(Idx);
        const float VW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 13.5f, Cols[Idx].first);
        const float CW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, Cols[Idx].second);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { CellX + (ColW - VW) * 0.5f, Box.Y + 10.0f },
            13.5f,
            (Idx == 0u) ? Theme.AccentFill : Theme.TextPrimary,
            Cols[Idx].first);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { CellX + std::max(4.0f, (ColW - CW) * 0.5f), Box.Y + 33.0f },
            ColW - 8.0f,
            8.8f,
            Theme.TextMuted,
            Cols[Idx].second);
    }
}

void ConstructPackingMetricCards(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const std::array<std::array<std::string_view, 3u>, 3u>& Cards,
    const UVAppearance::UVThemeColors& Theme)
{
    // Match the HTML .kpi-trio: three standalone cards with no enclosing card/background.
    constexpr float Gap = 8.0f;
    const float CardW = (Bounds.Width - Gap * 2.0f) / 3.0f;
    for (uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const float X = Bounds.X + static_cast<float>(Index) * (CardW + Gap);
        const Rectangle2D Card{ X, Bounds.Y, CardW, Bounds.Height };
        Surface.FillRectangle(Card, ColorRgba{ 26, 26, 26, 255 }, 16.0f);
        Surface.StrokeRectangle(Card, ColorRgba{ 255, 255, 255, 15 }, 16.0f, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 11.0f, Bounds.Y + 9.0f }, CardW - 22.0f, 9.5f,
            Theme.TextMuted, Cards[Index][0]);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 11.0f, Bounds.Y + 24.0f }, CardW - 22.0f, 18.0f,
            Index == 0u ? Theme.AccentFill : Theme.TextPrimary, Cards[Index][1]);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 11.0f, Bounds.Y + 48.0f }, CardW - 22.0f, 9.0f,
            Theme.TextMuted, Cards[Index][2]);
    }
}

[[nodiscard]] float EvaluateUDIMFillPercent(const GeometrySpace::PolygonNetwork& Net, uint32_t TargetUDIM) noexcept
{
    double AreaSum = 0.0;
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Net.Faces.size()); ++FaceIdx)
    {
        const auto& Face = Net.Faces[FaceIdx];
        if (Face.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            continue;
        }
        const auto UV = Net.Edges[Face.BoundaryLoopEdgeIndex].ParameterUV;
        const int  TU = std::clamp(static_cast<int>(std::floor(UV.U)), 0, 9);
        const int  TV = std::max(0, static_cast<int>(std::floor(UV.V)));
        const uint32_t Code = static_cast<uint32_t>(1001 + TU + 10 * TV);
        if (Code == TargetUDIM)
        {
            AreaSum += std::max(0.0f, Net.EvaluateFaceSignedAreaUV(FaceIdx));
        }
    }
    return static_cast<float>(std::clamp(AreaSum * 100.0, 0.0, 100.0));
}

} // namespace

void UVInspectorPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session       = TargetSession;
    ScrollOffsetY = 0.0f;
    CachedNetworkRevision = UINT64_MAX;
    CachedUVRevision = UINT64_MAX;
    CachedChartCount = 0u;
    CachedTabContentHeights.fill(0.0f);
    LayerNameEditor = {};
    EditingLayerOrdinal = 0u;
    ActiveLayerDropdownOpen = false;
    TargetUdimDropdownOpen = false;
    PackingScopeDropdownOpen = false;
    LayerColorPickerOpen = false;
}

float UVInspectorPanel::RenderPackingTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    Rectangle2D             PopupClip,
    const PaneInputReading& Input,
    double                  Δt)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;
    if (Input.EscapePressed)
    {
        ActiveLayerDropdownOpen = false;
        TargetUdimDropdownOpen = false;
        PackingScopeDropdownOpen = false;
        LayerColorPickerOpen = false;
    }

    float CurY = StartY;
    Rectangle2D ActiveLayerDropdownBounds{};
    Rectangle2D TargetUdimDropdownBounds{};
    Rectangle2D PackingScopeDropdownBounds{};

    const float FillPct     = EvaluateUDIMFillPercent(Session->Asset.Network, Session->ActiveUDIMNumber);
    const float RemPct      = Session->Asset.Layers.DeriveRemainderSharePercent();
    const auto  TileCfg     = Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber);
    const auto  Islands     = SeamCutGenerator::PartitionConnectedUVIslands(Session->Asset.Network);
    const auto  SelectedFaces = Session->SelectionMask.CollectSelectedFaces();
    std::vector<uint8_t> FaceSelected(Session->Asset.Network.Faces.size(), 0u);
    for (const uint32_t FaceIndex : SelectedFaces)
    {
        if (FaceIndex < FaceSelected.size())
        {
            FaceSelected[FaceIndex] = 1u;
        }
    }

    uint32_t TotalIslandsInTile = 0u;
    uint32_t SelectedIslandsInTile = 0u;
    uint32_t SelectedFacesInTile = 0u;
    for (const UVIslandPartition& Island : Islands)
    {
        if (Island.AssignedUDIMNumber != Session->ActiveUDIMNumber)
        {
            continue;
        }
        ++TotalIslandsInTile;
        bool IslandSelected = false;
        for (const uint32_t FaceIndex : Island.FaceIndices)
        {
            if (FaceIndex < FaceSelected.size() && FaceSelected[FaceIndex] != 0u)
            {
                ++SelectedFacesInTile;
                IslandSelected = true;
            }
        }
        SelectedIslandsInTile += IslandSelected ? 1u : 0u;
    }

    char SelectedCount[32]{}, SelectedSub[48]{}, FillStr[24]{}, FillSub[48]{}, RemStr[24]{}, RemSub[32]{};
    std::snprintf(SelectedCount, sizeof(SelectedCount), "%u / %u", SelectedIslandsInTile, TotalIslandsInTile);
    if (SelectedFacesInTile > 0u)
    {
        std::snprintf(SelectedSub, sizeof(SelectedSub), "%u faces selected", SelectedFacesInTile);
    }
    else
    {
        std::snprintf(SelectedSub, sizeof(SelectedSub), "Objects in UDIM %u", Session->ActiveUDIMNumber);
    }
    std::snprintf(FillStr, sizeof(FillStr), "%.1f%%", FillPct);
    std::snprintf(FillSub, sizeof(FillSub), "%u charts · %u px", TotalIslandsInTile, TileCfg.ResolutionPx);
    std::snprintf(RemStr, sizeof(RemStr), "%.0f%%", RemPct);
    std::snprintf(RemSub, sizeof(RemSub), "%zu layers active", Session->Asset.Layers.InspectLayers().size());

    ConstructPackingMetricCards(
        Surface,
        { LeftX, CurY, InnerW, 66.0f },
        {{{ std::string("Selected · UDIM ") + std::to_string(Session->ActiveUDIMNumber), SelectedCount, SelectedSub },
          { std::string("UDIM ") + std::to_string(Session->ActiveUDIMNumber) + " Fill", FillStr, FillSub },
          { "Auto Remainder", RemStr, RemSub }}},
        Theme);
    CurY += 82.0f;

    // Section 1: Importance Space Allocation, matching the HTML inspector's controls.
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 392.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;
        ConstructSectionHeading(Surface, SX, SY, SW, "Importance Space Allocation", Theme, {}, ControlIcon::LayersSlabs);
        SY += 45.0f;

        const auto Layers = Session->Asset.Layers.InspectLayers();
        const auto* CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        const std::string ActiveLabel = CurrentLayer != nullptr
            ? CurrentLayer->LayerTitle + " (" + std::to_string(static_cast<uint32_t>(std::round(CurrentLayer->TargetSharePercent))) + "%)"
            : std::string("Unassigned Remainder");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Active Layer");
        ActiveLayerDropdownBounds = { SX, SY + 17.0f, SW, 28.0f };
        const OptionalTooltip ActiveLayerTip = OptionalTooltip::FromTitled(
            "Active Layer", "Choose which priority allocation layer is active and select its faces in the workspace.");
        const ControlContact ActiveLayerButton = DropdownSelector::ConstructButton(
            Surface, ActiveLayerDropdownBounds, ActiveLabel.c_str(), ActiveLayerDropdownOpen, Input.PrimaryPointer, 1.0f,
            ControlIcon::LayersSlabs, ActiveLayerTip);
        if (ActiveLayerButton.Clicked)
        {
            ActiveLayerDropdownOpen = !ActiveLayerDropdownOpen;
            TargetUdimDropdownOpen = false;
        }
        SY += 52.0f;

        CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        if (CurrentLayer != nullptr)
        {
            float NormShare = CurrentLayer->TargetSharePercent * 0.01f;
            char ShareVal[24]{};
            std::snprintf(ShareVal, sizeof(ShareVal), "%.0f%%", CurrentLayer->TargetSharePercent);
            const OptionalTooltip ShareTip = OptionalTooltip::FromTitled(
                "Space Share", "Set the active layer's target share of the UV packing area. Other explicit layer shares limit the available range.");
            if (ConstructSliderControl(
                    Surface, { SX, SY, SW, 28.0f }, "Space Share", ShareVal, NormShare, Theme, Cursor, Down, Pressed,
                    ColorRgba{ 0u, 0u, 0u, 0u }, ShareTip))
            {
                Session->AdjustSelectedLayerShare(NormShare * 100.0f);
            }
        }
        else
        {
            float FixedNorm = RemPct * 0.01f;
            ConstructSliderControl(
                Surface, { SX, SY, SW, 28.0f }, "Remainder Share (Auto)", RemStr, FixedNorm,
                Theme, Cursor, false, false,
                ColorRgba{ 0u, 0u, 0u, 0u },
                OptionalTooltip::FromText("Unassigned faces automatically receive the remaining allocation budget."));
        }
        SY += 54.0f;

        const Rectangle2D NameBounds{ SX, SY + 17.0f, SW, 28.0f };
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Layer Name");
        CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        if (EditingLayerOrdinal != Session->SelectedLayerOrdinal)
        {
            ActiveLayerDropdownOpen = false;
            TargetUdimDropdownOpen = false;
            PackingScopeDropdownOpen = false;
            LayerColorPickerOpen = false;
            LayerNameEditor = {};
            EditingLayerOrdinal = Session->SelectedLayerOrdinal;
            if (CurrentLayer != nullptr)
            {
                LayerNameEditor.Begin(CurrentLayer->LayerTitle.c_str());
                LayerNameEditor.Active = false;
            }
        }
        const OptionalTooltip NameTip = OptionalTooltip::FromTitled(
            "Layer Name", "Rename the active priority layer. The updated name is shared with the UV outliner.");
        const ControlContact NameContact = TextInputField::Construct(
            Surface, NameBounds, LayerNameEditor, Input.PrimaryPointer, 11.5f, 1.0f, NameTip);
        if (NameContact.Clicked && CurrentLayer != nullptr)
        {
            LayerNameEditor.Begin(CurrentLayer->LayerTitle.c_str());
            EditingLayerOrdinal = Session->SelectedLayerOrdinal;
        }
        if (LayerNameEditor.Active)
        {
            WorkspaceAppearance::ForwardInputToTextBuffer(LayerNameEditor, Input, Δt);
            if (Input.EscapePressed)
            {
                LayerNameEditor.Cancel();
                if (CurrentLayer != nullptr && EditingLayerOrdinal == Session->SelectedLayerOrdinal)
                {
                    Session->RenameSelectedLayer(LayerNameEditor.Text);
                }
            }
            else if (Input.EnterPressed || (Input.PrimaryPointer.Pressed && !NameBounds.Contains(Cursor)))
            {
                LayerNameEditor.Finalize();
            }
            if (LayerNameEditor.Active && CurrentLayer != nullptr && LayerNameEditor.Length > 0u &&
                CurrentLayer->LayerTitle != LayerNameEditor.Text)
            {
                Session->RenameSelectedLayer(LayerNameEditor.Text);
            }
            if (LayerNameEditor.Finalized && CurrentLayer != nullptr)
            {
                Session->RenameSelectedLayer(LayerNameEditor.Text);
                LayerNameEditor.Finalized = false;
            }
        }
        SY += 62.0f;

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY + 9.0f }, 11.5f, Theme.TextMuted, "Layer Colour");
        constexpr std::array<Mathematics::Scalar3D, 6u> Swatches {{
            { 0.937f, 0.957f, 0.812f }, { 0.620f, 0.812f, 1.000f }, { 0.608f, 0.890f, 0.769f },
            { 0.949f, 0.757f, 0.490f }, { 0.898f, 0.596f, 0.608f }, { 0.769f, 0.655f, 0.906f },
        }};
        constexpr std::array<const char*, 6u> SwatchNames {{ "Studio Ivory", "Sky", "Mint", "Amber", "Coral", "Lavender" }};
        constexpr std::array<const char*, 6u> SwatchHex {{ "#EFF4CF", "#9ECFFF", "#9BE3C4", "#F2C17D", "#E5989B", "#C4A7E7" }};
        uint32_t ActiveSwatch = static_cast<uint32_t>(Swatches.size());
        if (CurrentLayer != nullptr)
        {
            for (uint32_t Slot = 0u; Slot < Swatches.size(); ++Slot)
            {
                const auto& Tint = CurrentLayer->LinearTintRGB;
                if (std::abs(Tint.X - Swatches[Slot].X) < 0.015f &&
                    std::abs(Tint.Y - Swatches[Slot].Y) < 0.015f &&
                    std::abs(Tint.Z - Swatches[Slot].Z) < 0.015f)
                {
                    ActiveSwatch = Slot;
                    break;
                }
            }
        }
        const float ColorPillW = std::min(210.0f, SW);
        const Rectangle2D ColorPill{ SX + SW - ColorPillW, SY, ColorPillW, 30.0f };
        Surface.FillRectangle(ColorPill, ColorRgba{ 20, 20, 20, 255 }, 15.0f);
        Surface.StrokeRectangle(ColorPill, ColorRgba{ 48, 48, 48, 255 }, 15.0f, 1.0f);
        const ColorRgba CurrentTint = CurrentLayer != nullptr
            ? LinearTintToRgba(CurrentLayer->LinearTintRGB)
            : ColorRgba{ 128, 128, 128, 255 };
        Surface.FillCircle({ ColorPill.X + 16.0f, ColorPill.Y + 15.0f }, 8.0f, CurrentTint);
        Surface.StrokeCircle({ ColorPill.X + 16.0f, ColorPill.Y + 15.0f }, 8.0f, ColorRgba{ 255, 255, 255, 70 }, 1.0f);
        const std::string_view ColorName = CurrentLayer == nullptr
            ? std::string_view("No Layer")
            : (ActiveSwatch < Swatches.size() ? std::string_view(SwatchNames[ActiveSwatch]) : std::string_view("Custom"));
        const std::string_view HexCode = ActiveSwatch < Swatches.size() ? std::string_view(SwatchHex[ActiveSwatch]) : std::string_view("CUSTOM");
        const float HexWidth = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, HexCode);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { ColorPill.X + 31.0f, ColorPill.Y + 9.0f },
            ColorPill.Width - HexWidth - 49.0f, 10.5f, Theme.TextPrimary, ColorName);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { ColorPill.Right() - HexWidth - 9.0f, ColorPill.Y + 10.0f },
            8.8f, Theme.TextMuted, HexCode);
        const bool ColorPillHovered = ColorPill.Contains(Cursor);
        if (ColorPillHovered)
        {
            RecordControlTooltip(ColorPill, "Layer Colour", OptionalTooltip::FromTitled(
                "Layer Colour", "Choose a layer tint from the shared studio palette."), "Open the layer colour palette.");
        }
        if (ColorPillHovered && Pressed)
        {
            LayerColorPickerOpen = !LayerColorPickerOpen;
        }
        const Rectangle2D ColorPaletteBounds{ ColorPill.Right() - 152.0f, SY + 32.0f, 152.0f, 30.0f };
        if (LayerColorPickerOpen)
        {
            Surface.FillRectangle(ColorPaletteBounds, ColorRgba{ 20, 20, 20, 255 }, 12.0f);
            Surface.StrokeRectangle(ColorPaletteBounds, ColorRgba{ 48, 48, 48, 255 }, 12.0f, 1.0f);
            for (uint32_t Slot = 0u; Slot < Swatches.size(); ++Slot)
            {
                const Rectangle2D SwatchHit{ ColorPaletteBounds.X + 7.0f + static_cast<float>(Slot) * 23.0f,
                    ColorPaletteBounds.Y + 7.0f, 18.0f, 16.0f };
                const ColorRgba SwatchColor = LinearTintToRgba(Swatches[Slot]);
                Surface.FillRectangle(SwatchHit, SwatchColor, 8.0f);
                if (Slot == ActiveSwatch)
                {
                    Surface.StrokeRectangle(SwatchHit, Theme.PrimaryFill, 8.0f, 1.5f);
                }
                if (SwatchHit.Contains(Cursor))
                {
                    ControlAppearance::RecordHoveredTooltip(
                        SwatchHit, OptionalTooltip::FromTitled(SwatchNames[Slot], SwatchHex[Slot]));
                    if (Pressed && CurrentLayer != nullptr)
                    {
                        Session->AssignSelectedLayerTint(Swatches[Slot]);
                        LayerColorPickerOpen = false;
                    }
                }
            }
            if (Pressed && !ColorPaletteBounds.Contains(Cursor) && !ColorPill.Contains(Cursor))
            {
                LayerColorPickerOpen = false;
            }
        }
        SY += 68.0f;

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Target UDIM");
        CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        const uint32_t TargetUDIM = CurrentLayer != nullptr ? CurrentLayer->TargetUDIMNumber : 0u;
        const std::string TargetLabel = TargetUDIM == 0u ? "Auto UDIM" : "UDIM " + std::to_string(TargetUDIM);
        TargetUdimDropdownBounds = { SX, SY + 17.0f, SW, 28.0f };
        const OptionalTooltip TargetTip = OptionalTooltip::FromTitled(
            "Target UDIM", "Pin the active layer's UV islands to a tile. Choosing an explicit UDIM translates its islands into that tile.");
        if (CurrentLayer != nullptr)
        {
            const ControlContact TargetButton = DropdownSelector::ConstructButton(
                Surface, TargetUdimDropdownBounds, TargetLabel.c_str(), TargetUdimDropdownOpen, Input.PrimaryPointer, 1.0f,
                ControlIcon::LayoutGrid, TargetTip);
            if (TargetButton.Clicked)
            {
                TargetUdimDropdownOpen = !TargetUdimDropdownOpen;
                ActiveLayerDropdownOpen = false;
                PackingScopeDropdownOpen = false;
            }
        }
        else
        {
            Surface.FillRectangle(TargetUdimDropdownBounds, Theme.WellBg, 14.0f);
            Surface.StrokeRectangle(TargetUdimDropdownBounds, Theme.BorderSubtle, 14.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { TargetUdimDropdownBounds.X + 12.0f, TargetUdimDropdownBounds.Y + 8.0f },
                TargetUdimDropdownBounds.Width - 24.0f, 9.5f, Theme.TextMuted, "Select a priority layer first");
            if (TargetUdimDropdownBounds.Contains(Cursor))
            {
                ControlAppearance::RecordHoveredTooltip(
                    TargetUdimDropdownBounds, OptionalTooltip::FromText("Choose a priority layer to set or move a Target UDIM."));
            }
            TargetUdimDropdownOpen = false;
        }
        SY += 47.0f;

        const float HalfW = (SW - 8.0f) * 0.5f;
        if (ConstructCapsuleButton(
                Surface, { SX, SY, HalfW, 33.0f }, "Bind Selected Faces", Theme, Cursor, Pressed,
                false, false, 9.6f, -1.0f,
                OptionalTooltip::FromText("Assign selected faces to the active priority allocation layer.")))
        {
            if (Session->SelectedLayerOrdinal != 0u)
            {
                Session->BindSelectionToPriorityLayer(Session->SelectedLayerOrdinal);
            }
        }
        if (ConstructCapsuleButton(
                Surface, { SX + HalfW + 8.0f, SY, HalfW, 33.0f }, "Unassign Selected", Theme, Cursor, Pressed,
                false, false, 9.6f, -1.0f,
                OptionalTooltip::FromText("Move selected faces back to the automatically budgeted remainder.")))
        {
            Session->BindSelectionToPriorityLayer(0u);
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Concave Packing Engine (.pz-section)
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 338.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Concave Packing Engine", Theme, "EXACT SHAPE", ControlIcon::LayoutGrid);
        SY += 24.0f;

        auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();

        // Match StudioControls.SegmentedRow: two options inside one recessed capsule track.
        const Rectangle2D BudgetTrack{ SX, SY, SW, 34.0f };
        Surface.FillRectangle(BudgetTrack, ColorRgba{ 11, 11, 11, 255 }, 17.0f);
        Surface.StrokeRectangle(BudgetTrack, ColorRgba{ 255, 255, 255, 22 }, 17.0f, 1.0f);
        const float BudgetGap = 4.0f;
        const float BudgetInnerW = (SW - 12.0f) * 0.5f;
        const bool StrictOn = Prefs.BudgetMode == Configuration::UVBudgetEnforcementMode::StrictRatio;
        const std::array<std::string_view, 2u> BudgetLabels {{ "Strict Ratio", "Fill Pockets" }};
        for (uint32_t Index = 0u; Index < BudgetLabels.size(); ++Index)
        {
            const bool Active = (Index == 0u) == StrictOn;
            const Rectangle2D Option{
                SX + 4.0f + static_cast<float>(Index) * (BudgetInnerW + BudgetGap), SY + 4.0f,
                BudgetInnerW, 26.0f };
            const bool Hovered = Option.Contains(Cursor);
            if (Active || Hovered)
            {
                Surface.FillRectangle(Option, Active ? ColorRgba{ 41, 41, 41, 255 } : ColorRgba{ 33, 33, 33, 255 }, 13.0f);
            }
            const float TextW = WorkspaceAppearance::MeasureWeightedTextWidth(
                Surface, FontWeightClass::Regular, 10.5f, BudgetLabels[Index]);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular,
                { Option.X + (Option.Width - TextW) * 0.5f, Option.Y + 7.0f },
                Option.Width - 12.0f, 10.5f, Active || Hovered ? Theme.TextPrimary : Theme.TextMuted, BudgetLabels[Index]);
            if (Hovered)
            {
                const auto Tip = Index == 0u
                    ? OptionalTooltip::FromText("Keep each layer within its explicit packing-space share.")
                    : OptionalTooltip::FromText("Allow suitable islands to expand into unused pockets after packing.");
                RecordControlTooltip(Option, BudgetLabels[Index], Tip, "Choose the packing budget enforcement mode.");
            }
            if (Hovered && Pressed)
            {
                if (Index == 0u)
                {
                    Prefs.BudgetMode = Configuration::UVBudgetEnforcementMode::StrictRatio;
                    Session->FillPocketsEnabled = false;
                }
                else
                {
                    Prefs.BudgetMode = Configuration::UVBudgetEnforcementMode::FillRemainderPockets;
                    Session->FillPocketsEnabled = true;
                }
            }
        }
        SY += 42.0f;

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "UDIM Packing Scope");
        constexpr std::array<const char*, 4u> ScopeOptions {{
            "Pack into Active UDIM Tile", "Pack Per Assigned UDIM Tile", "Auto-Spill Overflow to New UDIMs", "Pack Selected Islands Only" }};
        const uint32_t ScopeIndex = static_cast<uint32_t>(Prefs.PackingScope);
        PackingScopeDropdownBounds = { SX, SY + 17.0f, SW, 28.0f };
        const OptionalTooltip ScopeTip = OptionalTooltip::FromTitled(
            "UDIM Packing Scope", "Choose which UDIM tiles are affected by the next pack operation.");
        const ControlContact ScopeButton = DropdownSelector::ConstructButton(
            Surface, PackingScopeDropdownBounds, ScopeOptions[std::min<uint32_t>(ScopeIndex, 3u)],
            PackingScopeDropdownOpen, Input.PrimaryPointer, 0.92f, ControlIcon::LayoutGrid, ScopeTip);
        if (ScopeButton.Clicked)
        {
            PackingScopeDropdownOpen = !PackingScopeDropdownOpen;
            ActiveLayerDropdownOpen = false;
            TargetUdimDropdownOpen = false;
        }
        SY += 52.0f;

        // Island Margin Padding (0..64 px)
        float MarginNorm = static_cast<float>(Prefs.IslandMarginPixels) / 64.0f;
        char  MarginTxt[24]{};
        std::snprintf(MarginTxt, sizeof(MarginTxt), "%u px", Prefs.IslandMarginPixels);
        if (ConstructSliderControl(
                Surface, { SX, SY, SW, 28.0f }, "Island Margin Padding", MarginTxt, MarginNorm, Theme, Cursor, Down, Pressed,
                ColorRgba{ 0u, 0u, 0u, 0u },
                OptionalTooltip::FromTitled("Island Margin Padding", "Set the edge gutter between packed islands in pixels.")))
        {
            Prefs.IslandMarginPixels = static_cast<uint32_t>(std::round(MarginNorm * 64.0f));
        }
        SY += 54.0f;

        // Match StudioControls.SegmentedRow: full-width segmented rotation choices.
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Rotation Steps");
        constexpr std::array<std::pair<const char*, uint32_t>, 4u> RotSteps {{
            { "0° Locked", 0u }, { "90° Ortho", 90u }, { "45° Oct", 45u }, { "15° Fine", 15u },
        }};
        const Rectangle2D RotationTrack{ SX, SY + 17.0f, SW, 34.0f };
        Surface.FillRectangle(RotationTrack, ColorRgba{ 11, 11, 11, 255 }, 17.0f);
        Surface.StrokeRectangle(RotationTrack, ColorRgba{ 255, 255, 255, 22 }, 17.0f, 1.0f);
        constexpr float RotationGap = 4.0f;
        const float RotationSegmentW = (SW - 12.0f) / 4.0f;
        for (uint32_t Idx = 0u; Idx < RotSteps.size(); ++Idx)
        {
            const Rectangle2D Segment{
                SX + 4.0f + static_cast<float>(Idx) * (RotationSegmentW + RotationGap), SY + 21.0f,
                RotationSegmentW, 26.0f };
            const bool Active = Prefs.RotationStepDegrees == RotSteps[Idx].second;
            const bool Hovered = Segment.Contains(Cursor);
            if (Active || Hovered)
            {
                Surface.FillRectangle(Segment, Active ? ColorRgba{ 41, 41, 41, 255 } : ColorRgba{ 33, 33, 33, 255 }, 13.0f);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { Segment.X + 5.0f, Segment.Y + 8.0f },
                Segment.Width - 10.0f, 9.8f, Active || Hovered ? Theme.TextPrimary : Theme.TextMuted, RotSteps[Idx].first);
            if (Hovered)
            {
                RecordControlTooltip(
                    Segment, RotSteps[Idx].first,
                    OptionalTooltip::FromText("Set the rotation increments considered by the island packer."),
                    "Choose a pack rotation step.");
            }
            if (Hovered && Pressed)
            {
                Prefs.RotationStepDegrees = RotSteps[Idx].second;
            }
        }
        SY += 59.0f;

        // Equalize Intra-Group Scale switch
        (void)ConstructToggleRow(
            Surface, { SX, SY, SW, 46.0f }, "Equalize Intra-Group Scale", Prefs.EqualizeIntraLayerScale, Theme, Cursor, Pressed,
            OptionalTooltip::FromText("Normalize texel density inside each priority layer before scaling."),
            "Normalize texel density inside each priority layer", "before scaling");
        SY += 53.0f;

        // Auto-spill scope routes through the multi-UDIM pack directive; other scopes use the single-tile pack path.
        const bool AutoSpillScope = Prefs.PackingScope == Configuration::UVPackingScopeMode::AutoSpillOverflowUDIMs;
        const char* PackButtonLabel = AutoSpillScope
            ? "Pack with Auto UDIM Spill (Shift+P)"
            : "Pack with Importance Budgets (Shift+P)";
        if (ConstructCapsuleButton(
                Surface,
                { SX, SY, SW, 34.0f },
                PackButtonLabel,
                Theme,
                Cursor,
                Pressed,
                false,
                true,
                10.5f,
                17.0f,
                OptionalTooltip::FromTitled(
                    AutoSpillScope ? "Pack with Auto UDIM Spill" : "Pack with Importance Budgets",
                    AutoSpillScope
                        ? "Pack priority-layer islands across successive UDIM tiles when the active tile fills."
                        : "Arrange UV islands using the active allocation shares.")))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline, nullptr);
        }

        CurY += SecRect.Height + 12.0f;
    }

    // Repaint open dropdowns last so their sheets sit above later controls in the panel.
    auto DrawFittedOptionSheet = [&](
        Rectangle2D ButtonBounds,
        const char* const* Options,
        uint32_t OptionCount,
        uint32_t SelectedIndex,
        bool& IsOpen,
        auto&& OnChoice)
    {
        if (!IsOpen || OptionCount == 0u)
        {
            return;
        }
        Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(ButtonBounds, OptionCount);
        Rectangle2D SheetAnchor = ButtonBounds;
        if (SheetBounds.Bottom() > PopupClip.Bottom())
        {
            SheetAnchor.Y = ButtonBounds.Y - SheetBounds.Height - 2.0f * ControlMetrics::DropdownPaddingY - ButtonBounds.Height;
            const Rectangle2D AboveSheet = DropdownSelector::QuerySheetBounds(SheetAnchor, OptionCount);
            if (AboveSheet.Y >= PopupClip.Y)
            {
                SheetBounds = AboveSheet;
            }
            else
            {
                SheetAnchor = ButtonBounds;
            }
        }
        uint32_t ChosenIndex = SelectedIndex;
        const ControlContact Contact = DropdownSelector::ConstructOptionSheet(
            Surface, SheetAnchor, Options, OptionCount, SelectedIndex, Input.PrimaryPointer, ChosenIndex);
        if (Contact.Clicked)
        {
            OnChoice(ChosenIndex);
            IsOpen = false;
        }
        else if (Input.PrimaryPointer.Released && !ButtonBounds.Contains(Cursor) && !SheetBounds.Contains(Cursor))
        {
            IsOpen = false;
        }
    };

    const auto LayersForPopup = Session->Asset.Layers.InspectLayers();
    std::vector<std::string> ActiveLabelStorage{ "Unassigned Remainder" };
    for (const auto& Layer : LayersForPopup)
    {
        ActiveLabelStorage.emplace_back(
            Layer.LayerTitle + " (" + std::to_string(static_cast<uint32_t>(std::round(Layer.TargetSharePercent))) + "%)");
    }
    std::vector<const char*> ActivePopupOptions{};
    for (const std::string& Label : ActiveLabelStorage)
    {
        ActivePopupOptions.push_back(Label.c_str());
    }
    uint32_t ActivePopupIndex = 0u;
    for (uint32_t Index = 0u; Index < LayersForPopup.size(); ++Index)
    {
        if (LayersForPopup[Index].LayerOrdinal == Session->SelectedLayerOrdinal)
        {
            ActivePopupIndex = Index + 1u;
            break;
        }
    }
    DrawFittedOptionSheet(
        ActiveLayerDropdownBounds, ActivePopupOptions.data(), static_cast<uint32_t>(ActivePopupOptions.size()),
        ActivePopupIndex, ActiveLayerDropdownOpen,
        [&](uint32_t Chosen)
        {
            if (Chosen < ActiveLabelStorage.size())
            {
                Session->SelectPriorityLayer(Chosen == 0u ? 0u : LayersForPopup[Chosen - 1u].LayerOrdinal);
            }
        });

    std::vector<uint32_t> TilePopupOptions{ 0u };
    for (const uint32_t Tile : Session->Asset.UDIMTiles)
    {
        if (std::find(TilePopupOptions.begin(), TilePopupOptions.end(), Tile) == TilePopupOptions.end())
        {
            TilePopupOptions.push_back(Tile);
        }
    }
    if (const auto* TargetLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        TargetLayer != nullptr && TargetLayer->TargetUDIMNumber >= 1001u &&
        std::find(TilePopupOptions.begin(), TilePopupOptions.end(), TargetLayer->TargetUDIMNumber) == TilePopupOptions.end())
    {
        TilePopupOptions.push_back(TargetLayer->TargetUDIMNumber);
    }
    std::sort(TilePopupOptions.begin() + 1, TilePopupOptions.end());
    std::vector<std::string> TileLabelStorage{};
    std::vector<const char*> TilePopupLabels{};
    for (const uint32_t Tile : TilePopupOptions)
    {
        TileLabelStorage.emplace_back(Tile == 0u ? "Auto UDIM" : "UDIM " + std::to_string(Tile));
    }
    for (const std::string& Label : TileLabelStorage)
    {
        TilePopupLabels.push_back(Label.c_str());
    }
    const auto* PopupLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
    uint32_t TilePopupIndex = 0u;
    if (PopupLayer != nullptr && PopupLayer->TargetUDIMNumber != 0u)
    {
        const auto It = std::find(TilePopupOptions.begin(), TilePopupOptions.end(), PopupLayer->TargetUDIMNumber);
        if (It != TilePopupOptions.end())
        {
            TilePopupIndex = static_cast<uint32_t>(std::distance(TilePopupOptions.begin(), It));
        }
    }
    DrawFittedOptionSheet(
        TargetUdimDropdownBounds, TilePopupLabels.data(), static_cast<uint32_t>(TilePopupLabels.size()),
        TilePopupIndex, TargetUdimDropdownOpen,
        [&](uint32_t Chosen)
        {
            if (PopupLayer != nullptr && Chosen < TilePopupOptions.size())
            {
                (void)Session->MoveSelectedLayerToUDIM(TilePopupOptions[Chosen]);
            }
        });

    constexpr std::array<const char*, 4u> ScopePopupOptions {{
        "Pack into Active UDIM Tile", "Pack Per Assigned UDIM Tile", "Auto-Spill Overflow to New UDIMs", "Pack Selected Islands Only" }};
    const uint32_t ScopePopupIndex = static_cast<uint32_t>(Session->WorkspaceConfig.InspectUVPreferences().PackingScope);
    DrawFittedOptionSheet(
        PackingScopeDropdownBounds, ScopePopupOptions.data(), static_cast<uint32_t>(ScopePopupOptions.size()),
        std::min<uint32_t>(ScopePopupIndex, static_cast<uint32_t>(ScopePopupOptions.size() - 1u)),
        PackingScopeDropdownOpen,
        [&](uint32_t Chosen)
        {
            if (Chosen < ScopePopupOptions.size())
            {
                Session->WorkspaceConfig.AccessUVPreferences().PackingScope =
                    static_cast<Configuration::UVPackingScopeMode>(Chosen);
            }
        });

    return CurY - StartY;
}

float UVInspectorPanel::RenderUnwrapTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;

    float CurY = StartY;

    // Section 1: Unwrap, Quads & Projections
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 198.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Unwrap, Quads & Stacking", Theme);
        SY += 24.0f;

        const float HalfW = (SW - 6.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 30.0f }, "LSCM Unwrap (U)", Theme, Cursor, Pressed, false, true, 10.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::UnwrapConformal, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 30.0f }, "Smart UV Project", Theme, Cursor, Pressed, false, false, 10.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::ProjectSmartUV, nullptr);
        }
        SY += 36.0f;

        const float ThirdW = (SW - 8.0f) / 3.0f;
        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Follow Quads", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::FollowActiveQuads, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Stack Similar", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::StackSimilarIslands, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Unstack", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::UnstackOverlapping, nullptr);
        }
        SY += 32.0f;

        const float FourthW = (SW - 9.0f) / 4.0f;
        const std::array<std::pair<const char*, UVCommandDirective>, 4u> Projs {{
            { "Cube",     UVCommandDirective::ProjectCubicBox    },
            { "Cylinder", UVCommandDirective::ProjectCylindrical },
            { "Sphere",   UVCommandDirective::ProjectSpherical   },
            { "Camera",   UVCommandDirective::ProjectCameraView  },
        }};
        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            if (ConstructCapsuleButton(
                    Surface,
                    { SX + static_cast<float>(Idx) * (FourthW + 3.0f), SY, FourthW, 25.0f },
                    Projs[Idx].first,
                    Theme,
                    Cursor,
                    Pressed,
                    false,
                    false,
                    9.0f,
                    8.0f))
            {
                (void)Session->ExecuteDirective(Projs[Idx].second, nullptr);
            }
        }
        SY += 33.0f;

        float AngleNorm = (Session->SharpAngleThreshold - 15.0f) / 74.0f;
        char  AngleTxt[24]{};
        std::snprintf(AngleTxt, sizeof(AngleTxt), "%.0f°", Session->SharpAngleThreshold);
        if (ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Smart Seam Angle Limit", AngleTxt, AngleNorm, Theme, Cursor, Down, Pressed))
        {
            Session->SharpAngleThreshold = 15.0f + AngleNorm * 74.0f;
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Polygon Network, Pins & Seams
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 134.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Pin, Stitch & Seam Operations", Theme);
        SY += 24.0f;

        const float ThirdW = (SW - 8.0f) / 3.0f;
        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Pin UVs", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::PinSelectedUVs, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Unpin UVs", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::UnpinSelectedUVs, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Rectify", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::RectifySelectedQuads, nullptr);
        }
        SY += 32.0f;

        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Mark Seams", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::MarkSharpSeams, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Clear Seams", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::ClearSelectedSeams, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Stitch", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::StitchSelectedSeams, nullptr);
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 3: Relax & Texel Density
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 146.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "GPU Stretch Relax & Texel Density", Theme);
        SY += 24.0f;

        float IterNorm = static_cast<float>(Session->RelaxIterations) / 64.0f;
        char  IterTxt[24]{};
        std::snprintf(IterTxt, sizeof(IterTxt), "%u passes", Session->RelaxIterations);
        if (ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Relax Passes", IterTxt, IterNorm, Theme, Cursor, Down, Pressed))
        {
            Session->RelaxIterations = std::max(1u, static_cast<uint32_t>(std::round(IterNorm * 64.0f)));
        }
        SY += 36.0f;

        const float HalfW = (SW - 6.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 28.0f }, "Relax Stretch GPU", Theme, Cursor, Pressed, false, true, 9.8f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 28.0f }, "Equalize Density", Theme, Cursor, Pressed, false, false, 9.8f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::EqualizeTexelDensity, nullptr);
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderUDIMTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;

    float CurY = StartY;

    char ActBuf[24]{}, CountBuf[24]{}, DensBuf[24]{};
    std::snprintf(ActBuf, sizeof(ActBuf), "%u", Session->ActiveUDIMNumber);
    std::snprintf(CountBuf, sizeof(CountBuf), "%zu", Session->Asset.UDIMTiles.size());
    std::snprintf(DensBuf, sizeof(DensBuf), "%.1f px/cm", Session->WorkspaceConfig.InspectUVPreferences().TargetTexelDensity * 0.01f);

    ConstructMetricTrio(
        Surface,
        { LeftX, CurY, InnerW, 56.0f },
        ActBuf,   "Active UDIM",
        CountBuf, "Atlas Tiles",
        DensBuf,  "Texel Density",
        Theme);
    CurY += 66.0f;

    // Section 1: UDIM Tiles grid (.pz-udim-grid)
    {
        const float TileListHeight = static_cast<float>(Session->Asset.UDIMTiles.size()) * 36.0f;
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 88.0f + TileListHeight };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "UDIM Tiles", Theme);
        SY += 24.0f;

        const float HalfW = (SW - 6.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 26.0f }, "+ Add UDIM Tile", Theme, Cursor, Pressed, false, true, 9.6f))
        {
            (void)Session->ConstructNextUDIMTile();
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 26.0f }, "Distribute UDIMs", Theme, Cursor, Pressed, false, false, 9.6f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::AutoDistributeUDIMs, nullptr);
        }
        SY += 34.0f;

        for (const uint32_t TileCode : Session->Asset.UDIMTiles)
        {
            const bool        IsAct = (Session->ActiveUDIMNumber == TileCode);
            const auto        Cfg   = Session->QueryUDIMTileSetting(TileCode);
            const float       Occ   = EvaluateUDIMFillPercent(Session->Asset.Network, TileCode);
            const Rectangle2D Row   { SX, SY, SW, 32.0f };
            const bool        Hov   = Row.Contains(Cursor);

            Surface.FillRectangle(Row, IsAct ? Theme.AccentSoft : (Hov ? Theme.HoverBg : Theme.WellBg), 8.0f);
            Surface.StrokeRectangle(Row, IsAct ? Theme.AccentBorder : Theme.BorderSubtle, 8.0f, 1.0f);

            Surface.FillCircle({ Row.X + 12.0f, Row.Y + 14.0f }, 3.5f, IsAct ? Theme.AccentFill : Theme.TextMuted);

            char RowTxt[64]{};
            const uint32_t TU = (TileCode - 1001u) % 10u;
            const uint32_t TV = (TileCode - 1001u) / 10u;
            std::snprintf(RowTxt, sizeof(RowTxt), "UDIM %u   (%u,%u) · %.1f%%", TileCode, TU, TV, Occ);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Row.X + 24.0f, Row.Y + 7.5f }, 9.8f, Theme.TextPrimary, RowTxt);

            char ResolutionTag[16]{};
            if (Cfg.ResolutionPx >= 1024u)
            {
                std::snprintf(ResolutionTag, sizeof(ResolutionTag), "%uK", Cfg.ResolutionPx / 1024u);
            }
            else
            {
                std::snprintf(ResolutionTag, sizeof(ResolutionTag), "%u", Cfg.ResolutionPx);
            }
            const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.0f, ResolutionTag);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Row.Right() - RW - 10.0f, Row.Y + 8.0f }, 9.0f, Theme.AccentFill, ResolutionTag);

            // Bottom 2px fill bar
            if (Occ > 0.2f)
            {
                Surface.FillRectangle({ Row.X + 4.0f, Row.Bottom() - 3.0f, (Row.Width - 8.0f) * (Occ * 0.01f), 2.0f }, Theme.AccentFill, 1.0f);
            }

            if (Hov && Pressed)
            {
                Session->ActiveUDIMNumber  = TileCode;
                Session->FramebuffersDirty = true;
            }
            SY += 36.0f;
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Active UDIM Resolution & Backing
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 132.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        char SecTitle[48]{};
        std::snprintf(SecTitle, sizeof(SecTitle), "UDIM %u Settings", Session->ActiveUDIMNumber);
        ConstructSectionHeading(Surface, SX, SY, SW, SecTitle, Theme);
        SY += 24.0f;

        auto& TileCfg = Session->AccessUDIMTileSetting(Session->ActiveUDIMNumber);

        // 5-column resolution deck: 512 | 1K | 2K | 4K | 8K
        constexpr std::array<std::pair<const char*, uint32_t>, 5u> ResDeck {{
            { "512", 512u  },
            { "1K",  1024u },
            { "2K",  2048u },
            { "4K",  4096u },
            { "8K",  8192u },
        }};
        const float ResW = (SW - 12.0f) / 5.0f;
        for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
        {
            if (ConstructCapsuleButton(
                    Surface,
                    { SX + static_cast<float>(Idx) * (ResW + 3.0f), SY, ResW, 26.0f },
                    ResDeck[Idx].first,
                    Theme,
                    Cursor,
                    Pressed,
                    TileCfg.ResolutionPx == ResDeck[Idx].second,
                    false,
                    9.2f,
                    8.0f))
            {
                TileCfg.ResolutionPx      = ResDeck[Idx].second;
                Session->FramebuffersDirty = true;
            }
        }
        SY += 34.0f;

        // 4-column Backing selector
        constexpr std::array<std::pair<const char*, GeometricRaster::UVBackdropPreset>, 4u> Backings {{
            { "Slate",      GeometricRaster::UVBackdropPreset::None             },
            { "ColorCheck", GeometricRaster::UVBackdropPreset::ColorChecker     },
            { "Trim Atlas", GeometricRaster::UVBackdropPreset::TrimSheetAtlas   },
            { "Aniso Pack", GeometricRaster::UVBackdropPreset::BrushedAnisoPack },
        }};
        const float BackW = (SW - 9.0f) / 4.0f;
        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            if (ConstructCapsuleButton(
                    Surface,
                    { SX + static_cast<float>(Idx) * (BackW + 3.0f), SY, BackW, 26.0f },
                    Backings[Idx].first,
                    Theme,
                    Cursor,
                    Pressed,
                    TileCfg.BackdropPreset == Backings[Idx].second,
                    false,
                    8.8f,
                    8.0f))
            {
                TileCfg.BackdropPreset     = Backings[Idx].second;
                Session->FramebuffersDirty = true;
            }
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderShadingTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;

    float CurY = StartY;

    // 1. .pz-shading-hero Banner
    {
        const Rectangle2D Hero{ LeftX, CurY, InnerW, 76.0f };
        Surface.FillRectangle(Hero, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(Hero, Theme.BorderStrong, 20.0f, 1.0f);

        ConstructMatCapOrbPreview(Surface, { Hero.X + 34.0f, Hero.Y + 38.0f }, 20.0f, SelectedMatCap, true, Theme);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Hero.X + 66.0f, Hero.Y + 16.0f }, 12.5f, Theme.TextPrimary, "Studio + Multi-MatCap");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.X + 66.0f, Hero.Y + 36.0f }, 9.2f, Theme.TextMuted, "GGX Direct Lit + 16 Procedural MatCaps");

        char SubLine[64]{};
        std::snprintf(SubLine, sizeof(SubLine), "%s tris · UDIM %u", FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str(), Session->ActiveUDIMNumber);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.X + 66.0f, Hero.Y + 52.0f }, 8.8f, Theme.AccentFill, SubLine);

        CurY += Hero.Height + 10.0f;
    }

    // 2. 2x2 Bento Telemetry Grid (.pz-bento-grid)
    {
        const float Gap   = 8.0f;
        const float TileW = (InnerW - Gap) * 0.5f;
        const float TileH = 54.0f;

        char OvStr[32]{}, StrStr[32]{}, ResStr[32]{}, SurfStr[32]{};
        std::snprintf(OvStr, sizeof(OvStr), "%u Overlaps", Session->LatestMetrics.OverlapFaceCount);
        std::snprintf(StrStr, sizeof(StrStr), "%.2fx", Session->LatestMetrics.MeanL2Stretch);
        std::snprintf(ResStr, sizeof(ResStr), "%u px", Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber).ResolutionPx);
        std::snprintf(SurfStr, sizeof(SurfStr), "%s f", FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str());

        const std::array<std::pair<const char*, const char*>, 4u> Bento {{
            { "UV Health",    OvStr   },
            { "Mean Stretch", StrStr  },
            { "Atlas Res",    ResStr  },
            { "Surface",      SurfStr },
        }};

        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            const float BX = LeftX + static_cast<float>(Idx % 2u) * (TileW + Gap);
            const float BY = CurY  + static_cast<float>(Idx / 2u) * (TileH + Gap);
            const Rectangle2D Cell{ BX, BY, TileW, TileH };
            Surface.FillRectangle(Cell, Theme.SectionBg, 14.0f);
            Surface.StrokeRectangle(Cell, Theme.BorderSubtle, 14.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Cell.X + 12.0f, Cell.Y + 9.0f }, 8.8f, Theme.TextMuted, Bento[Idx].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium,  { Cell.X + 12.0f, Cell.Y + 26.0f }, 12.5f, (Idx == 0u && Session->LatestMetrics.OverlapFaceCount == 0u) ? Theme.ReadyGreen : Theme.TextPrimary, Bento[Idx].second);
        }
        CurY += (TileH * 2.0f + Gap) + 10.0f;
    }

    // 3. Surface Finish & 16-Orb MatCap color set
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 236.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Surface Finish & MatCaps", Theme);
        SY += 26.0f;

        // 8x2 MatCap Sphere Grid
        const float ColStep = SW / 8.0f;
        for (uint32_t Slot = 0u; Slot < 16u; ++Slot)
        {
            const uint32_t Col = Slot % 8u;
            const uint32_t Row = Slot / 8u;
            const Point2D  Ctr { SX + ColStep * (static_cast<float>(Col) + 0.5f), SY + 13.0f + static_cast<float>(Row) * 32.0f };
            ConstructMatCapOrbPreview(Surface, Ctr, 11.5f, Slot, SelectedMatCap == Slot, Theme);

            const Rectangle2D Hit{ Ctr.X - 13.0f, Ctr.Y - 13.0f, 26.0f, 26.0f };
            if (Hit.Contains(Cursor) && Pressed)
            {
                SelectedMatCap = static_cast<uint8_t>(Slot);
                Session->ApplyMatCapToActiveSelection(SelectedMatCap);
            }
        }
        SY += 70.0f;

        char MetTxt[24]{}, RghTxt[24]{}, BldTxt[24]{};
        std::snprintf(MetTxt, sizeof(MetTxt), "%.2f", SurfaceMetallic);
        std::snprintf(RghTxt, sizeof(RghTxt), "%.2f", SurfaceRoughness);
        std::snprintf(BldTxt, sizeof(BldTxt), "%.0f%%", SurfaceMatBlend * 100.0f);

        bool MaterialDirty = false;
        MaterialDirty |= ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Metallic", MetTxt, SurfaceMetallic, Theme, Cursor, Down, Pressed);
        SY += 34.0f;
        MaterialDirty |= ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Roughness", RghTxt, SurfaceRoughness, Theme, Cursor, Down, Pressed);
        SY += 34.0f;
        MaterialDirty |= ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "MatCap Blend", BldTxt, SurfaceMatBlend, Theme, Cursor, Down, Pressed);

        if (MaterialDirty)
        {
            Session->ApplySurfaceRoughnessMetallic(SurfaceRoughness, SurfaceMetallic, SurfaceMatBlend);
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderCheckTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;

    float CurY = StartY;

    char OvStr[24]{}, FlpStr[24]{}, StrStr[24]{};
    std::snprintf(OvStr, sizeof(OvStr), "%u", Session->LatestMetrics.OverlapFaceCount);
    std::snprintf(FlpStr, sizeof(FlpStr), "%u", Session->LatestMetrics.FlippedFaceCount);
    std::snprintf(StrStr, sizeof(StrStr), "%.2f", Session->LatestMetrics.MeanL2Stretch);

    ConstructMetricTrio(
        Surface,
        { LeftX, CurY, InnerW, 56.0f },
        OvStr,  "Overlapping Faces",
        FlpStr, "Flipped Winding",
        StrStr, "Mean L2 Stretch",
        Theme);
    CurY += 66.0f;

    const Rectangle2D SecRect{ LeftX, CurY, InnerW, 148.0f };
    Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
    Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

    const float SX = SecRect.X + 16.0f;
    const float SW = SecRect.Width - 32.0f;
    float       SY = SecRect.Y + 14.0f;

    ConstructSectionHeading(Surface, SX, SY, SW, "UV Integrity & Repair", Theme, "VULKAN SAT");
    SY += 26.0f;

    const float HalfW = (SW - 6.0f) * 0.5f;
    if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 28.0f }, "Select Overlaps", Theme, Cursor, Pressed, false, false, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::SelectOverlappingFaces, nullptr);
    }
    if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 28.0f }, "Select Flipped", Theme, Cursor, Pressed, false, false, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::SelectFlippedFaces, nullptr);
    }
    SY += 34.0f;

    if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 30.0f }, "Run GPU Inspection", Theme, Cursor, Pressed, false, false, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::ValidateDistortionGPU, nullptr);
    }
    if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 30.0f }, "Auto-Relax & Repack", Theme, Cursor, Pressed, false, true, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);
        (void)Session->ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline, nullptr);
    }

    CurY += SecRect.Height + 12.0f;
    return CurY - StartY;
}

void UVInspectorPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PanelBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    const auto&   Network = Session->Asset.Network;
    if (CachedNetworkRevision != Network.NetworkRevision || CachedUVRevision != Network.UVRevision)
    {
        CachedNetworkRevision = Network.NetworkRevision;
        CachedUVRevision = Network.UVRevision;
        CachedChartCount = static_cast<uint32_t>(SeamCutGenerator::PartitionConnectedUVIslands(Network).size());
    }
    const std::vector<uint32_t> SelectedFaces = Session->SelectionMask.CollectSelectedFaces();

    const float PadX   = 18.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = std::max(150.0f, Bounds.Width - PadX * 2.0f);
    float       CurY   = Bounds.Y + 36.0f;

    // 1. Breadcrumb + "+ Export OBJ" + "Reset"
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY + 4.0f },
        InnerW - 140.0f,
        9.0f,
        Theme.TextMuted,
        std::array<const char*, 5u>{
            "Inspector / Importance Packing",
            "Inspector / Unwrap & Tools",
            "Inspector / Spatial UDIM Matrix",
            "Inspector / Surface & Multi-MatCap",
            "Inspector / Checks, Studio & Flat Debug" }[
                std::min<std::size_t>(static_cast<std::size_t>(Session->ActiveInspectorTab), 4u)]);

    const Rectangle2D ResetBtn  { LeftX + InnerW - 48.0f, CurY, 48.0f, 22.0f };
    const Rectangle2D ExportBtn { ResetBtn.X - 84.0f, CurY, 78.0f, 22.0f };
    if (ConstructCapsuleButton(
            Surface, ExportBtn, "+ Export OBJ", Theme, Cursor, Pressed, false, false, 8.8f, -1.0f,
            OptionalTooltip::FromText("Export the current polygon network and UV coordinates as an OBJ file.")))
    {
        std::string Diag {};
        (void)Session->ExportExternalGeometry(std::filesystem::temp_directory_path() / "FrontierExport.obj", Diag);
    }
    if (ConstructCapsuleButton(
            Surface, ResetBtn, "Reset", Theme, Cursor, Pressed, false, false, 8.8f, -1.0f,
            OptionalTooltip::FromText("Reload the current sample asset and reset its workspace state.")))
    {
        Session->LoadSamplePreset(Session->ActivePreset);
    }
    CurY += 28.0f;

    // 2. Large 23px Light Selection / Asset Heading + Subtitle (matching InspectorPanel.cpp & Experimental/UVEditor)
    const std::string Heading = SelectedFaces.empty()
        ? Session->Asset.Network.NetworkTitle
        : FormatCountWithCommas(SelectedFaces.size()) + " Faces Selected";
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Light,
        { LeftX, CurY },
        InnerW,
        23.0f,
        Theme.TextPrimary,
        Heading);
    CurY += 29.0f;

    char SubMeta[112]{};
    const auto TileCfg = Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber);
    if (SelectedFaces.empty())
    {
        std::snprintf(
            SubMeta,
            sizeof(SubMeta),
            "%u charts · %s faces · Whole Surface",
            CachedChartCount,
            FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str());
    }
    else
    {
        uint32_t FirstSelectedUDIM = 0u;
        bool MultipleSelectedUDIMs = false;
        for (const uint32_t FaceIndex : SelectedFaces)
        {
            if (FaceIndex >= Network.Faces.size())
            {
                continue;
            }
            const uint32_t FaceUDIM = Network.DeriveFaceUDIMNumber(FaceIndex);
            if (FirstSelectedUDIM == 0u)
            {
                FirstSelectedUDIM = FaceUDIM;
            }
            else if (FirstSelectedUDIM != FaceUDIM)
            {
                MultipleSelectedUDIMs = true;
                break;
            }
        }
        std::snprintf(
            SubMeta,
            sizeof(SubMeta),
            "%s · UDIM %s",
            FormatCountWithCommas(SelectedFaces.size()).c_str(),
            MultipleSelectedUDIMs ? "Multiple" : std::to_string(FirstSelectedUDIM).c_str());
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY },
        InnerW,
        9.8f,
        Theme.TextMuted,
        SubMeta);
    CurY += 42.0f;

    // 3. 5-Tab Segmented Bar (.inspector-tabs: Pack | Unwrap | UDIM | Shading | Check)
    {
        const Rectangle2D TabBar{ LeftX, CurY, InnerW, 34.0f };
        Surface.FillRectangle(TabBar, Theme.WellBg, 11.0f);
        Surface.StrokeRectangle(TabBar, Theme.BorderSubtle, 11.0f, 1.0f);

        constexpr std::array<const char*, 5u> TabNames {{ "Pack", "Unwrap", "UDIM", "Shading", "Check" }};
        constexpr std::array<const char*, 5u> TabTips {{
            "Allocation layers, island packing, and packing scope.",
            "Unwrap, projection, seam, pin, stitch, and relax tools.",
            "Manage UDIM tiles, resolution, and viewport backdrops.",
            "Configure MatCaps and surface material response.",
            "Inspect UV overlaps, flips, stretch, and repair options." }};
        const float SegW = (InnerW - 6.0f) / 5.0f;
        for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
        {
            const Rectangle2D Seg{ TabBar.X + 3.0f + static_cast<float>(Idx) * SegW, TabBar.Y + 3.0f, SegW, 28.0f };
            const auto        TargetTab = static_cast<UVInspectorTab>(Idx);
            const bool        Active    = (Session->ActiveInspectorTab == TargetTab);
            const bool        Hovered   = Seg.Contains(Cursor);
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    Seg, OptionalTooltip::FromText(TabTips[Idx]));
            }

            if (Active)
            {
                Surface.FillRectangle(Seg, Theme.RaisedBg, 8.0f);
                Surface.StrokeRectangle(Seg, Theme.AccentBorder, 8.0f, 1.0f);
            }
            else if (Hovered)
            {
                Surface.FillRectangle(Seg, Theme.HoverBg, 8.0f);
            }

            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, TabNames[Idx]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Seg.X + (Seg.Width - TW) * 0.5f, Seg.Y + 7.0f },
                9.8f,
                Active ? Theme.TextPrimary : Theme.TextMuted,
                TabNames[Idx]);

            if (Hovered && Pressed)
            {
                Session->ActiveInspectorTab = TargetTab;
                ScrollOffsetY               = 0.0f;
                ActiveLayerDropdownOpen = false;
                TargetUdimDropdownOpen = false;
                PackingScopeDropdownOpen = false;
            }
        }
        CurY += 44.0f;
    }

    // 4. Scrollable Active Tab Body above Sticky 40px Inspector Footer
    constexpr float   FooterH = 40.0f;
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - FooterH - CurY)
    };

    const std::size_t TabIndex = std::min<std::size_t>(
        static_cast<std::size_t>(Session->ActiveInspectorTab), CachedTabContentHeights.size() - 1u);
    const float PreviousMaxScroll = std::max(0.0f, CachedTabContentHeights[TabIndex] - ScrollRect.Height);
    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 28.0f;
    }
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, PreviousMaxScroll);

    Surface.PushClip(ScrollRect);
    const float BodyStartY = ScrollRect.Y - ScrollOffsetY;
    float       ContentH   = 0.0f;

    switch (Session->ActiveInspectorTab)
    {
        case UVInspectorTab::Packing: ContentH = RenderPackingTab(Surface, LeftX, BodyStartY, InnerW, ScrollRect, Input, Δt); break;
        case UVInspectorTab::Unwrap:  ContentH = RenderUnwrapTab(Surface, LeftX, BodyStartY, InnerW, Input);  break;
        case UVInspectorTab::UDIM:    ContentH = RenderUDIMTab(Surface, LeftX, BodyStartY, InnerW, Input);    break;
        case UVInspectorTab::Shading: ContentH = RenderShadingTab(Surface, LeftX, BodyStartY, InnerW, Input); break;
        case UVInspectorTab::Check:   ContentH = RenderCheckTab(Surface, LeftX, BodyStartY, InnerW, Input);   break;
    }

    CachedTabContentHeights[TabIndex] = ContentH;
    const float MaxScroll = std::max(0.0f, ContentH - ScrollRect.Height);
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScroll);
    Surface.PopClip();

    // 5. Sticky 40px Inspector Footer (.pz-inspector-foot)
    const Rectangle2D FootRect{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FootRect, Theme.PanelBg);
    Surface.StrokeLine({ FootRect.X, FootRect.Y }, { FootRect.Right(), FootRect.Y }, Theme.BorderSubtle, 1.0f);

    Surface.FillCircle({ LeftX + 5.0f, FootRect.Y + 20.0f }, 3.0f, Theme.ReadyGreen);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + 14.0f, FootRect.Y + 13.0f },
        9.5f,
        Theme.TextMuted,
        "Studio + Multi-MatCap");

    char FootRight[48]{};
    std::snprintf(FootRight, sizeof(FootRight), "UDIM %u · %u px", Session->ActiveUDIMNumber, TileCfg.ResolutionPx);
    const float FRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, FootRight);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + InnerW - FRW, FootRect.Y + 13.0f },
        9.5f,
        Theme.TextMuted,
        FootRight);

    Surface.PopClip();
    (void)ControlTooltip::PresentHoveredControlTooltip(
        Surface, { 0.0f, 0.0f, Bounds.Width, Bounds.Height }, Input.PrimaryPointer);
}

void UVInspectorPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##UVInspectorPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(Surface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces
