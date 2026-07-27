/*==============================================================================================================================================
                                                              DRAUGHTSHAPESTORE.H
==============================================================================================================================================*/
// 🧩 The draughting workspace's analytic 2D shape model + per-view store: the source of truth the plane view draws + picks, the CAD
//    outliner lists, and the CAD Properties / History panels read. Shapes hold their DEFINITION in world mm (a Line is two points, a
//    Polyline is N) — never a flattened pixel list; the view maps them to screen each frame, and picking runs point-to-segment on the
//    analytic form. The 3D CAD surfaces stay GPU-tessellated; this is the CPU analytic sketch layer that will feed them. Lives under the
//    Authoring pillar (Authoring/Modeling/Draughting) — the sketch layer's home beside Geometry / Picking / Selection / UV.

// 💡 Alternative spelling under consideration: DraftShape… (American). Swap the identifiers here + in the .cpp if adopted.

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_DRAUGHTING_DRAUGHTSHAPESTORE_H
#define FRONTIER_AUTHORING_MODELING_DRAUGHTING_DRAUGHTSHAPESTORE_H

#include "imgui.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The analytic shape a store entry carries. Every category interprets its DEFINING clicks (the user's placement points) into a
//    render definition + analytic scalars, then flattens to a polyline for drawing / picking / length. Named .Category per the skills.
//    Straight families keep their points verbatim; the round + curve families solve scalars (radius / axes / degree) at construction.
enum class DraughtShapeCategory
{
    Line      = 0,   // [-] - a single straight segment (start, end)
    Polyline  = 1,   // [-] - a connected run of straight segments (N points, N >= 2)
    Arc       = 2,   // [-] - a circular arc through three clicks (start, end, through)
    Bezier    = 3,   // [-] - a Bezier curve over its control points (de Casteljau)
    BSpline   = 4,   // [-] - a clamped B-spline over its control points (Cox-de Boor)
    Nurbs     = 5,   // [-] - a rational B-spline (unit weights this phase; distinct entity from BSpline)
    Spline    = 6,   // [-] - a Catmull-Rom spline interpolating its control points
    Conic     = 7,   // [-] - a rational-quadratic conic (start, shoulder, end) weighted by Rho
    Rectangle = 8,   // [-] - an axis-aligned rectangle from two opposite corners (closed loop)
    Circle    = 9,   // [-] - a full circle (centre, radius handle)
    Ellipse   = 10,  // [-] - a rotated ellipse (centre, major end, minor extent; closed loop)
    Polygon   = 11,  // [-] - a regular N-gon (centre, vertex handle; closed loop)
    Slot      = 12,  // [-] - a stadium / obround (two endpoints A,B + a half-width radius; closed loop)
    Profile   = 13,  // [-] - a boolean result / explicit closed loop: Points ARE the outer loop verbatim; HoleLoops punch it
};

// 📝 A snap target category the draw path can catch the cursor to. Grid is the lattice snap the view already had; the point-family
//    targets scan the displayed shapes' analytic forms. Intersection is a named seam only this pass (a dedicated tool computes it
//    later — ResolveSnapCandidate never returns it). Named .Category per the skills; a SnapTargetMask on the view enables each one.
enum class DraughtSnapCategory
{
    Grid         = 0,   // [-] - the minor-cell lattice snap (the view's existing grid snap)
    Endpoint     = 1,   // [-] - a shape's defining point / vertex
    Midpoint     = 2,   // [-] - the midpoint of a flattened outline segment
    Center       = 3,   // [-] - a round family's solved Centre
    AlongCurve   = 4,   // [-] - the nearest point on a shape's outline
    Intersection = 5,   // [-] - 💡 deferred — a dedicated tool computes outline intersections later
};

// 📝 A geometric constraint the solver enforces between shapes' shared points / edges (faithful to p4.html's constraint set). The
//    solver relaxes the sketch so every constraint holds: Horizontal / Vertical flatten one edge to its midpoint axis; Parallel /
//    Perpendicular align one edge's angle to another's; Equal matches two edge lengths; Coincident is satisfied by welding two points
//    into one pooled point; Fixed pins a point in place; Tangent aligns an edge to a circle (best-effort). Named .Category per the skills.
enum class DraughtConstraintCategory
{
    Coincident    = 0,   // [-] - two points share one location (welded in the pool)
    Horizontal    = 1,   // [-] - an edge lies on a horizontal (constant Z) line
    Vertical      = 2,   // [-] - an edge lies on a vertical (constant X) line
    Parallel      = 3,   // [-] - two edges share a direction
    Perpendicular = 4,   // [-] - two edges meet at a right angle
    Tangent       = 5,   // [-] - an edge is tangent to a round shape (best-effort align)
    Equal         = 6,   // [-] - two edges share a length
    Fixed         = 7,   // [-] - a point is pinned (never relaxed)
};

// 📝 A driving dimension: editing its TargetValue re-solves the sketch to satisfy it (faithful to p4.html's dimensions). Horizontal /
//    Vertical / Aligned drive an edge's component / distance; Radius / Diameter write a round shape's analytic scalar directly (never by
//    moving points); Angular drives the angle between two edges. Named .Category per the skills; a …Category enum suffix, not Kind.
enum class DraughtDimensionCategory
{
    Horizontal = 0,   // [-] - the horizontal (X) span between two points (mm)
    Vertical   = 1,   // [-] - the vertical (Z) span between two points (mm)
    Aligned    = 2,   // [-] - the straight-line distance between two points (mm)
    Radius     = 3,   // [-] - a round shape's radius (mm) — written to the analytic scalar
    Diameter   = 4,   // [-] - a round shape's diameter (mm) — written to the analytic scalar
    Angular    = 5,   // [-] - the angle between two edges (degrees)
};

// 📝 Which dashed reference axes a datum draws through its anchor. A datum is a world-anchored REFERENCE point (the Mirror / Array
//    tools reference its active axis), never geometry — so this is a display + intent cue only, never fed to a shape walk / solver.
//    Named .AxisDisplay per the skills (a …Category-style enum suffix would misread — this is an axis selection, not a shape family).
enum class DraughtDatumAxis
{
    None       = 0,   // [-] - just the point (no axis lines)
    Horizontal = 1,   // [-] - a horizontal dashed line through the anchor (constant Z)
    Vertical   = 2,   // [-] - a vertical dashed line through the anchor (constant X)
    Cross      = 3,   // [-] - both dashed lines (a full cross through the anchor)
};

// 📝 How a loft interpolates its surface ACROSS the sections (the "v" direction, section-to-section). Ruled runs straight lines between
//    adjacent sections (a developable, unrollable band — the sheet-metal case); Smooth fits a curve through the section rings so the body
//    bulges / waists continuously (the organic case). Per the skills verb list Loft is an approved operation; .Category enum suffix.
enum class LoftTransitionCategory
{
    Ruled  = 0,   // [-] - straight lines between adjacent sections (developable band, linear in v)
    Smooth = 1,   // [-] - a fitted curve through the section rings (continuous bulge, spline in v)
};

// 📝 How the lofted surface LEAVES an end section — a per-end continuity target. Normal exits with no imposed slope (G0 position match
//    only); Tangent holds the surface tangent (G1) at the end, weighted by a magnitude; Curvature matches curvature (G2) for a class-A
//    blend. The core tier honours Normal; Tangent / Curvature are the P4 advanced tier. .Category enum suffix per the skills.
enum class LoftEndCategory
{
    Normal    = 0,   // [-] - position match only (G0), no imposed end slope
    Tangent   = 1,   // [-] - hold the end tangent (G1), scaled by the end weight
    Curvature = 2,   // [-] - match end curvature (G2) for a smooth class-A blend
};

