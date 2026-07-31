/*==============================================================================================================================================
                                                           PAINTCATALOGUE.H
==============================================================================================================================================*/
// 🧩 The texture-paint workspace's instrument catalogue: 10 families over 102 instruments, each with its authored SVG art in two crops. The card
//    components render whatever tables they are handed; this is the paint workspace's set, the same way ModellingCatalogue is the modelling one.
//    ⚠️ GENERATED — see PaintCatalogue.cpp.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTCATALOGUE_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTCATALOGUE_H

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//----------------------------------------------------------------------------------------------------------------------

// 📝 The widest `variants` list any instrument carries, sized from the data rather than guessed. Only the dry-media family
//    uses it (charcoal Vine/Compressed, pastel Soft/Oil/Hard) and it drives that family's variant reveal.
constexpr int PaintVariantLimit = 3;

constexpr int PaintFamilyCount = 10;
constexpr int PaintInstrumentCount = 102;

//----------------------------------------------------------------------------------------------------------------------
//                                                           TYPES
//----------------------------------------------------------------------------------------------------------------------

// 🔴 A three-valued flag, for the SEED booleans where `false` is genuinely AUTHORED and therefore means something different
//    from absent. `taper` is why this exists: the prototype's default is `def:Tool.taper !== false`, so an ABSENT taper
//    means TRUE. Four pens author taper:false, one authors taper:true, five leave it out — collapsing absent and false into
//    a plain bool would flip 6 of the 10 pens from Speed-taper-on to off, and nothing would report it.
//    📝 Unauthored is 0 so a zero-initialised descriptor reads as "nothing authored", which is what an absent SEED row is.
enum class PaintAuthoredFlag
{
    Unauthored = 0,
    AuthoredTrue,
    AuthoredFalse,
};


// 📝 One instrument. The five base fields come from the family walk; the rest are SEED overrides, absent on most
//    instruments — a null string or a false flag means "not authored", which is what the schema's reveals test.
//    🔴 The art is held as TWO pre-baked documents rather than one document plus a crop parameter. SvgIconRegistry
//       hashes only the document bytes and the raster edge, so two keys over one document would collide onto a single
//       texture; distinct bytes keep them separate without touching the shared registry.
struct PaintInstrumentDescriptor
{
    const char* Label;          // [-] - display name, e.g. "Classic Gold-Nib Fountain"
    const char* FamilyKey;      // [-] - owning family's key, e.g. "pen"
    const char* DotColour;      // [-] - the family's rail dot, as an authored CSS hex
    const char* NibDocument;    // [-] - complete <svg>, viewBox cropped to the 48x48 nib window
    const char* FullDocument;   // [-] - complete <svg>, the authored 300x60 landscape box
    bool        Airbrush;                           // [-] - false when not authored (false is never authored, so this is lossless)
    const char* Binder;                             // [-] - null when not authored
    const char* Bristle;                            // [-] - null when not authored
    const char* DryTitle;                           // [-] - null when not authored
    const char* Edge;                               // [-] - null when not authored
    const char* Etype;                              // [-] - null when not authored
    const char* Grade;                              // [-] - null when not authored
    float       Hardness;                           // [-] - 0 when not authored
    bool        Highlighter;                        // [-] - false when not authored (false is never authored, so this is lossless)
    const char* Ink;                                // [-] - null when not authored
    const char* Lead;                               // [-] - null when not authored
    bool        Mechanical;                         // [-] - false when not authored (false is never authored, so this is lossless)
    const char* Nib;                                // [-] - null when not authored
    const char* NibShape;                           // [-] - null when not authored
    const char* Nozzle;                             // [-] - null when not authored
    const char* Paint;                              // [-] - null when not authored
    const char* Shape;                              // [-] - null when not authored
    PaintAuthoredFlag Taper;                        // [-] - Unauthored / AuthoredTrue / AuthoredFalse; absent != false here
    const char* Variant;                            // [-] - null when not authored
    const char* Variants[PaintVariantLimit];        // [-] - null-padded
};


// 📝 One family band in rail order. The tally is carried rather than counted at draw time because the rail shows it
//    beside every row, and recounting 102 instruments per row per frame would be work for a number that never changes.
struct PaintFamilyDescriptor
{
    const char* Key;      // [-] - "pen", "colored-pencil", ...
    const char* Caption;  // [-] - the rail's display word, e.g. "Coloured"
    const char* DotColour;// [-] - authored CSS hex for the rail dot
    int         Tally;    // [-] - how many instruments this family holds

    // 🔴 The preview's INK choices, per family rather than per instrument — the prototype keys SWATCHES by family key, so
    //    every pen offers the same four inks. Ragged on purpose: eraser carries ONE (paper #f4f1ea, because an eraser
    //    lays down the page colour) while brush and marker carry five, so the count travels with the pointer instead of
    //    a fixed-width array needing a sentinel every consumer would have to know about.
    const char* const* Swatches;    // [-] - authored CSS hex inks, SwatchCount long
    int                SwatchCount; // [-] - 1..5
};

//----------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//----------------------------------------------------------------------------------------------------------------------

// The family table in rail order, and its length.
const PaintFamilyDescriptor* ResolvePaintFamilies(int& FamilyCount);

// The full instrument table, and its length. Instruments are grouped by family in rail order.
const PaintInstrumentDescriptor* ResolvePaintInstruments(int& InstrumentCount);

// The half-open index range [FirstIndex, LastIndex) of one family's instruments in the instrument table. Contiguous by
// construction, so a family's grid is a slice rather than a filter.
void ResolvePaintFamilyRange(int FamilyIndex, int& FirstIndex, int& LastIndex);

} // namespace Frontier

#endif
