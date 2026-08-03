/*==============================================================================================================================================
                                                       PAINTCONSOLEBRIDGE.CPP
==============================================================================================================================================*/
// 🧩 Builds the console's cluster/action tables from the paint catalogue (one cluster per family, one action per instrument), rebuilds the options
//    arena each frame from the live schema, and supplies the two painters that keep paint's own surfaces intact inside the console's frame.
//
//    🔴 No resolver is bound. Paint has no availability question — see the header. The console's null-resolver rule makes every action Live, which is
//       the truth for this catalogue rather than a stand-in for one.

#include "PaintConsoleBridge.h"

#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ClusterDescriptor.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ActionDescriptor.h"
#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include "PaintCardShell.h"
#include "PaintCatalogue.h"
#include "PaintIconPack.h"
#include "PaintIconStore.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    ActionDescriptor  ActionTable[PaintInstrumentCount];
    ClusterDescriptor ClusterTable[PaintFamilyCount];
    bool              TablesBuilt = false;

    // 📝 The token→instrument map. GateToken is the DENSE action index, which for paint IS the catalogue instrument index — the catalogue is already
    //    grouped by family in rail order, so a family's actions are a contiguous slice and the two numbering schemes coincide. Recorded explicitly
    //    anyway rather than relied upon: the painters key off it, and a future catalogue reorder would otherwise mis-address every well silently.
    int InstrumentForAction[PaintInstrumentCount];

    // Where each family's actions begin, so a (cluster, action) pair reads back to a catalogue index without re-walking the ranges.
    int FamilyFirstAction[PaintFamilyCount];

    void BuildTablesOnce()
    {
        if (TablesBuilt) { return; }

        int FamilyCount = 0;
        const PaintFamilyDescriptor* const Families = ResolvePaintFamilies(FamilyCount);

        int InstrumentCount = 0;
        const PaintInstrumentDescriptor* const Instruments = ResolvePaintInstruments(InstrumentCount);

        int Emitted = 0;

        for (int FamilyIndex = 0; FamilyIndex < FamilyCount && FamilyIndex < PaintFamilyCount; ++FamilyIndex)
        {
            int FirstIndex = 0;
            int LastIndex  = 0;
            ResolvePaintFamilyRange(FamilyIndex, FirstIndex, LastIndex);

            FamilyFirstAction[FamilyIndex] = Emitted;
            const int ClusterFirst = Emitted;

            for (int Index = FirstIndex; Index < LastIndex && Emitted < PaintInstrumentCount; ++Index)
            {
                const PaintInstrumentDescriptor& Instrument = Instruments[Index];

                ActionDescriptor& Action = ActionTable[Emitted];
                Action.Label     = Instrument.Label;
                Action.Keystroke = nullptr;                 // the paint prototype gives instruments no accelerators
                // 📝 The glyph name is left null on purpose: every tile's art comes from the tile painter below, which draws the instrument's own
                //    SVG nib crop. The console's placeholder glyph is the fallback for a tile the painter declines, and it declines none of these.
                Action.GlyphName      = nullptr;
                Action.GateToken      = static_cast<unsigned int>(Index);
                // The options arena is per FRAME and per open action, so the authored table is null here and the descriptor points at the context's
                // arena instead — see ComposePaintConsoleDescriptor.
                Action.Parameters     = nullptr;
                Action.ParameterCount = 0;

                InstrumentForAction[Emitted] = Index;
                ++Emitted;
            }

            ClusterDescriptor& Cluster = ClusterTable[FamilyIndex];
            Cluster.Caption        = Families[FamilyIndex].Caption;
            Cluster.Keystroke      = nullptr;
            Cluster.GlyphName      = nullptr;
            Cluster.Actions        = (Emitted > ClusterFirst) ? &ActionTable[ClusterFirst] : nullptr;
            Cluster.ActionCount    = Emitted - ClusterFirst;
            Cluster.SeparatorAbove = false;
        }

        TablesBuilt = true;
    }

    //------------------------------------------------- THE COLUMN PAINTER -------------------------------------------------

    // 📝 Slide 2's left column, drawn by paint itself. This is a thin mapping, deliberately: the console hands over a rectangle, and the ALREADY
    //    PORTED ConstructPaintPreviewColumn does every bit of the actual painting — the 220-stamp ribbon, the dab, the swatch chips, the standing
    //    instrument, the spec rows. Nothing about the preview was reimplemented to fit the console.
    void PaintPreviewSurface(const SurfaceRegion& Region, const PaletteSpecification& Palette, void* ContextPointer)
    {
        (void)Palette;   // the preview draws in paint's OWN palette; the shared tokens have no paper or well-ring colour

        PaintConsoleContext& Context = *static_cast<PaintConsoleContext*>(ContextPointer);
        if (Context.CardPalette == nullptr || Context.CardMetrics == nullptr || Context.Preview == nullptr) { return; }
        if (Context.InstrumentIndex < 0) { return; }

        PaintPaneRegion Pane = {};
        Pane.BodyMinimum = Region.Minimum;
        Pane.BodyMaximum = Region.Maximum;
        Pane.IsVisible   = true;   // the console only calls a painter for a column it is actually showing

        const int Clicked = ConstructPaintPreviewColumn(*Context.CardPalette, *Context.CardMetrics, Pane,
                                                        *Context.Preview, Context.Stroke,
                                                        Context.Registry, Context.StripStore,
                                                        Context.InstrumentIndex,
                                                        Context.VisibleGroupCount, Context.ParameterCount);

        // Latched rather than applied: the panel drains this after the console returns, so a swatch change never lands mid-draw.
        if (Clicked >= 0) { Context.ClickedSwatch = Clicked; }
    }

    //-------------------------------------------------- THE TILE PAINTER --------------------------------------------------

    // 📝 One tile's artwork: the instrument's round well. The console has already laid out the cell and drawn its ground, so this fills only the art
    //    square — which is what keeps a painted tile reading as a tile (same hit target, same caption, same hover).
    //    🔴 The well is drawn here rather than by calling into PaintInstrumentGrid: that function owns a whole GRID (its own cells, scroll, fade and
    //       hit-testing), and the console owns those now. What is preserved is the WELL itself — the ring, the circular crop, and the two art modes —
    //       which is the part that carries the instrument's identity.
    bool PaintInstrumentWell(const ActionDescriptor& Action, const SurfaceRegion& Region, bool Gated, void* ContextPointer)
    {
        (void)Gated;   // paint has no gate; every instrument draws live

        PaintConsoleContext& Context = *static_cast<PaintConsoleContext*>(ContextPointer);
        if (Context.CardPalette == nullptr) { return false; }

        const int Index = static_cast<int>(Action.GateToken);
        if (Index < 0 || Index >= PaintInstrumentCount) { return false; }

        ImDrawList* const DrawList = ImGui::GetWindowDrawList();

        const float  Radius = (Region.Maximum.x - Region.Minimum.x) * 0.5f;
        const ImVec2 Centre((Region.Minimum.x + Region.Maximum.x) * 0.5f,
                            (Region.Minimum.y + Region.Maximum.y) * 0.5f);

        DrawList->AddCircleFilled(Centre, Radius, Context.CardPalette->PaneFill, 32);

        // The full strip is landscape, so its height follows the texture's real aspect; the nib crop is square and fills the well exactly.
        ImTextureID ArtTexture = 0;
        float       ArtWidth   = Radius * 2.0f;
        float       ArtHeight  = Radius * 2.0f;

        if (Context.ArtMode == PaintWellArtMode::FullStrip && Context.StripStore != nullptr)
        {
            const PaintStripTexture* const Strip = ResolvePaintStrip(*Context.StripStore, Index);
            if (Strip != nullptr && Strip->StripTextureId != 0 && Strip->PixelHeight > 0u)
            {
                const float Aspect = static_cast<float>(Strip->PixelWidth) / static_cast<float>(Strip->PixelHeight);
                ArtTexture = Strip->StripTextureId;
                ArtWidth   = Radius * 2.0f;
                ArtHeight  = (Aspect > 0.0f) ? (Radius * 2.0f / Aspect) : (Radius * 2.0f);
            }
        }

        if (ArtTexture == 0 && Context.Registry != nullptr)
        {
            ArtTexture = ResolveIconTexture(*Context.Registry, ResolvePaintNibKey(Index));
        }

        if (ArtTexture == 0)
        {
            // Nothing uploaded yet. The ring still draws, so the tile reads as an instrument awaiting art rather than as a hole.
            DrawList->AddCircle(Centre, Radius, Context.CardPalette->WellRing, 32, 1.0f);
            return true;
        }

        const ImVec2 ArtMinimum(Centre.x - ArtWidth * 0.5f, Centre.y - ArtHeight * 0.5f);
        const ImVec2 ArtMaximum(Centre.x + ArtWidth * 0.5f, Centre.y + ArtHeight * 0.5f);

        // 🔴 Two crops, for the reason the grid states: a square nib fills the well, so rounding it at the full radius IS the circle. A landscape
        //    strip does NOT — rounding it would clip a pill, so the band is clipped to the well's chord at that height instead.
        const bool ArtFillsWell = (ArtWidth >= Radius * 2.0f - 0.5f) && (ArtHeight >= Radius * 2.0f - 0.5f);
        if (ArtFillsWell)
        {
            DrawList->AddImageRounded(ArtTexture, ArtMinimum, ArtMaximum, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                      IM_COL32_WHITE, Radius);
        }
        else
        {
            const float BandHalfHeight = ArtHeight * 0.5f;
            const float ChordHalfWidth = (BandHalfHeight < Radius)
                                       ? std::sqrt((Radius * Radius) - (BandHalfHeight * BandHalfHeight))
                                       : 0.0f;
            DrawList->PushClipRect(ImVec2(Centre.x - ChordHalfWidth, Centre.y - Radius),
                                   ImVec2(Centre.x + ChordHalfWidth, Centre.y + Radius), true);
            DrawList->AddImage(ArtTexture, ArtMinimum, ArtMaximum);
            DrawList->PopClipRect();
        }

        DrawList->AddCircle(Centre, Radius, Context.CardPalette->WellRing, 32, 1.0f);
        return true;
    }

    //------------------------------------------------- THE OPTIONS ARENA -------------------------------------------------

    // 📝 One paint control row, converted into the console's parameter vocabulary. Converted field by field rather than cast: the two structs are
    //    only incidentally similar (paint has no swatch/axis/snap kinds and the console has no PaintRowCondition), so a cast would turn a future
    //    divergence into silent field shear.
    //    🔴 The row's CURRENT value is written into InitialReading/InitialOptionLabel/InitialActivation, not its authored default. The console's
    //       ParameterBlock seeds itself from these, and paint owns the live values — handing over the authored default would make every reveal reset
    //       the reader's edits to factory settings.
    ParameterDescriptor ConvertControl(const PaintControlDescriptor& Source, const PaintControlValue& Value)
    {
        ParameterDescriptor Row = {};

        switch (Source.Category)
        {
            case PaintControlCategory::Slider:    Row.Category = ParameterCategory::Slider;    break;
            case PaintControlCategory::Segmented: Row.Category = ParameterCategory::Segmented; break;
            case PaintControlCategory::Select:    Row.Category = ParameterCategory::Dropdown;  break;
            case PaintControlCategory::Switch:
            default:                              Row.Category = ParameterCategory::Switch;    break;
        }

        Row.Key       = Source.Key;
        Row.GlyphName = Source.GlyphName;
        Row.Label     = Source.Label;

        Row.MinimumBoundary = Source.MinimumBoundary;
        Row.MaximumBoundary = Source.MaximumBoundary;
        Row.InitialReading  = Value.Reading;
        Row.Unit            = Source.Unit;

        // A segmented strip's options are inline in the console's descriptor but borrowed in paint's, so they are copied across up to the shared
        // ceiling; a dropdown's stay borrowed in both.
        if (Source.Category == PaintControlCategory::Segmented && Source.OptionLabels != nullptr)
        {
            const int Bound = (Source.OptionCount < ParameterSegmentedOptionLimit)
                            ? Source.OptionCount : ParameterSegmentedOptionLimit;
            for (int Option = 0; Option < Bound; ++Option)
            {
                Row.SegmentedLabels[Option] = Source.OptionLabels[Option];
            }
        }
        else if (Source.Category == PaintControlCategory::Select)
        {
            Row.DropdownLabels = Source.OptionLabels;
            Row.DropdownCount  = Source.OptionCount;
        }

        // Carried by INDEX here rather than by label: paint has already resolved the authored label to an index, and re-resolving by label would
        // reintroduce the two-nib-set ordering hazard that PaintCardSpecification warns about.
        Row.InitialOptionLabel = nullptr;
        Row.InitialOption      = Value.ChosenOption;
        Row.InitialActivation  = Value.Activated;

        return Row;
    }
}   // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BindPaintConsoleContext(PaintConsoleContext&       Context,
                             const PaintVisibleControl* Visible,
                             int                        VisibleCount,
                             const PaintControlValue*   Values,
                             int                        ValueCount)
{
    Context.RowCount = 0;
    if (Visible == nullptr || Values == nullptr) { return; }

    const int Bound = (VisibleCount < ConsoleParameterBlockLimit) ? VisibleCount : ConsoleParameterBlockLimit;
    for (int Index = 0; Index < Bound; ++Index)
    {
        const PaintVisibleControl& Row = Visible[Index];
        if (Row.Control == nullptr) { continue; }
        if (Row.ValueIndex < 0 || Row.ValueIndex >= ValueCount) { continue; }

        Context.Rows[Context.RowCount] = ConvertControl(*Row.Control, Values[Row.ValueIndex]);
        ++Context.RowCount;
    }
}


