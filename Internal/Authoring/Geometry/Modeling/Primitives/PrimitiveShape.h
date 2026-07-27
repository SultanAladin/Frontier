/*============================================================================================================================================
                                                              PRIMITIVESHAPE.H
============================================================================================================================================*/
// ðŸ§© Procedural primitive generator: twenty parametrized base shapes, each constructing the engine's single authoring container
//    (PolygonCluster â€” VertexField positions in double cm + a flat FaceVertexIndices / FaceVertexCounts face list that keeps
//    triangles, quads, and N-gons verbatim). A generated cluster flows through the SAME RegisterAuthoredObject chain an
//    imported polygon surface does, so a primitive is born identical + equally editable. This is the module PolygonCluster.h
//    anticipates ("CAD tessellation and primitive generators must all construct THIS type"); it depends only on the geometry
//    container + the Math library â€” no Vulkan, no Interface. Round shapes share the cap-style + loop-count parameter pattern
//    (see CapCategory).

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_MODELING_PRIMITIVES_PRIMITIVESHAPE_H
#define FRONTIER_AUTHORING_GEOMETRY_MODELING_PRIMITIVES_PRIMITIVESHAPE_H

#include "PolygonCluster.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        SHAPE + CAP CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The twenty base primitives. PrimitiveShapeCount stays last so a caller can iterate the whole set (the force-test
//    cycles it). Numbered explicitly so a persisted scene / preset references a stable ordinal (a new shape appends, never
//    inserts). "Category" suffix per the naming skill (Kind is banned).
enum PrimitiveShapeCategory
{
    PrimitiveShapeBox          = 0,   // [-] - axis-aligned box, per-axis segments
    PrimitiveShapePlane        = 1,   // [-] - flat XZ grid, per-axis subdivisions
    PrimitiveShapeSphereUv     = 2,   // [-] - latitude/longitude sphere (ring bands + tri poles)
    PrimitiveShapeSphereIco    = 3,   // [-] - icosahedron subdivided + normalized
    PrimitiveShapeCylinder     = 4,   // [-] - radial wall + two caps
    PrimitiveShapeCone         = 5,   // [-] - radial sidewall to an apex + one cap
    PrimitiveShapeCapsule      = 6,   // [-] - cylinder wall + two hemispheres
    PrimitiveShapeTorus        = 7,   // [-] - swept minor circle around a major circle
    PrimitiveShapeTube         = 8,   // [-] - hollow cylinder (inner + outer wall + ring end caps)
    PrimitiveShapeDisc         = 9,   // [-] - flat filled circle
    PrimitiveShapePyramid      = 10,  // [-] - rectangular base + four apex triangles
    PrimitiveShapePrism        = 11,  // [-] - n-gon base extruded + two N-gon ends
    PrimitiveShapeWedge        = 12,  // [-] - right-triangular prism
    PrimitiveShapeTetrahedron  = 13,  // [-] - 4-face Platonic solid
    PrimitiveShapeOctahedron   = 14,  // [-] - 8-face Platonic solid
    PrimitiveShapeDodecahedron = 15,  // [-] - 12 pentagon-face Platonic solid
    PrimitiveShapeIcosahedron  = 16,  // [-] - 20-face Platonic solid
    PrimitiveShapeArrow        = 17,  // [-] - cylinder shaft + cone head (gizmo body)
    PrimitiveShapeTorusKnot    = 18,  // [-] - (p,q) torus knot swept tube
    PrimitiveShapeHelix        = 19,  // [-] - coil / spring swept tube
    PrimitiveShapeCount        = 20   // [-] - iteration bound (keep last)
};

