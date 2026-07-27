/*==============================================================================================================================================
                                                              CONTENTCAROUSEL.CPP
==============================================================================================================================================*/
// 🧩 Records a horizontally-scrolling strip of thumbnail tiles. Stateless; the caller owns the tiles and the selected index. The selected
//    tile wears the accent border. Textures are looked up by handle so this file stays preview-source-agnostic.

#include "ContentCarousel.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructContentCarousel(const ThemeConfiguration& Theme, const ContentCarouselDescriptor& Descriptor)
{
    if (Descriptor.Items == nullptr || Descriptor.ItemCount <= 0)
    {
        return false;
    }

    const char* Identifier = Descriptor.Identifier ? Descriptor.Identifier : "##carousel";
    const float TileSize    = Descriptor.TileSize > 0.0f ? Descriptor.TileSize : Theme.Metrics.RowHeight * 4.0f;

    // 📝 A fixed-height horizontally-scrolling child holds the strip; caption + padding set the row height.
    const float StripHeight = TileSize + ImGui::GetTextLineHeightWithSpacing() + Theme.Metrics.PanelPadding * 2.0f;

    bool Changed = false;

    ImGui::PushID(Identifier);
    ImGui::BeginChild("##strip", ImVec2(0.0f, StripHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (int Index = 0; Index < Descriptor.ItemCount; ++Index)
    {
        const CarouselItem& Item = Descriptor.Items[Index];
        const bool Selected = (Descriptor.SelectedIndex != nullptr && Descriptor.SelectedIndex[0] == Index);

        if (Index > 0)
        {
            ImGui::SameLine(0.0f, Theme.Metrics.ControlSpacing);
        }

        ImGui::PushID(Index);
        ImGui::BeginGroup();

        // 📝 The selected tile gets an accent border; others get the subtle panel border.
        ImGui::PushStyleColor(ImGuiCol_Border, Selected ? Theme.Palette.AccentPrimary : Theme.Palette.PanelBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Selected ? 2.0f : Theme.Metrics.BorderThickness);

        bool Clicked;
        if (Item.ThumbnailRasterId != 0)
        {
            const ImTextureID Texture = static_cast<ImTextureID>(static_cast<intptr_t>(Item.ThumbnailRasterId));
            Clicked = ImGui::ImageButton("##tile", Texture, ImVec2(TileSize, TileSize));
        }
        else
        {
            Clicked = ImGui::Button("##tile", ImVec2(TileSize, TileSize));
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // 📝 Caption clipped to the tile width beneath the thumbnail.
        if (Item.Caption)
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + TileSize);
            ImGui::TextUnformatted(Item.Caption);
            ImGui::PopTextWrapPos();
        }

        ImGui::EndGroup();

        if (Clicked && Descriptor.SelectedIndex != nullptr)
        {
            Descriptor.SelectedIndex[0] = Index;
            Changed = true;
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopID();

    return Changed;
}

}   // namespace Frontier
