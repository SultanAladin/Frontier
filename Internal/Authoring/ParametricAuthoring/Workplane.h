/*==============================================================================================================================================
                                                                    WORKPLANE.H
==============================================================================================================================================*/
// 🧩 The construction-plane authoring layer: a standalone analytic model of the flat reference sheets a sketch is drawn ON — origin +
//    orientation + a finite display rectangle + a snap grid, held parametrically (a construction METHOD + its references) so the plane
//    re-solves its world frame when the geometry it anchors to moves. Lives beside ParametricSketchShapeStore under the Authoring pillar
//    (device-independent CPU model; the viewport projects + strokes it, the outliner lists it, the Properties / History panels read it).
//    Definition mirrors WorkplaneExplainer.html: Method (XY/XZ/YZ default + Offset/Angle/ThreePoint/Midplane/Tangent/PointNormal/OnFace),
//    a world origin + U/V/Normal frame, and per-plane display cues (visibility, extent, tint/opacity, grid + spacing, snap, lock, title).
//    Z-up right-handed world (matching the viewport camera + ground grid): the XY plane's normal is +Z (lies flat), XZ is +Y, YZ is +X.

#pragma once
#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_WORKPLANE_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_WORKPLANE_H

#include "imgui.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 How a workplane's world frame is DERIVED — the parametric recipe the solver re-runs when a reference moves (faithful to the
//    construction methods in WorkplaneExplainer.html). The three principal methods (PrincipalXY/XZ/YZ) are the handed-to-you defaults
//    (no references, a fixed world frame); the rest anchor to geometry. This phase solves the principal + Offset + Angle frames
//    analytically; the geometry-referencing methods (ThreePoint/Midplane/Tangent/PointNormal/OnFace) carry their intent + references and
//    fall back to the seeded frame until the reference-resolve layer lands. Named .Category per the skills (a …Category enum suffix, not Kind).
enum class WorkplaneConstructionCategory
{
    PrincipalXY = 0,   // [-] - the horizontal default sheet (origin world zero, normal +Y) — the usual active plane
    PrincipalXZ = 1,   // [-] - the front default sheet (normal +Z)
    PrincipalYZ = 2,   // [-] - the right default sheet (normal +X)
    Offset      = 3,   // [-] - a parallel copy of a reference plane shifted along its normal by OffsetDistance (the workhorse)
    Angle       = 4,   // [-] - a reference plane rotated about a pivot edge by AngleDegrees
    ThreePoint  = 5,   // [-] - the unique plane through three referenced points / vertices
    Midplane    = 6,   // [-] - the plane centred exactly between two parallel reference faces (symmetry)
    Tangent     = 7,   // [-] - a flat plane touching a curved / cylindrical reference face
    PointNormal = 8,   // [-] - the plane through a reference point, perpendicular to a reference line / curve tangent
    OnFace      = 9,   // [-] - adopts an existing planar face of a solid (sketch directly on the model face)
};

// 📝 Which reference axes a plane draws / snaps its 2D (u, v) grid to on screen — a display + intent cue, never geometry. Cross draws both
//    the U and V minor-cell lattices (the usual look); UAxis / VAxis draw only one family; None hides the lattice but keeps the sheet
//    outline. Named .AxisDisplay per the skills (an axis selection, not a shape family). Independent of GridEnabled — this only picks WHICH
//    lattice lines draw when the grid is on.
enum class WorkplaneGridAxis
{
    None  = 0,   // [-] - no lattice lines (just the sheet rectangle)
    UAxis = 1,   // [-] - only the U-parallel family of lines
    VAxis = 2,   // [-] - only the V-parallel family of lines
    Cross = 3,   // [-] - both families (the full lattice)
};

