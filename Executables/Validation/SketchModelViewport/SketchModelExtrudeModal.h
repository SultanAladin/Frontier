/*==============================================================================================================================================
                                                        SKETCHMODELEXTRUDEMODAL.H
==============================================================================================================================================*/
// 🧩 The Extrude tool (Blender `E`) as a modal DRAG on the CURRENT SELECTION: a SweepPrism commit in the Q console captures every selected shape at
//    ZERO added height, then the pointer's travel ALONG THE PROJECTED SWEEP AXIS grows the height live — a left-click / Enter confirms it, Esc /
//    right-click restores every captured shape to exactly the state it was armed in. Typing a number sets the height exactly.
//
//    🔴 THE PREVIEW *IS* THE GEOMETRY. Unlike the fillet / offset modals — which stroke an analytic outline and mutate nothing until confirm — this
//       modal writes the live height straight onto each target's ExtrudeDepth every frame, because the store's tessellator + the GPU matcap pass
//       already turn that field into a solid. So the "preview" is the real extruded body, updating as the pointer moves, which is what makes the
//       gesture read like Blender's. The cost is that a cancel must UNDO: each target's arm-time Elevation / ExtrudeDepth / MatcapFillEnabled are
//       snapshotted so a restore is exact. (The GPU re-upload this provokes per frame is expected — RestageResident drains the device first for
//       precisely this "a body being dragged re-tessellates every frame" case.)
//
//    🔴 THE DRAG IS SCREEN-SPACE ALONG THE SWEEP AXIS, not a ground cast. Every other tool here reads the cursor through
//       CastCursorToGroundMillimetres, but that solves the plane Z = 0 and so can never express a HEIGHT. The height instead comes from projecting
//       the profile's own sweep axis (world +Z at the selection centroid) to pixels and measuring the pointer's travel along it — exactly how
//       Blender maps mouse motion onto a constrained extrude axis. A view looking straight down the axis projects it to nothing; the driver reports
//       that as unresolved and the modal holds its last height rather than exploding.
//
//    Height is a signed DELTA on top of each target's arm-time depth, so re-extruding an already-solid profile ADJUSTS its height from where it
//    stands instead of collapsing it to zero first. A confirm at ~zero height restores the arm-time state (nothing was extruded, so nothing is kept).

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELEXTRUDEMODAL_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELEXTRUDEMODAL_H

#include "imgui.h"

#include <cstdint>
#include <vector>

namespace Frontier { struct ParametricSketchShapeStore; }

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One captured sweep operand + the exact state it was armed in. The three snapshot fields are what let a cancel restore the shape byte-for-byte
//    instead of guessing at "flat" — an already-extruded profile that is re-adjusted and then cancelled must return to ITS OWN height, not to zero.
struct SketchModelExtrudeTarget
{
    uint32_t Identifier      = 0;       // [-]  - the shape being swept
    float    BaseElevation   = 0.0f;    // [mm] - its Elevation at arm time (the Symmetric straddle is recomputed off this every frame, never accumulated in place)
    float    PriorDepth      = 0.0f;    // [mm] - its ExtrudeDepth at arm time (the drag adds to this; a cancel restores it)
    bool     PriorMatcapFill = false;   // [-]  - its MatcapFillEnabled at arm time (a cancel restores it, so a never-extruded shape drops back to the flat CPU wash)
};

// 📝 The per-frame input edges the caller resolves and hands the integrate, so the integrate stays ImGui- and projection-free (mirrors
//    SketchModelInsetModalInput). SweepMillimetres is the pointer's travel along the PROJECTED sweep axis, already converted to mm by the driver —
//    SweepResolved is false when that axis is degenerate on screen (a view down the axis), and the integrate then holds the last height.
struct SketchModelExtrudeModalInput
{
    bool  ConfirmPressed   = false;   // [-]  - left-click OR Enter this frame → keep the swept height
    bool  CancelPressed    = false;   // [-]  - Esc OR right-click this frame → restore every target's arm-time state
    bool  BackspacePressed = false;   // [-]  - Backspace this frame → pop one numeric character
    char  TypedDigits[8]   = {};      // [-]  - 0-9 / '.' / '-' captured this frame (empty = none)
    bool  SweepResolved    = false;   // [-]  - the projected sweep axis was usable this frame (false = hold the last height)
    float SweepMillimetres = 0.0f;    // [mm] - the pointer's signed travel along the sweep axis since the arm anchor
};

// 📝 The one extrude modal held in SketchModelSummonedState. Armed = false while idle; an arm snapshots every selected shape and seeds a ZERO delta,
//    so the solid does not pop the instant the tool is chosen — the height only grows as the pointer travels. SymmetricEnabled straddles the sketch
//    plane (read once from the console's Direction row at arm time).
struct SketchModelExtrudeModal
{
    bool                                  Armed = false;   // [-]  - a selection is captured and the drag is live (false = idle)
    std::vector<SketchModelExtrudeTarget> Targets;         // [-]  - the shapes captured at arm time, each with its restore snapshot

    ImVec2 AnchorPixel      = ImVec2(0, 0);   // [px] - the cursor at arm time; the drag measures its travel from here, so the height starts at exactly zero

