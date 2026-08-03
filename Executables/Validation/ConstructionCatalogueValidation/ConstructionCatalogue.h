/*==============================================================================================================================================
                                                        CONSTRUCTIONCATALOGUE.H
==============================================================================================================================================*/
// 🧩 The construction workspace's catalogue — 11 bands over 128 B-rep operations — and the OpenCASCADE dimension-raising gate that decides,
//    against a live document geometry profile, which of them are available, gated-with-reason, or hidden. GENERATED from
//    Documentation/Prototypes/ConstructionCatalogueMenu.html by _ClaudeScratch/tmp/EmitConstructionCatalogue.py, which evaluates the file's
//    own tables rather than transcribing them. 🔴 This is the 5-state Evaluate() model, NOT the ToolCard's 3-state StrataMask — so the op and
//    band structs are the target's own; only the per-op parameter rows reuse Frontier::ToolParameterDescriptor. ⚠️ EDIT THE PROTOTYPE AND
//    RE-RUN THE EMITTER, never this file.

#pragma once
#ifndef FRONTIER_VALIDATION_CONSTRUCTIONCATALOGUE_CONSTRUCTIONCATALOGUE_H
#define FRONTIER_VALIDATION_CONSTRUCTIONCATALOGUE_CONSTRUCTIONCATALOGUE_H

#include "EngineContext/Interface/Components/Menus/ToolCard/ToolCardSpecification.h"

