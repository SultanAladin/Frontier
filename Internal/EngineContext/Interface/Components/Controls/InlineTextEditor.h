/*==============================================================================================================================================
                                                            INLINETEXTEDITOR.H
==============================================================================================================================================*/
// 🧩 A modular double-click-to-edit text field that lives on a FOREGROUND DRAW LIST, not in ImGui's layout — so it can edit any label drawn as a
//    custom overlay (a dock trapezoid tab, a floating-window title, a panel-box header). ImGui's own InputText cannot be positioned freely on the
//    draw list, so this is the shared caret editor for that case. One InlineTextEditState tracks the single edit in flight (which target, the live
//    buffer, the caret length); free functions begin / resolve / paint it. Stateless drawing — one definition serves every renamable overlay label.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_INLINETEXTEDITOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_INLINETEXTEDITOR_H

#include "imgui.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The single inline edit in flight. TargetIdentifier == 0 means no edit is active; a non-zero caller-chosen key names the label being edited
//    (a document id, a box id, …) so the caller knows which title to repaint as the live buffer. Buffer holds the live text; Length is its size.
struct InlineTextEditState
{
    uint32_t TargetIdentifier = 0;    // [-] - the label under edit (0 = none); a caller-chosen key
    char     Buffer[64]       = {};   // [-] - live edit text, null-terminated
    int      Length           = 0;    // [-] - characters in Buffer (excluding the terminator)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Begin editing the label keyed by Identifier, seeding the buffer with InitialText. Identifier must be non-zero (0 is the "none" sentinel).
void BeginInlineTextEdit(InlineTextEditState& State, uint32_t Identifier, const char* InitialText);

// 📝 True while the label keyed by Identifier is the one under edit — the caller paints the live buffer for it (via PaintInlineTextEdit) instead
//    of the stored title.
bool InlineTextEditActive(const InlineTextEditState& State, uint32_t Identifier);

// 📝 Pull printable characters off the ImGui input queue, honour Backspace, and settle the edit: Enter commits (the committed text is copied into
//    OutTitle, capped at OutCapacity; an empty buffer commits FallbackText), Escape cancels (OutTitle untouched). Returns true on the cycle the
//    edit ends (commit OR cancel) so the caller can clear any per-target flag; the edit is then no longer active. No-op if no edit is in flight.
bool ResolveInlineTextEdit(InlineTextEditState& State, char* OutTitle, int OutCapacity, const char* FallbackText);

// 📝 Commit the in-flight edit immediately, without waiting for Enter — copies the live buffer (or FallbackText when empty) into OutTitle and ends
//    the edit. Call this when the field loses focus (a click elsewhere), the same way a normal text field settles on blur. No-op if none in flight.
void CommitInlineTextEdit(InlineTextEditState& State, char* OutTitle, int OutCapacity, const char* FallbackText);

// 📝 Paint the label at TextPos, clipped to ClipRight: the live buffer plus a blinking caret while under edit, otherwise the static Title. One
//    call replaces a plain draw-list AddText so a label becomes edit-aware in place.
void PaintInlineTextEdit(ImDrawList* DrawList, const InlineTextEditState& State, uint32_t Identifier, const char* Title,
                         ImVec2 TextPos, float ClipRight, ImU32 Color);

}   // namespace Frontier

#endif
