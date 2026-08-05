/*==============================================================================================================================================
                                                        SKETCHMODELINSETMODAL.H
==============================================================================================================================================*/
// 🧩 The Offset tool (Plasticity / Blender `I`) as a modal DRAG over the CURRENT SELECTION of closed ParametricSketch shapes: a SketchOffset commit
//    in the Q console arms the modal on every selected closed shape, then the pointer's DISTANCE from the arm anchor grows a signed offset — a live
//    analytic preview traces the inflated / deflated outline of every captured shape (SolveLoopOffset), and nothing mutates until confirm. Dragging
//    OUTWARD (away, positive) inflates; a leading '-' / a typed negative deflates (an inward offset). A left-click / Enter COMMITS (AppendOffsetResult
//    appends one Profile per surviving outer loop, originals kept), Esc / right-click cancels.
//
//    Retyped from the retired DraughtInsetModal onto the ported Frontier::ParametricSketch* engine, the multi-panel registry dropped (ONE modal lives
//    in SketchModelSummonedState, beside the fillet modal). Unlike the fillet modal the distance is SIGNED (in↔out, no fillet/chamfer split) and there
//    is no corner pick — the whole selection offsets. The LAST commit is retained so the operator/redo box can re-adjust Distance + Corners live
//    (ReapplySketchInsetModalCommit re-runs AppendOffsetResult on the SAME sources): the originals are kept, so re-applying appends a FRESH offset each
//    time — the box first REMOVES the prior offset Profiles it appended (tracked ids) then re-appends, so a slide reads as one moving offset, not a stack.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELINSETMODAL_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELINSETMODAL_H

#include "imgui.h"

#include "Operations/Transform/ParametricSketchTransform.h"   // 📝 SolveLoopOffset / AppendOffsetResult / SketchOffsetCornerStyle — the Clipper2 offset core the modal previews / commits.

#include <cstdint>
#include <vector>

