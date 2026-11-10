/*==========================================================================================================================================
                                                              EDITORTHEME.CPP
============================================================================================================================================*/
// 🧩 Appearance-driven palette for the editor's own panels: ThemeStructure colours resolved to ImGui inks.

#include "EditorTheme.h"

#include "Engine/Interface/StackRailControls.h"
#include "Engine/Interface/ThemeStructure.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

ImU32 Ink(ColorQuad Colour) noexcept
{
    const auto Channel = [](float Value) {
        return static_cast<int>(std::clamp(Value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return IM_COL32(Channel(Colour.Red), Channel(Colour.Green), Channel(Colour.Blue), Channel(Colour.Alpha));
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                      RESOLUTION
//----------------------------------------------------------------------------------------------------------------------

EditorTheme ResolveEditorTheme(const ThemePalette& Palette, ColorQuad Accent)
{
    EditorTheme Theme;
    Theme.PanelBg    = Ink(Palette.PanelBackground);
    Theme.PanelEdge  = Ink(Palette.PanelBorder);
    Theme.Surface    = Ink(Palette.CardBackground);
    Theme.Surface2   = Ink(Palette.InputBackground);
    Theme.Surface3   = Ink(Palette.ActiveBackground);
    Theme.Line       = Ink(Palette.DividerColor);
    Theme.LineSoft   = KitInkWithAlpha(Theme.Line, 0.6f);
    Theme.RowHover   = KitInkWithAlpha(Theme.Surface3, 0.5f);
    Theme.Text       = Ink(Palette.TextMain);
    Theme.Muted      = Ink(Palette.TextMuted);
    Theme.Faint      = KitInkWithAlpha(Theme.Muted, 0.62f);
    Theme.Accent     = Ink(Accent);
    Theme.AccentSoft = KitInkWithAlpha(Theme.Accent, 0.14f);
    Theme.ChipOnBg   = Theme.Accent;
    Theme.ChipOnInk  = IM_COL32_WHITE;
    return Theme;
}

} // namespace Frontier
