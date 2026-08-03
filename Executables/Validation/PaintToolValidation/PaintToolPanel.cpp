/*==============================================================================================================================================
                                                          PAINTTOOLPANEL.CPP
==============================================================================================================================================*/
// 🧩 The card assembled on the shared console: state transitions, the metrics + footer paint pushes into the descriptor, the per-frame
//    resolve->console->fold loop, and the parameter aliasing the preview reads. The two-slide shell, the rail, the grid and the four parameter
//    widgets are the CONSOLE's now — this file drives them with paint's own catalogue and folds the console's reports back into paint's values.
//    Ported behaviour from Documentation/Prototypes/PaintToolMenu.html's RenderCard() / RenderOptions() / Commit() / PaintPreview().

#include "PaintToolPanel.h"

#include "PaintCatalogue.h"
#include "PaintIconPack.h"
#include "PaintIconStore.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"
#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Token/MetricsSpecification.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

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


// 📝 Paint's box geometry, pushed into the shared MetricsSpecification. The console resolves the theme-scaled defaults (modelling's
//    523x420 numbers), and paint overrides the fields it disagrees on — which is nearly all of them, since paint's card is 560x420 with a
//    363 px right column and a set of parameter-row numbers a third shorter than modelling's. Built from the ALREADY-SCALED PaintCardMetrics
//    so the two cards scale together off one UiScale knob.
//    🔴 Only the fields MetricsSpecification actually carries are copied; PaintCardMetrics' preview/rail/grid-swap numbers stay paint's own,
//       read by paint's own painters, and have no console counterpart to write.
MetricsSpecification ResolvePaintConsoleMetrics(const ThemeConfiguration& Theme, const PaintCardMetrics& Card)
{
    MetricsSpecification Metrics = ResolveConsoleMetrics(Theme);

    // ── the box ──
    Metrics.CardWidth        = Card.CardWidth;
    Metrics.CardHeight       = Card.CardHeight;
    Metrics.LeftColumnWidth  = Card.LeftColumnWidth;
    Metrics.RightColumnWidth = Card.RightColumnWidth;
    Metrics.CardRounding     = Card.CardRounding;

    // ── pane bands ──
    Metrics.HeaderHeight      = Card.HeaderHeight;
    Metrics.GridFootHeight    = Card.GridFootHeight;
    Metrics.OptionsFootHeight = Card.OptionsFootHeight;

    // ── rail + grid ──
    Metrics.RailRowHeight  = Card.RailRowHeight;
    Metrics.GridColumnCount = Card.GridColumnCount;
    Metrics.TileGap         = Card.TileGap;
    Metrics.TileRounding    = Card.TileRounding;
    Metrics.TileArtEdge     = Card.WellDiameter;   // paint's tile carries a round well, not a glyph mark

    // ── parameter rows ──
    Metrics.ParameterPadding  = Card.OptionsPaddingY;   // .options-body padding
    Metrics.ParameterRowGap   = Card.OptionsRowGap;
    Metrics.ParameterLabelGap = Card.ControlGap;
    Metrics.ValuePillHeight   = Card.ValueBoxHeight;
    Metrics.SliderTrackHeight = Card.SliderHeight;
    Metrics.SliderKnobEdge    = Card.SliderKnobEdge;
    Metrics.SegmentHeight     = Card.SegmentHeight;
    Metrics.SegmentGap        = Card.SegmentGap;
    Metrics.SwitchWidth       = Card.SwitchWidth;
    Metrics.SwitchHeight      = Card.SwitchHeight;
    Metrics.SwitchNubEdge     = Card.SwitchNubEdge;

    // ── animation ──
    Metrics.CarouselSeconds = Card.CarouselSeconds;
    Metrics.OpenSeconds     = Card.OpenSeconds;

    return Metrics;
}