// 📝 Which of the seated frame's own axes the Angle method PIVOTS about — so a tilt is not locked to one direction. UAxis rotates V + Normal
//    about U (tips the sheet forward / back, like a drawing board — the old fixed behaviour); VAxis rotates U + Normal about V (tips it left /
//    right); NormalAxis rotates U + V about the Normal (spins the sheet in its own plane — a roll). Named .PivotAxis per the skills (which axis
//    is the hinge, not a rotation "mode"). Only read by the Angle method; the others ignore it.
enum class WorkplaneAnglePivot
{
    UAxis      = 0,   // [-] - hinge about U (tilt forward / back) — the horizontal pivot
    VAxis      = 1,   // [-] - hinge about V (tilt left / right)   — the vertical pivot
    NormalAxis = 2,   // [-] - spin about the Normal (in-plane roll)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A stable reference into some host geometry the parametric plane anchors to — the atoms the construction methods pick (a face id, an
//    edge id, a vertex id, or a sibling plane id). Frontier's geometry ids are not yet unified across the subsystems, so this is an opaque
//    (Category, Identifier) pair the reference-resolve layer interprets later; this phase carries them verbatim so a plane records its
//    intent (Deleting these breaks the plane — the Explainer's "Reference(s)" note). ReferenceCategory 0 = unset (an empty slot).
struct WorkplaneReference
{
    int      ReferenceCategory = 0;   // [-] - 0 unset / 1 planar face / 2 edge or axis / 3 point or vertex / 4 sibling plane
    uint32_t Identifier        = 0;   // [-] - the host geometry's id within its owning store (0 = unset)
};

// 📝 One construction plane — its DEFINITION (the parametric recipe + references) plus its solved world FRAME plus per-plane display cues.
//    The frame (Origin + AxisU + AxisV + Normal) is what the viewport projects and the sketch tools snap onto; it is SOLVED from the method
//    every edit (SolveWorkplaneFrame), never authored directly, so a moved reference re-seats the plane. Definition scalars (OffsetDistance
//    / AngleDegrees / FlipNormalEnabled / FlipAlignmentEnabled) drive the geometry-referencing methods. Display cues (Displayed / Extent /
//    Colour / GridEnabled / GridSpacing / SnapEnabled / LockEnabled / Title) are viewport-only, never fed to a solver. Plain-old-data
//    (fixed Title buffer, no heap fields) so the whole-struct history snapshot path stays a trivial copy, matching ParametricSketchShape.
struct Workplane
{
    uint32_t                      Identifier   = 0;                                   // [-]  - unique within the owning store
    WorkplaneConstructionCategory Method       = WorkplaneConstructionCategory::PrincipalXY; // [-] - the parametric recipe the frame solves from

    // Parametric DEFINITION — the recipe + references SolveWorkplaneFrame re-runs. References are the anchored host geometry (empty for
    //    the principal defaults). The scalars drive the geometry-referencing methods; unused ones stay at their defaults per method.
    WorkplaneReference PrimaryReference;                     // [-]  - the plane / face the Offset / Angle / Tangent / OnFace method anchors to
    WorkplaneReference PivotReference;                       // [-]  - the edge / axis the Angle method pivots about
    WorkplaneReference PointReferenceA;                      // [-]  - a referenced point (ThreePoint p0, Midplane face-b, PointNormal origin)
    WorkplaneReference PointReferenceB;                      // [-]  - a referenced point (ThreePoint p1)
    WorkplaneReference PointReferenceC;                      // [-]  - a referenced point (ThreePoint p2)
    float              OffsetDistance = 0.0f;                // [mm] - Offset method: signed shift along the reference normal (can be negative)
    // 📝 The AUTHORED base origin (world mm) the solve seats the frame at BEFORE any method transform — non-zero only for a plane PLACED at a
    //    picked point (the interactive draw sweeps a rectangle on the ground and centres the sheet at the drag midpoint). The principal
    //    methods keep this origin (Offset still shifts along the normal FROM it); it defaults to world zero, so every existing caller (which
    //    never sets it) solves exactly as before (origin world zero). Not a solved output — a definition input, snapshotted with the plane.
    float              AuthoredOriginX = 0.0f, AuthoredOriginY = 0.0f, AuthoredOriginZ = 0.0f; // [mm] - placed-plane base origin (0 = world zero)
    float              AngleDegrees   = 0.0f;                // [deg]- Angle method: signed rotation about the pivot axis
    WorkplaneAnglePivot AnglePivot    = WorkplaneAnglePivot::UAxis; // [-] - Angle method: which frame axis is the rotation hinge
    bool               FlipNormalEnabled    = false;         // [-]  - reverse which side is "up" (flips extrude direction + sketch handedness)
    bool               FlipAlignmentEnabled = false;         // [-]  - Midplane / parallel case: swap which reference the plane aligns to

