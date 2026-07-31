/*==============================================================================================================================================
                                                         POLYGONACTIONPANEL.CPP
==============================================================================================================================================*/
// 🧩 Selection authoring plus the live context menu. The stratum buttons, the count spinners and the connectivity toggles write straight into one
//    SelectionProfile; the menu then renders whatever FilterOperationCatalogue makes of it. The preset row matters most: each preset drives one gate
//    class to a KNOWN outcome (even vs odd boundary, ring with vs without quad walk, one region vs two), so a reviewer can confirm the interesting
//    behaviour without hand-dialling ten fields.

#include <cstdio>
#include <cstring>

#include "PolygonActionPanel.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 One preset: a whole SelectionProfile that exercises a specific gate. Named for the OUTCOME being demonstrated rather
    //    than the field values, because the value combination is the uninteresting part.
    struct SelectionPreset
    {
        const char*      Label;
        const char*      Expectation;   // What a reviewer should see; shown as a hint beside the preset
        SelectionProfile Profile;
    };

    SelectionProfile MakeProfile(TopologyStratum Stratum, int SelectedCount, int RegionCount, int BoundaryEdgeCount)
    {
        SelectionProfile Profile;
        Profile.ActiveStratum       = Stratum;
        Profile.SelectedCount       = SelectedCount;
        Profile.DistinctRegionCount = RegionCount;
        Profile.BoundaryEdgeCount   = BoundaryEdgeCount;
        return Profile;
    }

    // 📝 The presets. Built by a function rather than a constexpr table so each one reads as a short recipe.
    int ResolveSelectionPresets(SelectionPreset* Presets, int Capacity)
    {
        int Count = 0;
        const auto Push = [&](const char* Label, const char* Expectation, const SelectionProfile& Profile)
        {
            if (Count < Capacity)
            {
                Presets[Count].Label       = Label;
                Presets[Count].Expectation = Expectation;
                Presets[Count].Profile     = Profile;
                ++Count;
            }
        };

        // Two face regions: BridgeSpan's exactly-2 gate passes here and fails in the single-region preset below.
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::Face, 8, 2, 0);
            Profile.SharedComponentCondition = true;
            Profile.ManifoldCondition        = true;
            Profile.QuadPairCondition        = true;
            Push("2 face regions", "Bridge Span live", Profile);
        }
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::Face, 4, 1, 0);
            Profile.SharedComponentCondition = true;
            Push("1 face region", "Bridge Span -> needs 2 faces", Profile);
        }

        // Boundary parity: the case the research flagged, where Blender blames the wrong precondition.
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::Border, 8, 1, 8);
            Profile.BoundaryCondition   = true;
            Profile.ClosedLoopCondition = true;
            Profile.OpenSurfaceCondition = true;
            Push("border, 8 edges (even)", "Grid Fill live", Profile);
        }
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::Border, 7, 1, 7);
            Profile.BoundaryCondition   = true;
            Profile.ClosedLoopCondition = true;
            Profile.OpenSurfaceCondition = true;
            Push("border, 7 edges (odd)", "Grid Fill -> requires an even boundary", Profile);
        }

        // Ring walkability: distinguishes a topology shortfall from a count shortfall on the same row.
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::EdgeRing, 6, 1, 0);
            Profile.QuadRingCondition    = true;
            Profile.TwoFaceEdgeCondition = true;
            Push("edge ring, quad walk", "Insert Edge Loop live", Profile);
        }
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::EdgeRing, 6, 1, 0);
            Profile.QuadRingCondition = false;
            Push("edge ring, tri blocked", "Insert Edge Loop -> requires a quad ring", Profile);
        }

        // A bare vertex: most of the menu should vanish rather than grey, proving the hide-vs-grey split.
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::Vertex, 1, 1, 0);
            Push("single vertex", "face/normal rows absent, not greyed", Profile);
        }
        {
            SelectionProfile Profile = MakeProfile(TopologyStratum::Object, 1, 1, 0);
            Profile.ShellCount           = 2;
            Profile.OpenSurfaceCondition = true;
            Profile.SymmetryAxisCondition = true;
            Push("object, 2 shells", "Separate Shell + Partition live", Profile);
        }
        return Count;
    }

    // 📝 A labelled integer spinner. ImGui::InputInt with its own buttons is noisy at this size, so the value sits between two
    //    small steppers and is clamped to a sane selection range.
    void ConstructCountSpinner(const char* Label, int& Value, int Minimum, int Maximum)
    {
        ImGui::PushID(Label);
        ImGui::TextUnformatted(Label);
        ImGui::SameLine(190.0f);
        if (ImGui::SmallButton("-")) { --Value; }
        ImGui::SameLine();
        ImGui::Text("%4d", Value);
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) { ++Value; }
        if (Value < Minimum) { Value = Minimum; }
        if (Value > Maximum) { Value = Maximum; }
        ImGui::PopID();
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructPolygonActionPanel(const ThemeConfiguration& Theme, PolygonActionState& State)
{
    //--------------------------------------------------- LEFT: SELECTION AUTHORING ---------------------------------------------------
    ImGui::BeginChild("##authoring", ImVec2(340.0f, 0.0f), false);

    ImGui::TextUnformatted("STRATUM");
    ImGui::Separator();

    // One button per stratum; the active one is filled with the accent so the menu header and this row always agree.
    for (int Index = 0; Index < static_cast<int>(TopologyStratum::StratumCount); ++Index)
    {
        const TopologyStratum Stratum = static_cast<TopologyStratum>(Index);
        const bool Active = (State.Profile.ActiveStratum == Stratum);

        if (Active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme.Palette.AccentPrimary);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme.Palette.TextOnAccent);
        }
        if (ImGui::Button(DescribeTopologyStratum(Stratum), ImVec2(100.0f, 0.0f)))
        {
            State.Profile.ActiveStratum = Stratum;
        }
        if (Active)
        {
            ImGui::PopStyleColor(2);
        }
        if ((Index % 3) != 2 && Index != static_cast<int>(TopologyStratum::StratumCount) - 1)
        {
            ImGui::SameLine();
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextUnformatted("COUNTS");
    ImGui::Separator();
    ConstructCountSpinner("Selected", State.Profile.SelectedCount, 0, 64);
    ConstructCountSpinner("Distinct regions", State.Profile.DistinctRegionCount, 0, 8);
    ConstructCountSpinner("Boundary edges", State.Profile.BoundaryEdgeCount, 0, 32);
    ConstructCountSpinner("Shells", State.Profile.ShellCount, 1, 8);
    ConstructCountSpinner("Max face sides", State.Profile.MaximumFaceSideCount, 3, 8);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextUnformatted("CONNECTIVITY");
    ImGui::Separator();
    ImGui::Checkbox("Quad ring walkable",  &State.Profile.QuadRingCondition);
    ImGui::Checkbox("Closed loop",         &State.Profile.ClosedLoopCondition);
    ImGui::Checkbox("Open boundary",       &State.Profile.BoundaryCondition);
    ImGui::Checkbox("Manifold",            &State.Profile.ManifoldCondition);
    ImGui::Checkbox("Shared components",   &State.Profile.SharedComponentCondition);
    ImGui::Checkbox("Two-face edges",      &State.Profile.TwoFaceEdgeCondition);
    ImGui::Checkbox("Matched quad pair",   &State.Profile.QuadPairCondition);
    ImGui::Checkbox("Adjacent triangles",  &State.Profile.TrianglePairCondition);
    ImGui::Checkbox("Open surface",        &State.Profile.OpenSurfaceCondition);
    ImGui::Checkbox("Symmetry axis",       &State.Profile.SymmetryAxisCondition);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextUnformatted("PRESETS");
    ImGui::Separator();

    SelectionPreset Presets[12] = {};
    const int PresetCount = ResolveSelectionPresets(Presets, 12);
    for (int Index = 0; Index < PresetCount; ++Index)
    {
        ImGui::PushID(Index);
        if (ImGui::Button(Presets[Index].Label, ImVec2(200.0f, 0.0f)))
        {
            State.Profile = Presets[Index].Profile;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme.Palette.TextMuted), "%s", Presets[Index].Expectation);
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Checkbox("Reveal gated rows", &State.RevealGatedEntries);
    ImGui::EndChild();

    ImGui::SameLine();

    //------------------------------------------------- RIGHT: TALLY AND THE LIVE MENU ------------------------------------------------
    ImGui::BeginChild("##readout", ImVec2(0.0f, 0.0f), false);

    int AvailableCount = 0;
    int GatedCount     = 0;
    int HiddenCount    = 0;
    TallyOperationAvailability(State.Profile, AvailableCount, GatedCount, HiddenCount);

    // Only the count is wanted here; the table pointer is deliberately discarded (the tally above already walked it).
    int CatalogueCount = 0;
    (void)ResolveOperationCatalogue(CatalogueCount);

    ImGui::TextUnformatted("AVAILABILITY");
    ImGui::Separator();
    ImGui::Text("catalogue  %3d", CatalogueCount);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme.Palette.AccentPrimary), "available  %3d", AvailableCount);
    ImGui::Text("gated      %3d", GatedCount);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme.Palette.TextMuted), "hidden     %3d", HiddenCount);

    // 🔴 The invariant that proves the three-state model is intact: every catalogue row lands in exactly one bucket. Shown in
    //    the UI rather than asserted, so a reviewer sees it hold as they click rather than trusting a passing test.
    const int Total = AvailableCount + GatedCount + HiddenCount;
    if (Total != CatalogueCount)
    {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "TALLY MISMATCH %d != %d", Total, CatalogueCount);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextUnformatted("LAST ACTIVATION");
    ImGui::Separator();
    if (State.ActivationTally > 0)
    {
        ImGui::Text("%s", State.LastActivatedLabel);
        if (State.LastBranchOption[0] != '\0')
        {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme.Palette.TextMuted), "  branch: %s", State.LastBranchOption);
        }
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme.Palette.TextMuted), "  activations: %d", State.ActivationTally);
    }
    else
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme.Palette.TextMuted), "(click a live row)");
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme.Palette.TextMuted),
                       "right-click this side to open the menu  ·  Esc or click away to dismiss");

    // The anchor defaults to just right of the authoring column; a right-click moves it, which is how the edge-flip placement
    // gets exercised without a viewport.
    const ImVec2 RegionMin = ImGui::GetItemRectMin();
    if (State.MenuAnchorX == 0.0f && State.MenuAnchorY == 0.0f)
    {
        State.MenuAnchorX = RegionMin.x + 40.0f;
        State.MenuAnchorY = RegionMin.y + 40.0f;
    }

    // 🔴 Gated on THIS region being hovered. Polled bare, the click also fired while the pointer was over the authoring column
    //    or over the open card itself, which re-anchored the menu out from under whatever row was being clicked.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        State.ReanchorRequested = true;
    }

    ImGui::EndChild();

    //---------------------------------------------------------- THE MENU ----------------------------------------------------------
    State.EntryCount = FilterOperationCatalogue(State.Profile, State.RevealGatedEntries,
                                                State.Entries, ResolvedEntryCapacity);

    if (State.MenuOpen && State.EntryCount > 0)
    {
        // Branch payloads. Domain data the menu does not know, supplied here exactly as a real editor would.
        static const char* const AxisOptions[]     = { "X", "Y", "Z" };
        static const char* const MergeOptions[]    = { "Centre", "First", "Last" };
        static const char* const MaterialOptions[] = { "Default", "Metal", "Glass", "Rubber" };
        static const char* const TraitOptions[]    = { "Side count", "Area", "Normal", "Material" };

        const MenuBranchDescriptor Branches[] =
        {
            { "Align To",         AxisOptions,     3, 0 },
            { "Mirror Axis",      AxisOptions,     3, 0 },
            { "Merge Target",     MergeOptions,    3, 0 },
            { "Assign Material",  MaterialOptions, 4, 0 },
            { "Similar Trait",    TraitOptions,    4, 0 }
        };

        const ImGuiViewport* Viewport = ImGui::GetMainViewport();

        // The card scrolls past two thirds of the work area rather than the whole of it, matching the mockup's `max-height:68vh`:
        // a menu that reached the screen edge would have no visible margin telling a reader it is a card at all.
        const float HeightLimit = Viewport->WorkSize.y * 0.68f;
        const float ContentHeight = ResolveTopologyActionMenuHeight(Theme, State.Entries, State.EntryCount);
        const float CardHeight    = ContentHeight < HeightLimit ? ContentHeight : HeightLimit;

        // The subtitle states the filtered count against the catalogue total, which is the size the catalogue reports itself.
        // Only the count is wanted here, but the table pointer is [[nodiscard]] — so it is bound rather than dropped.
        int CatalogueCount = 0;
        const TopologyOperationDescriptor* const Catalogue = ResolveOperationCatalogue(CatalogueCount);
        (void)Catalogue;

        TopologyActionMenuDescriptor MenuDescriptor = {};
        MenuDescriptor.Identifier      = "##polygon-action-menu";
        MenuDescriptor.ActiveStratum   = State.Profile.ActiveStratum;
        MenuDescriptor.SelectedCount   = State.Profile.SelectedCount;
        MenuDescriptor.CatalogueCount  = CatalogueCount;
        MenuDescriptor.Entries         = State.Entries;
        MenuDescriptor.EntryCount      = State.EntryCount;
        MenuDescriptor.Branches        = Branches;
        MenuDescriptor.BranchCount     = 5;
        MenuDescriptor.AnchorPosition  = ImVec2(State.MenuAnchorX, State.MenuAnchorY);
        MenuDescriptor.CardWidth       = 0.0f;
        MenuDescriptor.HeightLimit     = HeightLimit;
        MenuDescriptor.Growth          = ResolveMenuPlacement(
            MenuDescriptor.AnchorPosition, ImVec2(268.0f * Theme.Metrics.UiScale, CardHeight),
            Viewport->WorkPos,
            ImVec2(Viewport->WorkPos.x + Viewport->WorkSize.x, Viewport->WorkPos.y + Viewport->WorkSize.y));

        const TopologyActionMenuResult MenuResult = ConstructTopologyActionMenu(Theme, MenuDescriptor);

        if (MenuResult.ActivatedIndex >= 0 && MenuResult.ActivatedIndex < State.EntryCount)
        {
            const ResolvedOperationEntry& Entry = State.Entries[MenuResult.ActivatedIndex];
            if (Entry.Descriptor != nullptr && Entry.Descriptor->Label != nullptr)
            {
                std::snprintf(State.LastActivatedLabel, sizeof(State.LastActivatedLabel), "%s", Entry.Descriptor->Label);

                State.LastBranchOption[0] = '\0';
                if (MenuResult.BranchOptionIndex >= 0 && Entry.Descriptor->BranchCaption != nullptr)
                {
                    // Resolve the chosen option through the same caption match the menu used, so the readout cannot disagree
                    // with what was clicked.
                    for (int Index = 0; Index < 5; ++Index)
                    {
                        if (std::strcmp(Branches[Index].Caption, Entry.Descriptor->BranchCaption) == 0 &&
                            MenuResult.BranchOptionIndex < Branches[Index].OptionCount)
                        {
                            std::snprintf(State.LastBranchOption, sizeof(State.LastBranchOption), "%s",
                                          Branches[Index].Options[MenuResult.BranchOptionIndex]);
                            break;
                        }
                    }
                }
                ++State.ActivationTally;
            }
        }

        // A chosen row closes the menu, exactly as the prototype does — a context menu that stayed open after a command would
        // read as though the click had missed.
        if (MenuResult.ActivatedIndex >= 0 || MenuResult.DismissRequested)
        {
            State.MenuOpen = false;
        }
    }

    // The deferred right-click, applied last: by now an open card has already reported whether this same press dismissed it, so
    // opening here always wins and the menu lands under the pointer.
    if (State.ReanchorRequested)
    {
        const ImVec2 Pointer = ImGui::GetMousePos();
        State.MenuAnchorX        = Pointer.x;
        State.MenuAnchorY        = Pointer.y;
        State.MenuOpen           = true;
        State.ReanchorRequested  = false;
    }
}

}   // namespace Frontier
