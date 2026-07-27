/*==============================================================================================================================================
                                                              SECTIONHEADER.CPP
==============================================================================================================================================*/
// 🧩 Records a collapsible section header row, custom-drawn to the UVeditor .prop-title look: an uppercase muted caption on the left and a
//    chevron on the right that ROTATES with an eased fold fraction (not a binary flip), so the card behind it can reveal / hide its body with
//    a smooth ease. NO frame and NO border — the rounded card (PropertyPanelBase) supplies the fill, so headers never read as a stacked,
//    outlined, merged block.
//
//    🔴 The header hit-rect is a FIXED height and does NOT move with the fold — this is the guard against open/close oscillation. If the
//       clickable strip grew or shrank with the animated body, the cursor would land on a different strip the next frame and the card would
//       flicker open↔closed. The header owns only the toggle EDGE: it reports whether it was clicked THIS frame (one edge per press); the
//       caller flips the persisted intent once and eases the body toward it. Stateless — no fold / intent state lives here.

#include "SectionHeader.h"

#include "imgui.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Draw a chevron glyph rotated by Angle (radians) about Centre. Angle 0 => pointing down (v, card open); Angle -90° => pointing
    //    right (>, card collapsed). The eased fold fraction drives the angle so the chevron sweeps as the body reveals.
    void DrawChevron(ImDrawList* Draw, ImVec2 Centre, float Radius, float Angle, ImU32 Color)
    {
        const float Cos = std::cos(Angle);
        const float Sin = std::sin(Angle);
        auto Rotate = [&](float LocalX, float LocalY) -> ImVec2
        {
            return ImVec2(Centre.x + LocalX * Cos - LocalY * Sin, Centre.y + LocalX * Sin + LocalY * Cos);
        };
        // The resting (down) chevron: a "v" from (-R,-0.4R) → (0,0.5R) → (R,-0.4R). Rotation carries it to the collapsed ">".
        Draw->AddLine(Rotate(-Radius, -Radius * 0.4f), Rotate(0.0f, Radius * 0.5f), Color, 1.6f);
        Draw->AddLine(Rotate(0.0f, Radius * 0.5f),     Rotate(Radius, -Radius * 0.4f), Color, 1.6f);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructSectionHeader(const ThemeConfiguration& Theme, const SectionHeaderDescriptor& Descriptor)
{
    const char* Title = Descriptor.Title ? Descriptor.Title : "";

    ImGui::PushID(Title);

    // 📝 A borderless full-width clickable row (UVeditor .prop-title: padding 10px 12px). The whole strip toggles the section. The height
    //    is FIXED (never scaled by the fold) so the hit target stays put across the reveal animation — the oscillation guard.
    const float  RowH   = ImGui::GetFontSize() + 20.0f;   // ~ .prop-title vertical padding
    const float  Width  = ImGui::GetContentRegionAvail().x;
    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##header", ImVec2(Width > 1.0f ? Width : 1.0f, RowH));
    const bool Clicked  = Descriptor.Expanded != nullptr && ImGui::IsItemClicked();
    const bool Hovered  = ImGui::IsItemHovered();

    ImDrawList* Draw = ImGui::GetWindowDrawList();

    // 📝 Caption: uppercase, muted, letter-spaced feel (drawn as-is; the caller passes the display title). Left inset 12px. Brightens on
    //    hover so the strip reads as clickable.
    const ImVec2 TextSize = ImGui::CalcTextSize(Title);
    Draw->AddText(ImVec2(Origin.x + 12.0f, Origin.y + (RowH - TextSize.y) * 0.5f),
                  Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted, Title);

    // 📝 Chevron on the right (only when collapsible), 16px inset, its angle driven by the eased fold: fully open (Fold=1) points down;
    //    fully collapsed (Fold=0) points right. The caller supplies the eased fraction so the sweep matches the body reveal.
    if (Descriptor.Expanded != nullptr)
    {
        const float  Fold   = Descriptor.FoldFraction < 0.0f ? 0.0f : (Descriptor.FoldFraction > 1.0f ? 1.0f : Descriptor.FoldFraction);
        const float  Angle  = (1.0f - Fold) * (-1.5708f);   // 0 rad (down) at open, -90° (right) at collapsed
        const ImVec2 Centre(Origin.x + Width - 16.0f, Origin.y + RowH * 0.5f);
        DrawChevron(Draw, Centre, 4.5f, Angle, Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted);
    }

    ImGui::PopID();

    return Clicked;
}

}   // namespace Frontier
