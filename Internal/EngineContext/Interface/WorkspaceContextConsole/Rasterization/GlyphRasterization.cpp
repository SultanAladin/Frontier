/*==============================================================================================================================================
                                                          GLYPHRASTERIZATION.CPP
==============================================================================================================================================*/
// 🧩 Turns a glyph NAME into drawn pixels for every strip (rail, action grid, probe, parameters). During the console build-out this draws ONE
//    placeholder mark for every name — no SVG art, no registry lookup — so the layout, gating, and carousel can be proven before any real icon is
//    authored. The SvgIconRegistry* parameter is kept in the signature for the eventual wiring but is deliberately unused here.
//
//    📝 The placeholder is a rounded square with an inset diagonal — obvious enough that a real icon is visibly absent, uniform enough that every
//       strip lays out at the size it will use once art lands. GatedCondition only dims it; the prototype recolours a gated mark, and a flat dim is
//       the honest stand-in for that until the two-document ink split returns with real SVGs.

#include "GlyphRasterization.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InscribeConsoleGlyph(const SvgIconRegistry* Icons,
                          const char*            GlyphName,
                          ImVec2                 TopLeft,
                          float                  EdgePixels,
                          bool                   GatedCondition)
{
    (void)Icons;       // 📝 kept for the eventual real-art wiring; unused while every glyph is the placeholder
    (void)GlyphName;   // 📝 the placeholder is name-agnostic by design during build-out

    ImDrawList* const Canvas = ImGui::GetWindowDrawList();

    const ImVec2 BottomRight(TopLeft.x + EdgePixels, TopLeft.y + EdgePixels);
    const float  Rounding = EdgePixels * 0.18f;
    const float  Inset    = EdgePixels * 0.22f;

    // 📝 A live mark reads at full ink; a gated one is dimmed to roughly a third, the same read-weight a real gated glyph would carry.
    const ImU32 Ink = GatedCondition ? IM_COL32(0x9A, 0x9A, 0xA2, 0x66)
                                     : IM_COL32(0xD6, 0xD6, 0xDC, 0xFF);

    Canvas->AddRect(TopLeft, BottomRight, Ink, Rounding, 0, 1.5f);
    Canvas->AddLine(ImVec2(TopLeft.x + Inset,     BottomRight.y - Inset),
                    ImVec2(BottomRight.x - Inset, TopLeft.y + Inset), Ink, 1.5f);
}

} // namespace Frontier
