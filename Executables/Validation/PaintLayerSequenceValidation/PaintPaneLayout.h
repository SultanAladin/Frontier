/*==============================================================================================================================================
                                                             PAINTPANELAYOUT.H
==============================================================================================================================================*/
// 🧩 The composite surfaces the four panes are assembled from: the 46 px pane head, the 26 px foot, the Layer ⇄ Mask tab strip, the generic fold
//    card, the 82 px field row, the metadata row, chips, slots, the compact log — and the one deferred menu overlay every dropdown routes through.
//
// 🔴 A menu is drawn LAST but probed FIRST. ImGui gives hover to the first item that claims a pixel, so an overlay submitted after the content
//    beneath it would never receive the click. Every menu is therefore assembled from an anchor cached on the cycle its face was pressed, probed
//    before any pane draws, and painted after every pane has drawn. That is also why the overlay carries its own clip rect: in the source a menu
//    is clipped by the scrolling pane it lives in, not by the card.
//
// 📝 Vertical flow, not a widget tree. Each surface draws at the pen, reports what it consumed, and steps the pen — which is what lets a fold
//    animate against a remembered height without any of the surfaces below it knowing.

#pragma once
#ifndef FRONTIER_PAINTLAYERSEQUENCEVALIDATION_PAINTPANELAYOUT_H
#define FRONTIER_PAINTLAYERSEQUENCEVALIDATION_PAINTPANELAYOUT_H

#include "PaintLayerStore.h"
#include "PaintSurfaceChrome.h"

namespace PaintLayerSequenceValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr float CardHeadHeight  = 30.0f;   // [px] - .card-head
constexpr float FieldRowHeight  = 30.0f;   // [px] - .prow min-height
constexpr float FieldRowGap     =  5.0f;   // [px] - .prow margin-bottom
constexpr float FieldLabelWidth = 82.0f;   // [px] - .prow first column
constexpr float FieldLabelGap   = 10.0f;   // [px] - .prow column-gap
constexpr float MetaRowHeight   = 27.0f;   // [px] - .mrow
constexpr float SlotHeight      = 50.0f;   // [px] - .slot (38 thumb + 6 padding each side)
constexpr float SlotThumbEdge   = 38.0f;   // [px] - .slot-thumb
constexpr float ChipHeight      = 21.0f;   // [px] - .chip
constexpr float MetaChipHeight  = 20.0f;   // [px] - .mchip
constexpr float LogRowHeight    = 22.0f;   // [px] - .log-row
constexpr int   LayoutKeySpan   = 64;      // [-]  - Call-site key buffer


//------------------------------------------------------------------------------------------------------------------------
//                                                            FLOW
//------------------------------------------------------------------------------------------------------------------------

// 📝 A vertical pen inside a fixed-width column. Every surface below draws at `Pen` and steps it.
struct PaintFlow
{
    ImDrawList* Draw;    // [-]  - Target list
    float       Left;    // [px] - Column left edge
    float       Width;   // [px] - Column width
    float       Pen;     // [px] - Current vertical position
};

inline ImVec2 ResolvePen(const PaintFlow& Flow) { return ImVec2(Flow.Left, Flow.Pen); }


//------------------------------------------------------------------------------------------------------------------------
//                                                        PANE CHROME
//------------------------------------------------------------------------------------------------------------------------

// 📝 What a pane head carries. Unused fields simply do not draw — the source's four head shapes are this one band with different columns filled.
struct PaneHeadEntry
{
    PaintGlyph  Icon;          // [-] - Boxed 26 px icon
    bool        IconPresent;    // [-] - Draw the boxed icon column
    const char* Label;         // [-] - 12.5 px title
    const char* Detail;        // [-] - 9.5 px sub-line
    const char* Count;         // [-] - Trailing pill, null for none
    const char* Chord;         // [-] - Trailing keyboard hint, null for none
    bool        Forward;       // [-] - Trailing step chevron
    bool        Backward;      // [-] - Leading step chevron, replaces the boxed icon
};

// 📝 Draws the 46 px head band. Returns true when a stepping head was pressed.
bool RecordPaneHead(ImDrawList* Draw, ImVec2 TopLeft, float Width, const PaneHeadEntry& Entry, const char* Id);

// 📝 One 26 px foot band: a 7 px hue square, then up to four text cells with a spacer before the trailing pair.
void RecordPaneFoot(ImDrawList* Draw,
                    ImVec2      TopLeft,
                    float       Width,
                    ImU32       Hue,
                    bool        HuePresent,
                    const char* Leading,
                    const char* Middle,
                    const char* Trailing);

