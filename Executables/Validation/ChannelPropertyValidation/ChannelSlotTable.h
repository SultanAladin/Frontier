/*==============================================================================================================================================
                                                              CHANNELSLOTTABLE.H
==============================================================================================================================================*/
// 🧩 The fourteen PBR channel slots the paint surface authors, and the caller-owned authoring store behind them. Ported 1:1 from
//    Documentation/Prototypes/ChannelPropertyPanel.html (itself the channel slide of PaintingSurface/Interface/LayerInspector.js). Each slot
//    names its editor, its numeric span and the atlas component the deposit lands in; the store holds one authoring record per channel.
//    🔴 SourceMode::Texture means PAINTED BY HAND, not loaded from disk (LayerKinds.js:68 — the atlas IS the storage). An imported map is an
//       optional BASE beneath the strokes: erasing the strokes and deleting the import are independent, and neither leaves the mode.

#pragma once
#ifndef FRONTIER_CHANNELPROPERTYVALIDATION_CHANNELSLOTTABLE_H
#define FRONTIER_CHANNELPROPERTYVALIDATION_CHANNELSLOTTABLE_H

#include "imgui.h"

namespace ChannelPropertyValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int ChannelSlotCount  = 14;   // [idx] - Authored channels
constexpr int ChannelGroupCount = 4;    // [idx] - Sections the add-menu is divided into
constexpr int AtlasTotal        = 5;    // [idx] - RGBA8 atlases the fourteen channels occupy
constexpr int GeneratorCount    = 10;   // [idx] - Entries in the generator catalogue
constexpr int GeneratorGroups   = 3;    // [idx] - Mask / Wear / Procedural
constexpr int ParameterCeiling  = 3;    // [idx] - Widest generator parameter count


//------------------------------------------------------------------------------------------------------------------------
//                                                            TYPES
//------------------------------------------------------------------------------------------------------------------------

// The per-channel authoring editor. Mirrors CHANNEL_SLOTS[].Edit in the prototype.
enum class ChannelEditor
{
    Colour,     // RGB swatch
    Scalar,     // bounded slider + editable pill
    Derived     // no storage, no control — computed from another channel
};

// The per-channel source. Mirrors CHANNEL_MODES in LayerKinds.js.
enum class SourceMode
{
    Value = 0,      // flat authored constant
    Texture,        // painted by hand into the atlas
    Generator       // evaluated procedurally
};

// Which section of the add-menu a channel is listed under.
enum class ChannelGroup
{
    Surface = 0,
    Radiance,
    Reflectance,
    Scattering
};


//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// One immutable channel definition — the schema row, never edited at runtime.
struct ChannelSlot
{
    const char*   Label;        // [-]   - Display name ("Base Colour")
    const char*   Placement;    // [-]   - Atlas + component the deposit lands in
    ChannelGroup  Group;        // [-]   - Add-menu section
    ChannelEditor Editor;       // [-]   - Which control the body draws
    float         Minimum;      // [-]   - Scalar span floor (unused for Colour/Derived)
    float         Maximum;      // [-]   - Scalar span ceiling
    const char*   Format;       // [-]   - printf form for the value pill
    const char*   Unit;         // [-]   - Side-segment text
    ImU32         Hue;          // [-]   - Chip swatch tone
    bool          Removable;    // [-]   - false pins the channel on (Base Colour)
};

// One generator parameter definition.
struct GeneratorParameter
{
    const char* Label;          // [-]   - Row label
    float       Minimum;        // [-]   - Span floor
    float       Maximum;        // [-]   - Span ceiling
    float       Default;        // [-]   - Seed on assignment
};

// One generator catalogue entry.
struct GeneratorEntry
{
    const char*        Label;                          // [-]   - Display name
    const char*        Note;                           // [-]   - One-line mechanism summary
    const char*        Section;                        // [-]   - Catalogue group heading, null to continue the previous
    int                ParameterCount;                 // [idx] - Live entries in Parameters
    GeneratorParameter Parameters[ParameterCeiling];   // [-]   - Parameter definitions
};

// The mutable authoring record for one channel — everything the panel edits.
struct ChannelAuthoring
{
    bool       Enabled;                            // [-]   - Listed as a chip + drawn as a card
    bool       Expanded;                           // [-]   - Card fold, caller-owned so it survives cycles
    SourceMode Source;                             // [-]   - Value / Texture / Generator
    float      Amount;                             // [-]   - Scalar editor's authored constant
    float      Tint[3];                            // [0-1] - Colour editor's authored RGB
    int        StrokeCount;                        // [idx] - Painted strokes in the atlas, 0 = unallocated
    bool       ImportedBase;                       // [-]   - An imported map sits beneath the strokes
    char       ImportName[64];                     // [-]   - Imported file name
    int        ImportWidth;                        // [px]  - Imported extent
    int        ImportHeight;                       // [px]  - Imported extent
    const char* ImportFormat;                      // [-]   - Imported encoding
    int        GeneratorIndex;                     // [idx] - Assigned catalogue entry, -1 = none
    float      GeneratorParameters[ParameterCeiling]; // [-] - Live parameter values
};

// The whole panel's caller-owned state.
struct ChannelPropertyState
{
    char             LayerName[64];                   // [-]   - Focused layer's name
    const char*      Classification;                  // [-]   - "Material" / "Generator" / ...
    const char*      Blend;                           // [-]   - Blend name shown in the sub-line
    ChannelAuthoring Channels[ChannelSlotCount];      // [-]   - One record per slot
    bool             ChipsExpanded;                   // [-]   - The chips card's fold
    bool             AddMenuOpen;                     // [-]   - The "+" popup
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The immutable fourteen-row schema. Index with a channel ordinal (0 .. ChannelSlotCount-1).
const ChannelSlot* ResolveChannelSlotTable();

// 📝 The immutable generator catalogue. Index with a generator ordinal (0 .. GeneratorCount-1).
const GeneratorEntry* ResolveGeneratorCatalogue();

// 📝 Display name of one add-menu section.
const char* DescribeChannelGroup(ChannelGroup Group);

// 📝 Display name of one source mode.
const char* DescribeSourceMode(SourceMode Source);

// 📝 Seed the store to the prototype's opening pose (nine channels live, Base Colour painted + imported,
//    Roughness and Ambient Occlusion driven by generators).
void InitializeChannelPropertySample(ChannelPropertyState& State);

// 📝 Reset one channel's generator parameters to the catalogue defaults. No-op when no generator is assigned.
void ResetGeneratorParameters(ChannelAuthoring& Authoring);

// 📝 Assign a generator to a channel and seed its parameters from the catalogue.
void AssignGenerator(ChannelAuthoring& Authoring, int GeneratorIndex);

// 📝 Count the enabled channels.
int CountEnabledChannels(const ChannelPropertyState& State);

}   // namespace ChannelPropertyValidation

#endif
