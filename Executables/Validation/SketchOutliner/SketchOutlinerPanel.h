/*==============================================================================================================================================
                                                            SKETCHOUTLINERPANEL.H
==============================================================================================================================================*/
// 🧩 A standalone validation surface for the parametric-sketch outliner — the modeling-tree counterpart to SceneDirectoryPanel. It owns a small
//    in-memory directory of RecordEntry items describing a parametric part (the part root, datum primitives + coordinate frame, sketches with
//    their curves and constraints, solid shells, features in the build order, and a component instance) and draws the whole tree: section header,
//    search, chip filters, animated rows (twisty · glyph · label · visibility eye), footer, plus a right-click ContextMenu and an add-object menu.
//    Every row's icon is a real parametric-sketch SVG glyph resolved out of the SvgIconRegistry ("cad-" / "g-" keys), with a procedural stroke
//    fallback when a key has not uploaded.
//
//    It is fully self-contained: one caller-owned SketchOutlinerState lives for the window's life; the two entry points below build it once and
//    draw one frame, so a main loop can drive it with parameters exactly like SceneDirectoryValidation. Its data types live in the app-local
//    namespace SketchOutlinerValidation, NOT namespace Frontier, so its RecordEntry / RecordToken / RecordClassification never collide with the pillar's.

#pragma once
#ifndef FRONTIER_SKETCHOUTLINER_VALIDATION_PANEL_H
#define FRONTIER_SKETCHOUTLINER_VALIDATION_PANEL_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier { struct SvgIconRegistry; }

namespace SketchOutlinerValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                              TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A stable, stale-detecting identity for one row (SKILL-Naming §4). Issued by a monotonic counter in the state; never reused so a deleted
//    row's token can never alias a live one. 0 is the null token.
using RecordToken = std::uint32_t;

