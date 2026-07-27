/*==============================================================================================================================================
                                                              THEMERESOLVER.CPP
==============================================================================================================================================*/
// 🧩 Combines palette + metrics into the active theme and mirrors it into ImGui's global style. This is the seam between Frontier's own
//    theme description and ImGui's style array — every colour handed to ImGui here traces back to ColorPaletteDescriptor.

#include "ThemeResolver.h"

#include "ColorPaletteDescriptor.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Fetch the four float channels of a packed colour — ImGui's style array is float-based.
    ImVec4 UnpackColor(ImU32 Packed)
    {
        return ImGui::ColorConvertU32ToFloat4(Packed);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ThemeConfiguration ResolveActiveTheme()
{
    ThemeConfiguration Theme = {};
    Theme.Palette = ResolveDarkPalette();
    Theme.Metrics = ResolveDefaultMetrics();
    return Theme;
}


void EnforceThemeStyle(const ThemeConfiguration& Theme)
{
    ImGuiStyle& Style = ImGui::GetStyle();

    Style.WindowRounding    = Theme.Metrics.CornerRounding;
    Style.FrameRounding     = Theme.Metrics.CornerRounding;
    Style.PopupRounding     = Theme.Metrics.CornerRounding;
    Style.WindowBorderSize  = Theme.Metrics.BorderThickness;
    Style.FrameBorderSize   = Theme.Metrics.BorderThickness;
    Style.WindowPadding     = ImVec2(Theme.Metrics.PanelPadding, Theme.Metrics.PanelPadding);
    Style.FramePadding      = ImVec2(6.0f, 3.0f);
    Style.ItemSpacing       = ImVec2(Theme.Metrics.ControlSpacing, Theme.Metrics.ControlSpacing);
    Style.IndentSpacing     = Theme.Metrics.IndentWidth;

    ImVec4* Colors = Style.Colors;
    Colors[ImGuiCol_WindowBg]         = UnpackColor(Theme.Palette.PanelBackground);
    Colors[ImGuiCol_ChildBg]          = UnpackColor(Theme.Palette.PanelBackground);
    Colors[ImGuiCol_PopupBg]          = UnpackColor(Theme.Palette.PanelHeader);
    Colors[ImGuiCol_Border]           = UnpackColor(Theme.Palette.PanelBorder);
    Colors[ImGuiCol_FrameBg]          = UnpackColor(Theme.Palette.ControlBackground);
    Colors[ImGuiCol_FrameBgHovered]   = UnpackColor(Theme.Palette.ControlHovered);
    Colors[ImGuiCol_FrameBgActive]    = UnpackColor(Theme.Palette.ControlActive);
    Colors[ImGuiCol_TitleBg]          = UnpackColor(Theme.Palette.PanelHeader);
    Colors[ImGuiCol_TitleBgActive]    = UnpackColor(Theme.Palette.PanelHeader);
    Colors[ImGuiCol_Header]           = UnpackColor(Theme.Palette.AccentSubtle);
    Colors[ImGuiCol_HeaderHovered]    = UnpackColor(Theme.Palette.ControlHovered);
    Colors[ImGuiCol_HeaderActive]     = UnpackColor(Theme.Palette.AccentPrimary);
    Colors[ImGuiCol_Button]           = UnpackColor(Theme.Palette.ControlBackground);
    Colors[ImGuiCol_ButtonHovered]    = UnpackColor(Theme.Palette.ControlHovered);
    Colors[ImGuiCol_ButtonActive]     = UnpackColor(Theme.Palette.ControlActive);
    Colors[ImGuiCol_SliderGrab]       = UnpackColor(Theme.Palette.AccentPrimary);
    Colors[ImGuiCol_SliderGrabActive] = UnpackColor(Theme.Palette.AccentPrimary);
    Colors[ImGuiCol_CheckMark]        = UnpackColor(Theme.Palette.AccentPrimary);
    Colors[ImGuiCol_Text]             = UnpackColor(Theme.Palette.TextPrimary);
    Colors[ImGuiCol_TextDisabled]     = UnpackColor(Theme.Palette.TextMuted);
    Colors[ImGuiCol_Separator]        = UnpackColor(Theme.Palette.PanelBorder);
    Colors[ImGuiCol_Tab]              = UnpackColor(Theme.Palette.ControlBackground);
    Colors[ImGuiCol_TabHovered]       = UnpackColor(Theme.Palette.ControlHovered);
    Colors[ImGuiCol_TabActive]        = UnpackColor(Theme.Palette.AccentPrimary);
}

}   // namespace Frontier
