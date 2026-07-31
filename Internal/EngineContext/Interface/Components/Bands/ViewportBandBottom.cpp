/*==============================================================================================================================================
                                                            VIEWPORTBANDBOTTOM.CPP
==============================================================================================================================================*/
// 🧩 Paints the 30 px footer band and lays out its runs. The fill is the SAME header tone the top band carries and carries no edge hairline, so
//    the two bands frame the canvas as one continuous chrome family. The hint text is deliberately dimmer than muted body copy — it is a
//    persistent reminder, not content. The control cluster is right-aligned from the span the caller declares; a pill recorded here should carry
//    an Above* anchor so its menu overhangs the canvas upward.

#include "ViewportBandBottom.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    const float BandHeight     = 30.0f;   // [px] - `--footer-h`
    const float BandPaddingX   = 14.0f;   // [px] - `.vp-footer` horizontal padding
    const float RunGap          = 10.0f;  // [px] - gap between the coordinate run and the control cluster
    const float HintTextScale   =  0.85f; // [-]  - 11 px runs against the 13 px base font
    const float ControlHeight   = 22.0f;  // [px] - `.vp-pill-sm` height; the footer hosts the compact pill variant

    // 📝 The mockup's hint tone (#4a4a52) — a literal because the shared palette carries no "fainter than muted" text tone.
    //    Everything else in the band comes from the theme, the fill included, so header and footer read as one chrome family.
    const ImU32 HintTextTone = IM_COL32(74, 74, 82, 255);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ResolveViewportBandBottomHeight()
{
    return BandHeight;
}


void BeginViewportBandBottom(const ThemeConfiguration& Theme, const ViewportBandBottomDescriptor& Descriptor)
{
    const ImVec2 BandMin = ImGui::GetCursorScreenPos();
    float BandSpan       = ImGui::GetContentRegionAvail().x;
    if (BandSpan < 1.0f) { BandSpan = 1.0f; }
    const ImVec2 BandMax(BandMin.x + BandSpan, BandMin.y + BandHeight);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(BandMin, BandMax, Theme.Palette.PanelHeader);

    const float MidY      = (BandMin.y + BandMax.y) * 0.5f;
    const float RunHeight = ImGui::GetFontSize() * HintTextScale;

    if (Descriptor.NavigationHintText != nullptr)
    {
        Draw->AddText(ImGui::GetFont(), RunHeight,
                      ImVec2(BandMin.x + BandPaddingX, MidY - RunHeight * 0.5f),
                      HintTextTone, Descriptor.NavigationHintText);
    }

    // -- Open the band region and park the cursor in the trailing cluster -----------------------------------------------
    ImGui::PushID(Descriptor.Identifier != nullptr ? Descriptor.Identifier : "##viewport-band-bottom");
    ImGui::BeginChild("##band", ImVec2(BandSpan, BandHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoBackground);

    float ClusterX = BandMin.x + BandPaddingX;
    if (Descriptor.TrailingClusterSpan > 0.0f)
    {
        ClusterX = BandMax.x - BandPaddingX - Descriptor.TrailingClusterSpan;
    }

    // 📝 The coordinate readout sits immediately LEFT of the control cluster, so it never collides with the pills.
    if (Descriptor.CoordinateText != nullptr)
    {
        const float ReadoutSpan = ImGui::CalcTextSize(Descriptor.CoordinateText).x * HintTextScale;
        Draw->AddText(ImGui::GetFont(), RunHeight,
                      ImVec2(ClusterX - RunGap - ReadoutSpan, MidY - RunHeight * 0.5f),
                      Theme.Palette.TextMuted, Descriptor.CoordinateText);
    }

    ImGui::SetCursorScreenPos(ImVec2(ClusterX, MidY - ControlHeight * 0.5f));
}


void EndViewportBandBottom(const ThemeConfiguration& Theme)
{
    (void)Theme;

    ImGui::EndChild();
    ImGui::PopID();
}

}   // namespace Frontier
