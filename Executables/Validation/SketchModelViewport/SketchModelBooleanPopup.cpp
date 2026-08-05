/*==============================================================================================================================================
                                                        SKETCHMODELBOOLEANPOPUP.CPP
==============================================================================================================================================*/
// 📝 The boolean popup's reconcile / record / preview, over the ported Frontier boolean core. See the header for the surface's contract.

#include "SketchModelBooleanPopup.h"

#include "ParametricSketchShapeStore.h"

#include "EngineContext/Interface/Components/PropertyPanelBase.h"
#include "EngineContext/Interface/Components/Controls/BooleanEntry.h"
#include "EngineContext/Interface/Components/Controls/Dropdown.h"
#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

#include <algorithm>
#include <cstdio>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Twice the signed area of a loop (the shoelace sum). Positive = CCW (an outer boundary), negative = CW (a punched hole) — the same
    //    convention the boolean core emits, so the preview can classify a solved loop without reaching into that file's statics.
    float LoopTwiceSignedArea(const std::vector<ImVec2>& Loop)
    {
        if (Loop.size() < 3)
            return 0.0f;
        float Sum = 0.0f;
        for (size_t Index = 0; Index < Loop.size(); ++Index)
        {
            const ImVec2& A = Loop[Index];
            const ImVec2& B = Loop[(Index + 1) % Loop.size()];
            Sum += (A.x * B.y) - (B.x * A.y);
        }
        return Sum;
    }

    // Whether two operand lists hold the SAME shapes regardless of order — the dismissal test. Re-picking the same pair in the opposite order
    //    is the same decision the user already declined, so an order-sensitive compare would let the popup spring back on a mere re-click.
    bool SameOperandSet(const std::vector<uint32_t>& Left, const std::vector<uint32_t>& Right)
    {
        if (Left.size() != Right.size())
            return false;
        std::vector<uint32_t> A = Left, B = Right;
        std::sort(A.begin(), A.end());
        std::sort(B.begin(), B.end());
        return A == B;
    }

    // Every selected shape resolves AND is closed. A boolean over an open run can only fail, so the popup stays silent rather than arming to
    //    offer an operation that would raise "Boolean needs closed shapes" the moment it ran.
    bool SelectionIsAllClosed(Frontier::ParametricSketchShapeStore& Store, const std::vector<uint32_t>& Identifiers)
    {
        for (uint32_t Identifier : Identifiers)
        {
            const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, Identifier);
            if (Shape == nullptr || !Shape->ClosedEnabled)
                return false;
        }
        return !Identifiers.empty();
    }

    int IndexFromOperation(Frontier::BooleanCategory Op)
    {
        switch (Op)
        {
            case Frontier::BooleanCategory::Subtract:  return 1;
            case Frontier::BooleanCategory::Intersect: return 2;
            default:                                   return 0;
        }
    }

    Frontier::BooleanCategory OperationFromIndex(int Index)
    {
        switch (Index)
        {
            case 1:  return Frontier::BooleanCategory::Subtract;
            case 2:  return Frontier::BooleanCategory::Intersect;
            default: return Frontier::BooleanCategory::Union;
        }
    }

    const char* OperationLabel(Frontier::BooleanCategory Op)
    {
        switch (Op)
        {
            case Frontier::BooleanCategory::Subtract:  return "Subtract";
            case Frontier::BooleanCategory::Intersect: return "Intersect";
            default:                                   return "Union";
        }
    }

    // The operand list rotated so the chosen base leads — the order AppendBooleanResult reads (front = subject, the rest are clips). Rotating
    //    rather than swapping preserves the relative order of the remaining tools, so a 3-operand subtract carves in a predictable sequence.
    std::vector<uint32_t> ResolveOrderedOperands(const SketchModelBooleanPopup& Popup)
    {
        std::vector<uint32_t> Ordered = Popup.Operands;
        if (Ordered.size() < 2)
            return Ordered;
        const int Index = std::clamp(Popup.BaseIndex, 0, (int)Ordered.size() - 1);
        std::rotate(Ordered.begin(), Ordered.begin() + Index, Ordered.end());
        return Ordered;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ReconcileSketchModelBooleanPopup(SketchModelBooleanPopup&              Popup,
                                      Frontier::ParametricSketchShapeStore& Store,
                                      ImVec2                                CursorPixel)
{
    const std::vector<uint32_t>& Live = Store.SelectionSet;

    // -- Below two operands there is nothing to boolean: close the card and forget the dismissal, so re-picking a pair arms it fresh. --
    if (Live.size() < 2 || !SelectionIsAllClosed(Store, Live))
    {
        Popup.Open = false;
        Popup.Operands.clear();
        if (Live.size() < 2)
            Popup.DismissedOperands.clear();
        return;
    }

    // -- The user declined THIS exact operand set; stay shut until the selection genuinely changes. --
    if (SameOperandSet(Live, Popup.DismissedOperands))
    {
        Popup.Open = false;
        return;
    }

    // -- A different operand set than the one on show: re-arm. Seeding the anchor only here is what keeps the card still instead of chasing
    //    the pointer, and resetting BaseIndex avoids carrying a stale base index into a shorter selection (an out-of-range base). --
    if (!Popup.Open || !SameOperandSet(Live, Popup.Operands))
    {
        Popup.Operands          = Live;
        Popup.BaseIndex         = 0;
        Popup.AnchorPixel       = CursorPixel;
        Popup.Open              = true;
        Popup.DismissedOperands.clear();
    }
    else
    {
        Popup.Operands = Live;   // same set, possibly re-ordered by the user's clicks: track it without disturbing the card
        Popup.BaseIndex = std::clamp(Popup.BaseIndex, 0, (int)Popup.Operands.size() - 1);
    }
}

bool ConstructSketchModelBooleanPopup(const Frontier::ThemeConfiguration&   Theme,
                                      SketchModelBooleanPopup&              Popup,
                                      Frontier::ParametricSketchShapeStore& Store,
                                      bool&                                 OutOwnsPress)
{
    OutOwnsPress = false;
    if (!Popup.Open || Popup.Operands.size() < 2)
        return false;

    // -- Pin beside the arm-time cursor, nudged clear of it so the card never sits under the pointer that summoned it. --
    const float Width = 244.0f;
    ImGui::SetNextWindowPos(ImVec2(Popup.AnchorPixel.x + 18.0f, Popup.AnchorPixel.y + 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(Width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));

    bool Committed = false;
    bool Dismissed = false;

    const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##sketch-model-boolean-popup", nullptr, Flags))
    {
        // 🔴 CLAIM THE PRESS while the pointer is over the card or one of its widgets is active. The card floats inside the canvas rect, so without
        //    this the canvas pick reads a click here as an empty canvas click and CLEARS the selection — destroying the operands mid-decision. The
        //    hovered test covers the click that lands on the card; the active-item test keeps a held dropdown / drag owning the press as it moves.
        OutOwnsPress = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                              ImGuiHoveredFlags_ChildWindows) ||
                       (ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows));

        static bool Expanded = true;
        if (Frontier::BeginPropertyCard(Theme, "Boolean", &Expanded))
        {
            // -- Operation: which region op Apply commits. --
            static const char* const OperationOptions[] = { "Union", "Subtract", "Intersect" };
            int OperationIndex = IndexFromOperation(Popup.Operation);

            Frontier::DropdownDescriptor Operation = {};
            Operation.Label         = "Operation";
            Operation.SelectedIndex = &OperationIndex;
            Operation.Options       = OperationOptions;
            Operation.OptionCount   = 3;
            Operation.Enabled       = true;
            if (Frontier::ConstructDropdown(Theme, Operation))
                Popup.Operation = OperationFromIndex(OperationIndex);

            // -- Base: which operand is the subject. Only Subtract is directed, so the row is disabled (but still shown, so the layout does not
            //    jump) for Union / Intersect, where the operand order cannot change the result. --
            const size_t OperandCount = std::min<size_t>(Popup.Operands.size(), 8);
            char        BaseTitles[8][40] = {};
            const char* BaseOptions[8]    = {};
            for (size_t Index = 0; Index < OperandCount; ++Index)
            {
                const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, Popup.Operands[Index]);
                std::snprintf(BaseTitles[Index], sizeof(BaseTitles[Index]), "%s",
                              (Shape != nullptr && Shape->Title[0] != '\0') ? Shape->Title : "Shape");
                BaseOptions[Index] = BaseTitles[Index];
            }

            Popup.BaseIndex = std::clamp(Popup.BaseIndex, 0, (int)OperandCount - 1);
            Frontier::DropdownDescriptor Base = {};
            Base.Label         = "Base";
            Base.SelectedIndex = &Popup.BaseIndex;
            Base.Options       = BaseOptions;
            Base.OptionCount   = (int)OperandCount;
            Base.Enabled       = (Popup.Operation == Frontier::BooleanCategory::Subtract);
            Frontier::ConstructDropdown(Theme, Base);

            // -- Keep operands: off hides them after the commit (recoverable via the outliner), on leaves them displayed. --
            Frontier::BooleanEntryDescriptor Keep = {};
            Keep.Label   = "Keep operands";
            Keep.Value   = &Popup.KeepOperandsEnabled;
            Keep.Enabled = true;
            Frontier::ConstructBooleanEntry(Theme, Keep);

            Frontier::EndPropertyCard(Theme);
        }

        // -- Apply / Cancel. Apply re-seats the store's SelectionSet in base-first order (the operand list AppendBooleanResult reads) and commits;
        //    the outcome decides whether this counts as a commit, since the core leaves the store untouched on every failure path. --
        const float ButtonWidth = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
        if (ImGui::Button("Apply", ImVec2(ButtonWidth, 0.0f)))
        {
            const std::vector<uint32_t> Ordered = ResolveOrderedOperands(Popup);
            Store.SelectionSet = Ordered;
            Store.Selected     = Ordered.empty() ? 0u : Ordered.back();

            const Frontier::BooleanOutcome Outcome =
                Frontier::AppendBooleanResult(Store, Popup.Operation, Popup.KeepOperandsEnabled);

            if (Outcome == Frontier::BooleanOutcome::Committed)
            {
                ++Popup.CommitSerial;
                Popup.LastLabel    = OperationLabel(Popup.Operation);
                Popup.LastOperands = (int)Ordered.size();
                Committed          = true;

                // The commit consumed the selection (the core clears it and reselects its result), so the card has nothing left to act on.
                Popup.Open = false;
                Popup.Operands.clear();
                Popup.DismissedOperands.clear();
            }
            // On a failure the core already raised the notice and changed nothing; the card stays up so the settings can be adjusted.
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("Cancel", ImVec2(ButtonWidth, 0.0f)))
            Dismissed = true;
    }
    ImGui::End();

    ImGui::PopStyleVar();     // WindowPadding
    ImGui::PopStyleColor();   // WindowBg

    if (Dismissed)
        DismissSketchModelBooleanPopup(Popup);

    return Committed;
}