    // Solved world FRAME (Z-up right-handed world mm) — the output of SolveWorkplaneFrame, what the viewport draws + the sketch maps (u, v)
    //    through. AxisU / AxisV span the sheet; Normal = AxisU x AxisV (before any FlipNormalEnabled). Never authored directly.
    ImVec2  Padding_Unused = ImVec2(0, 0);                   // [-]  - reserved (keeps the frame block 16-byte friendly; unused this phase)
    float   OriginX = 0.0f, OriginY = 0.0f, OriginZ = 0.0f;  // [mm] - the plane's world origin (the 2D (u,v) zero)
    float   AxisUX  = 1.0f, AxisUY  = 0.0f, AxisUZ  = 0.0f;  // [-]  - the U (sketch-horizontal) direction, unit
    float   AxisVX  = 0.0f, AxisVY  = 1.0f, AxisVZ  = 0.0f;  // [-]  - the V (sketch-vertical) direction, unit (Z-up: +Y on ground XY)
    float   NormalX = 0.0f, NormalY = 0.0f, NormalZ = 1.0f;  // [-]  - the plane normal (extrude direction), unit (Z-up ground: +Z)

    // DISPLAY cues (viewport-only, non-parametric) — mirrors the Explainer's display table.
    bool              Displayed    = true;                   // [-]  - outliner visibility toggle (the eye affordance)
    float             Extent       = 2000.0f;                // [mm] - half-size of the finite display rectangle (planes are infinite; this is the clickable sheet). 2 m half → a 4 m sheet, readable at the ~18 m boot orbit (200 mm was a 0.4 m sheet, invisibly small)
    float             ColourRGBA[4] = { 0.36f, 0.62f, 1.0f, 0.16f }; // [-] - sheet tint + translucency (default the Explainer's XY blue wash)
    bool              GridEnabled  = true;                    // [-]  - draw the snap lattice ON the plane
    float             GridSpacing  = 200.0f;                  // [mm] - minor-cell spacing of the on-plane grid (0.2 m cells → ~20 cells across a 4 m sheet)
    int               GridSubdivisions = 5;                   // [-]  - minor cells per major cell (>= 1)
    WorkplaneGridAxis GridAxis     = WorkplaneGridAxis::Cross; // [-]  - which lattice families draw when the grid is on
    bool              SnapEnabled  = true;                     // [-]  - the cursor snaps to grid intersections while sketching
    bool              LockEnabled  = false;                    // [-]  - freeze the active sketch plane (cursor can't wander onto another face)
    char              Title[48]    = {};                       // [-]  - display title ("Workplane 1", "Offset XY 60", …) — appears in the outliner
};

// 📝 A SNAPSHOT of a store's whole authored plane set AFTER one edit, captured so undo / redo / jump-to restores any timeline point in O(1)
//    (no forward replay), mirroring ParametricSketchRevision. Carries every persisted field (the planes, the id source, the selection) but
//    never the transient solve — the frame re-solves lazily on restore (Workplane keeps its default frame until SolveWorkplaneFrame runs).
//    One snapshot lives on each WorkplaneHistoryEntry; restoring copies it back over the store's live fields.
struct WorkplaneRevision
{
    std::vector<Workplane> Planes;                 // [-] - the committed planes at this step
    uint32_t               NextIdentifier = 1;      // [-] - monotonic plane-id source at this step
    uint32_t               Selected       = 0;      // [-] - selected plane id at this step
};

// 📝 One entry in the store's edit log — the History panel's row source, mirroring ParametricSketchHistoryEntry. Label is the reader line
//    ("Added Workplane 1"); Glyph the timeline node icon (a Lucide name set at record time); Detail the ';'-separated "Key=Value" run the
//    timeline expands under the row; PlaneId ties the row back to the plane it recorded; FlashLevel is the eased 1→0 new-row pulse the
//    History panel decays each frame; Revision is the plane-set snapshot AFTER this edit — the state undo / redo / jump restore.
struct WorkplaneHistoryEntry
{
    char              Label[48]  = {};    // [-] - the reader-facing history line
    char              Glyph[24]  = {};    // [-] - the timeline node icon (Lucide name)
    char              Detail[96] = {};    // [-] - ';'-separated "Key=Value" detail pairs the History timeline expands (optional)
    uint32_t          PlaneId    = 0;     // [-] - the plane this entry recorded (0 = a non-plane edit)
    float             FlashLevel = 0.0f;  // [-] - eased 1→0 new-row highlight (decayed by the History panel per frame)
    WorkplaneRevision Revision;           // [-] - plane-set snapshot AFTER this edit (undo / redo / jump restore this)
};

// 📝 One document's workplane store, keyed by the owning panel — the source of truth the viewport draws + picks, the outliner lists, and the
//    Properties / History panels read + mutate. Planes is the committed set; Selected / Hovered mirror the viewport + outliner (0 = none).
//    EditLog + LogCursor back the History panel (the cursor marks the current state; rows past it read as redoable). Mirrors
//    ParametricSketchShapeStore's per-panel shape store, pared to the plane concern. Resolved / released per panel key by the registry below.
struct WorkplaneStore
{
    uint32_t               OwnerDocument  = 0;    // [-] - the panel key this store belongs to
    std::vector<Workplane> Planes;                // [-] - the committed construction planes
    uint32_t               NextIdentifier = 1;    // [-] - monotonic plane-id source
    uint32_t               Selected       = 0;    // [-] - selected plane id (0 = none)
    uint32_t               Hovered        = 0;    // [-] - hovered plane id (0 = none)

