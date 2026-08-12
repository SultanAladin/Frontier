/*==============================================================================================================================================
                                                              PAINTLAYERSTORE.H
==============================================================================================================================================*/
// 🧩 The paint surface's layer sequence and the fixed vocabulary behind it. Transcribed 1:1 from Documentation/Prototypes/TexturePaintUI.html
//    (the SCHEMA + STATE blocks): a layer owns exactly ONE material — its set of the fourteen PBR channels — and exactly ONE mask. Nothing else
//    nests, which is what lets a stack entry unfold into precisely two sub-cards and lets the FACE be a two-state value rather than a tree path.
//
// 🔴 The tables here are VOCABULARY, not authoring records: a layer only remembers which blend / generator / channel source it chose and the
//    values it authored for them. Re-choosing a generator reseeds its parameters from the catalogue defaults, exactly as the source does.
//
// 📝 Draw-free by construction — nothing in this pair touches ImGui beyond ImU32 for the transcribed hues, so the sequence can be seeded,
//    mutated and recorded without a frame in flight.

#pragma once
#ifndef FRONTIER_PAINTLAYERSEQUENCEVALIDATION_PAINTLAYERSTORE_H
#define FRONTIER_PAINTLAYERSEQUENCEVALIDATION_PAINTLAYERSTORE_H

#include "imgui.h"

namespace PaintLayerSequenceValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int ChannelCount           = 14;   // [idx] - Authored PBR channels (CH)
constexpr int ChannelGroupCount      =  4;   // [idx] - Add-menu sections (CH_GROUPS)
constexpr int BlendCount             =  7;   // [idx] - Blend names (BLEND)
constexpr int LayerCategoryCount     =  4;   // [idx] - Layer categories (KINDS)
constexpr int GeneratorCount         =  5;   // [idx] - Generator catalogue entries (GENS)
constexpr int GeneratorGroupCount    =  3;   // [idx] - Generator catalogue headings (Mask / Wear / Procedural)
constexpr int GeneratorParameterMax  =  3;   // [idx] - Widest generator's parameter count
constexpr int HistoryCategoryCount   =  7;   // [idx] - Timeline categories (HKIND)

constexpr int LayerCapacity          = 48;   // [idx] - Layers the sequence can hold
constexpr int HistoryCapacity        = 64;   // [idx] - Recorded revisions per layer
constexpr int NameCapacity           = 48;   // [-]   - Layer name buffer
constexpr int TokenCapacity          = 12;   // [-]   - Layer token buffer ("L3")
constexpr int TitleCapacity          = 72;   // [-]   - History title buffer
constexpr int DetailCapacity         = 56;   // [-]   - History detail buffer
constexpr int TextureNameCapacity    = 56;   // [-]   - Imported texture filename buffer
constexpr int FormatCapacity         = 16;   // [-]   - Imported texture encoding buffer
constexpr int MenuKeyCapacity        = 40;   // [-]   - Call-site key of the one open menu
constexpr int FilterCapacity         = 48;   // [-]   - Stack filter term buffer
constexpr int SurfaceCapacity        = 32;   // [-]   - Paint surface name buffer


//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which control a channel's card body draws. `Derived` has no storage and no authored value at all.
enum class ChannelEditor
{
    Colour  = 0,
    Scalar  = 1,
    Derived = 2
};


// 📝 Where a channel's contribution comes from. Mirrors the prototype's Modes[key] strings.
// 🔴 `Texture` means PAINTED BY HAND into the atlas, not loaded from disk — an imported map is an optional BASE beneath the strokes.
enum class ChannelSource
{
    Value     = 0,
    Texture   = 1,
    Generator = 2
};


// 📝 The add-menu section a channel is listed under.
enum class ChannelSection
{
    Surface     = 0,
    Radiance    = 1,
    Reflectance = 2,
    Scattering  = 3
};


// 📝 The tone the mask starts from before invert and strength fold in.
enum class MaskBase
{
    White = 0,
    Black = 1
};


// 📝 Where the mask's coverage comes from.
enum class MaskSource
{
    Solid     = 0,
    Generator = 1
};


