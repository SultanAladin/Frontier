/*============================================================================================================================================
                                                           UVOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVOutlinerPanel with Search, UDIM Filter, Inline Priority Layer Rename (TextInputField), Dup/Delete & Modes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVOutlinerPanel.h"

#include "Engine/Components/Controls/CapsuleButton.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] ColorRgba ConvertLinearTint(Mathematics::Scalar3D Tint, uint8_t Alpha = 255u) noexcept
{
    return ColorRgba{
        static_cast<uint8_t>(std::clamp(Tint.X * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(Tint.Y * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(Tint.Z * 255.0f, 0.0f, 255.0f)),
        Alpha,
    };
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty())
    {
        return true;
    }
    auto It = std::search(
        Haystack.begin(),
        Haystack.end(),
        Needle.begin(),
        Needle.end(),
        [](char Lhs, char Rhs)
        {
            return std::tolower(static_cast<unsigned char>(Lhs)) == std::tolower(static_cast<unsigned char>(Rhs));
        });
    return It != Haystack.end();
}

} // namespace

void UVOutlinerPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session          = TargetSession;
    RenamingLayerOrd = 0u;
    SearchEditBuffer.Cancel();
    RenameEditBuffer.Cancel();
}

void UVOutlinerPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    const ColorRgba PanelBg    { 20, 21, 25, 255 };
    const ColorRgba CardBg     { 27, 29, 35, 255 };
    const ColorRgba CardActive { 38, 43, 54, 255 };
    const ColorRgba BorderCol  { 48, 52, 62, 255 };
    const ColorRgba IvoryText  { 239, 244, 207, 255 };
    const ColorRgba CrispText  { 230, 234, 242, 255 };
    const ColorRgba MutedText  { 146, 152, 166, 255 };
    const ColorRgba AccentBlue { 112, 176, 255, 255 };
    const ColorRgba WarnCoral  { 236, 108, 102, 255 };

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, PanelBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    WorkspaceAppearance::ForwardInputToTextBuffer(SearchEditBuffer, Input, Δt);
    WorkspaceAppearance::ForwardInputToTextBuffer(RenameEditBuffer, Input, Δt);

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Clicked = Input.PrimaryPointer.Pressed;

    float       CurY   = Bounds.Y + 10.0f;
    const float LeftX  = Bounds.X + 12.0f;
    const float InnerW = std::max(120.0f, Bounds.Width - 24.0f);

    // 1. Pane Header + Live Mesh / Distortion Badge
    Surface.Text({ LeftX, CurY }, 13.5f, IvoryText, "UV SPACE");
    char MeshBadge[64]{};
    if (Session->LatestMetrics.FlippedFaceCount > 0u || Session->LatestMetrics.OverlapFaceCount > 0u)
    {
        std::snprintf(
            MeshBadge,
            sizeof(MeshBadge),
            "%u Flip | %u Ovlp",
            Session->LatestMetrics.FlippedFaceCount,
            Session->LatestMetrics.OverlapFaceCount);
        Surface.TextRightAligned({ LeftX, CurY, InnerW, 18.0f }, 11.0f, WarnCoral, MeshBadge);
    }
    else
    {
        std::snprintf(MeshBadge, sizeof(MeshBadge), "%zu Faces", Session->Asset.Network.Faces.size());
        Surface.TextRightAligned({ LeftX, CurY, InnerW, 18.0f }, 11.5f, MutedText, MeshBadge);
    }
    CurY += 22.0f;

    // 2. Selection Mode Strip (Faces [1] | Edges [2] | Islands [3] | Layers [4])
    {
        const std::array<const char*, 4u> ModeLabels {{ "Face [1]", "Edge [2]", "Island [3]", "Layer [4]" }};
        const float MChipW = (InnerW - 12.0f) / 4.0f;
        for (uint32_t MIdx = 0u; MIdx < 4u; ++MIdx)
        {
            const Rectangle2D MRect { LeftX + static_cast<float>(MIdx) * (MChipW + 4.0f), CurY, MChipW, 22.0f };
            const bool        Active = (static_cast<uint32_t>(Session->SelectionMode) == MIdx);
            Surface.FillRectangle(MRect, Active ? CardActive : CardBg, 4.0f);
            Surface.StrokeRectangle(MRect, Active ? AccentBlue : BorderCol, 4.0f, 1.0f);
            Surface.TextCentered(MRect, 10.5f, Active ? IvoryText : CrispText, ModeLabels[MIdx]);
            if (Clicked && MRect.Contains(Cursor))
            {
                Session->SelectionMode = static_cast<UVSelectionMode>(MIdx);
                Session->SynchronizeSelectionToGPU();
            }
        }
        CurY += 28.0f;
    }

    // 3. Outliner Search Field (TextInputField)
    {
        const Rectangle2D SearchRect { LeftX, CurY, InnerW, 24.0f };
        (void)TextInputField::Construct(Surface, SearchRect, SearchEditBuffer, Input.PrimaryPointer, 11.5f);
        if (SearchEditBuffer.Length == 0u && !SearchEditBuffer.Active)
        {
            Surface.Text({ LeftX + 8.0f, CurY + 5.0f }, 11.0f, MutedText, "Search layers...");
        }
        Session->OutlinerSearchQuery = std::string(SearchEditBuffer.Text, SearchEditBuffer.Length);
        CurY += 30.0f;
    }

    // 4. Sample Studio Asset Preset Strip
    Surface.Text({ LeftX, CurY }, 10.5f, MutedText, "STUDIO SAMPLE ASSET");
    CurY += 16.0f;

    const std::array<const char*, 5u> PresetNames {{ "Integ", "Coupe", "Crate", "Bust", "Arch" }};
    const float ChipGap = 4.0f;
    const float ChipW   = (InnerW - ChipGap * 4.0f) / 5.0f;
    for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
    {
        const Rectangle2D ChipRect { LeftX + static_cast<float>(Idx) * (ChipW + ChipGap), CurY, ChipW, 24.0f };
        const bool Active = (static_cast<uint32_t>(Session->ActivePreset) == Idx);
        Surface.FillRectangle(ChipRect, Active ? CardActive : CardBg, 5.0f);
        Surface.StrokeRectangle(ChipRect, Active ? AccentBlue : BorderCol, 5.0f, 1.0f);
        Surface.TextCentered(ChipRect, 11.0f, Active ? IvoryText : CrispText, PresetNames[Idx]);
        if (Clicked && ChipRect.Contains(Cursor))
        {
            Session->LoadSamplePreset(static_cast<GeometrySpace::SampleAssetPreset>(Idx));
        }
    }
    CurY += 32.0f;

    // 5. Priority Allocation Layers Section + "+ Layer" / "Dup" / "Del" Buttons
    Surface.Text({ LeftX, CurY + 4.0f }, 10.5f, MutedText, "PRIORITY LAYERS");
    const Rectangle2D DelBtnRect { LeftX + InnerW - 36.0f, CurY, 36.0f, 20.0f };
    const Rectangle2D DupBtnRect { LeftX + InnerW - 76.0f, CurY, 36.0f, 20.0f };
    const Rectangle2D AddBtnRect { LeftX + InnerW - 134.0f, CurY, 54.0f, 20.0f };

    Surface.FillRectangle(AddBtnRect, CardActive, 10.0f);
    Surface.StrokeRectangle(AddBtnRect, AccentBlue, 10.0f, 1.0f);
    Surface.TextCentered(AddBtnRect, 10.5f, IvoryText, "+ Layer");
    if (Clicked && AddBtnRect.Contains(Cursor))
    {
        char NewTitle[32]{};
        std::snprintf(NewTitle, sizeof(NewTitle), "Detail Layer %u", CreatedLayerCount++);
        (void)Session->CreatePriorityLayer(NewTitle, { 0.608f, 0.855f, 0.773f }, 10.0f);
    }

    Surface.FillRectangle(DupBtnRect, CardBg, 10.0f);
    Surface.StrokeRectangle(DupBtnRect, BorderCol, 10.0f, 1.0f);
    Surface.TextCentered(DupBtnRect, 10.0f, CrispText, "Dup");
    if (Clicked && DupBtnRect.Contains(Cursor))
    {
        (void)Session->DuplicateSelectedLayer();
    }

    Surface.FillRectangle(DelBtnRect, CardBg, 10.0f);
    Surface.StrokeRectangle(DelBtnRect, BorderCol, 10.0f, 1.0f);
    Surface.TextCentered(DelBtnRect, 10.0f, WarnCoral, "Del");
    if (Clicked && DelBtnRect.Contains(Cursor))
    {
        (void)Session->DeleteSelectedLayer();
    }
    CurY += 26.0f;

    for (const auto& Layer : Session->Asset.Layers.InspectLayers())
    {
        if (!ContainsCaseInsensitive(Layer.LayerTitle, Session->OutlinerSearchQuery))
        {
            continue;
        }
        const auto& Ref = Session->Asset.Layers.InspectPartitionSpan(
            Session->Asset.Network,
            Layer.LayerOrdinal,
            Session->RootInstanceToken);

        if (Session->OutlinerUDIMFilter != 0u &&
            std::find(Ref.UDIMNumbers.begin(), Ref.UDIMNumbers.end(), Session->OutlinerUDIMFilter) == Ref.UDIMNumbers.end())
        {
            continue;
        }

        const Rectangle2D RowRect { LeftX, CurY, InnerW, 44.0f };
        const bool Selected = (Session->SelectedLayerOrdinal == Layer.LayerOrdinal);
        Surface.FillRectangle(RowRect, Selected ? CardActive : CardBg, 6.0f);
        Surface.StrokeRectangle(RowRect, Selected ? IvoryText : BorderCol, 6.0f, Selected ? 1.4f : 1.0f);

        const Rectangle2D Swatch { LeftX + 10.0f, CurY + 11.0f, 14.0f, 22.0f };
        Surface.FillRectangle(Swatch, ConvertLinearTint(Layer.LinearTintRGB), 3.0f);

        if (RenamingLayerOrd == Layer.LayerOrdinal)
        {
            const Rectangle2D EditRect { LeftX + 30.0f, CurY + 5.0f, InnerW - 88.0f, 22.0f };
            (void)TextInputField::Construct(Surface, EditRect, RenameEditBuffer, Input.PrimaryPointer, 11.5f);
            if (RenameEditBuffer.Finalized || (!RenameEditBuffer.Active && !RenameEditBuffer.Cancelled))
            {
                Session->RenameSelectedLayer(std::string_view(RenameEditBuffer.Text, RenameEditBuffer.Length));
                RenamingLayerOrd = 0u;
            }
            else if (RenameEditBuffer.Cancelled)
            {
                RenamingLayerOrd = 0u;
            }
        }
        else
        {
            Surface.Text({ LeftX + 32.0f, CurY + 6.0f }, 12.0f, Selected ? IvoryText : CrispText, Layer.LayerTitle.c_str());
            if (Selected)
            {
                const Rectangle2D RenChip { LeftX + InnerW - 88.0f, CurY + 11.0f, 32.0f, 22.0f };
                Surface.FillRectangle(RenChip, PanelBg, 6.0f);
                Surface.StrokeRectangle(RenChip, BorderCol, 6.0f, 1.0f);
                Surface.TextCentered(RenChip, 10.0f, MutedText, "Ren");
                if (Clicked && RenChip.Contains(Cursor))
                {
                    RenamingLayerOrd = Layer.LayerOrdinal;
                    RenameEditBuffer.Begin(Layer.LayerTitle.c_str());
                    RenameEditBuffer.SelectAll();
                }
            }
        }

        char SubLine[96]{};
        std::snprintf(SubLine, sizeof(SubLine), "%zu faces  |  %.2f m^2", Ref.FaceIndices.size(), Ref.TotalArea3D);
        Surface.Text({ LeftX + 32.0f, CurY + 24.0f }, 10.5f, MutedText, SubLine);

        char SharePill[32]{};
        std::snprintf(SharePill, sizeof(SharePill), "%.0f%%", Layer.TargetSharePercent);
        const Rectangle2D PillRect { LeftX + InnerW - 50.0f, CurY + 11.0f, 40.0f, 22.0f };
        Surface.FillRectangle(PillRect, { 18, 20, 25, 255 }, 11.0f);
        Surface.StrokeRectangle(PillRect, BorderCol, 11.0f, 1.0f);
        Surface.TextCentered(PillRect, 11.0f, IvoryText, SharePill);

        if (Clicked && RowRect.Contains(Cursor) && RenamingLayerOrd != Layer.LayerOrdinal)
        {
            Session->SelectPriorityLayer(Layer.LayerOrdinal);
        }
        CurY += 48.0f;
    }

    // Remainder Auto-Calculated Row (Ordinal 0)
    {
        const Rectangle2D RemRect { LeftX, CurY, InnerW, 40.0f };
        const bool Selected = (Session->SelectedLayerOrdinal == 0u);
        Surface.FillRectangle(RemRect, Selected ? CardActive : CardBg, 6.0f);
        Surface.StrokeRectangle(RemRect, Selected ? AccentBlue : BorderCol, 6.0f, 1.0f);

        const Rectangle2D Swatch { LeftX + 10.0f, CurY + 9.0f, 14.0f, 22.0f };
        Surface.FillRectangle(Swatch, { 132, 142, 158, 255 }, 3.0f);
        Surface.Text({ LeftX + 32.0f, CurY + 6.0f }, 11.5f, CrispText, "Unassigned Remainder");

        const auto& RemRef = Session->Asset.Layers.InspectPartitionSpan(Session->Asset.Network, 0u, Session->RootInstanceToken);
        char RemSub[96]{};
        std::snprintf(RemSub, sizeof(RemSub), "%zu faces  |  Auto Share", RemRef.FaceIndices.size());
        Surface.Text({ LeftX + 32.0f, CurY + 22.0f }, 10.5f, MutedText, RemSub);

        char RemPct[32]{};
        std::snprintf(RemPct, sizeof(RemPct), "%.0f%%", Session->Asset.Layers.DeriveRemainderSharePercent());
        Surface.TextRightAligned({ LeftX, CurY + 11.0f, InnerW - 12.0f, 18.0f }, 11.5f, AccentBlue, RemPct);

        if (Clicked && RemRect.Contains(Cursor))
        {
            Session->SelectPriorityLayer(0u);
        }
        CurY += 46.0f;
    }

    // 6. UDIM Filter & Active Tile Selector Strip (+ UDIM button)
    Surface.Text({ LeftX, CurY + 2.0f }, 10.5f, MutedText, "UDIM FILTER & TILES");
    const Rectangle2D AddUdimBtn { LeftX + InnerW - 52.0f, CurY - 2.0f, 52.0f, 20.0f };
    Surface.FillRectangle(AddUdimBtn, CardActive, 10.0f);
    Surface.StrokeRectangle(AddUdimBtn, AccentBlue, 10.0f, 1.0f);
    Surface.TextCentered(AddUdimBtn, 10.0f, IvoryText, "+ UDIM");
    if (Clicked && AddUdimBtn.Contains(Cursor))
    {
        (void)Session->ConstructNextUDIMTile();
    }
    CurY += 22.0f;

    const std::size_t TotalChips = Session->Asset.UDIMTiles.size() + 1u;
    const float       TileChipW  = std::min(58.0f, (InnerW - static_cast<float>(TotalChips - 1u) * 5.0f) / static_cast<float>(TotalChips));

    // "All" filter chip
    {
        const Rectangle2D AllRect { LeftX, CurY, TileChipW, 24.0f };
        const bool AllActive = (Session->OutlinerUDIMFilter == 0u);
        Surface.FillRectangle(AllRect, AllActive ? CardActive : CardBg, 5.0f);
        Surface.StrokeRectangle(AllRect, AllActive ? IvoryText : BorderCol, 5.0f, 1.0f);
        Surface.TextCentered(AllRect, 11.0f, AllActive ? IvoryText : CrispText, "All");
        if (Clicked && AllRect.Contains(Cursor))
        {
            Session->OutlinerUDIMFilter = 0u;
        }
    }

    for (std::size_t TIdx = 0u; TIdx < Session->Asset.UDIMTiles.size(); ++TIdx)
    {
        const uint32_t Code       = Session->Asset.UDIMTiles[TIdx];
        const bool     TileActive = (Session->ActiveUDIMNumber == Code);
        const Rectangle2D TRect { LeftX + static_cast<float>(TIdx + 1u) * (TileChipW + 5.0f), CurY, TileChipW, 24.0f };
        Surface.FillRectangle(TRect, TileActive ? CardActive : CardBg, 5.0f);
        Surface.StrokeRectangle(TRect, TileActive ? AccentBlue : BorderCol, 5.0f, 1.0f);

        char CodeLabel[24]{};
        std::snprintf(CodeLabel, sizeof(CodeLabel), "%u", Code);
        Surface.TextCentered(TRect, 11.0f, TileActive ? IvoryText : CrispText, CodeLabel);
        if (Clicked && TRect.Contains(Cursor))
        {
            Session->ActiveUDIMNumber   = Code;
            Session->OutlinerUDIMFilter = (Session->OutlinerUDIMFilter == Code) ? 0u : Code;
        }
    }

    Surface.PopClip();
}

void UVOutlinerPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
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

    ImGui::InvisibleButton("##UVOutlinerCanvas", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)));

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             WindowSurface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(WindowSurface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces
