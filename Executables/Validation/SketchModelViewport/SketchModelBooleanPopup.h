/*==============================================================================================================================================
                                                        SKETCHMODELBOOLEANPOPUP.H
==============================================================================================================================================*/
// 🧩 The 2D BOOLEAN popup — the surface that appears the moment the canvas selection reaches TWO closed shapes, offering Union / Subtract /
//    Intersect over them. Unlike Offset / Fillet / Extend (armed from the Q console, then a canvas drag) a boolean needs no pointer gesture at
//    all: the operands ARE the selection, so the only thing missing is which operation to run — hence a small settings card rather than a modal
//    drag. The card floats beside the selection, previews its outcome live, and commits through the ported Frontier::AppendBooleanResult.
//
//      • Operation      — Union / Subtract / Intersect (Frontier::BooleanCategory).
//      • Base           — WHICH operand is the subtrahend base. Subtract is directed (base minus every other operand), so with the base fixed at
//                         selection order you could only ever carve in the order you happened to click. The picker reorders the operand list.
//      • Keep operands  — off (default) hides the operands after the commit, recoverable via the outliner; on leaves them displayed so the fresh
//                         region overlays its sources.
//      • Live preview   — the surviving region is re-solved read-only on every settings change (ResolveBooleanPreviewLoops) and stroked on the
//                         canvas, so the outcome is visible BEFORE Apply. Outer loops and punched holes stroke distinctly.
//
//    🔴 The popup is DISMISSABLE and must not nag: Esc / right-click / Cancel closes it, and it will NOT re-open for the same selection (the
//       dismissed operand set is remembered). Changing the selection re-arms it. This is what keeps a plain shift-click multi-select usable.
//
//    🔴 The store is the single source of truth: this unit owns no geometry. It holds the operand ORDER + the chosen settings, and the commit is
//       Frontier::AppendBooleanResult reading Store.SelectionSet — which the commit re-seats from Operands so the base leads.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELBOOLEANPOPUP_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELBOOLEANPOPUP_H

#include "imgui.h"

#include "Operations/Boolean/ParametricSketchBoolean.h"   // 📝 BooleanCategory / AppendBooleanResult / ResolveBooleanPreviewLoops — the Clipper2 region core.

#include <cstdint>
#include <vector>

namespace Frontier { struct ParametricSketchShapeStore; struct ThemeConfiguration; }

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One preview loop the consumer strokes while the popup is open. Hole = true is a punched interior (a CW loop from the solve) — stroked in a
//    dimmer tint so a void reads differently from the region's outer boundary.
struct SketchBooleanPreviewLoop
{
    std::vector<ImVec2> Points;          // [mm] - the world-mm loop
    bool                Hole = false;    // [-]  - a punched interior (CW) rather than an outer boundary (CCW)
};

// 📝 The one boolean popup held in SketchModelSummonedState. Open = false while idle. Operands is the ORDERED operand list (front = the Subtract
//    base) captured when the popup armed; it is deliberately a copy of the selection rather than a live read, so the base reorder survives and a
//    transient selection wobble cannot swap operands underneath the user mid-decision.
struct SketchModelBooleanPopup
{
    bool                  Open = false;                                   // [-] - the card is showing (false = idle)
    std::vector<uint32_t> Operands;                                       // [-] - the ordered operands (front = Subtract base)

    Frontier::BooleanCategory Operation = Frontier::BooleanCategory::Union;   // [-] - the operation the Apply commits
    int                       BaseIndex = 0;                              // [-] - which Operands entry is the base (rotated to front on commit)
    bool                      KeepOperandsEnabled = false;                // [-] - leave the operands displayed after the commit

    ImVec2 AnchorPixel = ImVec2(0, 0);                                    // [px] - where the card pins (near the selection / cursor at arm time)

    // 📝 The operand set the user DISMISSED, so the popup does not spring straight back up for the very same selection. Compared as a set (order
    //    -insensitive): re-picking the same two shapes in the other order is still the same dismissal. Cleared when the selection truly changes.
    std::vector<uint32_t> DismissedOperands;                              // [-] - the set an Esc / Cancel rejected (empty = nothing dismissed)

    // 📝 A monotonic counter bumped ONCE per committed boolean — the panel compares it against a last-logged serial to record exactly one History
    //    revision per apply (the boolean twin of the inset modal's CommitSerial).
    uint32_t    CommitSerial = 0;                                         // [-] - +1 per successful commit (0 = none yet)
    const char* LastLabel    = nullptr;                                   // [-] - the committed op's name ("Union" / "Subtract" / "Intersect")
    int         LastOperands = 0;                                         // [-] - how many operands the commit consumed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Reconcile the popup against the store's live selection — call ONCE per frame before recording it. Opens the card when the selection holds ≥2
//    shapes of which every one is CLOSED (an open polyline has no region, so a boolean over one would only ever raise a notice — staying silent
//    there is what stops the popup interrupting ordinary multi-select), and closes it when the selection falls below two. A selection that differs
//    from the dismissed set re-arms a dismissed popup. AnchorPixel is seeded from CursorPixel on the arming frame only, so the card does not chase
//    the pointer. Never mutates geometry.
void ReconcileSketchModelBooleanPopup(SketchModelBooleanPopup&              Popup,
                                      Frontier::ParametricSketchShapeStore& Store,
                                      ImVec2                                CursorPixel);

// 📝 Record the popup's settings card for one cycle and act on its buttons: Apply commits through Frontier::AppendBooleanResult (re-seating
//    Store.SelectionSet so the chosen base leads), bumps CommitSerial + fills the Last* block, and closes; Cancel / the close box dismisses
//    (remembering the operand set so it does not re-open). A no-op when idle. Returns true on the cycle it COMMITTED, so the caller logs one
//    History revision off the returned edge. Call from inside the canvas child, alongside the other summoned surfaces.
bool ConstructSketchModelBooleanPopup(const Frontier::ThemeConfiguration&   Theme,
                                      SketchModelBooleanPopup&              Popup,
                                      Frontier::ParametricSketchShapeStore& Store);

// 📝 Resolve the live preview loops the consumer paints while the popup is open — the surviving region at the current Operation + base order,
//    WITHOUT mutating the store. Outer loops come back Hole = false, punched interiors Hole = true. Empty when idle, when an operand has since
//    vanished, or when the operation leaves no region (a disjoint Intersect), which is itself the useful signal that Apply would do nothing.
std::vector<SketchBooleanPreviewLoop> ResolveSketchModelBooleanPreview(const SketchModelBooleanPopup&        Popup,
                                                                      Frontier::ParametricSketchShapeStore& Store);

// 📝 Dismiss the popup from OUTSIDE its card — the canvas's Esc / right-click edge, so a boolean card obeys the same release gesture every other
//    sketch tool does. Remembers the operand set (no re-open for the same selection). A no-op when already idle. Returns true if it closed
//    something, so the caller can veto its own Esc handling on that frame.
bool DismissSketchModelBooleanPopup(SketchModelBooleanPopup& Popup);

}   // namespace SketchModelViewportValidation

#endif   // FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELBOOLEANPOPUP_H
