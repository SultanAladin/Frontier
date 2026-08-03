/*==============================================================================================================================================
                                                            SKETCHOUTLINERPANEL.CPP
==============================================================================================================================================*/
// 🧩 The parametric-sketch modeling tree, drawn one-to-one with SceneDirectoryPanel but over CAD content: a part root, datum primitives + a
//    coordinate frame, sketches with their curves and constraints, solid shells, features in the build order, and a component instance. Every
//    row is custom-drawn through ImDrawList — rows, twisties, tinted classification icons, filter chips and menus — and each icon is a real SVG
//    glyph resolved from the SvgIconRegistry by the row's IconKey ("cad-" / "g-" tiers), with a procedural stroke fallback when a key has not
//    uploaded. The shared theme supplies the base panel / text / border tones; a small outliner-local palette adds the blue selection accent and
//    the per-classification icon tints. All of it lives in the nested namespace Frontier::SketchOutlinerUi so its RecordEntry / RecordToken never
//    collide with the pillar's Frontier::RecordEntry (the SceneDirectory tree in Scene/RecordEntry.h).

#include "SketchOutlinerPanel.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstring>

namespace Frontier::SketchOutlinerUi
{

using Frontier::ThemeConfiguration;
using Frontier::SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                      OUTLINER PALETTE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Tones the shared theme does not carry (blue accent, faint text, chip / menu fills). The base panel / border / text tones still come from
    //    the resolved theme so a global palette swap re-tints the outliner too.
    struct OutlinerPalette
    {
        ImU32 DeepBackground  = IM_COL32( 0x0a, 0x0a, 0x0b, 255);   // panel background
        ImU32 StripBackground = IM_COL32( 0x08, 0x08, 0x0a, 255);   // footer fill
        ImU32 HeaderFill      = IM_COL32( 0x11, 0x11, 0x13, 255);   // section-header + filters fill
        ImU32 SearchFill      = IM_COL32( 0x14, 0x14, 0x16, 255);   // search / chip fill
        ImU32 BorderSoft      = IM_COL32( 0x16, 0x16, 0x18, 255);   // soft border
        ImU32 BorderLine      = IM_COL32( 0x1c, 0x1c, 0x20, 255);   // border
        ImU32 RowHover        = IM_COL32( 0x16, 0x16, 0x18, 255);   // row hover
        ImU32 RowActive       = IM_COL32( 0x24, 0x24, 0x28, 255);   // row active
        ImU32 MenuFill        = IM_COL32( 0x14, 0x14, 0x16, 255);   // ctx-menu / add-menu fill
        ImU32 MenuBorder      = IM_COL32( 0x2a, 0x2a, 0x30, 255);   // ctx-menu border
        ImU32 MenuHover       = IM_COL32( 0x22, 0x22, 0x2a, 255);   // ctx-item hover
        ImU32 TextPrimary     = IM_COL32( 0xd8, 0xd8, 0xdc, 255);   // primary text
        ImU32 TextDim         = IM_COL32( 0x8a, 0x8a, 0x92, 255);   // dim text
        ImU32 TextFaint       = IM_COL32( 0x5a, 0x5a, 0x62, 255);   // faint text
        ImU32 Accent          = IM_COL32( 0x3a, 0x7b, 0xd5, 255);   // accent (blue)
        ImU32 AccentSoft      = IM_COL32( 0x3a, 0x7b, 0xd5,  38);   // drop-target tint
        ImU32 Danger          = IM_COL32( 0xe0, 0x5a, 0x5a, 255);   // delete / clear-all
        ImU32 LiveDot         = IM_COL32( 0x3a, 0xd0, 0x7a, 255);   // footer "Live" dot
    };

    const OutlinerPalette Palette;


    //---------------------------------------------------- PROFILE LOOKUPS ----------------------------------------------------

    // 📝 The one classification row for an opaque id, or nullptr when the profile does not carry it. Every icon / tint / label / container
    //    decision the panel makes routes through here — the panel never names a classification value.
    const OutlinerClassRow* ResolveClassRow(const OutlinerContentProfile& Profile, int ClassificationId)
    {
        for (int Index = 0; Index < Profile.ClassRowCount; ++Index)
        {
            if (Profile.ClassRows[Index].ClassificationId == ClassificationId) { return &Profile.ClassRows[Index]; }
        }
        return nullptr;
    }

    // 📝 Issue the next stable token and stamp a freshly built entry. Used by the add-object catalogue and the group action; the profile's own
    //    SeedSample builds its default tree the same way through the public InitializeSketchOutlinerSample.
    RecordEntry ConstructEntry(SketchOutlinerState&  State,
                               const char*           Label,
                               int                   ClassificationId,
                               const char*           IconKey,
                               ImU32                 TintColor,
                               bool                  ExpandedState)
    {
        RecordEntry Entry;
        Entry.Token            = State.NextToken++;
        Entry.Label            = Label;
        Entry.ClassificationId = ClassificationId;
        Entry.IconKey          = IconKey;
        Entry.TintColor        = TintColor;
        Entry.ExpandedState    = ExpandedState;
        Entry.ConcealedState   = false;
        return Entry;
    }
}


