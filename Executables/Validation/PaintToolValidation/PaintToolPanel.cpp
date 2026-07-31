/*==============================================================================================================================================
                                                          PAINTTOOLPANEL.CPP
==============================================================================================================================================*/
// 🧩 The card assembled: state transitions, the per-frame resolve->draw->apply loop, and the parameter aliasing the preview reads.
//    Ported from Documentation/Prototypes/PaintToolMenu.html's RenderCard() / RenderOptions() / Commit() / PaintPreview().

#include "PaintToolPanel.h"

#include "PaintCatalogue.h"
#include "PaintIconPack.h"
#include "PaintIconStore.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 One wheel notch, in pixels. Not from the prototype — its panes are native scrolling divs and the step is the browser's.
constexpr float PaintScrollWheelStep = 42.0f;


bool SameKey(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr) { return false; }
    return std::strcmp(Left, Right) == 0;
}


// 📝 The prototype's `ActiveParams.<key>` lookup. Returns false when the key is not declared at all, which is the
//    distinction the `??` chain below is built on — an ABSENT key and a key holding 0 take different branches.
bool ReadKeyedReading(const PaintSchema& Schema, const PaintControlValue* Values, int ValueCount,
                      const char* Key, float& Reading)
{
    int ValueIndex = 0;
    for (int GroupIndex = 0; GroupIndex < Schema.GroupCount; ++GroupIndex)
    {
        const PaintControlGroup& Group = Schema.Groups[GroupIndex];
        for (int ControlIndex = 0; ControlIndex < Group.ControlCount; ++ControlIndex)
        {
            if (ValueIndex >= ValueCount) { return false; }

            if (SameKey(Group.Controls[ControlIndex].Key, Key))
            {
                Reading = Values[ValueIndex].Reading;
                return true;
            }
            ++ValueIndex;
        }
    }
    return false;
}

// 📝 Both transitions below shut the dropdown, which the prototype gets for free by rebuilding the pane. Here the open row is
//    remembered across frames, so a slide change or an instrument swap would float a list over a pane that no longer owns it.
void DismissOpenSelect(PaintOptionsState& Options)
{
    Options.OpenSelectValueIndex = -1;
}


// 📝 The family an instrument belongs to. Resolved through the family KEY rather than by dividing the index, because the
//    catalogue's bands are contiguous but not equal-sized (eraser has 5, brush has 23).
const PaintFamilyDescriptor& ResolveFamilyOf(const PaintInstrumentDescriptor& Instrument)
{
    int FamilyCount = 0;
    const PaintFamilyDescriptor* Families = ResolvePaintFamilies(FamilyCount);
    for (int Index = 0; Index < FamilyCount; ++Index)
    {
        if (SameKey(Families[Index].Key, Instrument.FamilyKey)) { return Families[Index]; }
    }
    return Families[0];
}


// 📝 The panes are drawn into the shell's clip with the window draw list, not into child windows, so there is no ImGui scroll to
//    inherit — the wheel is read here and the offset clamped to the content the pane actually has.
//    🔴 Clamped against the CURRENT content height every frame, not just on change: the options list grows and shrinks as
//       live-state rows appear, and an offset held from a taller list would leave the pane scrolled past its own last row.
float AdvancePaneScroll(const PaintPaneRegion& Region, float ScrollOffset, float ContentHeight)
{
    const float ViewHeight   = Region.BodyMaximum.y - Region.BodyMinimum.y;
    const float ScrollLimit  = (ContentHeight > ViewHeight) ? (ContentHeight - ViewHeight) : 0.0f;

    const bool PointerInside = ImGui::IsMouseHoveringRect(Region.BodyMinimum, Region.BodyMaximum, false);
    if (PointerInside)
    {
        ScrollOffset -= ImGui::GetIO().MouseWheel * PaintScrollWheelStep;
    }

    if (ScrollOffset < 0.0f)        { ScrollOffset = 0.0f; }
    if (ScrollOffset > ScrollLimit) { ScrollOffset = ScrollLimit; }
    return ScrollOffset;
}


// 📝 The prototype's Commit(): write the value, then let the caller re-render. The re-render is the NEXT frame's resolve here,
//    which is the whole reason the column reports instead of applying.
//    🔴 A committed option can add or remove a row, so the open dropdown is dismissed on a choice — the prototype's rebuild
//       destroys the open list as a side effect, and leaving it open here would float it over a row that may no longer exist.
void ApplyOptionsOutcome(PaintToolPanelState& State, const PaintOptionsOutcome& Outcome)
{
    if (Outcome.Request == PaintOptionsRequest::None)   { return; }
    if (Outcome.ValueIndex < 0 || Outcome.ValueIndex >= State.ValueCount) { return; }

    PaintControlValue& Value = State.Values[Outcome.ValueIndex];

    switch (Outcome.Request)
    {
        case PaintOptionsRequest::SetReading:       Value.Reading      = Outcome.Reading;      break;
        case PaintOptionsRequest::ChooseOption:     Value.ChosenOption = Outcome.ChosenOption;
                                                    DismissOpenSelect(State.Options);          break;
        case PaintOptionsRequest::ToggleActivation: Value.Activated    = Outcome.Activated;    break;
        default:                                                                               break;
    }
}

} // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                     STATE TRANSITIONS
//------------------------------------------------------------------------------------------------------------------------

void OpenPaintToolPanel(PaintToolPanelState& State, int FamilyIndex)
{
    int FamilyCount = 0;
    ResolvePaintFamilies(FamilyCount);
    if (FamilyIndex < 0)            { FamilyIndex = 0; }
    if (FamilyIndex >= FamilyCount) { FamilyIndex = FamilyCount - 1; }

    State.FamilyIndex = FamilyIndex;

    // 🔴 The card opens on the LIBRARY slide with a valid slide 2 behind it. The prototype's OpenMenu only renders slide 1, but its
    //    slide 2 reads `ActiveTool` which is null until OpenTool runs — a null the DOM tolerates and a schema walk does not. So the
    //    family's first instrument is selected here, giving the options column real rows before the carousel can ever reach it.
    int FirstIndex = 0;
    int LastIndex  = 0;
    ResolvePaintFamilyRange(State.FamilyIndex, FirstIndex, LastIndex);
    SelectPaintInstrument(State, FirstIndex < LastIndex ? FirstIndex : -1);

    State.Shell.Slide      = PaintCardSlide::Library;
    State.Shell.SlidePhase = 0.0f;
    State.Shell.OpenPhase  = 0.0f;   // the pop replays from the start on every open, as the class re-add does
    State.Shell.IsOpen     = true;

    State.GridScroll    = 0.0f;
    State.OptionsScroll = 0.0f;
    RequestPaintGridReplay(State.Grid, State.FamilyIndex);
}


void ClosePaintToolPanel(PaintToolPanelState& State)
{
    // 📝 CloseMenu() also clears OptionsShown, so a re-open starts on the grid rather than resuming slide 2. Requested rather than
    //    assigned so the carousel travels back while the card fades, which is what dropping both classes at once does.
    State.Shell.IsOpen = false;
    RequestPaintCardSlide(State.Shell, PaintCardSlide::Library);
    DismissOpenSelect(State.Options);
}