// 📝 How section points are PAIRED across sections so the surface does not twist (the "candy-wrapper" seam). Automatic rotates each
//    section's start index to minimise the total inter-section travel (the anti-twist search); Connectors honours an explicit list of
//    vertex pairs the user pinned. The core tier ships Automatic; Connectors is the P4 tier. .Category enum suffix per the skills.
enum class LoftCorrelationCategory
{
    Automatic  = 0,   // [-] - rotate each section's start to minimise inter-section travel (anti-twist search)
    Connectors = 1,   // [-] - honour explicit user-pinned vertex pairs
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One PARAMETRIC corner edit carried on a shape (the Plasticity `B` result stored analytically, NOT baked into Points). CornerIndex
//    addresses the defining vertex the fillet / chamfer rounds; Magnitude is the fillet radius / chamfer setback in mm; ChamferEnabled
//    picks a straight setback edge over a tangent arc. The corner stays one real vertex in Points (so it reads as a single grabbable
//    corner + one clean curve, never a run of sample dots); the flatten path solves the arc + tessellates it for display only, and the
//    Properties panel edits Magnitude to re-shape the corner later. Empty CornerFillets = a plain unfilleted shape.
struct DraughtCornerFillet
{
    int   CornerIndex     = -1;      // [-]  - the defining-vertex index this edit rounds (into the host shape's Points)
    float Magnitude       = 0.0f;    // [mm] - the fillet radius (arc) / chamfer setback distance
    bool  ChamferEnabled  = false;   // [-]  - true = straight setback edge (chamfer); false = tangent arc (fillet)
};

// 📝 One analytic shape — its definition in WORLD MM, plus display cues. Points holds the DEFINING points: for the straight + curve
//    families these are the vertices / control points verbatim (Line 2, Polyline / Bezier / … N); for the round families (Arc, Circle,
//    Ellipse, Polygon) they are the placement clicks, and the solved scalars below carry the true geometry. The view flattens any
//    category to a polyline every frame (nothing stores pixels); picking + length run on that flattening. ClosedEnabled joins the last
//    sample back to the first (Rectangle / Ellipse / Polygon loops). FillEnabled paints a semi-transparent fill inside a closed shape.
//    Displayed is the outliner eye; TintIndex a swatch; Title the label.
struct DraughtShape
{
    uint32_t             Identifier   = 0;                            // [-]  - unique within the owning store
    DraughtShapeCategory Category     = DraughtShapeCategory::Line;   // [-]  - which analytic form the points describe
    std::vector<ImVec2>  Points;                                      // [mm] - the defining points / control points in world mm
    float                Elevation    = 0.0f;                         // [mm] - height off the sketch plane (Z); 2D pick / boolean ignore it, the 3D view lifts the shape by it (G Z)
    std::vector<std::vector<ImVec2>> HoleLoops;                       // [mm] - inner hole loops (each closed, CW); empty for every non-Profile shape
    std::vector<DraughtCornerFillet> CornerFillets;                   // [-]  - parametric corner fillets / chamfers (solved + tessellated at flatten; empty = none)
    bool                 ClosedEnabled = false;                       // [-]  - loop (last sample rejoins the first)
    bool                 FillEnabled  = true;                         // [-]  - paint a semi-transparent white wash (only when ClosedEnabled); the default face look
    bool                 MatcapFillEnabled = false;                   // [-]  - promote the face to a chrome MATCAP SOLID on the GPU scene pass (F tool); overrides the flat wash (only when ClosedEnabled)
    float                ExtrudeDepth = 0.0f;                         // [mm] - height the closed face sweeps along +Z into a solid prism (E tool); 0 = a flat single-cap facet. Implies MatcapFillEnabled (the prism renders on the GPU scene pass); the side walls' lateral normals give the matcap its solid read
    bool                 Displayed    = true;                         // [-]  - outliner visibility toggle (the eye affordance)
    bool                 LockEnabled  = false;                        // [-]  - locked: excluded from pick + solver relaxation (red-dashed), toggled from the context menu
    uint32_t             TintIndex    = 0;                            // [-]  - display swatch index (a cue only; never geometry)
    char                 Title[48]    = {};                           // [-]  - display title ("Line 1", "Circle 2", …)

    // 📝 Free-text authoring notes carried on the shape (metadata, never geometry). Edited only in the Properties panel; persisted with the
    //    shape and carried through whole-struct copy / restore. A fixed buffer keeps the struct plain-old-data (trivial copy for the history
    //    snapshot path), matching Title. Empty = no note. The outliner may hint a note glyph off IsNotePresent(*Shape) later.
    char                 Notes[512]   = {};                           // [-]  - user annotation text (empty = none)

    uint32_t             FolderIdentifier = 0;                        // [-]  - owning user folder (0 = none → falls into its auto-category)

    // 📝 Per-shape reference DATUM — a Mirror / Array origin the shape carries as a property (enabled from the Properties panel, not a
    //    standalone entity). Seeded at the shape centre on enable, user-draggable, and drawn (with its red/green dashed axes) ONLY while the
    //    shape is selected. Plain-old-data so it rides the whole-struct history snapshot with no extra wiring. DatumAxis picks which axes the
    //    Mirror reflects across (Horizontal / Vertical / Cross → a 4-up); DatumRotation orients those axes about DatumAnchor.
    bool                 DatumEnabled  = false;                       // [-]  - the shape carries a reference datum (Mirror origin), shown only while selected
    ImVec2               DatumAnchor   = ImVec2(0, 0);                // [mm] - datum position in world mm (seeded at the shape centre on enable; user-draggable)
    float                DatumRotation = 0.0f;                        // [rad] - datum axis orientation about the anchor (0 = world-aligned)
    DraughtDatumAxis     DatumAxis     = DraughtDatumAxis::Cross;     // [-]  - which reference axes the datum draws / mirrors across

    // 📝 Live (parametric) mirror link. A shape with MirrorSource != 0 is a DRIVEN mirror child: it owns no independent geometry — every
    //    edit (AppendDraughtEditDetailed → ReflowMirrorChildren) recomputes its Points / HoleLoops / solved scalars by reflecting the SOURCE
    //    shape across the source datum's axis, so the copy follows the source AND the datum in real time. MirrorAxisIndex names which datum
    //    axis produced this copy (0 = reflected across the horizontal axis line, 1 = across the vertical), so a Cross (two-copy) mirror
    //    re-solves each copy against the right axis. The child is not directly editable (treated like LockEnabled in the pick / gizmo gates).
    uint32_t             MirrorSource    = 0;                         // [-]  - source shape id driving this copy (0 = an ordinary, non-driven shape)
    int                  MirrorAxisIndex = 0;                         // [-]  - which datum axis reflected this copy (0 = horizontal line, 1 = vertical line)

    // 📝 Live (parametric) ARRAY link. A shape with ArraySource != 0 is a DRIVEN array copy: it owns no independent geometry — every edit
    //    (AppendDraughtEditDetailed → ReflowArrayChildren) recomputes its Points / HoleLoops by stamping the SOURCE shape at this copy's slot,
    //    so the run follows the source AND the datum in real time. ArrayMode mirrors the source mode (0 linear / 1 radial / 2 instance-on-points);
    //    ArrayInstance is this copy's ordinal in the run (1..N-1; the original is ordinal 0 and never a child). Frozen like a mirror child.
    uint32_t             ArraySource   = 0;                           // [-]  - source shape id driving this copy (0 = not an array child)
    int                  ArrayMode     = 0;                           // [-]  - 0 linear / 1 radial / 2 instance-on-points (matches the source)
    int                  ArrayInstance = 0;                           // [-]  - this copy's ordinal in the run (1..N-1)

    // 📝 ARRAY parameters carried on a SOURCE shape (the array it drives). ArrayEnabled turns the run on; ArrayModeChoice picks the mode;
    //    ArrayCount is the total instance count INCLUDING the original (>= 2 emits copies). Linear steps each copy by ArrayLinearStep (dx/dy in
    //    mm, rotated through the datum angle so a tilted datum tilts the run). Radial sweeps ArrayRadialSweep radians about the datum anchor.
    //    Instance-on-points drops one copy per defining vertex of ArrayHostShape. Plain-old-data → rides the whole-struct history snapshot.
    int                  ArrayEnabled     = 0;                        // [-]  - 0 off / 1 on: this shape drives an array of copies
    int                  ArrayModeChoice  = 0;                        // [-]  - 0 linear / 1 radial / 2 instance-on-points
    int                  ArrayCount       = 3;                        // [-]  - total instances incl. the original (>= 2 emits copies)
    ImVec2               ArrayLinearStep  = ImVec2(50, 0);            // [mm] - per-copy dx/dy for linear mode (datum-rotated)
    float                ArrayRadialSweep = 6.2831853f;               // [rad]- total sweep angle for radial mode (default full turn)
    uint32_t             ArrayHostShape   = 0;                        // [-]  - instance-on-points host id (one copy per host defining vertex)

    // Solved analytic scalars — populated at construction by the round + curve families; ignored by the straight families.
    ImVec2 Centre       = ImVec2(0, 0);   // [mm]  - centre (Arc / Circle / Ellipse / Polygon)
    float  Radius       = 0.0f;           // [mm]  - radius (Arc / Circle) or major axis re-used by Polygon as circum-radius
    float  StartAngle   = 0.0f;           // [rad] - arc start angle (Arc)
    float  SweepAngle   = 0.0f;           // [rad] - arc signed sweep (Arc)
    float  MajorAxis    = 0.0f;           // [mm]  - ellipse major semi-axis
    float  MinorAxis    = 0.0f;           // [mm]  - ellipse minor semi-axis
    float  Rotation     = 0.0f;           // [rad] - ellipse major-axis rotation
    float  Rho          = 0.5f;           // [-]   - conic shoulder weight ratio (0.5 = parabola)
    int    Degree       = 3;              // [-]   - B-spline / NURBS degree (clamped to point count - 1)
    int    SideCount    = 6;              // [-]   - polygon side count (>= 3)