// 📝 The face the whole card is showing. ONE value — every tab, spine and swipe track reads it.
enum class InspectorFace
{
    Layer = 0,
    Mask  = 1
};


// 📝 Timeline categories. The hue doubles as the timeline node colour and the compact-log dot.
enum class HistoryCategory
{
    Stroke    = 0,
    Channel   = 1,
    Mask      = 2,
    Generator = 3,
    Texture   = 4,
    Edit      = 5,
    Create    = 6
};


// 📝 What a recorded revision is scoped to. A channel scope additionally carries its channel ordinal.
enum class HistoryScope
{
    Layer       = 0,
    Mask        = 1,
    ChannelSlot = 2
};


// 📝 Which revisions the timeline lists.
enum class HistoryFilter
{
    All      = 0,
    Stroke   = 1,
    Channel  = 2,
    Mask     = 3
};


//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One immutable channel definition — the schema row, never edited at runtime.
struct ChannelDefinition
{
    const char*    Key;         // [-] - Schema key, also the scope name in the history log
    const char*    Label;       // [-] - Display name
    const char*    Placement;   // [-] - Atlas + component the deposit lands in
    ChannelSection Section;     // [-] - Add-menu section
    ChannelEditor  Editor;      // [-] - Control the card body draws
    float          Minimum;     // [-] - Scalar span floor
    float          Maximum;     // [-] - Scalar span ceiling
    const char*    Unit;        // [-] - Pill side-segment text, null for a plain 0-1 percentage
    ImU32          Hue;         // [-] - Chip swatch + card dot tone
    bool           Removable;   // [-] - false pins the channel on (Base Colour)
};


// 📝 One tweakable input of a generator.
struct GeneratorParameterDefinition
{
    const char* Label;      // [-] - Row caption
    float       Default;    // [-] - Value a freshly-chosen generator seeds this parameter to
};


// 📝 One entry of the generator catalogue.
struct GeneratorDefinition
{
    const char*                  Label;                                    // [-] - Menu + slot caption
    const char*                  Note;                                     // [-] - One-line mechanism summary
    const char*                  Section;                                  // [-] - Catalogue heading
    GeneratorParameterDefinition Parameters[GeneratorParameterMax];        // [-] - Parameter rows
    int                          ParameterCount;                           // [-] - Live entries in Parameters
};


// 📝 One layer category — the "Add layer" list and the stack thumbnail's corner dot.
struct LayerCategoryDefinition
{
    const char* Label;      // [-] - Display name
    const char* Summary;    // [-] - Add-menu trailing note
    ImU32       Tint;       // [-] - Bubble / spine / corner-dot tone
};


// 📝 One timeline category — label pill text and node hue.
struct HistoryCategoryDefinition
{
    const char* Label;      // [-] - Uppercase pill text
    ImU32       Hue;        // [-] - Node + dot tone
};


// 📝 The per-channel authoring record. Everything a layer edits about one channel.
struct ChannelAuthoring
{
    bool          Enabled;                                      // [-]   - Listed as a chip and drawn as a card
    ChannelSource Source;                                       // [-]   - Value / Texture / Generator
    float         Amount;                                       // [-]   - Scalar editor's authored constant
    bool          AmountAuthored;                               // [-]   - false => the schema floor is shown, never an authored value
    float         Tint[3];                                      // [0-1] - Colour editor's authored RGB
    bool          TintAuthored;                                 // [-]   - false => the neutral placeholder is shown
    int           StrokeCount;                                  // [idx] - Strokes painted into the atlas for this channel
    bool          TexturePresent;                               // [-]   - An imported map sits beneath the strokes
    char          TextureName[TextureNameCapacity];             // [-]   - Imported filename
    int           TextureWidth;                                 // [px]  - Imported extent
    int           TextureHeight;                                // [px]  - Imported extent
    char          TextureFormat[FormatCapacity];                // [-]   - Imported encoding
    int           GeneratorOrdinal;                             // [idx] - Catalogue entry, -1 => none assigned
    float         GeneratorParameters[GeneratorParameterMax];   // [-]   - Live parameter values
};


