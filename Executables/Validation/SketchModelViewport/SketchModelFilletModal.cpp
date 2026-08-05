/*==============================================================================================================================================
                                                        SKETCHMODELFILLETMODAL.CPP
==============================================================================================================================================*/
// 🧩 The Plasticity `B` tool as a modal over ONE picked corner: arm on a vertex handle, then fold each frame's cursor drag / typed number into a
//    live magnitude whose SIGN chooses the outcome — positive grows a FILLET radius, negative swaps to a CHAMFER distance. The drag reads as the
//    signed projection of the pointer onto the corner's interior bisector (pushing INTO the corner = positive fillet; pulling OUT past the corner =
//    negative chamfer), so the gesture matches Plasticity's single `B` tool that fillets one way and chamfers the other. The magnitude clamps to the
//    corner's safe limit (the shorter leg). Nothing mutates until confirm: the preview is resolved fresh each frame by the consumer from
//    ResolveSketchFilletModalPreview, and only a confirm commits through FilletShapeCorner / ChamferShapeCorner. Retyped 1:1 from the retired
//    DraughtFilletModal onto the ported Frontier::ParametricSketch* engine, with the multi-panel registry dropped for one modal in state.

#include "SketchModelFilletModal.h"

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
    constexpr float CornerEps = 1e-4f;   // [mm] - matches ParametricSketchFillet's degenerate-corner floor

    // 📝 Parse the typed numeric buffer into a double. Partial buffers not yet a number ("", "-", ".", "-.") resolve to 0.0 so the live
    //    preview stays put until a digit follows. Returns false on a partial buffer.
    bool EvaluateNumericAmount(const char* Text, double& Parsed)
    {
        Parsed = 0.0;
        if (Text == nullptr || Text[0] == '\0')
        {
            return false;
        }

        char*        Terminator = nullptr;
        const double Candidate  = std::strtod(Text, &Terminator);
        if (Terminator == Text || Terminator == nullptr || *Terminator != '\0')
        {
            return false;   // partial like "-" / "." / "-." — no full number yet
        }

        Parsed = Candidate;
        return true;
    }

    // 📝 Append one typed character to the numeric buffer: a single leading '-' only (which forces Chamfer), a single '.', digits anywhere.
    //    Drops anything else or an overflow. Enables numeric entry on the first accepted character.
    void AbsorbNumericCharacter(SketchModelFilletModal& Modal, char Character)
    {
        const size_t Length = std::strlen(Modal.NumericText);
        if (Length + 1 >= sizeof(Modal.NumericText))
        {
            return;
        }

        const bool DigitCharacter = (Character >= '0' && Character <= '9');
        if (Character == '-')
        {
            if (Length != 0)   // sign only leads
            {
                return;
            }
        }
        else if (Character == '.')
        {
            if (std::strchr(Modal.NumericText, '.') != nullptr)   // single decimal point
            {
                return;
            }
        }
        else if (!DigitCharacter)
        {
            return;
        }

        Modal.NumericText[Length]     = Character;
        Modal.NumericText[Length + 1] = '\0';
        Modal.NumericEntryEnabled     = true;
    }

    // 📝 Pop one character off the numeric buffer (Backspace). Clearing it disables numeric entry so pointer motion drives the amount again.
    void ReleaseNumericCharacter(SketchModelFilletModal& Modal)
    {
        const size_t Length = std::strlen(Modal.NumericText);
        if (Length == 0)
        {
            return;
        }

        Modal.NumericText[Length - 1] = '\0';
        if (Modal.NumericText[0] == '\0')
        {
            Modal.NumericEntryEnabled = false;
        }
    }

    // 📝 Resolve the interior bisector unit vector at the corner Index of Shape — the axis the drag projects onto. Returns false for a
    //    degenerate / collinear corner or an endpoint of an open run. The bisector points INTO the corner (toward the shape interior), so a
    //    pointer pushed that way reads positive (fillet) and pulled the other way negative.
    bool ResolveCornerBisector(const Frontier::ParametricSketchShape& Shape, int Index, ImVec2& CornerOut, ImVec2& BisectorOut)
    {
        const int Count = (int)Shape.Points.size();
        if (Count < 3 || Index < 0 || Index >= Count)
        {
            return false;
        }

        ImVec2 Previous, Corner, Next;
        if (Shape.ClosedEnabled)
        {
            Previous = Shape.Points[(Index - 1 + Count) % Count];
            Corner   = Shape.Points[Index];
            Next     = Shape.Points[(Index + 1) % Count];
        }
        else
        {
            if (Index == 0 || Index == Count - 1)
            {
                return false;   // endpoint of an open run — only one leg
            }
            Previous = Shape.Points[Index - 1];
            Corner   = Shape.Points[Index];
            Next     = Shape.Points[Index + 1];
        }

        const ImVec2 RawU  = ImVec2(Previous.x - Corner.x, Previous.y - Corner.y);
        const ImVec2 RawV  = ImVec2(Next.x - Corner.x,     Next.y - Corner.y);
        const float  LenU  = std::sqrt(RawU.x * RawU.x + RawU.y * RawU.y);
        const float  LenV  = std::sqrt(RawV.x * RawV.x + RawV.y * RawV.y);
        if (LenU < CornerEps || LenV < CornerEps)
        {
            return false;
        }

        const ImVec2 UnitU  = ImVec2(RawU.x / LenU, RawU.y / LenU);
        const ImVec2 UnitV  = ImVec2(RawV.x / LenV, RawV.y / LenV);
        const ImVec2 Sum    = ImVec2(UnitU.x + UnitV.x, UnitU.y + UnitV.y);
        const float  SumLen = std::sqrt(Sum.x * Sum.x + Sum.y * Sum.y);
        if (SumLen < CornerEps)
        {
            return false;   // fully-folded (180°) corner — no interior direction
        }

        CornerOut   = Corner;
        BisectorOut = ImVec2(Sum.x / SumLen, Sum.y / SumLen);   // unit, pointing into the corner interior
        return true;
    }

    // 📝 Fill Modal.ReadoutText with the short Plasticity-faithful HUD string. During numeric entry the raw typed buffer is echoed; otherwise
    //    the live clamped magnitude prints in mm. The label follows the live Category so a drag past zero reads "Chamfer" the instant it flips.
    void ConstructReadout(SketchModelFilletModal& Modal)
    {
        const char* Label = (Modal.Category == Frontier::ParametricSketchCornerCategory::Fillet) ? "Fillet R" : "Chamfer";
        if (Modal.NumericEntryEnabled)
        {
            std::snprintf(Modal.ReadoutText, sizeof(Modal.ReadoutText), "%s: %s mm", Label, Modal.NumericText);
        }
        else
        {
            std::snprintf(Modal.ReadoutText, sizeof(Modal.ReadoutText), "%s: %.3g mm", Label, Modal.Magnitude);
        }
    }

    // 📝 Reset the modal to idle, clearing every armed field so a re-arm never carries stale corner / numeric text / readout.
    void ResetModal(SketchModelFilletModal& Modal)
    {
        Modal.Armed               = false;
        Modal.DragArmed           = false;
        Modal.TargetIdentifier    = 0;
        Modal.CornerIndex         = -1;
        Modal.Category            = Frontier::ParametricSketchCornerCategory::Fillet;
        Modal.CornerWorld         = ImVec2(0, 0);
        Modal.SafeLimit           = 0.0f;
        Modal.Magnitude           = 0.0f;
        Modal.NumericEntryEnabled = false;
        Modal.NumericText[0]      = '\0';
        Modal.ReadoutText[0]      = '\0';
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ACTIVATION
//------------------------------------------------------------------------------------------------------------------------

void ActivateSketchModelFilletModal(SketchModelFilletModal&              Modal,
                                    Frontier::ParametricSketchShapeStore& Store,
                                    uint32_t                             Identifier,
                                    int                                  CornerIndex)
{
    ResetModal(Modal);   // fresh arm — clear any prior residue first

    Frontier::ParametricSketchShape* Target = Frontier::ResolveParametricSketchShape(Store, Identifier);
    if (Target == nullptr)
    {
        return;   // no shape — no-op arm
    }

    // The corner must resolve a fillet (a real, non-collinear corner with two legs); reading the safe limit proves that.
    const float SafeLimit = Frontier::ResolveCornerSafeLimit(*Target, CornerIndex);
    if (SafeLimit < CornerEps)
    {
        return;   // degenerate / endpoint / collinear — no-op arm, Armed stays false
    }

    ImVec2 Corner, Bisector;
    if (!ResolveCornerBisector(*Target, CornerIndex, Corner, Bisector))
    {
        return;
    }

    Modal.TargetIdentifier = Identifier;
    Modal.CornerIndex      = CornerIndex;
    Modal.CornerWorld      = Corner;
    Modal.SafeLimit        = SafeLimit;
    Modal.Category         = Frontier::ParametricSketchCornerCategory::Fillet;
    Modal.Magnitude        = 0.0f;   // starts at zero; the first drag / digit grows it
    Modal.Armed            = true;   // armed last, so a mid-arm early return leaves the modal idle
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            INTEGRATE
//------------------------------------------------------------------------------------------------------------------------

bool IntegrateSketchModelFilletModal(SketchModelFilletModal&              Modal,
                                     Frontier::ParametricSketchShapeStore& Store,
                                     const SketchModelFilletModalInput&   Input)
{
    if (!Modal.Armed)
    {
        return false;   // idle — nothing to fold, the caller does not veto the pick
    }

    // A lost / changed target disarms without touching the store (the captured shape may be gone).
    Frontier::ParametricSketchShape* Target = Frontier::ResolveParametricSketchShape(Store, Modal.TargetIdentifier);
    if (Target == nullptr)
    {
        ResetModal(Modal);
        return false;
    }

    // ── Cancel: disarm with no change (nothing was mutated — the preview is resolved separately). ──────────────────────────
    if (Input.CancelPressed)
    {
        ResetModal(Modal);
        return true;   // was active — veto the same-frame pick
    }

    // ── Fold this frame's numeric edits into the buffer. ──────────────────────────────────────────────────────────────────
    if (Input.BackspacePressed) ReleaseNumericCharacter(Modal);
    for (size_t Index = 0; Index < sizeof(Input.TypedDigits) && Input.TypedDigits[Index] != '\0'; ++Index)
    {
        AbsorbNumericCharacter(Modal, Input.TypedDigits[Index]);
    }

    // ── Derive the signed magnitude: numeric entry overrides the drag; a leading '-' (or a drag past zero) selects Chamfer. ──
    float Signed = 0.0f;
    if (Modal.NumericEntryEnabled)
    {
        double NumericValue = 0.0;
        if (EvaluateNumericAmount(Modal.NumericText, NumericValue))
        {
            Signed = (float)NumericValue;
        }
    }
    else
    {
        // Signed projection of the pointer's offset from the corner onto the interior bisector: pushing INTO the corner reads
        //    positive (fillet), pulling OUT past the corner reads negative (chamfer). Resolve the bisector fresh (the corner may move).
        ImVec2 Corner, Bisector;
        if (ResolveCornerBisector(*Target, Modal.CornerIndex, Corner, Bisector))
        {
            const ImVec2 Offset = ImVec2(Input.CursorX - Corner.x, Input.CursorY - Corner.y);
            Signed = Offset.x * Bisector.x + Offset.y * Bisector.y;
        }
    }

    // Sign chooses the outcome; the clamped magnitude is the |value| bounded by the corner's safe limit.
    Modal.Category  = (Signed < 0.0f) ? Frontier::ParametricSketchCornerCategory::Chamfer
                                      : Frontier::ParametricSketchCornerCategory::Fillet;
    float Magnitude = std::fabs(Signed);
    if (Magnitude > Modal.SafeLimit)
    {
        Magnitude = Modal.SafeLimit;
    }
    Modal.Magnitude = Magnitude;

    ConstructReadout(Modal);

    // ── Confirm: commit through the analytic edit, RETAIN it for the redo box, then disarm. A zero / degenerate magnitude commits nothing but still
    //    closes (and leaves no retained edit). The retained fields survive ResetModal — they are read by the operator/redo box AFTER the disarm. ─
    if (Input.ConfirmPressed)
    {
        // Snapshot the commit BEFORE ResetModal wipes the armed fields, so the retained values are the ones actually committed.
        const uint32_t CommitShapeId  = Modal.TargetIdentifier;
        const int      CommitCorner   = Modal.CornerIndex;
        const float    CommitMagnitude = Modal.Magnitude;
        const Frontier::ParametricSketchCornerCategory CommitCategory = Modal.Category;
        const float    CommitSafeLimit = Modal.SafeLimit;

        bool Committed = false;
        if (CommitMagnitude >= CornerEps)
        {
            if (CommitCategory == Frontier::ParametricSketchCornerCategory::Fillet)
                Committed = Frontier::FilletShapeCorner(Store, CommitShapeId, CommitCorner, CommitMagnitude);
            else
                Committed = Frontier::ChamferShapeCorner(Store, CommitShapeId, CommitCorner, CommitMagnitude);
        }

        ResetModal(Modal);

        if (Committed)
        {
            Modal.LastCommitValid = true;
            Modal.LastShapeId     = CommitShapeId;
            Modal.LastCornerIndex = CommitCorner;
            Modal.LastMagnitude   = CommitMagnitude;
            Modal.LastCategory    = CommitCategory;
            Modal.LastSafeLimit   = CommitSafeLimit;
            ++Modal.CommitSerial;   // one fresh-corner commit — the panel logs a History revision + the tool re-arms the pick on this edge
        }
    }

    return true;   // a modal was active this frame — veto the same-frame pick / draw / pan
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            PREVIEW
//------------------------------------------------------------------------------------------------------------------------

Frontier::ParametricSketchCornerSolution ResolveSketchFilletModalPreview(const SketchModelFilletModal&        Modal,
                                                                         Frontier::ParametricSketchShapeStore& Store)
{
    Frontier::ParametricSketchCornerSolution Solution;   // Resolved defaults false — the idle / degenerate return
    if (!Modal.Armed || Modal.Magnitude < CornerEps)
    {
        return Solution;
    }

    const Frontier::ParametricSketchShape* Target = Frontier::ResolveParametricSketchShape(Store, Modal.TargetIdentifier);
    if (Target == nullptr)
    {
        return Solution;
    }

    return Frontier::SolveCornerEdit(*Target, Modal.CornerIndex, Modal.Magnitude, Modal.Category);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       REDO-BOX RE-APPLY
//------------------------------------------------------------------------------------------------------------------------

bool ReapplySketchFilletModalCommit(SketchModelFilletModal&              Modal,
                                    Frontier::ParametricSketchShapeStore& Store,
                                    float                                NewMagnitude,
                                    Frontier::ParametricSketchCornerCategory NewCategory)
{
    if (!Modal.LastCommitValid)
        return false;

    // The shape may have been deleted between the commit and this edit — drop the retained edit if so (the redo box then hides).
    Frontier::ParametricSketchShape* const Target = Frontier::ResolveParametricSketchShape(Store, Modal.LastShapeId);
    if (Target == nullptr)
    {
        Modal.LastCommitValid = false;
        return false;
    }

    // A non-positive magnitude ERASES the corner edit (Fillet/ChamferShapeCorner treat magnitude <= 0 as the clear-to-sharp request), and the redo
    //    box then has nothing left to adjust. Otherwise re-commit at the new amount / category: the commit overwrites the corner's stored fillet
    //    record in place (keyed by CornerIndex), so this never stacks arcs no matter how many times the slider is dragged.
    if (NewMagnitude <= 0.0f)
    {
        Frontier::FilletShapeCorner(Store, Modal.LastShapeId, Modal.LastCornerIndex, 0.0f);   // magnitude 0 = erase (category irrelevant)
        Modal.LastCommitValid = false;
        return true;
    }

    bool Committed = false;
    if (NewCategory == Frontier::ParametricSketchCornerCategory::Fillet)
        Committed = Frontier::FilletShapeCorner(Store, Modal.LastShapeId, Modal.LastCornerIndex, NewMagnitude);
    else
        Committed = Frontier::ChamferShapeCorner(Store, Modal.LastShapeId, Modal.LastCornerIndex, NewMagnitude);

    if (Committed)
    {
        Modal.LastMagnitude = NewMagnitude;
        Modal.LastCategory  = NewCategory;
    }
    return Committed;
}

void ClearSketchFilletModalHistory(SketchModelFilletModal& Modal)
{
    Modal.LastCommitValid = false;
    Modal.LastShapeId     = 0;
    Modal.LastCornerIndex = -1;
    Modal.LastMagnitude   = 0.0f;
    Modal.LastCategory    = Frontier::ParametricSketchCornerCategory::Fillet;
    Modal.LastSafeLimit   = 0.0f;
}

}   // namespace SketchModelViewportValidation
