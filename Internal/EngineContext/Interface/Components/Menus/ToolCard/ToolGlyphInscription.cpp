/*==============================================================================================================================================
                                                        TOOLGLYPHINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Resolves a glyph name to its uploaded SVG texture and composites it into the current draw list. The registry already dedups by content hash and
//    hands back an ImTextureID, so this unit is thin by design — its whole job is to make the key derivation and the missing-key case uniform.

#include "ToolGlyphInscription.h"

#include "../../../Icons/IconPackToolMenu.h"
#include "../../../Icons/SvgIconRegistry.h"

#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The stand-in for a name the registry does not know: a dotted square at the glyph's own footprint. Deliberately obvious
    //    rather than graceful — a silent gap would let a band table reference a glyph that was never authored and still look
    //    plausible, which is how a whole family of marks goes missing without anyone noticing.
    void InscribeAbsentGlyphMarker(ImVec2 TopLeft, float EdgePixels)
    {
        ImDrawList* Canvas = ImGui::GetWindowDrawList();
        const ImVec2 BottomRight(TopLeft.x + EdgePixels, TopLeft.y + EdgePixels);
        const ImU32  MarkerInk = IM_COL32(0xC9, 0xA2, 0x27, 0xB0);   // the same amber a gated tile explains itself in

        // A dashed outline, drawn as short segments so no dash-pattern support is assumed of the draw list.
        const float DashStep = EdgePixels * 0.25f;
        for (float Offset = 0.0f; Offset < EdgePixels; Offset += DashStep * 2.0f)
        {
            const float Span = (Offset + DashStep < EdgePixels) ? DashStep : (EdgePixels - Offset);
            Canvas->AddLine(ImVec2(TopLeft.x + Offset, TopLeft.y),
                            ImVec2(TopLeft.x + Offset + Span, TopLeft.y), MarkerInk, 1.0f);
            Canvas->AddLine(ImVec2(TopLeft.x + Offset, BottomRight.y),
                            ImVec2(TopLeft.x + Offset + Span, BottomRight.y), MarkerInk, 1.0f);
            Canvas->AddLine(ImVec2(TopLeft.x, TopLeft.y + Offset),
                            ImVec2(TopLeft.x, TopLeft.y + Offset + Span), MarkerInk, 1.0f);
            Canvas->AddLine(ImVec2(BottomRight.x, TopLeft.y + Offset),
                            ImVec2(BottomRight.x, TopLeft.y + Offset + Span), MarkerInk, 1.0f);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InscribeToolGlyph(const SvgIconRegistry* Icons,
                       const char*            GlyphName,
                       ImVec2                 TopLeft,
                       float                  EdgePixels,
                       bool                   GatedCondition)
{
    if (Icons == nullptr || GlyphName == nullptr)
    {
        InscribeAbsentGlyphMarker(TopLeft, EdgePixels);
        return;
    }

    const std::string IconKey    = ResolveToolGlyphKey(GlyphName, GatedCondition);
    const ImTextureID IconTexture = ResolveIconTexture(*Icons, IconKey);
    if (IconTexture == 0)
    {
        InscribeAbsentGlyphMarker(TopLeft, EdgePixels);
        return;
    }

    // 📝 The texture is one supersampled master minified by the registry's linear sampler; no per-size raster exists to pick.
    ImGui::GetWindowDrawList()->AddImage(IconTexture, TopLeft,
                                         ImVec2(TopLeft.x + EdgePixels, TopLeft.y + EdgePixels));
}


void InscribeStratumBadge(const SvgIconRegistry* Icons,
                          const char*            StratumName,
                          ImVec2                 TopLeft,
                          float                  EdgePixels)
{
    if (Icons == nullptr || StratumName == nullptr)
    {
        InscribeAbsentGlyphMarker(TopLeft, EdgePixels);
        return;
    }

    std::string IconKey = "tool-badge-";
    IconKey += StratumName;

    const ImTextureID BadgeTexture = ResolveIconTexture(*Icons, IconKey);
    if (BadgeTexture == 0)
    {
        InscribeAbsentGlyphMarker(TopLeft, EdgePixels);
        return;
    }

    ImGui::GetWindowDrawList()->AddImage(BadgeTexture, TopLeft,
                                         ImVec2(TopLeft.x + EdgePixels, TopLeft.y + EdgePixels));
}

} // namespace Frontier
