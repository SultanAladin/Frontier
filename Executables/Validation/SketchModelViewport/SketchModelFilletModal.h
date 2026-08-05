/*==============================================================================================================================================
                                                        SKETCHMODELFILLETMODAL.H
==============================================================================================================================================*/
// 🧩 The Plasticity `B` tool as a modal keyboard/drag over ONE picked corner of a ParametricSketch shape: arm on a vertex handle, then DRAG
//    to set the magnitude — dragging positive (INTO the corner, along the interior bisector) grows a FILLET radius, dragging negative (OUT
//    past the corner) swaps to a CHAMFER distance, exactly one tool, the sign choosing the outcome. Typed digits set an exact amount
//    ("12" = fillet R12; a leading '-' or a negative drag = chamfer). A left-click or Enter confirms (commits via FilletShapeCorner /
//    ChamferShapeCorner), Esc or right-click cancels. The magnitude clamps to the corner's safe limit (the shorter leg). The modal never
//    mutates the shape while previewing — it holds the arm-time corner + a live magnitude, and the consumer paints a preview arc/edge from
//    ResolveSketchFilletModalPreview each frame, committing only on confirm. Retyped 1:1 from the retired DraughtFilletModal onto the ported
//    Frontier::ParametricSketch* engine; the multi-panel registry is dropped — ONE modal lives in SketchModelSummonedState.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELFILLETMODAL_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELFILLETMODAL_H

#include "imgui.h"

#include "Operations/Fillet/ParametricSketchFillet.h"   // 📝 ParametricSketchCornerCategory / ParametricSketchCornerSolution / SolveCornerEdit — the analytic core the modal previews / commits.

#include <cstdint>