// 📝 The layer's one mask.
struct MaskAuthoring
{
    bool       Enabled;                                       // [-]   - A mask exists at all
    MaskBase   Base;                                          // [-]   - White / Black starting tone
    bool       Inverted;                                      // [-]   - Flip the resolved coverage
    float      Strength;                                      // [%]   - Mask opacity multiplier, 0-100
    MaskSource Source;                                        // [-]   - Solid fill / procedural generator
    int        GeneratorOrdinal;                              // [idx] - Catalogue entry driving the coverage
    float      GeneratorParameters[GeneratorParameterMax];    // [-]   - Live parameter values
};


// 📝 One recorded revision. The log is linear: recording behind the cursor drops the redo tail.
struct HistoryEntry
{
    HistoryCategory Category;                   // [-]   - Timeline category
    HistoryScope    Scope;                      // [-]   - Layer / mask / one channel
    int             ScopeChannel;               // [idx] - Channel ordinal when Scope is ChannelSlot, else -1
    char            Title[TitleCapacity];       // [-]   - Row title
    char            Detail[DetailCapacity];     // [-]   - Secondary line, may be empty
    int             Hour;                       // [-]   - Stamped clock hour, 0-23
    int             Minute;                     // [-]   - Stamped clock minute, 0-59
};


// 📝 One layer of the sequence. Its material is the fourteen channel records; its mask is the single MaskAuthoring.
struct PaintLayerEntry
{
    char             Name[NameCapacity];            // [-]   - Display name
    char             Token[TokenCapacity];          // [-]   - Stable identity shown in the metadata rows
    int              CategoryOrdinal;               // [idx] - Index into the category table
    int              BlendOrdinal;                  // [idx] - Index into the blend table
    float            Opacity;                       // [%]   - Layer opacity, 0-100
    bool             Shown;                         // [-]   - Visibility
    bool             Expanded;                      // [-]   - Stack entry's fold
    bool             MaterialExpanded;              // [-]   - Material sub-card's fold
    bool             MaskExpanded;                  // [-]   - Mask sub-card's fold
    ImU32            Tag;                           // [-]   - Bubble / spine / footer hue
    ImU32            Paint;                         // [-]   - Base colour swatch
    char             PaintHex[8];                   // [-]   - Base colour as authored text
    ChannelAuthoring Channels[ChannelCount];        // [-]   - The layer's one material
    MaskAuthoring    Mask;                          // [-]   - The layer's one mask
    HistoryEntry     History[HistoryCapacity];      // [-]   - Recorded revisions, oldest first
    int              HistoryCount;                  // [idx] - Live entries in History
    int              Cursor;                        // [idx] - Revision the layer is resolved at, -1 => before the first
};


