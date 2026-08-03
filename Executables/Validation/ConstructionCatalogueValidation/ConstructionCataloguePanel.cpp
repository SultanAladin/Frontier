/*==============================================================================================================================================
                                                    CONSTRUCTIONCATALOGUEPANEL.CPP
==============================================================================================================================================*/
// 🧩 The construction catalogue's validation surface, now driven by the SHARED WorkspaceContextConsole rather than a target-owned card shell. The
//    console draws the rail + gate-driven action grid + options; this panel only (1) holds the console's cross-frame state, (2) owns the live
//    ConstructionDocument the gate reads, (3) exposes that document through the plain-ImGui scaffolding column's editor, and (4) folds a commit back.
//    🔴 The 5-state gate is NOT drawn here and NOT re-implemented — it reaches the console through ConstructionConsoleBridge's VerdictResolver, which
//    collapses GateResult into the console's ActionVerdict. This file names no rail, grid, tile or carousel drawing: that is the console's, shared
//    across all three workspaces. Scope is "gate lift only": the document-state editor moved to the scaffolding column, and the result-banner +
//    workplane fix-chip overlays are dropped (the gate's verdict shows through the console's own greyed/badged tiles instead).

#include "ConstructionCataloguePanel.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace ConstructionCatalogueValidation
{

using Frontier::ConsoleCarousel;
using Frontier::ConsoleFocus;
using Frontier::ConsoleResult;
using Frontier::ParameterBlock;

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    //------------------------------------------------- STATE PRESETS -------------------------------------------------

    // 📝 One document-state preset: a name, a note, and the field values a single click applies. These are the prototype's PRESETS (the slide-1
    //    convenience rows) held as authored panel scaffolding — they drive the gate, they are not gate data. A value the preset omits keeps whatever
    //    the document already held, exactly as the prototype's Object.assign does.
    struct DocumentPreset
    {
        const char* Name;
        const char* Note;
        ConstructionDocument State;
    };

    ConstructionDocument PresetDocument(ConstructionDimension Dimension, int Selected, int Profiles, int Solids,
                                        bool Workplane, bool Closed, bool Planar, bool Axis, bool Path, bool Uniform,
                                        bool Support, bool Refplane, bool Imagery, bool Measurable, bool Opening)
    {
        ConstructionDocument Document = DefaultConstructionDocument();
        Document.ActiveDimension          = Dimension;
        Document.SelectedCount            = Selected;
        Document.ProfileCount             = Profiles;
        Document.SolidCount               = Solids;
        Document.WorkplaneActivation      = Workplane;
        Document.ClosedProfileCondition   = Closed;
        Document.PlanarProfileCondition   = Planar;
        Document.AxisAvailability         = Axis;
        Document.PathAvailability         = Path;
        Document.UniformClosureCondition  = Uniform;
        Document.SupportMaterialCondition = Support;
        Document.ReferencePlaneCondition  = Refplane;
        Document.SourceImageryCondition   = Imagery;
        Document.MeasurableCondition      = Measurable;
        Document.OpeningCondition         = Opening;
        return Document;
    }

    const DocumentPreset Presets[] =
    {
        { "Empty document", "nothing selected \xC2\xB7 no workplane",
          PresetDocument(ConstructionDimension::Nothing, 0, 0, 0, false, false, true,  false, false, true,  false, false, false, false, false) },
        { "Workplane set", "the 22 sketch operations wake",
          PresetDocument(ConstructionDimension::Nothing, 0, 0, 0, true,  false, true,  false, false, true,  false, true,  false, false, false) },
        { "Open wire", "raising ops stay LIVE, badged \xE2\x86\x92 shell",
          PresetDocument(ConstructionDimension::Wire, 1, 1, 0, true,  false, true,  true,  true,  true,  false, false, false, true,  false) },
        { "Closed wire", "same ops, now \xE2\x86\x92 solid",
          PresetDocument(ConstructionDimension::Wire, 1, 1, 0, true,  true,  true,  true,  true,  true,  false, false, false, true,  false) },
        { "Planar face", "the clean case \xE2\x80\x94 no solid checkbox needed",
          PresetDocument(ConstructionDimension::Face, 1, 1, 0, true,  true,  true,  true,  true,  true,  true,  false, false, true,  false) },
        { "Two profiles", "loft and ruled sweep reach their minimum",
          PresetDocument(ConstructionDimension::Wire, 2, 2, 0, true,  true,  true,  true,  true,  true,  false, false, false, true,  false) },
        { "Open shell", "needs sewing before it can be a solid",
          PresetDocument(ConstructionDimension::Shell, 1, 0, 0, true,  false, true,  false, false, true,  true,  false, false, true,  true) },
        { "One solid", "profile sweep VANISHES \xE2\x80\x94 no successor",
          PresetDocument(ConstructionDimension::Solid, 1, 0, 1, true,  true,  true,  true,  false, true,  true,  true,  false, true,  false) },
        { "Two solids", "the booleans reach their counts",
          PresetDocument(ConstructionDimension::Solid, 2, 0, 2, true,  true,  true,  true,  false, true,  true,  true,  false, true,  false) },
    };
    constexpr int PresetCount = static_cast<int>(sizeof(Presets) / sizeof(Presets[0]));


    //------------------------------------------------- COMMIT + OPEN RESOLUTION -------------------------------------------------

    // 📝 The console reports an activation by (ActivatedCluster, ActivatedAction) — dense cluster + action-within-cluster indices into the tables
    //    the bridge built, which mirror the catalogue's (band, operation). So a commit walks straight back to the ConstructionOperation to latch
    //    what it produced, exactly as the old shell did.
    void RecordCommit(ConstructionCatalogueState& State, int Cluster, int Action)
    {
        int BandCount = 0;
        const ConstructionBand* const Bands = ResolveConstructionBands(BandCount);
        if (Cluster < 0 || Cluster >= BandCount) { return; }
        const ConstructionBand& Band = Bands[Cluster];
        if (Action < 0 || Action >= Band.OperationCount) { return; }
        const ConstructionOperation& Operation = Band.Operations[Action];

        std::snprintf(State.LastCommittedLabel, sizeof(State.LastCommittedLabel), "%s", Operation.Label ? Operation.Label : "");
        ConstructionDimension Result = ConstructionDimension::Nothing;
        if (ResultType(Operation, State.Document, Result))
        {
            std::snprintf(State.LastCommittedResult, sizeof(State.LastCommittedResult), "produced a %s", DescribeDimension(Result));
        }
        else
        {
            std::snprintf(State.LastCommittedResult, sizeof(State.LastCommittedResult), "declared result");
        }
        ++State.CommitTally;
    }


    //------------------------------------------------- THE DOCUMENT-STATE EDITOR (SCAFFOLDING) -------------------------------------------------

    // 📝 The gate's whole input surface, drawn with plain ImGui in the scaffolding column so nothing about it can be mistaken for the console under
    //    review. Every control is a direct edit to a gate input; the console re-evaluates the whole action field from the mutated document next
    //    frame through the bridge's resolver. This is the prototype's renderProbeReadout — dimension chips, the law verdict, count steppers,
    //    condition toggles, presets, the live tally, and the reveal note — relocated out of the card, not re-implemented.
    void InscribeDocumentEditor(ConstructionCatalogueState& State)
    {
        //---- Input dimension: seven chips ----
        ImGui::TextDisabled("INPUT DIMENSION");
        for (int Index = 0; Index < ConstructionDimensionCount; ++Index)
        {
            const ConstructionDimension Dimension = static_cast<ConstructionDimension>(Index);
            const bool Active = State.Document.ActiveDimension == Dimension;
            if (Index % 4 != 0) { ImGui::SameLine(); }
            if (Active) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.34f, 0.52f, 1.0f)); }
            if (ImGui::SmallButton(DescribeDimension(Dimension)))
            {
                State.Document.ActiveDimension = Dimension;
                if (Dimension == ConstructionDimension::Nothing) { State.Document.SelectedCount = 0; }
                else if (State.Document.SelectedCount == 0)       { State.Document.SelectedCount = 1; }
            }
            if (Active) { ImGui::PopStyleColor(); }
        }

        //---- The law's verdict ----
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("DIMENSION-RAISING LAW");
        ConstructionDimension Raised = ConstructionDimension::Nothing;
        const bool HasSuccessor = RaiseDimension(State.Document.ActiveDimension, State.Document.ClosedProfileCondition, Raised);
        ImGui::Text("%s", DescribeDimension(State.Document.ActiveDimension));
        ImGui::SameLine(); ImGui::TextDisabled("--raise-->");
        ImGui::SameLine();
        if (HasSuccessor) { ImGui::TextColored(ImVec4(0.30f, 0.62f, 0.95f, 1.0f), "%s", DescribeDimension(Raised)); }
        else              { ImGui::TextDisabled("no successor"); }
        ImGui::TextDisabled("%s", DescribeLaw(State.Document.ActiveDimension, State.Document.ClosedProfileCondition));

        //---- Counts: five steppers ----
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("COUNTS");
        struct CountRow { const char* Label; int* Field; };
        const CountRow Counts[] =
        {
            { "Selected",       &State.Document.SelectedCount },
            { "Profiles",       &State.Document.ProfileCount },
            { "Solids",         &State.Document.SolidCount },
            { "Boundary edges", &State.Document.BoundaryEdgeCount },
            { "Circles",        &State.Document.ExistingCircleCount },
        };
        for (const CountRow& Count : Counts)
        {
            ImGui::PushID(Count.Field);
            ImGui::TextUnformatted(Count.Label);
            ImGui::SameLine(120.0f);
            if (ImGui::SmallButton("<") && *Count.Field > 0)  { --*Count.Field; }
            ImGui::SameLine(); ImGui::Text("%d", *Count.Field);
            ImGui::SameLine();
            if (ImGui::SmallButton(">") && *Count.Field < 12) { ++*Count.Field; }
            ImGui::PopID();
        }

        //---- Conditions: thirteen toggles ----
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("CONDITIONS");
        struct FlagRow { const char* Label; bool* Field; };
        const FlagRow Flags[] =
        {
            { "Workplane set",     &State.Document.WorkplaneActivation },
            { "Profile closed",    &State.Document.ClosedProfileCondition },
            { "Profile planar",    &State.Document.PlanarProfileCondition },
            { "Axis available",    &State.Document.AxisAvailability },
            { "Path available",    &State.Document.PathAvailability },
            { "Uniform closure",   &State.Document.UniformClosureCondition },
            { "Material to reach", &State.Document.SupportMaterialCondition },
            { "Shared endpoint",   &State.Document.TangentEndpointCondition },
            { "Has an opening",    &State.Document.OpeningCondition },
            { "Reference plane",   &State.Document.ReferencePlaneCondition },
            { "Imported source",   &State.Document.SourceImageryCondition },
            { "Measurable",        &State.Document.MeasurableCondition },
            { "Pending geometry",  &State.Document.PendingGeometryCondition },
        };
        for (const FlagRow& Flag : Flags)
        {
            ImGui::PushID(Flag.Field);
            ImGui::Checkbox(Flag.Label, Flag.Field);
            ImGui::PopID();
        }

        //---- Presets: nine one-click document states ----
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("PRESETS");
        for (int Index = 0; Index < PresetCount; ++Index)
        {
            ImGui::PushID(Index);
            if (ImGui::Selectable(Presets[Index].Name)) { State.Document = Presets[Index].State; }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", Presets[Index].Note); }
            ImGui::PopID();
        }

        //---- The live tally, displayed not asserted ----
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("LIVE TALLY");
        const ConstructionTally Tally = TallyBuckets(State.Document);
        const bool Holds = Tally.Available + Tally.Gated + Tally.Hidden == Tally.Total && Tally.Total == 128;
        ImGui::Text("%d available   %d gated   %d hidden", Tally.Available, Tally.Gated, Tally.Hidden);
        ImGui::TextColored(Holds ? ImVec4(0.30f, 0.72f, 0.48f, 1.0f) : ImVec4(0.88f, 0.38f, 0.25f, 1.0f),
                           "%d + %d + %d = %d %s", Tally.Available, Tally.Gated, Tally.Hidden, Tally.Total,
                           Holds ? "invariant holds" : "INVARIANT BROKEN");
    }


    //------------------------------------------------- THE SCAFFOLDING COLUMN -------------------------------------------------

    // The validation scaffolding: the placement note, the last commit, and the document-state editor. Plain widgets throughout, so nothing about it
    // can be mistaken for the console under review.
    void InscribeScaffoldingColumn(ConstructionCatalogueState& State)
    {
        ImGui::TextUnformatted("CONSTRUCTION CATALOGUE");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("right-click the field to re-anchor");
        ImGui::Spacing();

        ImGui::TextUnformatted("CONSOLE");
        ImGui::Checkbox("open", &State.CardOpen);
        ImGui::TextUnformatted(State.Carousel.ShowingOptions ? "slide  options" : "slide  browse");
        ImGui::Text("travel %.2f", State.Carousel.Travel);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("LAST COMMIT");
        if (State.CommitTally > 0)
        {
            ImGui::Text("applied %d", State.CommitTally);
            ImGui::Text("%s", State.LastCommittedLabel);
            ImGui::TextDisabled("%s", State.LastCommittedResult);
        }
        else
        {
            ImGui::TextDisabled("nothing applied yet");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("DOCUMENT STATE");
        ImGui::TextDisabled("edit to re-evaluate the gate");
        ImGui::Spacing();
        InscribeDocumentEditor(State);
    }
}   // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeConstructionCatalogueSample(ConstructionCatalogueState& State)
{
    State.Document = DefaultConstructionDocument();

    State.Focus.OpenCluster = 0;    // the console holds the first populated cluster; -1 lets it resolve the first non-empty itself
    State.Focus.OpenAction  = -1;

    State.Carousel.ShowingOptions = false;
    State.Carousel.Travel         = 0.0f;
    State.Carousel.OpenAge        = 0.0f;

    State.Parameters.Cluster = -1;
    State.Parameters.Action  = -1;

    State.CardOpen          = true;
    State.CardAnchorX       = 0.0f;
    State.CardAnchorY       = 0.0f;
    State.ReanchorRequested = false;
    State.CommitTally       = 0;
}