namespace ConstructionCatalogueValidation
{

//----------------------------------------------------------------------------------------------------------------------
//                                                          TYPES
//----------------------------------------------------------------------------------------------------------------------

// 📝 The seven dimensions of the B-rep chain, low to high. Nothing is a REAL member (an empty document), not the absence of
//    one — the creation ops apply precisely when the active dimension is Nothing, and the raising law's top has no successor.
enum class ConstructionDimension
{
    Nothing,
    Vertex,
    Edge,
    Wire,
    Face,
    Shell,
    Solid,
};

constexpr int ConstructionDimensionCount = 7;

// 📝 The gate's verdict for one operation against the current document. Five states, in the prototype's order: Available and
//    ClosureShortfall both RUN (the second warns the result is a shell, not a solid); WorkplaneAbsent and ProfileShortfall are
//    gated-with-reason; DimensionMismatch is hidden, not greyed.
enum class GateResult
{
    Available,          // runs; result is as declared
    ClosureShortfall,   // runs; the law predicts a shell, not a solid
    WorkplaneAbsent,    // gated; a workplane is the one offered remedy
    ProfileShortfall,   // gated; some other precondition is unmet
    DimensionMismatch,  // hidden; does not apply to this dimension at all
};

// 📝 The document geometry profile — everything the gate is allowed to read. A real editor fills this by interrogating the shape
//    once per change; the validation panel fills it from the probe controls. Every field earns its place by gating at least one op.
struct ConstructionDocument
{
    ConstructionDimension ActiveDimension;          // [-]  - the dimension of the current selection
    int                   SelectedCount;            // [idx]- components picked
    int                   ProfileCount;             // [idx]- closed profiles present
    int                   BoundaryEdgeCount;        // [idx]- edges on the active boundary
    int                   ExistingCircleCount;      // [idx]- circles/arcs present
    int                   SolidCount;               // [idx]- solids present
    bool                  WorkplaneActivation;      // [-]  - a workplane is set
    bool                  ClosedProfileCondition;   // [-]  - the active profile closes
    bool                  PlanarProfileCondition;   // [-]  - the active profile is planar
    bool                  AxisAvailability;         // [-]  - an axis is available
    bool                  PathAvailability;         // [-]  - a sweep path is available
    bool                  UniformClosureCondition;  // [-]  - profiles share closure
    bool                  PendingGeometryCondition; // [-]  - pending geometry exists
    bool                  SupportMaterialCondition; // [-]  - material for a feature to reach
    bool                  TangentEndpointCondition; // [-]  - a shared tangent endpoint
    bool                  OpeningCondition;         // [-]  - an opening to close
    bool                  ReferencePlaneCondition;  // [-]  - a reference plane
    bool                  SourceImageryCondition;   // [-]  - imported source imagery
    bool                  MeasurableCondition;      // [-]  - a measurable component
};

// 📝 One catalogue operation. Carries the whole gate surface the prototype op schema carries — dims/needs as bitmasks, the count
//    minima, the boundary range, and the raising flag. `DimensionMask` is a bit per ConstructionDimension; `AcceptsAnyGeometry`
//    marks an op whose dims were absent (accepts any dimension EXCEPT Nothing). `NeedMask` is a bit per requirement key, indexed
//    as ConstructionRequirementKey. Parameters reuse the shared descriptor so the reused options column draws them.
struct ConstructionOperation
{
    const char*  GlyphName;          // [-]  - tile artwork, raw prototype identifier
    const char*  Label;             // [-]  - tile caption
    const char*  Accelerator;       // [-]  - corner keystroke; null for none
    unsigned int DimensionMask;     // [-]  - accepted dimensions, bit per ConstructionDimension
    bool         AcceptsAnyGeometry;// [-]  - dims were absent: any dimension but Nothing
    unsigned int NeedMask;          // [-]  - requirement keys, bit per ConstructionRequirementKey
    int          MinimumCount;      // [idx]- minCount: least selected
    int          MinimumProfile;    // [idx]- minProfile: least profiles
    int          MinimumSolid;      // [idx]- minSolid: least solids
    int          ExactSolid;        // [idx]- exactSolid: precise solid count (0 == unused)
    int          MinimumCircle;     // [idx]- minCircle: least circles
    bool         BoundaryPresent;   // [-]  - the boundary edge range applies
    int          BoundaryFloor;     // [idx]- boundary[0]
    int          BoundaryCeiling;   // [idx]- boundary[1]
    bool         RaisesDimension;   // [-]  - the raising law computes the result type
    const Frontier::ToolParameterDescriptor* Parameters;  // [-] - options rows (borrowed); null for none
    int          ParameterCount;    // [idx]- number of rows
};

// 📝 One rail band: a caption, its rail/header glyph, and the operations it opens.
struct ConstructionBand
{
    const char*                  Caption;      // [-]  - rail label + grid header title
    const char*                  GlyphName;    // [-]  - rail artwork + grid header badge
    const ConstructionOperation* Operations;   // [-]  - the band's ops (borrowed)
    int                          OperationCount; // [idx]
};

// 📝 The bucket tally the prototype displays and asserts on: every op lands in exactly one, and the three sum to the catalogue total.
struct ConstructionTally
{
    int Available;   // [idx]- Available or ClosureShortfall
    int Gated;       // [idx]- WorkplaneAbsent or ProfileShortfall
    int Hidden;      // [idx]- DimensionMismatch
    int Total;       // [idx]- the three summed
};

//----------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//----------------------------------------------------------------------------------------------------------------------

// The band table and its length.
const ConstructionBand* ResolveConstructionBands(int& BandCount);

// The document profile the panel opens against — the prototype's default DOC (empty document, no workplane).
ConstructionDocument DefaultConstructionDocument();

// The dimension-raising law: the successor dimension of Dimension when a profile of the given closure is swept, or a sentinel
// meaning "no successor". ClosedCondition only matters for Wire and Shell.
bool RaiseDimension(ConstructionDimension Dimension, bool ClosedCondition, ConstructionDimension& Raised);

// Why the law produced that answer, for the result-type readout's second line.
const char* DescribeLaw(ConstructionDimension Dimension, bool ClosedCondition);

// The gate. Evaluates one operation against the document, in the prototype's exact order:
// dimension -> workplane -> counts -> remaining inputs -> closure(last).
GateResult EvaluateOperation(const ConstructionOperation& Operation, const ConstructionDocument& Document);

// The first shortfall text for a gated tile's chip, in the same order the gate stops on. Written into Destination (capacity
// bytes); returns Destination. Some texts are computed ("needs 2 solids"), so this cannot return a static pointer.
const char* ShortfallText(const ConstructionOperation& Operation, const ConstructionDocument& Document,
                          char* Destination, int Capacity);

// The result type an op would produce right now, for the live badge — or false into Result when the op does not raise dimension
// (a primitive's result is declared, not derived).
bool ResultType(const ConstructionOperation& Operation, const ConstructionDocument& Document, ConstructionDimension& Result);

// The four rendering predicates, computed from EvaluateOperation exactly as the prototype does.
bool OperationOmitted(const ConstructionOperation& Operation, const ConstructionDocument& Document);
bool OperationGated(const ConstructionOperation& Operation, const ConstructionDocument& Document);
bool OperationFixable(const ConstructionOperation& Operation, const ConstructionDocument& Document);
bool OperationLive(const ConstructionOperation& Operation, const ConstructionDocument& Document);

// The invariant tally over the whole catalogue at the given document.
ConstructionTally TallyBuckets(const ConstructionDocument& Document);

// A dimension's display name (for the badge caption and the document control surface).
const char* DescribeDimension(ConstructionDimension Dimension);

} // namespace ConstructionCatalogueValidation

#endif