// ðŸ“ How a round primitive closes an open circular end. Shared by Cylinder / Cone / Tube / Disc so the cylinder example
//    (cap / no-cap / triangle-fan cap / square-grid cap) reads the same on every shape that has an end to close.
enum CapCategory
{
    CapNone        = 0,   // [-] - leave the end open (no fill face)
    CapTriangleFan = 1,   // [-] - a single-vertex fan from the ring centre (CapLoopCount ignored)
    CapSquareGrid  = 2    // [-] - concentric quad rings toward the centre (CapLoopCount concentric bands)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PER-SHAPE PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ One parameter block per shape. Defaults describe a ~200 cm well-formed unit so ConstructPrimitiveDefault yields a
//    sensible object with no tuning. Extents are half-extents / radii in authoring cm; loop counts are the segment / ring
//    subdivision the surface is built from (never below the minimum a closed shell needs).

struct BoxParameters
{
    double   HalfExtentX  = 100.0;   // [cm] - half-width  along X
    double   HalfExtentY  = 100.0;   // [cm] - half-height along Y
    double   HalfExtentZ  = 100.0;   // [cm] - half-depth  along Z
    uint32_t SegmentX     = 1;       // [-]  - quad columns along X (>= 1)
    uint32_t SegmentY     = 1;       // [-]  - quad columns along Y (>= 1)
    uint32_t SegmentZ     = 1;       // [-]  - quad columns along Z (>= 1)
};

struct PlaneParameters
{
    double   HalfExtentX   = 100.0;   // [cm] - half-width along X
    double   HalfExtentZ   = 100.0;   // [cm] - half-depth along Z
    uint32_t SubdivisionX  = 1;       // [-]  - quad columns along X (>= 1)
    uint32_t SubdivisionZ  = 1;       // [-]  - quad columns along Z (>= 1)
};

struct SphereUvParameters
{
    double   Radius        = 100.0;   // [cm] - sphere radius
    uint32_t SegmentCount  = 32;      // [-]  - longitude divisions (>= 3)
    uint32_t RingCount     = 16;      // [-]  - latitude bands including the two pole caps (>= 2)
};

struct SphereIcoParameters
{
    double   Radius            = 100.0;   // [cm] - sphere radius
    uint32_t SubdivisionLevel  = 2;       // [-]  - recursive splits of the base icosahedron (0..5 sane)
};

struct CylinderParameters
{
    double      Radius          = 100.0;       // [cm] - cross-section radius
    double      Height          = 200.0;       // [cm] - full height along Y
    uint32_t    RadialLoopCount = 32;          // [-]  - radial segments around the wall (>= 3)
    uint32_t    HeightLoopCount = 1;           // [-]  - wall quad rows up the height (>= 1)
    uint32_t    CapLoopCount    = 1;           // [-]  - concentric cap bands when Cap == CapSquareGrid (>= 1)
    CapCategory Cap             = CapTriangleFan;
};

struct ConeParameters
{
    double      BaseRadius      = 100.0;       // [cm] - base cross-section radius
    double      Height          = 200.0;       // [cm] - apex height along Y
    uint32_t    RadialLoopCount = 32;          // [-]  - radial segments around the base (>= 3)
    uint32_t    CapLoopCount    = 1;           // [-]  - concentric base bands when Cap == CapSquareGrid (>= 1)
    CapCategory Cap             = CapTriangleFan;
};

struct CapsuleParameters
{
    double   Radius              = 60.0;    // [cm] - cross-section + hemisphere radius
    double   CylinderHeight      = 120.0;   // [cm] - straight mid-section height along Y
    uint32_t SegmentCount        = 24;      // [-]  - longitude divisions (>= 3)
    uint32_t HemisphereRingCount = 6;       // [-]  - latitude bands per hemisphere (>= 1)
};

struct TorusParameters
{
    double   MajorRadius        = 100.0;   // [cm] - centre-of-tube ring radius
    double   MinorRadius        = 35.0;    // [cm] - tube cross-section radius
    uint32_t MajorSegmentCount  = 48;      // [-]  - segments around the major ring (>= 3)
    uint32_t MinorSegmentCount  = 18;      // [-]  - segments around the tube (>= 3)
};

struct TubeParameters
{
    double      InnerRadius     = 60.0;    // [cm] - hollow inner radius
    double      OuterRadius     = 100.0;   // [cm] - solid outer radius (> InnerRadius)
    double      Height          = 200.0;   // [cm] - full height along Y
    uint32_t    RadialLoopCount = 32;      // [-]  - radial segments (>= 3)
    uint32_t    HeightLoopCount = 1;       // [-]  - wall quad rows up the height (>= 1)
};

struct DiscParameters
{
    double      Radius          = 100.0;       // [cm] - disc radius
    uint32_t    RadialLoopCount = 32;          // [-]  - radial segments (>= 3)
    uint32_t    CapLoopCount    = 1;           // [-]  - concentric bands when Cap == CapSquareGrid (>= 1)
    CapCategory Cap             = CapTriangleFan;
};

struct PyramidParameters
{
    double HalfBaseX = 100.0;   // [cm] - half base extent along X
    double HalfBaseZ = 100.0;   // [cm] - half base extent along Z
    double Height    = 200.0;   // [cm] - apex height along Y
};

struct PrismParameters
{
    double   Radius     = 100.0;   // [cm] - n-gon circumradius
    double   Height     = 200.0;   // [cm] - full height along Y
    uint32_t SideCount  = 6;       // [-]  - polygon sides (>= 3)
};

struct WedgeParameters
{
    double HalfExtentX = 100.0;   // [cm] - half-width  along X
    double HeightY     = 200.0;   // [cm] - full height along Y (the ramped axis)
    double HalfExtentZ = 100.0;   // [cm] - half-depth  along Z
};

struct PlatonicParameters
{
    double Radius = 100.0;   // [cm] - circumradius (shared by tetra / octa / dodeca / icosa)
};

struct ArrowParameters
{
    double   ShaftRadius     = 30.0;    // [cm] - cylinder shaft radius
    double   ShaftLength     = 150.0;   // [cm] - shaft height along Y
    double   HeadRadius       = 60.0;   // [cm] - cone head base radius
    double   HeadLength       = 80.0;   // [cm] - cone head height along Y
    uint32_t RadialLoopCount  = 24;     // [-]  - radial segments (>= 3)
};

struct TorusKnotParameters
{
    double   MajorRadius       = 100.0;   // [cm] - knot curve scale
    double   TubeRadius        = 20.0;    // [cm] - swept tube radius
    uint32_t WindP             = 2;       // [-]  - longitudinal winds (>= 1)
    uint32_t WindQ             = 3;       // [-]  - meridional winds (>= 1)
    uint32_t CurveSampleCount  = 128;     // [-]  - samples along the knot curve (>= 3)
    uint32_t TubeSegmentCount  = 12;      // [-]  - segments around the tube (>= 3)
};

struct HelixParameters
{
    double   CoilRadius        = 80.0;    // [cm] - coil centreline radius
    double   TubeRadius        = 15.0;    // [cm] - swept tube radius
    double   Pitch             = 60.0;    // [cm] - vertical rise per full turn
    uint32_t TurnCount         = 4;       // [-]  - full turns (>= 1)
    uint32_t CurveSampleCount  = 128;     // [-]  - samples along the coil per model (>= 3)
    uint32_t TubeSegmentCount  = 10;      // [-]  - segments around the tube (>= 3)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Per-shape constructors. Each returns a fully-formed PolygonCluster (positions in authoring cm, CCW-outward winding,
//    quads where a face is naturally four-sided) with its Descriptor already refreshed. A degenerate parameter (a count
//    below the minimum a closed shell needs) is clamped up, never rejected â€” a primitive always yields a valid body.
PolygonCluster ConstructBoxPrimitive(const BoxParameters& Parameters);
PolygonCluster ConstructPlanePrimitive(const PlaneParameters& Parameters);
PolygonCluster ConstructSphereUvPrimitive(const SphereUvParameters& Parameters);
PolygonCluster ConstructSphereIcoPrimitive(const SphereIcoParameters& Parameters);
PolygonCluster ConstructCylinderPrimitive(const CylinderParameters& Parameters);
PolygonCluster ConstructConePrimitive(const ConeParameters& Parameters);
PolygonCluster ConstructCapsulePrimitive(const CapsuleParameters& Parameters);
PolygonCluster ConstructTorusPrimitive(const TorusParameters& Parameters);
PolygonCluster ConstructTubePrimitive(const TubeParameters& Parameters);
PolygonCluster ConstructDiscPrimitive(const DiscParameters& Parameters);
PolygonCluster ConstructPyramidPrimitive(const PyramidParameters& Parameters);
PolygonCluster ConstructPrismPrimitive(const PrismParameters& Parameters);
PolygonCluster ConstructWedgePrimitive(const WedgeParameters& Parameters);
PolygonCluster ConstructTetrahedronPrimitive(const PlatonicParameters& Parameters);
PolygonCluster ConstructOctahedronPrimitive(const PlatonicParameters& Parameters);
PolygonCluster ConstructDodecahedronPrimitive(const PlatonicParameters& Parameters);
PolygonCluster ConstructIcosahedronPrimitive(const PlatonicParameters& Parameters);
PolygonCluster ConstructArrowPrimitive(const ArrowParameters& Parameters);
PolygonCluster ConstructTorusKnotPrimitive(const TorusKnotParameters& Parameters);
PolygonCluster ConstructHelixPrimitive(const HelixParameters& Parameters);

// Construct a primitive of the given category with its default parameters. The single-argument entry a caller that only
// wants "a box" (or that cycles the whole set) uses; the explicit-parameter constructors above are for authored tuning.
// An out-of-range category returns an empty cluster (VertexCount 0).
PolygonCluster ConstructPrimitiveDefault(PrimitiveShapeCategory Category);

// Human-readable name for a category (stable, ASCII, no spaces) â€” used for object titles + logging. Returns "unknown"
// for an out-of-range category.
const char* ResolvePrimitiveShapeName(PrimitiveShapeCategory Category);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_MODELING_PRIMITIVES_PRIMITIVESHAPE_H