    // Flatten cache — the display polyline (world mm) + its axis-aligned boundary, memoised so the hot snap / pick / render paths
    // flatten each shape once per CHANGE rather than 3-4x every frame. CachedOutlineValid is false on any fresh or copied struct
    // (every geometry edit rebuilds the shape via ConstructDraughtShape, whole-struct replace → the cache resets to invalid by
    // default), so a mutation self-invalidates with no manual stamp. CachedSampleBudget records the budget the outline was flattened
    // at; a different budget forces a rebuild. These are a memo of the analytic definition above — never geometry, never persisted.
    std::vector<ImVec2> CachedOutline;                          // [mm] - last flattened display polyline (empty until first build)
    ImVec2              CachedBoundaryMinimum = ImVec2(0, 0);    // [mm] - AABB minimum corner of CachedOutline
    ImVec2              CachedBoundaryMaximum = ImVec2(0, 0);    // [mm] - AABB maximum corner of CachedOutline
    int                 CachedSampleBudget    = -1;             // [-]  - the sample budget CachedOutline was built at (-1 = none yet)
    bool                CachedOutlineValid    = false;          // [-]  - CachedOutline reflects the current definition + budget
};

// 📝 Whether a shape is FROZEN against direct geometry edits — a user-locked shape (LockEnabled) OR a driven mirror child (MirrorSource != 0,
//    whose geometry is recomputed from its source every edit, so a manual move would be overwritten). Frozen shapes stay pickable / selectable
//    (so they can be inspected / deleted) but the point-drag, gizmo, sub-entity, and modal edit gates skip them. Pure read of two fields.
inline bool DraughtShapeFrozen(const DraughtShape& Shape)
{
    return Shape.LockEnabled || Shape.MirrorSource != 0 || Shape.ArraySource != 0;
}

// 📝 One world-anchored INSCRIPTION on the sketch plane — free-text placed with the Inscribe tool. Metadata + presentation, never geometry:
//    it is not flattened, picked as an outline, or fed to the constraint solver / booleans, so it lives in its OWN list (never a shape walk).
//    Anchor is the world-mm placement point; the view projects it each frame and the glyph pass renders Text as a SCREEN-FIXED SDF billboard
//    (constant PixelHeight px, tinted TintIndex, aligned by Alignment about the anchor). Plain-old-data (fixed Text buffer) so the whole-struct
//    history snapshot path stays trivial, matching DraughtShape. Displayed is the outliner eye; LockEnabled excludes it from pick.
struct DraughtInscription
{
    uint32_t Identifier   = 0;              // [-]  - unique within the owning store (SEPARATE id space from shapes)
    ImVec2   Anchor       = ImVec2(0, 0);   // [mm] - world-plane anchor point
    char     Text[256]    = {};             // [-]  - the inscription glyphs (ASCII this pass)
    float    PixelHeight  = 18.0f;          // [px] - screen-fixed display height
    uint32_t TintIndex    = 0;              // [-]  - display swatch index (mirrors the shape tint ladder)
    int      Alignment    = 0;              // [-]  - 0 left / 1 centre / 2 right about the anchor
    bool     Displayed    = true;           // [-]  - outliner visibility toggle (the eye affordance)
    bool     LockEnabled  = false;          // [-]  - locked: excluded from pick
};

// 📝 One world-anchored DATUM — a reference point on the sketch plane placed with the Datum tool. Like an inscription it is metadata, never
//    geometry: it is not flattened, picked as an outline, or fed to the boolean / constraint solver, so it lives in its OWN list (never a shape
//    walk). Anchor is the world-mm placement point the view projects each frame; AxisDisplay picks which dashed reference axes it draws through
//    the anchor (the Mirror / Array tools reference the active axis). Plain-old-data (fixed Title buffer) so the whole-struct history snapshot
//    path stays trivial, matching DraughtShape / DraughtInscription. Displayed is the outliner eye; LockEnabled excludes it from pick.
struct DraughtDatum
{
    uint32_t         Identifier   = 0;                         // [-]  - unique within the owning store (SEPARATE id space from shapes / inscriptions)
    ImVec2           Anchor       = ImVec2(0, 0);              // [mm] - world-plane anchor point
    float            Rotation     = 0.0f;                      // [rad] - orientation of the reference axes about the anchor (0 = world-aligned; Mirror / Array read the rotated axis)
    DraughtDatumAxis AxisDisplay  = DraughtDatumAxis::Cross;   // [-]  - which dashed reference axes to draw through the anchor
    uint32_t         TintIndex    = 0;                         // [-]  - display swatch index (mirrors the shape tint ladder)
    char             Title[48]    = {};                        // [-]  - display title ("Datum 1", …)
    bool             Displayed    = true;                      // [-]  - outliner visibility toggle (the eye affordance)
    bool             LockEnabled  = false;                     // [-]  - locked: excluded from pick
    bool             PinnedEnabled = false;                    // [-]  - pinned: axis lines stay drawn even when unselected (else lines show only while selected)
};

// 📝 The full recipe for ONE loft — every catalog variant expressed as data, so a loft re-solves from this when a source profile edits
//    (ReflowLoftBodies). SectionProfiles are the ordered section shape ids (2+, or 1 + an apex expressed as a section whose flattened
//    outline is a single point); each section's own Elevation supplies its Z plane. GuideCurves reshape the in-between (P4); Centerline is
//    a spine the sections stay perpendicular to (P4). ClosedLoopEnabled wraps the last section back to the first (a ring). Transition
//    picks ruled vs smooth; StartCondition / EndCondition + their weights set the per-end continuity (P4); Correlation + Connectors set
//    the anti-twist pairing. Plain-old-data-ish (one small vector of pairs) so it rides the DraughtLoftBody snapshot cleanly.
struct LoftSpecification
{
    std::vector<uint32_t>   SectionProfiles;                                  // [-] - ordered section shape ids (2+, or 1 + apex section)
    std::vector<uint32_t>   GuideCurves;                                      // [-] - guide-rail shape ids reshaping the in-between (0+; P4)
    uint32_t                Centerline       = 0;                             // [-] - spine shape id the sections stay ⟂ to (0 = none; P4)
    bool                    ClosedLoopEnabled = false;                        // [-] - wrap the last section back to the first (periodic in v)
    LoftTransitionCategory  Transition       = LoftTransitionCategory::Smooth;// [-] - ruled (straight-v) vs smooth (spline-v) transition
    LoftEndCategory         StartCondition   = LoftEndCategory::Normal;       // [-] - continuity target at the first section (P4 for non-Normal)
    LoftEndCategory         EndCondition     = LoftEndCategory::Normal;       // [-] - continuity target at the last section (P4 for non-Normal)
    float                   StartWeight      = 1.0f;                          // [-] - tangent magnitude at the first section
    float                   EndWeight        = 1.0f;                          // [-] - tangent magnitude at the last section
    LoftCorrelationCategory Correlation      = LoftCorrelationCategory::Automatic; // [-] - anti-twist point pairing
    std::vector<std::pair<uint32_t, uint32_t>> Connectors;                    // [-] - explicit pinned vertex pairs (Connectors mode; P4)
};

// 📝 One lofted BODY — a 3D display surface interpolated through its LoftSpecification's sections. This is a TESSELLATED display surface
//    (positions + normals + triangle indices, world mm / cm), NOT an exact B-rep — matching the CAD analytic-operations approach. It lives
//    in its OWN list on the store (never a shape walk / boolean / solver sees it), keyed by a separate id space. The kernel fills the CPU
//    arrays; the GPU preview (P2) uploads them and leaves the buffer handle here, re-uploading only when TessellationRevision changes.
//    SolidEnabled records whether the sections were closed (a capped solid) or open (a double-sided sheet). Source + Recipe let
//    ReflowLoftBodies re-solve the body in place when a source profile edits, mirroring the mirror / array driven-child idiom.
struct DraughtLoftBody
{
    uint32_t              Identifier          = 0;       // [-]  - unique within the owning store (SEPARATE id space from shapes / datums)
    LoftSpecification     Recipe;                        // [-]  - the recipe this body re-solves from (holds the source section ids)
    std::vector<float>    Positions;                     // [mm] - interleaved-free vertex positions (x,y,z per vertex, 3 floats each)
    std::vector<float>    Normals;                       // [-]  - unit vertex normals (x,y,z per vertex, 3 floats each; parallels Positions)
    std::vector<uint32_t> Indices;                       // [-]  - triangle-list indices into the vertex arrays
    bool                  SolidEnabled        = false;   // [-]  - sections closed → capped solid; open → double-sided sheet
    uint32_t              TintIndex           = 0;       // [-]  - display swatch index (inherited from the base section)
    uint32_t              FolderIdentifier    = 0;       // [-]  - owning user folder (inherited from the base section)
    char                  Title[48]           = {};      // [-]  - display title ("Loft 1", …)
    bool                  Displayed           = true;    // [-]  - outliner visibility toggle (the eye affordance)
    uint32_t              TessellationRevision = 0;      // [-]  - bumped each re-solve so the GPU preview re-uploads only on change
};

// 📝 A stable reference into a shape's defining Points — the atom constraints + dimensions address. p4.html shares points by id; Frontier's
//    points are index-addressed with no identity, so a handle is (ShapeIdentifier, PointIndex). The solve-time point pool welds handles
//    whose positions coincide (Coincident / draw-time weld) into one moveable pooled point; the solver relaxes pooled points, then writes
//    them back. An edge between two points on the same or different shapes is two handles (see DraughtConstraint / DraughtDimension refs).
struct DraughtPointHandle
{
    uint32_t ShapeIdentifier = 0;    // [-] - the owning shape's Identifier (0 = unset)
    int      PointIndex      = 0;    // [-] - the index into that shape's Points
};

// 📝 One geometric constraint the solver enforces. Category picks the rule; the point handles reference the constrained atoms. Point-based
//    (Coincident / Fixed) use PrimaryA (+ PrimaryB for Coincident). Edge-based (Horizontal / Vertical) use one edge (PrimaryA..PrimaryB).
//    Two-edge (Parallel / Perpendicular / Equal / Tangent) use both edges (PrimaryA..PrimaryB and SecondaryA..SecondaryB). Identifier is
//    stable within the store so the Properties / render paths address it. "Constraint" is a domain term (allowed); .Category per the skills.
struct DraughtConstraint
{
    uint32_t                  Identifier = 0;                                  // [-] - unique within the owning store
    DraughtConstraintCategory Category   = DraughtConstraintCategory::Fixed;  // [-] - which rule the solver enforces
    DraughtPointHandle        PrimaryA;                                        // [-] - first edge start / the constrained point
    DraughtPointHandle        PrimaryB;                                        // [-] - first edge end (unused for point-only rules)
    DraughtPointHandle        SecondaryA;                                      // [-] - second edge start (two-edge rules only)
    DraughtPointHandle        SecondaryB;                                      // [-] - second edge end (two-edge rules only)
};

// 📝 One driving dimension. Category picks what it drives; TargetValue is the driven number (mm for lengths, degrees for Angular) the user
//    edits to re-solve the sketch. Point-span dimensions (Horizontal / Vertical / Aligned) drive PrimaryA..PrimaryB. Round dimensions
//    (Radius / Diameter) target ShapeIdentifier's analytic scalar (PrimaryA carries the shape id, PointIndex unused). Angular drives the
//    angle between the PrimaryA..PrimaryB edge and the SecondaryA..SecondaryB edge. Offset places the leader in world mm (glued on zoom).
struct DraughtDimension
{
    uint32_t                  Identifier  = 0;                                     // [-]  - unique within the owning store
    DraughtDimensionCategory  Category    = DraughtDimensionCategory::Aligned;     // [-]  - what the dimension drives
    float                     TargetValue = 0.0f;                                  // [mm/deg] - the driven number (mm, or degrees for Angular)
    DraughtPointHandle        PrimaryA;                                            // [-]  - first point / edge start / round shape id
    DraughtPointHandle        PrimaryB;                                            // [-]  - second point / edge end
    DraughtPointHandle        SecondaryA;                                          // [-]  - second edge start (Angular only)
    DraughtPointHandle        SecondaryB;                                          // [-]  - second edge end (Angular only)
    ImVec2                    Offset      = ImVec2(0, 0);                          // [mm] - leader offset in world mm (placement drag)
};

// 📝 One user-created outliner folder — a named group shapes may be moved into. The auto-categories (Profiles / Curves) are derived
//    from a shape's DraughtShapeCategory and need no record; a folder is the only thing the user authors, so it lives on the store.
//    CollapsedEnabled mirrors the outliner's per-folder fold in the shared store so a torn-off box keeps the same fold as its origin.
struct DraughtFolder
{
    uint32_t Identifier      = 0;       // [-] - unique within the owning store (matches DraughtShape::FolderIdentifier)
    char     Title[48]       = {};      // [-] - display title ("Folder 1", …)
    bool     CollapsedEnabled = false;  // [-] - the folder section is folded shut in the outliner
};

// 📝 A geometry SNAPSHOT — the whole authored state of a store AFTER one edit, captured so undo / redo / jump-to can restore any point
//    on the timeline in O(1) rather than replaying edits forward. Carries every persisted field (shapes, holes, corner fillets,
//    constraints, dimensions, folders, selection, and the monotonic id sources) but NOT the transient flatten caches (rebuilt lazily
//    on the next draw) nor the edit log itself (that would recurse). One snapshot lives on each DraughtHistoryEntry; restoring copies
//    it back over the store's live fields. Shape structs keep their CachedOutlineValid = false default so a restore self-invalidates.
struct DraughtRevision
{
    std::vector<DraughtShape>       Shapes;                    // [-] - the committed analytic shapes at this step (caches left invalid)
    std::vector<DraughtInscription> Inscriptions;              // [-] - world-anchored inscriptions at this step
    std::vector<DraughtDatum>       Datums;                    // [-] - world-anchored datums at this step
    std::vector<DraughtLoftBody>    LoftBodies;                // [-] - lofted 3D bodies at this step (tessellation rides the snapshot)
    std::vector<DraughtConstraint>  Constraints;               // [-] - geometric constraints at this step
    std::vector<DraughtDimension>   Dimensions;                // [-] - driving dimensions at this step
    std::vector<DraughtFolder>      Folders;                   // [-] - user folders at this step
    uint32_t                        NextIdentifier            = 1; // [-] - monotonic shape-id source at this step
    uint32_t                        NextInscriptionIdentifier = 1; // [-] - monotonic inscription-id source at this step
    uint32_t                        NextDatumIdentifier       = 1; // [-] - monotonic datum-id source at this step
    uint32_t                        NextLoftIdentifier        = 1; // [-] - monotonic loft-id source at this step
    uint32_t                        NextFolderIdentifier      = 1; // [-] - monotonic folder-id source at this step
    uint32_t                        NextConstraintIdentifier  = 1; // [-] - monotonic constraint-id source at this step
    uint32_t                        NextDimensionIdentifier   = 1; // [-] - monotonic dimension-id source at this step
    uint32_t                        Selected                  = 0; // [-] - selected shape id at this step
    uint32_t                        SelectedInscription       = 0; // [-] - selected inscription id at this step
    uint32_t                        SelectedDatum             = 0; // [-] - selected datum id at this step
    uint32_t                        SelectedLoft              = 0; // [-] - selected loft id at this step
};

// 📝 One entry in the store's edit log — the History panel's row source. Label is the reader line ("Added Line 1"); ShapeId ties the
//    row back to the shape it recorded; FlashLevel is the eased 1→0 fade the History panel decays each frame for a new-row pulse; and
//    Revision is the geometry snapshot AFTER this edit — the state undo / redo / jump restore. The row's "category" for the timeline
//    reads off Glyph; Detail supplies the prototype's key→value detail lines (a compact "Radius=5.00 mm" run, ';'-separated).
struct DraughtHistoryEntry
{
    char            Label[48]   = {};     // [-] - the reader-facing history line
    char            Glyph[24]   = {};     // [-] - the timeline node icon (Lucide name; set at record time from the shape / edit kind)
    char            Detail[96]  = {};     // [-] - ';'-separated "Key=Value" detail pairs the History timeline expands (optional)
    uint32_t        ShapeId     = 0;      // [-] - the shape this entry recorded (0 = a non-shape edit)
    float           FlashLevel  = 0.0f;   // [-] - eased 1→0 new-row highlight (decayed by the History panel per frame)
    DraughtRevision Revision;             // [-] - geometry snapshot AFTER this edit (undo / redo / jump restore this)
};

// 📝 One draughting view's shape store, keyed by the owning panel. Shapes is the committed set; Selected / Hovered mirror the plane
//    view + outliner (0 = none). The in-progress draw lives here too so a torn-off box keeps its own half-drawn shape: DrawingEnabled
//    holds while a click-to-draw is live, DrawingCategory is the tool being drawn, PendingPoints are the placed vertices (world mm),
//    and RubberEnd is the live cursor endpoint the rubber-band previews. EditLog + LogCursor back the History panel (the cursor marks
//    the current state; rows past it read as redoable). Mirrors DraughtingViewState — resolved / released per panel key.
struct DraughtShapeStore
{
    uint32_t                  OwnerDocument  = 0;    // [-]  - the panel key this store belongs to
    std::vector<DraughtShape> Shapes;               // [-]  - the committed analytic shapes
    uint32_t                  NextIdentifier = 1;    // [-]  - monotonic shape-id source
    uint32_t                  Selected       = 0;    // [-]  - selected shape id (0 = none; mirrors SelectionSet.back())
    uint32_t                  Hovered        = 0;    // [-]  - hovered shape id (0 = none)

