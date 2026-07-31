/*==============================================================================================================================================
                                                         POLYGONGLYPHIDENTITY.H
==============================================================================================================================================*/
// 🧩 The glyph vocabulary for polygon-mutation operations — names only, no geometry and no ImGui. Split from the stroke table so the operation
//    catalogue can name a glyph without acquiring a dependency on a drawing backend: the catalogue is data about topology, and it stays
//    compilable in a unit test that never links a renderer. The paths themselves live in PolygonGlyphTable.
//    Glyphs are SHARED across related operations: the four merge variants read one MergeInward, the three dissolve semantics read one
//    DissolveFade. That sharing is deliberate — 95 distinct pictures would be 95 pictures nobody can tell apart, whereas one glyph per FAMILY
//    plus the row label reads at a glance.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_POLYGONGLYPHIDENTITY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_POLYGONGLYPHIDENTITY_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One glyph identity per operation FAMILY rather than per operation. Ordered by the band that introduces it, so a reader
//    scanning the catalogue table sees glyph names rise roughly in step with the bands. Blank is 0 so a zero-initialised
//    descriptor draws nothing rather than drawing the first real glyph.
enum class PolygonGlyph : uint8_t
{
    Blank = 0,

    // Transform.
    TranslateArrows,        // [-] - Four-way arrow cross
    RotateArc,              // [-] - Arc with an arrowhead
    ScaleCorner,            // [-] - Box with a corner handle
    SlideTrack,             // [-] - Edge with a bead sliding along it
    FlattenPlane,           // [-] - Points collapsing onto a line
    AxisMarker,             // [-] - Three axis stubs from an origin
    CircleFit,              // [-] - Square dissolving into a circle
    SpanEven,               // [-] - Beads at equal intervals
    StraightenLine,         // [-] - Zigzag straightening into a line

    // Extrude / Build.
    ExtrudeOut,             // [-] - Face lifting off a base with an arrow
    ExtrudeSpike,           // [-] - Point lifting to a spike
    InsetRing,              // [-] - Square inside a square
    OutlineRing,            // [-] - Square outside a square
    BevelChamfer,           // [-] - Corner cut into a chamfer
    BridgeSpan,             // [-] - Two openings joined by a tube
    FillPatch,              // [-] - Closed outline with a hatch fill
    GridPatch,              // [-] - Outline filled with a quad grid
    ShellThickness,         // [-] - Surface doubled with a gap
    SweepPath,              // [-] - Profile swept along a curve

    // Cut / Split.
    LoopInsert,             // [-] - New line threading a quad strip
    LoopOffset,             // [-] - Line offset from an existing line
    SubdivideQuads,         // [-] - Quad split into four
    UnSubdivideMerge,       // [-] - Four quads merged into one
    ConnectPath,            // [-] - Two points joined by a new edge
    SeamSplit,              // [-] - Edge parting into two
    RipApart,               // [-] - Vertex torn into two
    PokeCentre,             // [-] - Face fanned from a centre point
    BisectPlane,            // [-] - Plane slicing a solid
    PartitionSolid,         // [-] - Two solids with an overlap marked

    // Merge / Weld.
    MergeInward,            // [-] - Points converging on a centre
    WeldTarget,             // [-] - One point dragged onto another
    CollapseDown,           // [-] - Region shrinking to a point
    CoplanarUnify,          // [-] - Two faces losing their shared edge

    // Subdivision.
    SmoothSurface,          // [-] - Faceted profile rounding
    RelaxNeighbour,         // [-] - Point pulled toward its neighbours
    SharpenInverse,         // [-] - Rounded profile creasing
    DecimateSparse,         // [-] - Dense grid thinning
    RemeshUniform,          // [-] - Irregular grid regularising

    // Normals / Shading.
    NormalArrow,            // [-] - Surface with an outward normal
    WindingReverse,         // [-] - Normal flipped through the surface
    ShadeSmoothCurve,       // [-] - Continuous shaded curve
    ShadeFacet,             // [-] - Discontinuous faceted curve
    SharpEdgeMark,          // [-] - Edge marked with a crease tick

