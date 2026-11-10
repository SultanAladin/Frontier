/*==========================================================================================================================================
                                                               EDITORTHEME.H
============================================================================================================================================*/
// 🧩 Appearance-driven palette for the editor's own panels: ThemeStructure colours resolved to ImGui inks.

#pragma once

#include <imgui.h>

namespace Frontier
{

struct ColorQuad;
struct ThemePalette;

//----------------------------------------------------------------------------------------------------------------------
//                                                       EDITOR THEME
//----------------------------------------------------------------------------------------------------------------------

// 📝 Field-for-field counterpart of the kit's constants, but resolved every cycle from the Control
//    Centre's Appearance settings (theme + accent) through ThemeStructure — the editor's own panels
//    follow the user's appearance instead of carrying a fixed palette. WorkspaceHost resolves it in
//    Synchronize() and hands it to the panels before they construct.
struct EditorTheme
{
    ImU32 PanelBg    = IM_COL32(16, 18, 22, 255);      // PanelBackground
    ImU32 PanelEdge  = IM_COL32(255, 255, 255, 20);    // PanelBorder
    ImU32 Surface    = IM_COL32(22, 25, 31, 255);      // CardBackground
    ImU32 Surface2   = IM_COL32(29, 33, 41, 255);      // CardSubBackground / fields
    ImU32 Surface3   = IM_COL32(38, 43, 54, 255);      // ActiveBackground
    ImU32 Line       = IM_COL32(255, 255, 255, 18);    // DividerColor
    ImU32 LineSoft   = IM_COL32(255, 255, 255, 11);    // DividerColor, softened
    ImU32 RowHover   = IM_COL32(255, 255, 255, 8);     // Hover wash
    ImU32 Text       = IM_COL32(238, 240, 243, 255);   // TextMain
    ImU32 Muted      = IM_COL32(154, 160, 170, 255);   // TextMuted
    ImU32 Faint      = IM_COL32(154, 160, 170, 158);   // TextMuted, softened
    ImU32 Accent     = IM_COL32(0x3b, 0x82, 0xf6, 255);   // Appearance accent (Blue default)
    ImU32 AccentSoft = IM_COL32(0x3b, 0x82, 0xf6, 36);    // Accent wash
    ImU32 ChipOnBg   = IM_COL32(0x3b, 0x82, 0xf6, 255);   // Active chip surface
    ImU32 ChipOnInk  = IM_COL32(255, 255, 255, 255);      // Ink on the accent
};

// Resolve one palette snapshot from the appearance theme structure.
[[nodiscard]] EditorTheme ResolveEditorTheme(const ThemePalette& Palette, ColorQuad Accent);

} // namespace Frontier
