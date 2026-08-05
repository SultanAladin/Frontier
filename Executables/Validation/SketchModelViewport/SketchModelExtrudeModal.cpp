/*==============================================================================================================================================
                                                        SKETCHMODELEXTRUDEMODAL.CPP
==============================================================================================================================================*/
// 🧩 The Extrude tool as a modal DRAG: arm on the selection at zero added height, then fold each frame's sweep travel / typed number into a signed
//    height and WRITE it onto every target so the store's tessellator + the GPU matcap pass show the real solid growing under the pointer. A confirm
//    keeps it; a cancel restores each target's arm-time snapshot exactly. See the header for why the preview is the geometry (and why that obliges
//    the snapshot) and why the drag is measured along the projected sweep axis rather than through a ground cast.

#include "SketchModelExtrudeModal.h"

#include "ParametricSketchShapeStore.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float DepthEps = 1e-3f;   // [mm] - below this the height counts as zero (a confirm restores rather than keeps)

    // 📝 Parse the typed numeric buffer into a double. Partial buffers not yet a number ("", "-", ".", "-.") resolve to 0.0 so the live height stays
    //    put until a digit follows. Returns false on a partial buffer. (The same reader the fillet / offset modals use.)
    bool EvaluateNumericAmount(const char* Text, double& Parsed)
    {
        Parsed = 0.0;
        if (Text == nullptr || Text[0] == '\0')
            return false;

        char*        Terminator = nullptr;
        const double Candidate  = std::strtod(Text, &Terminator);
        if (Terminator == Text || Terminator == nullptr || *Terminator != '\0')
            return false;   // partial like "-" / "." / "-." — no full number yet

        Parsed = Candidate;
        return true;
    }

    // 📝 Append one typed character to the numeric buffer: a single leading '-' (sweep downward), a single '.', digits anywhere. Drops anything else
    //    or an overflow. Enables numeric entry on the first accepted character, which is what makes the typed value override pointer motion.
    void AbsorbNumericCharacter(SketchModelExtrudeModal& Modal, char Character)
    {
        const size_t Length = std::strlen(Modal.NumericText);
        if (Length + 1 >= sizeof(Modal.NumericText))
            return;

        const bool DigitCharacter = (Character >= '0' && Character <= '9');
        if (Character == '-')
        {
            if (Length != 0) return;   // sign only leads
        }
        else if (Character == '.')
        {
            if (std::strchr(Modal.NumericText, '.') != nullptr) return;   // single decimal point
        }
        else if (!DigitCharacter)
        {
            return;
        }

        Modal.NumericText[Length]     = Character;
        Modal.NumericText[Length + 1] = '\0';
        Modal.NumericEntryEnabled     = true;
    }

    // 📝 Pop one character off the numeric buffer (Backspace). Clearing it disables numeric entry so pointer motion drives the height again.
    void ReleaseNumericCharacter(SketchModelExtrudeModal& Modal)
    {
        const size_t Length = std::strlen(Modal.NumericText);
        if (Length == 0)
            return;

        Modal.NumericText[Length - 1] = '\0';
        if (Modal.NumericText[0] == '\0')
            Modal.NumericEntryEnabled = false;
    }

    // 📝 Fill Modal.ReadoutText with the short HUD string. During numeric entry the raw typed buffer is echoed (so a half-typed "1" reads as typed);
    //    otherwise the live height prints in mm. The count is carried so a multi-shape sweep reads as one gesture over N profiles.
    void ConstructReadout(SketchModelExtrudeModal& Modal)
    {
        const int Count = (int)Modal.Targets.size();
        if (Modal.NumericEntryEnabled)
            std::snprintf(Modal.ReadoutText, sizeof(Modal.ReadoutText), "Extrude: %s mm (%d)%s",
                          Modal.NumericText, Count, Modal.SymmetricEnabled ? " sym" : "");
        else
            std::snprintf(Modal.ReadoutText, sizeof(Modal.ReadoutText), "Extrude: %.2f mm (%d)%s",
                          Modal.Depth, Count, Modal.SymmetricEnabled ? " sym" : "");
    }

    // 🔴 WRITE the live height onto every target — the step that makes the drag show a real solid rather than an outline. Two rules make this safe to
    //    run every frame:
    //      • the total depth is PriorDepth + the drag delta, so re-extruding an already-solid profile continues from its own height;
    //      • Elevation is recomputed from the arm-time BaseElevation each frame (never adjusted in place), so the Symmetric straddle cannot
    //        accumulate a drift of half a height per frame — the classic bug of folding a centring offset into a value you then re-read.
    //    A ~zero total depth drops the matcap promotion back to the arm-time reading, so a shape dragged back to nothing looks exactly like the flat
    //    profile it was instead of lingering as a zero-depth GPU body.
    //
    // 🔴 Symmetric centres the ADDED height (the drag delta), NOT the total. Centring the total would need to know whether the target's PRIOR depth
    //    had itself been straddled — a fact no field on the shape records — and guessing it wrong shifts the solid by half its old height on every
    //    re-adjust. Centring the delta is well-defined from what is actually known, and for the ordinary case (a flat profile, PriorDepth 0) delta
    //    IS the total, so the fresh symmetric sweep straddles the sketch plane exactly as expected.
    void ImposeSweepOnTargets(const SketchModelExtrudeModal& Modal, Frontier::ParametricSketchShapeStore& Store)
    {
        for (const SketchModelExtrudeTarget& Target : Modal.Targets)
        {
            Frontier::ParametricSketchShape* const Shape = Frontier::ResolveParametricSketchShape(Store, Target.Identifier);
            if (Shape == nullptr)
                continue;

            const float TotalDepth = Target.PriorDepth + Modal.Depth;
            const bool  Swept      = std::fabs(TotalDepth) >= DepthEps;

            Shape->ExtrudeDepth      = Swept ? TotalDepth : 0.0f;
            Shape->MatcapFillEnabled = Swept ? true : Target.PriorMatcapFill;
            Shape->Elevation         = (Swept && Modal.SymmetricEnabled) ? (Target.BaseElevation - Modal.Depth * 0.5f)
                                                                        : Target.BaseElevation;
        }
    }

    // 📝 Restore every target to the exact state it was armed in — the cancel path, and the "confirmed at zero height" path (nothing was swept, so
    //    there is nothing to keep). Writing all three snapshot fields back is what makes a cancel exact for a profile that was ALREADY a solid.
    void ReinstateArmTimeTargets(const SketchModelExtrudeModal& Modal, Frontier::ParametricSketchShapeStore& Store)
    {
        for (const SketchModelExtrudeTarget& Target : Modal.Targets)
        {
            Frontier::ParametricSketchShape* const Shape = Frontier::ResolveParametricSketchShape(Store, Target.Identifier);
            if (Shape == nullptr)
                continue;
            Shape->Elevation         = Target.BaseElevation;
            Shape->ExtrudeDepth      = Target.PriorDepth;
            Shape->MatcapFillEnabled = Target.PriorMatcapFill;
        }
    }

    // 📝 Reset the modal to idle, clearing every armed field so a re-arm never carries stale targets / numeric text / readout. Leaves the retained
    //    Last* block and CommitSerial untouched (the panel's History hook reads them AFTER the disarm), exactly like the fillet / offset modals.
    void ResetModal(SketchModelExtrudeModal& Modal)
    {
        Modal.Armed = false;
        Modal.Targets.clear();
        Modal.AnchorPixel         = ImVec2(0, 0);
        Modal.AnchorPending       = false;
        Modal.AnchorCentroid      = ImVec2(0, 0);
        Modal.AnchorElevation     = 0.0f;
        Modal.Depth               = 0.0f;
        Modal.SymmetricEnabled    = false;
        Modal.NumericEntryEnabled = false;
        Modal.NumericText[0]      = '\0';
        Modal.ReadoutText[0]      = '\0';
    }

    // 📝 Snapshot one shape as a sweep operand, resolving nothing about geometry beyond "it exists". An OPEN curve is a legitimate operand (it sweeps
    //    into a thin wall), so no closure test gates this — that split lives in the tessellator, which is the one place that should know it.
    bool CaptureTarget(Frontier::ParametricSketchShapeStore& Store, uint32_t Identifier, SketchModelExtrudeTarget& OutTarget)
    {
        const Frontier::ParametricSketchShape* const Shape = Frontier::ResolveParametricSketchShape(Store, Identifier);
        if (Shape == nullptr || !Shape->Displayed)
            return false;

        OutTarget.Identifier      = Identifier;
        OutTarget.BaseElevation   = Shape->Elevation;
        OutTarget.PriorDepth      = Shape->ExtrudeDepth;
        OutTarget.PriorMatcapFill = Shape->MatcapFillEnabled;
        return true;
    }

    // 📝 The targets' XY centroid + mean elevation at arm time — where the driver probes the sweep axis, so the pixels-per-mm the drag divides by is
    //    measured AT the solid rather than at the world origin (perspective makes those differ by a lot). Averages the defining points, which is cheap
    //    and always inside the profile. Falls back to the origin when nothing resolved, which cannot happen on an armed modal.
    void ResolveAnchorFrame(SketchModelExtrudeModal& Modal, Frontier::ParametricSketchShapeStore& Store)
    {
        ImVec2 Sum(0, 0);
        float  ElevationSum = 0.0f;
        int    PointCount   = 0;
        int    ShapeCount   = 0;

        for (const SketchModelExtrudeTarget& Target : Modal.Targets)
        {
            const Frontier::ParametricSketchShape* const Shape = Frontier::ResolveParametricSketchShape(Store, Target.Identifier);
            if (Shape == nullptr)
                continue;
            for (const ImVec2& Point : Shape->Points) { Sum.x += Point.x; Sum.y += Point.y; ++PointCount; }
            ElevationSum += Target.BaseElevation;
            ++ShapeCount;
        }

        Modal.AnchorCentroid  = (PointCount > 0) ? ImVec2(Sum.x / (float)PointCount, Sum.y / (float)PointCount) : ImVec2(0, 0);
        Modal.AnchorElevation = (ShapeCount > 0) ? (ElevationSum / (float)ShapeCount) : 0.0f;
    }

    // 📝 Finish an arm that captured at least one target: seed the anchor frame, zero the height, promote the targets so the solid is ready to appear
    //    the moment the pointer moves, and mark it armed LAST so a mid-arm early return leaves the modal idle.
    void SealArm(SketchModelExtrudeModal& Modal, Frontier::ParametricSketchShapeStore& Store, ImVec2 AnchorPixel,
                 bool AnchorPending, bool SymmetricEnabled)
    {
        ResolveAnchorFrame(Modal, Store);
        Modal.AnchorPixel         = AnchorPixel;
        Modal.AnchorPending       = AnchorPending;
        Modal.Depth               = 0.0f;   // starts at exactly zero — nothing pops on arm; the first drag grows it
        Modal.SymmetricEnabled    = SymmetricEnabled;
        Modal.NumericEntryEnabled = false;
        Modal.NumericText[0]      = '\0';
        Modal.VacantArmReported   = false;
        Modal.Armed               = true;

        ImposeSweepOnTargets(Modal, Store);   // zero height → each target still reads exactly as it was armed
        ConstructReadout(Modal);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ACTIVATION
//------------------------------------------------------------------------------------------------------------------------

void ActivateSketchModelExtrudeModal(SketchModelExtrudeModal&              Modal,
                                     Frontier::ParametricSketchShapeStore& Store,
                                     bool                                  SymmetricEnabled)
{
    ResetModal(Modal);   // fresh arm — clear any prior residue first

    // The operand set is the SELECTION: the ordered SelectionSet, or the lone Selected when a single click made the pick. Deliberately NOT a
    //    fall-back-to-everything (which the offset tool does): sweeping every shape in the sketch because none was picked would be a destructive
    //    surprise, whereas an offset merely appends. An empty selection instead routes the caller to the pick-then-drag phase.
    std::vector<uint32_t> Sources = Store.SelectionSet;
    if (Sources.empty() && Store.Selected != 0)
        Sources.push_back(Store.Selected);

    for (const uint32_t Identifier : Sources)
    {
        SketchModelExtrudeTarget Target;
        if (CaptureTarget(Store, Identifier, Target))
            Modal.Targets.push_back(Target);
    }

    if (Modal.Targets.empty())
    {
        Modal.VacantArmReported = true;   // the caller drops to the pick phase (or reports) instead of arming a gesture over nothing
        return;
    }

    // 🔴 No anchor to seed — this arm is dispatched from the console, which the panel records after the driver has run, so there is no live pointer
    //    position here. Pending instead: the driver latches it on its first armed frame, and the height is a hard zero until the pointer leaves it.
    SealArm(Modal, Store, ImVec2(0, 0), /*AnchorPending*/ true, SymmetricEnabled);
}


void ActivateSketchModelExtrudeModalOnShape(SketchModelExtrudeModal&              Modal,
                                            Frontier::ParametricSketchShapeStore& Store,
                                            uint32_t                              ShapeIdentifier,
                                            ImVec2                                AnchorPixel,
                                            bool                                  SymmetricEnabled)
{
    ResetModal(Modal);   // fresh arm — clear any prior residue first

    SketchModelExtrudeTarget Target;
    if (!CaptureTarget(Store, ShapeIdentifier, Target))
    {
        Modal.VacantArmReported = true;   // vanished between hover and click
        return;
    }
    Modal.Targets.push_back(Target);

    // The pick also SELECTS the shape, so the green highlight agrees with what the drag is sweeping and a later re-run of the tool picks it up
    //    from the selection rather than needing a second pick.
    Store.SelectionSet.assign(1, ShapeIdentifier);
    Store.Selected = ShapeIdentifier;

    // The pick click IS on the canvas, on the profile — the right origin to measure travel from, so the anchor is final immediately.
    SealArm(Modal, Store, AnchorPixel, /*AnchorPending*/ false, SymmetricEnabled);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            INTEGRATE
//------------------------------------------------------------------------------------------------------------------------

bool IntegrateSketchModelExtrudeModal(SketchModelExtrudeModal&              Modal,
                                      Frontier::ParametricSketchShapeStore& Store,
                                      const SketchModelExtrudeModalInput&   Input)
{
    if (!Modal.Armed)
        return false;   // idle — nothing to fold, the caller does not veto the pick

    // ── Cancel: restore every target's arm-time state, then disarm. The live write means there IS something to undo here (unlike the fillet /
    //    offset modals, whose cancel is a pure disarm) — see the header. ──────────────────────────────────────────────────────────────────────
    if (Input.CancelPressed)
    {
        ReinstateArmTimeTargets(Modal, Store);
        ResetModal(Modal);
        return true;   // was active — veto the same-frame pick
    }

    // ── Fold this frame's numeric edits into the buffer. ──────────────────────────────────────────────────────────────────────────────────────
    if (Input.BackspacePressed) ReleaseNumericCharacter(Modal);
    for (size_t Index = 0; Index < sizeof(Input.TypedDigits) && Input.TypedDigits[Index] != '\0'; ++Index)
        AbsorbNumericCharacter(Modal, Input.TypedDigits[Index]);

    // ── Derive the live height: a typed number is the ABSOLUTE height and overrides the drag; otherwise the pointer's travel along the projected
    //    sweep axis IS the height. An unresolved axis (a view straight down it) holds the last height rather than snapping to zero. ────────────
    if (Modal.NumericEntryEnabled)
    {
        double NumericValue = 0.0;
        if (EvaluateNumericAmount(Modal.NumericText, NumericValue))
            Modal.Depth = (float)NumericValue;
    }
    else if (Input.SweepResolved)
    {
        Modal.Depth = Input.SweepMillimetres;
    }

    // 🔴 Write the height onto the targets EVERY frame, before the confirm test. This is the live solid: the store tessellates it in the panel's
    //    publish step and the GPU matcap pass renders it, so the user watches the prism grow instead of an outline.
    ImposeSweepOnTargets(Modal, Store);
    ConstructReadout(Modal);

    // ── Confirm: keep what is already written and retain the sweep for the History hook, then disarm. A ~zero height keeps NOTHING — the targets are
    //    restored to their arm-time state, because a click that never dragged is a cancelled gesture in every CAD app, not a zero-height solid. ──
    if (Input.ConfirmPressed)
    {
        const float ConfirmedDepth = Modal.Depth;
        const int   TargetCount    = (int)Modal.Targets.size();
        const bool  Swept          = std::fabs(ConfirmedDepth) >= DepthEps;

        if (!Swept)
            ReinstateArmTimeTargets(Modal, Store);

        ResetModal(Modal);

        if (Swept)
        {
            Modal.LastCommitValid = true;
            Modal.LastDepth       = ConfirmedDepth;
            Modal.LastTargetCount = TargetCount;
            ++Modal.CommitSerial;   // one confirmed sweep — the panel logs a History revision off this edge
        }
    }

    return true;   // a modal was active this frame — veto the same-frame pick / draw / pan
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ABANDON
//------------------------------------------------------------------------------------------------------------------------

void AbandonSketchModelExtrudeModal(SketchModelExtrudeModal&              Modal,
                                    Frontier::ParametricSketchShapeStore& Store)
{
    if (!Modal.Armed)
        return;
    ReinstateArmTimeTargets(Modal, Store);
    ResetModal(Modal);
}

}   // namespace SketchModelViewportValidation