// 📝 The 30 px Layer ⇄ Mask strip with its sliding 2 px thumb. Returns the pressed face, or -1.
int RecordFaceTabs(ImDrawList* Draw, ImVec2 TopLeft, float Width, InspectorFace Face, float Travel, const char* Id);


//------------------------------------------------------------------------------------------------------------------------
//                                                          FOLD CARD
//------------------------------------------------------------------------------------------------------------------------

// 📝 One card in flight between its head and its seal. The body is always drawn — clipped to the animated span — so the remembered height
//    stays current even on the cycle a fold begins.
struct PaintCardExtent
{
    char   Key[LayoutKeySpan];   // [-]  - Call-site key
    ImVec2 Origin;               // [px] - Card top-left
    float  Width;                // [px] - Card width
    float  Travel;               // [0-1]- Eased fold travel
    float  VisibleSpan;          // [px] - Body height on screen this cycle
    float  BodyTop;              // [px] - Body content origin
    bool   HeadPressed;          // [-]  - The head was pressed this cycle
};

// 📝 Draws the card head and opens its clipped body. `Body` comes back as a flow to draw the body's rows into.
void OpenPaintCard(PaintFlow&       Flow,
                   const char*      Key,
                   const char*      Title,
                   ImU32            DotHue,
                   bool             DotPresent,
                   const char*      Tag,
                   bool             TagLive,
                   bool             Folded,
                   PaintCardExtent& Extent,
                   PaintFlow&       Body);

// 📝 Closes the body clip, remembers the height it consumed, draws the card's border and steps the outer pen past it.
void SealPaintCard(PaintFlow& Flow, PaintCardExtent& Extent, const PaintFlow& Body);


//------------------------------------------------------------------------------------------------------------------------
//                                                            ROWS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Where a field row's control goes. The row steps the pen itself, so the caller only draws the control.
struct FieldPlacement
{
    ImVec2 Origin;   // [px] - Control top-left, vertically centred in the row
    float  Width;    // [px] - Control width
};

FieldPlacement RecordFieldRow(PaintFlow& Flow, const char* Label, float ControlHeight);

// 📝 A full-width field row with no label column — the source's .prow.wide.
FieldPlacement RecordWideRow(PaintFlow& Flow, float ControlHeight);

// 📝 One key / value metadata row with its hairline. `Dim` greys the value, as .v.dim does.
void RecordMetaRow(PaintFlow& Flow, const char* Key, const char* Value, bool Dim, bool Hairline);

// 📝 A wrapped run of metadata chips. Returns the height consumed; each chip is "Caption" or "Caption <strong>".
struct MetaChipEntry
{
    const char* Caption;      // [-] - Leading text
    const char* Strong;       // [-] - Bright trailing text, null for none
    ImU32       Dot;          // [-] - Leading 5 px dot tone
    bool        DotPresent;   // [-] - Draw the dot
};

void RecordMetaChipRun(PaintFlow& Flow, const MetaChipEntry* Entries, int EntryCount);

// 📝 The 9 px uppercase caption that labels a slot group.
void RecordSlotCaption(PaintFlow& Flow, const char* Label);

// 📝 An italic 10 px note standing in for an empty list.
void RecordAbsentNote(PaintFlow& Flow, const char* Text);

// 📝 The centred 11 px notice a whole pane falls back to.
void RecordEmptyPane(ImDrawList* Draw, ImVec2 TopLeft, ImVec2 Size, const char* Leading, const char* Strong, const char* Trailing);


//------------------------------------------------------------------------------------------------------------------------
//                                                            SLOTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One texture / paint / generator slot. Up to two trailing icon buttons; `Pressed` reports which fired, -1 for none.
struct SlotEntry
{
    const char* Name;             // [-] - 11 px title
    const char* Detail;           // [-] - 9.5 px sub-line
    PaintGlyph  Thumb;            // [-] - Glyph drawn in the 38 px tile
    bool        ThumbGlyph;       // [-] - Draw the glyph rather than a flat tone
    ImU32       ThumbTone;        // [-] - Flat tile tone when ThumbGlyph is false
    PaintGlyph  Actions[2];       // [-] - Trailing buttons, left to right
    bool        ActionDangerous[2];   // [-] - Danger hover wash per button
    bool        ActionAvailable[2];   // [-] - Press is refused when false
    int         ActionCount;      // [-] - Live entries in Actions
};

int RecordSlot(PaintFlow& Flow, const SlotEntry& Entry, const char* Id);

