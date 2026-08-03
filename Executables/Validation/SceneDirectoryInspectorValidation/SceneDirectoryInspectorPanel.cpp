/*==============================================================================================================================================
                                                    SCENEDIRECTORYINSPECTORPANEL.CPP
==============================================================================================================================================*/
// 🧩 The drawing half of the summoned scene-directory inspector, ported 1:1 from Documentation/Prototypes/SceneDirectoryInspector.html. The
//    non-drawing tables (classification hues, the record tree helpers, establishProfile / profileCards, the branching RevisionStore) all live in
//    SceneDirectoryInspector.cpp; this file owns the interaction surface those tables feed: the Tab / right-click summon over a bare viewport, the
//    fixed 548x372 card, the outer directory <-> inspect carousel, the inner Properties <-> History carousel, the directory rows (twisty · glyph ·
//    label · tally · eye) with selection / rename / drag relocation / filter, the metadata pane with its inline action list, the property cards with
//    every field widget, and the branching history rail (branch pills · timeline · undo / redo). Every mutation logs a revision, matching the
//    prototype's appearance (visual-only — the snapshot restore is not wired). All of it lives in the app-local namespace
//    SceneDirectoryInspectorValidation, never Frontier, so its RecordEntry / RecordToken never collide with the pillar's.

#include "SceneDirectoryInspectorPanel.h"
#include "InspectorContentProfile.h"
#include "InspectorGlyphs.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

using Frontier::ThemeConfiguration;

namespace SceneDirectoryInspectorValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INSPECTOR PALETTE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The prototype's CSS custom-properties, packed as 0xAABBGGRR ImU32. The card is a dark glass surface; the two panes share this palette so a
    //    swap re-tints both. Names track the CSS variable they came from (--bg / --ink / --dim / --line / --accent ...).
    struct InspectorPalette
    {
        ImU32 CardFill      = IM_COL32( 0x14, 0x15, 0x18, 250);   // --card: the summoned surface
        ImU32 PaneFill      = IM_COL32( 0x0f, 0x10, 0x13, 255);   // --bg: pane background
        ImU32 HeadFill      = IM_COL32( 0x17, 0x18, 0x1c, 255);   // pane-head / foot fill
        ImU32 WidgetFill    = IM_COL32( 0x1b, 0x1c, 0x21, 255);   // prop widget fill
        ImU32 RowHover      = IM_COL32( 0x1c, 0x1d, 0x22, 255);   // row hover
        ImU32 RowActive     = IM_COL32( 0x22, 0x28, 0x34, 255);   // row selected fill
        ImU32 BorderSoft    = IM_COL32( 0x22, 0x24, 0x2a, 255);   // --line
        ImU32 BorderLine    = IM_COL32( 0x2a, 0x2c, 0x34, 255);   // stronger border
        ImU32 MenuFill      = IM_COL32( 0x1a, 0x1b, 0x20, 255);   // fold / add menu fill
        ImU32 MenuHover     = IM_COL32( 0x24, 0x26, 0x2e, 255);   // menu row hover
        ImU32 TextPrimary   = IM_COL32( 0xe6, 0xe7, 0xea, 255);   // --ink
        ImU32 TextDim       = IM_COL32( 0x9a, 0x9c, 0xa6, 255);   // --dim
        ImU32 TextFaint     = IM_COL32( 0x60, 0x62, 0x6c, 255);   // --faint
        ImU32 Accent        = IM_COL32( 0x4f, 0x8e, 0xff, 255);   // --accent (blue)
        ImU32 AccentSoft    = IM_COL32( 0x4f, 0x8e, 0xff,  40);   // drop-target / selection tint
        ImU32 Danger        = IM_COL32( 0xe0, 0x5a, 0x5a, 255);   // delete row
        ImU32 Track         = IM_COL32( 0x28, 0x2a, 0x32, 255);   // slider track
        ImU32 KnobInk       = IM_COL32( 0x0c, 0x0d, 0x10, 255);   // ink on an accent fill
    };

    const InspectorPalette Palette;

    // The prototype's fixed card geometry (MENU_W / MENU_H) and the compact per-pane bands. Numbers are the exact CSS values.
    constexpr float CardWidth     = 548.0f;
    constexpr float CardHeight    = 372.0f;
    constexpr float PaneHeadH     = 46.0f;    // .pane-head compact
    constexpr float CarouselSegH  = 31.0f;    // .prop-carousel segmented control
    constexpr float PaneFootH     = 26.0f;    // .pane-foot
    constexpr float RowHeight     = 26.0f;    // directory row (.drow-ish)
    constexpr float IndentWidth   = 15.0f;    // per-depth indent
    constexpr float OuterSeconds  = 0.34f;    // .menu-track transition (.34s)
    constexpr float InnerSeconds  = 0.34f;    // .inspect-track transition
    constexpr float OpenSeconds   = 0.22f;    // the pop on summon
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      DRAWING PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // A cubic-bezier ease — the prototype's carousel + open curves, restated here (same solver the sibling targets use).
    float SolveCubicBezier(float Progress, float X1, float Y1, float X2, float Y2)
    {
        if (Progress <= 0.0f) { return 0.0f; }
        if (Progress >= 1.0f) { return 1.0f; }
        float Low = 0.0f, High = 1.0f, Guess = Progress;
        for (int Iteration = 0; Iteration < 20; ++Iteration)
        {
            const float OneMinus = 1.0f - Guess;
            const float SampleX  = 3.0f * OneMinus * OneMinus * Guess * X1 + 3.0f * OneMinus * Guess * Guess * X2 + Guess * Guess * Guess;
            if (SampleX < Progress) { Low = Guess; } else { High = Guess; }
            Guess = (Low + High) * 0.5f;
        }
        const float OneMinus = 1.0f - Guess;
        return 3.0f * OneMinus * OneMinus * Guess * Y1 + 3.0f * OneMinus * Guess * Guess * Y2 + Guess * Guess * Guess;
    }

    // Advance an eased 0..1 travel toward a target this frame over Seconds, linear step (the bezier shapes it at read time).
    void AdvanceTravel(float& Travel, bool TargetOn, float DeltaSeconds, float Seconds)
    {
        const float Target = TargetOn ? 1.0f : 0.0f;
        if (Travel == Target) { return; }
        const float Step = (Seconds > 0.0f) ? (DeltaSeconds / Seconds) : 1.0f;
        if (Target > Travel) { Travel = ImMin(1.0f, Travel + Step); }
        else                 { Travel = ImMax(0.0f, Travel - Step); }
    }

    // One line clipped to a width, so a rail label / tile caption never bleeds past its column.
    void DrawClippedText(ImDrawList* Draw, ImVec2 Position, ImU32 Colour, const char* Text, float MaxWidth)
    {
        ImGui::PushClipRect(Position, ImVec2(Position.x + MaxWidth, Position.y + ImGui::GetTextLineHeight() + 2.0f), true);
        Draw->AddText(Position, Colour, Text);
        ImGui::PopClipRect();
    }

    // A hit region over the last-emitted invisible button, hovered + clicked in one call.
    struct Hit { bool Hovered; bool Clicked; };
    Hit RegionButton(const char* Id, ImVec2 TopLeft, ImVec2 Size)
    {
        ImGui::SetCursorScreenPos(TopLeft);
        const bool Clicked = ImGui::InvisibleButton(Id, Size);
        return Hit{ ImGui::IsItemHovered(), Clicked };
    }

    // A wall-clock "HH:MM" stamp for a fresh revision. ImGui carries no clock and the model owns none, so a monotonic frame counter drives a
    // synthetic minute past the seed's 09:07 — deterministic, and it advances so successive edits read as later. (The prototype used the real
    // clock; a headless validation build has none, so this keeps the rows ordered without a wall clock.)
    void CaptureTimeText(char* Destination, int Capacity)
    {
        static int Minute = 8;                 // first recorded edit reads 09:08, one past the seed tip
        const int Hour = 9 + (Minute / 60);
        std::snprintf(Destination, Capacity, "%02d:%02d", Hour, Minute % 60);
        ++Minute;
    }

    // Resolve the row icon at Origin: the uploaded SVG (WHITE-tinted, only alpha carried), else a small procedural mark. The inspector glyphs are
    // placeholders today, so the fallback is what draws — a rounded square with a centred dot, tinted by the classification hue.
    void ConstructGlyph(ImDrawList* Draw, const Frontier::SvgIconRegistry* Registry, const std::string& Key, ImVec2 Origin, float Box, ImU32 Tint)
    {
        ImTextureID Texture = Registry != nullptr ? Frontier::ResolveIconTexture(*Registry, Key) : (ImTextureID)0;
        if (Texture != 0)
        {
            const ImU32 ImageTint = IM_COL32(0xff, 0xff, 0xff, (Tint >> IM_COL32_A_SHIFT) & 0xFF);
            Draw->AddImage(Texture, Origin, ImVec2(Origin.x + Box, Origin.y + Box), ImVec2(0, 0), ImVec2(1, 1), ImageTint);
            return;
        }
        const ImVec2 Min = ImVec2(Origin.x + Box * 0.18f, Origin.y + Box * 0.18f);
        const ImVec2 Max = ImVec2(Origin.x + Box * 0.82f, Origin.y + Box * 0.82f);
        Draw->AddRect(Min, Max, Tint, Box * 0.16f, 0, 1.4f);
        Draw->AddCircleFilled(ImVec2(Origin.x + Box * 0.5f, Origin.y + Box * 0.5f), Box * 0.09f, Tint);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SAMPLE CONTENT
//------------------------------------------------------------------------------------------------------------------------
// 📝 The directory tree is the shared SketchOutliner's (Frontier::SketchOutlinerUi::SketchOutlinerState, caller-owned), so the seed builds
//    Frontier::SketchOutlinerUi::RecordEntry values directly (all fields public) rather than an SDI node. 🔴 The directory runs the SDI content
//    profile (InspectorContentProfile), so a row's opaque ClassificationId IS the SDI RecordClassification and its hue + glyph key come off that
//    profile's tables. The classification side-table the seed used to maintain is gone, and with it the drift that mis-typed every row born in
//    the add menu; only the property bag still hangs off the token, because the shared tree row has no room for it.

namespace
{
    namespace SO = Frontier::SketchOutlinerUi;

    // 📝 A per-frame scratch mirror of the selected row, carrying only what the preserved card / metadata / property code reads off a record:
    //    the token (widget-ID uniqueness), the display name (the Name text field writes here), the SDI classification (card schema + hue +
    //    glyph), and the visibility. Name writes land here during the frame and are pushed back onto the SketchOutliner entry's Label after
    //    the card drew (report-then-apply). It replaces the deleted SDI RecordEntry the drawing half used to walk.
    struct ScratchRecord
    {
        RecordToken          Token          = 0;
        std::string          Name;
        RecordClassification Classification = RecordClassification::Solid;
        bool                 Hidden         = false;
    };

    // Resolve a token to its live SketchOutliner entry anywhere in the reused tree (read + write; the card pushes a rename back through it).
    SO::RecordEntry* ResolveOutlinerEntry(std::vector<SO::RecordEntry>& Region, RecordToken Target)
    {
        for (SO::RecordEntry& Entry : Region)
        {
            if (Entry.Token == Target) { return &Entry; }
            SO::RecordEntry* Nested = ResolveOutlinerEntry(Entry.NestedRegion, Target);
            if (Nested != nullptr) { return Nested; }
        }
        return nullptr;
    }
    const SO::RecordEntry* ResolveOutlinerEntry(const std::vector<SO::RecordEntry>& Region, RecordToken Target)
    {
        return ResolveOutlinerEntry(const_cast<std::vector<SO::RecordEntry>&>(Region), Target);
    }

}


RecordProfile& ProfileFor(InspectorPanelState& State, RecordToken Token)
{
    for (auto& Pair : State.Profiles) { if (Pair.first == Token) { return Pair.second; } }
    State.Profiles.emplace_back(Token, RecordProfile{});
    return State.Profiles.back().second;
}


RecordClassification KindFor(const InspectorPanelState& State, RecordToken Token)
{
    const SO::RecordEntry* Entry = ResolveOutlinerEntry(State.Directory.RootRegion, Token);
    if (Entry == nullptr) { return RecordClassification::Solid; }   // a safe card schema when the token resolves to no row
    return ResolveClassificationOfId(Entry->ClassificationId);
}


void InitializeInspectorSample(InspectorPanelState& State)
{
    // 📝 The empty opening pose. The directory profile's own SeedSample clears the tree and adds nothing, so the workspace opens with no
    //    authored content and every row arrives through the outliner's add menu (which lands top-tier rows when the root region is empty).
    State.Directory = Frontier::SketchOutlinerUi::SketchOutlinerState{};
    SO::InitializeSketchOutlinerSample(State.Directory, ResolveInspectorContentProfile());

    State.Profiles.clear();
    State.ShownToken = 0;

    // 🔴 An empty history is ONE empty branch, not zero branches: RecordRevision / StepBack / StepForward / JumpToRevision / ForkBranch all
    //    early-return unless Active indexes a live branch, so a branch-less store would swallow every logged revision without a trace.
    State.Revisions.Branches.clear();
    State.Revisions.Active    = 0;
    State.Revisions.BranchSeq = 0;
    RevisionBranch Trunk;
    Trunk.Name   = "Trunk";
    Trunk.Cursor = -1;
    State.Revisions.Branches.push_back(std::move(Trunk));
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SELECTION SURFACE
//------------------------------------------------------------------------------------------------------------------------
// 📝 The reused SketchOutliner owns rows / selection / rename / drag / eye / filter / menus; this half only READS the selection it surfaces.
//    The active record the panes draw is the first selected token, resolved to a scratch mirror (name + SDI classification + visibility +
//    child count) walked out of the outliner tree. Nothing here mutates the tree — the outliner already did that this frame.

namespace
{
    // The first selected token, or the root Part when nothing is selected (the prototype's activeRecord). 0 when the tree is empty.
    RecordToken ActiveToken(const InspectorPanelState& State)
    {
        if (!State.Directory.SelectionSet.empty()) { return State.Directory.SelectionSet.front(); }
        if (!State.Directory.RootRegion.empty())   { return State.Directory.RootRegion.front().Token; }
        return 0;
    }

    // Fill a scratch mirror from the active outliner row (name + SDI kind + visibility). Returns false when the token resolves to no row.
    bool ResolveActive(const InspectorPanelState& State, RecordToken Token, ScratchRecord& Out)
    {
        const SO::RecordEntry* Entry = ResolveOutlinerEntry(State.Directory.RootRegion, Token);
        if (Entry == nullptr) { return false; }
        Out.Token          = Entry->Token;
        Out.Name           = Entry->Label;
        Out.Classification = KindFor(State, Entry->Token);
        Out.Hidden         = Entry->ConcealedState;
        return true;
    }

    // The direct child count of the active row (nestedCount) — the folder card's seed + the metadata "N records" line.
    int NestedTally(const InspectorPanelState& State, RecordToken Token)
    {
        const SO::RecordEntry* Entry = ResolveOutlinerEntry(State.Directory.RootRegion, Token);
        return Entry != nullptr ? (int)Entry->NestedRegion.size() : 0;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      REVISION LOGGING
//------------------------------------------------------------------------------------------------------------------------
// 📝 The reused SketchOutliner owns every tree mutation now (rename / duplicate / hide / isolate / group / delete / relocate / append); this
//    half only logs a revision when it observes one of those deltas across frames. LogRevision stamps the frame clock onto the record.

namespace
{
    void LogRevision(InspectorPanelState& State, RevisionCategory Category, const char* Title, const char* Subtitle)
    {
        char TimeText[8]; CaptureTimeText(TimeText, sizeof(TimeText));
        RecordRevision(State.Revisions, Category, Title, Subtitle, TimeText);
    }

}


//------------------------------------------------------------------------------------------------------------------------
//                                                      FOLD MEMORY
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // foldMemory: a per-card collapsed flag keyed "classification/Title". The vector stays small (a handful of cards per record).
    std::string FoldKey(RecordClassification Classification, const char* Title)
    {
        std::string Key = ClassificationKey(Classification);
        Key += "/";
        Key += (Title ? Title : "");
        return Key;
    }

    bool CardCollapsed(const InspectorPanelState& State, const std::string& Key)
    {
        return std::find(State.CollapsedCards.begin(), State.CollapsedCards.end(), Key) != State.CollapsedCards.end();
    }

    void ToggleCardFold(InspectorPanelState& State, const std::string& Key)
    {
        auto Found = std::find(State.CollapsedCards.begin(), State.CollapsedCards.end(), Key);
        if (Found != State.CollapsedCards.end()) { State.CollapsedCards.erase(Found); }
        else { State.CollapsedCards.push_back(Key); }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      METADATA PANE
//------------------------------------------------------------------------------------------------------------------------
// 📝 Slide 1's right pane (renderMetadata): the hero (icon · name · class), a handful of read-only summary lines, and the Properties + History
//    CTA. The record actions the prototype listed here (rename / duplicate / hide / isolate / group / delete / new) are now the reused
//    SketchOutliner's own context + add menus on the directory rail, so this pane is read-only — it reflects the selection, it does not mutate it.
//    The hero + summary + CTA draw inline in the SLIDE 1 block below off the resolved scratch mirror; no helper is needed here.


//------------------------------------------------------------------------------------------------------------------------
//                                                      PROFILE FIELD BINDING
//------------------------------------------------------------------------------------------------------------------------
// 📝 The prototype's field objects carry a `key` string naming the profile member the widget binds. C++ has no reflection, so one resolver
//    maps each key to a typed lvalue into the live RecordProfile. Slider / scalar over an int-typed member (SegmentTally, RingTally, …) is
//    surfaced as a float view (IntField != nullptr) so the widget reads / writes it through the int, matching the JS number field.

namespace
{
    struct FieldBinding
    {
        float*       FloatField = nullptr;   // float member (or one axis of a vector)
        int*         IntField   = nullptr;   // int member (Format 0 fields, dropdown / selection indices)
        bool*        BoolField  = nullptr;   // bool member
        std::string* TextField  = nullptr;   // string member
        int*         VectorField= nullptr;   // unused (vectors resolve axis-by-axis via FloatField)
        int*         Colour     = nullptr;   // int[4] RGBA (Albedo)
        float        Vec[3]     = { 0, 0, 0 };
        float*       VecPtr[3]  = { nullptr, nullptr, nullptr };
    };

    // Resolve a profile member by key. Vector keys fill VecPtr[0..2]; scalar keys fill exactly one of the typed pointers.
    FieldBinding BindField(RecordProfile& P, const char* Key)
    {
        FieldBinding B;
        auto Is = [&](const char* K) { return std::strcmp(Key, K) == 0; };

        // -- vectors --
        if (Is("Position")) { B.VecPtr[0] = &P.Position[0]; B.VecPtr[1] = &P.Position[1]; B.VecPtr[2] = &P.Position[2]; return B; }
        if (Is("Rotation")) { B.VecPtr[0] = &P.Rotation[0]; B.VecPtr[1] = &P.Rotation[1]; B.VecPtr[2] = &P.Rotation[2]; return B; }
        if (Is("Scale"))    { B.VecPtr[0] = &P.Scale[0];    B.VecPtr[1] = &P.Scale[1];    B.VecPtr[2] = &P.Scale[2];    return B; }

        // -- colour --
        if (Is("Albedo")) { B.Colour = P.Albedo; return B; }

        // -- strings --
        if (Is("DocumentPath")) { B.TextField = &P.DocumentPath; return B; }

        // -- bools --
        if (Is("Visible"))          { B.BoolField = &P.Visible; return B; }
        if (Is("Suppressed"))       { B.BoolField = &P.Suppressed; return B; }
        if (Is("FullyConstrained")) { B.BoolField = &P.FullyConstrained; return B; }
        if (Is("CappedEnds"))       { B.BoolField = &P.CappedEnds; return B; }
        if (Is("ProfileClosed"))    { B.BoolField = &P.ProfileClosed; return B; }
        if (Is("Ruled"))            { B.BoolField = &P.Ruled; return B; }
        if (Is("Selectable"))       { B.BoolField = &P.Selectable; return B; }
        if (Is("FlipNormal"))       { B.BoolField = &P.FlipNormal; return B; }
        if (Is("PlaneGrid"))        { B.BoolField = &P.PlaneGrid; return B; }
        if (Is("PlaneSnap"))        { B.BoolField = &P.PlaneSnap; return B; }
        if (Is("PlaneLock"))        { B.BoolField = &P.PlaneLock; return B; }

        // -- ints (dropdown / selection indices + count fields) --
        if (Is("Units"))          { B.IntField = &P.Units; return B; }
        if (Is("BooleanMode"))    { B.IntField = &P.BooleanMode; return B; }
        if (Is("PlaneChoice"))    { B.IntField = &P.PlaneChoice; return B; }
        if (Is("ShadingMode"))    { B.IntField = &P.ShadingMode; return B; }
        if (Is("AxisChoice"))     { B.IntField = &P.AxisChoice; return B; }
        if (Is("SegmentTally"))   { B.IntField = &P.SegmentTally; return B; }
        if (Is("RingTally"))      { B.IntField = &P.RingTally; return B; }
        if (Is("SectionTally"))   { B.IntField = &P.SectionTally; return B; }
        if (Is("NestedTally"))    { B.IntField = &P.NestedTally; return B; }
        if (Is("CurveTally"))     { B.IntField = &P.CurveTally; return B; }
        if (Is("ConstraintTally")){ B.IntField = &P.ConstraintTally; return B; }
        if (Is("PlaneMethod"))    { B.IntField = &P.PlaneMethod; return B; }
        if (Is("PlaneAnglePivot")){ B.IntField = &P.PlaneAnglePivot; return B; }

        // -- floats --
        if (Is("ToleranceLinear")) { B.FloatField = &P.ToleranceLinear; return B; }
        if (Is("ToleranceAngular")){ B.FloatField = &P.ToleranceAngular; return B; }
        if (Is("GridSnap"))        { B.FloatField = &P.GridSnap; return B; }
        if (Is("ExtrudeDepth"))    { B.FloatField = &P.ExtrudeDepth; return B; }
        if (Is("DraftAngle"))      { B.FloatField = &P.DraftAngle; return B; }
        if (Is("WallThickness"))   { B.FloatField = &P.WallThickness; return B; }
        if (Is("Radius"))          { B.FloatField = &P.Radius; return B; }
        if (Is("Height"))          { B.FloatField = &P.Height; return B; }
        if (Is("BaseRadius"))      { B.FloatField = &P.BaseRadius; return B; }
        if (Is("TipRadius"))       { B.FloatField = &P.TipRadius; return B; }
        if (Is("SweepAngle"))      { B.FloatField = &P.SweepAngle; return B; }
        if (Is("Roughness"))       { B.FloatField = &P.Roughness; return B; }
        if (Is("Metalness"))       { B.FloatField = &P.Metalness; return B; }
        if (Is("TangencyStart"))   { B.FloatField = &P.TangencyStart; return B; }
        if (Is("TangencyEnd"))     { B.FloatField = &P.TangencyEnd; return B; }
        if (Is("PlaneOffset"))     { B.FloatField = &P.PlaneOffset; return B; }
        if (Is("PlaneAngle"))      { B.FloatField = &P.PlaneAngle; return B; }
        if (Is("PlaneExtent"))     { B.FloatField = &P.PlaneExtent; return B; }
        if (Is("PlaneGridSpacing")){ B.FloatField = &P.PlaneGridSpacing; return B; }

        return B;   // unknown key: an inert binding (draws a blank row, as the JS default did)
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PROPERTY CARDS
//------------------------------------------------------------------------------------------------------------------------
// 📝 Slide 2's Properties face (renderProperties + buildCard + the nine field builders). Each card is a collapsible head + a body of field
//    rows. The nine controls (text / vector / slider / scalar / boolean / selection / dropdown / colour / path) each edit their bound member
//    live. Name writes through to the record (and the directory); Visible cascades the subtree. Fold state persists in CollapsedCards.

namespace
{
    constexpr float FieldRowH  = 30.0f;    // one property row
    constexpr float CardHeadH  = 28.0f;    // card head band
    constexpr float LabelColW  = 96.0f;    // the .plabel column width

    // A fmt(value, decimals) → text, matching the prototype's fixed-decimal number formatting.
    void FormatValue(char* Out, int Cap, float Value, int Decimals)
    {
        std::snprintf(Out, Cap, "%.*f", Decimals, Value);
    }

    // The shared row shell: draw the label, return the value-column rectangle.
    ImVec2 RowShell(ImDrawList* Draw, const char* Label, ImVec2 TopLeft, float Width, float& OutValueW)
    {
        const float MidY = TopLeft.y + FieldRowH * 0.5f;
        const ImVec2 LabelSize = ImGui::CalcTextSize(Label);
        Draw->AddText(ImVec2(TopLeft.x, MidY - LabelSize.y * 0.5f), Palette.TextDim, Label);
        OutValueW = Width - LabelColW;
        return ImVec2(TopLeft.x + LabelColW, TopLeft.y + 4.0f);
    }

    // ── text: Name writes straight to the record; other keys write the profile string. ──
    void FieldText(ImDrawList* Draw, InspectorPanelState& State, ScratchRecord& Entry, const FieldSpec& Field,
                   const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        const bool IsName = std::strcmp(Field.Key, "Name") == 0;

        char Buffer[128];
        if (IsName)                     { std::snprintf(Buffer, sizeof(Buffer), "%s", Entry.Name.c_str()); }
        else if (Bind.TextField)        { std::snprintf(Buffer, sizeof(Buffer), "%s", Bind.TextField->c_str()); }
        else                            { Buffer[0] = '\0'; }

        ImGui::SetCursorScreenPos(ImVec2(Box.x, Box.y));
        ImGui::SetNextItemWidth(ValueW);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Palette.WidgetFill);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextPrimary);
        char Id[64]; std::snprintf(Id, sizeof(Id), "##tx_%s_%u", Field.Key, Entry.Token);
        if (ImGui::InputText(Id, Buffer, sizeof(Buffer)))
        {
            if (IsName)              { Entry.Name = Buffer; }
            else if (Bind.TextField) { *Bind.TextField = Buffer; }
        }
        ImGui::PopStyleColor(2);
    }

    // ── vector: three axis-tagged number boxes ──
    void FieldVector(ImDrawList* Draw, ScratchRecord& Entry, const FieldSpec& Field, const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        const char* Axes[3] = { "X", "Y", "Z" };
        const float BoxW = (ValueW - 8.0f) / 3.0f;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Bx = Box.x + Axis * (BoxW + 4.0f);
            Draw->AddText(ImVec2(Bx + 2.0f, Box.y + 4.0f), Palette.TextFaint, Axes[Axis]);
            ImGui::SetCursorScreenPos(ImVec2(Bx + 14.0f, Box.y));
            ImGui::SetNextItemWidth(BoxW - 14.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Palette.WidgetFill);
            ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextPrimary);
            char Id[64]; std::snprintf(Id, sizeof(Id), "##vec_%s%d_%u", Field.Key, Axis, Entry.Token);
            float Value = Bind.VecPtr[Axis] ? *Bind.VecPtr[Axis] : 0.0f;
            if (ImGui::DragFloat(Id, &Value, Field.Step, 0.0f, 0.0f, Field.Format == 0 ? "%.0f" : (Field.Format == 1 ? "%.1f" : "%.2f")))
            {
                if (Bind.VecPtr[Axis]) { *Bind.VecPtr[Axis] = Value; }
            }
            ImGui::PopStyleColor(2);
        }
    }

    // ── slider: bounded track with fill + knob (also drives an int member when Format 0) ──
    void FieldSlider(ImDrawList* Draw, ScratchRecord& Entry, const FieldSpec& Field, const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        float Value = Bind.IntField ? (float)*Bind.IntField : (Bind.FloatField ? *Bind.FloatField : 0.0f);

        const float NumW = 54.0f;
        const float TrackX = Box.x, TrackW = ValueW - NumW - 8.0f;
        const float TrackY = Box.y + FieldRowH * 0.5f - 4.0f - 4.0f;

        Draw->AddRectFilled(ImVec2(TrackX, TrackY), ImVec2(TrackX + TrackW, TrackY + 4.0f), Palette.Track, 2.0f);
        const float T = (Field.Maximum > Field.Minimum) ? (Value - Field.Minimum) / (Field.Maximum - Field.Minimum) : 0.0f;
        const float TC = ImClamp(T, 0.0f, 1.0f);
        Draw->AddRectFilled(ImVec2(TrackX, TrackY), ImVec2(TrackX + TrackW * TC, TrackY + 4.0f), Palette.Accent, 2.0f);
        Draw->AddCircleFilled(ImVec2(TrackX + TrackW * TC, TrackY + 2.0f), 5.0f, Palette.Accent);

        char TrackId[64]; std::snprintf(TrackId, sizeof(TrackId), "##sl_%s_%u", Field.Key, Entry.Token);
        const Hit TrackHit = RegionButton(TrackId, ImVec2(TrackX, Box.y), ImVec2(TrackW, FieldRowH - 8.0f));
        if (TrackHit.Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const float MouseT = ImClamp((ImGui::GetIO().MousePos.x - TrackX) / ImMax(TrackW, 1.0f), 0.0f, 1.0f);
            float Next = Field.Minimum + MouseT * (Field.Maximum - Field.Minimum);
            if (Field.Format == 0) { Next = std::round(Next); }
            if (Bind.IntField)   { *Bind.IntField = (int)std::lround(Next); }
            else if (Bind.FloatField) { *Bind.FloatField = Next; }
            Value = Next;
        }

        // numeric echo
        char Num[24]; FormatValue(Num, sizeof(Num), Value, Field.Format);
        const ImVec2 NumSize = ImGui::CalcTextSize(Num);
        Draw->AddText(ImVec2(Box.x + ValueW - NumW, Box.y + FieldRowH * 0.5f - 4.0f - NumSize.y * 0.5f), Palette.TextPrimary, Num);
    }

    // ── scalar: unbounded, drag-to-nudge ──
    void FieldScalar(ImDrawList* Draw, ScratchRecord& Entry, const FieldSpec& Field, const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        ImGui::SetCursorScreenPos(ImVec2(Box.x, Box.y));
        ImGui::SetNextItemWidth(ValueW - 26.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Palette.WidgetFill);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextPrimary);
        char Id[64]; std::snprintf(Id, sizeof(Id), "##sc_%s_%u", Field.Key, Entry.Token);

        const char* Fmt = Field.Format == 0 ? "%.0f" : (Field.Format == 1 ? "%.1f" : (Field.Format == 3 ? "%.3f" : "%.2f"));
        if (Bind.IntField)
        {
            float Value = (float)*Bind.IntField;
            if (ImGui::DragFloat(Id, &Value, Field.Step, 0.0f, 0.0f, Fmt)) { *Bind.IntField = (int)std::lround(Value); }
        }
        else if (Bind.FloatField)
        {
            if (ImGui::DragFloat(Id, Bind.FloatField, Field.Step, 0.0f, 0.0f, Fmt)) {}
        }
        ImGui::PopStyleColor(2);
        // unit segment
        Draw->AddText(ImVec2(Box.x + ValueW - 20.0f, Box.y + 4.0f), Palette.TextFaint, Field.Unit);
    }

    // ── boolean: a switch; Visible cascades the record's subtree + directory ──
    void FieldBoolean(ImDrawList* Draw, InspectorPanelState& State, ScratchRecord& Entry, const FieldSpec& Field,
                      const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        const bool IsVisible = std::strcmp(Field.Key, "Visible") == 0;
        const bool On = IsVisible ? !Entry.Hidden : (Bind.BoolField ? *Bind.BoolField : false);

        const float SwW = 34.0f, SwH = 18.0f;
        const ImVec2 SwMin(Box.x, Box.y + 2.0f), SwMax(Box.x + SwW, Box.y + 2.0f + SwH);
        Draw->AddRectFilled(SwMin, SwMax, On ? Palette.Accent : Palette.Track, SwH * 0.5f);
        const float NubX = On ? (SwMax.x - SwH * 0.5f) : (SwMin.x + SwH * 0.5f);
        Draw->AddCircleFilled(ImVec2(NubX, SwMin.y + SwH * 0.5f), SwH * 0.5f - 2.0f, On ? Palette.KnobInk : Palette.TextDim);

        char Id[64]; std::snprintf(Id, sizeof(Id), "##bool_%s_%u", Field.Key, Entry.Token);
        const Hit SwHit = RegionButton(Id, SwMin, ImVec2(SwW, SwH));
        if (SwHit.Clicked)
        {
            const bool Next = !On;
            if (Bind.BoolField) { *Bind.BoolField = Next; }
            // The Visible switch mirrors the eye the reused SketchOutliner owns: write the new ConcealedState straight back onto the row so
            // the rail's eye affordance and the card agree, and mirror it locally so this frame's remaining draws read the fresh value.
            if (IsVisible)
            {
                Entry.Hidden = !Next;
                if (SO::RecordEntry* Row = ResolveOutlinerEntry(State.Directory.RootRegion, Entry.Token)) { Row->ConcealedState = Entry.Hidden; }
            }
        }
    }

    // ── segmented selection: one-of-N chips ──
    void FieldSelection(ImDrawList* Draw, ScratchRecord& Entry, const FieldSpec& Field, const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        const int Current = Bind.IntField ? *Bind.IntField : 0;
        const float OptW = Field.OptionCount > 0 ? ValueW / Field.OptionCount : ValueW;
        for (int Opt = 0; Opt < Field.OptionCount; ++Opt)
        {
            const ImVec2 OptMin(Box.x + Opt * OptW, Box.y + 2.0f), OptMax(Box.x + (Opt + 1) * OptW - 3.0f, Box.y + FieldRowH - 6.0f);
            const bool Sel = Opt == Current;
            Draw->AddRectFilled(OptMin, OptMax, Sel ? Palette.RowActive : Palette.WidgetFill, 3.0f);
            const char* Label = Field.Options[Opt];
            const ImVec2 LabelSize = ImGui::CalcTextSize(Label);
            Draw->AddText(ImVec2((OptMin.x + OptMax.x) * 0.5f - LabelSize.x * 0.5f, (OptMin.y + OptMax.y) * 0.5f - LabelSize.y * 0.5f),
                          Sel ? Palette.TextPrimary : Palette.TextDim, Label);
            char Id[64]; std::snprintf(Id, sizeof(Id), "##seg_%s%d_%u", Field.Key, Opt, Entry.Token);
            const Hit OptHit = RegionButton(Id, OptMin, ImVec2(OptMax.x - OptMin.x, OptMax.y - OptMin.y));
            if (OptHit.Clicked && Bind.IntField) { *Bind.IntField = Opt; }
        }
    }

    // ── dropdown: a popped list one-of-N (ImGui combo carries the pop + clip handling the prototype hand-placed) ──
    void FieldDropdown(ImDrawList* Draw, ScratchRecord& Entry, const FieldSpec& Field, const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        int Current = Bind.IntField ? *Bind.IntField : 0;
        const char* Preview = (Current >= 0 && Current < Field.OptionCount) ? Field.Options[Current] : "";
        ImGui::SetCursorScreenPos(ImVec2(Box.x, Box.y));
        ImGui::SetNextItemWidth(ValueW);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Palette.WidgetFill);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextPrimary);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Palette.MenuFill);
        char Id[64]; std::snprintf(Id, sizeof(Id), "##dd_%s_%u", Field.Key, Entry.Token);
        if (ImGui::BeginCombo(Id, Preview))
        {
            for (int Opt = 0; Opt < Field.OptionCount; ++Opt)
            {
                const bool Sel = Opt == Current;
                if (ImGui::Selectable(Field.Options[Opt], Sel) && Bind.IntField) { *Bind.IntField = Opt; }
                if (Sel) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(3);
    }

    // ── colour: a swatch that pops the RGBA picker (Albedo, int[4] 0..255) ──
    void FieldColour(ImDrawList* Draw, ScratchRecord& Entry, const FieldSpec& Field, const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        int* C = Bind.Colour;
        float Rgba[4] = { C ? C[0] / 255.0f : 0.0f, C ? C[1] / 255.0f : 0.0f, C ? C[2] / 255.0f : 0.0f, C ? C[3] / 255.0f : 1.0f };
        ImGui::SetCursorScreenPos(ImVec2(Box.x, Box.y));
        char Id[64]; std::snprintf(Id, sizeof(Id), "##col_%s_%u", Field.Key, Entry.Token);
        if (ImGui::ColorEdit4(Id, Rgba, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
        {
            if (C) { C[0] = (int)(Rgba[0] * 255.0f + 0.5f); C[1] = (int)(Rgba[1] * 255.0f + 0.5f);
                     C[2] = (int)(Rgba[2] * 255.0f + 0.5f); C[3] = (int)(Rgba[3] * 255.0f + 0.5f); }
        }
        if (C)
        {
            char Hex[16]; std::snprintf(Hex, sizeof(Hex), "#%02X%02X%02X", C[0], C[1], C[2]);
            Draw->AddText(ImVec2(Box.x + 30.0f, Box.y + 4.0f), Palette.TextDim, Hex);
        }
    }

    // ── path: a text field + a browse button (the JS prompt() has no headless equal, so browse is inert) ──
    void FieldPath(ImDrawList* Draw, ScratchRecord& Entry, const FieldSpec& Field, const FieldBinding& Bind, ImVec2 TopLeft, float Width)
    {
        float ValueW; const ImVec2 Box = RowShell(Draw, Field.Label, TopLeft, Width, ValueW);
        char Buffer[256]; std::snprintf(Buffer, sizeof(Buffer), "%s", Bind.TextField ? Bind.TextField->c_str() : "");
        ImGui::SetCursorScreenPos(ImVec2(Box.x, Box.y));
        ImGui::SetNextItemWidth(ValueW - 26.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Palette.WidgetFill);
        ImGui::PushStyleColor(ImGuiCol_Text, Palette.TextPrimary);
        char Id[64]; std::snprintf(Id, sizeof(Id), "##pt_%s_%u", Field.Key, Entry.Token);
        if (ImGui::InputText(Id, Buffer, sizeof(Buffer)) && Bind.TextField) { *Bind.TextField = Buffer; }
        ImGui::PopStyleColor(2);
        const ImVec2 BrowseMin(Box.x + ValueW - 22.0f, Box.y), BrowseMax(Box.x + ValueW, Box.y + FieldRowH - 8.0f);
        Draw->AddRectFilled(BrowseMin, BrowseMax, Palette.MenuHover, 3.0f);
        Draw->AddText(ImVec2(BrowseMin.x + 6.0f, BrowseMin.y + 3.0f), Palette.TextDim, "\xE2\x80\xA6");   // ellipsis
    }

    // Dispatch one field row (buildField).
    void ConstructField(ImDrawList* Draw, InspectorPanelState& State, ScratchRecord& Entry, RecordProfile& Profile,
                        const FieldSpec& Field, ImVec2 TopLeft, float Width)
    {
        FieldBinding Bind = BindField(Profile, Field.Key);
        switch (Field.Control)
        {
            case FieldControl::Text:      FieldText(Draw, State, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Vector:    FieldVector(Draw, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Slider:    FieldSlider(Draw, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Scalar:    FieldScalar(Draw, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Boolean:   FieldBoolean(Draw, State, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Selection: FieldSelection(Draw, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Dropdown:  FieldDropdown(Draw, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Colour:    FieldColour(Draw, Entry, Field, Bind, TopLeft, Width); break;
            case FieldControl::Path:      FieldPath(Draw, Entry, Field, Bind, TopLeft, Width); break;
        }
    }

    // One property card (buildCard): a collapsible head + a field body. Returns the toggled fold key when the head was clicked (empty = none).
    std::string ConstructCard(ImDrawList* Draw, const Frontier::SvgIconRegistry* Icons, InspectorPanelState& State,
                              ScratchRecord& Entry, RecordProfile& Profile, const CardSpec& Card, float X, float& Y, float Width)
    {
        const std::string Key = FoldKey(Entry.Classification, Card.Title);
        const bool Collapsed = CardCollapsed(State, Key);
        std::string Toggled;

        // -- Card head --
        const ImVec2 HeadMin(X, Y), HeadMax(X + Width, Y + CardHeadH);
        Draw->AddRectFilled(HeadMin, HeadMax, Palette.HeadFill, 5.0f);
        const float MidY = Y + CardHeadH * 0.5f;
        // chevron
        const float A = Collapsed ? 0.0f : 1.5707963f;
        const float Cs = std::cos(A), Sn = std::sin(A), R = 3.2f;
        const ImVec2 Centre(X + 12.0f, MidY);
        auto Rot = [&](float Dx, float Dy) { return ImVec2(Centre.x + Dx * Cs - Dy * Sn, Centre.y + Dx * Sn + Dy * Cs); };
        Draw->AddTriangleFilled(Rot(-R, -R), Rot(R, 0.0f), Rot(-R, R), Palette.TextDim);
        Draw->AddText(ImVec2(X + 24.0f, MidY - ImGui::GetTextLineHeight() * 0.5f), Palette.TextPrimary, Card.Title);
        char Count[16]; std::snprintf(Count, sizeof(Count), "%d", Card.FieldCount);
        const ImVec2 CountSize = ImGui::CalcTextSize(Count);
        Draw->AddText(ImVec2(HeadMax.x - 10.0f - CountSize.x, MidY - CountSize.y * 0.5f), Palette.TextFaint, Count);

        char HeadId[64]; std::snprintf(HeadId, sizeof(HeadId), "##card_%s_%u", Card.Title, Entry.Token);
        const Hit HeadHit = RegionButton(HeadId, HeadMin, ImVec2(Width, CardHeadH));
        if (HeadHit.Clicked) { Toggled = Key; }
        Y += CardHeadH + 2.0f;

        // -- Card body --
        if (!Collapsed)
        {
            const ImVec2 BodyMin(X, Y);
            const float BodyH = Card.FieldCount * FieldRowH + 6.0f;
            Draw->AddRectFilled(BodyMin, ImVec2(X + Width, Y + BodyH), Palette.PaneFill, 4.0f);
            float FieldY = Y + 3.0f;
            for (int F = 0; F < Card.FieldCount; ++F)
            {
                ConstructField(Draw, State, Entry, Profile, Card.Fields[F], ImVec2(X + 10.0f, FieldY), Width - 20.0f);
                FieldY += FieldRowH;
            }
            Y += BodyH + 6.0f;
        }
        else { Y += 6.0f; }

        return Toggled;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      HISTORY RAIL
//------------------------------------------------------------------------------------------------------------------------
// 📝 Slide 2's History face (renderBranches + renderRevisions): the branch pills (hxTrees) with a fork "+", the vertical timeline of revision
//    rows (node · line · title · type chip · time) with at-cursor / future styling, and undo / redo. A row click jumps the cursor; a pill click
//    switches the branch; the "+" forks; a pill "×" drops. All reported and applied after the frame.

namespace
{
    enum class HistoryAction { None, SelectBranch, DropBranch, ForkBranch, JumpTo, Undo, Redo };
    struct HistoryReport
    {
        HistoryAction Action = HistoryAction::None;
        int           Index  = 0;      // branch index or revision index
    };

    ImU32 RevisionToneColour(RevisionTone Tone)
    {
        switch (Tone)
        {
            case RevisionTone::Generative: return IM_COL32(0x6f, 0xc8, 0xa0, 255);
            case RevisionTone::Material:   return IM_COL32(0xc9, 0xa0, 0x6f, 255);
            case RevisionTone::Parametric: return IM_COL32(0x7f, 0xb0, 0xe8, 255);
            case RevisionTone::None:       default: return Palette.TextDim;
        }
    }

    // The branch pills row (renderBranches): a pill per branch + a trailing "+" fork chip.
    void ConstructBranchPills(ImDrawList* Draw, InspectorPanelState& State, float X, float Y, float Width, HistoryReport& Report)
    {
        RevisionStore& Store = State.Revisions;
        float PillX = X;
        const float PillH = 22.0f;
        for (int Index = 0; Index < (int)Store.Branches.size(); ++Index)
        {
            const RevisionBranch& Branch = Store.Branches[Index];
            const bool Active = Index == Store.Active;
            char Caption[64]; std::snprintf(Caption, sizeof(Caption), "%s", Branch.Name.c_str());
            const ImVec2 NameSize = ImGui::CalcTextSize(Caption);
            char CountText[16]; std::snprintf(CountText, sizeof(CountText), "%d", (int)Branch.Revisions.size());
            const ImVec2 CountSize = ImGui::CalcTextSize(CountText);
            const bool ShowX = Store.Branches.size() > 1;
            const float PillW = NameSize.x + 12.0f + CountSize.x + 10.0f + (ShowX ? 16.0f : 0.0f);

            const ImVec2 PillMin(PillX, Y), PillMax(PillX + PillW, Y + PillH);
            Draw->AddRectFilled(PillMin, PillMax, Active ? Palette.RowActive : Palette.MenuFill, PillH * 0.5f);
            if (Active) { Draw->AddRect(PillMin, PillMax, Palette.Accent, PillH * 0.5f, 0, 1.2f); }
            const float MidY = Y + PillH * 0.5f;
            Draw->AddText(ImVec2(PillX + 8.0f, MidY - NameSize.y * 0.5f), Active ? Palette.TextPrimary : Palette.TextDim, Caption);
            Draw->AddText(ImVec2(PillX + 8.0f + NameSize.x + 6.0f, MidY - CountSize.y * 0.5f), Palette.TextFaint, CountText);

            char PillId[32]; std::snprintf(PillId, sizeof(PillId), "##br%d", Index);
            const Hit PillHit = RegionButton(PillId, PillMin, ImVec2(PillW - (ShowX ? 16.0f : 0.0f), PillH));
            if (PillHit.Clicked && Report.Action == HistoryAction::None) { Report.Action = HistoryAction::SelectBranch; Report.Index = Index; }

            if (ShowX)
            {
                const ImVec2 XMin(PillMax.x - 16.0f, Y), XMax(PillMax.x, Y + PillH);
                Draw->AddText(ImVec2(XMin.x + 4.0f, MidY - CountSize.y * 0.5f), Palette.TextFaint, "\xC3\x97");
                char XId[32]; std::snprintf(XId, sizeof(XId), "##brx%d", Index);
                const Hit XHit = RegionButton(XId, XMin, ImVec2(16.0f, PillH));
                if (XHit.Clicked && Report.Action == HistoryAction::None) { Report.Action = HistoryAction::DropBranch; Report.Index = Index; }
            }

            PillX += PillW + 6.0f;
        }

        // fork "+"
        const ImVec2 AddMin(PillX, Y), AddMax(PillX + 22.0f, Y + PillH);
        Draw->AddRectFilled(AddMin, AddMax, Palette.MenuFill, PillH * 0.5f);
        Draw->AddText(ImVec2(PillX + 7.0f, Y + PillH * 0.5f - ImGui::GetTextLineHeight() * 0.5f), Palette.TextDim, "+");
        const Hit AddHit = RegionButton("##bradd", AddMin, ImVec2(22.0f, PillH));
        if (AddHit.Clicked && Report.Action == HistoryAction::None) { Report.Action = HistoryAction::ForkBranch; }
    }

    // The revision timeline (renderRevisions): a vertical rail of rows. Advances Y past the whole list.
    void ConstructTimeline(ImDrawList* Draw, const Frontier::SvgIconRegistry* Icons, InspectorPanelState& State,
                           float X, float& Y, float Width, HistoryReport& Report)
    {
        RevisionStore& Store = State.Revisions;
        if (Store.Active < 0 || Store.Active >= (int)Store.Branches.size()) { return; }
        const RevisionBranch& Branch = Store.Branches[Store.Active];

        if (Branch.Revisions.empty())
        {
            Draw->AddText(ImVec2(X + 8.0f, Y + 8.0f), Palette.TextFaint, "No revisions on this branch.");
            Y += 30.0f;
            return;
        }

        const float RowH  = 42.0f;
        const float RailX = X + 14.0f;
        for (int Index = 0; Index < (int)Branch.Revisions.size(); ++Index)
        {
            const Revision& Rev = Branch.Revisions[Index];
            const bool AtCursor = Index == Branch.Cursor;
            const bool Future   = Index > Branch.Cursor;
            const float RowTop  = Y + Index * RowH;
            const float NodeY   = RowTop + RowH * 0.5f;

            // row fill for the cursor
            if (AtCursor)
            {
                Draw->AddRectFilled(ImVec2(X, RowTop + 2.0f), ImVec2(X + Width, RowTop + RowH - 2.0f), Palette.RowActive, 4.0f);
                Draw->AddRect(ImVec2(X, RowTop + 2.0f), ImVec2(X + Width, RowTop + RowH - 2.0f), Palette.BorderLine, 4.0f);
            }

            // rail: line above (unless first) + node + line below (unless last)
            const ImU32 Hue = 0xFF000000u | RevisionHue(Rev.Category);
            const ImU32 FadedHue = Future ? ((Hue & 0x00FFFFFFu) | 0x66000000u) : Hue;
            if (Index != 0)                              { Draw->AddLine(ImVec2(RailX, RowTop), ImVec2(RailX, NodeY - 6.0f), Palette.BorderLine, 1.4f); }
            if (Index != (int)Branch.Revisions.size()-1) { Draw->AddLine(ImVec2(RailX, NodeY + 6.0f), ImVec2(RailX, RowTop + RowH), Palette.BorderLine, 1.4f); }
            Draw->AddCircleFilled(ImVec2(RailX, NodeY), 5.5f, FadedHue);
            if (AtCursor) { Draw->AddCircle(ImVec2(RailX, NodeY), 8.0f, Palette.Accent, 0, 1.6f); }

            // body: title · type chip · time, then subtitle
            const float BodyX = RailX + 16.0f;
            const ImU32 TitleCol = Future ? Palette.TextFaint : Palette.TextPrimary;
            Draw->AddText(ImVec2(BodyX, RowTop + 8.0f), TitleCol, Rev.Title.c_str());

            const char* TypeLabel = RevisionLabel(Rev.Category);
            const ImVec2 TitleSize = ImGui::CalcTextSize(Rev.Title.c_str());
            const ImVec2 ChipSize  = ImGui::CalcTextSize(TypeLabel);
            const ImVec2 ChipMin(BodyX + TitleSize.x + 8.0f, RowTop + 7.0f);
            const ImVec2 ChipMax(ChipMin.x + ChipSize.x + 10.0f, ChipMin.y + ChipSize.y + 4.0f);
            const ImU32 ChipInk = RevisionToneColour(RevisionToneOf(Rev.Category));
            Draw->AddRectFilled(ChipMin, ChipMax, (ChipInk & 0x00FFFFFFu) | 0x33000000u, 3.0f);
            Draw->AddText(ImVec2(ChipMin.x + 5.0f, ChipMin.y + 2.0f), ChipInk, TypeLabel);

            // time (right-aligned)
            const ImVec2 TimeSize = ImGui::CalcTextSize(Rev.TimeText.c_str());
            Draw->AddText(ImVec2(X + Width - 8.0f - TimeSize.x, RowTop + 8.0f), Palette.TextFaint, Rev.TimeText.c_str());

            if (!Rev.Subtitle.empty())
            {
                Draw->AddText(ImVec2(BodyX, RowTop + 24.0f), Palette.TextDim, Rev.Subtitle.c_str());
            }

            char RowId[32]; std::snprintf(RowId, sizeof(RowId), "##rev%d", Index);
            const Hit RowHit = RegionButton(RowId, ImVec2(X, RowTop), ImVec2(Width, RowH));
            if (RowHit.Clicked && Report.Action == HistoryAction::None) { Report.Action = HistoryAction::JumpTo; Report.Index = Index; }
        }
        Y += Branch.Revisions.size() * RowH;
    }

    void ApplyHistoryReport(InspectorPanelState& State, const HistoryReport& Report)
    {
        RevisionStore& Store = State.Revisions;
        switch (Report.Action)
        {
            case HistoryAction::None: break;
            case HistoryAction::SelectBranch:
                if (Report.Index >= 0 && Report.Index < (int)Store.Branches.size()) { Store.Active = Report.Index; }
                break;
            case HistoryAction::DropBranch:  DropBranch(Store, Report.Index); break;
            case HistoryAction::ForkBranch:  ForkBranch(Store); break;
            case HistoryAction::JumpTo:      JumpToRevision(Store, Report.Index); break;
            case HistoryAction::Undo:        StepBack(Store); break;
            case HistoryAction::Redo:        StepForward(Store); break;
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PANE CHROME
//------------------------------------------------------------------------------------------------------------------------
// 📝 The shared pane-head / pane-foot bands and the small chrome buttons the summon shell hangs off. Each returns whether it was clicked so
//    the shell can report the interaction (report-then-apply). All positions are card-local, offset by the card origin at call time.

namespace
{
    // A pane-head: icon · (label / sub-label) · optional right-side content is drawn by the caller. Returns the head rect for hit-testing.
    void ConstructPaneHead(ImDrawList* Draw, const Frontier::SvgIconRegistry* Icons, ImVec2 TopLeft, float Width,
                           const std::string& IconKey, ImU32 IconTint, const char* Label, const char* Sub, ImU32 SubColour)
    {
        const ImVec2 Min = TopLeft, Max = ImVec2(TopLeft.x + Width, TopLeft.y + PaneHeadH);
        Draw->AddRectFilled(Min, Max, Palette.HeadFill);
        Draw->AddLine(ImVec2(Min.x, Max.y), ImVec2(Max.x, Max.y), Palette.BorderSoft);
        if (!IconKey.empty()) { ConstructGlyph(Draw, Icons, IconKey, ImVec2(TopLeft.x + 10.0f, TopLeft.y + PaneHeadH * 0.5f - 9.0f), 18.0f, IconTint); }
        Draw->AddText(ImVec2(TopLeft.x + 34.0f, TopLeft.y + 8.0f), Palette.TextPrimary, Label);
        if (Sub) { Draw->AddText(ImVec2(TopLeft.x + 34.0f, TopLeft.y + 24.0f), SubColour, Sub); }
    }

    // A pane-foot band with left content drawn by the caller (this only lays the band).
    void ConstructPaneFoot(ImDrawList* Draw, ImVec2 TopLeft, float Width, ImU32 Fill)
    {
        const ImVec2 Min = TopLeft, Max = ImVec2(TopLeft.x + Width, TopLeft.y + PaneFootH);
        Draw->AddRectFilled(Min, Max, Fill);
        Draw->AddLine(Min, ImVec2(Max.x, Min.y), Palette.BorderSoft);
    }

    // A small square chrome button (hdr-btn) carrying one chrome glyph. Returns clicked.
    bool ChromeButton(ImDrawList* Draw, const Frontier::SvgIconRegistry* Icons, const char* Id, ImVec2 Centre,
                      const char* GlyphKey, bool Disabled)
    {
        const float S = 22.0f;
        const ImVec2 Min(Centre.x - S * 0.5f, Centre.y - S * 0.5f), Max(Centre.x + S * 0.5f, Centre.y + S * 0.5f);
        const Hit BtnHit = Disabled ? Hit{ false, false } : RegionButton(Id, Min, ImVec2(S, S));
        if (!Disabled && BtnHit.Hovered) { Draw->AddRectFilled(Min, Max, Palette.MenuHover, 4.0f); }
        ConstructGlyph(Draw, Icons, ChromeGlyphKey(GlyphKey), ImVec2(Centre.x - 8.0f, Centre.y - 8.0f), 16.0f,
                       Disabled ? Palette.TextFaint : Palette.TextDim);
        return !Disabled && BtnHit.Clicked;
    }

    // A segmented control cell (pc-seg). Returns clicked.
    bool SegCell(ImDrawList* Draw, const char* Id, ImVec2 Min, ImVec2 Max, const char* Label, bool Active)
    {
        Draw->AddRectFilled(Min, Max, Active ? Palette.RowActive : Palette.WidgetFill, 4.0f);
        const ImVec2 LabelSize = ImGui::CalcTextSize(Label);
        Draw->AddText(ImVec2((Min.x + Max.x) * 0.5f - LabelSize.x * 0.5f, (Min.y + Max.y) * 0.5f - LabelSize.y * 0.5f),
                      Active ? Palette.TextPrimary : Palette.TextDim, Label);
        const Hit CellHit = RegionButton(Id, Min, ImVec2(Max.x - Min.x, Max.y - Min.y));
        return CellHit.Clicked;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SUMMON SHELL  (public)
//------------------------------------------------------------------------------------------------------------------------

void ConstructSceneDirectoryInspectorPanel(const ThemeConfiguration& Theme,
                                           InspectorPanelState&      State,
                                           const Frontier::SvgIconRegistry* Icons)
{
    const ImGuiIO& Io = ImGui::GetIO();
    const float Dt = Io.DeltaTime;

    // -- Summon / dismiss + Tab-through carousel + revision shortcuts (the prototype's keydown map). --
    const bool Editing = ImGui::GetIO().WantTextInput;   // a field or the filter box holds focus
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && State.Directory.RenameTarget == 0)
    {
        if (!State.SummonOpen)
        {
            State.SummonRequested = true;
            State.RequestX = Io.MousePos.x; State.RequestY = Io.MousePos.y;
        }
        else if (State.OnInspect) { State.OnInspect = false; }                        // slide back to directory
        else if (!State.Directory.SelectionSet.empty()) { State.OnInspect = true; }   // slide to inspect (only with a selection)
    }
    // 🔴 Tab is the ONLY opener. The prototype's right-click summon is gone: in the modelling workspace that hosts this card, right-click
    //    belongs to the construction console, and two surfaces answering one press would race for the same click.
    if (State.SummonOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (State.OnInspect) { State.OnInspect = false; }   // Escape steps inspect -> directory, then dismisses
        else { State.SummonOpen = false; }
    }
    if (State.SummonOpen && State.OnInspect && State.Face == InspectorFace::History && !Editing)
    {
        if (Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { StepBack(State.Revisions); }
        if (Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) { StepForward(State.Revisions); }
    }

    // -- Apply a deferred summon request now (place + clamp the card fully on-screen). --
    if (State.SummonRequested)
    {
        const float Pad = 12.0f;
        float Left = State.RequestX + 10.0f, Top = State.RequestY + 10.0f;
        if (Left + CardWidth  > Io.DisplaySize.x - Pad) { Left = ImMax(Pad, State.RequestX - CardWidth - 10.0f); }
        if (Top  + CardHeight > Io.DisplaySize.y - Pad) { Top  = ImMax(Pad, Io.DisplaySize.y - Pad - CardHeight); }
        State.SummonX = Left; State.SummonY = Top;
        State.SummonOpen = true;
        State.OnInspect  = false;   // always open on slide 1
        State.OpenAge    = 0.0f;
        State.SummonRequested = false;
    }

    // -- Ease the two carousels + the open pop. --
    AdvanceTravel(State.OuterTravel, State.OnInspect, Dt, OuterSeconds);
    AdvanceTravel(State.InnerTravel, State.Face == InspectorFace::History, Dt, InnerSeconds);
    if (State.SummonOpen) { State.OpenAge = ImMin(OpenSeconds, State.OpenAge + Dt); }

    if (!State.SummonOpen) { return; }

    // -- The dismiss veil (a full-viewport dim on the BACKGROUND list so it sits behind the card window, not over it). --
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, IM_COL32(0, 0, 0, 90));
    const ImVec2 CardTL(State.SummonX, State.SummonY);
    const ImVec2 CardBR(CardTL.x + CardWidth, CardTL.y + CardHeight);
    const bool OverCard = Io.MousePos.x >= CardTL.x && Io.MousePos.x <= CardBR.x &&
                          Io.MousePos.y >= CardTL.y && Io.MousePos.y <= CardBR.y;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !OverCard) { State.SummonOpen = false; return; }

    // -- The card shell: a scaled pop on summon, a dark glass fill. --
    const float PopT  = OpenSeconds > 0.0f ? SolveCubicBezier(State.OpenAge / OpenSeconds, 0.16f, 1.0f, 0.3f, 1.0f) : 1.0f;
    const float Scale = 0.98f + 0.02f * PopT;
    const ImVec2 Centre((CardTL.x + CardBR.x) * 0.5f, (CardTL.y + CardBR.y) * 0.5f);
    const ImVec2 ScaledTL(Centre.x - (CardWidth * 0.5f) * Scale, Centre.y - (CardHeight * 0.5f) * Scale);
    const ImVec2 ScaledBR(Centre.x + (CardWidth * 0.5f) * Scale, Centre.y + (CardHeight * 0.5f) * Scale);

    // 🔴 The card + ALL its contents draw on the overlay window's OWN draw list — not the foreground list — so ImGui hit-testing
    //    and draw order agree and every custom button / field inside actually receives clicks. The window is brought to front and
    //    focused on summon so it captures input over the bare host window. Only the veil above rides the foreground list.
    ImGui::SetNextWindowPos(CardTL);
    ImGui::SetNextWindowSize(ImVec2(CardWidth, CardHeight));
    if (State.OpenAge <= 0.0f) { ImGui::SetNextWindowFocus(); }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##SceneDirectoryInspectorCard", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* Fg = ImGui::GetWindowDrawList();
    Fg->AddRectFilled(ScaledTL, ScaledBR, Palette.CardFill, 12.0f);
    Fg->AddRect(ScaledTL, ScaledBR, Palette.BorderLine, 12.0f, 0, 1.0f);

    // Clip everything to the card so the two slides never bleed past the rounded shell.
    Fg->PushClipRect(CardTL, CardBR, true);

    // -- Outer carousel: two slides across a 2× track, shifted by OuterTravel (eased). --
    const float OuterShaped = SolveCubicBezier(State.OuterTravel, 0.4f, 0.0f, 0.2f, 1.0f);
    const float SlideDx = -OuterShaped * CardWidth;

    const float RailW = 214.0f;
    const float DetailW = CardWidth - RailW;

    HistoryReport HistRep;
    bool RequestInspect = false;   // the advance head / CTA asks to slide to inspect
    bool RequestReturn  = false;   // the back head asks to slide to directory

    // -- Resolve the active record the panes read off the reused SketchOutliner's selection into a per-frame scratch mirror. --
    const RecordToken ActiveTok = ActiveToken(State);
    const int         SelCount  = (int)State.Directory.SelectionSet.size();
    ScratchRecord     ActiveRec;
    const bool        HasActive = ResolveActive(State, ActiveTok, ActiveRec);
    ScratchRecord*    Active    = HasActive ? &ActiveRec : nullptr;

    // ========================= SLIDE 1 : directory | metadata =========================
    {
        const float S1X = CardTL.x + SlideDx;

        // -- Directory pane (rail) — the reused SketchOutliner draws its own header · search · filter chips · rows · footer. --
        // 🔴 ConstructSketchOutlinerPanel reads GetCursorScreenPos() for its top-left and GetWindowSize().y for its bottom, and opens its own
        //    child, so it must run inside a child window sized to the rail rect. A child positioned at the rail's top-left gives it exactly the
        //    rail column; the panel then fills that column with its full chrome. The tree scroll clips to the child, not the card.
        const float DirX = S1X;
        ImGui::SetCursorScreenPos(ImVec2(DirX, CardTL.y));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette.PaneFill);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::BeginChild("##sdi-rail", ImVec2(RailW, CardHeight), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::SetCursorScreenPos(ImVec2(DirX, CardTL.y));
            SO::ConstructSketchOutlinerPanel(Theme, State.Directory, Icons, ResolveInspectorContentProfile());
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // -- Metadata pane (detail) --
        const float MetaX = S1X + RailW;
        {
            std::string HeadIcon; ImU32 HeadTint = Palette.TextDim; ImU32 SubCol = Palette.TextFaint;
            const char* HeadName = "Nothing selected"; std::string HeadSub = "\xE2\x80\x94";
            if (SelCount > 1) { HeadIcon = ChromeGlyphKey("target"); HeadName = "records"; HeadSub = "multiple selection"; }
            else if (Active)
            {
                HeadIcon = ClassificationGlyphKey(Active->Classification);
                HeadTint = 0xFF000000u | ClassificationHue(Active->Classification);
                HeadName = Active->Name.c_str();
                HeadSub  = ClassificationLabel(Active->Classification);
                SubCol   = 0xFF000000u | ClassificationHue(Active->Classification);
            }
            char NameBuf[128];
            if (SelCount > 1) { std::snprintf(NameBuf, sizeof(NameBuf), "%d records", SelCount); HeadName = NameBuf; }
            ConstructPaneHead(Fg, Icons, ImVec2(MetaX, CardTL.y), DetailW, HeadIcon, HeadTint, HeadName, HeadSub.c_str(), SubCol);

            // head button: the step-to-inspect chevron (the record actions live on the directory rail's own menus now)
            if (ChromeButton(Fg, Icons, "##advanceStep", ImVec2(MetaX + DetailW - 18.0f, CardTL.y + PaneHeadH * 0.5f), "stepNext",
                             SelCount == 0))
            { RequestInspect = true; }
            // clicking the head body (not a button) also advances
            const Hit HeadHit = RegionButton("##advanceHead", ImVec2(MetaX, CardTL.y), ImVec2(DetailW - 30.0f, PaneHeadH));
            if (HeadHit.Clicked && SelCount != 0) { RequestInspect = true; }

            // metadata body
            const float MBodyTop = CardTL.y + PaneHeadH + 4.0f;
            const float MBodyBot = CardTL.y + CardHeight - PaneFootH;
            Fg->PushClipRect(ImVec2(MetaX, MBodyTop), ImVec2(MetaX + DetailW, MBodyBot), true);
            if (!Active && SelCount == 0)
            {
                Fg->AddText(ImVec2(MetaX + 12.0f, MBodyTop + 8.0f), Palette.TextFaint, "Select a record to read its metadata.");
            }
            else if (Active)
            {
                float MY = MBodyTop + 6.0f;
                // hero
                Fg->AddText(ImVec2(MetaX + 40.0f, MY + 2.0f), Palette.TextPrimary, Active->Name.c_str());
                Fg->AddText(ImVec2(MetaX + 40.0f, MY + 18.0f), 0xFF000000u | ClassificationHue(Active->Classification),
                            ClassificationLabel(Active->Classification));
                ConstructGlyph(Fg, Icons, ClassificationGlyphKey(Active->Classification), ImVec2(MetaX + 12.0f, MY + 4.0f), 24.0f,
                               0xFF000000u | ClassificationHue(Active->Classification));
                MY += 42.0f;
                // read-only summary lines
                CardSpec Cards[8]; const int CardCount = ResolveProfileCards(Active->Classification, Cards);
                int FieldTally = 0; for (int C = 0; C < CardCount; ++C) { FieldTally += Cards[C].FieldCount; }
                const int Nested = NestedTally(State, Active->Token);
                struct Line { const char* K; std::string V; };
                char TokenText[16]; std::snprintf(TokenText, sizeof(TokenText), "%u", Active->Token);
                char FieldText[16]; std::snprintf(FieldText, sizeof(FieldText), "%d", FieldTally);
                char NestText[24];
                if (Nested == 0) { std::snprintf(NestText, sizeof(NestText), "terminal"); }
                else             { std::snprintf(NestText, sizeof(NestText), "%d records", Nested); }
                const Line Lines[] = {
                    { "Token",   TokenText },
                    { "Nested",  NestText },
                    { "Visible", Active->Hidden ? "hidden" : "shown" },
                    { "Fields",  FieldText },
                };
                for (const Line& L : Lines)
                {
                    Fg->AddText(ImVec2(MetaX + 12.0f, MY), Palette.TextDim, L.K);
                    const ImVec2 VSize = ImGui::CalcTextSize(L.V.c_str());
                    Fg->AddText(ImVec2(MetaX + DetailW - 12.0f - VSize.x, MY), Palette.TextPrimary, L.V.c_str());
                    MY += 20.0f;
                }
                MY += 10.0f;
                // Properties + History CTA
                const ImVec2 CtaMin(MetaX + 12.0f, MY), CtaMax(MetaX + DetailW - 12.0f, MY + 30.0f);
                Fg->AddRectFilled(CtaMin, CtaMax, Palette.WidgetFill, 6.0f);
                ConstructGlyph(Fg, Icons, ChromeGlyphKey("sliders"), ImVec2(CtaMin.x + 8.0f, MY + 7.0f), 16.0f, Palette.Accent);
                Fg->AddText(ImVec2(CtaMin.x + 30.0f, MY + 8.0f), Palette.TextPrimary, "Properties + History");
                Fg->AddText(ImVec2(CtaMax.x - 34.0f, MY + 8.0f), Palette.TextFaint, "Tab");
                const Hit CtaHit = RegionButton("##cta", CtaMin, ImVec2(CtaMax.x - CtaMin.x, 30.0f));
                if (CtaHit.Clicked) { RequestInspect = true; }
            }
            Fg->PopClipRect();

            // metadata foot
            ConstructPaneFoot(Fg, ImVec2(MetaX, MBodyBot), DetailW, Palette.MenuFill);
            if (Active)
            {
                char Foot[80]; std::snprintf(Foot, sizeof(Foot), "%s", ClassificationLabel(Active->Classification));
                Fg->AddText(ImVec2(MetaX + 10.0f, MBodyBot + PaneFootH * 0.5f - ImGui::GetTextLineHeight() * 0.5f), Palette.TextDim, Foot);
            }
        }
    }

    // ========================= SLIDE 2 : back-rail | properties ⇄ history =========================
    {
        const float S2X = CardTL.x + SlideDx + CardWidth;

        // -- Back rail (identity + back head) --
        const float BackX = S2X;
        {
            const ImVec2 Min(BackX, CardTL.y), Max(BackX + RailW, CardTL.y + PaneHeadH);
            Fg->AddRectFilled(Min, Max, Palette.HeadFill);
            Fg->AddLine(ImVec2(Min.x, Max.y), ImVec2(Max.x, Max.y), Palette.BorderSoft);
            ConstructGlyph(Fg, Icons, ChromeGlyphKey("stepPrev"), ImVec2(BackX + 10.0f, CardTL.y + PaneHeadH * 0.5f - 9.0f), 18.0f, Palette.TextDim);
            Fg->AddText(ImVec2(BackX + 34.0f, CardTL.y + 8.0f), Palette.TextPrimary, "Back");
            Fg->AddText(ImVec2(BackX + 34.0f, CardTL.y + 24.0f), Palette.TextFaint, "Directory");
            const Hit BackHit = RegionButton("##returnHead", Min, ImVec2(RailW, PaneHeadH));
            if (BackHit.Clicked) { RequestReturn = true; }

            // identity hero mirrors slide 1's active record
            if (Active)
            {
                const float IY = CardTL.y + PaneHeadH + 8.0f;
                ConstructGlyph(Fg, Icons, ClassificationGlyphKey(Active->Classification), ImVec2(BackX + 12.0f, IY), 24.0f,
                               0xFF000000u | ClassificationHue(Active->Classification));
                Fg->AddText(ImVec2(BackX + 42.0f, IY + 2.0f), Palette.TextPrimary, Active->Name.c_str());
                Fg->AddText(ImVec2(BackX + 42.0f, IY + 18.0f), 0xFF000000u | ClassificationHue(Active->Classification),
                            ClassificationLabel(Active->Classification));
            }
            ConstructPaneFoot(Fg, ImVec2(BackX, CardTL.y + CardHeight - PaneFootH), RailW, Palette.HeadFill);
        }

        // -- Detail pane: head (identity + undo/redo) · seg carousel · inner viewport · foot --
        const float DetX = S2X + RailW;
        {
            const bool ShowHistory = State.Face == InspectorFace::History;
            RevisionStore& Store = State.Revisions;
            const RevisionBranch* Branch = (Store.Active >= 0 && Store.Active < (int)Store.Branches.size()) ? &Store.Branches[Store.Active] : nullptr;

            // head
            std::string HeadIcon = ShowHistory ? ChromeGlyphKey("clock")
                                               : (Active ? ClassificationGlyphKey(Active->Classification) : ChromeGlyphKey("sliders"));
            const char* HeadName = ShowHistory ? (Branch ? Branch->Name.c_str() : "History")
                                               : (Active ? Active->Name.c_str() : "Nothing selected");
            char SubBuf[64];
            if (ShowHistory && Branch) { std::snprintf(SubBuf, sizeof(SubBuf), "%d of %d revisions", Branch->Cursor + 1, (int)Branch->Revisions.size()); }
            else if (Active)           { std::snprintf(SubBuf, sizeof(SubBuf), "%s", ClassificationLabel(Active->Classification)); }
            else                       { std::snprintf(SubBuf, sizeof(SubBuf), "\xE2\x80\x94"); }
            const ImU32 HeadTint = (!ShowHistory && Active) ? (0xFF000000u | ClassificationHue(Active->Classification)) : Palette.TextDim;
            ConstructPaneHead(Fg, Icons, ImVec2(DetX, CardTL.y), DetailW, HeadIcon, HeadTint, HeadName, SubBuf, Palette.TextFaint);

            // undo / redo (only meaningful on the History face; the prototype shows them there)
            if (ShowHistory)
            {
                const bool NoUndo = !Branch || Branch->Cursor < 0;
                const bool NoRedo = !Branch || Branch->Cursor >= (int)Branch->Revisions.size() - 1;
                if (ChromeButton(Fg, Icons, "##undo", ImVec2(DetX + DetailW - 42.0f, CardTL.y + PaneHeadH * 0.5f), "undo", NoUndo))
                { HistRep.Action = HistoryAction::Undo; }
                if (ChromeButton(Fg, Icons, "##redo", ImVec2(DetX + DetailW - 18.0f, CardTL.y + PaneHeadH * 0.5f), "redo", NoRedo))
                { HistRep.Action = HistoryAction::Redo; }
            }

            // segmented control (Properties | History)
            const float SegY = CardTL.y + PaneHeadH + 3.0f;
            const float SegW = (DetailW - 16.0f) * 0.5f;
            if (SegCell(Fg, "##segProps", ImVec2(DetX + 8.0f, SegY), ImVec2(DetX + 8.0f + SegW, SegY + CarouselSegH - 6.0f),
                        "Properties", !ShowHistory)) { State.Face = InspectorFace::Properties; }
            if (SegCell(Fg, "##segHist", ImVec2(DetX + 8.0f + SegW, SegY), ImVec2(DetX + DetailW - 8.0f, SegY + CarouselSegH - 6.0f),
                        "History", ShowHistory)) { State.Face = InspectorFace::History; }

            // inner viewport: Properties pane and History pane on a 2× track shifted by InnerTravel
            const float ViewTop = SegY + CarouselSegH;
            const float ViewBot = CardTL.y + CardHeight - PaneFootH;
            const float ViewHeight = ViewBot - ViewTop;
            const float TopPad = 6.0f;   // [px] - the gap the body content keeps below the segmented control

            // 🔴 The Properties + History bodies draw into this fixed clip rect, so content taller than ViewHeight (a Workplane's two cards,
            //    a long timeline) is cut off at ViewBot with no way to reach it. Each face carries its OWN scroll offset: it is subtracted from
            //    the body's start Y, the wheel drives it while the pane is hovered, and it is clamped to [0, content-view] from the extent the
            //    body reported LAST frame (immediate-mode: the true content height is only known after the draw, so the clamp trails by a frame).
            float& ActiveScroll = ShowHistory ? State.HistoryScroll : State.PropertiesScroll;
            const bool PaneHovered =
                Io.MousePos.x >= DetX && Io.MousePos.x <= DetX + DetailW &&
                Io.MousePos.y >= ViewTop && Io.MousePos.y <= ViewBot;
            if (PaneHovered && Io.MouseWheel != 0.0f) { ActiveScroll -= Io.MouseWheel * 34.0f; }

            Fg->PushClipRect(ImVec2(DetX, ViewTop), ImVec2(DetX + DetailW, ViewBot), true);
            const float InnerShaped = SolveCubicBezier(State.InnerTravel, 0.4f, 0.0f, 0.2f, 1.0f);
            const float InnerDx = -InnerShaped * DetailW;

            // Properties pane
            float PropContentH = 0.0f;
            {
                const float PX = DetX + InnerDx;
                if (!Active) { Fg->AddText(ImVec2(PX + 12.0f, ViewTop + 8.0f), Palette.TextFaint, "Select a record to inspect its properties."); }
                else
                {
                    RecordProfile& Profile = ProfileFor(State, Active->Token);
                    EstablishProfile(Active->Classification, NestedTally(State, Active->Token), !Active->Hidden, Profile);
                    const float PStart = ViewTop + TopPad - State.PropertiesScroll;
                    float PY = PStart;
                    CardSpec Cards[8]; const int CardCount = ResolveProfileCards(Active->Classification, Cards);
                    for (int C = 0; C < CardCount; ++C)
                    {
                        const std::string Toggled = ConstructCard(Fg, Icons, State, *Active, Profile, Cards[C], PX + 8.0f, PY, DetailW - 16.0f);
                        if (!Toggled.empty()) { ToggleCardFold(State, Toggled); }
                    }
                    PropContentH = (PY - PStart) + TopPad;   // full body extent, top pad + cards
                }
            }
            // History pane
            float HistContentH = 0.0f;
            {
                const float HX = DetX + InnerDx + DetailW;
                const float HStart = ViewTop + TopPad - State.HistoryScroll;
                float HY = HStart;
                ConstructBranchPills(Fg, State, HX + 8.0f, HY, DetailW - 16.0f, HistRep);
                HY += 30.0f;
                ConstructTimeline(Fg, Icons, State, HX + 8.0f, HY, DetailW - 16.0f, HistRep);
                HistContentH = (HY - HStart) + TopPad;
            }
            Fg->PopClipRect();

            // -- Clamp each face's scroll to [0, content - viewport] from the extent just measured (0 when the body fits). --
            const float PropMax = (PropContentH > ViewHeight) ? (PropContentH - ViewHeight) : 0.0f;
            const float HistMax = (HistContentH > ViewHeight) ? (HistContentH - ViewHeight) : 0.0f;
            if (State.PropertiesScroll < 0.0f)      { State.PropertiesScroll = 0.0f; }
            if (State.PropertiesScroll > PropMax)   { State.PropertiesScroll = PropMax; }
            if (State.HistoryScroll < 0.0f)         { State.HistoryScroll = 0.0f; }
            if (State.HistoryScroll > HistMax)      { State.HistoryScroll = HistMax; }

            // -- A thin scrollbar hint on the right edge when the active face overflows, so the clip is legibly "more below". --
            {
                const float FaceContentH = ShowHistory ? HistContentH : PropContentH;
                if (FaceContentH > ViewHeight)
                {
                    const float TrackX = DetX + DetailW - 4.0f;
                    const float ThumbFrac = ViewHeight / FaceContentH;
                    const float ThumbH = ViewHeight * ThumbFrac;
                    const float FaceMax = FaceContentH - ViewHeight;
                    const float ThumbY = ViewTop + (ViewHeight - ThumbH) * (FaceMax > 0.0f ? (ActiveScroll / FaceMax) : 0.0f);
                    Fg->AddRectFilled(ImVec2(TrackX, ThumbY), ImVec2(TrackX + 3.0f, ThumbY + ThumbH), Palette.BorderLine, 1.5f);
                }
            }

            // detail foot
            ConstructPaneFoot(Fg, ImVec2(DetX, ViewBot), DetailW, Palette.MenuFill);
            char Foot[96];
            if (ShowHistory && Branch)
            {
                std::snprintf(Foot, sizeof(Foot), "%d revisions \xC2\xB7 %d branch%s",
                              (int)Branch->Revisions.size(), (int)Store.Branches.size(), Store.Branches.size() == 1 ? "" : "es");
            }
            else
            {
                CardSpec Cards[8]; int FieldTally = 0;
                if (Active) { const int CC = ResolveProfileCards(Active->Classification, Cards); for (int C = 0; C < CC; ++C) { FieldTally += Cards[C].FieldCount; } }
                std::snprintf(Foot, sizeof(Foot), "%d fields", FieldTally);
            }
            Fg->AddText(ImVec2(DetX + 10.0f, ViewBot + PaneFootH * 0.5f - ImGui::GetTextLineHeight() * 0.5f), Palette.TextDim, Foot);
        }
    }

    Fg->PopClipRect();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // -- Report-then-apply: fold this card's own interactions into state AFTER the whole card drew (the directory rail's SketchOutliner
    //    already applied its own row / rename / drag / eye / menu mutations inside its call above). --
    // Push a card-side Name edit back onto the reused tree row, and log the rename as a revision (the prototype's rename -> edit map).
    if (HasActive)
    {
        if (SO::RecordEntry* Row = ResolveOutlinerEntry(State.Directory.RootRegion, ActiveRec.Token))
        {
            if (Row->Label != ActiveRec.Name && !ActiveRec.Name.empty())
            {
                char Title[200]; std::snprintf(Title, sizeof(Title), "Rename %s", ActiveRec.Name.c_str());
                LogRevision(State, RevisionCategory::Edit, Title, Row->Label.c_str());
                Row->Label = ActiveRec.Name;
            }
        }
    }
    ApplyHistoryReport(State, HistRep);
    if (RequestInspect && SelCount != 0) { State.OnInspect = true; }
    if (RequestReturn) { State.OnInspect = false; }

    // Track the shown token so a selection change is observable next frame (drives the card refresh + future revision deltas).
    State.ShownToken = ActiveTok;
}

}   // namespace SceneDirectoryInspectorValidation