void InitializeSketchOutlinerSample(SketchOutlinerState& State, const OutlinerContentProfile& Profile)
{
    // 📝 The profile owns its default tree — clear + seed + landmark-select all live in Profile.SeedSample so the panel names no content.
    if (Profile.SeedSample != nullptr) { Profile.SeedSample(State, Profile); }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      TREE TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Resolve a token to its live RecordEntry (mutable), searching the whole tree. Returns nullptr for a stale / null token.
    RecordEntry* ResolveEntry(std::vector<RecordEntry>& Region, RecordToken Target)
    {
        for (RecordEntry& Entry : Region)
        {
            if (Entry.Token == Target) { return &Entry; }
            RecordEntry* Nested = ResolveEntry(Entry.NestedRegion, Target);
            if (Nested != nullptr) { return Nested; }
        }
        return nullptr;
    }

    // 📝 Resolve the region that directly contains a token, plus the slot index, so a row can be detached / relocated in place.
    bool ResolveContainer(std::vector<RecordEntry>& Region,
                          RecordToken               Target,
                          std::vector<RecordEntry>** OutContainer,
                          std::size_t*               OutIndex)
    {
        for (std::size_t Index = 0; Index < Region.size(); ++Index)
        {
            if (Region[Index].Token == Target)
            {
                *OutContainer = &Region;
                *OutIndex     = Index;
                return true;
            }
            if (ResolveContainer(Region[Index].NestedRegion, Target, OutContainer, OutIndex))
            {
                return true;
            }
        }
        return false;
    }

    bool SelectionContains(const SketchOutlinerState& State, RecordToken Target)
    {
        return std::find(State.SelectionSet.begin(), State.SelectionSet.end(), Target) != State.SelectionSet.end();
    }

    // 📝 Is Candidate nested anywhere inside Enclosure's region (so a relocation into one's own subtree can be rejected)?
    bool NestedWithin(RecordEntry& Enclosure, RecordToken Candidate)
    {
        for (RecordEntry& Entry : Enclosure.NestedRegion)
        {
            if (Entry.Token == Candidate) { return true; }
            if (NestedWithin(Entry, Candidate)) { return true; }
        }
        return false;
    }

    bool ContainerClassification(const OutlinerContentProfile& Profile, int ClassificationId)
    {
        const OutlinerClassRow* Row = ResolveClassRow(Profile, ClassificationId);
        return Row != nullptr && Row->Container;
    }

    // 📝 Count leaf rows (items with no nested region) for the footer "N objects" readout.
    int AccumulateLeafCount(const std::vector<RecordEntry>& Region)
    {
        int Count = 0;
        for (const RecordEntry& Entry : Region)
        {
            if (Entry.NestedRegion.empty()) { ++Count; }
            else { Count += AccumulateLeafCount(Entry.NestedRegion); }
        }
        return Count;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      FILTER PREDICATES
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    bool AnyFilterActive(const SketchOutlinerState& State)
    {
        return State.SearchText[0] != '\0' || !State.FilterChips.empty();
    }

    // 📝 Case-insensitive substring test of the search box against a label.
    bool MatchesSearch(const SketchOutlinerState& State, const RecordEntry& Entry)
    {
        if (State.SearchText[0] == '\0') { return true; }
        std::string Needle = State.SearchText;
        std::transform(Needle.begin(), Needle.end(), Needle.begin(), [](unsigned char Ch){ return (char)std::tolower(Ch); });
        std::string Hay = Entry.Label;
        std::transform(Hay.begin(), Hay.end(), Hay.begin(), [](unsigned char Ch){ return (char)std::tolower(Ch); });
        return Hay.find(Needle) != std::string::npos;
    }

    // 📝 Does the entry itself satisfy every active chip? Chips of the same facet are OR'd; different facets are AND'd.
    bool MatchesChips(const SketchOutlinerState& State, const RecordEntry& Entry)
    {
        bool ClassificationFacetPresent = false;
        bool ClassificationMatched      = false;
        bool TintFacetPresent           = false;
        bool TintMatched                = false;

        for (const FilterChip& Chip : State.FilterChips)
        {
            if (Chip.Facet == FilterFacet::OnlyVisible)
            {
                if (Entry.ConcealedState) { return false; }
            }
            else if (Chip.Facet == FilterFacet::Classification)
            {
                ClassificationFacetPresent = true;
                if (Entry.ClassificationId == Chip.ClassificationValue) { ClassificationMatched = true; }
            }
            else if (Chip.Facet == FilterFacet::Tint)
            {
                TintFacetPresent = true;
                if (Entry.TintColor == Chip.TintValue) { TintMatched = true; }
            }
        }

        if (ClassificationFacetPresent && !ClassificationMatched) { return false; }
        if (TintFacetPresent && !TintMatched) { return false; }
        return true;
    }

    bool SelfMatches(const SketchOutlinerState& State, const RecordEntry& Entry)
    {
        return MatchesChips(State, Entry) && MatchesSearch(State, Entry);
    }

    // 📝 Keep an entry if it OR any nested item matches, so containers of a match stay reachable.
    bool RegionMatches(const SketchOutlinerState& State, const RecordEntry& Entry)
    {
        if (SelfMatches(State, Entry)) { return true; }
        for (const RecordEntry& Nested : Entry.NestedRegion)
        {
            if (RegionMatches(State, Nested)) { return true; }
        }
        return false;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      ICON RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Draw the row's icon at Origin: prefer the real uploaded SVG texture resolved from the registry by IconKey (tinted by TintColor via the
    //    ImGui::Image colour multiply), and fall back to the profile's procedural stroke art when the registry is null or that key has not
    //    uploaded. The panel names no glyph — every branch of the stroke fallback lives in the profile (Profile.PaintFallbackGlyph).
    void ConstructIcon(ImDrawList*                      DrawList,
                       const OutlinerContentProfile&    Profile,
                       const Frontier::SvgIconRegistry* Registry,
                       const std::string&               IconKey,
                       ImVec2                           Origin,
                       ImU32                            TintColor)
    {
        const float Box = 18.0f;
        ImTextureID Texture = Registry != nullptr ? Frontier::ResolveIconTexture(*Registry, IconKey) : (ImTextureID)0;
        if (Texture != 0)
        {
            DrawList->AddImage(Texture, Origin, ImVec2(Origin.x + Box, Origin.y + Box), ImVec2(0, 0), ImVec2(1, 1), TintColor);
            return;
        }
        if (Profile.PaintFallbackGlyph != nullptr) { Profile.PaintFallbackGlyph(DrawList, IconKey.c_str(), Origin, TintColor); }
        else { DrawList->AddCircle(ImVec2(Origin.x + Box * 0.5f, Origin.y + Box * 0.5f), 5.0f, TintColor, 0, 1.3f); }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SELECTION LOGIC
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The flat token order of every currently-visible row, honouring fold state + active filter. Drives shift-range selection.
    void AccumulateVisibleOrder(const SketchOutlinerState& State, const std::vector<RecordEntry>& Region, std::vector<RecordToken>& Order)
    {
        const bool Filtering = AnyFilterActive(State);
        for (const RecordEntry& Entry : Region)
        {
            if (Filtering && !RegionMatches(State, Entry)) { continue; }
            Order.push_back(Entry.Token);
            const bool Open = Entry.ExpandedState || Filtering;
            if (!Entry.NestedRegion.empty() && Open)
            {
                AccumulateVisibleOrder(State, Entry.NestedRegion, Order);
            }
        }
    }

    void SelectOnly(SketchOutlinerState& State, RecordToken Target)
    {
        State.SelectionSet.clear();
        State.SelectionSet.push_back(Target);
        State.RangeAnchor = Target;
    }

    void ToggleInSelection(SketchOutlinerState& State, RecordToken Target)
    {
        auto Found = std::find(State.SelectionSet.begin(), State.SelectionSet.end(), Target);
        if (Found != State.SelectionSet.end()) { State.SelectionSet.erase(Found); }
        else { State.SelectionSet.push_back(Target); }
        State.RangeAnchor = Target;
    }

    void SelectRange(SketchOutlinerState& State, RecordToken Target)
    {
        std::vector<RecordToken> Order;
        AccumulateVisibleOrder(State, State.RootRegion, Order);
        auto AnchorAt = std::find(Order.begin(), Order.end(), State.RangeAnchor);
        auto TargetAt = std::find(Order.begin(), Order.end(), Target);
        if (AnchorAt == Order.end() || TargetAt == Order.end()) { SelectOnly(State, Target); return; }
        std::size_t Low  = std::min(AnchorAt - Order.begin(), TargetAt - Order.begin());
        std::size_t High = std::max(AnchorAt - Order.begin(), TargetAt - Order.begin());
        State.SelectionSet.assign(Order.begin() + Low, Order.begin() + High + 1);
    }

    // 📝 Apply a click's modifier keys to the selection (plain / ctrl-toggle / shift-range).
    void ApplyClickSelection(SketchOutlinerState& State, RecordToken Target)
    {
        ImGuiIO& Io = ImGui::GetIO();
        if (Io.KeyShift && State.RangeAnchor != 0) { SelectRange(State, Target); }
        else if (Io.KeyCtrl) { ToggleInSelection(State, Target); }
        else { SelectOnly(State, Target); }
    }

    // 📝 The tokens an action should operate on: the selection, or a fallback token if nothing is selected.
    std::vector<RecordToken> ResolveActionTargets(const SketchOutlinerState& State, RecordToken Fallback)
    {
        if (!State.SelectionSet.empty()) { return State.SelectionSet; }
        std::vector<RecordToken> Single;
        if (Fallback != 0) { Single.push_back(Fallback); }
        return Single;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      MUTATION ACTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Forward-declared: the unique-name resolver lives with the add-object catalogue further down, but GroupTargets needs it here.
    std::string ResolveUniqueName(SketchOutlinerState& State, const char* Stem);

    void ConcealRegion(RecordEntry& Entry, bool Concealed)
    {
        Entry.ConcealedState = Concealed;
        for (RecordEntry& Nested : Entry.NestedRegion) { ConcealRegion(Nested, Concealed); }
    }

    // 📝 Toggle visibility for the whole selection (or one row), driving every target to the opposite of the clicked row.
    void ToggleConcealment(SketchOutlinerState& State, RecordToken Clicked)
    {
        RecordEntry* Anchor = ResolveEntry(State.RootRegion, Clicked);
        if (Anchor == nullptr) { return; }
        const bool Target = !Anchor->ConcealedState;
        std::vector<RecordToken> Targets = ResolveActionTargets(State, Clicked);
        if (Targets.size() <= 1) { ConcealRegion(*Anchor, Target); return; }
        for (RecordToken Token : Targets)
        {
            RecordEntry* Entry = ResolveEntry(State.RootRegion, Token);
            if (Entry != nullptr) { ConcealRegion(*Entry, Target); }
        }
    }

    void CollapseEveryContainer(std::vector<RecordEntry>& Region)
    {
        for (RecordEntry& Entry : Region)
        {
            if (!Entry.NestedRegion.empty()) { Entry.ExpandedState = false; }
            CollapseEveryContainer(Entry.NestedRegion);
        }
    }

    void DeleteTargets(SketchOutlinerState& State, RecordToken Fallback)
    {
        std::vector<RecordToken> Targets = ResolveActionTargets(State, Fallback);
        for (RecordToken Token : Targets)
        {
            std::vector<RecordEntry>* Container = nullptr;
            std::size_t Index = 0;
            if (ResolveContainer(State.RootRegion, Token, &Container, &Index))
            {
                Container->erase(Container->begin() + Index);
            }
            auto Selected = std::find(State.SelectionSet.begin(), State.SelectionSet.end(), Token);
            if (Selected != State.SelectionSet.end()) { State.SelectionSet.erase(Selected); }
        }
    }

    // 📝 Re-stamp every token in a cloned subtree so a duplicate never aliases the original's identity.
    void ReissueTokens(SketchOutlinerState& State, RecordEntry& Entry)
    {
        Entry.Token = State.NextToken++;
        for (RecordEntry& Nested : Entry.NestedRegion) { ReissueTokens(State, Nested); }
    }

    void DuplicateTargets(SketchOutlinerState& State, RecordToken Fallback)
    {
        std::vector<RecordToken> Targets = ResolveActionTargets(State, Fallback);
        std::vector<RecordToken> FreshSelection;
        for (RecordToken Token : Targets)
        {
            std::vector<RecordEntry>* Container = nullptr;
            std::size_t Index = 0;
            if (!ResolveContainer(State.RootRegion, Token, &Container, &Index)) { continue; }
            RecordEntry Clone = (*Container)[Index];
            ReissueTokens(State, Clone);
            Clone.Label += "_copy";
            FreshSelection.push_back(Clone.Token);
            Container->insert(Container->begin() + Index + 1, std::move(Clone));
        }
        if (!FreshSelection.empty()) { State.SelectionSet = FreshSelection; State.RangeAnchor = FreshSelection.front(); }
    }

    // 📝 The profile's grouping container — the first container-flagged add-catalogue row (Directory / Folder). GroupTargets folds the selection
    //    into a fresh instance of it, so the "Group into X" action names no classification.
    const OutlinerAddRow* ResolveGroupingRow(const OutlinerContentProfile& Profile)
    {
        for (int Index = 0; Index < Profile.AddRowCount; ++Index)
        {
            const OutlinerAddRow& Row = Profile.AddRows[Index];
            if (Row.Section != nullptr) { continue; }
            if (ContainerClassification(Profile, Row.ClassificationId)) { return &Row; }
        }
        return nullptr;
    }

    void GroupTargets(SketchOutlinerState& State, const OutlinerContentProfile& Profile, RecordToken Fallback)
    {
        std::vector<RecordToken> Targets = ResolveActionTargets(State, Fallback);
        if (Targets.empty()) { return; }

        const OutlinerAddRow* GroupingRow = ResolveGroupingRow(Profile);
        if (GroupingRow == nullptr) { return; }
        const OutlinerClassRow* GroupingClass = ResolveClassRow(Profile, GroupingRow->ClassificationId);
        if (GroupingClass == nullptr) { return; }

        std::vector<RecordEntry>* FirstContainer = nullptr;
        std::size_t FirstIndex = 0;
        if (!ResolveContainer(State.RootRegion, Targets.front(), &FirstContainer, &FirstIndex)) { return; }

        std::string GroupName = ResolveUniqueName(State, GroupingRow->Stem);
        RecordEntry Grouping = ConstructEntry(State, GroupName.c_str(), GroupingClass->ClassificationId,
                                              GroupingClass->IconKey, GroupingClass->Tint, true);
        for (RecordToken Token : Targets)
        {
            std::vector<RecordEntry>* Container = nullptr;
            std::size_t Index = 0;
            if (ResolveContainer(State.RootRegion, Token, &Container, &Index))
            {
                Grouping.NestedRegion.push_back(std::move((*Container)[Index]));
                Container->erase(Container->begin() + Index);
            }
        }
        // 📝 Re-resolve the insertion point: earlier erases may have shifted the first container's contents.
        std::vector<RecordEntry>* Destination = FirstContainer;
        std::size_t InsertAt = std::min(FirstIndex, Destination->size());
        RecordToken GroupToken = Grouping.Token;
        Destination->insert(Destination->begin() + InsertAt, std::move(Grouping));
        SelectOnly(State, GroupToken);
    }

    // 📝 Relocate every dragged token (the selection, or the single dragged one) into the target container.
    void RelocateInto(SketchOutlinerState& State, RecordToken Dragged, RecordToken TargetToken)
    {
        RecordEntry* Target = ResolveEntry(State.RootRegion, TargetToken);
        if (Target == nullptr) { return; }
        std::vector<RecordToken> Movers = SelectionContains(State, Dragged) ? State.SelectionSet : std::vector<RecordToken>{ Dragged };
        for (RecordToken Token : Movers)
        {
            if (Token == TargetToken) { continue; }
            RecordEntry* Moving = ResolveEntry(State.RootRegion, Token);
            if (Moving == nullptr || NestedWithin(*Moving, TargetToken)) { continue; }

            std::vector<RecordEntry>* Container = nullptr;
            std::size_t Index = 0;
            if (!ResolveContainer(State.RootRegion, Token, &Container, &Index)) { continue; }
            if (Container == &Target->NestedRegion) { continue; }   // already a direct nested item
            RecordEntry Detached = std::move((*Container)[Index]);
            Container->erase(Container->begin() + Index);
            // 📝 Target may have moved in memory after the erase; re-resolve before appending.
            Target = ResolveEntry(State.RootRegion, TargetToken);
            if (Target == nullptr) { return; }
            Target->NestedRegion.push_back(std::move(Detached));
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      ADD-OBJECT CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Resolve a unique display name for a fresh item: the bare stem when free, otherwise the first free "<Stem>_NN" (zero-padded to 2).
    std::string ResolveUniqueName(SketchOutlinerState& State, const char* Stem)
    {
        auto Taken = [&](const std::string& Name)
        {
            bool Found = false;
            struct Scan { static void Walk(const std::vector<RecordEntry>& Region, const std::string& N, bool& F)
            {
                for (const RecordEntry& E : Region) { if (E.Label == N) { F = true; } Walk(E.NestedRegion, N, F); }
            } };
            Scan::Walk(State.RootRegion, Name, Found);
            return Found;
        };

        std::string Base = Stem;
        if (!Taken(Base)) { return Base; }
        for (int Suffix = 1; ; ++Suffix)
        {
            char Numbered[160];
            std::snprintf(Numbered, sizeof(Numbered), "%s_%02d", Stem, Suffix);
            if (!Taken(Numbered)) { return Numbered; }
        }
    }

    // 📝 Where a new object lands: a container anchor => inside it; a leaf anchor => the root's region; no anchor => the root's region.
    std::vector<RecordEntry>* ResolveAddDestination(SketchOutlinerState& State, const OutlinerContentProfile& Profile, RecordToken Anchor)
    {
        if (Anchor != 0)
        {
            RecordEntry* AnchorEntry = ResolveEntry(State.RootRegion, Anchor);
            if (AnchorEntry != nullptr && ContainerClassification(Profile, AnchorEntry->ClassificationId))
            {
                return &AnchorEntry->NestedRegion;
            }
        }
        if (!State.RootRegion.empty())
        {
            RecordEntry& Root = State.RootRegion.front();
            return &Root.NestedRegion;
        }
        return &State.RootRegion;
    }

    // 📝 Create the add-catalogue row's item: its icon / tint / container flag come from the profile's ClassRows entry for the row's
    //    classification, so the panel names no content. The row must be a creatable item (non-null Section rows never reach here).
    void AppendObject(SketchOutlinerState& State, const OutlinerContentProfile& Profile, const OutlinerAddRow& Row)
    {
        const OutlinerClassRow* Class = ResolveClassRow(Profile, Row.ClassificationId);
        if (Class == nullptr) { return; }

        std::vector<RecordEntry>* Destination = ResolveAddDestination(State, Profile, State.AddMenuAnchor);
        std::string Candidate = ResolveUniqueName(State, Row.Stem);

        RecordEntry Fresh = ConstructEntry(State, Candidate.c_str(), Class->ClassificationId, Class->IconKey, Class->Tint, Class->Container);
        RecordToken FreshToken = Fresh.Token;
        Destination->push_back(std::move(Fresh));
        SelectOnly(State, FreshToken);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INLINE RENAME
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void BeginRename(SketchOutlinerState& State, const RecordEntry& Entry)
    {
        State.RenameTarget = Entry.Token;
        std::snprintf(State.RenameBuffer, sizeof(State.RenameBuffer), "%s", Entry.Label.c_str());
        State.RenameJustOpened = true;
    }

    void FinalizeRename(SketchOutlinerState& State)
    {
        if (State.RenameTarget == 0) { return; }
        RecordEntry* Entry = ResolveEntry(State.RootRegion, State.RenameTarget);
        if (Entry != nullptr && State.RenameBuffer[0] != '\0') { Entry->Label = State.RenameBuffer; }
        State.RenameTarget = 0;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      FILTER FACETS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    bool ChipPresent(const SketchOutlinerState& State, FilterFacet Facet, int ClassificationId, ImU32 Tint)
    {
        for (const FilterChip& Chip : State.FilterChips)
        {
            if (Chip.Facet != Facet) { continue; }
            if (Facet == FilterFacet::OnlyVisible) { return true; }
            if (Facet == FilterFacet::Classification && Chip.ClassificationValue == ClassificationId) { return true; }
            if (Facet == FilterFacet::Tint && Chip.TintValue == Tint) { return true; }
        }
        return false;
    }

    void ToggleChip(SketchOutlinerState& State, FilterFacet Facet, int ClassificationId, ImU32 Tint)
    {
        for (std::size_t Index = 0; Index < State.FilterChips.size(); ++Index)
        {
            const FilterChip& Chip = State.FilterChips[Index];
            const bool Same = Chip.Facet == Facet &&
                ((Facet == FilterFacet::OnlyVisible) ||
                 (Facet == FilterFacet::Classification && Chip.ClassificationValue == ClassificationId) ||
                 (Facet == FilterFacet::Tint && Chip.TintValue == Tint));
            if (Same) { State.FilterChips.erase(State.FilterChips.begin() + Index); return; }
        }
        FilterChip Fresh = {};
        Fresh.Facet = Facet;
        Fresh.ClassificationValue = ClassificationId;
        Fresh.TintValue = Tint;
        State.FilterChips.push_back(Fresh);
    }

    // 📝 The chip label for a classification id — the profile's ClassRows entry, so a chip reads the same word the filter menu offered.
    const char* ClassificationLabel(const OutlinerContentProfile& Profile, int ClassificationId)
    {
        const OutlinerClassRow* Row = ResolveClassRow(Profile, ClassificationId);
        return Row != nullptr ? Row->Label : "Item";
    }

    // 📝 The chip label for a tint — the profile's TintFacets swatch it came from.
    const char* TintLabel(const OutlinerContentProfile& Profile, ImU32 Tint)
    {
        for (int Index = 0; Index < Profile.TintFacetCount; ++Index)
        {
            if (Profile.TintFacets[Index].Tint == Tint) { return Profile.TintFacets[Index].Label; }
        }
        return "Colour";
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      ROW DRAWING
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Per-cycle context threaded through the row recursion so each row can register drag/drop, clicks, menu requests, and resolve its icon
    //    texture without reaching back into ImGui window scope repeatedly. IconRegistry is borrowed (may be null => procedural fallback).
    struct RowContext
    {
        const ThemeConfiguration*        Theme;
        SketchOutlinerState*             State;
        const OutlinerContentProfile*    Profile;
        ImDrawList*                      DrawList;
        const Frontier::SvgIconRegistry* IconRegistry;
        float                            RowHeight;
        float                            IndentWidth;
        float                            ContentLeft;
        float                            ContentRight;
    };

    void ConstructRow(RowContext& Context, RecordEntry& Entry, int IndentDepth);

    // 📝 Per-container reveal ease (0 = collapsed, 1 = fully open), eased toward ExpandedState each frame and stored against the container's
    //    token. Read by both the row's twisty (chevron morph) and the recursion (subtree slide/fade). A stable, non-colliding key derives from
    //    the token so folding one container never disturbs another's animation.
    ImGuiID OpennessKey(RecordToken Token)
    {
        return ImGui::GetID((void*)(uintptr_t)(0xF0000000u ^ Token));
    }

    // 📝 Read the container's current openness WITHOUT advancing it — used by the twisty so it can morph in sync without double-stepping the ease.
    float PeekOpenness(RecordToken Token, bool TargetOpen)
    {
        ImGuiStorage* Store = ImGui::GetStateStorage();
        return Store->GetFloat(OpennessKey(Token), TargetOpen ? 1.0f : 0.0f);
    }

    // 📝 Advance the container's openness ease one frame toward TargetOpen and store it. Called exactly once per container per frame. A pure
    //    exponential crawls near the ends, so a constant floor velocity carries the tail at a steady rate and BandHeight lets it land exactly on
    //    0/1 the moment the residual band is sub-pixel — no lingering-gap-then-snap.
    float ResolveOpenness(RecordToken Token, bool TargetOpen, float BandHeight)
    {
        ImGuiStorage* Store  = ImGui::GetStateStorage();
        const ImGuiID Key    = OpennessKey(Token);
        const float   Target = TargetOpen ? 1.0f : 0.0f;
        float Ease = Store->GetFloat(Key, Target);

        const float Delta = Target - Ease;
        const float SubPixel = BandHeight > 1.0f ? (0.5f / BandHeight) : 0.01f;
        if (ImFabs(Delta) <= SubPixel)
        {
            Store->SetFloat(Key, Target);
            return Target;
        }

        const float Dt        = ImGui::GetIO().DeltaTime;
        const float Direction = Delta > 0.0f ? 1.0f : -1.0f;
        const float Exponential = Delta * ImMin(Dt * 11.0f, 1.0f);   // fast, smooth bulk of the travel
        const float FloorStep   = Direction * Dt * 4.0f;             // constant tail so it can't crawl (≈4 openness/sec floor)
        float Step = ImFabs(Exponential) > ImFabs(FloorStep) ? Exponential : FloorStep;
        if (ImFabs(Step) >= ImFabs(Delta)) { Ease = Target; }        // would reach/overshoot => land exactly
        else                               { Ease += Step; }

        Store->SetFloat(Key, Ease);
        return Ease;
    }

    // 📝 The pixel height a region occupies RIGHT NOW, honouring every nested container's eased openness (not just its target fold state). This
    //    is the crux of the smooth collapse: because it peeks the same ease the recursion draws with, the reserved band height tracks the
    //    animation frame-for-frame. PeekOpenness only reads (never steps) so measuring can't perturb the animation.
    float MeasureRegionHeight(RowContext& Context, const std::vector<RecordEntry>& Region)
    {
        const bool Filtering = AnyFilterActive(*Context.State);
        float Total = 0.0f;
        for (const RecordEntry& Entry : Region)
        {
            if (Filtering && !RegionMatches(*Context.State, Entry)) { continue; }
            Total += Context.RowHeight;
            if (!Entry.NestedRegion.empty())
            {
                const float Openness = Filtering ? 1.0f : PeekOpenness(Entry.Token, Entry.ExpandedState);
                if (Openness > 0.0f)
                {
                    Total += MeasureRegionHeight(Context, Entry.NestedRegion) * Openness;
                }
            }
        }
        return Total;
    }

    void ConstructRegionRows(RowContext& Context, std::vector<RecordEntry>& Region, int IndentDepth)
    {
        const bool Filtering = AnyFilterActive(*Context.State);
        for (RecordEntry& Entry : Region)
        {
            if (Filtering && !RegionMatches(*Context.State, Entry)) { continue; }
            ConstructRow(Context, Entry, IndentDepth);

            if (Entry.NestedRegion.empty()) { continue; }

            // 📝 The container's ease drives a growing/shrinking clip window that the nested rows slide into. Measure the full subtree FIRST so
            //    the ease can land exactly on 0/1 the moment the residual band is sub-pixel (no lingering-gap-then-snap on full close).
            const float FullHeight = MeasureRegionHeight(Context, Entry.NestedRegion);
            const float Openness   = Filtering ? 1.0f : ResolveOpenness(Entry.Token, Entry.ExpandedState, FullHeight);
            if (Openness <= 0.0f) { continue; }

            const bool  Animating   = Openness < 1.0f;
            const float ShownHeight = Animating ? FullHeight * Openness : FullHeight;
            const ImVec2 RevealTop  = ImGui::GetCursorScreenPos();

            const bool ClipBand = Animating;
            if (ClipBand)
            {
                Context.DrawList->PushClipRect(ImVec2(RevealTop.x, RevealTop.y),
                                               ImVec2(Context.ContentRight, RevealTop.y + ShownHeight), true);
                const float SlideOffset = (1.0f - Openness) * Context.RowHeight * 0.5f;
                ImGui::SetCursorScreenPos(ImVec2(RevealTop.x, RevealTop.y - SlideOffset));
            }
            ConstructRegionRows(Context, Entry.NestedRegion, IndentDepth + 1);
            if (ClipBand) { Context.DrawList->PopClipRect(); }

            // 📝 Reserve exactly the eased height so subsequent siblings sit flush against the animated band.
            ImGui::SetCursorScreenPos(RevealTop);
            ImGui::Dummy(ImVec2(Context.ContentRight - RevealTop.x, ShownHeight));
        }
    }

    void ConstructRow(RowContext& Context, RecordEntry& Entry, int IndentDepth)
    {
        SketchOutlinerState& State = *Context.State;
        ImDrawList* DrawList = Context.DrawList;

        const float RowHeight = Context.RowHeight;
        ImVec2 CursorTop = ImGui::GetCursorScreenPos();
        const float RowLeft  = Context.ContentLeft;
        const float RowRight = Context.ContentRight;
        ImVec2 RowMin = ImVec2(RowLeft, CursorTop.y);
        ImVec2 RowMax = ImVec2(RowRight, CursorTop.y + RowHeight);

        // 📝 One invisible button spans the whole row for hit-testing; child hit-zones (twisty, eye) are handled by sub-rects afterwards.
        ImGui::PushID((int)Entry.Token);
        ImGui::SetCursorScreenPos(RowMin);
        ImGui::InvisibleButton("##row", ImVec2(RowRight - RowLeft, RowHeight));
        const bool Hovered = ImGui::IsItemHovered();
        const bool Selected = SelectionContains(State, Entry.Token);

        // -- Backgrounds --
        if (Selected)
        {
            DrawList->AddRectFilled(RowMin, RowMax, Palette.RowActive);
            DrawList->AddRectFilled(RowMin, ImVec2(RowMin.x + 2.0f, RowMax.y), Palette.Accent);
        }
        else if (Hovered)
        {
            DrawList->AddRectFilled(RowMin, RowMax, Palette.RowHover);
        }

        // -- Drag source / drop target --
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            if (!Selected) { SelectOnly(State, Entry.Token); }
            RecordToken Dragged = Entry.Token;
            ImGui::SetDragDropPayload("SKETCH_ROW", &Dragged, sizeof(RecordToken));
            ImGui::TextUnformatted(Entry.Label.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            DrawList->AddRectFilled(RowMin, RowMax, Palette.AccentSoft);
            DrawList->AddRect(RowMin, RowMax, Palette.Accent, 0.0f, 0, 1.0f);
            const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("SKETCH_ROW");
            if (Payload != nullptr)
            {
                RecordToken Dragged = *(const RecordToken*)Payload->Data;
                RelocateInto(State, Dragged, Entry.Token);
            }
            ImGui::EndDragDropTarget();
        }

        // -- Layout anchors --
        const float BaseIndent = 10.0f;
        float PenX = RowMin.x + BaseIndent + (float)IndentDepth * Context.IndentWidth;
        const float CentreY = RowMin.y + RowHeight * 0.5f;

        // -- Twisty (only for containers) --
        const bool HasRegion = !Entry.NestedRegion.empty();
        ImVec2 TwistyMin = ImVec2(PenX, CentreY - 8.0f);
        if (HasRegion)
        {
            const bool Filtering = AnyFilterActive(State);
            const float Openness = Filtering ? 1.0f : PeekOpenness(Entry.Token, Entry.ExpandedState);
            const float Angle    = (Openness - 1.0f) * 1.5707963f;   // -90° collapsed → 0° open
            const float Cos = ImCos(Angle), Sin = ImSin(Angle);
            const ImVec2 Pivot = ImVec2(PenX + 8.0f, CentreY);
            auto Rotate = [&](float Dx, float Dy) { return ImVec2(Pivot.x + Dx * Cos - Dy * Sin, Pivot.y + Dx * Sin + Dy * Cos); };
            const ImVec2 Ta = Rotate(-3.5f, -2.5f);
            const ImVec2 Tb = Rotate( 0.0f,  2.5f);
            const ImVec2 Tc = Rotate( 3.5f, -2.5f);
            DrawList->AddLine(Ta, Tb, Palette.TextFaint, 1.6f);
            DrawList->AddLine(Tb, Tc, Palette.TextFaint, 1.6f);
            if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ImVec2 Mouse = ImGui::GetIO().MousePos;
                if (Mouse.x >= TwistyMin.x && Mouse.x <= TwistyMin.x + 16.0f)
                {
                    Entry.ExpandedState = !Entry.ExpandedState;
                    ImGui::PopID();
                    return;
                }
            }
        }
        PenX += 16.0f + 7.0f;

        // -- Classification icon (real SVG texture, procedural fallback) --
        const bool Concealed = Entry.ConcealedState;
        ImU32 IconTint = Entry.TintColor;
        if (Concealed) { IconTint = (IconTint & 0x00FFFFFF) | (100 << 24); }
        ConstructIcon(DrawList, *Context.Profile, Context.IconRegistry, Entry.IconKey, ImVec2(PenX, CentreY - 9.0f), IconTint);
        PenX += 18.0f + 7.0f;

        // -- Label (or inline rename box) --
        const float EyeZone = 26.0f;
        const float LabelRight = RowMax.x - EyeZone;
        if (State.RenameTarget == Entry.Token)
        {
            ImGui::SetCursorScreenPos(ImVec2(PenX, CentreY - RowHeight * 0.5f + 4.0f));
            ImGui::SetNextItemWidth(LabelRight - PenX);
            if (State.RenameJustOpened) { ImGui::SetKeyboardFocusHere(); State.RenameJustOpened = false; }
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 255));
            ImGui::PushStyleColor(ImGuiCol_Border, Palette.Accent);
            const bool Committed = ImGui::InputText("##rename", State.RenameBuffer, sizeof(State.RenameBuffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleColor(2);
            if (Committed || (!ImGui::IsItemActive() && !State.RenameJustOpened && ImGui::IsItemDeactivated()))
            {
                FinalizeRename(State);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { State.RenameTarget = 0; }
        }
        else
        {
            ImU32 LabelColor = Concealed ? Palette.TextFaint : Palette.TextPrimary;
            if (AnyFilterActive(State) && !SelfMatches(State, Entry))
            {
                LabelColor = (LabelColor & 0x00FFFFFF) | (115 << 24);   // dim rows kept only for a nested match
            }
            DrawList->PushClipRect(ImVec2(PenX, RowMin.y), ImVec2(LabelRight, RowMax.y), true);
            DrawList->AddText(ImVec2(PenX, CentreY - ImGui::GetFontSize() * 0.5f), LabelColor, Entry.Label.c_str());
            DrawList->PopClipRect();
        }

        // -- Visibility eye (drawn on hover or when concealed) --
        ImVec2 EyeCentre = ImVec2(RowMax.x - 13.0f, CentreY);
        if (Hovered || Concealed)
        {
            ImU32 EyeColor = Concealed ? Palette.TextFaint : (Hovered ? Palette.TextDim : Palette.TextFaint);
            DrawList->AddEllipse(EyeCentre, ImVec2(6.0f, 4.0f), EyeColor, 0.0f, 0, 1.2f);
            if (Concealed)
            {
                DrawList->AddLine(ImVec2(EyeCentre.x - 6.0f, EyeCentre.y - 5.0f),
                                  ImVec2(EyeCentre.x + 6.0f, EyeCentre.y + 5.0f), EyeColor, 1.3f);
            }
            else
            {
                DrawList->AddCircle(EyeCentre, 1.8f, EyeColor, 0, 1.2f);
            }
            if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ImVec2 Mouse = ImGui::GetIO().MousePos;
                if (Mouse.x >= EyeCentre.x - 9.0f && Mouse.x <= EyeCentre.x + 9.0f)
                {
                    ToggleConcealment(State, Entry.Token);
                    ImGui::PopID();
                    return;
                }
            }
        }

        // -- Row-level click / double-click / right-click --
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ApplyClickSelection(State, Entry.Token);
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                BeginRename(State, Entry);
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                if (!SelectionContains(State, Entry.Token)) { SelectOnly(State, Entry.Token); }
                State.ContextMenuTarget = Entry.Token;
                State.ContextMenuRequested = true;
                // The menu opens from the click itself, then folds back inside the panel's confinement band (see BeginDropdownPopup).
                State.MenuOriginX = ImGui::GetIO().MousePos.x;
                State.MenuOriginY = ImGui::GetIO().MousePos.y;
            }
        }

        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(CursorTop.x, CursorTop.y + RowHeight));
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      CHROME + MENUS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 A small header / chrome icon button drawn with our own tones. Returns true if its hit-zone was clicked. These few glyphs (add / filter)
    //    are pure strokes — they are chrome, not classification icons, so they don't route through the SVG registry.
    bool ConstructIconButton(ImDrawList* DrawList, ImVec2 Centre, const char* Glyph, ImU32 Tint)
    {
        ImVec2 Min = ImVec2(Centre.x - 11.0f, Centre.y - 11.0f);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Glyph);
        ImGui::InvisibleButton("##iconbtn", ImVec2(22.0f, 22.0f));
        const bool Pressed = ImGui::IsItemClicked();
        if (ImGui::IsItemHovered()) { DrawList->AddRectFilled(Min, ImVec2(Min.x + 22.0f, Min.y + 22.0f), Palette.MenuHover, 5.0f); }
        ImGui::PopID();

        if (std::strcmp(Glyph, "add") == 0)
        {
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - 5.0f), ImVec2(Centre.x, Centre.y + 5.0f), Tint, 1.5f);
            DrawList->AddLine(ImVec2(Centre.x - 5.0f, Centre.y), ImVec2(Centre.x + 5.0f, Centre.y), Tint, 1.5f);
        }
        else if (std::strcmp(Glyph, "filter") == 0)
        {
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y - 5.0f), ImVec2(Centre.x + 6.0f, Centre.y - 5.0f), Tint, 1.3f);
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y - 5.0f), ImVec2(Centre.x - 1.0f, Centre.y + 1.0f), Tint, 1.3f);
            DrawList->AddLine(ImVec2(Centre.x + 6.0f, Centre.y - 5.0f), ImVec2(Centre.x + 1.0f, Centre.y + 1.0f), Tint, 1.3f);
            DrawList->AddLine(ImVec2(Centre.x - 1.0f, Centre.y + 1.0f), ImVec2(Centre.x - 1.0f, Centre.y + 5.0f), Tint, 1.3f);
            DrawList->AddLine(ImVec2(Centre.x + 1.0f, Centre.y + 1.0f), ImVec2(Centre.x + 1.0f, Centre.y + 3.0f), Tint, 1.3f);
        }
        return Pressed;
    }

    enum class ItemMarker { None, Radio, RadioFilled, Check };

    // 📝 One dropdown-list item: a lighter-grey hover fill with a full-height blue accent bar, an eased 14→20px text indent on hover, an
    //    optional icon (a real SVG glyph resolved from the registry by IconKey — procedural fallback when absent), and an optional trailing
    //    marker. LeadSwatch != 0 draws a colour dot in the glyph slot (COLOURS facet) instead of an icon. TextOverride != 0 forces the label
    //    colour (danger-red Delete).
    bool ConstructDropdownItem(const OutlinerContentProfile& Profile, const Frontier::SvgIconRegistry* Registry, const char* Label,
                               const std::string& IconKey, ImU32 GlyphTint,
                               ItemMarker Marker, bool Active, ImU32 LeadSwatch = 0, ImU32 TextOverride = 0)
    {
        ImDrawList* Draw   = ImGui::GetWindowDrawList();
        const float AvailW = ImGui::GetContentRegionAvail().x;
        const float ItemH  = 30.0f;
        const ImVec2 ItemMin = ImGui::GetCursorScreenPos();
        const ImVec2 ItemMax(ItemMin.x + AvailW, ItemMin.y + ItemH);

        const bool Clicked = ImGui::InvisibleButton("##dditem", ImVec2(AvailW, ItemH));
        const bool Hovered = ImGui::IsItemHovered();

        ImGuiStorage* Store = ImGui::GetStateStorage();
        const ImGuiID  Key  = ImGui::GetItemID();
        float Ease = Store->GetFloat(Key, 0.0f);
        const float Rate = ImGui::GetIO().DeltaTime * 9.0f;
        Ease += ((Hovered ? 1.0f : 0.0f) - Ease) * (Rate < 1.0f ? Rate : 1.0f);
        Store->SetFloat(Key, Ease);

        if (Ease > 0.01f)
        {
            ImU32 Fill = (Palette.RowActive & 0x00FFFFFF) | ((ImU32)(Ease * 255.0f) << 24);
            Draw->AddRectFilled(ItemMin, ItemMax, Fill, 0.0f);
            ImU32 Bar = (Palette.Accent & 0x00FFFFFF) | ((ImU32)(Ease * 255.0f) << 24);
            Draw->AddRectFilled(ItemMin, ImVec2(ItemMin.x + 4.0f, ItemMax.y), Bar, 0.0f);
        }

        const float IndentX = 14.0f + Ease * 6.0f;    // hover padding-left 14→20px
        float PenX = ItemMin.x + IndentX;
        const float CentreY = (ItemMin.y + ItemMax.y) * 0.5f;

        if (LeadSwatch != 0)
        {
            Draw->AddCircleFilled(ImVec2(PenX + 7.0f, CentreY), 5.0f, LeadSwatch);
            PenX += 24.0f;
        }
        else if (!IconKey.empty())
        {
            ConstructIcon(Draw, Profile, Registry, IconKey, ImVec2(PenX, CentreY - 9.0f), GlyphTint);
            PenX += 24.0f;
        }

        const ImU32 TextColor = TextOverride != 0 ? TextOverride : (Active ? Palette.Accent : Palette.TextPrimary);
        Draw->AddText(ImVec2(PenX, CentreY - ImGui::GetFontSize() * 0.5f), TextColor, Label);

        if (Marker != ItemMarker::None)
        {
            const float  R = 7.0f;
            const ImVec2 MarkC(ItemMax.x - 14.0f - R, CentreY);
            if (Marker == ItemMarker::Check)
            {
                if (Active)
                {
                    Draw->AddLine(ImVec2(MarkC.x - 4.5f, MarkC.y + 0.5f), ImVec2(MarkC.x - 1.0f, MarkC.y + 4.0f), Palette.Accent, 1.8f);
                    Draw->AddLine(ImVec2(MarkC.x - 1.0f, MarkC.y + 4.0f), ImVec2(MarkC.x + 5.0f, MarkC.y - 3.5f), Palette.Accent, 1.8f);
                }
            }
            else
            {
                const bool Filled = (Marker == ItemMarker::RadioFilled);
                Draw->AddCircle(MarkC, R, Filled ? Palette.Accent : Palette.BorderLine, 20, Filled ? 2.0f : 1.5f);
                if (Filled) { Draw->AddCircleFilled(MarkC, R - 3.5f, Palette.Accent, 20); }
            }
        }
        return Clicked;
    }

    // 📝 A faint section label inside a dropdown list (Sketch / Features / Datums ...).
    void ConstructDropdownSection(const char* Text)
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        Draw->AddText(ImVec2(Pos.x + 12.0f, Pos.y + 4.0f), Palette.TextFaint, Text);
        ImGui::Dummy(ImVec2(0.0f, 20.0f));
    }

    bool BeginDropdownPopup(const SketchOutlinerState& State, const char* Id, float Width);
    void EndDropdownPopup();

    void ConstructDropdownDivider()
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        const float  W   = ImGui::GetContentRegionAvail().x;
        Draw->AddLine(ImVec2(Pos.x, Pos.y + 4.0f), ImVec2(Pos.x + W, Pos.y + 4.0f), Palette.BorderLine, 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    void ConstructContextMenu(SketchOutlinerState& State, const OutlinerContentProfile& Profile, const Frontier::SvgIconRegistry* Registry)
    {
        if (State.ContextMenuRequested)
        {
            ImGui::OpenPopup("##sketch-ctx");
            State.ContextMenuRequested = false;
        }
        if (BeginDropdownPopup(State, "##sketch-ctx", 210.0f))
        {
            RecordToken Target = State.ContextMenuTarget;
            ImGui::PushID("addobj");
            if (ConstructDropdownItem(Profile, Registry, "Add Object", "", Palette.TextDim, ItemMarker::None, false))
            {
                // 📝 MenuOriginX/Y is deliberately left alone: the add menu replaces this one, so it opens from the same point the right-click did.
                State.AddMenuRequested = true;
                State.AddMenuAnchor = Target;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            ConstructDropdownDivider();
            ImGui::PushID("rename");
            if (ConstructDropdownItem(Profile, Registry, "Rename", "", 0, ItemMarker::None, false))
            {
                RecordEntry* Entry = ResolveEntry(State.RootRegion, Target);
                if (Entry != nullptr) { SelectOnly(State, Target); BeginRename(State, *Entry); }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            ImGui::PushID("duplicate");
            if (ConstructDropdownItem(Profile, Registry, "Duplicate", "", 0, ItemMarker::None, false)) { DuplicateTargets(State, Target); ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
            ImGui::PushID("visibility");
            if (ConstructDropdownItem(Profile, Registry, "Toggle Visibility", "", 0, ItemMarker::None, false)) { ToggleConcealment(State, Target); ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
            // 📝 "Group into X" — the label / icon / tint come from the profile's grouping container row so the action names no classification.
            const OutlinerAddRow* GroupingRow = ResolveGroupingRow(Profile);
            if (GroupingRow != nullptr)
            {
                const OutlinerClassRow* GroupingClass = ResolveClassRow(Profile, GroupingRow->ClassificationId);
                char GroupLabel[64];
                std::snprintf(GroupLabel, sizeof(GroupLabel), "Group into %s", GroupingRow->Label);
                const char* GroupIcon = GroupingClass != nullptr ? GroupingClass->IconKey : "";
                const ImU32 GroupTint = GroupingClass != nullptr ? GroupingClass->Tint : Palette.TextDim;
                ImGui::PushID("group");
                if (ConstructDropdownItem(Profile, Registry, GroupLabel, GroupIcon, GroupTint, ItemMarker::None, false)) { GroupTargets(State, Profile, Target); ImGui::CloseCurrentPopup(); }
                ImGui::PopID();
            }
            ConstructDropdownDivider();
            ImGui::PushID("delete");
            if (ConstructDropdownItem(Profile, Registry, "Delete", "", 0, ItemMarker::None, false, 0, Palette.Danger)) { DeleteTargets(State, Target); ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
            EndDropdownPopup();
        }
    }

    // 📝 Shared popup framing for the dropdown-style menus: menu fill, hairline border, tight window padding and a small inter-item gap so the
    //    rows read as one continuous list.
    //
    //    Placement is resolved HERE rather than left to ImGui. Left alone, a popup lands at the mouse and is clamped only to the viewport, so one
    //    opened near the panel's right or bottom edge spills outside the box and reads as belonging to whatever sits beside it. Instead the menu is
    //    pinned with SetNextWindowPos at the recorded open point and folded back inside the confinement band: it flips to the far side of the origin
    //    when it would overrun an edge, then clamps. The height needed for that decision comes from the previous frame's measured window (the popup
    //    auto-resizes, so frame one has none) — ImGui hides a just-opened auto-resize popup for exactly one frame while it measures, so the flip is
    //    settled before anything is visible.
    bool BeginDropdownPopup(const SketchOutlinerState& State, const char* Id, float Width)
    {
        ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));

        const bool Confined = State.ConfineRight > State.ConfineLeft && State.ConfineBottom > State.ConfineTop;
        if (Confined)
        {
            const float Inset = 4.0f;
            const float BandLeft   = State.ConfineLeft   + Inset;
            const float BandTop    = State.ConfineTop    + Inset;
            const float BandRight  = State.ConfineRight  - Inset;
            const float BandBottom = State.ConfineBottom - Inset;

            // A popup keeps its window across opens, so last frame's measured height is the best estimate; fall back to a modest guess when unknown.
            char PopupName[24];
            std::snprintf(PopupName, sizeof(PopupName), "##Popup_%08x", ImGui::GetID(Id));
            const ImGuiWindow* Previous = ImGui::FindWindowByName(PopupName);
            const float MenuHeight = (Previous != nullptr && Previous->SizeFull.y > 1.0f) ? Previous->SizeFull.y : 180.0f;

            float MenuX = State.MenuOriginX;
            float MenuY = State.MenuOriginY;
            if (MenuX + Width > BandRight)       { MenuX = State.MenuOriginX - Width; }
            if (MenuY + MenuHeight > BandBottom) { MenuY = State.MenuOriginY - MenuHeight; }
            MenuX = ImClamp(MenuX, BandLeft, ImMax(BandLeft, BandRight - Width));
            MenuY = ImClamp(MenuY, BandTop,  ImMax(BandTop,  BandBottom - MenuHeight));

            ImGui::SetNextWindowPos(ImVec2(MenuX, MenuY));
            // 🔴 A tall menu inside a short panel must scroll, not overflow the band — without a ceiling the auto-resize would grow straight past it.
            ImGui::SetNextWindowSizeConstraints(ImVec2(Width, 0.0f), ImVec2(Width, ImMax(48.0f, BandBottom - BandTop)));
        }

        ImGui::PushStyleColor(ImGuiCol_PopupBg, Palette.MenuFill);
        ImGui::PushStyleColor(ImGuiCol_Border, Palette.MenuBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        const bool Open = ImGui::BeginPopup(Id);
        if (!Open) { ImGui::PopStyleVar(3); ImGui::PopStyleColor(2); }
        return Open;
    }

    void EndDropdownPopup()
    {
        ImGui::EndPopup();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    void ConstructAddMenu(SketchOutlinerState& State, const OutlinerContentProfile& Profile, const Frontier::SvgIconRegistry* Registry)
    {
        if (State.AddMenuRequested)
        {
            ImGui::OpenPopup("##sketch-add");
            State.AddMenuRequested = false;
        }
        if (BeginDropdownPopup(State, "##sketch-add", 230.0f))
        {
            for (int Index = 0; Index < Profile.AddRowCount; ++Index)
            {
                const OutlinerAddRow& Row = Profile.AddRows[Index];
                if (Row.Section != nullptr)
                {
                    ConstructDropdownSection(Row.Section);
                    continue;
                }
                // 📝 The item's icon / tint come from its ClassRows entry, so the add menu names no glyph — only the classification id.
                const OutlinerClassRow* Class = ResolveClassRow(Profile, Row.ClassificationId);
                const char* IconKey = Class != nullptr ? Class->IconKey : "";
                const ImU32 Tint    = Class != nullptr ? Class->Tint : Palette.TextDim;
                ImGui::PushID(Row.Label);
                if (ConstructDropdownItem(Profile, Registry, Row.Label, IconKey, Tint, ItemMarker::None, false))
                {
                    AppendObject(State, Profile, Row);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            EndDropdownPopup();
        }
    }

    void ConstructFilterMenu(SketchOutlinerState& State, const OutlinerContentProfile& Profile, const Frontier::SvgIconRegistry* Registry)
    {
        if (State.FilterMenuRequested)
        {
            ImGui::OpenPopup("##sketch-filter");
            State.FilterMenuRequested = false;
        }
        if (BeginDropdownPopup(State, "##sketch-filter", 230.0f))
        {
            ConstructDropdownSection("TYPES");
            for (int Index = 0; Index < Profile.FilterClassIdCount; ++Index)
            {
                const int ClassId = Profile.FilterClassIds[Index];
                const OutlinerClassRow* Class = ResolveClassRow(Profile, ClassId);
                if (Class == nullptr) { continue; }
                const bool On = ChipPresent(State, FilterFacet::Classification, ClassId, 0);
                ImGui::PushID(ClassId);
                if (ConstructDropdownItem(Profile, Registry, Class->Label, Class->IconKey, Class->Tint, ItemMarker::Check, On))
                {
                    ToggleChip(State, FilterFacet::Classification, ClassId, 0);
                }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ConstructDropdownSection("COLOURS");
            for (int Index = 0; Index < Profile.TintFacetCount; ++Index)
            {
                const OutlinerTintFacet& Facet = Profile.TintFacets[Index];
                const bool On = ChipPresent(State, FilterFacet::Tint, 0, Facet.Tint);
                ImGui::PushID(Facet.Label);
                if (ConstructDropdownItem(Profile, Registry, Facet.Label, "", 0, ItemMarker::Check, On, Facet.Tint))
                {
                    ToggleChip(State, FilterFacet::Tint, 0, Facet.Tint);
                }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            const bool VisibleOn = ChipPresent(State, FilterFacet::OnlyVisible, 0, 0);
            ImGui::PushID("onlyvisible");
            if (ConstructDropdownItem(Profile, Registry, "Only visible", "", 0, ItemMarker::Check, VisibleOn))
            {
                ToggleChip(State, FilterFacet::OnlyVisible, 0, 0);
            }
            ImGui::PopID();
            EndDropdownPopup();
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      KEYBOARD
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void EnforceKeyboardActions(SketchOutlinerState& State)
    {
        if (State.RenameTarget != 0) { return; }               // rename input owns the keyboard
        ImGuiIO& Io = ImGui::GetIO();
        if (Io.WantTextInput) { return; }                      // search box focused

        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !State.SelectionSet.empty())
        {
            DeleteTargets(State, 0);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_F2) && State.RangeAnchor != 0)
        {
            RecordEntry* Entry = ResolveEntry(State.RootRegion, State.RangeAnchor);
            if (Entry != nullptr) { BeginRename(State, *Entry); }
        }
        else if (Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
        {
            std::vector<RecordToken> Order;
            AccumulateVisibleOrder(State, State.RootRegion, Order);
            State.SelectionSet = Order;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            State.SelectionSet.clear();
            State.RangeAnchor = 0;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConfineSketchOutlinerMenus(SketchOutlinerState& State, float Left, float Top, float Right, float Bottom)
{
    State.ConfineLeft   = Left;
    State.ConfineTop    = Top;
    State.ConfineRight  = Right;
    State.ConfineBottom = Bottom;
}

void ConstructSketchOutlinerPanel(const ThemeConfiguration& Theme, SketchOutlinerState& State, const Frontier::SvgIconRegistry* IconRegistry,
                                  const OutlinerContentProfile& Profile)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    ImVec2 PanelMin = ImGui::GetCursorScreenPos();
    const float PanelWidth = ImGui::GetContentRegionAvail().x;
    const float PanelRight = PanelMin.x + PanelWidth;

    const float HeaderHeight  = 44.0f;
    const float SearchHeight  = 44.0f;
    const float FiltersHeight = 52.0f;
    const float FooterHeight  = 30.0f;
    const float RowHeight     = 32.0f;
    const float IndentWidth   = 22.0f;

    // -- Section header (stacked icon · SKETCH · add · filter) — the single title bar over the parametric part tree -------------------
    ImVec2 HeaderMin = PanelMin;
    ImVec2 HeaderMax = ImVec2(PanelRight, HeaderMin.y + HeaderHeight);
    DrawList->AddRectFilled(HeaderMin, HeaderMax, Palette.HeaderFill);
    DrawList->AddLine(ImVec2(HeaderMin.x, HeaderMax.y), ImVec2(HeaderMax.x, HeaderMax.y), Palette.BorderSoft);
    const float HeaderCentreY = HeaderMin.y + HeaderHeight * 0.5f;
    // 📝 Stacked-plates icon (three offset rhombi) so the header reads "[stacked] SKETCH" like the mockup — decorative, not a button.
    {
        const ImVec2 IconCentre = ImVec2(HeaderMin.x + 20.0f, HeaderCentreY);
        const float  HalfW = 7.5f, HalfH = 4.2f;
        for (int Plate = 0; Plate < 3; ++Plate)
        {
            const float OffsetY = (float)(Plate - 1) * 4.4f;
            const ImVec2 Cy = ImVec2(IconCentre.x, IconCentre.y + OffsetY);
            const ImU32 PlateTint = (Plate == 0) ? Palette.TextPrimary : Palette.TextDim;
            DrawList->AddQuadFilled(ImVec2(Cy.x, Cy.y - HalfH), ImVec2(Cy.x + HalfW, Cy.y),
                                    ImVec2(Cy.x, Cy.y + HalfH), ImVec2(Cy.x - HalfW, Cy.y), PlateTint);
        }
    }
    DrawList->AddText(ImVec2(HeaderMin.x + 38.0f, HeaderCentreY - ImGui::GetFontSize() * 0.5f), Palette.TextPrimary,
                      Profile.HeaderCaption != nullptr ? Profile.HeaderCaption : "OUTLINER");
    // 📝 Both header menus hang from just under the header strip rather than from the mouse, so they read as dropped out of the icon they belong to.
    //    BeginDropdownPopup folds them leftward and clamps them into the panel band, so neither can escape the box however narrow it is.
    if (ConstructIconButton(DrawList, ImVec2(PanelRight - 44.0f, HeaderCentreY), "add", Palette.TextDim))
    {
        State.AddMenuRequested = true;
        State.AddMenuAnchor = State.RangeAnchor;
        State.MenuOriginX = PanelRight - 44.0f;
        State.MenuOriginY = HeaderMax.y;
    }
    if (ConstructIconButton(DrawList, ImVec2(PanelRight - 18.0f, HeaderCentreY), "filter", Palette.TextDim))
    {
        State.FilterMenuRequested = true;
        State.MenuOriginX = PanelRight - 18.0f;
        State.MenuOriginY = HeaderMax.y;
    }

    // -- Search ---------------------------------------------------------------------------------------------------------
    ImVec2 SearchMin = ImVec2(PanelMin.x, HeaderMax.y);
    ImVec2 SearchMax = ImVec2(PanelRight, SearchMin.y + SearchHeight);
    DrawList->AddRectFilled(SearchMin, SearchMax, Palette.DeepBackground);
    DrawList->AddLine(ImVec2(SearchMin.x, SearchMax.y), ImVec2(SearchMax.x, SearchMax.y), Palette.BorderSoft);
    {
        ImVec2 BoxMin = ImVec2(SearchMin.x + 12.0f, SearchMin.y + 8.0f);
        ImVec2 BoxMax = ImVec2(SearchMax.x - 12.0f, SearchMax.y - 8.0f);
        DrawList->AddRectFilled(BoxMin, BoxMax, Palette.SearchFill, 8.0f);
        DrawList->AddRect(BoxMin, BoxMax, Palette.BorderLine, 8.0f, 0, 1.0f);
        ImVec2 SearchIcon = ImVec2(BoxMin.x + 11.0f, (BoxMin.y + BoxMax.y) * 0.5f);
        DrawList->AddCircle(ImVec2(SearchIcon.x - 1.5f, SearchIcon.y - 1.5f), 4.5f, Palette.TextFaint, 0, 1.3f);
        DrawList->AddLine(ImVec2(SearchIcon.x + 2.0f, SearchIcon.y + 2.0f), ImVec2(SearchIcon.x + 5.0f, SearchIcon.y + 5.0f), Palette.TextFaint, 1.3f);
        ImGui::SetCursorScreenPos(ImVec2(BoxMin.x + 26.0f, BoxMin.y + 3.0f));
        ImGui::SetNextItemWidth(BoxMax.x - BoxMin.x - 34.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextPrimary);
        ImGui::InputTextWithHint("##search", Profile.SearchHint != nullptr ? Profile.SearchHint : "Filter...",
                                 State.SearchText, sizeof(State.SearchText));
        ImGui::PopStyleColor(2);
    }

    // -- Filter chips ---------------------------------------------------------------------------------------------------
    ImVec2 FiltersMin = ImVec2(PanelMin.x, SearchMax.y);
    ImVec2 FiltersMax = ImVec2(PanelRight, FiltersMin.y + FiltersHeight);
    DrawList->AddRectFilled(FiltersMin, FiltersMax, Palette.HeaderFill);
    DrawList->AddLine(ImVec2(FiltersMin.x, FiltersMax.y), ImVec2(FiltersMax.x, FiltersMax.y), Palette.BorderSoft);
    {
        // 📝 Label row sits at the top; the chip / +Filters row sits ~22px lower so the two never touch. The taller FiltersHeight (52)
        //    gives the second row room to breathe.
        ImGui::SetCursorScreenPos(ImVec2(FiltersMin.x + 12.0f, FiltersMin.y + 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextFaint);
        ImGui::TextUnformatted("FILTERS");
        ImGui::PopStyleColor();
        if (!State.FilterChips.empty())
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, Palette.Accent);
            ImGui::Text("%d", (int)State.FilterChips.size());
            ImGui::PopStyleColor();
            ImGui::SameLine(FiltersMax.x - FiltersMin.x - 66.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, Palette.Danger);
            if (ImGui::SmallButton("Clear all")) { State.FilterChips.clear(); }
            ImGui::PopStyleColor();
        }

        // 📝 One removable chip per active filter, laid out left-to-right; clicking a chip removes it.
        ImGui::SetCursorScreenPos(ImVec2(FiltersMin.x + 12.0f, FiltersMin.y + 28.0f));
        for (std::size_t Index = 0; Index < State.FilterChips.size(); ++Index)
        {
            const FilterChip& Chip = State.FilterChips[Index];
            const char* Label = Chip.Facet == FilterFacet::Classification ? ClassificationLabel(Profile, Chip.ClassificationValue)
                              : Chip.Facet == FilterFacet::Tint            ? TintLabel(Profile, Chip.TintValue)
                                                                          : "Only visible";
            ImGui::PushID((int)(Index + 1));
            ImGui::PushStyleColor(ImGuiCol_Button, Palette.SearchFill);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Palette.RowActive);
            ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextPrimary);
            char ChipText[64];
            std::snprintf(ChipText, sizeof(ChipText), "%s  x", Label);
            if (ImGui::SmallButton(ChipText)) { State.FilterChips.erase(State.FilterChips.begin() + Index); ImGui::PopStyleColor(3); ImGui::PopID(); break; }
            ImGui::PopStyleColor(3);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextDim);
        if (ImGui::SmallButton("+ Filters"))
        {
            State.FilterMenuRequested = true;
            const ImVec2 ChipMin = ImGui::GetItemRectMin();
            State.MenuOriginX = ChipMin.x;
            State.MenuOriginY = ImGui::GetItemRectMax().y + 2.0f;
        }
        ImGui::PopStyleColor(2);
    }

    // -- Tree region (scrollable) ---------------------------------------------------------------------------------------
    ImVec2 TreeMin = ImVec2(PanelMin.x, FiltersMax.y);
    float TreeBottom = PanelMin.y + ImGui::GetWindowSize().y - FooterHeight;
    ImVec2 TreeMax = ImVec2(PanelRight, TreeBottom);
    DrawList->AddRectFilled(TreeMin, TreeMax, Palette.DeepBackground);

    const float ViewportHeight = TreeMax.y - TreeMin.y;
    const float TopPadding     = 6.0f;

    ImGui::SetCursorScreenPos(TreeMin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    // 📝 ImGui's own scroll is disabled — we translate the rows ourselves so we can (a) ease the offset for a lagging, non-snappy feel and
    //    (b) push slightly PAST the ends for a rubber-band overscroll that springs back. The whole tree is clipped to the viewport band.
    ImGui::BeginChild("##sketch-tree", ImVec2(PanelWidth, ViewportHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // 📝 Feed the wheel into a scroll TARGET while the tree is hovered; ScrollActual chases it (the lag). One notch ≈ 3 rows.
    if (ImGui::IsWindowHovered())
    {
        const float Wheel = ImGui::GetIO().MouseWheel;
        if (Wheel != 0.0f) { State.ScrollTarget -= Wheel * RowHeight * 3.0f; }
    }

    RowContext Context = {};
    Context.Theme        = &Theme;
    Context.State        = &State;
    Context.Profile      = &Profile;
    Context.DrawList     = ImGui::GetWindowDrawList();
    Context.IconRegistry = IconRegistry;
    Context.RowHeight    = RowHeight;
    Context.IndentWidth  = IndentWidth;
    Context.ContentLeft  = TreeMin.x;
    Context.ContentRight = PanelRight;

    const bool Filtering = AnyFilterActive(State);
    std::vector<RecordEntry>& Roots = State.RootRegion;
    bool AnyVisible = false;
    for (const RecordEntry& Entry : Roots) { if (!Filtering || RegionMatches(State, Entry)) { AnyVisible = true; break; } }

    // 📝 Full drawn extent so we know how far the scroll may travel (and thus where overscroll begins). Measured before drawing so the
    //    rubber-band clamp this frame uses the same content the rows will occupy.
    const float ContentHeight = TopPadding + (Filtering && !AnyVisible ? 36.0f : MeasureRegionHeight(Context, Roots));
    const float MaxScroll     = ContentHeight - ViewportHeight > 0.0f ? ContentHeight - ViewportHeight : 0.0f;

    // 📝 Rubber-band the TARGET back inside [0, MaxScroll]: when the wheel has pushed it past an end, spring it home. The pull is eased so
    //    the content drifts back rather than snapping. OverscrollLimit caps how far the band can stretch so a fast flick can't fling it away.
    const float OverscrollLimit = RowHeight * 3.0f;
    State.ScrollTarget = ImClamp(State.ScrollTarget, -OverscrollLimit, MaxScroll + OverscrollLimit);
    const float Rate = ImGui::GetIO().DeltaTime;
    if (State.ScrollTarget < 0.0f)             { State.ScrollTarget += (0.0f - State.ScrollTarget) * ImMin(Rate * 12.0f, 1.0f); if (State.ScrollTarget > -0.4f) State.ScrollTarget = 0.0f; }
    else if (State.ScrollTarget > MaxScroll)   { State.ScrollTarget += (MaxScroll - State.ScrollTarget) * ImMin(Rate * 12.0f, 1.0f); if (State.ScrollTarget < MaxScroll + 0.4f) State.ScrollTarget = MaxScroll; }

    // 📝 Ease the rendered offset toward the target — this is the "scroll lag". Snap once within a sub-pixel to settle cleanly.
    State.ScrollActual += (State.ScrollTarget - State.ScrollActual) * ImMin(Rate * 14.0f, 1.0f);
    if (ImFabs(State.ScrollTarget - State.ScrollActual) < 0.4f) { State.ScrollActual = State.ScrollTarget; }

    // 📝 Translate the whole tree up by the eased offset (overscroll included) and clip to the viewport band.
    Context.DrawList->PushClipRect(TreeMin, TreeMax, true);
    ImGui::SetCursorScreenPos(ImVec2(TreeMin.x, TreeMin.y + TopPadding - State.ScrollActual));

    if (Filtering && !AnyVisible)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextFaint);
        ImGui::SetCursorScreenPos(ImVec2(TreeMin.x + (PanelWidth - ImGui::CalcTextSize("No objects match your filter.").x) * 0.5f,
                                         TreeMin.y + TopPadding + 18.0f - State.ScrollActual));
        ImGui::TextUnformatted("No objects match your filter.");
        ImGui::PopStyleColor();
    }
    else
    {
        ConstructRegionRows(Context, Roots, 0);
    }
    Context.DrawList->PopClipRect();

    // 🔴 An EMPTY tree submits no row, yet the cursor was moved to TreeMin.y + TopPadding (line above) to seat the first row — that move alone
    //    extends the child's content boundary, which ImGui asserts on unless an item follows. A zero-size Dummy at the current cursor legitimizes
    //    the extend without drawing anything (and is a harmless no-op when rows WERE submitted). Before the empty inspector pose every consumer
    //    seeded content, so this path was never exercised.
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    // 📝 Clicking empty tree space clears the selection.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
    {
        State.SelectionSet.clear();
        State.RangeAnchor = 0;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // -- Footer ---------------------------------------------------------------------------------------------------------
    ImVec2 FooterMin = ImVec2(PanelMin.x, TreeMax.y);
    ImVec2 FooterMax = ImVec2(PanelRight, FooterMin.y + FooterHeight);
    DrawList->AddRectFilled(FooterMin, FooterMax, Palette.StripBackground);
    DrawList->AddLine(ImVec2(FooterMin.x, FooterMin.y), ImVec2(FooterMax.x, FooterMin.y), Palette.BorderSoft);
    const float FooterCentreY = FooterMin.y + FooterHeight * 0.5f;
    DrawList->AddCircleFilled(ImVec2(FooterMin.x + 16.0f, FooterCentreY), 3.0f, Palette.LiveDot);
    DrawList->AddText(ImVec2(FooterMin.x + 26.0f, FooterCentreY - ImGui::GetFontSize() * 0.5f), Palette.TextFaint, "Live");
    {
        char Readout[48];
        if (State.SelectionSet.size() > 1) { std::snprintf(Readout, sizeof(Readout), "%d selected", (int)State.SelectionSet.size()); }
        else { std::snprintf(Readout, sizeof(Readout), "%d objects", AccumulateLeafCount(State.RootRegion)); }
        float TextWidth = ImGui::CalcTextSize(Readout).x;
        DrawList->AddText(ImVec2(FooterMax.x - 14.0f - TextWidth, FooterCentreY - ImGui::GetFontSize() * 0.5f), Palette.TextFaint, Readout);
    }

    // -- Menus + keyboard -----------------------------------------------------------------------------------------------
    ConstructContextMenu(State, Profile, IconRegistry);
    ConstructAddMenu(State, Profile, IconRegistry);
    ConstructFilterMenu(State, Profile, IconRegistry);
    EnforceKeyboardActions(State);
}

}   // namespace Frontier::SketchOutlinerUi