// 📝 Everything the card remembers between cycles. Caller-owned, so the panel itself stays stateless.
struct PaintLayerSequenceState
{
    char            SurfaceName[SurfaceCapacity];   // [-]   - Paint surface the sequence belongs to
    char            Filter[FilterCapacity];         // [-]   - Stack filter term
    int             Focus;                          // [idx] - Focused layer
    InspectorFace   Face;                           // [-]   - Layer / mask — one value the whole card reads
    int             Slide;                          // [idx] - 1 => stack + properties, 2 => preview + inspector
    char            OpenMenu[MenuKeyCapacity];      // [-]   - Call-site key of the one open menu, empty => none
    int             Lifted;                         // [idx] - Layer being dragged, -1 => none
    int             Renaming;                       // [idx] - Layer whose name is being edited, -1 => none
    HistoryFilter   Timeline;                       // [-]   - Timeline filter chip
    bool            ChannelFolded[ChannelCount];    // [-]   - Per-channel card fold in the inspector
    PaintLayerEntry Layers[LayerCapacity];          // [idx] - The sequence, index 0 on top
    int             LayerCount;                     // [idx] - Live entries in Layers
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// -- Vocabulary ----------------------------------------------------------------------------------------------------------

// 📝 The fourteen-row channel schema, in the fixed order every list and menu walks.
const ChannelDefinition* ResolveChannelTable();

// 📝 The generator catalogue.
const GeneratorDefinition* ResolveGeneratorTable();

// 📝 The layer category table.
const LayerCategoryDefinition* ResolveLayerCategoryTable();

// 📝 The timeline category table, indexed by HistoryCategory.
const HistoryCategoryDefinition* ResolveHistoryCategoryTable();

// 📝 One blend name by ordinal. Out-of-range clamps to the first entry.
const char* ResolveBlendName(int Ordinal);

// 📝 One add-menu section heading by ordinal.
const char* ResolveChannelSectionName(ChannelSection Section);

// 📝 One generator catalogue heading by ordinal.
const char* ResolveGeneratorSectionName(int Ordinal);

// 📝 Channel ordinal for a schema key, or -1 when the key is unknown.
int ResolveChannelOrdinal(const char* Key);


// -- Derived reads -------------------------------------------------------------------------------------------------------

// 📝 The focused layer, or null when the sequence is empty.
PaintLayerEntry* ResolveFocusedLayer(PaintLayerSequenceState& Sequence);
const PaintLayerEntry* ResolveFocusedLayer(const PaintLayerSequenceState& Sequence);

// 📝 The channel's live source. A derived channel always reports Value — it has nothing to author.
ChannelSource ResolveChannelSource(const PaintLayerEntry& Layer, int ChannelOrdinal);

// 📝 Enabled channels on a layer.
int CountEnabledChannels(const PaintLayerEntry& Layer);

// 📝 Strokes summed over every channel of a layer.
int AccumulateStrokeCount(const PaintLayerEntry& Layer);

// 📝 Channels whose source is Texture and which carry an imported base.
int CountImportedTextures(const PaintLayerEntry& Layer);

// 📝 Channels whose source is Generator and which have a catalogue entry assigned.
int CountAssignedGenerators(const PaintLayerEntry& Layer);

// 📝 The 0-1 grey the mask resolves to once base tone and invert fold together — what every mask swatch paints.
float ResolveMaskTone(const MaskAuthoring& Mask);

// 📝 Revisions on a layer matching one scope. Pass -1 for ScopeChannel on layer / mask scopes.
int CountScopedRevisions(const PaintLayerEntry& Layer, HistoryScope Scope, int ScopeChannel);


// -- Mutation ------------------------------------------------------------------------------------------------------------

// 📝 Append one revision, dropping the redo tail first. The clock stamp advances one minute per record so the timeline reads in order.
void RecordRevision(PaintLayerEntry&  Layer,
                    HistoryCategory   Category,
                    HistoryScope      Scope,
                    int               ScopeChannel,
                    const char*       Title,
                    const char*       Detail);

// 📝 Bind a generator to one channel and reseed its parameters from the catalogue defaults. Out-of-range clears the binding.
void AlignChannelGenerator(ChannelAuthoring& Authoring, int GeneratorOrdinal);

// 📝 Bind a generator to the mask and reseed its parameters from the catalogue defaults.
void AlignMaskGenerator(MaskAuthoring& Mask, int GeneratorOrdinal);

// 📝 Create a layer of one category at the top of the sequence and focus it. No-op once the sequence is full.
void ConstructLayer(PaintLayerSequenceState& Sequence, int CategoryOrdinal);

// 📝 Clone one layer into its own slot, pushing the original down, and re-date the copied revisions. No-op once the sequence is full.
void DuplicateLayer(PaintLayerSequenceState& Sequence, int Ordinal);

// 📝 Remove one layer. No-op while only one layer remains, exactly as the source guards it.
void ReclaimLayer(PaintLayerSequenceState& Sequence, int Ordinal);

// 📝 Move one layer to a drop slot expressed in pre-removal coordinates. No-op when the slot resolves to no movement.
void ReorderLayer(PaintLayerSequenceState& Sequence, int FromOrdinal, int Slot);

// 📝 Create a mask on one layer from the given source and focus the mask face.
void ConstructMask(PaintLayerSequenceState& Sequence, int Ordinal, MaskSource Source);

// 📝 Remove the mask from one layer and fall back to the layer face.
void ReclaimMask(PaintLayerSequenceState& Sequence, int Ordinal);

// 📝 Seed the sequence to the prototype's opening pose — six layers, each with its own recorded history.
void InitializePaintLayerSample(PaintLayerSequenceState& Sequence);

}   // namespace PaintLayerSequenceValidation

#endif
