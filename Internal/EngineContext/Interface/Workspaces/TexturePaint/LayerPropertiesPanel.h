/*==============================================================================================================================================
                                                          LAYERPROPERTIESPANEL.H
==============================================================================================================================================*/
// 🧩 The masking half of a paint layer's property column: one "Mask" card carrying the base tone, the invert flip, the strength, and the mask
//    SOURCE — either painted strokes (with an optional imported base texture) or a procedural generator with its own parameter rows. Built
//    entirely from the shared Interface controls (SelectionEntry chips, ValueSlider pills, PropertyPanelBase cards), so it inherits the
//    ControlsGallery look with no private chrome beyond the three bespoke strips this file draws: the mask preview swatch, the texture slot,
//    and the grouped generator popup.
//
// 🔴 The generator table is VOCABULARY, not state: LayerPropertiesState only remembers which generator is chosen and the parameter values for
//    it. Switching generators reseeds the parameters from the table's defaults, exactly as the source design does.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_LAYERPROPERTIESPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_LAYERPROPERTIESPANEL_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int MaskGeneratorCapacity      = 5;    // [-] - Generators in the built-in catalogue
constexpr int MaskGeneratorGroupCapacity = 3;    // [-] - Catalogue group headings (Mask / Wear / Procedural)
constexpr int MaskParameterCapacity      = 3;    // [-] - Widest generator's parameter count
constexpr int MaskTextureLabelCapacity   = 96;   // [-] - Imported-texture filename buffer


//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The tone the mask starts from before invert + strength are applied.
enum class MaskBaseTone
{
    White = 0,
    Black = 1
};


// 📝 Where the mask's coverage comes from. Only one source is live at a time; the other keeps its state for when it returns.
enum class MaskSourceOrigin
{
    Painted   = 0,   // hand-painted strokes in the mask atlas, optionally over an imported base
    Generated = 1    // a procedural generator evaluated from surface signals (curvature, occlusion, noise)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One tweakable input of a generator. Caller-facing vocabulary only — the live value lives in LayerPropertiesState.
struct MaskParameterDefinition
{
    const char* Label;      // [-]  - Row caption
    float       Minimum;    // [-]  - Lower bound
    float       Maximum;    // [-]  - Upper bound
    float       Default;    // [-]  - Value a freshly-chosen generator seeds this parameter to
};


// 📝 One entry of the built-in generator catalogue.
struct MaskGeneratorDefinition
{
    const char*             Label;                              // [-] - Menu + slot caption
    const char*             Group;                              // [-] - Catalogue heading this generator sits under
    const char*             Summary;                            // [-] - Short "what it does" note shown beside the menu row
    MaskParameterDefinition Parameters[MaskParameterCapacity];   // [-] - Parameter rows, Label==null terminates
    int                     ParameterCount;                     // [-] - Live entries in Parameters
};


// 📝 Everything the mask card remembers between cycles. Caller-owned so the panel itself stays stateless (the GlassButtonPass rule).
struct LayerPropertiesState
{
    // -- Card folds (PropertyPanelBase intent flags) ---------------------------------------------------------------------
    bool MaskExpanded = true;    // [-] - The Mask card's collapse intent

    // -- Mask tone -------------------------------------------------------------------------------------------------------
    int   BaseToneOrdinal = static_cast<int>(MaskBaseTone::White);   // [-]  - Index into the White/Black chips
    int   InvertOrdinal   = 0;                                       // [-]  - Index into the Off/On chips
    float Strength        = 1.0f;                                    // [0-1] - Mask opacity multiplier

    // -- Source selection ------------------------------------------------------------------------------------------------
    int SourceOrdinal = static_cast<int>(MaskSourceOrigin::Painted);  // [-] - Index into the Painted/Generated chips

    // -- Painted source --------------------------------------------------------------------------------------------------
    int  StrokeCount    = 148;                                  // [-] - Strokes recorded in the mask atlas (0 => unallocated)
    bool TexturePresent = false;                                // [-] - An imported base texture is bound
    char TextureLabel[MaskTextureLabelCapacity] = { '\0' };     // [-] - Imported texture's filename

    // -- Generated source ------------------------------------------------------------------------------------------------
    int   GeneratorOrdinal = 2;                                       // [-] - Index into the catalogue, <0 => no generator chosen
    float ParameterValues[MaskParameterCapacity] = { 0.6f, 0.3f, 0.45f };   // [-] - Live values for the chosen generator
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The built-in generator catalogue. Returns the table and writes its length; the table is static, so the pointer outlives any caller.
const MaskGeneratorDefinition* ResolveMaskGeneratorTable(int& Count);

// 📝 The catalogue's group headings, in the order the popup lists them.
const char* const* ResolveMaskGeneratorGroupTable(int& Count);

// 📝 Resolve the chosen generator, or null when the ordinal is out of range (no generator bound).
const MaskGeneratorDefinition* ResolveMaskGenerator(const LayerPropertiesState& State);

// 📝 Bind a generator by catalogue ordinal and reseed every parameter from its defaults. Out-of-range clears the binding.
void AlignMaskGenerator(LayerPropertiesState& State, int Ordinal);

// 📝 Reseed the bound generator's parameters from its defaults, leaving the binding intact. No-op when nothing is bound.
void RestoreMaskParameters(LayerPropertiesState& State);

// 📝 The 0-1 grey the mask resolves to once base tone, invert and strength are folded together — what the preview swatch paints.
float ResolveMaskPreviewTone(const LayerPropertiesState& State);

// 📝 Seed the sample state the validation host opens on (a metal-edge-wear generator over a painted atlas).
void InitializeLayerPropertiesSample(LayerPropertiesState& State);

// 📝 Record the whole mask property column. UI only — nothing here touches a texture or a GPU resource.
void ConstructLayerPropertiesPanel(const ThemeConfiguration& Theme, LayerPropertiesState& State);

}   // namespace Frontier

#endif