// 📝 What kind of parametric-sketch item a row is (SKILL-Naming §4: RecordClassification, never Kind/Role/EntityType). Drives the resolved icon,
//    the add-object catalogue, and the chip filters. Containers (PartRoot / FeatureDirectory / SketchProfile) hold a SubtreeRegion; leaves do not.
enum class RecordClassification
{
    PartRoot,             // [-] - The single depth-0 container ("Part")
    FeatureDirectory,     // [-] - A grouping container (Origin / Sketches / Bodies / a user directory)
    DatumPlane,           // [-] - A reference plane leaf (XY / YZ / custom)
    DatumAxis,            // [-] - A reference axis leaf
    DatumPoint,           // [-] - A reference point leaf
    CoordinateFrame,      // [-] - The origin coordinate frame leaf
    SketchProfile,        // [-] - A 2D sketch container (holds curves + constraints)
    SketchCurve,          // [-] - A curve inside a sketch (line / arc / spline)
    SketchConstraint,     // [-] - A geometric constraint inside a sketch
    DimensionalConstraint,// [-] - A dimensional (measured) constraint inside a sketch
    SolidShell,           // [-] - A resulting closed solid shell leaf
    FeatureOperation,     // [-] - A feature in the build order (extrude / revolve / fillet)
    ComponentInstance     // [-] - A referenced sub-part instance leaf
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One item in the tree (SKILL-Naming §4: RecordEntry, never Node/Element/Entity). Its down-links live inline in NestedRegion (the
//    SubtreeRegion); the up-link is implicit in ownership. IconKey selects the resolved SVG glyph independent of Classification, so a datum
//    directory and a body directory can share the directory glyph while differing in classification.
struct RecordEntry
{
    RecordToken                Token;          // [-] - Stable identity issued by the state's TokenIssuer
    std::string                Label;          // [-] - Display name (editable via rename)
    RecordClassification       Classification; // [-] - What kind of sketch item this is (icon + filter facet)
    std::string                IconKey;        // [-] - Registry key of the drawn glyph (cad-profile / g-folder / ...)
    std::uint32_t              TintColor;      // [-] - Packed ImU32 accent tint (icon tint + colour-filter facet)
    bool                       ExpandedState;  // [-] - Container fold state (true = open)
    bool                       ConcealedState; // [-] - Visibility toggle (true = suppressed, cascades to the region)
    std::vector<RecordEntry>   NestedRegion;   // [-] - The SubtreeRegion — items nested one tier deeper
};


// 📝 One active filter chip. A row survives the facet if its classification / tint / visibility matches every active chip of that facet.
enum class FilterFacet
{
    Classification,     // [-] - Match RecordEntry.Classification against ClassificationValue
    Tint,               // [-] - Match RecordEntry.TintColor against TintValue
    OnlyVisible         // [-] - Keep only rows whose ConcealedState is false
};

struct FilterChip
{
    FilterFacet          Facet;                 // [-] - Which facet this chip constrains
    RecordClassification ClassificationValue;   // [-] - Target classification (Facet == Classification)
    std::uint32_t        TintValue;             // [-] - Target packed tint (Facet == Tint)
};


// 📝 All caller-owned state the outliner edits in place. One instance lives in the driver for the life of the window. The panel reads / writes
//    only through it so the drawing stays stateless; passing a different state draws a different part, so a main loop can parameterize it.
struct SketchOutlinerState
{
    // -- Backing tree + identity issuer --
    std::vector<RecordEntry> RootRegion;                 // [-] - Top-tier items (a single PartRoot in the default content)
    RecordToken              NextToken       = 1;        // [-] - Monotonic TokenIssuer cursor (0 reserved as null)

    // -- Selection (multi-select) + range anchor --
    std::vector<RecordToken> SelectionSet;               // [-] - Currently selected tokens
    RecordToken              RangeAnchor     = 0;        // [-] - Shift-range anchor (last single click)

    // -- Search + chip filters --
    char                     SearchText[128] = "";       // [-] - Live name-substring filter (lowercased on compare)
    std::vector<FilterChip>  FilterChips;                // [-] - Empty => show everything

    // -- Rename edit buffer (which token is being renamed + its scratch text) --
    RecordToken              RenameTarget    = 0;        // [-] - Token under inline rename (0 = none)
    char                     RenameBuffer[128] = "";     // [-] - Scratch text for the active rename
    bool                     RenameJustOpened  = false;  // [-] - One-shot focus request for the rename input

    // -- Context menu + add-object menu open requests (token they were opened over) --
    RecordToken              ContextMenuTarget    = 0;      // [-] - Row the ContextMenu was opened over
    bool                     ContextMenuRequested = false;  // [-] - Open the ContextMenu popup this cycle
    bool                     AddMenuRequested     = false;  // [-] - Open the add-object popup this cycle
    RecordToken              AddMenuAnchor        = 0;      // [-] - Where a new object should land (0 = Part root)
    bool                     FilterMenuRequested  = false;  // [-] - Open the add-filter popup this cycle

    // -- Smooth (eased) tree scrolling with rubber-band overscroll --
    float                    ScrollTarget    = 0.0f;      // [px] - Where the wheel wants the scroll to land (may sit past the ends while overscrolling)
    float                    ScrollActual    = 0.0f;      // [px] - The rendered scroll, eased toward ScrollTarget each frame so the wheel feels like it lags
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Assemble the default demonstration parametric part (Part → Origin/Sketches/Bodies + a feature order), and pre-select the first sketch. Call
//    once before the frame loop.
void InitializeSketchOutlinerSample(SketchOutlinerState& State);

// 📝 Draw the whole outliner inside the current ImGui window. Handles selection, rename, visibility, drag relocation, filters, and both menus.
//    IconRegistry supplies the resolved parametric-sketch SVG textures; pass a registry whose "cad-"/"g-" packs are registered. A null / empty
//    registry falls back to procedural glyph strokes so the panel still renders.
void ConstructSketchOutlinerPanel(const Frontier::ThemeConfiguration& Theme,
                                  SketchOutlinerState&                State,
                                  const Frontier::SvgIconRegistry*    IconRegistry);

}   // namespace SketchOutlinerValidation

#endif