void SelectPaintInstrument(PaintToolPanelState& State, int InstrumentIndex)
{
    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* Instruments = ResolvePaintInstruments(InstrumentCount);

    if (InstrumentIndex < 0 || InstrumentIndex >= InstrumentCount)
    {
        State.InstrumentIndex = -1;
        State.Schema          = PaintSchema{};
        State.ValueCount      = 0;
        DismissOpenSelect(State.Options);
        return;
    }

    const PaintInstrumentDescriptor& Instrument = Instruments[InstrumentIndex];

    State.InstrumentIndex = InstrumentIndex;
    State.Schema          = ResolvePaintSchema(Instrument);
    State.ValueCount      = SeedPaintValues(Instrument, State.Schema, State.Values, PaintSchemaValueLimit);

    // 📝 SwatchIndex = 0 in OpenTool. It is reset rather than carried because the swatch list is per FAMILY and ragged (eraser has
    //    one ink, brush has five), so an index held across a swap can name a colour the new family does not offer.
    State.Preview.SwatchIndex = 0;
    State.OptionsScroll       = 0.0f;
    DismissOpenSelect(State.Options);

    // The new instrument's switches mount already in position, as the prototype's rebuilt pane does — see SettlePaintSwitches.
    SettlePaintSwitches(State.Options, State.Values, State.ValueCount);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     PARAMETER RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

PaintStrokeParameters ResolvePaintStrokeParameters(const PaintSchema& Schema,
                                                   const PaintControlValue* Values, int ValueCount)
{
    PaintStrokeParameters Parameters;

    // 🔴 The prototype's six lines, transcribed rather than paraphrased:
    //      Size     = Number(P.size ?? 8)
    //      Opacity  = Number(P.opacity ?? 100) / 100
    //      Flow     = Number(P.flow ?? 90) / 100
    //      Grain    = Number(P.grain ?? P.bristleTx ?? 0) / 100
    //      Scatter  = Number(P.scatter ?? P.spread ?? 0) / 100
    //      Softness = Number(P.softness ?? (100 - (P.hardness ?? 100))) / 100
    //    `??` falls through on ABSENT only, never on zero — which is why the reader above reports presence separately
    //    rather than returning a 0 that would be indistinguishable from a genuine 0 reading.
    float Reading = 0.0f;

    if (ReadKeyedReading(Schema, Values, ValueCount, "size", Reading))    { Parameters.SizePixels = Reading; }
    if (ReadKeyedReading(Schema, Values, ValueCount, "opacity", Reading)) { Parameters.Opacity    = Reading / 100.0f; }
    if (ReadKeyedReading(Schema, Values, ValueCount, "flow", Reading))    { Parameters.Flow       = Reading / 100.0f; }

    // grain ?? bristleTx ?? 0 — the two keys that both mean tooth.
    if      (ReadKeyedReading(Schema, Values, ValueCount, "grain", Reading))     { Parameters.Grain = Reading / 100.0f; }
    else if (ReadKeyedReading(Schema, Values, ValueCount, "bristleTx", Reading)) { Parameters.Grain = Reading / 100.0f; }
    else                                                                        { Parameters.Grain = 0.0f; }

    // scatter ?? spread ?? 0
    if      (ReadKeyedReading(Schema, Values, ValueCount, "scatter", Reading)) { Parameters.Scatter = Reading / 100.0f; }
    else if (ReadKeyedReading(Schema, Values, ValueCount, "spread", Reading))  { Parameters.Scatter = Reading / 100.0f; }
    else                                                                      { Parameters.Scatter = 0.0f; }

    // 🔴 softness ?? (100 - (hardness ?? 100)). The nesting is load bearing and easy to flatten wrongly: when BOTH keys are
    //    absent the fallback is `100 - 100 = 0`, NOT 100. Reading it as "default softness 100" would make every instrument
    //    that declares neither key paint a fully-faded dab instead of a hard one.
    if (ReadKeyedReading(Schema, Values, ValueCount, "softness", Reading))
    {
        Parameters.Softness = Reading / 100.0f;
    }
    else
    {
        const float Hardness = ReadKeyedReading(Schema, Values, ValueCount, "hardness", Reading) ? Reading : 100.0f;
        Parameters.Softness  = (100.0f - Hardness) / 100.0f;
    }

    return Parameters;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        THE PER-FRAME PASS
//------------------------------------------------------------------------------------------------------------------------

void ConstructPaintToolPanel(PaintToolPanelState& State, const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                             SvgIconRegistry* Registry, PaintIconStore* StripStore, ImVec2 CardCentre)
{
    const float DeltaSeconds = ImGui::GetIO().DeltaTime;

    AdvancePaintCardShell(State.Shell, Metrics, DeltaSeconds);
    AdvancePaintRail(State.Rail, Metrics, State.FamilyIndex, DeltaSeconds);
    AdvancePaintGrid(State.Grid, Metrics, DeltaSeconds);
    AdvancePaintOptions(State.Options, State.Values, State.ValueCount, DeltaSeconds);

    const ImVec2 TopLeft(CardCentre.x - Metrics.CardWidth * 0.5f, CardCentre.y - Metrics.CardHeight * 0.5f);

    // 🔴 STEP 1 of the fixed order — resolve from LAST frame's values, before anything is drawn. Everything below reads this
    //    list; nothing below may rebuild it. The commit at the bottom is what makes the next frame's list differ.
    PaintVisibleControl Visible[PaintVisibleControlLimit] = {};
    int VisibleCount = 0;
    int VisibleGroupCount = 0;

    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* Instruments = ResolvePaintInstruments(InstrumentCount);
    const PaintInstrumentDescriptor* Active =
        (State.InstrumentIndex >= 0 && State.InstrumentIndex < InstrumentCount) ? &Instruments[State.InstrumentIndex] : nullptr;

    if (Active != nullptr)
    {
        VisibleCount = ResolveVisiblePaintControls(*Active, State.Schema, State.Values, State.ValueCount,
                                                   Visible, PaintVisibleControlLimit);
        for (int Index = 0; Index < VisibleCount; ++Index)
        {
            if (Visible[Index].StartsGroup) { ++VisibleGroupCount; }
        }
    }

    const PaintStrokeParameters Parameters = ResolvePaintStrokeParameters(State.Schema, State.Values, State.ValueCount);

    // 📝 Everything a pane reports is collected and applied AFTER the shell closes, for the reason the header states: an applied
    //    edit can change the row set, and the rows above it in this frame are already emitted.
    int                 ClickedFamily     = -1;
    int                 ClickedInstrument = -1;
    int                 ClickedSwatch     = -1;
    bool                BackRequested     = false;
    PaintOptionsOutcome Outcome;
    PaintOptionsRequest FootRequest = PaintOptionsRequest::None;

    if (BeginPaintCardShell(Palette, Metrics, State.Shell, TopLeft))
    {
        //---------------------------------------------- SLIDE 1 — RAIL + GRID -----------------------------------------------
        int FamilyCount = 0;
        const PaintFamilyDescriptor* Families = ResolvePaintFamilies(FamilyCount);
        const PaintFamilyDescriptor& Family   = Families[State.FamilyIndex];

        const PaintPaneRegion RailRegion = ResolvePaintPaneRegion(Metrics, State.Shell, TopLeft, PaintCardSlide::Library,
                                                                  true, Metrics.HeaderHeight, 0.0f);
        if (RailRegion.IsVisible)
        {
            ConstructPaintRailHeader(Palette, Metrics, Family.DotColour, FamilyCount, InstrumentCount,
                                     ImVec2(RailRegion.BodyMinimum.x, TopLeft.y), Metrics.LeftColumnWidth);
            ClickedFamily = ConstructPaintInstrumentRail(Palette, Metrics, State.Rail, RailRegion,
                                                         Families, FamilyCount, State.FamilyIndex);
        }

        const PaintPaneRegion GridRegion = ResolvePaintPaneRegion(Metrics, State.Shell, TopLeft, PaintCardSlide::Library,
                                                                  false, Metrics.HeaderHeight, Metrics.GridFootHeight);
        if (GridRegion.IsVisible)
        {
            int FirstIndex = 0;
            int LastIndex  = 0;
            ResolvePaintFamilyRange(State.FamilyIndex, FirstIndex, LastIndex);

            // 📝 The prototype's stand-in face: the header tile shows the family's FIRST instrument when the selection belongs to
            //    another band, because an empty black square beside a populated grid reads as a load failure.
            const bool FaceIsActive = (Active != nullptr) && SameKey(Active->FamilyKey, Family.Key);
            const int  FaceIndex    = FaceIsActive ? State.InstrumentIndex : FirstIndex;

            char GridSubtitle[64] = {};
            std::snprintf(GridSubtitle, sizeof(GridSubtitle), "%d instruments", LastIndex - FirstIndex);
            char GridTally[16] = {};
            std::snprintf(GridTally, sizeof(GridTally), "%d", LastIndex - FirstIndex);

            PaintPaneHeader Header;
            Header.Title       = Family.Caption;
            Header.Subtitle    = GridSubtitle;
            Header.TallyText   = GridTally;
            Header.TallyAccent = true;
            Header.IconIsNib   = true;
            Header.IconTexture = (Registry != nullptr) ? ResolveIconTexture(*Registry, ResolvePaintNibKey(FaceIndex)) : 0;
            (void)ConstructPaintPaneHeader(Palette, Metrics, Header, ImVec2(GridRegion.BodyMinimum.x, TopLeft.y),
                                           Metrics.RightColumnWidth);

            State.GridScroll = AdvancePaneScroll(GridRegion, State.GridScroll,
                                                 ResolvePaintGridContentHeight(Metrics, State.FamilyIndex));

            PaintGridArtSources Art;
            Art.Registry   = Registry;
            Art.StripStore = StripStore;
            Art.ArtMode    = State.ArtMode;
            ClickedInstrument = ConstructPaintInstrumentGrid(Palette, Metrics, State.Grid, GridRegion, Art,
                                                             State.FamilyIndex, State.InstrumentIndex, State.GridScroll);

            ConstructPaintGridFoot(Palette, Metrics,
                                   (Active != nullptr) ? Active->Label : nullptr, InstrumentCount,
                                   ImVec2(GridRegion.BodyMinimum.x, GridRegion.BodyMaximum.y), Metrics.RightColumnWidth);
        }

        //------------------------------------------- SLIDE 2 — PREVIEW + OPTIONS --------------------------------------------
        const PaintPaneRegion PreviewRegion = ResolvePaintPaneRegion(Metrics, State.Shell, TopLeft, PaintCardSlide::Options,
                                                                     true, Metrics.HeaderHeight, 0.0f);
        if (PreviewRegion.IsVisible && Active != nullptr)
        {
            const PaintFamilyDescriptor& OwnFamily = ResolveFamilyOf(*Active);

            char BackSubtitle[96] = {};
            std::snprintf(BackSubtitle, sizeof(BackSubtitle), "%s · %d instruments", OwnFamily.Caption, OwnFamily.Tally);

            PaintPaneHeader Header;
            Header.Title     = "Back";
            Header.Subtitle  = BackSubtitle;
            Header.IsBackRow = true;
            BackRequested    = ConstructPaintPaneHeader(Palette, Metrics, Header,
                                                        ImVec2(PreviewRegion.BodyMinimum.x, TopLeft.y),
                                                        Metrics.LeftColumnWidth);

            ClickedSwatch = ConstructPaintPreviewColumn(Palette, Metrics, PreviewRegion, State.Preview, Parameters,
                                                        Registry, StripStore, State.InstrumentIndex,
                                                        VisibleGroupCount, State.ValueCount);
        }

        const PaintPaneRegion OptionsRegion = ResolvePaintPaneRegion(Metrics, State.Shell, TopLeft, PaintCardSlide::Options,
                                                                     false, Metrics.HeaderHeight, Metrics.OptionsFootHeight);
        if (OptionsRegion.IsVisible && Active != nullptr)
        {
            PaintPaneHeader Header;
            Header.Title       = Active->Label;
            Header.Subtitle    = ResolveFamilyOf(*Active).Caption;
            Header.IconIsNib   = true;
            Header.IconTexture = (Registry != nullptr)
                               ? ResolveIconTexture(*Registry, ResolvePaintNibKey(State.InstrumentIndex)) : 0;
            (void)ConstructPaintPaneHeader(Palette, Metrics, Header, ImVec2(OptionsRegion.BodyMinimum.x, TopLeft.y),
                                           Metrics.RightColumnWidth);

            State.OptionsScroll = AdvancePaneScroll(OptionsRegion, State.OptionsScroll,
                                                    ResolvePaintOptionsContentHeight(Metrics, Visible, VisibleCount));

            Outcome = ConstructPaintOptionsColumn(Palette, Metrics, OptionsRegion, State.Options, Registry,
                                                  Visible, VisibleCount, State.Values, State.ValueCount,
                                                  State.OptionsScroll);

            FootRequest = ConstructPaintOptionsFoot(Palette, Metrics, VisibleCount, VisibleGroupCount,
                                                    ImVec2(OptionsRegion.BodyMinimum.x, OptionsRegion.BodyMaximum.y),
                                                    Metrics.RightColumnWidth);
        }

        EndPaintCardShell();
    }

    // 🔴 The toast is drawn AFTER the clip is popped and with the foreground list — `position:fixed; z-index:120` sits over the
    //    card, not inside a pane, and drawing it before EndPaintCardShell would clip it to the options column.
    ConstructPaintToast(Palette, Metrics, State.Options, CardCentre.x, TopLeft.y + Metrics.CardHeight);

    //-------------------------------------------------- STEP 3 — COMMIT ---------------------------------------------------
    // 📝 Order matters only in that the schema-changing acts come last: a family or instrument change discards the value set the
    //    edits below would have written into, so applying an edit first and then swapping would spend it on a dead schema.
    ApplyOptionsOutcome(State, Outcome);

    if (FootRequest == PaintOptionsRequest::ResetToDefaults && Active != nullptr)
    {
        // Re-seed from the schema, NOT a re-resolve: SeedParams(ActiveTool) rebuilds the values and leaves the rows alone.
        State.ValueCount = SeedPaintValues(*Active, State.Schema, State.Values, PaintSchemaValueLimit);
        DismissOpenSelect(State.Options);
        // Reset re-renders the pane too, so a switch returning to its default appears there rather than sliding back.
        SettlePaintSwitches(State.Options, State.Values, State.ValueCount);

        char Message[128] = {};
        std::snprintf(Message, sizeof(Message), "Reset %s", Active->Label);
        FlashPaintToast(State.Options, Message);
    }
    else if (FootRequest == PaintOptionsRequest::ApplyInstrument && Active != nullptr)
    {
        float SizeReading = 0.0f;
        const bool HasSize = ReadKeyedReading(State.Schema, State.Values, State.ValueCount, "size", SizeReading);

        char Message[160] = {};
        if (HasSize) { std::snprintf(Message, sizeof(Message), "%s active · %g px", Active->Label, SizeReading); }
        else         { std::snprintf(Message, sizeof(Message), "%s active · — px", Active->Label); }

        FlashPaintToast(State.Options, Message);
        ClosePaintToolPanel(State);
    }

    if (ClickedSwatch >= 0) { State.Preview.SwatchIndex = ClickedSwatch; }

    if (BackRequested)
    {
        RequestPaintCardSlide(State.Shell, PaintCardSlide::Library);
        DismissOpenSelect(State.Options);
    }

    if (ClickedFamily >= 0)
    {
        State.FamilyIndex = ClickedFamily;
        State.GridScroll  = 0.0f;
        // 📝 The unguarded replay, because this IS the prototype's click site: it rebuilds the grid and re-adds the fade class
        //    whether or not the band changed. The guarded restart is the one a render path may call; this is not a render path.
        RequestPaintGridReplay(State.Grid, State.FamilyIndex);
    }

    if (ClickedInstrument >= 0)
    {
        SelectPaintInstrument(State, ClickedInstrument);
        RequestPaintCardSlide(State.Shell, PaintCardSlide::Options);
    }
}

} // namespace Frontier
