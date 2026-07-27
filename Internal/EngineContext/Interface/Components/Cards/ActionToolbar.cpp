/*==============================================================================================================================================
                                                              ACTIONTOOLBAR.CPP
==============================================================================================================================================*/
// 🧩 Records a horizontal strip of square tool buttons through raw ImGui, tinting the active tool with the theme accent. Stateless; the
//    caller owns the tool set and its active/enabled flags. The icon texture is looked up by handle so this file stays SVG-agnostic.

#include "ActionToolbar.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

int ConstructActionToolbar(const ThemeConfiguration& Theme, const ActionToolbarDescriptor& Descriptor)
{
    if (Descriptor.Items == nullptr || Descriptor.ItemCount <= 0)
    {
        return -1;
    }

    const float Size = Descriptor.ButtonSize > 0.0f ? Descriptor.ButtonSize : Theme.Metrics.ControlHeight;
    const char* Identifier = Descriptor.Identifier ? Descriptor.Identifier : "##toolbar";

    int Pressed = -1;

    ImGui::PushID(Identifier);
    for (int Index = 0; Index < Descriptor.ItemCount; ++Index)
    {
        const ActionToolItem& Item = Descriptor.Items[Index];

        if (Index > 0)
        {
            ImGui::SameLine(0.0f, Theme.Metrics.ControlSpacing);
        }

        ImGui::PushID(Index);
        if (!Item.Enabled)
        {
            ImGui::BeginDisabled();
        }

        // 📝 The active tool wears the accent fill so the current mode is unmistakable.
        if (Item.Active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme.Palette.AccentPrimary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme.Palette.AccentPrimary);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme.Palette.AccentPrimary);
        }

        bool Clicked;
        if (Item.IconRasterId != 0)
        {
            const ImTextureID Texture = static_cast<ImTextureID>(static_cast<intptr_t>(Item.IconRasterId));
            Clicked = ImGui::ImageButton("##tool", Texture, ImVec2(Size, Size));
        }
        else
        {
            Clicked = ImGui::Button(Item.Caption ? Item.Caption : "##tool", ImVec2(Size, Size));
        }

        if (Item.Active)
        {
            ImGui::PopStyleColor(3);
        }

        if (Clicked && Item.Enabled)
        {
            Pressed = Index;
        }

        // 📝 Icon buttons surface their caption as a tooltip so the strip stays legible without labels.
        if (Item.Caption && Item.IconRasterId != 0 && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", Item.Caption);
        }

        if (!Item.Enabled)
        {
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    return Pressed;
}

}   // namespace Frontier