WorkspaceContextConsoleDescriptor ComposePaintConsoleDescriptor(PaintConsoleContext& Context)
{
    BuildTablesOnce();

    // 🔴 The open instrument's actions point at THIS frame's arena. Written into the shared table rather than a copy because the console reads the
    //    descriptor's own action for the open row — and cleared off every other action so a stale arena can never be read through a closed one.
    for (int Index = 0; Index < PaintInstrumentCount; ++Index)
    {
        const bool IsOpen = (InstrumentForAction[Index] == Context.InstrumentIndex);
        ActionTable[Index].Parameters     = IsOpen ? Context.Rows : nullptr;
        ActionTable[Index].ParameterCount = IsOpen ? Context.RowCount : 0;
    }

    WorkspaceContextConsoleDescriptor Descriptor = {};
    Descriptor.Clusters     = ClusterTable;
    Descriptor.ClusterCount = PaintFamilyCount;
    Descriptor.Gate         = VerdictBinding{};      // no gate — see the header
    Descriptor.Probe        = nullptr;               // the left column is painted, not measured
    Descriptor.Metrics      = nullptr;               // filled by the panel, which owns paint's geometry
    Descriptor.Surfaces     = SurfaceBinding{ &PaintPreviewSurface, &PaintInstrumentWell, &Context };
    return Descriptor;
}


