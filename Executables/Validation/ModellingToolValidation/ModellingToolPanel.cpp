/*==============================================================================================================================================
                                                        MODELLINGTOOLPANEL.CPP
==============================================================================================================================================*/
// 🧩 Selection authoring plus the live modelling-tool card. The catalogue this drives is NOT here: ModellingCatalogue.cpp is generated from
//    Documentation/Prototypes/ModellingToolMenu.html's own JS declarations, so the tables are provably the prototype's data. What lives here is
//    only what a real editor would otherwise supply from a viewport — which stratum is active and how many components are picked.
//    🔴 A stratum row rather than a modelled cage: every band can be driven to its populated, gated and empty states in one click, where a real
//       selection would need a purpose-built mesh per case. The selection is the fake; the card is the thing under review.

#include <cstdio>

#include "ModellingToolPanel.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The first band that has anything to show at this stratum. Opening the card on a band whose every tile is Absent would
    //    present an empty grid on a selection that has plenty of applicable tools further down, so the open band is RESOLVED
    //    against the stratum rather than remembered across a selection change.
    //    A single-shot band (no tile table) is skipped: it fires rather than populating a grid, so it cannot be the open band.
    int ResolveFirstPopulatedBand(const ToolBandDescriptor* Bands, int BandCount, unsigned int StratumBit)
    {
        for (int BandIndex = 0; BandIndex < BandCount; ++BandIndex)
        {
            if (Bands[BandIndex].Tiles == nullptr) { continue; }
            if (TallyApplicableTiles(Bands[BandIndex], StratumBit) > 0) { return BandIndex; }
        }
        return -1;
    }


    // 📝 Re-author the whole selection profile. Everything downstream of the stratum is reset rather than carried: an open action
    //    belongs to a cluster that may not survive the new stratum, and a half-travelled carousel would slide a pane that is about
    //    to be repopulated. The parameter block's identity is cleared so the next open reseeds from the descriptor.
    //    🔴 The stratum is written to the PANEL's own state, not into the console's focus: the console no longer holds a selection.
    //       It reaches the stratum only through the bridge context the resolver reads, which is refreshed once per frame below.
    void ApplyStratum(ModellingToolState& State, unsigned int StratumBit)
    {
        int BandCount = 0;
        const ToolBandDescriptor* const Bands = ResolveModellingBands(BandCount);

        State.StratumBit = StratumBit;

        // Cluster index is band index: the bridge emits exactly one cluster per authored band, in order.
        State.Focus.OpenCluster = ResolveFirstPopulatedBand(Bands, BandCount, StratumBit);
        State.Focus.OpenAction  = -1;

        State.Carousel.ShowingOptions = false;
        State.Carousel.Travel         = 0.0f;

        State.Parameters.Cluster = -1;   // [idx]- forces a reseed the next time an action opens
        State.Parameters.Action  = -1;
    }


    // 📝 How many bands and tools survive a stratum, for the authoring column's readout. Stated because the interesting property
    //    of the filter is not that it runs but WHAT it leaves: a reviewer comparing against the prototype needs the figures.
    void TallyStratumReach(unsigned int StratumBit, int& BandsShown, int& ToolsLive, int& ToolsGated)
    {
        int BandCount = 0;
        const ToolBandDescriptor* const Bands = ResolveModellingBands(BandCount);

        BandsShown = 0;
        ToolsLive  = 0;
        ToolsGated = 0;

        for (int BandIndex = 0; BandIndex < BandCount; ++BandIndex)
        {
            const int Applicable = TallyApplicableTiles(Bands[BandIndex], StratumBit);
            const int Live       = TallyLiveTiles(Bands[BandIndex], StratumBit);

            if (Applicable > 0 || Bands[BandIndex].Tiles == nullptr) { ++BandsShown; }

            ToolsLive  += Live;
            ToolsGated += (Applicable - Live);
        }
    }


    //---------------------------------------------------- AUTHORING COLUMN ----------------------------------------------------

    // Draw the stratum switch and the commit readout. This is validation scaffolding, not a ported surface — it uses plain ImGui
    // widgets on purpose, so nothing about its appearance can be mistaken for part of the card under review.
    void InscribeAuthoringColumn(ModellingToolState& State)
    {
        ImGui::TextUnformatted("SELECTION STRATUM");
        ImGui::Separator();
        ImGui::Spacing();

        for (int StratumIndex = 0; StratumIndex < ModellingStratumCount; ++StratumIndex)
        {
            const unsigned int StratumBit = ModellingStrata[StratumIndex];
            const bool         Active     = (State.StratumBit == StratumBit);

            char Caption[64] = {};
            std::snprintf(Caption, sizeof(Caption), "%s (%d)",
                          DescribeModellingStratum(StratumBit),
                          ResolveModellingSelectedCount(StratumBit));

            if (Active) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
            if (ImGui::Button(Caption, ImVec2(-1.0f, 0.0f))) { ApplyStratum(State, StratumBit); }
            if (Active) { ImGui::PopStyleColor(); }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        int BandsShown = 0;
        int ToolsLive  = 0;
        int ToolsGated = 0;
        TallyStratumReach(State.StratumBit, BandsShown, ToolsLive, ToolsGated);

        ImGui::Text("bands  %d", BandsShown);
        ImGui::Text("live   %d", ToolsLive);
        ImGui::Text("gated  %d", ToolsGated);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("CARD");
        ImGui::Checkbox("open", &State.CardOpen);
        ImGui::TextUnformatted(State.Carousel.ShowingOptions ? "slide  options" : "slide  tools");
        ImGui::Text("travel %.2f", State.Carousel.Travel);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("LAST COMMIT");
        if (State.CommitTally > 0)
        {
            ImGui::Text("applied %d", State.CommitTally);
            ImGui::Text("%s", State.LastCommittedLabel);
            ImGui::TextDisabled("%s", State.LastCommittedBand);
        }
        else
        {
            ImGui::TextDisabled("nothing applied yet");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("right-click the field to re-anchor");
    }


    // 📝 Latch what was committed, so a click is observable in an app that has nothing to mutate. The band is recorded alongside
    //    the tool because several bands carry a tool of the same name (Circle appears under both Primitive and Curve).
    //    🔴 The console reports a CLUSTER and an ACTION, which read back into the authored catalogue as band and tile because the
    //       bridge emits one cluster per band in order and one action per tile in order. A single-shot band's lone action is the
    //       band itself and has no tile, so an action index at or past TileCount falls back to the band caption.
    void RecordCommit(ModellingToolState& State, int BandIndex, int TileIndex)
    {
        int BandCount = 0;
        const ToolBandDescriptor* const Bands = ResolveModellingBands(BandCount);
        if (BandIndex < 0 || BandIndex >= BandCount) { return; }

        const ToolBandDescriptor& Band = Bands[BandIndex];

        const char* Label = Band.Caption;
        if (TileIndex >= 0 && TileIndex < Band.TileCount && Band.Tiles != nullptr)
        {
            Label = Band.Tiles[TileIndex].Label;
        }

        std::snprintf(State.LastCommittedLabel, sizeof(State.LastCommittedLabel), "%s", (Label != nullptr) ? Label : "");
        std::snprintf(State.LastCommittedBand,  sizeof(State.LastCommittedBand),  "%s", (Band.Caption != nullptr) ? Band.Caption : "");
        ++State.CommitTally;
    }

}   // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeModellingToolSample(ModellingToolState& State)
{
    // The prototype opens on Face with its authored selection, which is the stratum that populates the most bands.
    ApplyStratum(State, ToolStratumFace);

    State.CardOpen          = true;
    State.CardAnchorX       = 0.0f;
    State.CardAnchorY       = 0.0f;
    State.ReanchorRequested = false;
    State.Carousel.OpenAge  = 0.0f;
    State.CommitTally       = 0;
}


void ConstructModellingToolPanel(const ThemeConfiguration& Theme, ModellingToolState& State, const SvgIconRegistry* Icons)
{
    const MetricsSpecification Metrics = ResolveConsoleMetrics(Theme);

    // 📝 The authoring column is sized to its own content; the field takes the rest. Fixed rather than a splitter because the
    //    card has a FIXED size and the point of the field is to show it at that size with room around it.
    const float Scale           = (Theme.Metrics.UiScale > 0.0f) ? Theme.Metrics.UiScale : 1.0f;
    const float AuthoringWidth  = 210.0f * Scale;

    ImGui::BeginChild("ModellingAuthoring", ImVec2(AuthoringWidth, 0.0f), ImGuiChildFlags_Borders);
    InscribeAuthoringColumn(State);
    ImGui::EndChild();

    ImGui::SameLine();

    // 📝 The field is only the backdrop and the right-click surface. The console itself is drawn AFTER this child closes:
    //    🔴 ConstructWorkspaceContextConsole opens its own top-level ImGui window, and a Begin() nested inside an active
    //       BeginChild() is invalid — the console is silently never emitted. So the field records where the console goes,
    //       and the console is built at panel scope where a top-level window is legal.
    ImGui::BeginChild("ModellingField", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        const ImVec2 FieldMin  = ImGui::GetCursorScreenPos();
        const ImVec2 FieldSize = ImGui::GetContentRegionAvail();

        // First frame, and any frame the anchor has not been placed: centre the card in the field.
        if (State.CardAnchorX <= 0.0f && State.CardAnchorY <= 0.0f)
        {
            State.CardAnchorX = FieldMin.x + (FieldSize.x - Metrics.CardWidth)  * 0.5f;
            State.CardAnchorY = FieldMin.y + (FieldSize.y - Metrics.CardHeight) * 0.5f;
        }

        // 📝 A right-click on the field asks for the card THERE. Recorded rather than acted on: the same click is an outside
        //    press to an already-open card, and acting here would let the card's own dismissal close what the click just opened.
        //    An invisible button rather than IsWindowHovered so the field's own hit area is explicit and excludes the card.
        ImGui::InvisibleButton("ModellingFieldSurface", FieldSize, ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemActivated())
        {
            State.ReanchorRequested = true;
        }
    }
    ImGui::EndChild();

    State.Carousel.OpenAge += ImGui::GetIO().DeltaTime;
    AdvanceConsoleCarousel(State.Carousel, ImGui::GetIO().DeltaTime, Metrics);

    if (State.CardOpen)
    {
        // Refresh the resolver's live context to THIS frame's stratum, then compose and draw. Bound every frame rather than on the
        // stratum switch: the context also carries the converted probe, and rebinding is the one place both are guaranteed in step.
        BindModellingConsoleContext(State.Bridge, State.StratumBit);
        const WorkspaceContextConsoleDescriptor Descriptor = ComposeModellingConsoleDescriptor(State.Bridge);

        const ConsoleResult Result = ConstructWorkspaceContextConsole(Icons,
                                                                     Descriptor,
                                                                     ImVec2(State.CardAnchorX, State.CardAnchorY),
                                                                     State.Focus,
                                                                     State.Carousel,
                                                                     State.Parameters,
                                                                     Theme);

        if (Result.CommitRequested || Result.ActivatedCluster >= 0)
        {
            RecordCommit(State, Result.ActivatedCluster, Result.ActivatedAction);
        }

        if (Result.DismissRequested)
        {
            State.CardOpen = false;
        }
    }

    // The deferred re-anchor, applied after the card has had its say. Re-opening replays the open-pop from the top, because
    // the card appearing at a new place is a new opening — carrying the age would place it already settled.
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

}   // namespace Frontier
