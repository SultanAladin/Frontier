/*==============================================================================================================================================
                                                    SCENEDIRECTORYINSPECTOR.H
==============================================================================================================================================*/
// 🧩 The data model for the summoned two-slide scene-directory inspector, ported 1:1 from Documentation/Prototypes/SceneDirectoryInspector.html.
//    It is the property + history half: the flat RecordClassification set (nine kinds, each its own hue), the per-record RecordProfile the
//    property cards read and write, the card + field spec tables that drive the property surface, and the branching RevisionStore (lateral
//    branches, per-branch cursor, fork-on-record when recording behind the tip). 🔴 The DIRECTORY TREE is NOT here: the rows / selection /
//    rename / drag / eye / filter / menus are the reused SketchOutliner panel (SketchOutlinerState, caller-owned), so this half never carries a
//    tree node — the property resolvers read the selection SketchOutliner surfaces (classification + child count + visibility), not an SDI node.
//    Its types live in the app-local namespace SceneDirectoryInspectorValidation, never Frontier, so RecordClassification never collides.

#pragma once
#ifndef FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_SCENEDIRECTORYINSPECTOR_H
#define FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_SCENEDIRECTORYINSPECTOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace SceneDirectoryInspectorValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The nine record kinds the prototype's recordStore uses (CLASSIFICATION_HUE / CLASSIFICATION_LABEL / CLASSIFICATION_GLYPH keys).
//    Classification is the ONE field that drives everything downstream: the row hue, the type glyph, the metadata label, and — the
//    load-bearing part — which property cards slide 2 shows. Order matches the prototype's ADD_CHOICES for the add menu.
enum class RecordClassification
{
    Scene,       // [-] - the document root ("Part"); hue sky
    Folder,      // [-] - a grouping body; hue violet
    Sketch,      // [-] - a 2D sketch; hue cyan
    Solid,       // [-] - an extruded solid; hue amber
    Cylinder,    // [-] - a cylinder primitive; hue green
    Sphere,      // [-] - a sphere primitive; hue pink
    Cone,        // [-] - a cone primitive; hue red
    Revolve,     // [-] - a revolved feature; hue earth
    Loft,        // [-] - a lofted feature; hue blue
    Workplane    // [-] - a construction plane (origin + normal + grid); hue teal
};

// The nine add-menu / chip choices, in on-screen order (ADD_CHOICES) — Scene is the root and is never added, so it is omitted.
extern const RecordClassification AddChoices[9];

// Packed 0xAABBGGRR ImU32 hue for a classification (CLASSIFICATION_HUE), and its display label (CLASSIFICATION_LABEL).
std::uint32_t ClassificationHue(RecordClassification Classification);
const char*   ClassificationLabel(RecordClassification Classification);

// The classification's type-glyph registry key ("scene" / "folder" / ...), lowercase, matching the prototype's classification string.
const char*   ClassificationKey(RecordClassification Classification);


//------------------------------------------------------------------------------------------------------------------------
//                                                          RECORD PROFILE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The property bag a record's cards read and write (establishProfile). The prototype grows one object per record with a common base
//    then Object.assign's the classification-specific keys; C++ has no open object, so this is the UNION of every field any classification
//    uses, plus a Populated flag so establishProfile runs once (the JS `if(entry.profile) return`). Every field carries the prototype's
//    exact seed. Unused fields for a given classification are simply never shown by profileCards, exactly as the JS never assigns them.
struct RecordProfile
{
    bool  Populated = false;              // [-]  - establishProfile has seeded this (entry.profile presence)

    // -- base (every classification) --
    bool  Visible   = true;               // [-]  - mirrors !entry.hidden
    float Position[3] = { 0.0f, 0.0f, 0.0f };  // [-]
    float Rotation[3] = { 0.0f, 0.0f, 0.0f };  // [deg]
    float Scale[3]    = { 1.0f, 1.0f, 1.0f };  // [-]
    int   Albedo[4]   = { 214, 216, 222, 255 };// [-]  - RGBA 0..255
    float Roughness   = 0.42f;            // [-]
    float Metalness   = 0.08f;            // [-]
    int   ShadingMode = 0;                // [idx]- Smooth / Faceted / Flat
    bool  Selectable  = true;             // [-]

    // -- scene --
    int         Units            = 1;     // [idx]- Inches / Millimetres / Centimetres / Metres
    float       ToleranceLinear  = 0.01f; // [mm]
    float       ToleranceAngular = 0.5f;  // [deg]
    std::string DocumentPath     = "/Projects/Bracket_Rev4.wsdoc"; // [-]

    // -- folder --
    int   NestedTally = 0;                // [ct] - seeded from nestedCount(entry) at establish time
    int   BooleanMode = 0;                // [idx]- Union / Subtract / Intersect
    bool  Suppressed  = false;            // [-]