    // 📝 The per-shape DATUM currently picked for moving (its owning shape id; 0 = none). A transient selection cue like Selected — set when a
    //    datum outliner row / crosshair is clicked, so the viewport datum drag + the G/X/Y datum modal act on it. NOT persisted (cleared on a
    //    restore alongside Selected). The datum is a property of Selected's shape, so a non-zero DatumSelected always equals a currently-shown datum.
    uint32_t                  DatumSelected  = 0;    // [-]  - owning shape id of the datum picked for move (0 = none)

    // 📝 The sub-entity CURRENTLY picked for a datum alignment, mirrored here each frame from the view's component selection so the Properties
    //    "Align to selected edge" action can read it (the store has no other window into the view's sub-entity set). A transient cue — NOT
    //    persisted, NOT snapshotted, recomputed every frame. Category 0 = none, 1 = vertex (anchor only), 2 = edge (anchor at midpoint +
    //    rotation = edge angle). The two endpoints (world mm) describe the edge; a vertex leaves EndB == EndA.
    int                       AlignPickCategory = 0;          // [-]  - 0 none / 1 vertex / 2 edge
    ImVec2                    AlignPickEndA     = ImVec2(0, 0);// [mm] - the vertex, or the edge's first endpoint
    ImVec2                    AlignPickEndB     = ImVec2(0, 0);// [mm] - the edge's second endpoint (== EndA for a vertex)