namespace Frontier { struct ParametricSketchShapeStore; }

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-frame input edges the caller resolves from ImGui and hands the integrate, so the integrate stays ImGui-free (mirrors
//    SketchModelFilletModalInput). TypedDigits carries this frame's 0-9 / '.' / '-'; CursorX / CursorY are the live sketch-mm point driving the drag.
struct SketchModelInsetModalInput
{
    bool  ConfirmPressed   = false;   // [-]  - left-click OR Enter this frame → commit the offset Profiles
    bool  CancelPressed    = false;   // [-]  - Esc OR right-click this frame → disarm without a commit
    bool  BackspacePressed = false;   // [-]  - Backspace this frame → pop one numeric character
    char  TypedDigits[8]   = {};      // [-]  - 0-9 / '.' / '-' captured this frame (empty = none)
    bool  CursorMoved      = false;   // [-]  - the pointer moved this frame (drives the live distance when not numeric)
    float CursorX          = 0.0f;    // [mm] - live sketch X under the pointer (caller-unprojected)
    float CursorY          = 0.0f;    // [mm] - live sketch Y under the pointer
};

// 📝 One live preview outline the consumer strokes while the modal is armed. Closed = true is an inflated / deflated Profile loop (draw with the
//    closing edge); Closed = false is an open curve's single parallel offset (draw open — no closing edge, else a spurious chord appears).
struct SketchInsetPreviewLoop
{
    std::vector<ImVec2> Points;            // [mm] - the world-mm offset outline
    bool                Closed = true;     // [-]  - stroke closed (a Profile loop) vs open (a parallel curve)
};

// 📝 The one offset modal held in SketchModelSummonedState. Armed = false while idle; an arm captures every closed target shape + the arm-time cursor
//    anchor (the drag's distance origin). Magnitude is the live SIGNED offset distance (positive = outward / inflate, negative = inward / deflate).
//    CornerStyle is the join type both the preview and the commit build corners with (the operator box's Corners dropdown edits it).
struct SketchModelInsetModal
{
    bool                  Armed = false;                                   // [-]  - a selection is captured (false = idle)
    std::vector<uint32_t> Targets;                                         // [-]  - the closed shapes captured at arm time (the offset operands)

    ImVec2 AnchorWorld = ImVec2(0, 0);                                     // [mm] - the cursor's sketch position at arm time (the drag's distance origin)
    float  Magnitude   = 0.0f;                                             // [mm] - the live signed offset distance (outline moves by this)
    Frontier::SketchOffsetCornerStyle CornerStyle = Frontier::SketchOffsetCornerStyle::Miter;   // [-] - how corners are built (Miter default = sharp corners; a rectangle offset outward stays square. Round/Bevel via the redo box)

    bool   NumericEntryEnabled = false;                                    // [-]  - digits typed → the exact distance overrides pointer motion
    char   NumericText[32]     = {};                                       // [-]  - typed numeric buffer ("5", "-2.5")
    char   ReadoutText[64]     = {};                                       // [-]  - short HUD string ('\0' = idle, suppress the pill)

    // 📝 The LAST committed offset, retained AFTER the modal disarms so the top-left operator/redo box can re-adjust it — change the Distance or the
    //    Corners style — by re-running AppendOffsetResult on the SAME captured sources. Because offset KEEPS the originals (it is additive, not a
    //    consuming boolean), a naive re-apply would STACK a new set of Profiles each slide; so LastAppended holds the ids the last commit produced and
    //    the re-apply detaches them FIRST, then appends fresh — a slide reads as one moving offset. Cleared when the tool ends (Esc / new draw tool / new op).
    bool                  LastCommitValid = false;                         // [-]  - a commit is retained for the redo box (false = nothing to adjust)
    std::vector<uint32_t> LastSources;                                     // [-]  - the source shapes the last commit offset (re-apply re-reads these)
    std::vector<uint32_t> LastAppended;                                    // [-]  - the Profile ids the last commit appended (the re-apply detaches these first)
    float                 LastMagnitude = 0.0f;                            // [mm] - the committed distance (the redo box's live value)
    Frontier::SketchOffsetCornerStyle LastCornerStyle = Frontier::SketchOffsetCornerStyle::Round;   // [-] - the committed corner style (the redo box's dropdown value)

    // 📝 A monotonic counter bumped ONCE per FRESH commit (a real confirm through the drag), NOT on a redo-box re-adjust — the offset twin of the fillet
    //    modal's CommitSerial. The panel compares it against a last-recorded serial to log exactly one History revision per offset, and the driver reads
    //    the same edge to re-arm (sticky). A redo-box slide re-commits in place but leaves this untouched, so it neither re-logs nor re-arms.
    uint32_t              CommitSerial = 0;                                // [-]  - +1 on each fresh commit (0 = none yet)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Arm Modal over the store's current selection: capture every CLOSED shape (from SelectionSet, or the lone Selected) as an offset target, seed the
// arm-time cursor anchor + a zero distance, and mark it armed. A no-op arm (Armed stays false) when the selection holds no closed shape (an open Line /
// curve has no area to offset). Mirrors ActivateSketchModelFilletModal.
void ActivateSketchModelInsetModal(SketchModelInsetModal&                Modal,
                                   Frontier::ParametricSketchShapeStore& Store,
                                   float                                 AnchorX,
                                   float                                 AnchorY);

// Arm Modal on ONE picked shape — the two-phase pick's target (the edge / face the cursor was over when clicked), not the whole selection. Seeds the
// pick click's ground point as the drag anchor. A no-op arm (Armed stays false) if the id no longer resolves. Used by AdvanceSketchInsetModal's PHASE 1.
void ActivateSketchModelInsetModalOnShape(SketchModelInsetModal&                Modal,
                                          Frontier::ParametricSketchShapeStore& Store,
                                          uint32_t                              ShapeIdentifier,
                                          float                                 AnchorX,
                                          float                                 AnchorY);

// Run one modal cycle: fold the frame's numeric edit / cursor drag into the live signed distance, fill Modal.ReadoutText, and on confirm append the
// offset Profiles through AppendOffsetResult (retaining the sources + appended ids for the redo box) then disarm; on cancel disarm with no change.
// Returns true whenever the modal was armed at entry (so the caller vetoes the normal pick / draw / pan on the same confirm / cancel). No-op returning
// false when idle. Mirrors IntegrateSketchModelFilletModal.
bool IntegrateSketchModelInsetModal(SketchModelInsetModal&                Modal,
                                    Frontier::ParametricSketchShapeStore& Store,
                                    const SketchModelInsetModalInput&     Input);

// Resolve the live preview outlines the consumer paints while the modal is armed — the offset outline of every captured target at the current
// distance + corner style, WITHOUT mutating the store. A closed target yields one or more CLOSED Profile loops (outer / split island); an open
// curve yields ONE OPEN parallel curve (Closed = false, so the consumer omits the closing edge). Empty when idle / degenerate or the distance ate
// every region. Mirrors ResolveSketchFilletModalPreview.
std::vector<SketchInsetPreviewLoop> ResolveSketchInsetModalPreview(const SketchModelInsetModal&          Modal,
                                                                Frontier::ParametricSketchShapeStore& Store);

// 📝 Re-apply the modal's RETAINED last commit at a new distance / corner style — the operator/redo box's edit path. DETACHES the prior appended
//    Profiles first (LastAppended), re-selects the retained sources, re-runs AppendOffsetResult, and records the fresh appended ids — so a slide reads
//    as one moving offset, never a stack. A NewDistance == 0 erases the edit (detaches the prior Profiles, appends nothing) and drops LastCommitValid.
//    A no-op returning false when nothing is retained (LastCommitValid false) or every source has since vanished.
bool ReapplySketchInsetModalCommit(SketchModelInsetModal&                Modal,
                                   Frontier::ParametricSketchShapeStore& Store,
                                   float                                 NewDistance,
                                   Frontier::SketchOffsetCornerStyle     NewCornerStyle);

// 📝 Drop the retained last commit (the operator/redo box then hides). Called when the tool ends — Esc on an idle canvas, a new draw tool, or a new
//    op. A no-op when nothing was retained. Does NOT touch the committed geometry (the offset Profiles stay); it only stops the redo box offering it.
void ClearSketchInsetModalHistory(SketchModelInsetModal& Modal);

}   // namespace SketchModelViewportValidation

#endif   // FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELINSETMODAL_H
