/*==============================================================================================================================================
                                                            GLYPHINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The backend-agnostic drawing unit for vector menu glyphs: a glyph is declared as short runs of points plus circles in a 0-1 unit square, and
//    inscribed into any pixel box on any ImGui backend through ImDrawList alone. No texture, no upload, no device — which is what makes it usable
//    from a Direct3D validation host as well as from the Vulkan editor, where the texture-backed icon registry is not.
//    Knows nothing about polygons or topology: it draws unit-square paths. The paths themselves live with the domain that owns them.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_GLYPHINSCRIPTION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_GLYPHINSCRIPTION_H

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int GlyphPointCapacity  = 48;   // [idx] - Most points one glyph may declare across all its runs
constexpr int GlyphRunCapacity    = 8;    // [idx] - Most separate polylines one glyph may declare
constexpr int GlyphCircleCapacity = 4;    // [idx] - Most circles/dots one glyph may declare


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One point in glyph space: 0-1 on both axes, Y downward to match ImGui screen space (so a path reads the same way it draws).
struct GlyphPoint
{
    float X;    // [0-1] - Horizontal position within the unit square
    float Y;    // [0-1] - Vertical position, 0 at the top
};


// 📝 One polyline within a glyph, addressed as a span into the glyph's shared point array. Closed joins the last point back to
//    the first; Filled fills the run rather than stroking it (an arrowhead, a solid swatch).
struct GlyphRun
{
    int  FirstPoint;    // [idx] - Index of this run's first point
    int  PointCount;    // [idx] - Points in this run
    bool Closed;        // [-]   - Join last point to first
    bool Filled;        // [-]   - Fill instead of stroke
};


// 📝 One circle within a glyph — a vertex dot, a pivot marker, a rounded cap. Filled circles read as vertices; hollow ones read
//    as pivots, which is the same distinction the source mockup's glyphs draw.
struct GlyphCircle
{
    GlyphPoint Centre;      // [0-1] - Centre in glyph space
    float      Radius;      // [0-1] - Radius as a fraction of the box side
    bool       Filled;      // [-]   - Solid dot rather than an outline
};


// 📝 A complete glyph: one shared point array, the runs that index into it, and any circles. Fixed-capacity POD so the whole
//    glyph table is a constant-expression static array with no allocation and no initialisation order to reason about.
struct GlyphStrokeSet
{
    GlyphPoint  Points[GlyphPointCapacity]  = {};
    GlyphRun    Runs[GlyphRunCapacity]      = {};
    GlyphCircle Circles[GlyphCircleCapacity] = {};
    int         PointCount  = 0;    // [idx] - Points populated
    int         RunCount    = 0;    // [idx] - Runs populated
    int         CircleCount = 0;    // [idx] - Circles populated
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Inscribe one glyph into a square pixel box. Every coordinate is scaled by BoxSide and offset by BoxOrigin, so the caller
//    controls size purely by the box it passes — no per-glyph scale bookkeeping. Safe against a null draw list and an empty set.
void InscribeGlyph(ImDrawList*           DrawList,
                   const GlyphStrokeSet& Strokes,
                   ImVec2                BoxOrigin,
                   float                 BoxSide,
                   ImU32                 Tint,
                   float                 StrokeWidth);

// 📝 Append one polyline to a stroke set under construction, returning false when either capacity would overflow. Used by the
//    glyph tables to build their definitions readably instead of hand-indexing the shared point array.
bool AppendGlyphRun(GlyphStrokeSet& Strokes, const GlyphPoint* RunPoints, int RunPointCount, bool Closed, bool Filled);

// 📝 Append one circle to a stroke set under construction; false when the circle capacity would overflow.
bool AppendGlyphCircle(GlyphStrokeSet& Strokes, GlyphPoint Centre, float Radius, bool Filled);

}   // namespace Frontier

#endif