    // 🔴 A CONSOLE arm cannot seed the anchor: the commit is dispatched from the console surface, which the panel records AFTER the driver has already
    //    run for the cycle, so the arming code has no live pointer position to hand over. It raises this instead and the driver latches AnchorPixel on
    //    the first frame it sees the modal armed — the frame the drag genuinely becomes live, which is the true zero the travel should measure from.
    //    Harmless that it is not near the profile: extrude is a RELATIVE drag along the projected axis, so only the origin of the travel matters.
    //    A PICK arm has a real canvas click to use and lowers this immediately.
    bool   AnchorPending    = false;           // [-]  - AnchorPixel is a placeholder; the driver latches the live pointer on the first armed frame
    ImVec2 AnchorCentroid   = ImVec2(0, 0);   // [mm] - the targets' XY centroid; the driver projects the sweep axis THERE, so the scale matches where the solid is
    float  AnchorElevation  = 0.0f;           // [mm] - the targets' mean arm-time Elevation (the sweep axis is probed from this height)

    float  Depth            = 0.0f;   // [mm] - the live signed height DELTA the drag has travelled (added to each target's PriorDepth)
    bool   SymmetricEnabled = false;  // [-]  - straddle the sketch plane (Elevation drops by half the height) instead of sitting on it

    bool   NumericEntryEnabled = false;   // [-]  - digits typed → NumericText is the ABSOLUTE height and pointer motion is ignored
    char   NumericText[32]     = {};      // [-]  - typed numeric buffer ("30", "-12.5")
    char   ReadoutText[64]     = {};      // [-]  - short HUD string ('\0' = idle, suppress the pill)

    // 📝 The last CONFIRMED sweep, retained after the modal disarms so the panel can log exactly one History revision for it. There is deliberately
    //    no redo box here (unlike fillet / offset): the height is already re-adjustable by simply re-running the tool on the same selection, which
    //    picks up PriorDepth and continues from where it stands — so a retained re-apply path would be a second way to do the same thing.
    bool     LastCommitValid = false;   // [-]  - a confirm is retained for the History hook (false = nothing to log)
    float    LastDepth       = 0.0f;    // [mm] - the confirmed height of the first target (the revision's subtitle)
    int      LastTargetCount = 0;       // [-]  - how many shapes the confirm swept
    uint32_t CommitSerial    = 0;       // [-]  - +1 on each confirm (0 = none yet); the panel logs one revision per advance

    // 📝 Set for one frame when an arm found NO operand (an empty sketch / every shape hidden), so the driver can drop straight back to idle without
    //    the caller having to re-test the selection. The notice itself is raised on the store, exactly as the offset / transform paths do.
    bool VacantArmReported = false;   // [-]  - the last arm attempt had nothing to sweep
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Arm Modal over the store's CURRENT SELECTION: snapshot every resolvable selected shape (SelectionSet, or the lone Selected) as a sweep operand,
// seed a ZERO height delta, and mark it armed. A no-op arm (Armed stays false, VacantArmReported true) when nothing resolves.
// 🔴 Raises AnchorPending: a console arm has no live pointer position to hand over (see the field), so the driver latches the anchor on its first armed
//    frame. Nothing is extruded before then — the height is a hard zero until the pointer travels away from that latched origin.
void ActivateSketchModelExtrudeModal(SketchModelExtrudeModal&              Modal,
                                     Frontier::ParametricSketchShapeStore& Store,
                                     bool                                  SymmetricEnabled);

// Arm Modal on ONE picked shape — the fallback pick's target, used when the tool is chosen with NOTHING selected (the next canvas click over a
// profile becomes the operand). Seeds that click's pixel as the drag anchor DIRECTLY (AnchorPending stays low: the click was already on the canvas,
// on the profile, so it is exactly the right origin to measure from). A no-op arm if the id no longer resolves.
void ActivateSketchModelExtrudeModalOnShape(SketchModelExtrudeModal&              Modal,
                                            Frontier::ParametricSketchShapeStore& Store,
                                            uint32_t                              ShapeIdentifier,
                                            ImVec2                                AnchorPixel,
                                            bool                                  SymmetricEnabled);

// Run one modal cycle: fold the frame's numeric edit / sweep travel into the live height, WRITE it onto every target (the live solid), fill
// Modal.ReadoutText, and on confirm retain the sweep + disarm — or on cancel restore every target's arm-time state + disarm. A confirm at ~zero
// height restores instead of keeping (nothing was swept). Returns true whenever the modal was armed at entry, so the caller vetoes the normal
// pick / draw / pan on the same confirm / cancel. No-op returning false when idle.
bool IntegrateSketchModelExtrudeModal(SketchModelExtrudeModal&              Modal,
                                      Frontier::ParametricSketchShapeStore& Store,
                                      const SketchModelExtrudeModalInput&   Input);

// 📝 Restore every captured target to its arm-time Elevation / ExtrudeDepth / MatcapFillEnabled and disarm, WITHOUT consuming an input edge — the
//    escape hatch for a caller that must abandon the gesture from outside (a new tool armed over it, the Select release op). A no-op while idle.
//    Distinct from a cancel only in that it is caller-driven rather than input-driven; the geometry outcome is identical.
void AbandonSketchModelExtrudeModal(SketchModelExtrudeModal&              Modal,
                                    Frontier::ParametricSketchShapeStore& Store);

}   // namespace SketchModelViewportValidation

#endif   // FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELEXTRUDEMODAL_H