    // Attributes.
    CreaseWeight,           // [-] - Edge with weight ticks
    UvSeamMark,             // [-] - Edge with a dashed seam
    MaterialSwatch,         // [-] - Filled swatch square

    // Topology repair.
    TriangulateSplit,       // [-] - Quad split by a diagonal
    QuadrangulatePair,      // [-] - Two triangles fused into a quad
    PlanarCorrect,          // [-] - Warped quad flattening
    SpinDiagonal,           // [-] - Diagonal rotating within a quad
    HoleSeal,               // [-] - Gap in a surface closing
    LooseReclaim,           // [-] - Stray points swept up
    NonManifoldRepair,      // [-] - Three faces on one edge, flagged
    HullWrap,               // [-] - Points wrapped by a convex outline

    // Duplicate / Symmetry.
    DuplicateOffset,        // [-] - Shape with an offset copy
    ShellSeparate,          // [-] - One shape parting into two
    ExtractSurface,         // [-] - Region lifted clear of a body
    DetachComponent,        // [-] - Component pulled free
    MirrorAxis,             // [-] - Shape reflected across a dashed axis
    SymmetryBalance,        // [-] - Two halves matched across an axis

    // Selection conversion.
    SelectionGrow,          // [-] - Region expanding outward
    SelectionShrink,        // [-] - Region contracting inward
    LoopHighlight,          // [-] - A loop traced around a tube
    RingHighlight,          // [-] - A ring traced across a tube
    StratumPromote,         // [-] - Point becoming an edge becoming a face
    LinkedShell,            // [-] - A whole connected body highlighted
    SimilarTrait,           // [-] - Matching shapes paired
    BoundaryTrace,          // [-] - Open border traced
    CheckerSkip,            // [-] - Alternating filled squares
    ShortestPath,           // [-] - Path threading between two points
    TraitFilter,            // [-] - Funnel over a mixed set

    // Removal.
    RemoveCross,            // [-] - Cross over a component
    DissolveFade,           // [-] - Edge fading, faces merging
    DissolveAngle,          // [-] - Edge with a shallow angle arc

    // 📝 Creation primitives. Unlike every glyph above — which pictures an OPERATION on existing topology — these picture the thing
    //    that gets made, so they are drawn as recognisable solids rather than as before/after diagrams. They serve the empty
    //    selection, where nothing can be modified and creation is the only offer.
    PrimitiveBox,           // [-] - Cube in perspective
    PrimitivePlane,         // [-] - Flat quad in perspective
    PrimitiveSphere,        // [-] - Circle with a latitude ellipse
    PrimitiveCylinder,      // [-] - Tube with elliptical caps
    PrimitiveCone,          // [-] - Triangle on an elliptical base
    PrimitiveTorus,         // [-] - Two concentric ellipses
    PrimitiveCircle,        // [-] - Single flat ring
    PrimitiveVertex,        // [-] - One lone point
    PrimitiveGrid,          // [-] - Subdivided flat quad
    PrimitiveText,          // [-] - Glyph outline on a baseline
    CurveLine,              // [-] - Open polyline with handles
    CurveBezier,            // [-] - Curve with two control handles
    CurveCircle,            // [-] - Closed curve ring
    CurveSpiral,            // [-] - Coiled open curve
    LightPoint,             // [-] - Bulb with radiating stubs
    LightDirectional,       // [-] - Parallel rays at an angle
    LightSpot,              // [-] - Cone of light from a head
    LightArea,              // [-] - Rectangle casting downward rays
    CameraBody,             // [-] - Camera housing with a lens
    ReferenceImage,         // [-] - Framed picture with a corner fold
    EmptyPivot,             // [-] - Three axis stubs, no geometry

    GlyphCount
};

}   // namespace Frontier

#endif