    // -- sketch --
    int   PlaneChoice     = 0;            // [idx]- XY / XZ / YZ / Custom
    int   ConstraintTally = 12;           // [ct]
    int   CurveTally      = 8;            // [ct]
    bool  FullyConstrained = false;       // [-]
    float GridSnap        = 0.5f;         // [mm]

    // -- solid --
    float ExtrudeDepth  = 12.5f;          // [mm]
    float DraftAngle    = 0.0f;           // [deg]
    float WallThickness = 2.5f;           // [mm]
    bool  CappedEnds    = true;           // [-]  - also used by cylinder

    // -- cylinder / sphere / cone --
    float Radius       = 6.25f;           // [mm] - cylinder + sphere seed the same field to their own value
    float Height       = 18.0f;           // [mm] - cylinder + cone
    int   SegmentTally = 32;              // [ct] - cylinder / sphere / cone
    int   RingTally    = 24;              // [ct] - sphere
    float BaseRadius   = 7.0f;            // [mm] - cone
    float TipRadius    = 0.0f;            // [mm] - cone

    // -- revolve --
    float SweepAngle    = 360.0f;         // [deg]
    int   AxisChoice    = 1;              // [idx]- X / Y / Z
    bool  ProfileClosed = true;           // [-]

    // -- loft --
    int   SectionTally  = 3;              // [ct]
    float TangencyStart = 0.0f;           // [-]
    float TangencyEnd   = 0.0f;           // [-]
    bool  Ruled         = false;          // [-]

