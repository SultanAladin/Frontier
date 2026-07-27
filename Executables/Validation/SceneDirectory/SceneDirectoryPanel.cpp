/*==============================================================================================================================================
                                                            SCENEDIRECTORYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Reproduces Outliner.html with the real Interface theme, entirely custom-drawn through ImDrawList so the rows, twisties, tinted
//    classification icons, filter chips and menus match the mockup one-to-one. The shared theme supplies the base panel / text / border
//    tones; a small outliner-local palette adds the blue selection accent and the per-classification icon tints that the HTML carries in
//    its own :root block. Nothing here is app-specific — it is a pure showcase over SceneDirectoryState. All of it lives in the app-local
//    namespace SceneDirectoryValidation so its RecordEntry / RecordToken never collide with the pillar's Frontier::RecordEntry.

#include "SceneDirectoryPanel.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstring>

using Frontier::ThemeConfiguration;

namespace SceneDirectoryValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      OUTLINER PALETTE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Tones lifted from Outliner.html :root that the shared theme does not carry (blue accent, faint text, chip / menu fills). The base
    //    panel / border / text tones still come from the resolved theme so a global palette swap re-tints the outliner too.
    struct OutlinerPalette
    {
        ImU32 DeepBackground  = IM_COL32( 0x0a, 0x0a, 0x0b, 255);   // --bg-panel
        ImU32 StripBackground = IM_COL32( 0x08, 0x08, 0x0a, 255);   // tab strip / footer fill
        ImU32 HeaderFill      = IM_COL32( 0x11, 0x11, 0x13, 255);   // section-header + filters fill
        ImU32 SearchFill      = IM_COL32( 0x14, 0x14, 0x16, 255);   // search / chip fill
        ImU32 BorderSoft      = IM_COL32( 0x16, 0x16, 0x18, 255);   // --border-soft
        ImU32 BorderLine      = IM_COL32( 0x1c, 0x1c, 0x20, 255);   // --border
        ImU32 RowHover        = IM_COL32( 0x16, 0x16, 0x18, 255);   // --row-hover
        ImU32 RowActive       = IM_COL32( 0x24, 0x24, 0x28, 255);   // --row-active
        ImU32 MenuFill        = IM_COL32( 0x14, 0x14, 0x16, 255);   // ctx-menu / add-menu fill
        ImU32 MenuBorder      = IM_COL32( 0x2a, 0x2a, 0x30, 255);   // ctx-menu border
        ImU32 MenuHover       = IM_COL32( 0x22, 0x22, 0x2a, 255);   // ctx-item hover
        ImU32 TextPrimary     = IM_COL32( 0xd8, 0xd8, 0xdc, 255);   // --text
        ImU32 TextDim         = IM_COL32( 0x8a, 0x8a, 0x92, 255);   // --text-dim
        ImU32 TextFaint       = IM_COL32( 0x5a, 0x5a, 0x62, 255);   // --text-faint
        ImU32 Accent          = IM_COL32( 0x3a, 0x7b, 0xd5, 255);   // --accent (blue)
        ImU32 AccentSoft      = IM_COL32( 0x3a, 0x7b, 0xd5,  38);   // drop-target tint
        ImU32 Danger          = IM_COL32( 0xe0, 0x5a, 0x5a, 255);   // delete / clear-all
        ImU32 LiveDot         = IM_COL32( 0x3a, 0xd0, 0x7a, 255);   // footer "Live" dot
    };

    const OutlinerPalette Palette;

    // -- Per-classification icon tints (mirror the SVG stroke colours in Outliner.html) --
    ImU32 TintScene       = IM_COL32(0xc9, 0xc9, 0xcf, 255);
    ImU32 TintEnvironment = IM_COL32(0x4f, 0xb0, 0xe0, 255);
    ImU32 TintCamera      = IM_COL32(0x5a, 0x95, 0xdd, 255);
    ImU32 TintSun         = IM_COL32(0xe0, 0xb6, 0x4f, 255);
    ImU32 TintArea        = IM_COL32(0xc7, 0x74, 0xe0, 255);
    ImU32 TintSurface     = IM_COL32(0xb9, 0xb9, 0xc0, 255);
    ImU32 TintFolder      = IM_COL32(0xd0, 0x8a, 0x4f, 255);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SAMPLE CONTENT
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Issue the next stable token and stamp it onto a freshly built entry.
    RecordEntry ConstructEntry(SceneDirectoryState&  State,
                               const char*           Label,
                               RecordClassification  Classification,
                               const char*           IconGlyph,
                               ImU32                 TintColor,
                               bool                  ExpandedState)
    {
        RecordEntry Entry;
        Entry.Token          = State.NextToken++;
        Entry.Label          = Label;
        Entry.Classification = Classification;
        Entry.IconGlyph      = IconGlyph;
        Entry.TintColor      = TintColor;
        Entry.ExpandedState  = ExpandedState;
        Entry.ConcealedState = false;
        return Entry;
    }
}


void InitializeSceneDirectorySample(SceneDirectoryState& State)
{
    State.RootRegion.clear();
    State.SelectionSet.clear();

    RecordEntry Scene = ConstructEntry(State, "Scene", RecordClassification::SceneRoot, "scene", TintScene, true);

    RecordEntry Environment = ConstructEntry(State, "Environment", RecordClassification::EnclosureFolder, "environment", TintEnvironment, true);
    Environment.NestedRegion.push_back(ConstructEntry(State, "HDRI_Studio", RecordClassification::EnvironmentDome, "hdri", TintEnvironment, false));

    RecordEntry Cameras = ConstructEntry(State, "Cameras", RecordClassification::EnclosureFolder, "cameras", TintCamera, true);
    Cameras.NestedRegion.push_back(ConstructEntry(State, "Camera_Main", RecordClassification::CameraLens, "camera", TintCamera, false));

    RecordEntry Lights = ConstructEntry(State, "Lights", RecordClassification::EnclosureFolder, "lights", TintSun, true);
    Lights.NestedRegion.push_back(ConstructEntry(State, "Sun_Key", RecordClassification::LightEmitter, "sun", TintSun, false));
    Lights.NestedRegion.push_back(ConstructEntry(State, "Area_Softbox", RecordClassification::LightEmitter, "area", TintArea, false));

    RecordEntry Geometry = ConstructEntry(State, "Geometry", RecordClassification::EnclosureFolder, "geometry", TintSurface, true);
    RecordEntry CarGroup = ConstructEntry(State, "Car", RecordClassification::EnclosureFolder, "folder", TintFolder, true);
    CarGroup.NestedRegion.push_back(ConstructEntry(State, "SM_Body", RecordClassification::PolygonSurface, "mesh", TintSurface, false));
    CarGroup.NestedRegion.push_back(ConstructEntry(State, "SM_Wheels", RecordClassification::PolygonSurface, "mesh", TintSurface, false));
    Geometry.NestedRegion.push_back(std::move(CarGroup));

    Scene.NestedRegion.push_back(std::move(Environment));
    Scene.NestedRegion.push_back(std::move(Cameras));
    Scene.NestedRegion.push_back(std::move(Lights));
    Scene.NestedRegion.push_back(std::move(Geometry));

    State.RootRegion.push_back(std::move(Scene));

    // 📝 Pre-select SM_Body so the panel opens with a real selection, matching the mockup's boot behaviour.
    RecordToken BodyToken = 0;
    std::vector<RecordEntry>* Pending = &State.RootRegion;
    (void)Pending;
    // Locate SM_Body via a small local recursion.
    struct Locator
    {
        static RecordToken Resolve(const std::vector<RecordEntry>& Region)
        {
            for (const RecordEntry& Entry : Region)
            {
                if (Entry.Label == "SM_Body") { return Entry.Token; }
                RecordToken Found = Resolve(Entry.NestedRegion);
                if (Found != 0) { return Found; }
            }
            return 0;
        }
    };
    BodyToken = Locator::Resolve(State.RootRegion);
    if (BodyToken != 0)
    {
        State.SelectionSet.push_back(BodyToken);
        State.RangeAnchor = BodyToken;
    }
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

    bool SelectionContains(const SceneDirectoryState& State, RecordToken Target)
    {
        return std::find(State.SelectionSet.begin(), State.SelectionSet.end(), Target) != State.SelectionSet.end();
    }

    // 📝 Is Candidate nested anywhere inside Ancestor's region (so a relocation into one's own subtree can be rejected)?
    bool NestedWithin(RecordEntry& Ancestor, RecordToken Candidate)
    {
        for (RecordEntry& Entry : Ancestor.NestedRegion)
        {
            if (Entry.Token == Candidate) { return true; }
            if (NestedWithin(Entry, Candidate)) { return true; }
        }
        return false;
    }

    bool ContainerClassification(RecordClassification Classification)
    {
        return Classification == RecordClassification::SceneRoot || Classification == RecordClassification::EnclosureFolder;
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
    bool AnyFilterActive(const SceneDirectoryState& State)
    {
        return State.SearchText[0] != '\0' || !State.FilterChips.empty();
    }

    // 📝 Case-insensitive substring test of the search box against a label.
    bool MatchesSearch(const SceneDirectoryState& State, const RecordEntry& Entry)
    {
        if (State.SearchText[0] == '\0') { return true; }
        std::string Needle = State.SearchText;
        std::transform(Needle.begin(), Needle.end(), Needle.begin(), [](unsigned char Ch){ return (char)std::tolower(Ch); });
        std::string Hay = Entry.Label;
        std::transform(Hay.begin(), Hay.end(), Hay.begin(), [](unsigned char Ch){ return (char)std::tolower(Ch); });
        return Hay.find(Needle) != std::string::npos;
    }

    // 📝 Does the entry itself satisfy every active chip? Chips of the same facet are OR'd; different facets are AND'd (mirrors the mockup).
    bool MatchesChips(const SceneDirectoryState& State, const RecordEntry& Entry)
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
                if (Entry.Classification == Chip.ClassificationValue) { ClassificationMatched = true; }
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

    bool SelfMatches(const SceneDirectoryState& State, const RecordEntry& Entry)
    {
        return MatchesChips(State, Entry) && MatchesSearch(State, Entry);
    }

    // 📝 Keep an entry if it OR any nested item matches, so containers of a match stay reachable.
    bool RegionMatches(const SceneDirectoryState& State, const RecordEntry& Entry)
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
//                                                      ICON GLYPHS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Draw a small classification glyph centred in an 18px box at Origin, tinted by TintColor. Each branch is a compact ImDrawList
    //    rendition of the matching SVG in Outliner.html — enough to read the item type at a glance without shipping an icon atlas.
    void ConstructGlyph(ImDrawList* DrawList, const char* Glyph, ImVec2 Origin, ImU32 TintColor)
    {
        const float Box = 18.0f;
        ImVec2 Centre = ImVec2(Origin.x + Box * 0.5f, Origin.y + Box * 0.5f);
        const float Thickness = 1.3f;

        auto Rect = [&](float HalfW, float HalfH, float Rounding)
        {
            DrawList->AddRect(ImVec2(Centre.x - HalfW, Centre.y - HalfH),
                              ImVec2(Centre.x + HalfW, Centre.y + HalfH), TintColor, Rounding, 0, Thickness);
        };

        if (std::strcmp(Glyph, "scene") == 0)
        {
            DrawList->AddCircle(Centre, 6.3f, TintColor, 0, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - 6.3f, Centre.y), ImVec2(Centre.x + 6.3f, Centre.y), TintColor, 1.1f);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - 6.3f), ImVec2(Centre.x, Centre.y + 6.3f), TintColor, 1.1f);
            DrawList->AddBezierQuadratic(ImVec2(Centre.x - 6.3f, Centre.y), Centre, ImVec2(Centre.x + 6.3f, Centre.y), TintColor, 1.1f);
        }
        else if (std::strcmp(Glyph, "folder") == 0 || std::strcmp(Glyph, "geometry") == 0)
        {
            // 📝 A tab-topped folder outline.
            ImVec2 Min = ImVec2(Centre.x - 6.5f, Centre.y - 4.5f);
            ImVec2 Max = ImVec2(Centre.x + 6.5f, Centre.y + 5.0f);
            DrawList->AddRect(Min, Max, TintColor, 1.5f, 0, Thickness);
            DrawList->AddLine(ImVec2(Min.x, Min.y), ImVec2(Min.x + 3.5f, Min.y - 2.0f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Min.x + 3.5f, Min.y - 2.0f), ImVec2(Min.x + 6.0f, Min.y), TintColor, Thickness);
        }
        else if (std::strcmp(Glyph, "mesh") == 0)
        {
            // 📝 A cube in oblique projection.
            float H = 6.2f;
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - H), ImVec2(Centre.x + H, Centre.y - H * 0.45f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H, Centre.y - H * 0.45f), ImVec2(Centre.x + H, Centre.y + H * 0.55f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H, Centre.y + H * 0.55f), ImVec2(Centre.x, Centre.y + H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y + H), ImVec2(Centre.x - H, Centre.y + H * 0.55f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y + H * 0.55f), ImVec2(Centre.x - H, Centre.y - H * 0.45f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y - H * 0.45f), ImVec2(Centre.x, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - H), ImVec2(Centre.x, Centre.y + H), TintColor, 1.1f);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y - H * 0.45f), ImVec2(Centre.x, Centre.y), TintColor, 1.1f);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y), ImVec2(Centre.x + H, Centre.y - H * 0.45f), TintColor, 1.1f);
        }
        else if (std::strcmp(Glyph, "sun") == 0)
        {
            DrawList->AddCircle(Centre, 3.0f, TintColor, 0, Thickness);
            for (int Ray = 0; Ray < 8; ++Ray)
            {
                float Angle = (float)Ray * 0.785398f;
                ImVec2 Inner = ImVec2(Centre.x + cosf(Angle) * 5.0f, Centre.y + sinf(Angle) * 5.0f);
                ImVec2 Outer = ImVec2(Centre.x + cosf(Angle) * 7.0f, Centre.y + sinf(Angle) * 7.0f);
                DrawList->AddLine(Inner, Outer, TintColor, 1.2f);
            }
        }
        else if (std::strcmp(Glyph, "area") == 0)
        {
            Rect(5.0f, 5.0f, 1.0f);
        }
        else if (std::strcmp(Glyph, "lights") == 0)
        {
            // 📝 A bulb: circle body + two base lines.
            DrawList->AddCircle(ImVec2(Centre.x, Centre.y - 1.5f), 4.0f, TintColor, 0, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - 2.5f, Centre.y + 4.0f), ImVec2(Centre.x + 2.5f, Centre.y + 4.0f), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Centre.x - 2.0f, Centre.y + 5.3f), ImVec2(Centre.x + 2.0f, Centre.y + 5.3f), TintColor, 1.2f);
        }
        else if (std::strcmp(Glyph, "camera") == 0 || std::strcmp(Glyph, "cameras") == 0)
        {
            Rect(6.5f, 4.0f, 1.5f);
            DrawList->AddCircle(Centre, 2.3f, TintColor, 0, Thickness);
        }
        else if (std::strcmp(Glyph, "hdri") == 0 || std::strcmp(Glyph, "environment") == 0)
        {
            ImVec2 Min = ImVec2(Centre.x - 6.0f, Centre.y - 5.0f);
            ImVec2 Max = ImVec2(Centre.x + 6.0f, Centre.y + 5.0f);
            DrawList->AddRect(Min, Max, TintColor, 1.5f, 0, Thickness);
            DrawList->AddCircleFilled(ImVec2(Min.x + 3.0f, Min.y + 3.0f), 1.4f, TintColor);
            DrawList->AddLine(ImVec2(Min.x, Max.y - 1.0f), ImVec2(Min.x + 4.0f, Centre.y), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Min.x + 4.0f, Centre.y), ImVec2(Max.x - 3.0f, Max.y - 2.0f), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Max.x - 3.0f, Max.y - 2.0f), ImVec2(Max.x, Centre.y + 1.0f), TintColor, 1.2f);
        }
        else
        {
            DrawList->AddCircle(Centre, 5.0f, TintColor, 0, Thickness);
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SELECTION LOGIC
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The flat token order of every currently-visible row, honouring fold state + active filter. Drives shift-range selection.
    void AccumulateVisibleOrder(const SceneDirectoryState& State, const std::vector<RecordEntry>& Region, std::vector<RecordToken>& Order)
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

    void SelectOnly(SceneDirectoryState& State, RecordToken Target)
    {
        State.SelectionSet.clear();
        State.SelectionSet.push_back(Target);
        State.RangeAnchor = Target;
    }

    void ToggleInSelection(SceneDirectoryState& State, RecordToken Target)
    {
        auto Found = std::find(State.SelectionSet.begin(), State.SelectionSet.end(), Target);
        if (Found != State.SelectionSet.end()) { State.SelectionSet.erase(Found); }
        else { State.SelectionSet.push_back(Target); }
        State.RangeAnchor = Target;
    }

    void SelectRange(SceneDirectoryState& State, RecordToken Target)
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

    // 📝 Apply a click's modifier keys to the selection (plain / ctrl-toggle / shift-range), matching the mockup.
    void ApplyClickSelection(SceneDirectoryState& State, RecordToken Target)
    {
        ImGuiIO& Io = ImGui::GetIO();
        if (Io.KeyShift && State.RangeAnchor != 0) { SelectRange(State, Target); }
        else if (Io.KeyCtrl) { ToggleInSelection(State, Target); }
        else { SelectOnly(State, Target); }
    }

    // 📝 The tokens an action should operate on: the selection, or a fallback token if nothing is selected.
    std::vector<RecordToken> ResolveActionTargets(const SceneDirectoryState& State, RecordToken Fallback)
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
    std::string ResolveUniqueName(SceneDirectoryState& State, const char* Stem);

    void ConcealRegion(RecordEntry& Entry, bool Concealed)
    {
        Entry.ConcealedState = Concealed;
        for (RecordEntry& Nested : Entry.NestedRegion) { ConcealRegion(Nested, Concealed); }
    }

    // 📝 Toggle visibility for the whole selection (or one row), driving every target to the opposite of the clicked row.
    void ToggleConcealment(SceneDirectoryState& State, RecordToken Clicked)
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

    void DeleteTargets(SceneDirectoryState& State, RecordToken Fallback)
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
    void ReissueTokens(SceneDirectoryState& State, RecordEntry& Entry)
    {
        Entry.Token = State.NextToken++;
        for (RecordEntry& Nested : Entry.NestedRegion) { ReissueTokens(State, Nested); }
    }

    void DuplicateTargets(SceneDirectoryState& State, RecordToken Fallback)
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

    void GroupTargets(SceneDirectoryState& State, RecordToken Fallback)
    {
        std::vector<RecordToken> Targets = ResolveActionTargets(State, Fallback);
        if (Targets.empty()) { return; }

        std::vector<RecordEntry>* FirstContainer = nullptr;
        std::size_t FirstIndex = 0;
        if (!ResolveContainer(State.RootRegion, Targets.front(), &FirstContainer, &FirstIndex)) { return; }

        std::string GroupName = ResolveUniqueName(State, "Folder");
        RecordEntry Grouping = ConstructEntry(State, GroupName.c_str(), RecordClassification::EnclosureFolder, "folder", TintFolder, true);
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
    void RelocateInto(SceneDirectoryState& State, RecordToken Dragged, RecordToken TargetToken)
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
        // 📝 Do NOT force the group open — reparenting into a collapsed folder keeps it collapsed (matches the add-into-collapsed rule).
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      ADD-OBJECT CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    struct AddOption
    {
        const char*          Section;        // [-] - Non-null => this row is a section label, not a creatable item
        const char*          Label;          // [-] - Menu label (what the dropdown row reads)
        const char*          Stem;           // [-] - Name stem: first instance is the bare stem, dupes get "_01", "_02"...
        const char*          IconGlyph;      // [-] - Drawn glyph
        RecordClassification Classification; // [-] - Classification of the created item
        ImU32                TintColor;      // [-] - Icon tint
        bool                 Container;      // [-] - Created as an open container
    };

    // 📝 Stem drives the label scheme the user asked for: "Folder", then "Folder_01", "Folder_02"; "Light", "Light_01"; and so on. The
    //    display label mirrors the stem so the dropdown row and the created name line up (Folder, Mesh, Light, Camera, Environment).
    const AddOption AddCatalogue[] =
    {
        { "Geometry", nullptr,     nullptr,       nullptr,    RecordClassification::SceneRoot,       0,           false },
        { nullptr,    "Mesh",      "Mesh",        "mesh",     RecordClassification::PolygonSurface,  TintSurface, false },
        { nullptr,    "Folder",    "Folder",      "folder",   RecordClassification::EnclosureFolder, TintFolder,  true  },
        { "Lights",   nullptr,     nullptr,       nullptr,    RecordClassification::SceneRoot,       0,           false },
        { nullptr,    "Sun Light", "Light",       "sun",      RecordClassification::LightEmitter,    TintSun,     false },
        { nullptr,    "Area Light","Light",       "area",     RecordClassification::LightEmitter,    TintArea,    false },
        { "Scene",    nullptr,     nullptr,       nullptr,    RecordClassification::SceneRoot,       0,           false },
        { nullptr,    "Camera",    "Camera",      "camera",   RecordClassification::CameraLens,      TintCamera,  false },
        { nullptr,    "Environment","Environment","hdri",     RecordClassification::EnvironmentDome, TintEnvironment, false },
    };

    // 📝 Resolve a unique display name for a fresh item: the bare stem when free, otherwise the first free "<Stem>_NN" (zero-padded to 2).
    std::string ResolveUniqueName(SceneDirectoryState& State, const char* Stem)
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

    // 📝 Where a new object lands: a container anchor => inside it; a leaf anchor => Scene root; no anchor => Scene root.
    std::vector<RecordEntry>* ResolveAddDestination(SceneDirectoryState& State, RecordToken Anchor)
    {
        if (Anchor != 0)
        {
            RecordEntry* AnchorEntry = ResolveEntry(State.RootRegion, Anchor);
            if (AnchorEntry != nullptr && ContainerClassification(AnchorEntry->Classification))
            {
                // 📝 Leave the fold state untouched — dropping a new object into a collapsed folder keeps it collapsed (the user still sees
                //    the count change and can open it themselves). Forcing it open on every add was the unwanted auto-expand.
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

    void AppendObject(SceneDirectoryState& State, const AddOption& Option)
    {
        std::vector<RecordEntry>* Destination = ResolveAddDestination(State, State.AddMenuAnchor);

        // 📝 Name it "<Stem>" first, then "<Stem>_01", "<Stem>_02"... (Folder / Folder_01, Light / Light_01).
        std::string Candidate = ResolveUniqueName(State, Option.Stem);

        RecordEntry Fresh = ConstructEntry(State, Candidate.c_str(), Option.Classification, Option.IconGlyph, Option.TintColor, Option.Container);
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
    void BeginRename(SceneDirectoryState& State, const RecordEntry& Entry)
    {
        State.RenameTarget = Entry.Token;
        std::snprintf(State.RenameBuffer, sizeof(State.RenameBuffer), "%s", Entry.Label.c_str());
        State.RenameJustOpened = true;
    }

    void FinalizeRename(SceneDirectoryState& State)
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
    // 📝 The classification facets the add-filter menu / chips expose (containers Scene/Root are excluded on purpose, as in the mockup).
    struct ClassificationFacet
    {
        RecordClassification Classification;
        const char*          Label;
        const char*          IconGlyph;
        ImU32                IconTint;
    };

    const ClassificationFacet ClassificationFacets[] =
    {
        { RecordClassification::PolygonSurface,  "Meshes",      "mesh",   TintSurface },
        { RecordClassification::LightEmitter,    "Lights",      "sun",    TintSun },
        { RecordClassification::CameraLens,      "Cameras",     "camera", TintCamera },
        { RecordClassification::EnvironmentDome, "Environment", "hdri",   TintEnvironment },
        { RecordClassification::EnclosureFolder, "Folders",     "folder", TintFolder },
    };

    struct TintFacet
    {
        ImU32       Tint;
        const char* Label;
    };

    const TintFacet TintFacets[] =
    {
        { TintSurface,     "Grey" },
        { TintCamera,      "Blue" },
        { TintEnvironment, "Sky" },
        { TintSun,         "Yellow" },
        { TintArea,        "Purple" },
        { TintFolder,      "Orange" },
    };

    bool ChipPresent(const SceneDirectoryState& State, FilterFacet Facet, RecordClassification Classification, ImU32 Tint)
    {
        for (const FilterChip& Chip : State.FilterChips)
        {
            if (Chip.Facet != Facet) { continue; }
            if (Facet == FilterFacet::OnlyVisible) { return true; }
            if (Facet == FilterFacet::Classification && Chip.ClassificationValue == Classification) { return true; }
            if (Facet == FilterFacet::Tint && Chip.TintValue == Tint) { return true; }
        }
        return false;
    }

    void ToggleChip(SceneDirectoryState& State, FilterFacet Facet, RecordClassification Classification, ImU32 Tint)
    {
        for (std::size_t Index = 0; Index < State.FilterChips.size(); ++Index)
        {
            const FilterChip& Chip = State.FilterChips[Index];
            const bool Same = Chip.Facet == Facet &&
                ((Facet == FilterFacet::OnlyVisible) ||
                 (Facet == FilterFacet::Classification && Chip.ClassificationValue == Classification) ||
                 (Facet == FilterFacet::Tint && Chip.TintValue == Tint));
            if (Same) { State.FilterChips.erase(State.FilterChips.begin() + Index); return; }
        }
        FilterChip Fresh = {};
        Fresh.Facet = Facet;
        Fresh.ClassificationValue = Classification;
        Fresh.TintValue = Tint;
        State.FilterChips.push_back(Fresh);
    }

    const char* ClassificationLabel(RecordClassification Classification)
    {
        for (const ClassificationFacet& Facet : ClassificationFacets)
        {
            if (Facet.Classification == Classification) { return Facet.Label; }
        }
        return "Item";
    }

    const char* TintLabel(ImU32 Tint)
    {
        for (const TintFacet& Facet : TintFacets)
        {
            if (Facet.Tint == Tint) { return Facet.Label; }
        }
        return "Colour";
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      ROW DRAWING
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Per-cycle context threaded through the row recursion so each row can register drag/drop, clicks, and menu requests without
    //    reaching back into ImGui window scope repeatedly.
    struct RowContext
    {
        const ThemeConfiguration* Theme;
        SceneDirectoryState*      State;
        ImDrawList*               DrawList;
        float                     RowHeight;
        float                     IndentWidth;
        float                     ContentLeft;
        float                     ContentRight;
    };

    void ConstructRow(RowContext& Context, RecordEntry& Entry, int IndentDepth);

    // 📝 Per-container reveal ease (0 = collapsed, 1 = fully open), eased toward ExpandedState each frame and stored against the container's
    //    token in the tree window's storage. Read by both the row's twisty (chevron morph) and the recursion (subtree slide/fade). A stable,
    //    non-colliding key derives from the token so folding one container never disturbs another's animation.
    ImGuiID OpennessKey(RecordToken Token)
    {
        return ImGui::GetID((void*)(uintptr_t)(0xF0000000u ^ Token));
    }

    // 📝 Read the container's current openness WITHOUT advancing it — used by the twisty so it can morph in sync without double-stepping the
    //    ease (the recursion below owns the single per-frame step via ResolveOpenness).
    float PeekOpenness(RecordToken Token, bool TargetOpen)
    {
        ImGuiStorage* Store = ImGui::GetStateStorage();
        return Store->GetFloat(OpennessKey(Token), TargetOpen ? 1.0f : 0.0f);
    }

    // 📝 Advance the container's openness ease one frame toward TargetOpen and store it. Called exactly once per container per frame.
    //    A pure exponential (Ease += (Target−Ease)·rate) approaches the end ASYMPTOTICALLY: near 0 it crawls, so a folding subtree hangs at a
    //    few visible pixels for many frames and then a fixed openness clamp lops off the remainder — that lingering-gap-then-snap was the
    //    reported artefact. Two things kill it: (1) a constant floor velocity so the tail travels at a steady rate instead of crawling, and
    //    (2) BandHeight (the subtree's current pixel height) lets us land exactly on the end the moment the remaining band is sub-pixel, so
    //    the final removal is invisible regardless of how tall the subtree is.
    float ResolveOpenness(RecordToken Token, bool TargetOpen, float BandHeight)
    {
        ImGuiStorage* Store  = ImGui::GetStateStorage();
        const ImGuiID Key    = OpennessKey(Token);
        const float   Target = TargetOpen ? 1.0f : 0.0f;
        float Ease = Store->GetFloat(Key, Target);

        const float Delta = Target - Ease;
        // 📝 Land exactly on the target when the residual band is under half a pixel (or openness is already essentially there).
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
        // 📝 Take whichever step moves FURTHER this frame, but never overshoot the target.
        float Step = ImFabs(Exponential) > ImFabs(FloorStep) ? Exponential : FloorStep;
        if (ImFabs(Step) >= ImFabs(Delta)) { Ease = Target; }        // would reach/overshoot => land exactly
        else                               { Ease += Step; }

        Store->SetFloat(Key, Ease);
        return Ease;
    }

    // 📝 The pixel height a region occupies RIGHT NOW, honouring every nested container's eased openness (not just its target fold state). This
    //    is the crux of the smooth collapse: because it peeks the same ease the recursion draws with, the reserved band height tracks the
    //    animation frame-for-frame. Measuring with a boolean Open instead made a nested subtree's height jump the instant its fold flipped —
    //    that step was the last-moment snap the user felt. PeekOpenness only reads (never steps) so measuring can't perturb the animation.
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

            // 📝 While filtering, forced-open subtrees skip the reveal animation (matches drop everything open) — otherwise the container's
            //    ease drives a growing/shrinking clip window that the nested rows slide into. Advancing the cursor by only the eased height is
            //    what makes the rows below glide up on collapse and down on open (the "shelf" motion). Measure the full subtree FIRST so the
            //    ease can land exactly on 0/1 the moment the residual band is sub-pixel (no lingering-gap-then-snap on full close).
            const float FullHeight = MeasureRegionHeight(Context, Entry.NestedRegion);
            const float Openness   = Filtering ? 1.0f : ResolveOpenness(Entry.Token, Entry.ExpandedState, FullHeight);
            if (Openness <= 0.0f) { continue; }

            const bool  Animating   = Openness < 1.0f;
            const float ShownHeight = Animating ? FullHeight * Openness : FullHeight;
            const ImVec2 RevealTop  = ImGui::GetCursorScreenPos();

            // 📝 Draw the whole subtree at its natural position. While mid-animation, clip it to the revealed band and slide it up slightly so
            //    the rows rise into place (the soft accordion feel). Once fully open we DON'T clip — a lingering clip rect at the settled band
            //    bottom was shaving the last row (the reported bottom-row clip); an unclipped, fully-open subtree draws every row intact.
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

            // 📝 Reserve exactly the eased height so subsequent siblings sit flush against the animated band. Dummy (not a bare cursor jump)
            //    submits a real item so ImGui grows the window/parent boundary — a raw SetCursorScreenPos past the extent trips an assert.
            ImGui::SetCursorScreenPos(RevealTop);
            ImGui::Dummy(ImVec2(Context.ContentRight - RevealTop.x, ShownHeight));
        }
    }

    void ConstructRow(RowContext& Context, RecordEntry& Entry, int IndentDepth)
    {
        SceneDirectoryState& State = *Context.State;
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
            ImGui::SetDragDropPayload("SCENE_ROW", &Dragged, sizeof(RecordToken));
            ImGui::TextUnformatted(Entry.Label.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            DrawList->AddRectFilled(RowMin, RowMax, Palette.AccentSoft);
            DrawList->AddRect(RowMin, RowMax, Palette.Accent, 0.0f, 0, 1.0f);
            const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("SCENE_ROW");
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
            // 📝 Morph the chevron with the container's openness ease so it rotates smoothly from ▶ (right, 0) to ▼ (down, 1) in lock-step
            //    with the subtree reveal below. The two arms are one V rotated about the twisty centre by (Openness − 1) · 90°.
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
            // Twisty hit-zone toggles fold state.
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

        // -- Classification icon --
        const bool Concealed = Entry.ConcealedState;
        ImU32 IconTint = Entry.TintColor;
        if (Concealed) { IconTint = (IconTint & 0x00FFFFFF) | (100 << 24); }
        ConstructGlyph(DrawList, Entry.IconGlyph, ImVec2(PenX, CentreY - 9.0f), IconTint);
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

        // -- Row-level click / double-click / right-click (fall-through past the sub-zones handled above) --
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
    // 📝 A flat strip label + optional trailing icon buttons drawn with our own tones (ImGui's default button chrome is bypassed so the
    //    outliner reads exactly like the mockup). Returns true if the caller-supplied hit-zone was clicked.
    bool ConstructIconButton(ImDrawList* DrawList, ImVec2 Centre, const char* Glyph, ImU32 Tint)
    {
        ImVec2 Min = ImVec2(Centre.x - 11.0f, Centre.y - 11.0f);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Glyph);
        ImGui::InvisibleButton("##iconbtn", ImVec2(22.0f, 22.0f));
        const bool Pressed = ImGui::IsItemClicked();
        if (ImGui::IsItemHovered()) { DrawList->AddRectFilled(Min, ImVec2(Min.x + 22.0f, Min.y + 22.0f), Palette.MenuHover, 5.0f); }
        ImGui::PopID();

        // 📝 Reuse the glyph painter for a couple of chrome icons; fall back to simple strokes for the rest.
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
        else if (std::strcmp(Glyph, "collapse") == 0)
        {
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y - 4.0f), ImVec2(Centre.x + 3.0f, Centre.y - 4.0f), Tint, 1.3f);
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y), ImVec2(Centre.x + 1.0f, Centre.y), Tint, 1.3f);
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y + 4.0f), ImVec2(Centre.x + 3.0f, Centre.y + 4.0f), Tint, 1.3f);
        }
        else if (std::strcmp(Glyph, "search") == 0)
        {
            DrawList->AddCircle(ImVec2(Centre.x - 1.5f, Centre.y - 1.5f), 4.5f, Tint, 0, 1.3f);
            DrawList->AddLine(ImVec2(Centre.x + 2.0f, Centre.y + 2.0f), ImVec2(Centre.x + 5.0f, Centre.y + 5.0f), Tint, 1.3f);
        }
        return Pressed;
    }

    // 📝 One dropdown-list item drawn exactly like Controls/Dropdown.cpp: a sharp lighter-grey hover fill with a full-height blue accent bar
    //    on the left, an animated 14→20px text indent on hover, an optional tinted glyph, and an optional trailing marker (radio for the
    //    current single-choice row, tick for an active multi-select chip). The hover indent + fill are eased per-item so the list feels
    //    alive (the "micro animation" the user asked for) — the ease state is stored against the item's ImGui ID.
    enum class ItemMarker { None, Radio, RadioFilled, Check };

    // 📝 LeadSwatch != 0 draws a filled colour dot in the glyph slot (for the COLOURS facet); otherwise Glyph (if non-null) is painted there.
    //    TextOverride != 0 forces the label colour (e.g. danger-red Delete); otherwise Active picks accent, else primary text.
    bool ConstructDropdownItem(const char* Label, const char* Glyph, ImU32 GlyphTint, ItemMarker Marker, bool Active,
                               ImU32 LeadSwatch = 0, ImU32 TextOverride = 0)
    {
        ImDrawList* Draw   = ImGui::GetWindowDrawList();
        const float AvailW = ImGui::GetContentRegionAvail().x;
        const float ItemH  = 30.0f;
        const ImVec2 ItemMin = ImGui::GetCursorScreenPos();
        const ImVec2 ItemMax(ItemMin.x + AvailW, ItemMin.y + ItemH);

        const bool Clicked = ImGui::InvisibleButton("##dditem", ImVec2(AvailW, ItemH));
        const bool Hovered = ImGui::IsItemHovered();

        // 📝 Ease a 0..1 hover value toward the hovered state; ImGui's per-item storage keeps it stable across frames without a global map.
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

        const float IndentX = 14.0f + Ease * 6.0f;    // .dd-item:hover padding-left 14→20px
        float PenX = ItemMin.x + IndentX;
        const float CentreY = (ItemMin.y + ItemMax.y) * 0.5f;

        if (LeadSwatch != 0)
        {
            Draw->AddCircleFilled(ImVec2(PenX + 7.0f, CentreY), 5.0f, LeadSwatch);
            PenX += 24.0f;
        }
        else if (Glyph != nullptr)
        {
            ConstructGlyph(Draw, Glyph, ImVec2(PenX, CentreY - 9.0f), GlyphTint);
            PenX += 24.0f;
        }

        const ImU32 TextColor = TextOverride != 0 ? TextOverride : (Active ? Palette.Accent : Palette.TextPrimary);
        Draw->AddText(ImVec2(PenX, CentreY - ImGui::GetFontSize() * 0.5f), TextColor, Label);

        // -- Trailing marker --
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

    // 📝 A faint section label inside a dropdown list (TYPES / COLOURS / Geometry ...).
    void ConstructDropdownSection(const char* Text)
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        Draw->AddText(ImVec2(Pos.x + 12.0f, Pos.y + 4.0f), Palette.TextFaint, Text);
        ImGui::Dummy(ImVec2(0.0f, 20.0f));
    }

    // 📝 Forward-declared here so the context menu (defined first) can share the add / filter menus' popup framing.
    bool BeginDropdownPopup(const char* Id, float Width);
    void EndDropdownPopup();

    // 📝 A thin hairline divider inside a dropdown list.
    void ConstructDropdownDivider()
    {
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        const float  W   = ImGui::GetContentRegionAvail().x;
        Draw->AddLine(ImVec2(Pos.x, Pos.y + 4.0f), ImVec2(Pos.x + W, Pos.y + 4.0f), Palette.BorderLine, 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    void ConstructContextMenu(SceneDirectoryState& State)
    {
        if (State.ContextMenuRequested)
        {
            ImGui::OpenPopup("##scene-ctx");
            State.ContextMenuRequested = false;
        }
        if (BeginDropdownPopup("##scene-ctx", 210.0f))
        {
            RecordToken Target = State.ContextMenuTarget;
            ImGui::PushID("addobj");
            if (ConstructDropdownItem("Add Object", "add", Palette.TextDim, ItemMarker::None, false))
            {
                State.AddMenuRequested = true;
                State.AddMenuAnchor = Target;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            ConstructDropdownDivider();
            ImGui::PushID("rename");
            if (ConstructDropdownItem("Rename", nullptr, 0, ItemMarker::None, false))
            {
                RecordEntry* Entry = ResolveEntry(State.RootRegion, Target);
                if (Entry != nullptr) { SelectOnly(State, Target); BeginRename(State, *Entry); }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            ImGui::PushID("duplicate");
            if (ConstructDropdownItem("Duplicate", nullptr, 0, ItemMarker::None, false)) { DuplicateTargets(State, Target); ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
            ImGui::PushID("visibility");
            if (ConstructDropdownItem("Toggle Visibility", nullptr, 0, ItemMarker::None, false)) { ToggleConcealment(State, Target); ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
            ImGui::PushID("group");
            if (ConstructDropdownItem("Group into Folder", "folder", TintFolder, ItemMarker::None, false)) { GroupTargets(State, Target); ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
            ConstructDropdownDivider();
            ImGui::PushID("delete");
            if (ConstructDropdownItem("Delete", nullptr, 0, ItemMarker::None, false, 0, Palette.Danger)) { DeleteTargets(State, Target); ImGui::CloseCurrentPopup(); }
            ImGui::PopID();
            EndDropdownPopup();
        }
    }

    // 📝 Shared popup framing for the dropdown-style menus: PanelHeader fill, hairline border, no rounding, tight window padding and a small
    //    inter-item gap so the rows read as one continuous list (matching Controls/Dropdown.cpp's popup).
    bool BeginDropdownPopup(const char* Id, float Width)
    {
        ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
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

    void ConstructAddMenu(SceneDirectoryState& State)
    {
        if (State.AddMenuRequested)
        {
            ImGui::OpenPopup("##scene-add");
            State.AddMenuRequested = false;
        }
        if (BeginDropdownPopup("##scene-add", 220.0f))
        {
            for (const AddOption& Option : AddCatalogue)
            {
                if (Option.Section != nullptr)
                {
                    ConstructDropdownSection(Option.Section);
                    continue;
                }
                ImGui::PushID(Option.Label);
                if (ConstructDropdownItem(Option.Label, Option.IconGlyph, Option.TintColor, ItemMarker::None, false))
                {
                    AppendObject(State, Option);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            EndDropdownPopup();
        }
    }

    void ConstructFilterMenu(SceneDirectoryState& State)
    {
        if (State.FilterMenuRequested)
        {
            ImGui::OpenPopup("##scene-filter");
            State.FilterMenuRequested = false;
        }
        if (BeginDropdownPopup("##scene-filter", 220.0f))
        {
            ConstructDropdownSection("TYPES");
            for (const ClassificationFacet& Facet : ClassificationFacets)
            {
                const bool On = ChipPresent(State, FilterFacet::Classification, Facet.Classification, 0);
                ImGui::PushID(Facet.Label);
                if (ConstructDropdownItem(Facet.Label, Facet.IconGlyph, Facet.IconTint, ItemMarker::Check, On))
                {
                    ToggleChip(State, FilterFacet::Classification, Facet.Classification, 0);
                }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ConstructDropdownSection("COLOURS");
            for (const TintFacet& Facet : TintFacets)
            {
                const bool On = ChipPresent(State, FilterFacet::Tint, RecordClassification::SceneRoot, Facet.Tint);
                // 📝 The colour swatch rides the glyph slot via LeadSwatch, so text lines up with the TYPES rows above.
                ImGui::PushID(Facet.Label);
                if (ConstructDropdownItem(Facet.Label, nullptr, 0, ItemMarker::Check, On, Facet.Tint))
                {
                    ToggleChip(State, FilterFacet::Tint, RecordClassification::SceneRoot, Facet.Tint);
                }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            const bool VisibleOn = ChipPresent(State, FilterFacet::OnlyVisible, RecordClassification::SceneRoot, 0);
            ImGui::PushID("onlyvisible");
            if (ConstructDropdownItem("Only visible", nullptr, 0, ItemMarker::Check, VisibleOn))
            {
                ToggleChip(State, FilterFacet::OnlyVisible, RecordClassification::SceneRoot, 0);
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
    void EnforceKeyboardActions(SceneDirectoryState& State)
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

void ConstructSceneDirectoryPanel(const ThemeConfiguration& Theme, SceneDirectoryState& State)
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

    // -- Section header (stacked icon · SCENE · add · filter) — the single title bar; the old "[v Scene]" tab strip is gone -----------
    ImVec2 HeaderMin = PanelMin;
    ImVec2 HeaderMax = ImVec2(PanelRight, HeaderMin.y + HeaderHeight);
    DrawList->AddRectFilled(HeaderMin, HeaderMax, Palette.HeaderFill);
    DrawList->AddLine(ImVec2(HeaderMin.x, HeaderMax.y), ImVec2(HeaderMax.x, HeaderMax.y), Palette.BorderSoft);
    const float HeaderCentreY = HeaderMin.y + HeaderHeight * 0.5f;
    // 📝 Stacked-plates icon (three offset rhombi) so the header reads "[stacked] SCENE" like the mockup — decorative, not a button.
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
    DrawList->AddText(ImVec2(HeaderMin.x + 38.0f, HeaderCentreY - ImGui::GetFontSize() * 0.5f), Palette.TextPrimary, "SCENE");
    if (ConstructIconButton(DrawList, ImVec2(PanelRight - 44.0f, HeaderCentreY), "add", Palette.TextDim))
    {
        State.AddMenuRequested = true;
        State.AddMenuAnchor = State.RangeAnchor;
    }
    if (ConstructIconButton(DrawList, ImVec2(PanelRight - 18.0f, HeaderCentreY), "filter", Palette.TextDim))
    {
        State.FilterMenuRequested = true;
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
        ImGui::InputTextWithHint("##search", "Filter scene...", State.SearchText, sizeof(State.SearchText));
        ImGui::PopStyleColor(2);
    }

    // -- Filter chips ---------------------------------------------------------------------------------------------------
    ImVec2 FiltersMin = ImVec2(PanelMin.x, SearchMax.y);
    ImVec2 FiltersMax = ImVec2(PanelRight, FiltersMin.y + FiltersHeight);
    DrawList->AddRectFilled(FiltersMin, FiltersMax, Palette.HeaderFill);
    DrawList->AddLine(ImVec2(FiltersMin.x, FiltersMax.y), ImVec2(FiltersMax.x, FiltersMax.y), Palette.BorderSoft);
    {
        // 📝 Label row sits at the top; the chip / +Filters row sits ~22px lower so the two never touch (the previous 9px vs 20px
        //    offsets left them overlapping). The taller FiltersHeight (52) gives the second row room to breathe.
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
            const char* Label = Chip.Facet == FilterFacet::Classification ? ClassificationLabel(Chip.ClassificationValue)
                              : Chip.Facet == FilterFacet::Tint            ? TintLabel(Chip.TintValue)
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
        if (ImGui::SmallButton("+ Filters")) { State.FilterMenuRequested = true; }
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
    ImGui::BeginChild("##scene-tree", ImVec2(PanelWidth, ViewportHeight), false,
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
    Context.DrawList     = ImGui::GetWindowDrawList();
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
    ConstructContextMenu(State);
    ConstructAddMenu(State);
    ConstructFilterMenu(State);
    EnforceKeyboardActions(State);
}

}   // namespace SceneDirectoryValidation
