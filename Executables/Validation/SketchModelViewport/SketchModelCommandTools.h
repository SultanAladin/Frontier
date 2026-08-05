/*==============================================================================================================================================
                                                        SKETCHMODELCOMMANDTOOLS.H
==============================================================================================================================================*/
// 🧩 The four 2D MODIFY COMMAND tools — Trim / Cut / Join / Remove — as one sticky click-to-apply driver, ported from the retired
//    DraughtingView's Modify-tool chain onto the Frontier::ParametricSketch* store. Unlike Fillet/Chamfer (a drag modal that folds a magnitude)
//    these carry no drag: each hovers a target, then a single left click APPLIES the op through the store's own verb and the tool STAYS ARMED so
//    the next target can be operated without reopening the Q console — the same persistent-tool idiom the sticky fillet uses.
//
//      • Trim   (T) — a click excises the picked outline SECTION (PartitionShapeSegment). While hovering, the exact span the click would remove is
//                     resolved WITHOUT mutating anything (ResolveTrimPreviewSpan) and stroked in the warning tint, so the cut is visible first.
//      • Cut        — a click SPLITS the picked curve at the clicked point (CutShapeAtPoint): a closed loop opens, an open run divides in two.
//      • Join   (J) — grows a pick SET one click at a time; at ≥2 it commits — any closed operand routes to the boolean Union
//                     (AppendBooleanResult), an all-open set welds end-to-end (AssembleOpenShapes). The set clears but the tool stays armed.
//      • Remove     — a click detaches the picked shape (DetachParametricSketchShape).
//
//    🔴 The store is the SINGLE source of truth: every verb above lives on it and records its own EditLog entry. This unit only drives the pick +
//       hover preview and calls the verb; the History-panel revision is recorded by the caller off the returned outcome.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELCOMMANDTOOLS_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELCOMMANDTOOLS_H

#include "imgui.h"

#include <cstdint>
#include <vector>

namespace Frontier { struct ParametricSketchShapeStore; }

namespace SketchModelViewportValidation
{

struct SketchModelViewportState;

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which command tool is armed. None is the idle state (the driver no-ops and the caller's normal pick / draw owns the canvas).
enum class SketchCommandTool
{
    None   = 0,   // [-] - idle; nothing armed
    Trim   = 1,   // [-] - excise the picked outline section
    Cut    = 2,   // [-] - split the picked curve at the click
    Join   = 3,   // [-] - weld / union the pick set (needs 2)
    Remove = 4,   // [-] - detach the picked shape
    Extend = 5    // [-] - lengthen the picked open end to the first curve ahead (Trim's mirror)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One viewport's command-tool state. Armed holds WHICH tool owns the canvas; the tool stays armed across applies (sticky) and leaves only on an
//    explicit exit — Escape, a new draw tool, or a different op. JoinPicks is Join's growing operand set (mirrored into the store's SelectionSet so
//    the render pass highlights them). The Last* block retains the most recent apply so the caller can log ONE History revision per apply and the
//    operator box can show Join's weld tolerance; ApplySerial is bumped once per successful apply (the caller's log edge, mirroring the fillet modal's
//    CommitSerial). WeldToleranceMm is Join's live parameter, edited by the operator box.
struct SketchModelCommandToolState
{
    SketchCommandTool Armed = SketchCommandTool::None;   // [-] - the tool owning the canvas (None = idle)

    std::vector<uint32_t> JoinPicks;                     // [-]  - Join's ordered operand set (grows one click at a time; cleared on commit)
    float                 WeldToleranceMm = 1.0f;        // [mm] - Join's weld tolerance (the operator box edits this)

    // 📝 The last APPLY, retained after it lands so the caller logs exactly one History revision for it (compare ApplySerial against a last-logged
    //    serial) and the operator box can name the op. ApplySerial is monotonic and survives everything except a fresh state reset.
    uint32_t    ApplySerial = 0;                         // [-] - +1 per successful apply (0 = none yet)
    const char* LastLabel   = nullptr;                   // [-] - the applied op's name ("Trim" / "Cut" / "Join" / "Remove"); nullptr = none
    uint32_t    LastShapeId = 0;                         // [-] - the shape the apply acted on (Join: the surviving / base id; 0 = n/a)
    int         LastOperands = 0;                        // [-] - how many shapes the apply consumed (Join reports its set size; else 1)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Arm Tool on this viewport: end any in-progress pick set, clear the store's draw arm (a command click must never also seat a draw point — the
//    same trap the fillet arm fixes), and mark the tool armed. Arming None disarms. The retained Last* / ApplySerial are left untouched so a box or
//    log edge in flight is not lost.
void ArmSketchCommandTool(SketchModelCommandToolState&          Tools,
                          Frontier::ParametricSketchShapeStore& Store,
                          SketchCommandTool                     Tool);

// 📝 Drop the armed tool + its pick set (Escape, a new draw tool, or a different op). Clears the store's SelectionSet echo Join grew. A no-op when
//    already idle. Does NOT undo applied geometry, and does NOT clear the retained Last* (the caller's log edge already consumed it).
void ReleaseSketchCommandTool(SketchModelCommandToolState&          Tools,
                             Frontier::ParametricSketchShapeStore& Store);

// Whether a command tool currently owns the canvas (the caller suppresses its normal selection / draw while true).
bool SketchCommandToolActive(const SketchModelCommandToolState& Tools);

// 📝 Advance the armed command tool for one cycle and render its hover cue: screen-space hover-pick the shape under the cursor (Store.Hovered, so
//    the render pass trails it), stroke Trim's doomed span / Join's growing set, and on a left click APPLY the tool's verb through the store. The
//    tool stays armed after an apply (sticky); Escape / right-click releases it. Bumps ApplySerial + fills the Last* block on each successful apply
//    so the caller can log one History revision. Returns true whenever it OWNED the press this cycle (an apply, a Join pick, or a release), so the
//    panel vetoes the normal selection / draw on that frame. A no-op returning false while idle. Call from inside the canvas child, alongside
//    AdvanceSketchFilletModal, when no summoned surface owns the press.
bool AdvanceSketchCommandTools(const SketchModelViewportState&       State,
                               Frontier::ParametricSketchShapeStore& Store,
                               SketchModelCommandToolState&          Tools,
                               ImVec2 CanvasOrigin, ImVec2 CanvasSize);

}   // namespace SketchModelViewportValidation

#endif   // FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELCOMMANDTOOLS_H