void ConstructConstructionCataloguePanel(const Frontier::ThemeConfiguration& Theme,
                                         ConstructionCatalogueState&         State,
                                         const Frontier::SvgIconRegistry*    Icons)
{
    const float Scale         = (Theme.Metrics.UiScale > 0.0f) ? Theme.Metrics.UiScale : 1.0f;
    const float ScaffoldWidth = 240.0f * Scale;

    ImGui::BeginChild("ConstructionScaffolding", ImVec2(ScaffoldWidth, 0.0f), ImGuiChildFlags_Borders);
    InscribeScaffoldingColumn(State);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ConstructionField", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    ImVec2 FieldMin  = ImGui::GetCursorScreenPos();
    ImVec2 FieldSize = ImGui::GetContentRegionAvail();
    {
        if (State.CardAnchorX <= 0.0f && State.CardAnchorY <= 0.0f)
        {
            State.CardAnchorX = FieldMin.x + FieldSize.x * 0.5f - 320.0f;
            State.CardAnchorY = FieldMin.y + FieldSize.y * 0.5f - 220.0f;
        }

        ImGui::InvisibleButton("ConstructionFieldSurface", FieldSize, ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemActivated()) { State.ReanchorRequested = true; }
    }
    ImGui::EndChild();

    // Advance the carousel, then drive the console: bind the resolver's live context to this frame's document, compose the descriptor, and draw.
    Frontier::AdvanceConsoleCarousel(State.Carousel, ImGui::GetIO().DeltaTime,
                                     Frontier::ResolveConsoleMetrics(Theme));

    if (State.CardOpen)
    {
        BindConstructionConsoleContext(State.Bridge, State.Document);
        const Frontier::WorkspaceContextConsoleDescriptor Descriptor = ComposeConstructionConsoleDescriptor(State.Bridge);

        const ConsoleResult Result = Frontier::ConstructWorkspaceContextConsole(
            Icons, Descriptor, ImVec2(State.CardAnchorX, State.CardAnchorY),
            State.Focus, State.Carousel, State.Parameters, Theme);

        if (Result.CommitRequested)
        {
            RecordCommit(State, Result.ActivatedCluster, Result.ActivatedAction);
        }
        if (Result.DismissRequested)
        {
            State.CardOpen = false;
        }
    }

    if (State.ReanchorRequested)
    {
        const ImVec2 Pointer = ImGui::GetIO().MousePos;
        State.CardAnchorX      = Pointer.x;
        State.CardAnchorY      = Pointer.y;
        State.CardOpen         = true;
        State.Carousel.OpenAge = 0.0f;
        State.ReanchorRequested = false;
    }
}

}   // namespace ConstructionCatalogueValidation
