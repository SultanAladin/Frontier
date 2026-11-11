/*============================================================================================================================================
                                                           UVOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVOutlinerPanel rendering Studio Asset chips, PriorityAllocationLayers, Remainder share, and UDIM tile strip

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVOutlinerPanel.h"

#include "Engine/Components/Controls/CapsuleButton.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

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

} // namespace

void UVOutlinerPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void UVOutlinerPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    (void)Δt;
    const ColorRgba PanelBg    { 20, 21, 25, 255 };
    const ColorRgba CardBg     { 27, 29, 35, 255 };
    const ColorRgba CardActive { 38, 43, 54, 255 };
    const ColorRgba BorderCol  { 48, 52, 62, 255 };
    const ColorRgba IvoryText  { 239, 244, 207, 255 };
    const ColorRgba CrispText  { 230, 234, 242, 255 };
    const ColorRgba MutedText  { 146, 152, 166, 255 };
    const ColorRgba AccentBlue { 112, 176, 255, 255 };

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, PanelBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Clicked = Input.PrimaryPointer.Pressed;

    float CurY         = Bounds.Y + 12.0f;
    const float LeftX  = Bounds.X + 12.0f;
    const float InnerW = std::max(120.0f, Bounds.Width - 24.0f);

    // 1. Pane Header
    Surface.Text({ LeftX, CurY }, 13.5f, IvoryText, "UV SPACE");
    char MeshBadge[64]{};
    std::snprintf(MeshBadge, sizeof(MeshBadge), "%zu Faces", Session->Asset.Network.Faces.size());
    Surface.TextRightAligned({ LeftX, CurY, InnerW, 18.0f }, 11.5f, MutedText, MeshBadge);
    CurY += 26.0f;

    // 2. Sample Studio Asset Preset Strip
    Surface.Text({ LeftX, CurY }, 11.0f, MutedText, "STUDIO SAMPLE ASSET");
    CurY += 18.0f;

    const std::array<const char*, 5u> PresetNames {{ "Integ", "Coupe", "Crate", "Bust", "Arch" }};
    const float ChipGap = 4.0f;
    const float ChipW   = (InnerW - ChipGap * 4.0f) / 5.0f;
    for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
    {
        const Rectangle2D ChipRect { LeftX + static_cast<float>(Idx) * (ChipW + ChipGap), CurY, ChipW, 26.0f };
        const bool Active = (static_cast<uint32_t>(Session->ActivePreset) == Idx);
        Surface.FillRectangle(ChipRect, Active ? CardActive : CardBg, 5.0f);
        Surface.StrokeRectangle(ChipRect, Active ? AccentBlue : BorderCol, 5.0f, 1.0f);
        Surface.TextCentered(ChipRect, 11.0f, Active ? IvoryText : CrispText, PresetNames[Idx]);
        if (Clicked && ChipRect.Contains(Cursor))
        {
            Session->LoadSamplePreset(static_cast<GeometrySpace::SampleAssetPreset>(Idx));
        }
    }
    CurY += 36.0f;

    // 3. Priority Allocation Layers Section + "+ Layer" Button
    Surface.Text({ LeftX, CurY + 4.0f }, 11.0f, MutedText, "PRIORITY ALLOCATION LAYERS");
    const Rectangle2D AddBtnRect { LeftX + InnerW - 72.0f, CurY, 72.0f, 22.0f };
    Surface.FillRectangle(AddBtnRect, CardActive, 11.0f);
    Surface.StrokeRectangle(AddBtnRect, AccentBlue, 11.0f, 1.0f);
    Surface.TextCentered(AddBtnRect, 11.0f, IvoryText, "+ Layer");
    if (Clicked && AddBtnRect.Contains(Cursor))
    {
        char NewTitle[32]{};
        std::snprintf(NewTitle, sizeof(NewTitle), "Detail Layer %u", CreatedLayerCount++);
        (void)Session->CreatePriorityLayer(NewTitle, { 0.608f, 0.855f, 0.773f }, 10.0f);
    }
    CurY += 28.0f;

    for (const auto& Layer : Session->Asset.Layers.InspectLayers())
    {
        const Rectangle2D RowRect { LeftX, CurY, InnerW, 44.0f };
        const bool Selected = (Session->SelectedLayerOrdinal == Layer.LayerOrdinal);
        Surface.FillRectangle(RowRect, Selected ? CardActive : CardBg, 6.0f);
        Surface.StrokeRectangle(RowRect, Selected ? IvoryText : BorderCol, 6.0f, Selected ? 1.4f : 1.0f);

        const Rectangle2D Swatch { LeftX + 10.0f, CurY + 11.0f, 14.0f, 22.0f };
        Surface.FillRectangle(Swatch, ConvertLinearTint(Layer.LinearTintRGB), 3.0f);

        Surface.Text({ LeftX + 32.0f, CurY + 7.0f }, 12.5f, Selected ? IvoryText : CrispText, Layer.LayerTitle.c_str());

        const auto& Ref = Session->Asset.Layers.InspectLayerReference(
            Session->Asset.Network,
            Layer.LayerOrdinal,
            Session->RootInstanceToken);
        char SubLine[96]{};
        std::snprintf(SubLine, sizeof(SubLine), "%zu faces  |  %.2f m^2", Ref.FaceIndices.size(), Ref.TotalArea3D);
        Surface.Text({ LeftX + 32.0f, CurY + 24.0f }, 10.5f, MutedText, SubLine);

        char SharePill[32]{};
        std::snprintf(SharePill, sizeof(SharePill), "%.0f%%", Layer.TargetSharePercent);
        const Rectangle2D PillRect { LeftX + InnerW - 52.0f, CurY + 11.0f, 42.0f, 22.0f };
        Surface.FillRectangle(PillRect, { 18, 20, 25, 255 }, 11.0f);
        Surface.StrokeRectangle(PillRect, BorderCol, 11.0f, 1.0f);
        Surface.TextCentered(PillRect, 11.0f, IvoryText, SharePill);

        if (Clicked && RowRect.Contains(Cursor))
        {
            Session->SelectPriorityLayer(Layer.LayerOrdinal);
        }
        CurY += 50.0f;
    }

    // Remainder Auto-Calculated Row (Ordinal 0)
    {
        const Rectangle2D RemRect { LeftX, CurY, InnerW, 42.0f };
        const bool Selected = (Session->SelectedLayerOrdinal == 0u);
        Surface.FillRectangle(RemRect, Selected ? CardActive : CardBg, 6.0f);
        Surface.StrokeRectangle(RemRect, Selected ? AccentBlue : BorderCol, 6.0f, 1.0f);

        const Rectangle2D Swatch { LeftX + 10.0f, CurY + 10.0f, 14.0f, 22.0f };
        Surface.FillRectangle(Swatch, { 132, 142, 158, 255 }, 3.0f);
        Surface.Text({ LeftX + 32.0f, CurY + 7.0f }, 12.0f, CrispText, "Unassigned Remainder");

        const auto& RemRef = Session->Asset.Layers.InspectLayerReference(Session->Asset.Network, 0u, Session->RootInstanceToken);
        char RemSub[96]{};
        std::snprintf(RemSub, sizeof(RemSub), "%zu faces  |  Auto Share", RemRef.FaceIndices.size());
        Surface.Text({ LeftX + 32.0f, CurY + 23.0f }, 10.5f, MutedText, RemSub);

        char RemPct[32]{};
        std::snprintf(RemPct, sizeof(RemPct), "%.0f%%", Session->Asset.Layers.DeriveRemainderSharePercent());
        Surface.TextRightAligned({ LeftX, CurY + 12.0f, InnerW - 12.0f, 18.0f }, 11.5f, AccentBlue, RemPct);

        if (Clicked && RemRect.Contains(Cursor))
        {
            Session->SelectPriorityLayer(0u);
        }
        CurY += 52.0f;
    }

    // 4. UDIM Tile Selector Strip
    Surface.Text({ LeftX, CurY }, 11.0f, MutedText, "ACTIVE UDIM TILES");
    CurY += 18.0f;
    const std::size_t TileCount = std::max<std::size_t>(1u, Session->Asset.UDIMTiles.size());
    const float TileChipW       = std::min(72.0f, (InnerW - static_cast<float>(TileCount - 1u) * 6.0f) / static_cast<float>(TileCount));
    for (std::size_t TIdx = 0u; TIdx < Session->Asset.UDIMTiles.size(); ++TIdx)
    {
        const uint32_t Code       = Session->Asset.UDIMTiles[TIdx];
        const bool     TileActive = (Session->ActiveUDIMNumber == Code);
        const Rectangle2D TRect { LeftX + static_cast<float>(TIdx) * (TileChipW + 6.0f), CurY, TileChipW, 26.0f };
        Surface.FillRectangle(TRect, TileActive ? CardActive : CardBg, 5.0f);
        Surface.StrokeRectangle(TRect, TileActive ? AccentBlue : BorderCol, 5.0f, 1.0f);

        char CodeLabel[24]{};
        std::snprintf(CodeLabel, sizeof(CodeLabel), "%u", Code);
        Surface.TextCentered(TRect, 11.5f, TileActive ? IvoryText : CrispText, CodeLabel);
        if (Clicked && TRect.Contains(Cursor))
        {
            Session->ActiveUDIMNumber = Code;
        }
    }
    CurY += 38.0f;

    // 5. GeometryInstance Non-Kinship Summary Footer
    if (CurY + 56.0f < Bounds.Bottom())
    {
        Surface.Text({ LeftX, CurY }, 11.0f, MutedText, "GEOMETRYINSTANCES");
        CurY += 18.0f;
        for (const auto& Tok : Session->Instances.EnumerateLiveInstances())
        {
            if (CurY + 26.0f > Bounds.Bottom())
            {
                break;
            }
            const auto* Inst = Session->Instances.InspectInstance(Tok);
            if (Inst != nullptr)
            {
                const Rectangle2D InstRow { LeftX, CurY, InnerW, 24.0f };
                Surface.FillRectangle(InstRow, CardBg, 4.0f);
                Surface.Text({ LeftX + 10.0f, CurY + 5.0f }, 11.0f, CrispText, Inst->InstanceTitle.c_str());
                CurY += 28.0f;
            }
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