    // 📝 Transient ARRAY host EYEDROPPER cue. The Properties Array card sets ArrayPickArmed = 1 to request an instance-on-points host; the view
    //    captures the next canvas left-click, writes the picked shape id to ArrayPickHost, and clears the armed flag. NOT persisted, NOT
    //    snapshotted. The card reads ArrayPickHost back on the following frame and folds it into the source shape's ArrayHostShape.
    int                       ArrayPickArmed = 0;             // [-]  - 1 = the Array card armed the host eyedropper (view is capturing the next click)
    uint32_t                  ArrayPickHost  = 0;             // [-]  - the last host shape the view captured for the armed eyedropper (0 = none yet)

    // World-anchored inscriptions (free-text on the sketch plane) — a SEPARATE list + id space from shapes so no shape walk / boolean /
    //    solver ever sees them. SelectedInscription / HoveredInscription mirror Selected / Hovered but are MUTUALLY EXCLUSIVE with them
    //    (selecting an inscription clears the shape selection and vice-versa). Persisted with the sketch + carried through the snapshot.
    std::vector<DraughtInscription> Inscriptions;                    // [-]  - the committed world-anchored inscriptions
    uint32_t                        NextInscriptionIdentifier = 1;   // [-]  - monotonic inscription-id source
    uint32_t                        SelectedInscription       = 0;   // [-]  - selected inscription id (0 = none)
    uint32_t                        HoveredInscription        = 0;   // [-]  - hovered inscription id (0 = none)

    // World-anchored datums (reference points on the sketch plane) — a SEPARATE list + id space from shapes / inscriptions so no shape walk /
    //    boolean / solver ever sees them. SelectedDatum / HoveredDatum mirror Selected / Hovered but are MUTUALLY EXCLUSIVE with both the shape
    //    and inscription selections (selecting a datum clears the others and vice-versa). Persisted with the sketch + carried through the snapshot.
    std::vector<DraughtDatum>       Datums;                          // [-]  - the committed world-anchored datums
    uint32_t                        NextDatumIdentifier       = 1;   // [-]  - monotonic datum-id source
    uint32_t                        SelectedDatum             = 0;   // [-]  - selected datum id (0 = none)
    uint32_t                        HoveredDatum              = 0;   // [-]  - hovered datum id (0 = none)

    // Lofted 3D bodies (tessellated display surfaces through 2+ section profiles) — a SEPARATE list + id space from shapes / inscriptions /
    //    datums so no shape walk / boolean / solver ever sees them (see DraughtLoft). Each carries its LoftSpecification so ReflowLoftBodies
    //    re-solves it in place when a source profile edits. SelectedLoft mirrors the other selections (mutually exclusive with them).
    std::vector<DraughtLoftBody>    LoftBodies;                      // [-]  - the committed lofted 3D bodies
    uint32_t                        NextLoftIdentifier        = 1;   // [-]  - monotonic loft-id source
    uint32_t                        SelectedLoft              = 0;   // [-]  - selected loft id (0 = none)

    // Shift-click multi-selection for a boolean: SelectionSet[0] is the base, the rest are tools. Invariant: distinct ids in click
    //    order; Selected == SelectionSet.back() when non-empty, Selected == 0 iff empty; a plain (non-shift) click resets the set.
    std::vector<uint32_t>     SelectionSet;                           // [-]  - the ordered multi-selection (base first)

    // A transient in-view message the boolean path raises (e.g. "Boolean needs closed shapes"). The view decays NoticeTimer each
    //    frame and clears Notice at zero; shared so the Properties panel's boolean buttons and the plane view read the same string.
    char                      Notice[64]     = {};                    // [-]  - transient message ("" = none)
    float                     NoticeTimer    = 0.0f;                  // [s]  - counts down; the view clears Notice at zero

    bool                      DrawingEnabled  = false;                        // [-]  - a click-to-draw is in progress
    DraughtShapeCategory      DrawingCategory = DraughtShapeCategory::Line;   // [-]  - the shape being drawn
    std::vector<ImVec2>       PendingPoints;                                  // [mm] - vertices placed so far (world mm)
    ImVec2                    RubberEnd = ImVec2(0, 0);                       // [mm] - live cursor endpoint (rubber-band)
    int                       PendingSideCount = 6;                           // [-]  - live polygon side count the wheel adjusts mid-draw (>= 3)

    std::vector<DraughtHistoryEntry> EditLog;        // [-]  - the History panel's row source (append-ordered)
    int                              LogCursor = 0;   // [-]  - current-state marker (count of applied entries)

    std::vector<DraughtFolder> Folders;                  // [-]  - user-created outliner folders (empty = only auto-categories)
    uint32_t                   NextFolderIdentifier = 1;  // [-]  - monotonic folder-id source

