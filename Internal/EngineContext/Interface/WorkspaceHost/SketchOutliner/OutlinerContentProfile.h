/*==============================================================================================================================================
                                                          OUTLINERCONTENTPROFILE.H
==============================================================================================================================================*/
// 🧩 The content profile that makes ONE outliner panel drive any tree. The panel (SketchOutlinerPanel) owns all behaviour — rows, twisties,
//    selection, rename, drag, eased scroll, filters, menus — and knows NOTHING about what the rows mean. Everything app-specific is DATA carried
//    here: the classification vocabulary (as an opaque-id table), the add-object catalogue, the filter facets, the header caption + search hint,
//    the default sample tree, and the procedural stroke art drawn when a registry glyph has not uploaded. A caller hands the panel a profile the
//    same way the Paint console takes a SurfaceBinding: "more outliners" means "another profile", never "another panel".
//
//    The classification is an OPAQUE int (RecordEntry.ClassificationId): the panel never names a value. Each profile owns the ClassRows table that
//    gives an id its label / icon key / tint / container flag, so a sketch tree (PartRoot / SketchCurve / ...) and a scene tree (SceneRoot /
//    LightEmitter / ...) are two data tables over the same code.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_SKETCHOUTLINER_CONTENTPROFILE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_SKETCHOUTLINER_CONTENTPROFILE_H

#include "imgui.h"

#include <cstdint>

namespace Frontier::SketchOutlinerUi
{

struct SketchOutlinerState;   // forward — the sample seeder fills one


//------------------------------------------------------------------------------------------------------------------------
//                                                          PROFILE TABLES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One classification the profile understands. ClassificationId is the opaque value stored on RecordEntry; everything else is how the panel
//    should present it. Container => holds a SubtreeRegion (draws a twisty, accepts drops, and a new object may land inside it).
struct OutlinerClassRow
{
    int           ClassificationId;   // [-] - Opaque id stored on RecordEntry.ClassificationId
    const char*   Label;              // [-] - Filter-chip / add-menu display label
    const char*   IconKey;            // [-] - Registry key of the drawn glyph ("cad-profile" / "scene-mesh" / "g-folder")
    ImU32         Tint;               // [-] - Icon tint + colour-filter facet value
    bool          Container;          // [-] - Holds a SubtreeRegion (twisty · drop target · add destination)
};

// 📝 One row of the add-object catalogue. Section non-null => a faint section label, the rest ignored. Otherwise a creatable item: Stem drives the
//    unique-name scheme ("Sketch", then "Sketch_01"...) and ClassificationId is looked up in ClassRows for its icon / tint / container flag.
struct OutlinerAddRow
{
    const char*   Section;            // [-] - Non-null => section label row, not a creatable item
    const char*   Label;              // [-] - Menu label (what the dropdown row reads)
    const char*   Stem;               // [-] - Unique-name stem for the created item
    int           ClassificationId;   // [-] - Classification of the created item (looked up in ClassRows)
};

// 📝 One colour swatch in the COLOURS filter section.
struct OutlinerTintFacet
{
    ImU32         Tint;               // [-] - Packed tint the chip filters on
    const char*   Label;              // [-] - Swatch label ("Blue" / "Amber" / ...)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          THE PROFILE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the panel needs that is NOT behaviour. Pointer tables are borrowed — a profile is normally a static const the Resolve* function
//    returns, so the tables outlive every frame. The two function pointers let a profile author its own default tree and its own procedural
//    fallback art without the panel naming a single classification.
struct OutlinerContentProfile
{
    const char*                 HeaderCaption;      // [-] - Section-header title ("SKETCH" / "SCENE")
    const char*                 SearchHint;         // [-] - Search-box placeholder ("Filter sketch...")

    const OutlinerClassRow*     ClassRows;          // [-] - Classification table (id -> label / icon / tint / container)
    int                         ClassRowCount;      // [-]

    const OutlinerAddRow*       AddRows;            // [-] - Add-object catalogue (sections + items)
    int                         AddRowCount;        // [-]

    const int*                  FilterClassIds;     // [-] - Which classification ids appear in the TYPES filter section, in order
    int                         FilterClassIdCount; // [-]

    const OutlinerTintFacet*    TintFacets;         // [-] - COLOURS filter section
    int                         TintFacetCount;     // [-]

    // 📝 Build the default demonstration tree into State (clears it first) and pre-select the profile's landmark row. Called by
    //    InitializeSketchOutlinerSample.
    void (*SeedSample)(SketchOutlinerState& State, const OutlinerContentProfile& Profile);

    // 📝 Draw an 18px procedural stroke glyph for IconKey, centred at Origin, tinted by Tint — used only when the registry is null or that key
    //    has not uploaded. Each profile owns the art for its own icon keys.
    void (*PaintFallbackGlyph)(ImDrawList* DrawList, const char* IconKey, ImVec2 Origin, ImU32 Tint);
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      BUILT-IN PROFILES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The parametric-sketch (CAD) content: Part / Origin / Sketches / Bodies / Features, "cad-" icon keys, the CAD add catalogue + facets.
const OutlinerContentProfile& ResolveSketchContentProfile();

// 📝 The scene content: Scene / Environment / Cameras / Lights / Geometry, "scene-" icon keys, the scene add catalogue + facets.
const OutlinerContentProfile& ResolveSceneContentProfile();

}   // namespace Frontier::SketchOutlinerUi

#endif
