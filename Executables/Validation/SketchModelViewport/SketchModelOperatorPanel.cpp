/*==============================================================================================================================================
                                                        SKETCHMODELOPERATORPANEL.CPP
==============================================================================================================================================*/
// 🧩 The Blender-style operator/redo box body. Pinned top-left of the canvas, it mirrors ControlsGallery's card language exactly (a Frontier
//    PropertyCard wrapping a ScalarEntry + a SelectionEntry) so it reads as one of the app's own controls. Its two rows edit the LAST committed
//    Fillet/Chamfer: Amount (mm) and the Fillet↔Chamfer type. Any change re-commits through ReapplySketchFilletModalCommit on the modal's retained
//    (shape, corner) — which overwrites the corner's stored fillet record in place, so a live Amount drag never stacks arcs. Draws nothing while no
//    commit is retained.

#include "SketchModelOperatorPanel.h"

#include "SketchModelFilletModal.h"
#include "SketchModelInsetModal.h"

#include "Operations/Fillet/ParametricSketchFillet.h"

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "EngineContext/Interface/Components/PropertyPanelBase.h"
#include "EngineContext/Interface/Components/Controls/ScalarEntry.h"
#include "EngineContext/Interface/Components/Controls/Dropdown.h"

#include <cmath>

namespace SketchModelViewportValidation
{

namespace
{
    // 📝 One box's live edit state, keyed to a retained commit so the controls seed from the committed values ONCE per new commit and then track the
    //    user's drags. Held function-local static (single box, single modal) — the box is a singleton in this validation app. SeededShapeId /
    //    SeededCorner detect a FRESH commit (a different corner just committed): when they differ from the modal's retained corner, the box re-seeds
    //    Amount / TypeIndex from the retained values instead of keeping the previous corner's edits.
    struct OperatorBoxState
    {
        bool     Expanded    = true;    // [-]  - the card fold (caller-owned so it survives frames)
        uint32_t SeededShapeId = 0;     // [-]  - the retained shape the live Amount / TypeIndex were seeded from
        int      SeededCorner  = -1;    // [-]  - the retained corner they were seeded from (a change re-seeds)
        float    Amount        = 0.0f;  // [mm] - the live Amount the ScalarEntry edits
        int      TypeIndex     = 0;     // [-]  - 0 = Fillet, 1 = Chamfer (the SelectionEntry index)
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructSketchModelOperatorPanel(const Frontier::ThemeConfiguration&   Theme,
                                       SketchModelFilletModal&               Modal,
                                       Frontier::ParametricSketchShapeStore& Store,
                                       ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    if (!Modal.LastCommitValid || CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return false;

    static OperatorBoxState Box;

    // 📝 A FRESH commit (a different corner than the box was last seeded from) re-seeds the live Amount / Type from the retained values, so the box
    //    always opens showing what was just committed rather than a stale previous edit.
    if (Box.SeededShapeId != Modal.LastShapeId || Box.SeededCorner != Modal.LastCornerIndex)
    {
        Box.SeededShapeId = Modal.LastShapeId;
        Box.SeededCorner  = Modal.LastCornerIndex;
        Box.Amount        = Modal.LastMagnitude;
        Box.TypeIndex     = (Modal.LastCategory == Frontier::ParametricSketchCornerCategory::Chamfer) ? 1 : 0;
    }

    // -- Pin the box near the canvas top-left, a small inset in from the corner. Fixed width; height auto-fits the card. --
    const float Inset = 12.0f;
    const float Width = 268.0f;
    ImGui::SetNextWindowPos(ImVec2(CanvasOrigin.x + Inset, CanvasOrigin.y + Inset), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(Width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);

    // 📝 The desk-black window background so the card's rounded --panel-2 fill reads over it exactly as in ControlsGallery (whose panel body is the
    //    desk background so the card corners show). No title bar / resize / move — this is a pinned HUD box, not a draggable window.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));

    bool Changed = false;
    const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##sketch-model-operator-box", nullptr, Flags))
    {
        const char* const Title = (Box.TypeIndex == 1) ? "Chamfer Corner" : "Fillet Corner";
        if (Frontier::BeginPropertyCard(Theme, Title, &Box.Expanded))
        {
            // -- Amount: the radius (fillet) / setback distance (chamfer) in mm. Clamped to the corner's safe limit (0..limit) so the drag can't
            //    exceed what the legs allow — the same ceiling the drag modal enforced. Step scales with the limit for a comfortable drag. --
            const float Ceiling = (Modal.LastSafeLimit > 0.0f) ? Modal.LastSafeLimit : 1000.0f;
            Frontier::ScalarEntryDescriptor Amount = {};
            Amount.Label   = "Amount";
            Amount.Value   = &Box.Amount;
            Amount.Step    = (Ceiling > 0.0f) ? (Ceiling * 0.01f) : 0.1f;
            Amount.Minimum = 0.0f;
            Amount.Maximum = Ceiling;
            Amount.Format  = "%.2f";
            Amount.Unit    = "mm";
            Amount.Enabled = true;
            if (Frontier::ConstructScalarEntry(Theme, Amount))
                Changed = true;

            // -- Type: swap Fillet ↔ Chamfer. A combo-popup dropdown exactly like ControlsGallery's Dropdown row (the space-saving sibling of the
            //    segmented pill), so the box reads as one of the app's own control cards. --
            static const char* const TypeOptions[] = { "Fillet", "Chamfer" };
            Frontier::DropdownDescriptor Type = {};
            Type.Label         = "Type";
            Type.SelectedIndex = &Box.TypeIndex;
            Type.Options       = TypeOptions;
            Type.OptionCount   = 2;
            Type.Enabled       = true;
            if (Frontier::ConstructDropdown(Theme, Type))
                Changed = true;

            Frontier::EndPropertyCard(Theme);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();     // WindowPadding
    ImGui::PopStyleColor();   // WindowBg

    // -- Re-apply on any edit: overwrite the retained corner's fillet record at the new amount / type. Idempotent, so a live drag never stacks. --
    if (Changed)
    {
        const Frontier::ParametricSketchCornerCategory NewCategory =
            (Box.TypeIndex == 1) ? Frontier::ParametricSketchCornerCategory::Chamfer
                                 : Frontier::ParametricSketchCornerCategory::Fillet;
        ReapplySketchFilletModalCommit(Modal, Store, Box.Amount, NewCategory);
    }

    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   OFFSET OPERATOR BOX
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The offset box's live edit state — the Inset-modal twin of OperatorBoxState. Keyed to the retained commit's FIRST source id + its appended
    //    count so a FRESH offset commit (a different selection) re-seeds Distance / CornerIndex from the retained values instead of keeping the prior
    //    edit. Held function-local static (single box, single modal) exactly like the fillet box.
    struct InsetOperatorBoxState
    {
        bool     Expanded     = true;    // [-]  - the card fold (survives frames)
        uint32_t SeededSource = 0;       // [-]  - the retained first source the live Distance / CornerIndex were seeded from
        int      SeededCount  = -1;      // [-]  - the retained source count they were seeded from (a change re-seeds)
        float    Distance     = 0.0f;    // [mm] - the live signed Distance the ScalarEntry edits (negative = inward)
        int      CornerIndex  = 0;       // [-]  - 0 = Round, 1 = Miter, 2 = Bevel (the Dropdown index)
    };

    // 📝 Map the Corners dropdown index ⇄ the offset core's SketchOffsetCornerStyle (Round = 0 / Miter = 1 / Bevel = 2, matching the enum's order).
    Frontier::SketchOffsetCornerStyle CornerStyleFromIndex(int Index)
    {
        switch (Index)
        {
            case 1:  return Frontier::SketchOffsetCornerStyle::Miter;
            case 2:  return Frontier::SketchOffsetCornerStyle::Bevel;
            default: return Frontier::SketchOffsetCornerStyle::Round;
        }
    }
    int IndexFromCornerStyle(Frontier::SketchOffsetCornerStyle Style)
    {
        switch (Style)
        {
            case Frontier::SketchOffsetCornerStyle::Miter: return 1;
            case Frontier::SketchOffsetCornerStyle::Bevel: return 2;
            default:                                       return 0;
        }
    }
}

bool ConstructSketchModelInsetOperatorPanel(const Frontier::ThemeConfiguration&   Theme,
                                            SketchModelInsetModal&                Modal,
                                            Frontier::ParametricSketchShapeStore& Store,
                                            ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                                            float StackBelow)
{
    if (!Modal.LastCommitValid || CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return false;

    static InsetOperatorBoxState Box;

    // 📝 A FRESH commit (a different source set than the box was last seeded from) re-seeds the live Distance / Corners from the retained values, so
    //    the box always opens showing what was just committed rather than a stale previous edit. Key on the first retained source + the source count.
    const uint32_t FirstSource = Modal.LastSources.empty() ? 0u : Modal.LastSources.front();
    const int      SourceCount = (int)Modal.LastSources.size();
    if (Box.SeededSource != FirstSource || Box.SeededCount != SourceCount)
    {
        Box.SeededSource = FirstSource;
        Box.SeededCount  = SourceCount;
        Box.Distance     = Modal.LastMagnitude;
        Box.CornerIndex  = IndexFromCornerStyle(Modal.LastCornerStyle);
    }

    // -- Pin near the canvas top-left, shifted down by StackBelow so it sits UNDER the Fillet box when both are retained (both pin top-left). --
    const float Inset = 12.0f;
    const float Width = 268.0f;
    ImGui::SetNextWindowPos(ImVec2(CanvasOrigin.x + Inset, CanvasOrigin.y + Inset + StackBelow), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(Width, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.96f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));

    bool Changed = false;
    const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##sketch-model-inset-operator-box", nullptr, Flags))
    {
        if (Frontier::BeginPropertyCard(Theme, "Offset", &Box.Expanded))
        {
            // -- Distance: the SIGNED offset in mm (positive inflates / outward, negative deflates / inward), so the entry spans a symmetric range.
            //    A comfortable step; the range is generous (the offset core clips a too-large inward distance to nothing on its own). --
            Frontier::ScalarEntryDescriptor Distance = {};
            Distance.Label   = "Distance";
            Distance.Value   = &Box.Distance;
            Distance.Step    = 0.1f;
            Distance.Minimum = -1000.0f;
            Distance.Maximum = 1000.0f;
            Distance.Format  = "%.2f";
            Distance.Unit    = "mm";
            Distance.Enabled = true;
            if (Frontier::ConstructScalarEntry(Theme, Distance))
                Changed = true;

            // -- Corners: how the offset builds its corners — Round (arc) / Miter (extended) / Bevel (cut). A combo dropdown, the Corners twin of the
            //    fillet box's Type row. --
            static const char* const CornerOptions[] = { "Round", "Miter", "Bevel" };
            Frontier::DropdownDescriptor Corners = {};
            Corners.Label         = "Corners";
            Corners.SelectedIndex = &Box.CornerIndex;
            Corners.Options       = CornerOptions;
            Corners.OptionCount   = 3;
            Corners.Enabled       = true;
            if (Frontier::ConstructDropdown(Theme, Corners))
                Changed = true;

            Frontier::EndPropertyCard(Theme);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();     // WindowPadding
    ImGui::PopStyleColor();   // WindowBg

    // -- Re-apply on any edit: detach the prior offset Profiles + re-append at the new distance / corner style. Idempotent (the re-apply removes its
    //    own prior output first), so a live Distance drag never stacks a fresh ring each frame. --
    if (Changed)
        ReapplySketchInsetModalCommit(Modal, Store, Box.Distance, CornerStyleFromIndex(Box.CornerIndex));

    return Changed;
}

}   // namespace SketchModelViewportValidation