    // The constraint layer the solver reads (see DraughtConstraintSolver). Constraints + Dimensions persist with the sketch; a Fixed
    //    constraint pins a point, so no separate fixed-point set is kept. The Next* sources hand out stable ids for the Properties / render
    //    paths. SolveGeneration bumps each time SolveDraughtSketch runs so the view can gate work on "did the sketch just re-solve".
    std::vector<DraughtConstraint> Constraints;                 // [-] - geometric constraints the solver enforces
    std::vector<DraughtDimension>  Dimensions;                  // [-] - driving dimensions (edit re-solves the sketch)
    uint32_t                       NextConstraintIdentifier = 1; // [-] - monotonic constraint-id source
    uint32_t                       NextDimensionIdentifier  = 1; // [-] - monotonic dimension-id source
};

// 📝 Every draughting view's shape store, keyed by owner. The cluster resolves (and lazily creates) a store each frame it paints that
//    box, mirroring DraughtingViewRegistry. Owned by the runtime + threaded through the dock-host chain alongside DraughtViews.
struct DraughtShapeRegistry
{
    std::vector<DraughtShapeStore> Stores;   // [-] - per-panel shape stores, lazily created
};

// 📝 One snap candidate the draw path caught: the world-mm point to snap to + which category found it (drives the marker glyph).
struct DraughtSnapCandidate
{
    ImVec2               Point    = ImVec2(0, 0);                  // [mm] - the world position to snap to
    DraughtSnapCategory  Category = DraughtSnapCategory::Grid;     // [-]  - which target matched (marker cue)
    bool                 Resolved = false;                         // [-]  - a candidate within tolerance was found
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the store for OwnerDocument, lazily creating an empty one on first paint. Mirrors ResolveDraughtingViewState.
DraughtShapeStore& ResolveDraughtShapeStore(DraughtShapeRegistry& Registry, uint32_t OwnerDocument);

// Drop OwnerDocument's store (called when its box closes so shapes do not outlive the panel). No-op when absent.
void ReleaseDraughtShapeStore(DraughtShapeRegistry& Registry, uint32_t OwnerDocument);

// Register the per-frame source bridge before the dock host paints: the ACTIVE draughting document's store, so the outliner /
// Properties / History boxes (separate from the view) read + mutate the same shapes without threading a pointer through the whole
// dock-host chain. Call once each frame with the live store (null when no draughting view is active); pass null to clear afterward.
// A file-scope static bridge, mirroring RegisterSceneOutlinerSource / RegisterPropertiesSceneSource.
void RegisterDraughtShapeSource(DraughtShapeStore* Store);

// Retrieve the store the runtime last published for this frame (see RegisterDraughtShapeSource). Null when no draughting view is
// active — the CAD outliner / Properties / History then paint their empty state.
DraughtShapeStore* RetrieveDraughtShapeSource();

// 📝 The per-frame draughting SCENE-VIEW bridge: the active view publishes the camera + canvas rect the GPU solid pass needs so the
//    lofted body renders INSIDE the sketch canvas, camera-locked, rather than in a side pane. A plain POD (two column-major mat4s laid
//    out exactly as DraughtSurfaceCameraBlock reads them — index = col*4 + row, no transpose; the flatten happens in the view where the
//    camera lives) so this store TU stays free of the camera headers. The canvas rect is screen px (ImGui coords) so the runtime sizes
//    the offscreen target to it and composites the image at that exact rect. ReadyStatus is false outside a live draughting view.
struct DraughtSceneView
{
    float    ViewProjection[16] = {};        // [-]  - world -> clip (the SAME settled matrix the CPU sketch projects with), column-major
    float    ViewMatrix[16]     = {};        // [-]  - world -> camera (matcap normal-transform source), column-major
    ImVec2   CanvasMinimum      = ImVec2(0, 0); // [px] - top-left of the sketch canvas in ImGui screen coords
    ImVec2   CanvasMaximum      = ImVec2(0, 0); // [px] - bottom-right of the sketch canvas in ImGui screen coords
    bool     ThreeDimensionalEnabled = false; // [-]  - the view is in perspective orbit (true) or ortho top-down (false)
    bool     ReadyStatus        = false;     // [-]  - a live draughting view published this frame (false = no active sketch)
};

// Publish the active draughting view's scene-view payload before the dock host paints, so the standalone GPU solid pass (CadMain /
// the editor) sizes its offscreen target to the canvas and drives the matcap camera from the SAME matrix the sketch uses. Call once
// each frame from the active view's paint; the runtime clears it (ReadyStatus false) after the frame. Mirrors RegisterDraughtShapeSource.
void RegisterDraughtSceneView(const DraughtSceneView& View);

// Retrieve the scene-view the active draughting view last published this frame. ReadyStatus is false when no draughting view painted —
// the caller then skips the solid pass (there is no sketch canvas to render into).
DraughtSceneView RetrieveDraughtSceneView();

// 📝 The REVERSE half of the scene bridge: the runtime (CadMain / the editor) publishes the offscreen solid target's ImGui texture handle
//    back to the view AFTER it has rendered the canvas-sized, camera-locked pass, so the draughting view composites that image INTO its own
//    canvas — layered above the dark fill but below the grid + outlines ("solid over the live sketch"). ImTextureID is an opaque handle
//    (the Vulkan descriptor) the view only forwards to ImGui::AddImage; the store TU never dereferences it. Null when no solid was rendered
//    this frame (nothing lofted / preview disabled) — the view then draws no image and the canvas is the pure sketch it was.
void RegisterDraughtSolidImage(ImTextureID Image, uint32_t Width, uint32_t Height);

// Retrieve the solid target image the runtime published this frame (see RegisterDraughtSolidImage). Image is null (0) when nothing was
// rendered; the extent lets the view keep the composite's aspect exact against the canvas rect. Read by the draughting view's render walk.
ImTextureID RetrieveDraughtSolidImage(uint32_t& Width, uint32_t& Height);

// 📝 One tessellated shape body destined for the GPU scene pass: a flat triangle soup in world MM (X-right / Y-up outline at Z = the
//    shape's Elevation), exactly the layout DraughtLoftBody uses so CadMain uploads it through the SAME RenderVertexStream path (and the
//    same mm → cm scale). The view fills these each frame from the CPU fill tessellators (EvaluateFilledPolygon + TriangulateFillRegion)
//    so the GPU fill and the CPU fill can never drift. Positions are triples (x, y, z) flattened; Indices are triangle triples. Identifier
//    ties a body to its source shape id so CadMain can cache device buffers by revision instead of re-uploading every frame.
struct DraughtShapeBody
{
    uint32_t              Identifier = 0;   // [-]  - the source shape's stable id (device-buffer cache key)
    uint32_t              Revision   = 0;   // [-]  - bumps when the tessellation changes, so CadMain re-uploads only then
    std::vector<float>    Positions;        // [mm] - flattened (x, y, z) world-mm vertices (z = the shape's Elevation for a flat cap; the prism spans Elevation..Elevation+ExtrudeDepth)
    std::vector<float>    Normals;          // [-]  - unit vertex normals (x, y, z per vertex; parallels Positions). Empty for a flat fill (the consumer takes the plane normal); a prism fills its cap ±Z + wall lateral normals so the matcap reads solid
    std::vector<uint32_t> Indices;          // [-]  - triangle-triple indices into Positions
};

// Publish the tessellated closed-shape / solid bodies the active draughting view built this frame, so the GPU scene pass (CadMain / the
// editor) uploads + draws them alongside the lofts. Call once each frame from the active view's paint, after the shapes are tessellated;
// an empty list means "no filled shapes this frame" and the pass draws only lofts. Mirrors RegisterDraughtSceneView.
void RegisterDraughtShapeBodies(const std::vector<DraughtShapeBody>& Bodies);

// Retrieve the tessellated shape bodies the active draughting view last published this frame (see RegisterDraughtShapeBodies). Empty when
// no draughting view painted or no closed + filled shapes exist. Read by the runtime's GPU-scene upload walk.
const std::vector<DraughtShapeBody>& RetrieveDraughtShapeBodies();

// How many defining clicks a category expects before it seals. Fixed-count families return their exact count (Line 2, Arc 3, …);
// the open-ended families (Polyline / Bezier / BSpline / Nurbs / Spline) return 0, meaning "collect until a finish gesture".
int ResolveDraughtDefiningCount(DraughtShapeCategory Category);

// Construct a shape record from its DEFINING points (world mm): copies the points, solves the analytic scalars (arc circumcentre,
// ellipse axes, polygon radius, curve degree / rho), and marks the closed families. Does not touch the store — AppendDraughtShape
// calls this, and the view calls it for the live preview so the rubber-band matches the sealed shape exactly. SideCount / Rho / Degree
// override the defaults when > 0 (pass 0 / negative to accept the category default).
DraughtShape ConstructDraughtShape(DraughtShapeCategory       Category,
                                   const std::vector<ImVec2>& DefiningPoints,
                                   int                        SideCountOverride = 0,
                                   float                      RhoOverride       = 0.0f,
                                   int                        DegreeOverride    = 0);

// Flatten a shape to its display polyline in world mm (the analytic-shape → display-polyline step). Straight families copy their
// points; round + curve families sample their analytic form. Reused by the view (render), the picker (distance), and length. The
// caller may pass a sample budget; 0 selects a sensible per-category default.
void EvaluateShapePolyline(const DraughtShape& Shape, std::vector<ImVec2>& Polyline, int SampleBudget = 0);

// Retrieve the shape's cached display polyline (world mm), flattening via EvaluateShapePolyline ONLY when the cache is invalid or was
// built at a different sample budget; on a rebuild it also recomputes the cached AABB. This is the single flatten path for the hot
// snap / pick / render loops — repeated same-frame calls (and idle frames) return the memo with no re-tessellation. Mutates the
// shape's cache fields, so it takes a non-const reference; SampleBudget 0 selects the per-category default (mirrors EvaluateShapePolyline).
const std::vector<ImVec2>& RetrieveCachedOutline(DraughtShape& Shape, int SampleBudget = 0);

// Resolve the analytic SPAN of one defining edge (IndexA → IndexB into Points) as a world-mm polyline in OutSpan (cleared first): the
// contiguous sub-run of the display outline between those two real endpoints. When a fillet / chamfer rounds an endpoint the span
// follows that arc / setback run smoothly (the corner vertex is replaced in the flattened outline), so an edge highlight / pick names
// the clean outline from one endpoint to the next — never the raw corner, never a ring of stray sample dots. Non-const (warms the
// flatten cache via RetrieveCachedOutline); SampleBudget 0 selects the per-category default. Returns true when OutSpan has ≥ 2 points.
bool ResolveAnalyticEdgeSpan(DraughtShape&        Shape,
                             int                  IndexA,
                             int                  IndexB,
                             int                  SampleBudget,
                             std::vector<ImVec2>& OutSpan);

// Resolve an adaptive per-shape sample budget from the curve's world length + the view scale (PixelsPerMm = base * zoom), so the
// on-screen chord error stays near a fixed target no matter the shape's size or the zoom. Estimates world length cheaply (control-hull
// perimeter for the free curves; the analytic circumference for the round families), scales by PixelsPerMm, divides by the target
// chord, and clamps to a sensible floor / ceiling. Polygon returns 0 (its SideCount is geometry, not sampling). The view passes the
// result into EvaluateShapePolyline; 0 falls back to the fixed per-category default (no regression for the fixed callers).
int ResolveAdaptiveSampleBudget(const DraughtShape& Shape, float PixelsPerMm);

// Append a shape of Category built from DefiningPoints (world mm) to the store: solves it via ConstructDraughtShape, assigns the next
// id, seeds its title ("Circle 1"), records an edit-log entry (FlashLevel 1.0) + advances LogCursor, and returns the new id. No-op
// (returns 0) when the points are too few for the category. (`Commit` is banned — this is the analytic-shape append.) SideCountOverride
// carries the live polygon side count the wheel set mid-draw (>= 3, else the default); Rho / Degree overrides mirror ConstructDraughtShape.
uint32_t AppendDraughtShape(DraughtShapeStore&         Store,
                            DraughtShapeCategory       Category,
                            const std::vector<ImVec2>& DefiningPoints,
                            int                        SideCountOverride = 0,
                            float                      RhoOverride       = 0.0f,
                            int                        DegreeOverride    = 0);

// Resolve a shape by id (null when absent) — the outliner / Properties borrow the selected shape through this.
DraughtShape* ResolveDraughtShape(DraughtShapeStore& Store, uint32_t Identifier);

// Append a world-anchored inscription at AnchorMm (world mm) carrying Text (copied, truncated to the fixed buffer): assigns the next
// inscription id, seeds default PixelHeight / tint, records an edit-log entry ("Added Inscription 1") + advances LogCursor, and returns
// the new id. Text may be null / empty (a blank inscription the Properties panel then fills in). Separate id space from shapes.
uint32_t AppendInscription(DraughtShapeStore& Store, ImVec2 AnchorMm, const char* Text);

// Resolve an inscription by id (null when absent) — the outliner / Properties borrow the selected inscription through this.
DraughtInscription* ResolveInscription(DraughtShapeStore& Store, uint32_t Identifier);

// Move an inscription's anchor to AnchorMm (world mm) — the gizmo / modal-G drag path. A one-liner today (the anchor is a single point,
// no re-solve), exposed as a verb so the move site reads like the shape path (EnforceShapePoint) and later growth stays localised.
void EnforceInscriptionAnchor(DraughtInscription& Inscription, ImVec2 AnchorMm);

// Detach the inscription Identifier from the store: erase it, clear it from the selection / hover, and record an edit-log entry. No-op
// when absent. Inscriptions carry no constraints / dimensions to prune, so this mirrors DetachDatum. (`Delete` / `Remove` → Detach.)
void DetachInscription(DraughtShapeStore& Store, uint32_t Identifier);

// Append a world-anchored datum at AnchorMm (world mm): assigns the next datum id, seeds the default axis (Cross) + title ("Datum 1"),
// records an edit-log entry ("Added Datum 1") + advances LogCursor, and returns the new id. Separate id space from shapes / inscriptions.
uint32_t AppendDatum(DraughtShapeStore& Store, ImVec2 AnchorMm);

// Resolve a datum by id (null when absent) — the outliner / Properties borrow the selected datum through this.
DraughtDatum* ResolveDatum(DraughtShapeStore& Store, uint32_t Identifier);

// Move a datum's anchor to AnchorMm (world mm) — the Select-tool drag path. A one-liner today (a single point, no re-solve), exposed as a
// verb so the move site reads like the inscription / shape paths (EnforceInscriptionAnchor / EnforceShapePoint) and later growth stays local.
void EnforceDatumAnchor(DraughtDatum& Datum, ImVec2 AnchorMm);

// Resolve the nearest DISPLAYED, unlocked datum whose anchor lies within ToleranceMm of WorldPoint, returning its id (0 = none close). A
// datum is a discrete point, so this is a point-to-point test (never an outline distance). The Select tool calls it with a pixel tolerance
// divided by the current scale so the catch radius is constant on screen.
uint32_t PickDatum(DraughtShapeStore& Store, ImVec2 WorldPoint, float ToleranceMm);

// Detach the datum Identifier from the store: erase it, clear it from the selection / hover, and record an edit-log entry. No-op when
// absent. Datums carry no constraints / dimensions to prune, so this is simpler than DetachDraughtShape. (`Delete` / `Remove` → Detach.)
void DetachDatum(DraughtShapeStore& Store, uint32_t Identifier);

// Evaluate the minimum distance (world mm) from WorldPoint to the shape's outline — point-to-segment over the CACHED flattened
// polyline (via RetrieveCachedOutline) for every category. Used by the picker + hover test. Takes a non-const reference because it
// warms the flatten cache; SampleBudget 0 selects the per-category default.
float EvaluateDistanceToShape(DraughtShape& Shape, ImVec2 WorldPoint, int SampleBudget = 0);

// Resolve the nearest DISPLAYED shape whose outline lies within ToleranceMm of WorldPoint, returning its id (0 = nothing close).
// The plane view calls this each frame with a pixel tolerance divided by the current scale. Broad-phase rejects any shape whose
// cached AABB (expanded by ToleranceMm) misses the cursor before flattening. Non-const because the cull warms the per-shape cache.
uint32_t PickDraughtShape(DraughtShapeStore& Store, ImVec2 WorldPoint, float ToleranceMm);

// Evaluate the total outline length (world mm) of a shape — the sum of its segment lengths (plus the closing edge when looped).
// The Properties panel reads this for the selected shape.
float EvaluateDraughtShapeLength(const DraughtShape& Shape);

// Resolve a shape's ONE governing dimension in world mm — the number the precision box types into: Circle / Arc / Polygon / Slot →
// Radius, Ellipse → MajorAxis, and the straight + free-curve families → total outline length. PrimaryLabel (when non-null) receives a
// short caption for that dimension ("R", "Major", "Length"). Returns 0 for a degenerate shape.
float ResolvePrimaryDimension(const DraughtShape& Shape, const char** PrimaryLabel);

// Enforce a new primary dimension (world mm) on a shape, re-solving its analytic form so the outline + length follow: the round
// families scale their radius / major axis about the centre (the minor axis tracks the major's ratio for an ellipse); the straight +
// free-curve families uniformly scale their points about their centroid to the requested outline length. A no-op for a non-positive
// target or a degenerate shape. The Points array is kept consistent so picking + the Properties readout stay exact.
void EnforcePrimaryDimension(DraughtShape& Shape, float TargetMillimetres);

//------------------------------------------------------------------------------------------------------------------------
//                                                     WINDING + FILL
//------------------------------------------------------------------------------------------------------------------------
// 📝 A closed shape reads as a hollow outline until it is FILLED; a stable fill needs a self-consistent orientation (WINDING). These
//    keep the fill deterministic no matter how the user clicked (a rectangle dragged from any corner fills the same). Pure analytic.

// Evaluate the signed area (shoelace) of a polyline in world mm — positive when counter-clockwise, negative when clockwise, ~0 when
// degenerate. The winding-order enforcement + the convex / concave fill decision read its sign.
float EvaluateSignedArea(const std::vector<ImVec2>& Polyline);

// Enforce a polyline's winding order in place: reverse the run when its signed-area sign disagrees with the requested orientation
// (CounterClockwiseEnabled true = positive area), so a fill reads consistently regardless of the click order.
void EnforceWindingOrder(std::vector<ImVec2>& Polyline, bool CounterClockwiseEnabled);

// Flatten a shape to its CLOSED display polyline + normalize the winding for a fill: samples the outline (SampleBudget as
// EvaluateShapePolyline), appends the closing point when the last does not already meet the first, and orients it counter-clockwise.
// The fill path calls this one function; a shape that is not closed yields an empty run.
void EvaluateFilledPolygon(const DraughtShape& Shape, std::vector<ImVec2>& OutPolyline, int SampleBudget = 0);

//------------------------------------------------------------------------------------------------------------------------
//                                                         SNAPPING
//------------------------------------------------------------------------------------------------------------------------
// 📝 Analytic point snapping the draw path calls after the grid snap: scan the displayed shapes for a point within a pixel tolerance
//    of the cursor (endpoints, segment midpoints, round-family centres, and the nearest point on an outline). An in-range candidate
//    overrides the grid snap so a click lands exactly on the caught feature. Intersection is a named seam only (a dedicated tool later).

// Resolve the nearest enabled snap target to WorldCursor across the store's displayed shapes. Mask is a bitmask of (1u <<
// (unsigned)DraughtSnapCategory) — only the set targets are scanned; Grid is handled by the view, not here. PixelsPerMm converts the
// fixed pixel tolerance to world mm so the catch radius is constant on screen. Returns a resolved candidate (the closest within
// tolerance) or an unresolved one (Resolved = false) when nothing is close. Never returns Intersection this pass.
DraughtSnapCandidate ResolveSnapCandidate(DraughtShapeStore& Store,
                                          ImVec2             WorldCursor,
                                          float              PixelsPerMm,
                                          unsigned           Mask);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PER-PARAMETER ENFORCE
//------------------------------------------------------------------------------------------------------------------------
// 📝 The editable Properties panel drives these: each re-solves ONE analytic parameter of a shape and keeps Points + the solved
//    scalars consistent so the plane view (which re-flattens every frame) follows the same frame. No-ops on a non-applicable
//    category or a degenerate input. These are the per-field siblings of EnforcePrimaryDimension.

// Enforce the round-family radius (Circle / Arc / Polygon → circum-radius; Slot → half-width) in world mm.
void EnforceShapeRadius(DraughtShape& Shape, float RadiusMillimetres);

// Enforce an ellipse's major + minor semi-axes (world mm) independently, scaling the stored major-end handle by the major ratio.
void EnforceEllipseAxes(DraughtShape& Shape, float MajorMillimetres, float MinorMillimetres);

// Read a rectangle's width + height (world mm) off its four-corner axis-aligned boundary. Both 0 for a non-rectangle / empty shape.
void ResolveRectangleDimensions(const DraughtShape& Shape, float& Width, float& Height);

// Enforce a rectangle's width + height (world mm) about its current centre, so only the box size changes; re-solves the corners.
void EnforceRectangleDimensions(DraughtShape& Shape, float WidthMillimetres, float HeightMillimetres);

// Enforce an ellipse's major-axis rotation (radians), re-placing the major-end handle so a later re-solve reads the same angle.
void EnforceShapeRotation(DraughtShape& Shape, float RotationRadians);

// Enforce a polygon's side count (clamped to >= 3); the outline re-flattens to the new vertex count about the same Centre + Radius.
void EnforcePolygonSideCount(DraughtShape& Shape, int SideCount);

// Enforce a conic's shoulder weight ratio (0.02 .. 0.98; 0.5 = parabola).
void EnforceConicRho(DraughtShape& Shape, float Rho);

// Enforce a B-spline / NURBS degree (clamped to the control-point count - 1, and >= 1).
void EnforceCurveDegree(DraughtShape& Shape, int Degree);

// Move one defining point (Index into Points) to a world-mm position, re-solving the round-family scalars whose points changed so
// the outline + Centre / Radius stay exact for the point-based families (Line / Polyline / Rectangle carry the point verbatim).
void EnforceShapePoint(DraughtShape& Shape, int Index, ImVec2 WorldMillimetres);

//------------------------------------------------------------------------------------------------------------------------
//                                                       EDIT LOG + FOLDERS
//------------------------------------------------------------------------------------------------------------------------

// Append one entry to the store's edit log at the cursor (discarding any redoable tail first so a fresh edit after an undo forks
// cleanly), with a reader Label + a timeline Glyph (a Lucide icon name) + the owning ShapeId (0 for a non-shape edit), seed its
// FlashLevel to 1.0, CAPTURE a geometry snapshot into the entry's Revision (so undo / redo / jump can restore this step), and advance
// LogCursor. Shared by AppendDraughtShape + the Properties per-field edits. (`Record` is banned.)
void AppendDraughtEdit(DraughtShapeStore& Store, uint32_t ShapeId, const char* Label, const char* Glyph);

// As AppendDraughtEdit, but also stores a ';'-separated "Key=Value" Detail run (e.g. "Radius=5.00 mm;Edges=4") the History timeline
// expands under the row. Detail may be null / empty. The plain AppendDraughtEdit forwards here with no detail.
void AppendDraughtEditDetailed(DraughtShapeStore& Store,
                               uint32_t           ShapeId,
                               const char*        Label,
                               const char*        Glyph,
                               const char*        Detail);

//------------------------------------------------------------------------------------------------------------------------
//                                                       UNDO / REDO / JUMP
//------------------------------------------------------------------------------------------------------------------------
// 📝 The timeline is a linear log with a cursor (LogCursor = count of applied entries). Each entry carries a full geometry snapshot
//    taken AFTER its edit, so moving the cursor to step K restores entry[K-1]'s snapshot (or the empty state at K = 0) in O(1) — no
//    forward replay. Undo steps the cursor back one, redo forward one, jump sets it to an arbitrary step; all three funnel through
//    RestoreDraughtRevisionAt. A fresh edit after an undo forks the tail (AppendDraughtEdit already discards the redoable entries).

// Capture the store's current authored state (shapes / constraints / dimensions / folders / id sources / selection) into OutRevision,
// dropping the transient flatten caches. Called by AppendDraughtEdit; exposed so a caller may snapshot before a compound edit.
void CaptureDraughtRevision(const DraughtShapeStore& Store, DraughtRevision& OutRevision);

// Restore the store's authored state to the snapshot at LogCursor = Step (Step in [0, EditLog.size()]): copies entry[Step-1]'s
// Revision back over the live fields (or clears to the empty sketch at Step 0), sets LogCursor = Step, invalidates flatten caches,
// and re-selects the snapshot's Selected (clamped to a surviving shape). No-op when Step is out of range or already current.
void RestoreDraughtRevisionAt(DraughtShapeStore& Store, int Step);

// Step the cursor back one applied edit (undo). Returns true when it moved (false at the start of the log).
bool UndoDraughtEdit(DraughtShapeStore& Store);

// Step the cursor forward one redoable edit (redo). Returns true when it moved (false at the tip of the log).
bool RedoDraughtEdit(DraughtShapeStore& Store);

// Attach a new user folder to the store (named "Folder N"), append an edit-log entry, and return its identifier. The caller may
// then set a shape's FolderIdentifier to the returned id to move it in (this pass adopts the currently-selected shape).
uint32_t AttachDraughtFolder(DraughtShapeStore& Store, const char* Title);

//------------------------------------------------------------------------------------------------------------------------
//                                                        SHAPE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------
// 📝 Whole-shape edits the context menu drives: clone a shape offset from its origin, detach one from the store, and read the
//    boundary of every displayed shape (Fit view). Each keeps the edit log + selection consistent; a Detach clears the selection.

// Duplicate the shape Identifier: clone it with a fresh id + title, nudge every defining point by OffsetMm (so the copy is visible),
// re-solve via ConstructDraughtShape, append it selected, record an edit-log entry, and return the new id (0 when the source is absent).
uint32_t DuplicateDraughtShape(DraughtShapeStore& Store, uint32_t Identifier, ImVec2 OffsetMm);

// Resolve the shape's centre in world mm — the AABB midpoint of its flattened display outline (uniform across every category). Warms
// the flatten cache (non-const), so it takes a mutable reference. Used to seed the per-shape datum anchor at the shape centre on enable.
ImVec2 ResolveDraughtShapeCentroid(DraughtShape& Shape);

// Detach the shape Identifier from the store: erase it, drop any constraints / dimensions that reference it, clear it from the
// selection / hover, and record an edit-log entry. No-op when absent. (`Delete` / `Remove` map to Detach per the verb list.)
void DetachDraughtShape(DraughtShapeStore& Store, uint32_t Identifier);

// The Trim tool (Plasticity Trim): remove the ONE section of shape Identifier under CursorMm. A section is a span of the curve's
// flattened outline bounded by its INTERSECTIONS with every other displayed curve, its endpoints (open run), and — for a straight /
// compound family (Line / Polyline / Rectangle / Profile) — its interior defining vertices. Works on every family (Line / Arc /
// Bezier / Circle / Profile …) because it runs on the display tessellation, not just the defining points. A closed loop loses a
// section and survives as the single open span the other way round; an open run loses a section and survives as its head + tail
// (each an appended Polyline). Removing the only section detaches the shape. Re-solves + logs a "Trimmed" edit. Returns true when it
// changed the store; no-op false when the shape is absent or the cursor missed the outline within Tolerance.
bool PartitionShapeSegment(DraughtShapeStore& Store, uint32_t Identifier, ImVec2 CursorMm, float ToleranceMm);

// Resolve — WITHOUT mutating anything — the exact outline SECTION that PartitionShapeSegment would remove for a Trim click at CursorMm on
// shape Identifier, as a world-mm polyline in OutSpan (cleared first). The view strokes this as a live hover highlight so the user sees the
// span about to be trimmed before committing. Reproduces the same boundary build (intersections + endpoints + straight-family vertices) and
// section-location as the real trim. Returns true when a removable span was resolved (OutSpan has ≥ 2 points); false (OutSpan empty) when the
// shape is absent or the cursor missed the outline within ToleranceMm.
bool ResolveTrimPreviewSpan(DraughtShapeStore&   Store,
                            uint32_t             Identifier,
                            ImVec2               CursorMm,
                            float                ToleranceMm,
                            std::vector<ImVec2>& OutSpan);

// The Cut tool (Plasticity Cut Curve): SPLIT shape Identifier at the CLICKED point CursorMm (nearest point on its flattened outline
// within ToleranceMm). A CLOSED shape opens at the cut point into one open piece (a rectangle stays a rectangle-shaped run but is no
// longer a loop; a second cut then divides it into two); an OPEN run splits into a head + tail. The ANALYTIC round families stay exact:
// a cut Circle opens into one full-sweep Arc, a cut Arc divides into two Arcs (Centre / Radius / start + sweep preserved — never a
// dotted sample run). Straight / free families yield open Polyline pieces following the tessellation. The first piece reuses the
// original id; the rest are appended (tint / folder carried). No-op false when the click missed the outline. Logs a "Cut" edit.
// `Split` is the approved verb (Cut is the UI-tool label only).
bool CutShapeAtPoint(DraughtShapeStore& Store, uint32_t Identifier, ImVec2 CursorMm, float ToleranceMm);

// The Join tool on OPEN shapes (Plasticity "Join"): weld the selected open runs END-TO-END into ONE path. Each selected shape is
// flattened to its display polyline; runs whose free endpoints coincide within WeldToleranceMm are chained (reversing a run when its
// far end matches), skipping the shared point so no duplicate seam remains. When the finished path's two ends meet within tolerance it
// closes into a filled Profile (outliner Profiles group); otherwise it stays an open Polyline (Curves group). The first selected open
// shape keeps its id (tint / folder carried); every other welded shape is detached; unreachable runs are left untouched (never lost).
// Re-solves + logs a "Joined" edit. Returns false (no-op) when fewer than two open runs connect. Closed-only selections use the boolean
// Union path instead (see AppendBooleanResult). `Assemble` is the approved verb (Join is the UI-tool label only).
bool AssembleOpenShapes(DraughtShapeStore& Store, float WeldToleranceMm);

// Resolve the world-mm AABB enclosing every DISPLAYED shape's cached outline (for Fit view). Returns false + leaves the corners
// untouched when nothing is displayed. Non-const because it warms each shape's flatten cache through RetrieveCachedOutline.
bool ResolveShapesBoundary(DraughtShapeStore& Store, ImVec2& BoundaryMinimum, ImVec2& BoundaryMaximum);

} // namespace Frontier

#endif