int ResolvePaintInstrumentFor(int ClusterIndex, int ActionIndex)
{
    BuildTablesOnce();

    if (ClusterIndex < 0 || ClusterIndex >= PaintFamilyCount) { return -1; }
    if (ActionIndex  < 0 || ActionIndex >= ClusterTable[ClusterIndex].ActionCount) { return -1; }

    const int Dense = FamilyFirstAction[ClusterIndex] + ActionIndex;
    if (Dense < 0 || Dense >= PaintInstrumentCount) { return -1; }
    return InstrumentForAction[Dense];
}


void ResolvePaintConsolePosition(int InstrumentIndex, int& ClusterIndex, int& ActionIndex)
{
    BuildTablesOnce();

    ClusterIndex = 0;
    ActionIndex  = -1;
    if (InstrumentIndex < 0) { return; }

    for (int Family = 0; Family < PaintFamilyCount; ++Family)
    {
        const int First = FamilyFirstAction[Family];
        const int Last  = First + ClusterTable[Family].ActionCount;
        for (int Dense = First; Dense < Last; ++Dense)
        {
            if (InstrumentForAction[Dense] == InstrumentIndex)
            {
                ClusterIndex = Family;
                ActionIndex  = Dense - First;
                return;
            }
        }
    }
}

}   // namespace Frontier