namespace Frontier { struct ParametricSketchShapeStore; }

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-frame input edges the caller resolves from ImGui and hands the integrate, so the integrate stays ImGui-free (mirrors the retired
//    DraughtFilletModalInput). TypedDigits carries this frame's 0-9 / '.' / '-'; CursorX / CursorY are the live sketch-mm point.
struct SketchModelFilletModalInput
{
    bool  ConfirmPressed   = false;   // [-]  - left-click OR Enter this frame → commit the fillet / chamfer
    bool  CancelPressed    = false;   // [-]  - Esc OR right-click this frame → disarm without a commit
    bool  BackspacePressed = false;   // [-]  - Backspace this frame → pop one numeric character
    char  TypedDigits[8]   = {};      // [-]  - 0-9 / '.' / '-' captured this frame (empty = none)
    bool  CursorMoved      = false;   // [-]  - the pointer moved this frame (drives the live magnitude when not numeric)
    float CursorX          = 0.0f;    // [mm] - live sketch X under the pointer (caller-unprojected)
    float CursorY          = 0.0f;    // [mm] - live sketch Y under the pointer
};

// 📝 The one fillet modal held in SketchModelSummonedState. Armed = false while idle; an arm captures the target shape + corner index + the
//    arm-time corner world point + the safe limit. Category flips Fillet↔Chamfer by the magnitude sign each frame. NumericText is the raw typed
//    buffer; ReadoutText is the short HUD string; Magnitude is the live clamped radius / distance shown.
struct SketchModelFilletModal
{
    bool                                    Armed            = false;                                          // [-]  - a corner is captured (false = idle)
    bool                                    DragArmed        = false;                                          // [-]  - armed by a Fillet-tool PRESS (confirm on release, not a 2nd click)
    uint32_t                                TargetIdentifier = 0;                                              // [-]  - the shape whose corner is being edited (0 = none)
    int                                     CornerIndex      = -1;                                             // [-]  - the vertex index into that shape's Points
    Frontier::ParametricSketchCornerCategory Category        = Frontier::ParametricSketchCornerCategory::Fillet;// [-]  - live outcome (drag sign chooses; fillet default)

    ImVec2 CornerWorld = ImVec2(0, 0);   // [mm] - the corner vertex world position at arm time (the drag's origin reference)
    float  SafeLimit   = 0.0f;           // [mm] - the largest radius / distance the corner allows (the magnitude clamps to this)

    float  Magnitude   = 0.0f;           // [mm] - the live clamped fillet radius (Category Fillet) / chamfer distance (Category Chamfer)

    bool   NumericEntryEnabled = false;   // [-]  - digits typed → the exact amount overrides pointer motion
    char   NumericText[32]     = {};      // [-]  - typed numeric buffer ("12", "-8", "5.5"; a leading '-' forces Chamfer)
    char   ReadoutText[64]     = {};      // [-]  - short HUD string ('\0' = idle, suppress the pill)

    // 📝 The LAST committed corner edit, retained AFTER the modal disarms so the top-left operator/redo box (Blender-style) can re-adjust it — change
    //    the amount or swap Fillet↔Chamfer — by re-calling Fillet/ChamferShapeCorner on the SAME (shape, corner). That commit path overwrites the
    //    corner's stored ParametricSketchCornerFillet record in place (keyed by CornerIndex), so re-applying is idempotent: no arc stacks, and a zero
    //    magnitude erases the edit back to sharp. Cleared (LastCommitValid = false) when the tool ends — Esc, a new draw tool, or a new op.
    bool                                     LastCommitValid = false;                                            // [-]  - a commit is retained for the redo box (false = nothing to adjust)
    uint32_t                                 LastShapeId     = 0;                                                // [-]  - the shape the last commit edited
    int                                      LastCornerIndex = -1;                                               // [-]  - the corner vertex index it edited
    float                                    LastMagnitude   = 0.0f;                                             // [mm] - the committed radius / distance (the redo box's live value)
    Frontier::ParametricSketchCornerCategory LastCategory    = Frontier::ParametricSketchCornerCategory::Fillet; // [-]  - the committed outcome (the redo box's Fillet/Chamfer choice)
    float                                    LastSafeLimit   = 0.0f;                                             // [mm] - the corner's safe limit at commit (the redo box clamps its amount slider to this)

    // 📝 A monotonic counter bumped ONCE per FRESH-corner commit (a real confirm through the drag modal), NOT on a redo-box re-adjust. The panel
    //    compares it against a last-recorded serial to log exactly one History revision per corner edit, and AdvanceSketchFilletModal reads the
    //    same edge to re-arm the pick (sticky tool). A redo-box drag re-commits in place but leaves this untouched, so it neither re-logs nor re-arms.
    uint32_t                                 CommitSerial    = 0;                                                // [-]  - +1 on each fresh-corner commit (0 = none yet)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Arm Modal on vertex CornerIndex of the shape Identifier in Store: capture the corner world point + the safe limit, seed a zero magnitude, and
// mark it armed. A no-op arm (Armed stays false) when the corner is degenerate (collinear / a non-corner vertex / absent shape) — SolveCornerEdit
// resolves nothing there.
void ActivateSketchModelFilletModal(SketchModelFilletModal&              Modal,
                                    Frontier::ParametricSketchShapeStore& Store,
                                    uint32_t                             Identifier,
                                    int                                  CornerIndex);

// Run one modal cycle: fold the frame's numeric edit / cursor drag into the live magnitude + Category (drag / type sign chooses Fillet↔Chamfer),
// clamp to the safe limit, and fill Modal.ReadoutText. On confirm commit through FilletShapeCorner / ChamferShapeCorner and disarm; on cancel
// disarm with no change. Returns true whenever the modal was armed at entry (so the caller vetoes the normal pick / draw / pan on the same
// confirm / cancel). No-op returning false when idle.
bool IntegrateSketchModelFilletModal(SketchModelFilletModal&              Modal,
                                     Frontier::ParametricSketchShapeStore& Store,
                                     const SketchModelFilletModalInput&   Input);

// Resolve the live preview geometry the consumer paints while the modal is armed (the analytic arc / chamfer edge at the current magnitude),
// WITHOUT mutating the shape. Returns an unresolved solution (Resolved = false) when idle or degenerate.
Frontier::ParametricSketchCornerSolution ResolveSketchFilletModalPreview(const SketchModelFilletModal&        Modal,
                                                                         Frontier::ParametricSketchShapeStore& Store);

// 📝 Re-apply the modal's RETAINED last commit at a new magnitude / category — the operator/redo box's edit path. Overwrites the retained fields
//    (LastMagnitude / LastCategory) and re-commits through Fillet/ChamferShapeCorner on the SAME (LastShapeId, LastCornerIndex): the commit path
//    overwrites the corner's stored fillet record in place, so this is idempotent (no arc stacks). A NewMagnitude <= 0 erases the edit (back to
//    sharp) and drops LastCommitValid. A no-op returning false when nothing is retained (LastCommitValid false) or the shape has since vanished.
bool ReapplySketchFilletModalCommit(SketchModelFilletModal&              Modal,
                                    Frontier::ParametricSketchShapeStore& Store,
                                    float                                NewMagnitude,
                                    Frontier::ParametricSketchCornerCategory NewCategory);

// 📝 Drop the retained last commit (the operator/redo box then hides). Called when the tool ends — Esc on an idle canvas, a new draw tool, or a new
//    op. A no-op when nothing was retained. Does NOT touch the committed geometry (the corner edit stays); it only stops the redo box offering it.
void ClearSketchFilletModalHistory(SketchModelFilletModal& Modal);

}   // namespace SketchModelViewportValidation

#endif   // FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELFILLETMODAL_H