std::vector<SketchBooleanPreviewLoop> ResolveSketchModelBooleanPreview(const SketchModelBooleanPopup&        Popup,
                                                                      Frontier::ParametricSketchShapeStore& Store)
{
    std::vector<SketchBooleanPreviewLoop> Preview;
    if (!Popup.Open || Popup.Operands.size() < 2)
        return Preview;

    const std::vector<uint32_t>            Ordered = ResolveOrderedOperands(Popup);
    const std::vector<std::vector<ImVec2>> Solved  = Frontier::ResolveBooleanPreviewLoops(Store, Ordered, Popup.Operation);

    Preview.reserve(Solved.size());
    for (const std::vector<ImVec2>& Loop : Solved)
    {
        if (Loop.size() < 3)
            continue;
        SketchBooleanPreviewLoop Entry;
        Entry.Points = Loop;
        Entry.Hole   = (LoopTwiceSignedArea(Loop) < 0.0f);   // CW from the solve = a punched interior
        Preview.push_back(std::move(Entry));
    }
    return Preview;
}

bool DismissSketchModelBooleanPopup(SketchModelBooleanPopup& Popup)
{
    if (!Popup.Open)
        return false;

    // Remember WHAT was declined (not merely that something was) so the very same selection stays quiet while any other pair re-arms at once.
    Popup.DismissedOperands = Popup.Operands;
    Popup.Open              = false;
    return true;
}

}   // namespace SketchModelViewportValidation