// 📝 The visible list resolved from THIS state's live values, plus the group tally the footer note reports. Pulled into one place because
//    both the bridge context (which builds the options arena from it) and the footer tally read the same list, and re-resolving it twice
//    could drift if the values changed between the two calls.
int ResolveVisibleNow(const PaintToolPanelState& State, PaintVisibleControl* Visible, int Capacity, int& GroupCount)
{
    GroupCount = 0;

    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* Instruments = ResolvePaintInstruments(InstrumentCount);
    if (State.InstrumentIndex < 0 || State.InstrumentIndex >= InstrumentCount) { return 0; }

    const PaintInstrumentDescriptor& Active = Instruments[State.InstrumentIndex];
    const int Count = ResolveVisiblePaintControls(Active, State.Schema, State.Values, State.ValueCount, Visible, Capacity);

    for (int Index = 0; Index < Count; ++Index)
    {
        if (Visible[Index].StartsGroup) { ++GroupCount; }
    }
    return Count;
}


// 🔴 Fold the console's parameter edits back into paint's value array. The console mutates its ParameterBlock.Rows[] in place — there is no
//    outcome struct — and the block's rows are in the SAME order as the arena the bridge built from the visible list, so row i maps to the
//    value slot Visible[i].ValueIndex. Written here because the bridge's ConvertControl maps paint->console going in, and this is the reverse
//    it has no place to do (the bridge cannot see State.Values). Returns true when any value changed, so a live preview re-resolves.
bool FoldParameterBlock(PaintToolPanelState& State, const PaintVisibleControl* Visible, int VisibleCount)
{
    const int Bound = (VisibleCount < State.Parameters.RowCount) ? VisibleCount : State.Parameters.RowCount;
    bool Changed = false;

    for (int Row = 0; Row < Bound; ++Row)
    {
        const int ValueIndex = Visible[Row].ValueIndex;
        if (ValueIndex < 0 || ValueIndex >= State.ValueCount) { continue; }

        PaintControlValue&    Value  = State.Values[ValueIndex];
        const ParameterState& Reading = State.Parameters.Rows[Row];

        if (Value.Reading != Reading.Reading)           { Value.Reading = Reading.Reading;           Changed = true; }
        if (Value.ChosenOption != Reading.ChosenOption) { Value.ChosenOption = Reading.ChosenOption; Changed = true; }
        if (Value.Activated != Reading.Activation)      { Value.Activated = Reading.Activation;      Changed = true; }
    }
    return Changed;
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

    // 🔴 The card opens on the LIBRARY (action) slide with a valid slide 2 behind it. The family's first instrument is selected here, giving
    //    the options column real rows before the carousel can ever reach it — a schema walk does not tolerate the null ActiveTool the DOM does.
    int FirstIndex = 0;
    int LastIndex  = 0;
    ResolvePaintFamilyRange(State.FamilyIndex, FirstIndex, LastIndex);
    SelectPaintInstrument(State, FirstIndex < LastIndex ? FirstIndex : -1);

    // Open on the grid, with the carousel and its pop replayed from the start.
    State.Focus.OpenCluster       = FamilyIndex;
    State.Carousel.ShowingOptions = false;
    State.Carousel.Travel         = 0.0f;
    State.Carousel.OpenAge        = 0.0f;
    State.CardOpen                = true;
}


void ClosePaintToolPanel(PaintToolPanelState& State)
{
    // 📝 The console travels its carousel back to the action slide as it fades. Requested through the carousel rather than snapped so the
    //    slide-back plays, which is what the prototype's dropping both classes at once does.
    State.CardOpen                = false;
    State.Carousel.ShowingOptions = false;
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
        State.Focus.OpenAction = -1;
        // Force the console's parameter block to reseed the next time an action opens.
        State.Parameters.Cluster = -1;
        State.Parameters.Action  = -1;
        return;
    }

    const PaintInstrumentDescriptor& Instrument = Instruments[InstrumentIndex];

    State.InstrumentIndex = InstrumentIndex;
    State.Schema          = ResolvePaintSchema(Instrument);
    State.ValueCount      = SeedPaintValues(Instrument, State.Schema, State.Values, PaintSchemaValueLimit);

    // 📝 SwatchIndex = 0 in OpenTool. Reset rather than carried because the swatch list is per FAMILY and ragged (eraser has one ink, brush
    //    has five), so an index held across a swap can name a colour the new family does not offer.
    State.Preview.SwatchIndex = 0;

    // Point the console's open action at this instrument's console position, and clear the parameter block's identity so it reseeds.
    int ClusterIndex = 0;
    int ActionIndex  = -1;
    ResolvePaintConsolePosition(InstrumentIndex, ClusterIndex, ActionIndex);
    State.Focus.OpenCluster = ClusterIndex;
    State.Focus.OpenAction  = ActionIndex;
    State.Parameters.Cluster = -1;
    State.Parameters.Action  = -1;
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

