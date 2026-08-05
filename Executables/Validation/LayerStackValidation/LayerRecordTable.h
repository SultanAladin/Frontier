/*==============================================================================================================================================
                                                            LAYERRECORDTABLE.H
==============================================================================================================================================*/
// 🧩 The paint-surface layer stack and the caller-owned store behind it. Ported 1:1 from
//    Documentation/Prototypes/PaintingSurface/Interface/LayerInspector.js (BuildRow + BuildExpand + RenderStackFoot) with the kinds and blend
//    names taken from Layers/LayerKinds.js and Layers/LayerStack.js. One record per layer; the rail draws them TOP-DOWN, so ordinal 0 is the
//    TOPMOST layer and composites LAST.
//    🔴 A layer's Tag hue is its OWN identity colour, not its kind tint (LayerInspector.js:787 — keyed to the kind, every material layer
//       carried the same blue and the marker distinguished nothing). The kind tint lives on the thumb glyph; the two answer different questions.

#pragma once
#ifndef FRONTIER_LAYERSTACKVALIDATION_LAYERRECORDTABLE_H
#define FRONTIER_LAYERSTACKVALIDATION_LAYERRECORDTABLE_H

#include "imgui.h"

namespace LayerStackValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int LayerCapacity   = 12;   // [idx] - Cap from LayerStack.js:65 — each layer costs up to three atlases
constexpr int LayerKindCount  = 4;    // [idx] - Paint / Fill / Material / Generator
constexpr int BlendModeCount  = 7;    // [idx] - Entries in BLEND_MODES
constexpr int ComponentCeiling = 4;   // [idx] - Mask components one layer may carry


//------------------------------------------------------------------------------------------------------------------------
//                                                            TYPES
//------------------------------------------------------------------------------------------------------------------------

// What a layer IS. Mirrors LAYER_KIND_ORDER in LayerKinds.js.
enum class LayerKind
{
    Paint = 0,      // hand-painted, accepts strokes, starts EMPTY
    Fill,           // uniform authored values, flooded
    Material,       // a PBR preset, flooded
    Generator       // driven by a procedural pass
};

// Which fill a mask starts from.
enum class MaskFill
{
    White = 0,
    Black
};


//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// One immutable layer-kind definition — the schema row, never edited at runtime.
struct LayerKindEntry
{
    const char* Label;      // [-]   - Display name ("Paint")
    const char* Summary;    // [-]   - One-line mechanism note shown in the add list
    ImU32       Tint;       // [-]   - Kind tint, drawn on the thumb glyph
    bool        Paintable;  // [-]   - Accepts brush strokes
    bool        Flooded;    // [-]   - Content is flooded across the whole atlas at creation
};

// One mask component.
struct MaskComponent
{
    char  Label[48];    // [-]   - Component name
    bool  Enabled;      // [-]   - Contributes to the resolved mask
    float Weight;       // [0-1] - Contribution strength
};

// The mutable record for one layer — everything the rail edits.
struct LayerRecord
{
    char      Name[64];                             // [-]   - User-typed, renamed in place
    LayerKind Kind;                                 // [-]   - Paint / Fill / Material / Generator
    int       BlendIndex;                           // [idx] - Entry in the blend table
    float     Opacity;                              // [0-100] - Percent, as the pill shows it
    bool      Shown;                                // [-]   - Eye toggle; false mutes the row
    bool      Expanded;                             // [-]   - Inline expand fold, caller-owned
    float     Tag[3];                               // [0-1] - The layer's OWN identity hue (rail tag)
    float     Paint[3];                             // [0-1] - Authored baseColour, only where it paints one
    bool      PaintsBaseColour;                     // [-]   - Gates the Paint row
    int       ChannelTotal;                         // [idx] - Channels the layer writes, for the meta line
    int       StrokeCount;                          // [idx] - Strokes laid into the layer

    bool      MaskEnabled;                          // [-]   - An enabled mask, else the "Add mask" affordance
    MaskFill  MaskFillMode;                         // [-]   - White / Black start
    bool      MaskInverted;                         // [-]   - Invert toggle
    float     MaskStrength;                         // [0-100] - Mask opacity, percent
    int       ComponentTotal;                       // [idx] - Live entries in Components
    MaskComponent Components[ComponentCeiling];     // [-]   - The component stack
};

// The whole rail's caller-owned state.
struct LayerStackState
{
    char        SurfaceName[64];                    // [-]   - The mesh the stack paints ("Suzanne")
    LayerRecord Layers[LayerCapacity];              // [-]   - Ordinal 0 is the TOPMOST layer
    int         LayerTotal;                         // [idx] - Live entries in Layers
    int         FocusOrdinal;                       // [idx] - Focused row, -1 for none
    char        FilterTerm[64];                     // [-]   - Rail filter text
    bool        AddListOpen;                        // [-]   - The "Add Layer" kind list
    int         RenameOrdinal;                      // [idx] - Row being renamed in place, -1 for none
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The immutable four-row kind schema. Index with a LayerKind ordinal.
const LayerKindEntry* ResolveLayerKindTable();

// 📝 The blend-mode names. Index with a blend ordinal (0 .. BlendModeCount-1).
const char* const* ResolveBlendModeTable();

// 📝 Display name of one layer kind.
const char* DescribeLayerKind(LayerKind Kind);

// 📝 Seed the store to the prototype's opening pose (six layers, a focused paint layer with a mask).
void InitializeLayerStackSample(LayerStackState& State);

// 📝 Append a layer of the given kind at the TOP of the stack and focus it. No-op at capacity.
void InsertLayerOfKind(LayerStackState& State, LayerKind Kind);

// 📝 Drop one layer and re-seat the focus. No-op on the last remaining layer.
void WithdrawLayer(LayerStackState& State, int Ordinal);

// 📝 Move one layer to a new ordinal, sliding the rest around it. Used by the row drag.
void RelocateLayer(LayerStackState& State, int FromOrdinal, int ToOrdinal);

// 📝 Count the hidden layers, for the foot tally.
int CountHiddenLayers(const LayerStackState& State);

// 📝 Whether a layer's name passes the rail filter. An empty term passes everything.
bool QueryLayerPassesFilter(const LayerRecord& Record, const char* FilterTerm);

}   // namespace LayerStackValidation

#endif
