/*==============================================================================================================================================
                                                                MENUPILL.H
==============================================================================================================================================*/
// 🧩 The two-tone "[ label | ▾ ]" chrome pill and the grouped floating menu it opens (CadModellingInterface.html `.vp-gearpill` + `.gp-menu`).
//    The left half carries the header tone with an optional icon, status dot, or dim prefix; the right caret half is pitch black. The menu is a
//    titled card of grouped rows, each row optionally carrying a checkbox, a keystroke chip, and a leading separator. Stateless — one definition
//    serves every chrome pill (camera views, viewport settings, scene units); the caller owns the selection and reads back the activated row.
//    The menu anchors below or above and aligns to either pill edge, so a pill in a bottom band opens upward without clipping.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_MENUPILL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_MENUPILL_H

#include "imgui.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                           ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which corner the menu card grows from. The vertical term picks the side of the pill it opens on; the horizontal term picks
//    the pill edge it aligns to. A pill in the top band opens Below*; one in the bottom band opens Above* so it overhangs upward.
enum class MenuPillAnchor
{
    BelowLeadingEdge,     // [-] - Under the pill, menu left edge on the pill left edge  (`.gp-menu-left`)
    BelowTrailingEdge,    // [-] - Under the pill, menu right edge on the pill right edge (`.gp-menu`, the default)
    AboveLeadingEdge,     // [-] - Over the pill, left-aligned
    AboveTrailingEdge     // [-] - Over the pill, right-aligned                          (`.gp-menu-up`)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One row of the menu card (`.gp-item`). GroupCaption emits a small uppercase caption ABOVE this row when non-null
//    (`.gp-group`), so a caller declares grouping inline instead of interleaving two arrays. CheckmarkEnabled false draws the
//    flat indent the mockup uses for action rows (`.gp-chk.gp-noindent`); Marked fills the box with the accent (`.is-on`).
struct MenuPillItemDescriptor
{
    const char* Label;                        // [-] - Row text
    const char* KeyHint;                      // [-] - Keystroke chip pinned right (`.gp-key`); null omits it
    const char* GroupCaption;                 // [-] - Uppercase caption emitted above this row; null continues the group
    bool        CheckmarkEnabled;             // [-] - Draw the checkbox outline (false = flat indent, an action row)
    bool        Marked;                       // [-] - Checkbox filled with the accent (the mockup's `.is-on`)
    bool        SeparatorPreceding;           // [-] - Emit a hairline rule above this row (`.gp-sep`)
};


// 📝 The pill plus the menu it opens. MainText is the primary label; PrefixText prepends a dimmed run ("World ·" in the units
//    pill); IconTexture draws a glyph ahead of the text when non-zero; StatusDotEnabled draws the small green vitality dot
//    (`.vdot`). CompactEnabled selects the short footer variant (`.vp-pill-sm`, 22 px instead of 30 px).
struct MenuPillDescriptor
{
    const char*                  Identifier;         // [-]  - Unique id (scopes the ImGui popup)
    const char*                  MainText;           // [-]  - Primary pill label
    const char*                  PrefixText;         // [-]  - Dimmed run before MainText; null omits it
    ImTextureID                  IconTexture;        // [-]  - Glyph ahead of the text; 0 omits it
    bool                         StatusDotEnabled;   // [-]  - Draw the green vitality dot
    bool                         CompactEnabled;     // [-]  - Short footer variant (22 px)
    MenuPillAnchor               Anchor;             // [-]  - Which corner the menu grows from
    const char*                  MenuTitle;          // [-]  - Menu card header (`.gp-head`); null omits the header
    const MenuPillItemDescriptor* Items;             // [-]  - Menu rows (borrowed, not owned)
    int                          ItemCount;          // [-]  - Number of rows
};


// 📝 What one pill reports back. ActivatedIndex is the row clicked this cycle, or -1. MenuOpen lets a caller suppress viewport
//    navigation while a menu overhangs the canvas.
struct MenuPillResult
{
    int  ActivatedIndex;   // [idx] - Row clicked this cycle; -1 when none
    bool MenuOpen;         // [-]   - The menu is currently open
    bool Hovered;          // [-]   - Pointer is over the pill itself
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one chrome pill at the current cursor position and, while open, its floating menu. Advances the ImGui cursor past
//    the pill only (the menu floats). Clicking either half toggles the menu, matching the mockup where both the label and the
//    caret open it. No-op-safe against a null Items array.
MenuPillResult ConstructMenuPill(const ThemeConfiguration& Theme, const MenuPillDescriptor& Descriptor);

// 📝 The pill's outer height in pixels for the given variant — so a band can centre a pill vertically before recording it.
float ResolveMenuPillHeight(bool CompactEnabled);

}   // namespace Frontier

#endif
