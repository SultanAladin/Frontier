/*==============================================================================================================================================
                                                            SURFACEPAINTER.H
==============================================================================================================================================*/
// 🧩 The console's SECOND extension point, and the counterpart to VerdictResolver: where the resolver lets a workspace answer a question the console
//    asks, a painter lets a workspace DRAW a region the console has laid out but cannot fill. Same shape for the same reasons — a plain function
//    pointer plus an opaque context, so a binding is trivially copyable, allocates nothing, and adds no vtable to a struct the console copies per
//    frame.
//
//    🔴 Why this exists at all. Two of paint's surfaces cannot be expressed in the console's vocabulary and must not be dropped:
//      * the SLIDE-2 LEFT COLUMN, which for modelling is a probe readout but for paint is a live stroke preview over pale paper with an ink swatch
//        strip — a canvas, not a table of readings.
//      * the ACTION TILE's artwork, which for modelling is a glyph mark but for paint is a round well holding an SVG nib crop, at one of two art
//        modes, uploaded through the workspace's own texture stores.
//    Widening the console to model a stroke preview or a nib well would put paint-only concepts in front of every modelling and drafting author,
//    which is the thing this module was built to avoid. So the console keeps owning the LAYOUT (where the column is, where each tile sits, what is
//    clipped, what is scrolled) and hands the workspace a rectangle to paint. The workspace draws its own panel inside the console's frame.
//
//    📝 A painter is called with the region already clipped and positioned. It draws with ImGui's current window draw list and must not open a
//       window, push its own clip, or move the ImGui cursor — the console is mid-layout around it. Null is always legal and always means "the console
//       falls back to what it does natively" (the probe strip; the placeholder glyph), which is why modelling and construction bind neither.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PREDICATE_SURFACEPAINTER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PREDICATE_SURFACEPAINTER_H

#include "imgui.h"

namespace Frontier
{

struct ActionDescriptor;
struct PaletteSpecification;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The rectangle a painter fills, handed over rather than re-derived. The console has already applied the carousel shift and the open-pop scale, so
//    a painter that recomputed either from the theme would drift out of the slide it is riding on by a few pixels per frame of travel.
struct SurfaceRegion
{
    ImVec2 Minimum;      // [px] - top-left, in screen space, pop-scaled and carousel-shifted
    ImVec2 Maximum;      // [px] - bottom-right
    float  PopScale;     // [-]  - the open animation's scale, for a painter sizing its own art
};


// 📝 Fills the console's slide-2 left column. Returns nothing: what a preview reports (a swatch click) is folded back through the workspace's own
//    context pointer, which it owns and the console never reads — the console has no vocabulary for "an ink was chosen" and should not grow one.
using ColumnPainter = void (*)(const SurfaceRegion& Region, const PaletteSpecification& Palette, void* Context);


// 📝 Fills ONE action tile's artwork area, in place of the console's glyph mark. Called per drawn tile, inside the grid's scroll child and under the
//    console's clip. Gated is passed rather than looked up so a painter can grey its own art the way the glyph path does, without reaching back
//    through the gate for a verdict the console has already resolved this frame.
//    🔴 Returning false means "I did not paint this one" and the console draws its normal glyph instead. That is what keeps a workspace from having
//       to supply art for every action just to supply it for some — paint's wells cover instruments, but a future paint cluster of plain commands
//       would fall back per tile rather than per workspace.
using TilePainter = bool (*)(const ActionDescriptor& Action, const SurfaceRegion& Region, bool Gated, void* Context);


// 📝 Both painters plus the one context they share, because they are one workspace's answer to "draw your own panel" and always travel together. The
//    context is void* rather than const void* — unlike a verdict, painting legitimately mutates (a lazy texture upload, a cached raster).
struct SurfaceBinding
{
    ColumnPainter PaintColumn = nullptr;   // [-] - slide-2 left column; null falls back to the probe strip
    TilePainter   PaintTile   = nullptr;   // [-] - per-tile artwork; null falls back to the console glyph
    void*         Context     = nullptr;   // [-] - the workspace's own state, opaque to the console (borrowed)
};

} // namespace Frontier

#endif
