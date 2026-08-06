/*==============================================================================================================================================
                                                        SKETCHMODELINSETMODAL.CPP
==============================================================================================================================================*/
// 🧩 The Offset tool as a modal DRAG over the current selection of closed shapes: arm on every selected closed shape, then fold each frame's cursor
//    drag / typed number into a live SIGNED distance (positive = outward / inflate, negative = inward / deflate). The drag reads as the pointer's
//    signed radial distance from the arm anchor's centroid — pulling away inflates, pushing toward the centre deflates — so the gesture matches the
//    retired `I` tool. Nothing mutates until confirm: the preview is resolved fresh each frame by the consumer from ResolveSketchInsetModalPreview,
//    and only a confirm commits through AppendOffsetResult. Retyped from the retired DraughtInsetModal onto the ported Frontier::ParametricSketch*
//    engine, the multi-panel registry dropped for one modal in state. The commit KEEPS the originals, so the redo box's re-apply detaches its own
//    prior Profiles first (never a stack).

#include "SketchModelInsetModal.h"

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
    constexpr float DistanceEps      = 1e-3f;   // [mm] - below this the offset is treated as zero (no preview, no commit)
    constexpr int   PreviewFlatten   = 96;      // [-]  - dense flatten budget for the preview (matches the commit's OffsetFlattenBudget order)

    // 📝 Parse the typed numeric buffer into a double. Partial buffers not yet a number ("", "-", ".", "-.") resolve to 0.0 so the live preview
    //    stays put until a digit follows. Returns false on a partial buffer. (Mirrors the fillet modal's EvaluateNumericAmount.)
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

    // 📝 Append one typed character to the numeric buffer: a single leading '-' (a negative = inward offset), a single '.', digits anywhere. Drops
    //    anything else or an overflow. Enables numeric entry on the first accepted character.
    void AbsorbNumericCharacter(SketchModelInsetModal& Modal, char Character)
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

    // 📝 Pop one character off the numeric buffer (Backspace). Clearing it disables numeric entry so pointer motion drives the distance again.
    void ReleaseNumericCharacter(SketchModelInsetModal& Modal)
    {
        const size_t Length = std::strlen(Modal.NumericText);
        if (Length == 0)
            return;

        Modal.NumericText[Length - 1] = '\0';
        if (Modal.NumericText[0] == '\0')
            Modal.NumericEntryEnabled = false;
    }

    // 📝 The centroid of the captured targets' outlines at arm time — the reference the radial drag measures distance from. Averages every displayed
    //    closed target's defining points (cheap, stable, and inside the region), so pulling the pointer away from it inflates and pushing in deflates.
    ImVec2 ResolveTargetsCentroid(const SketchModelInsetModal& Modal, Frontier::ParametricSketchShapeStore& Store)
    {
        ImVec2 Sum(0, 0);
        int    Count = 0;
        for (uint32_t Identifier : Modal.Targets)
        {
            const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, Identifier);
            if (Shape == nullptr) continue;
            for (const ImVec2& Point : Shape->Points) { Sum.x += Point.x; Sum.y += Point.y; ++Count; }
        }
        if (Count == 0) return Modal.AnchorWorld;
        return ImVec2(Sum.x / (float)Count, Sum.y / (float)Count);
    }

    // 📝 Fill Modal.ReadoutText with the short HUD string. During numeric entry the raw typed buffer is echoed; otherwise the live signed distance
    //    prints in mm. "Out" for a positive (inflate) offset, "In" for a negative (deflate) one.
    void ConstructReadout(SketchModelInsetModal& Modal)
    {
        const char* Label = (Modal.Magnitude < 0.0f) ? "Offset In" : "Offset Out";
        if (Modal.NumericEntryEnabled)
            std::snprintf(Modal.ReadoutText, sizeof(Modal.ReadoutText), "%s: %s mm", Label, Modal.NumericText);
        else
            std::snprintf(Modal.ReadoutText, sizeof(Modal.ReadoutText), "%s: %.3g mm", Label, std::fabs(Modal.Magnitude));
    }

    // 📝 Reset the modal to idle, clearing every armed field so a re-arm never carries stale targets / numeric text / readout. Leaves the retained
    //    Last* block untouched (the redo box reads it after the disarm), exactly like the fillet modal's ResetModal.
    void ResetModal(SketchModelInsetModal& Modal)
    {
        Modal.Armed               = false;
        Modal.Targets.clear();
        Modal.AnchorWorld         = ImVec2(0, 0);
        Modal.Magnitude           = 0.0f;
        Modal.CornerStyle         = Frontier::SketchOffsetCornerStyle::Miter;   // sharp corners by default — a rectangle offset outward keeps square corners (Blender/CAD feel); the redo box's dropdown opts into Round/Bevel
        Modal.NumericEntryEnabled = false;
        Modal.NumericText[0]      = '\0';
        Modal.ReadoutText[0]      = '\0';
    }

    // 📝 Snapshot every OFFSETTABLE shape the offset should act on. Both closed regions (inflate / deflate into a Profile) AND open curves (a single
    //    parallel curve) offset now, so every resolvable shape qualifies. Source resolution matches AppendOffsetResult so the modal captures exactly
    //    what the commit will act on, with ONE addition: when NOTHING is selected we fall back to EVERY displayed shape. This is the Blender `I` feel
    //    the gate already promised — the Q console enables Offset the instant the store is non-empty (no pre-select required, see ResolveActiveDimension),
    //    so committing it with no select-click must offset the whole visible sketch, not silently no-op. A select-click still narrows it to that shape.
    void CaptureOffsettableSelection(Frontier::ParametricSketchShapeStore& Store, std::vector<uint32_t>& OutTargets)
    {
        OutTargets.clear();
        std::vector<uint32_t> Sources = Store.SelectionSet;
        if (Sources.empty() && Store.Selected != 0)
            Sources.push_back(Store.Selected);

        if (Sources.empty())
        {
            // No selection → the whole sketch. Take every DISPLAYED shape (a hidden shape is not an offset operand), preserving store order.
            for (const Frontier::ParametricSketchShape& Shape : Store.Shapes)
                if (Shape.Displayed && Shape.Identifier != 0)
                    OutTargets.push_back(Shape.Identifier);
            return;
        }

        for (uint32_t Identifier : Sources)
        {
            const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, Identifier);
            if (Shape != nullptr)
                OutTargets.push_back(Identifier);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ACTIVATION
//------------------------------------------------------------------------------------------------------------------------

void ActivateSketchModelInsetModal(SketchModelInsetModal&                Modal,
                                   Frontier::ParametricSketchShapeStore& Store,
                                   float                                 AnchorX,
                                   float                                 AnchorY)
{
    ResetModal(Modal);   // fresh arm — clear any prior residue first

    CaptureOffsettableSelection(Store, Modal.Targets);
    if (Modal.Targets.empty())
    {
        // No operand at all — the sketch is empty (or every shape hidden). Surface it instead of a silent no-op arm, so the tool never "does nothing"
        //    without a reason. Armed stays false, so the drag below never engages. (RaiseNotice is file-local per subsystem — set the shared store fields
        //    directly, exactly as the transform / boolean paths do.)
        std::snprintf(Store.Notice, sizeof(Store.Notice), "%s", "Offset needs a shape - draw one first");
        Store.NoticeTimer = 4.0f;
        return;
    }

    // 📝 The drag origin. When the caller has a live ground cursor (the sticky re-arm passes it) the anchor is that point, so a continued drag from the
    //    same spot reads zero until it moves. When the caller has NO cursor (the console commit arms cold) it passes the targets' centroid, so the very
    //    first drag reads the pointer's radius from the centre — "drag outward to inflate" with no dead zone. Either way the integrate measures the
    //    radial delta from the centroid, so the anchor only sets where "zero distance" sits.
    Modal.AnchorWorld = ImVec2(AnchorX, AnchorY);
    Modal.Magnitude   = 0.0f;   // starts at zero; the first drag / digit grows it
    Modal.CornerStyle = Frontier::SketchOffsetCornerStyle::Miter;   // sharp corners by default (see ResetModal)
    Modal.Armed       = true;   // armed last, so a mid-arm early return leaves the modal idle
}

void ActivateSketchModelInsetModalOnShape(SketchModelInsetModal&                Modal,
                                          Frontier::ParametricSketchShapeStore& Store,
                                          uint32_t                              ShapeIdentifier,
                                          float                                 AnchorX,
                                          float                                 AnchorY)
{
    ResetModal(Modal);   // fresh arm — clear any prior residue first

    // The TWO-PHASE pick supplies ONE shape (the edge / face the user pointed at), so the operand set is exactly that shape — not the whole selection.
    //    A no-op arm (Armed stays false) if the id no longer resolves (deleted between hover and click).
    const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, ShapeIdentifier);
    if (Shape == nullptr)
        return;

    Modal.Targets.clear();
    Modal.Targets.push_back(ShapeIdentifier);

    // The anchor is the pick click's ground point (where "zero distance" sits), so a drag away from it grows the offset from the very first pixel.
    Modal.AnchorWorld = ImVec2(AnchorX, AnchorY);
    Modal.Magnitude   = 0.0f;
    Modal.CornerStyle = Frontier::SketchOffsetCornerStyle::Miter;   // sharp corners by default (see ResetModal)
    Modal.Armed       = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            INTEGRATE
//------------------------------------------------------------------------------------------------------------------------

bool IntegrateSketchModelInsetModal(SketchModelInsetModal&                Modal,
                                    Frontier::ParametricSketchShapeStore& Store,
                                    const SketchModelInsetModalInput&     Input)
{
    if (!Modal.Armed)
        return false;   // idle — nothing to fold, the caller does not veto the pick

    // ── Cancel: disarm with no change (nothing was mutated — the preview is resolved separately). ──────────────────────────
    if (Input.CancelPressed)
    {
        ResetModal(Modal);
        return true;   // was active — veto the same-frame pick
    }

    // ── Fold this frame's numeric edits into the buffer. ──────────────────────────────────────────────────────────────────
    if (Input.BackspacePressed) ReleaseNumericCharacter(Modal);
    for (size_t Index = 0; Index < sizeof(Input.TypedDigits) && Input.TypedDigits[Index] != '\0'; ++Index)
        AbsorbNumericCharacter(Modal, Input.TypedDigits[Index]);

    // ── Derive the signed distance: numeric entry overrides the drag; a leading '-' (or a drag toward the centre) selects an inward offset. ──
    float Signed = 0.0f;
    if (Modal.NumericEntryEnabled)
    {
        double NumericValue = 0.0;
        if (EvaluateNumericAmount(Modal.NumericText, NumericValue))
            Signed = (float)NumericValue;
    }
    else if (Input.CursorMoved)
    {
        // The radial drag: the pointer's distance from the targets' centroid minus its distance at the ARM anchor. Pulling away inflates
        //    (positive), pushing toward the centre deflates (negative). Both distances are to the same centroid, so the delta is the offset.
        const ImVec2 Centroid = ResolveTargetsCentroid(Modal, Store);
        const ImVec2 Now      = ImVec2(Input.CursorX - Centroid.x,      Input.CursorY - Centroid.y);
        const ImVec2 Origin   = ImVec2(Modal.AnchorWorld.x - Centroid.x, Modal.AnchorWorld.y - Centroid.y);
        const float  NowRadius    = std::sqrt(Now.x * Now.x + Now.y * Now.y);
        const float  OriginRadius = std::sqrt(Origin.x * Origin.x + Origin.y * Origin.y);
        Signed = NowRadius - OriginRadius;
    }
    else
    {
        Signed = Modal.Magnitude;   // no motion this frame — hold the last distance
    }

    Modal.Magnitude = Signed;
    ConstructReadout(Modal);

    // ── Confirm: commit through AppendOffsetResult, RETAIN it for the redo box, then disarm. A ~zero distance commits nothing but still closes (and
    //    leaves no retained edit). The commit re-selects its fresh Profiles; capture those ids from the store's SelectionSet AFTER the append so the
    //    redo box can detach them before a re-apply. The retained fields survive ResetModal — the operator/redo box reads them AFTER the disarm. ──
    if (Input.ConfirmPressed)
    {
        const std::vector<uint32_t> CommitSources = Modal.Targets;   // snapshot before ResetModal wipes them
        const float                 CommitDistance = Modal.Magnitude;
        const Frontier::SketchOffsetCornerStyle CommitStyle = Modal.CornerStyle;

        bool Committed = false;
        std::vector<uint32_t> Appended;
        if (std::fabs(CommitDistance) >= DistanceEps)
        {
            // Re-seat the selection to the captured sources so AppendOffsetResult acts on exactly them (a stray hover / pick since arm cannot leak in).
            Store.SelectionSet = CommitSources;
            Store.Selected     = CommitSources.empty() ? 0u : CommitSources.back();

            Committed = (Frontier::AppendOffsetResult(Store, CommitDistance, CommitStyle) == Frontier::OffsetOutcome::Committed);
            if (Committed)
                Appended = Store.SelectionSet;   // AppendOffsetResult re-selected the fresh Profiles into SelectionSet
        }

        ResetModal(Modal);

        if (Committed)
        {
            Modal.LastCommitValid = true;
            Modal.LastSources     = CommitSources;
            Modal.LastAppended    = Appended;
            Modal.LastMagnitude   = CommitDistance;
            Modal.LastCornerStyle = CommitStyle;
            ++Modal.CommitSerial;   // one fresh commit — the panel logs a History revision + the tool re-arms on this edge
        }
    }

    return true;   // a modal was active this frame — veto the same-frame pick / draw / pan
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            PREVIEW
//------------------------------------------------------------------------------------------------------------------------

std::vector<SketchInsetPreviewLoop> ResolveSketchInsetModalPreview(const SketchModelInsetModal&          Modal,
                                                                  Frontier::ParametricSketchShapeStore& Store)
{
    std::vector<SketchInsetPreviewLoop> Loops;
    if (!Modal.Armed || std::fabs(Modal.Magnitude) < DistanceEps)
        return Loops;   // idle / degenerate — nothing to paint

    for (uint32_t Identifier : Modal.Targets)
    {
        Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, Identifier);
        if (Shape == nullptr)
            continue;

        // An OPEN curve previews as a single OPEN parallel curve (the SAME SolveOpenCurveOffset the commit runs); a CLOSED region previews its
        //    inflated / deflated CLOSED loops (EvaluateFilledPolygon + SolveRegionOffset). The region carries the shape's hole rings too, so a
        //    punched profile previews its voids exactly as the commit will offset them. Both are pure reads — nothing in the store is mutated.
        if (!Shape->ClosedEnabled)
        {
            std::vector<ImVec2> Open;
            Frontier::EvaluateShapePolyline(*Shape, Open, PreviewFlatten);
            if (Open.size() < 2)
                continue;
            std::vector<ImVec2> Parallel = Frontier::SolveOpenCurveOffset(Open, Modal.Magnitude);
            if (Parallel.size() >= 2)
                Loops.push_back(SketchInsetPreviewLoop{ std::move(Parallel), false });
            continue;
        }

        std::vector<std::vector<ImVec2>> Region;
        std::vector<ImVec2> Outline;
        Frontier::EvaluateFilledPolygon(*Shape, Outline, PreviewFlatten);
        if (Outline.size() < 3)
            continue;
        Region.push_back(std::move(Outline));
        for (const std::vector<ImVec2>& Hole : Shape->HoleLoops)
        {
            if (Hole.size() >= 3)
                Region.push_back(Hole);
        }

        std::vector<std::vector<ImVec2>> Offset = Frontier::SolveRegionOffset(Region, Modal.Magnitude, Modal.CornerStyle);
        for (std::vector<ImVec2>& Loop : Offset)
            if (Loop.size() >= 2)
                Loops.push_back(SketchInsetPreviewLoop{ std::move(Loop), true });
    }
    return Loops;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       REDO-BOX RE-APPLY
//------------------------------------------------------------------------------------------------------------------------

bool ReapplySketchInsetModalCommit(SketchModelInsetModal&                Modal,
                                   Frontier::ParametricSketchShapeStore& Store,
                                   float                                 NewDistance,
                                   Frontier::SketchOffsetCornerStyle     NewCornerStyle)
{
    if (!Modal.LastCommitValid)
        return false;

    // DETACH the prior appended Profiles first — offset keeps its originals, so without this a slider slide would STACK a fresh set every frame. A
    //    prior Profile may already be gone (a manual delete); DetachParametricSketchShape no-ops on an absent id, so the loop is safe.
    for (uint32_t Identifier : Modal.LastAppended)
        Frontier::DetachParametricSketchShape(Store, Identifier);
    Modal.LastAppended.clear();

    // Re-seat the selection to the retained SOURCES (still-present ones) so AppendOffsetResult re-offsets exactly them. If every source has vanished,
    //    drop the retained edit (the redo box then hides).
    std::vector<uint32_t> LiveSources;
    for (uint32_t Identifier : Modal.LastSources)
        if (Frontier::ResolveParametricSketchShape(Store, Identifier) != nullptr)
            LiveSources.push_back(Identifier);
    if (LiveSources.empty())
    {
        Modal.LastCommitValid = false;
        return false;
    }

    // A ~zero distance ERASES the edit: the prior Profiles are already detached above, so there is nothing to re-append. The redo box then hides.
    if (std::fabs(NewDistance) < DistanceEps)
    {
        Modal.LastCommitValid = false;
        return true;
    }

    Store.SelectionSet = LiveSources;
    Store.Selected     = LiveSources.back();

    const bool Committed = (Frontier::AppendOffsetResult(Store, NewDistance, NewCornerStyle) == Frontier::OffsetOutcome::Committed);
    if (Committed)
    {
        Modal.LastAppended    = Store.SelectionSet;   // the fresh Profiles the re-apply produced
        Modal.LastMagnitude   = NewDistance;
        Modal.LastCornerStyle = NewCornerStyle;
    }
    return Committed;
}

void ClearSketchInsetModalHistory(SketchModelInsetModal& Modal)
{
    Modal.LastCommitValid = false;
    Modal.LastSources.clear();
    Modal.LastAppended.clear();
    Modal.LastMagnitude   = 0.0f;
    Modal.LastCornerStyle = Frontier::SketchOffsetCornerStyle::Round;
}

}   // namespace SketchModelViewportValidation
