/*==============================================================================================================================================
                                                      INACTIVEINSTRUMENTLINEARIZER.CPP
==============================================================================================================================================*/
// 🧩 The bottom-left pill tray: collapsed instruments flattened into a 1D row of rounded pills that re-open on click

#include "InactiveInstrumentLinearizer.h"
#include "../InstrumentProjection/InstrumentBodyContext.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const float PillHeight     = 24.0f;   // [px] - pill height
static const float PillGap        = 8.0f;    // [px] - gap between pills
static const float PillPaddingX   = 12.0f;   // [px] - horizontal text padding inside a pill
static const float TrayMarginX    = 16.0f;   // [px] - tray inset from the viewport left edge
static const float TrayMarginY    = 16.0f;   // [px] - tray inset from the viewport bottom edge

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Pin one borderless full-width strip to the lower-left, then walk the store packing each collapsed record into a rounded
// solid pill left-to-right, tinted with that record's accent. A hover brightens the pill; a click reports its slot for
// re-opening. The tray draws nothing (and reserves no space) when no instrument is collapsed.
InactiveLinearizerOutcome ConstructInactiveInstrumentRow(InstrumentRecordArchive& Store, float ViewportHeight)
{
    InactiveLinearizerOutcome Outcome;

    ImGui::SetNextWindowPos(ImVec2(TrayMarginX, ViewportHeight - TrayMarginY - PillHeight - 8.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));   // 📝 The strip itself is invisible; only pills draw.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##inactive-instrument-row", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_AlwaysAutoResize);

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    ImFont*     Font     = ImGui::GetFont();
    ImVec2      Cursor   = ImGui::GetCursorScreenPos();

    float PenX = Cursor.x;
    float PenY = Cursor.y;

    for (uint32_t Slot = 0u; Slot < Store.HighWaterMark; ++Slot)
    {
        InstrumentRecordEntry& Record = Store.Records[Slot];
        if (!Record.Occupied || !Record.Collapsed)
        {
            continue;
        }

        ImVec2 TextExtent = Font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, Record.Title);
        float  PillWidth  = TextExtent.x + PillPaddingX * 2.0f;

        ImVec2 PillMinimum(PenX, PenY);
        ImVec2 PillMaximum(PenX + PillWidth, PenY + PillHeight);

        bool Hovered = ImGui::IsMouseHoveringRect(PillMinimum, PillMaximum);

        const InstrumentTileDescriptor& Look = Record.Presentation;
        ImU32 PillFill   = Hovered ? IM_COL32(28, 28, 34, 255) : ResolveInstrumentColour(Look.EnclosureFill);
        ImU32 PillBorder = ResolveInstrumentColour(Look.AccentPrimary);
        DrawList->AddRectFilled(PillMinimum, PillMaximum, PillFill, PillHeight * 0.5f);
        DrawList->AddRect(PillMinimum, PillMaximum, PillBorder, PillHeight * 0.5f, 0, 1.0f);
        DrawList->AddText(ImVec2(PenX + PillPaddingX, PenY + (PillHeight - TextExtent.y) * 0.5f),
                          ResolveInstrumentColour(Look.TitleTint), Record.Title);

        if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            Outcome.ReopenRequested = true;
            Outcome.ReopenSlotIndex = Slot;
        }

        PenX += PillWidth + PillGap;
    }

    // 📝 Reserve the row's height so ImGui's auto-resize gives the invisible strip a sane extent (avoids a zero-size window).
    ImGui::Dummy(ImVec2(PenX - Cursor.x, PillHeight));

    ImGui::End();
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(1);
    return Outcome;
}

}   // namespace Frontier
