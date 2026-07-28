/*==============================================================================================================================================
                                                            SCENEDIRECTORYPANEL.H
==============================================================================================================================================*/
// 🧩 A standalone validation surface for the outliner (the SceneDirectoryPanel) reproducing Outliner.html against the real Interface theme. It
//    owns a small in-memory SceneDirectory of RecordEntry items and draws the whole tree — tab strip, section header, search, chip filters, rows
//    with twisty / tinted classification icon / label / visibility eye, footer, plus a right-click ContextMenu and an add-object menu. Fully
//    decoupled from the modelling / paint / bake applications: it reads the shared theme and edits only its own caller-owned state.
//
//    Its data types live in the app-local namespace SceneDirectoryValidation, NOT namespace Frontier, so the panel's own RecordEntry /
//    RecordClassification / RecordToken never collide with the pillar's Frontier::RecordEntry (the shared Scene tree) when the theme type is
//    pulled into scope. This validation carries its own self-contained demo tree; it is intentionally NOT the thin shared OutlinerPanel.

#pragma once
#ifndef FRONTIER_SCENEDIRECTORY_VALIDATION_PANEL_H
#define FRONTIER_SCENEDIRECTORY_VALIDATION_PANEL_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier { struct SvgIconRegistry; }

namespace SceneDirectoryValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                              TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A stable, stale-detecting identity for one row (SKILL-Naming §4). Issued by a monotonic counter in the state; never reused so a
//    deleted row's token can never alias a live one. 0 is the null token.
using RecordToken = std::uint32_t;

// 📝 What kind of item a row is (SKILL-Naming §4: RecordClassification, never Kind/Role/EntityType). Drives the tinted icon, the add-object
//    catalogue, and the chip filters. Containers (SceneRoot / EnclosureFolder) hold a SubtreeRegion; leaves do not.
enum class RecordClassification
{
    SceneRoot,          // [-] - The single depth-0 container ("Scene")
    EnclosureFolder,    // [-] - A grouping container (folders / categories / GRP_*)
    PolygonSurface,     // [-] - A renderable surface leaf (SM_*)
    LightEmitter,       // [-] - A light leaf (sun / area)
    CameraLens,         // [-] - A camera leaf
    EnvironmentDome     // [-] - An environment / HDRI leaf
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One item in the tree (SKILL-Naming §4: RecordEntry, never Node/Element/Entity). Its down-links live inline in NestedRegion (the
//    SubtreeRegion); the up-link is implicit in ownership. IconGlyph selects a drawn glyph independent of Classification so a folder-icon
//    group and a category container can share the folder glyph while differing in classification.
struct RecordEntry
{
    RecordToken                Token;          // [-]   - Stable identity issued by the state's TokenIssuer
    std::string                Label;          // [-]   - Display name (editable via rename)
    RecordClassification       Classification; // [-]   - What kind of item this is (icon + filter facet)
    const char*                IconGlyph;      // [-]   - Which drawn glyph key this row shows (scene/folder/mesh/sun/...)
    std::uint32_t              TintColor;      // [-]   - Packed ImU32 accent tint (icon + colour-filter facet)
    bool                       ExpandedState;  // [-]   - Container fold state (true = open)
    bool                       ConcealedState; // [-]   - Visibility toggle (true = hidden, cascades to the region)
    std::vector<RecordEntry>   NestedRegion;   // [-]   - The SubtreeRegion — items nested one tier deeper
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


// 📝 All caller-owned state the outliner edits in place. One instance lives in main() for the life of the window. The panel reads / writes
//    only through it so the drawing stays stateless. Mirrors the data model + interaction flags of Outliner.html.
struct SceneDirectoryState
{
    // -- Backing tree + identity issuer --
    std::vector<RecordEntry> RootRegion;                 // [-] - Top-tier items (a single SceneRoot in the default content)
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
    RecordToken              ContextMenuTarget = 0;      // [-] - Row the ContextMenu was opened over
    bool                     ContextMenuRequested = false; // [-] - Open the ContextMenu popup this cycle
    bool                     AddMenuRequested     = false; // [-] - Open the add-object popup this cycle
    RecordToken              AddMenuAnchor        = 0;      // [-] - Where a new object should land (0 = Scene root)
    bool                     FilterMenuRequested  = false;  // [-] - Open the add-filter popup this cycle

    // -- Smooth (eased) tree scrolling with rubber-band overscroll --
    float                    ScrollTarget    = 0.0f;      // [px] - Where the wheel wants the scroll to land (may sit past the ends while overscrolling)
    float                    ScrollActual    = 0.0f;      // [px] - The rendered scroll, eased toward ScrollTarget each frame so the wheel feels like it lags
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Assemble the default demonstration tree (Scene → Environment / Cameras / Lights / Geometry), mirroring the Outliner.html content, and
//    pre-select SM_Body. Call once before the frame loop.
void InitializeSceneDirectorySample(SceneDirectoryState& State);

// 📝 Draw the whole outliner inside the current ImGui window. Handles selection, rename, visibility, drag relocation, filters, and both menus.
//    IconRegistry supplies the resolved scene-tier SVG textures (keys "scene-" / "g-"); pass a registry whose scene + global packs are registered
//    for real multi-colour glyphs, or nullptr to fall back to the procedural stroke art. When present, glyphs draw at their native SVG colour.
void ConstructSceneDirectoryPanel(const Frontier::ThemeConfiguration& Theme, SceneDirectoryState& State,
                                  const Frontier::SvgIconRegistry* IconRegistry);

}   // namespace SceneDirectoryValidation

#endif