void ConstructPaintToolPanel(PaintToolPanelState& State, const ThemeConfiguration& Theme, const PaintCardPalette& Palette,
                             const PaintCardMetrics& Metrics, SvgIconRegistry* Registry, PaintIconStore* StripStore,
                             ImVec2 CardCentre)
{
    const float DeltaSeconds = ImGui::GetIO().DeltaTime;

    const MetricsSpecification ConsoleMetrics = ResolvePaintConsoleMetrics(Theme, Metrics);

    // Advance the toast (paint's own) and the console carousel. Both step even on a frame the card is closed, so a lingering toast expires
    // and the slide-back finishes.
    AdvancePaintOptions(State.Toast, nullptr, 0, DeltaSeconds);
    State.Carousel.OpenAge += DeltaSeconds;
    AdvanceConsoleCarousel(State.Carousel, DeltaSeconds, ConsoleMetrics);

    const ImVec2 TopLeft(CardCentre.x - ConsoleMetrics.CardWidth * 0.5f, CardCentre.y - ConsoleMetrics.CardHeight * 0.5f);

    // 🔴 STEP 1 — resolve the visible list from LAST frame's values, before the console draws. Everything below reads this; the fold at the
    //    bottom is what makes the next frame's list differ.
    PaintVisibleControl Visible[PaintVisibleControlLimit] = {};
    int VisibleGroupCount = 0;
    const int VisibleCount = ResolveVisibleNow(State, Visible, PaintVisibleControlLimit, VisibleGroupCount);

    if (!State.CardOpen)
    {
        // Card closed: the toast may still be fading over an empty field, so it is drawn even here.
        ConstructPaintToast(Palette, Metrics, State.Toast, CardCentre.x, TopLeft.y + Metrics.CardHeight);
        return;
    }

    const PaintStrokeParameters Stroke = ResolvePaintStrokeParameters(State.Schema, State.Values, State.ValueCount);

    // 🔴 STEP 2 — refresh the bridge context and build the options arena from THIS frame's visible list, then compose the descriptor and
    //    stamp it with paint's geometry, footer and painters' shared state. The context must outlive the console call — the descriptor's
    //    open action points into Context.Rows.
    State.Bridge.CardPalette       = &Palette;
    State.Bridge.CardMetrics       = &Metrics;
    State.Bridge.Preview           = &State.Preview;
    State.Bridge.Stroke            = Stroke;
    State.Bridge.InstrumentIndex   = State.InstrumentIndex;
    State.Bridge.VisibleGroupCount = VisibleGroupCount;
    State.Bridge.ParameterCount    = VisibleCount;
    State.Bridge.Registry          = Registry;
    State.Bridge.StripStore        = StripStore;
    State.Bridge.ArtMode           = State.ArtMode;
    State.Bridge.ClickedSwatch     = -1;

    BindPaintConsoleContext(State.Bridge, Visible, VisibleCount, State.Values, State.ValueCount);

    WorkspaceContextConsoleDescriptor Descriptor = ComposePaintConsoleDescriptor(State.Bridge);
    Descriptor.Metrics = &ConsoleMetrics;

    // The footer's live tally: "<n> live parameters · <n> groups", both figures moving as the reader edits, digits in the accent ink.
    char FooterNote[96] = {};
    std::snprintf(FooterNote, sizeof(FooterNote), "%d live parameters \xc2\xb7 %d groups", VisibleCount, VisibleGroupCount);
    Descriptor.Footer.CommitCaption = "Select";
    Descriptor.Footer.RevertCaption = "Reset";
    Descriptor.Footer.NoteText      = FooterNote;
    Descriptor.Footer.AccentFigures = true;

    const ConsoleResult Result = ConstructWorkspaceContextConsole(Registry, Descriptor, TopLeft, State.Focus,
                                                                 State.Carousel, State.Parameters, Theme);

    // 🔴 STEP 3 — fold the console's reports back. The parameter block first (its edits feed the schema-changing acts below), then the swatch
    //    the column painter latched, then the footer buttons, and finally the grid selection LAST because it discards the value set the fold
    //    wrote into. A reset or an apply-and-close likewise spends the values, so both come before the re-select.
    (void)FoldParameterBlock(State, Visible, VisibleCount);

    if (State.Bridge.ClickedSwatch >= 0) { State.Preview.SwatchIndex = State.Bridge.ClickedSwatch; }

    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* Instruments = ResolvePaintInstruments(InstrumentCount);
    const PaintInstrumentDescriptor* Active =
        (State.InstrumentIndex >= 0 && State.InstrumentIndex < InstrumentCount) ? &Instruments[State.InstrumentIndex] : nullptr;

    // The footer's LEFT button — Reset. Re-seed from the schema, NOT a re-resolve: SeedParams(ActiveTool) rebuilds the values and leaves the
    //    rows alone. Clearing the block identity makes the console reseed its readings from the freshly-seeded values next frame.
    if (Result.RevertRequested && Active != nullptr)
    {
        State.ValueCount         = SeedPaintValues(*Active, State.Schema, State.Values, PaintSchemaValueLimit);
        State.Parameters.Cluster = -1;
        State.Parameters.Action  = -1;

        char Message[128] = {};
        std::snprintf(Message, sizeof(Message), "Reset %s", Active->Label);
        FlashPaintToast(State.Toast, Message);
    }

    // The footer's RIGHT button — Select. The console reports this as CommitRequested (and, coincidentally, the open action in
    //    ActivatedCluster/Action). Flash the active instrument's headline, then close.
    if (Result.CommitRequested && Active != nullptr)
    {
        float SizeReading = 0.0f;
        const bool HasSize = ReadKeyedReading(State.Schema, State.Values, State.ValueCount, "size", SizeReading);

        char Message[160] = {};
        if (HasSize) { std::snprintf(Message, sizeof(Message), "%s active \xc2\xb7 %g px", Active->Label, SizeReading); }
        else         { std::snprintf(Message, sizeof(Message), "%s active \xc2\xb7 \xe2\x80\x94 px", Active->Label); }

        FlashPaintToast(State.Toast, Message);
        ClosePaintToolPanel(State);
    }

    if (Result.DismissRequested)
    {
        State.CardOpen = false;
    }

    // 🔴 A GRID tile click does NOT come back as ActivatedCluster — the console only moves Focus.OpenAction and slides to options. So the
    //    selection is detected by the focus now naming a different instrument than the one whose schema is loaded; re-selecting re-resolves
    //    the schema and re-seeds its values. Done LAST because it discards the values every step above may have written. Guarded on the open
    //    action being valid so an open-on-empty focus does not re-select instrument -1.
    if (State.Focus.OpenAction >= 0)
    {
        const int FocusInstrument = ResolvePaintInstrumentFor(State.Focus.OpenCluster, State.Focus.OpenAction);
        if (FocusInstrument >= 0 && FocusInstrument != State.InstrumentIndex)
        {
            SelectPaintInstrument(State, FocusInstrument);
        }
    }

    // 🔴 The toast is drawn AFTER the console returns, over the whole card with the foreground list — `position:fixed; z-index:120` sits over
    //    the card, not inside a pane, and the console never clips the foreground list, so paint's own toast survives intact.
    ConstructPaintToast(Palette, Metrics, State.Toast, CardCentre.x, TopLeft.y + Metrics.CardHeight);
}

} // namespace Frontier
