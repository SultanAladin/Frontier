/*==============================================================================================================================================
                                                              VIEWPORTBANDTOP.CPP
==============================================================================================================================================*/
// 🧩 Paints the 52 px band and lays out its two clusters. The band is a child region so the caller's pills cannot escape it horizontally, but
//    their MENUS still float above the canvas because ImGui popups are their own windows — which is exactly the mockup's `overflow:visible` on
//    `.vp-topbar`. The title cluster stacks a 14 px name over an 11 px faint subtitle, matching the ToolOptionsWidget header language the mockup
//    reuses here; the trailing cluster is right-aligned from the span the caller declares.

#include "ViewportBandTop.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    const float BandHeight        = 52.0f;   // [px] - `.vp-topbar` height
    const float BandPaddingLeft   = 14.0f;   // [px] - `.vp-topbar` padding-left
    const float BandPaddingRight  = 12.0f;   // [px] - `.vp-topbar` padding-right
    const float TitleGlyphEdge    = 26.0f;   // [px] - `.vp-ic` box
    const float TitleContentGap   = 11.0f;   // [px] - `.vp-title` gap
    const float SubtitleOffset    =  3.0f;   // [px] - `.vp-sub` margin-top
    const float SubtitleTextScale =  0.85f;  // [-]  - 11 px subtitle against the 13 px base font

    // 📝 Where the leading cluster ended on THIS cycle, published for a caller that records a control there. Band recording is
    //    strictly sequential within one ImGui frame (Begin … End, never nested), so a single slot is sufficient and correct.
    ImVec2 LeadingClusterCursor = ImVec2(0.0f, 0.0f);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ResolveViewportBandTopHeight()
{
    return BandHeight;
}


void BeginViewportBandTop(const ThemeConfiguration& Theme, const ViewportBandTopDescriptor& Descriptor)
{
    const ImVec2 BandMin = ImGui::GetCursorScreenPos();
    float BandSpan       = ImGui::GetContentRegionAvail().x;
    if (BandSpan < 1.0f) { BandSpan = 1.0f; }
    const ImVec2 BandMax(BandMin.x + BandSpan, BandMin.y + BandHeight);

    // 📝 Fill painted into the PARENT draw list, before the child region opens, so the band reads as one continuous strip
    //    regardless of what the caller records inside it. No edge hairline: the tone step against the canvas is the only
    //    separator, so nothing bright runs across the chrome.
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(BandMin, BandMax, Theme.Palette.PanelHeader);

    // -- Leading cluster: glyph + stacked name/subtitle -----------------------------------------------------------------
    float CursorX    = BandMin.x + BandPaddingLeft;
    const float MidY = (BandMin.y + BandMax.y) * 0.5f;

    if (Descriptor.IconTexture != 0)
    {
        const ImVec2 GlyphMin(CursorX, MidY - TitleGlyphEdge * 0.5f);
        Draw->AddImage(Descriptor.IconTexture, GlyphMin,
                       ImVec2(GlyphMin.x + TitleGlyphEdge, GlyphMin.y + TitleGlyphEdge));
        CursorX += TitleGlyphEdge + TitleContentGap;
    }

    if (Descriptor.TitleText != nullptr)
    {
        const ImVec2 TitleSize = ImGui::CalcTextSize(Descriptor.TitleText);
        if (Descriptor.SubtitleText != nullptr)
        {
            // 📝 Two stacked runs: centre the PAIR on the band midline, then place each run within it.
            const float SubtitleHeight = TitleSize.y * SubtitleTextScale;
            const float StackHeight    = TitleSize.y + SubtitleOffset + SubtitleHeight;
            const float StackTop       = MidY - StackHeight * 0.5f;

            Draw->AddText(ImVec2(CursorX, StackTop), Theme.Palette.TextPrimary, Descriptor.TitleText);
            Draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * SubtitleTextScale,
                          ImVec2(CursorX, StackTop + TitleSize.y + SubtitleOffset),
                          Theme.Palette.TextMuted, Descriptor.SubtitleText);
            CursorX += TitleSize.x;
        }
        else
        {
            Draw->AddText(ImVec2(CursorX, MidY - TitleSize.y * 0.5f), Theme.Palette.TextPrimary, Descriptor.TitleText);
            CursorX += TitleSize.x;
        }
        CursorX += TitleContentGap;
    }

    // -- Open the band region and park the cursor where the caller's controls go ----------------------------------------
    ImGui::PushID(Descriptor.Identifier != nullptr ? Descriptor.Identifier : "##viewport-band-top");
    ImGui::BeginChild("##band", ImVec2(BandSpan, BandHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoBackground);

    // 📝 Right-align the trailing cluster from the span the caller declared (the mockup's `space-between`); fall back to just
    //    after the title cluster when no span was given. Either way the controls sit vertically centred in the band.
    float ClusterX = CursorX;
    if (Descriptor.TrailingClusterSpan > 0.0f)
    {
        const float AlignedX = BandMax.x - BandPaddingRight - Descriptor.TrailingClusterSpan;
        ClusterX = (AlignedX > CursorX) ? AlignedX : CursorX;
    }
    const float ControlHeight = Theme.Metrics.PillRowHeight > 1.0f ? Theme.Metrics.PillRowHeight
                                                                  : ImGui::GetFrameHeight();
    const float ControlTop    = MidY - ControlHeight * 0.5f;

    // 📝 Publish the leading-cluster continuation point before parking the cursor in the trailing cluster.
    LeadingClusterCursor = ImVec2(CursorX, ControlTop);

    ImGui::SetCursorScreenPos(ImVec2(ClusterX, ControlTop));
}


ImVec2 ResolveViewportBandTopLeadingCursor()
{
    return LeadingClusterCursor;
}


void EndViewportBandTop(const ThemeConfiguration& Theme)
{
    (void)Theme;

    ImGui::EndChild();
    ImGui::PopID();

    // 📝 The child already advanced the parent cursor by the band height; nothing further to reserve.
}

}   // namespace Frontier