    // -- workplane -- (mirrors WorkplaneExplainer.html's definition + display split; the outliner card face for Frontier::Workplane)
    int   PlaneMethod    = 0;             // [idx]- XY / XZ / YZ / Offset / Angle / 3-Point / Midplane / Tangent / Point-Normal / On-Face
    float PlaneOffset    = 0.0f;          // [mm] - Offset method: signed shift along the reference normal
    float PlaneAngle     = 0.0f;          // [deg]- Angle method: signed rotation about the pivot axis
    int   PlaneAnglePivot = 0;            // [idx]- Angle method hinge: 0 U (tilt fwd/back) / 1 V (tilt left/right) / 2 Normal (in-plane roll)
    bool  FlipNormal     = false;         // [-]  - reverse which side is "up" (flips extrude direction)
    float PlaneExtent    = 2000.0f;       // [mm] - half-size of the finite display rectangle (2 m half → 4 m sheet, readable at the boot orbit)
    bool  PlaneGrid      = true;          // [-]  - draw the snap lattice on the plane
    float PlaneGridSpacing = 200.0f;      // [mm] - minor-cell spacing of the on-plane grid (0.2 m cells)
    bool  PlaneSnap      = true;          // [-]  - the cursor snaps to grid intersections while sketching
    bool  PlaneLock      = false;         // [-]  - freeze the active sketch plane
    // [mm] - authored base origin, non-zero only for a plane PLACED by the interactive draw (the sheet is centred on the drag midpoint); the
    //        default XY plane keeps world zero. Carried into Frontier::Workplane.AuthoredOrigin so the solve seats the frame there.
    float PlaneOriginX   = 0.0f;
    float PlaneOriginY   = 0.0f;
    float PlaneOriginZ   = 0.0f;
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          RECORD TOKEN
//------------------------------------------------------------------------------------------------------------------------

// [-] - a record's stable identity; matches SketchOutliner's RecordToken (that panel owns the tree, this half keys profiles by the same id).
using RecordToken = std::uint32_t;


//------------------------------------------------------------------------------------------------------------------------
//                                                          PROPERTY CARDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The control kinds a field row can be (the prototype's field.ctl switch in buildField). Each maps to one field builder in the panel.
enum class FieldControl
{
    Text,        // [-] - a free string (Name writes through to the record)
    Vector,      // [-] - three axis-tagged number boxes (X/Y/Z)
    Slider,      // [-] - a bounded track with fill + knob
    Scalar,      // [-] - an unbounded value, drag-to-nudge
    Boolean,     // [-] - a switch (Visible drives the record)
    Selection,   // [-] - a segmented one-of-N
    Dropdown,    // [-] - a popped list one-of-N
    Colour,      // [-] - the RGBA picker
    Path         // [-] - a text path + browse
};

// One editable field in a card (a member of a card's `fields` array). Key names the RecordProfile member it binds. Unused members for a
// given control are simply ignored (the prototype's field objects only carry the keys that control reads).
struct FieldSpec
{
    FieldControl Control;              // [-]  - which widget
    const char*  Key;                  // [-]  - RecordProfile field this binds (or "Name" for the record)
    const char*  Label;                // [-]  - row label
    float        Step   = 0.01f;       // [-]  - vector / scalar nudge step
    int          Format = 2;           // [-]  - decimal places (fmt); 0 rounds to int
    float        Minimum = 0.0f;       // [-]  - slider bound
    float        Maximum = 1.0f;       // [-]  - slider bound
    const char*  Unit   = "\xC2\xB7";  // [-]  - unit segment text (middot default)
    const char*  Options[8] = {};      // [-]  - selection / dropdown option labels (nullptr-terminated)
    int          OptionCount = 0;      // [-]  - how many Options are set
};

// One property card (a card object: a title + its fields).
struct CardSpec
{
    const char* Title;                 // [-]  - card head text (also the fold-memory key half)
    FieldSpec   Fields[8];             // [-]  - up to eight rows
    int         FieldCount;            // [-]  - how many Fields are set
};

// Seed a profile if not yet populated (establishProfile), then return it. The selection is surfaced by SketchOutliner, so the record's
// live facts arrive as loose arguments: Classification drives the seed, NestedTally seeds the folder card's child count, Visible mirrors
// !hidden. Populated gates the one-time seed exactly as the prototype's `if(entry.profile) return`.
RecordProfile& EstablishProfile(RecordClassification Classification, int NestedTally, bool Visible, RecordProfile& Profile);

// Fill OutCards with the cards for a classification (profileCards) in on-screen order — the Record identity card first, then the
// classification-specific cards, then the shared Transform / Appearance cards where the prototype pushes them. Returns the count.
int ResolveProfileCards(RecordClassification Classification, CardSpec OutCards[8]);


//------------------------------------------------------------------------------------------------------------------------
//                                                          REVISION STORE
//------------------------------------------------------------------------------------------------------------------------

// 📝 A history category (the recordRevision(category,…) first argument / REVISION_CLASS key). Drives the timeline node glyph, the type
//    pill label + tone, and the marker hue. Visual-only: recording a revision logs a row, it does not restore geometry.
enum class RevisionCategory
{
    Start,       // [-] - document opened
    Feature,     // [-] - a feature commit
    Param,       // [-] - a parameter change
    Sketch,      // [-] - a sketch edit
    Transform,   // [-] - a relocate
    Body,        // [-] - a group
    Add,         // [-] - a create / duplicate
    Edit,        // [-] - an edit / isolate / rename
    Drop         // [-] - a delete / hide
};

// The pill tone (REVISION_CLASS.tone) — colours the type chip. None leaves it neutral.
enum class RevisionTone { None, Generative, Material, Parametric };

// The glyph name + label + tone for a category (REVISION_CLASS row), and its marker hue (REVISION_HUE).
const char*    RevisionGlyphName(RevisionCategory Category);
const char*    RevisionLabel(RevisionCategory Category);
RevisionTone   RevisionToneOf(RevisionCategory Category);
std::uint32_t  RevisionHue(RevisionCategory Category);

// One logged revision (makeRevision). At is a wall-clock stamp captured as "HH:MM" text at record time (the prototype's toLocaleTimeString).
struct Revision
{
    RevisionCategory Category = RevisionCategory::Edit;  // [-]
    std::string      Title;                              // [-] - the reader line
    std::string      Subtitle;                           // [-] - the detail line (may be empty)
    std::string      TimeText;                           // [-] - "HH:MM" captured at record
};

// One lateral branch (newBranch). Cursor is the current-state marker: -1 before the first revision, else the applied index. Rows past
// the cursor read as `future` (redoable); the row at the cursor reads as `at-cursor`.
struct RevisionBranch
{
    std::string           Name;             // [-] - "Trunk" / "Branch N"
    std::vector<Revision> Revisions;        // [-] - append-ordered
    int                   Cursor = -1;      // [-] - applied index (-1 = before the first)
};

// The whole revision store (revisionStore): the branch list + which is active. Fork-on-record lives in RecordRevision.
struct RevisionStore
{
    std::vector<RevisionBranch> Branches;   // [-] - lateral branches
    int                         Active = 0; // [idx]- the shown branch
    int                         BranchSeq = 0; // [-] - monotonic branch-name counter (branchSeq)
};

// Seed the store with the prototype's opening four revisions on a "Trunk" branch (seedRevisions).
void SeedRevisions(RevisionStore& Store);

// 📝 Log a revision (recordRevision): appends to the active branch at its tip, OR — if the cursor sits behind the tip — forks a new
//    lateral branch from the retained prefix, appends there, and makes it active. TimeText is supplied by the caller (the panel captures
//    the frame clock) since the model owns no clock.
void RecordRevision(RevisionStore& Store, RevisionCategory Category, const char* Title, const char* Subtitle, const char* TimeText);

// Undo / redo / jump on the active branch (stepBack / stepForward / jumpToRevision). Jump clamps to [-1, size-1].
void StepBack(RevisionStore& Store);
void StepForward(RevisionStore& Store);
void JumpToRevision(RevisionStore& Store, int Index);

// Fork a branch from the active branch's retained prefix [0, cursor] (the hx-tree-add "+"), making the fork active.
void ForkBranch(RevisionStore& Store);

// Drop a branch by index (hx-tree-x), clamping Active into range. No-op when only one branch remains (the × is hidden then).
void DropBranch(RevisionStore& Store, int Index);

}   // namespace SceneDirectoryInspectorValidation

#endif