    std::vector<WorkplaneHistoryEntry> EditLog;   // [-] - the History panel's row source (append-ordered)
    int                                LogCursor = 0; // [-] - current-state marker (count of applied entries)
};

// 📝 Every document's workplane store, keyed by owner — the cluster resolves (and lazily creates) a store each frame it paints that box,
//    mirroring ParametricSketchShapeRegistry.
struct WorkplaneRegistry
{
    std::vector<WorkplaneStore> Stores;   // [-] - per-panel plane stores, lazily created
};

// 📝 One tessellated plane BODY destined for the GPU scene pass OR the CPU viewport overlay: the finite sheet as a flat quad triangle soup
//    plus its on-plane grid line endpoints, world mm, so the consumer draws the sheet + lattice without re-deriving the frame. Quad is the
//    sheet's four corners (world mm, CCW); GridLines are flattened endpoint pairs (Ax,Ay,Az, Bx,By,Bz per segment). ColourRGBA carries the
//    sheet tint; Identifier ties a body to its source plane id so a device consumer caches by revision. Built by AssembleWorkplaneBodies.
struct WorkplaneBody
{
    uint32_t           Identifier = 0;                 // [-]  - the source plane's stable id (device / cache key)
    uint32_t           Revision   = 0;                 // [-]  - bumps when the sheet / grid changes, so the consumer re-uploads only then
    float              Quad[12]   = {};                // [mm] - the sheet's four corners, flattened (x,y,z) CCW
    std::vector<float> GridLines;                      // [mm] - on-plane lattice segment endpoints (Ax,Ay,Az,Bx,By,Bz per segment)
    float              ColourRGBA[4] = { 0.36f, 0.62f, 1.0f, 0.16f }; // [-] - resolved sheet tint + translucency
    float              NormalX = 0.0f, NormalY = 0.0f, NormalZ = 1.0f; // [-] - the sheet normal (for a lit / oriented consumer; Z-up)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the store for OwnerDocument, lazily creating an empty one on first paint. Mirrors ResolveParametricSketchShapeStore.
WorkplaneStore& ResolveWorkplaneStore(WorkplaneRegistry& Registry, uint32_t OwnerDocument);

// Drop OwnerDocument's store (called when its box closes so planes do not outlive the panel). No-op when absent.
void ReleaseWorkplaneStore(WorkplaneRegistry& Registry, uint32_t OwnerDocument);

// Register the per-frame source bridge before the dock host paints: the ACTIVE document's plane store, so the outliner / Properties /
// History boxes read + mutate the same planes without threading a pointer through the whole dock-host chain. Call once each frame with the
// live store (null when no plane-owning view is active); pass null to clear afterward. A file-scope static bridge, mirroring
// RegisterParametricSketchShapeSource.
void RegisterWorkplaneSource(WorkplaneStore* Store);

// Retrieve the store the runtime last published for this frame (see RegisterWorkplaneSource). Null when no plane-owning view is active —
// the outliner / Properties / History then paint their empty state.
WorkplaneStore* RetrieveWorkplaneSource();

// Solve a plane's world FRAME (Origin + AxisU + AxisV + Normal) from its Method + definition scalars + references, writing it back onto the
// plane. The principal methods seat the fixed Z-up world frames; Offset copies the seeded principal frame shifted by OffsetDistance along the
// normal; Angle rotates the seeded frame about AxisU by AngleDegrees. The geometry-referencing methods (ThreePoint / Midplane / Tangent /
// PointNormal / OnFace) seat the neutral XY frame this phase (the reference-resolve layer fills them later). FlipNormalEnabled negates the
// solved Normal (and swaps AxisV's sense) so the extrude side reverses. Idempotent; called after every definition edit.
void SolveWorkplaneFrame(Workplane& Plane);

// Construct a plane record for Method with a default definition + a solved frame + a seeded title, WITHOUT touching a store (AppendWorkplane
// calls this; the viewport calls it for a live preview). Origin defaults to world zero; the principal methods need no references.
Workplane ConstructWorkplane(WorkplaneConstructionCategory Method);

// Append a plane of Method to the store: builds it via ConstructWorkplane, assigns the next id, seeds its title ("Workplane 1"), records an
// edit-log entry (FlashLevel 1.0) + advances LogCursor, selects it, and returns the new id. (`Commit` is banned — this is the plane append.)
uint32_t AppendWorkplane(WorkplaneStore& Store, WorkplaneConstructionCategory Method);

// Resolve a plane by id (null when absent) — the outliner / Properties borrow the selected plane through this.
Workplane* ResolveWorkplane(WorkplaneStore& Store, uint32_t Identifier);

// Detach the plane Identifier from the store: erase it, clear it from the selection / hover, and record an edit-log entry. No-op when
// absent. (`Delete` / `Remove` map to Detach per the verb list.)
void DetachWorkplane(WorkplaneStore& Store, uint32_t Identifier);

// Re-solve the plane's frame after a definition edit and record an edit-log entry (Label / Glyph / optional Detail) so the History panel and
// undo / redo see the change. The Properties per-field edits funnel through here after writing a definition scalar. Detail may be null.
void EnforceWorkplaneDefinition(WorkplaneStore& Store, uint32_t Identifier, const char* Label, const char* Glyph, const char* Detail);

//------------------------------------------------------------------------------------------------------------------------
//                                                         EDIT LOG + HISTORY
//------------------------------------------------------------------------------------------------------------------------

// Append one entry to the store's edit log at the cursor (discarding any redoable tail first so a fresh edit after an undo forks cleanly),
// with a reader Label + a timeline Glyph (a Lucide icon name) + the owning PlaneId (0 for a non-plane edit) + an optional ';'-separated
// "Key=Value" Detail run, seed its FlashLevel to 1.0, CAPTURE a snapshot into the entry's Revision, and advance LogCursor. Shared by
// AppendWorkplane + the Properties edits. (`Record` is banned.) Detail may be null / empty.
void AppendWorkplaneEdit(WorkplaneStore& Store, uint32_t PlaneId, const char* Label, const char* Glyph, const char* Detail);

// Capture the store's current authored state (planes + id source + selection) into OutRevision. Called by AppendWorkplaneEdit; exposed so a
// caller may snapshot before a compound edit. Mirrors CaptureParametricSketchRevision.
void CaptureWorkplaneRevision(const WorkplaneStore& Store, WorkplaneRevision& OutRevision);

// Restore the store's authored state to the snapshot at LogCursor = Step (Step in [0, EditLog.size()]): copies entry[Step-1]'s Revision back
// over the live fields (or clears to the empty set at Step 0), sets LogCursor = Step, and re-selects the snapshot's Selected (clamped to a
// surviving plane). Re-solves every restored plane's frame. No-op when Step is out of range or already current.
void RestoreWorkplaneRevisionAt(WorkplaneStore& Store, int Step);

// Step the cursor back one applied edit (undo). Returns true when it moved (false at the start of the log).
bool UndoWorkplaneEdit(WorkplaneStore& Store);

// Step the cursor forward one redoable edit (redo). Returns true when it moved (false at the tip of the log).
bool RedoWorkplaneEdit(WorkplaneStore& Store);

//------------------------------------------------------------------------------------------------------------------------
//                                                         RENDER ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// Build one WorkplaneBody per DISPLAYED plane in Store from its solved frame + display cues (the finite sheet quad + the on-plane grid line
// endpoints in world mm), so the viewport overlay (or a GPU pass) draws the sheet + lattice without re-deriving the frame. GridEnabled /
// GridSpacing / GridSubdivisions / GridAxis drive the lattice; Extent sizes the quad; ColourRGBA carries the tint. Skips hidden planes.
// Clears OutBodies first. A pure ADAPTER over the solved frame — no re-solve (call SolveWorkplaneFrame on edit, not here).
void AssembleWorkplaneBodies(const WorkplaneStore& Store, std::vector<WorkplaneBody>& OutBodies);

} // namespace Frontier

#endif