// 📝 The 34 px preview strip that closes a channel card: chequered tile, mode caption, atlas placement.
void RecordPreviewStrip(PaintFlow& Flow, ImU32 Tone, bool TonePresent, const char* Caption, const char* Placement);


//------------------------------------------------------------------------------------------------------------------------
//                                                         CHIP RUNS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One channel chip: swatch, label, and a 15 px dismiss button unless the channel is pinned. Reports the chip whose dismiss fired.
struct ChannelChipOutcome
{
    int  Dismissed;    // [idx] - Channel ordinal whose cross fired, -1 for none
    bool AddPressed;   // [-]   - The trailing dashed add button fired
    ImVec2 AddOrigin;  // [px]  - Add button top-left, for the menu anchor
    float  AddEdge;    // [px]  - Add button edge
};

ChannelChipOutcome RecordChannelChipRun(PaintFlow&             Flow,
                                        const PaintLayerEntry& Layer,
                                        bool                   AddPresent,
                                        bool                   AddOpen,
                                        const char*            Id);


//------------------------------------------------------------------------------------------------------------------------
//                                                        COMPACT LOG
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-scope log inside a channel / mask card: newest first, capped at `Limit`. Returns the revision ordinal clicked, or -1.
int RecordCompactLog(PaintFlow&             Flow,
                     const PaintLayerEntry& Layer,
                     HistoryScope           Scope,
                     int                    ScopeChannel,
                     int                    Limit,
                     const char*            Id);


//------------------------------------------------------------------------------------------------------------------------
//                                                       MENU OVERLAY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which shape a menu takes. All four share the probe / paint pair below.
enum class PaintMenuCategory
{
    Options       = 0,   // Radio list — blends
    Generators    = 1,   // Grouped list with notes
    LayerKinds    = 2,   // Attached list under the Add button
    ChannelSet    = 3    // Grouped tick list
};

// 📝 One assembled row. Headings are inert; everything else carries the value it would choose.
struct PaintMenuLine
{
    ImVec2      Origin;      // [px] - Row top-left
    ImVec2      Size;        // [px] - Row extent
    const char* Label;       // [-]  - Row text
    const char* Note;        // [-]  - Trailing note, null for none
    int         Value;       // [idx]- What choosing this row yields
    ImU32       Swatch;      // [-]  - Leading swatch tone
    bool        SwatchPresent;   // [-] - Draw the swatch
    bool        Selected;    // [-]  - Radio filled / tick shown
    bool        Available;   // [-]  - A pinned row refuses the press
    bool        Heading;     // [-]  - Group caption rather than a choice
};

// 📝 One menu, assembled once per cycle from its cached anchor.
struct PaintMenuPlan
{
    bool              Live;                  // [-]  - A menu is open and its anchor is known
    char              Key[LayoutKeySpan];    // [-]  - Call-site key
    PaintMenuCategory Category;              // [-]  - Shape
    ImVec2            Origin;                // [px] - Menu top-left
    ImVec2            Size;                  // [px] - Menu extent
    ImVec2            ClipTopLeft;           // [px] - Clip the menu is confined to
    ImVec2            ClipBottomRight;       // [px] - Clip the menu is confined to
    float             Age;                   // [0-1]- Unfold keyframe progress
    PaintMenuLine     Lines[40];             // [-]  - Assembled rows
    int               LineCount;             // [-]  - Live entries in Lines
};

// 📝 Remember where a dropdown face / add button sat, and which pane clips it. Called by the pane that owns the site.
void PublishMenuAnchor(const char*       Key,
                       PaintMenuCategory Category,
                       ImVec2            FaceTopLeft,
                       ImVec2            FaceSize,
                       ImVec2            ClipTopLeft,
                       ImVec2            ClipBottomRight,
                       int               Current);

// 📝 Build the open menu's rows from its remembered anchor. Not live until the anchor has been published once.
PaintMenuPlan AssemblePaintMenu(const char* OpenKey, const PaintLayerEntry* Layer, float DeltaTime);

// 📝 Submit the menu's hit zones. 🔴 Must run before any pane draws — see the file header.
int ProbePaintMenu(const PaintMenuPlan& Plan);

// 📝 Paint the menu. 🔴 Must run after every pane has drawn.
void RecordPaintMenu(ImDrawList* Draw, const PaintMenuPlan& Plan);

// 📝 True while the cursor sits over the open menu, so the pane beneath can refuse a click that belongs to the overlay.
bool WithinPaintMenu(const PaintMenuPlan& Plan);

}   // namespace PaintLayerSequenceValidation

#endif
