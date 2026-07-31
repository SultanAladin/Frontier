/*==============================================================================================================================================
                                                            SELECTIONPREDICATE.H
==============================================================================================================================================*/
// 🧩 The gate that decides whether one polygon-mutation operation may run against the current selection. Three facts drive it: which stratum of
//    the complex is selected (vertex / edge / face / loop / ring / border / object), how MANY components are selected, and how they are CONNECTED.
//    A predicate that fails reports WHICH of the three it was — so a menu row can grey out with the true reason ("needs 2 regions", "requires a
//    quad ring") instead of a generic "unavailable". Pure arithmetic over a SelectionProfile: no ImGui, no device, no allocation — so the whole
//    availability table is unit-testable and the same predicates serve a menu, a toolbar, or a command palette.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_SELECTIONPREDICATE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_POLYGONMUTATION_SELECTIONPREDICATE_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which stratum of the polygon complex the selection occupies. Vertex / Edge / Face are the three primitive strata; EdgeLoop and EdgeRing
//    are edge selections carrying proven loop/ring connectivity (worth their own identity because ~20 operations accept ONLY those); Border is
//    an open-boundary edge run, promoted to a first-class stratum because it alone gates FillBoundary, GrowQuadRow and edge-level welds
//    (3ds Max models it as its own sub-object level for exactly this reason); Object is the whole complex.
//    🔴 Nothing is the EMPTY selection, and it is a real stratum rather than an absence: with nothing selected no operation can modify anything, so
//       the only meaningful offer is CREATION. Modelling it here means the availability gate answers "what applies to an empty selection?" through
//       the same mask test as every other stratum, instead of every caller special-casing a null. It sits LAST, after Object, so the existing
//       StratumMask bits keep their values — inserting it at 0 would renumber all seven and silently invalidate every mask already written.
enum class TopologyStratum : uint8_t
{
    Vertex = 0,
    Edge,
    Face,
    EdgeLoop,
    EdgeRing,
    Border,
    Object,
    Nothing,

    StratumCount
};

// 📝 A bitmask of accepted strata, so one operation declares every stratum it serves in a single field. Values are 1 << TopologyStratum.
enum class StratumMask : uint16_t
{
    None      = 0,
    Vertex    = 1u << 0,
    Edge      = 1u << 1,
    Face      = 1u << 2,
    EdgeLoop  = 1u << 3,
    EdgeRing  = 1u << 4,
    Border    = 1u << 5,
    Object    = 1u << 6,
    Nothing   = 1u << 7,

    // Frequently-declared unions.
    AnyEdgeForm   = Edge | EdgeLoop | EdgeRing | Border,      // [-] - Every stratum backed by edges
    AnyComponent  = Vertex | Edge | Face | EdgeLoop | EdgeRing | Border,
    // 🔴 AnyStratum deliberately EXCLUDES Nothing. It is the "serves every selection" shorthand written across the operation
    //    catalogue, and an empty selection is precisely the case where a modify operation does NOT apply — folding Nothing in would
    //    make every existing tool claim to work on nothing. Creation tools name Nothing explicitly instead.
    AnyStratum    = AnyComponent | Object,
    AnySelection  = AnyStratum | Nothing                     // [-] - Genuinely every stratum, empty included
};

// 📝 Why a row is not available. StratumMismatch means the operation is meaningless for what is selected — the row is OMITTED entirely rather
//    than greyed, because a vertex menu listing "Flip Normals" is noise. The other two mean the operation IS meaningful here but the selection
//    is not yet shaped right, so the row greys out and states the shortfall. 🔴 Keeping CountShortfall and TopologyShortfall distinct is the
//    whole point: Blender's Grid Fill reports "select two edge loops" when the real fault is odd boundary parity, and users chase the wrong fix.
enum class AvailabilityCondition : uint8_t
{
    Available = 0,          // [-] - Every predicate passed
    StratumMismatch,        // [-] - Wrong selection stratum; hide the row
    CountShortfall,         // [-] - Right stratum, wrong number of components selected
    TopologyShortfall       // [-] - Right stratum and count, but the connectivity is unsuitable
};

// 📝 Which connectivity facts an operation demands. A bitmask so one operation declares several at once. Each bit maps to one field of
//    SelectionProfile and, when the bit is set but the fact is absent, resolves to TopologyShortfall with that bit's explanatory text.
enum class ConnectivityRequirement : uint16_t
{
    None                 = 0,
    QuadRingWalkable     = 1u << 0,   // [-] - An edge ring can be walked through quads (InsertEdgeLoop dies at any tri / n-gon)
    ClosedBoundaryLoop   = 1u << 1,   // [-] - A genuine closed open-boundary exists (FillBoundary; a sealed box has none)
    EvenBoundaryParity   = 1u << 2,   // [-] - Boundary edge count is even (GridFillBoundary builds quads, so odd cannot close)
    ManifoldCondition    = 1u << 3,   // [-] - No edge carries 3+ faces (BevelEdge excludes those outright)
    SharedComponent      = 1u << 4,   // [-] - Components are shared by 2+ faces (BreakSharedVertex, DetachComponent)
    TwoFaceEdge          = 1u << 5,   // [-] - Selected edges border EXACTLY 2 faces (SpinEdgeDiagonal, FlipTriangleEdge)
    QuadPairMatched      = 1u << 6,   // [-] - Two faces with matching edge counts (SpinQuadFlow, UnifyPolygons)
    AdjacentTrianglePair = 1u << 7,   // [-] - Adjacent triangles exist to pair (QuadrangulateTrianglePairs)
    NonPlanarFace        = 1u << 8,   // [-] - A face with 4+ sides exists (MakeFacesPlanar; triangles are always planar)
    MultipleShell        = 1u << 9,   // [-] - 2+ disconnected shells (SeparateShell, PartitionBoolean)
    OpenSurface          = 1u << 10,  // [-] - An open (non-sealed) surface (Solidify has nothing to thicken on a closed solid)
    SymmetryAxis         = 1u << 11   // [-] - Topological symmetry was established (SymmetrizeAcrossAxis)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      MASK COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Bitwise composition for the two mask enums. Both are enum class for type safety — which costs the built-in operators, so a
//    catalogue entry could not otherwise write `StratumMask::Edge | StratumMask::Face`. Restoring only `|` and `&` (not arithmetic)
//    keeps the type closed: a mask can be composed and tested, never accidentally added to an integer.
constexpr StratumMask operator|(StratumMask Left, StratumMask Right)
{
    return static_cast<StratumMask>(static_cast<uint16_t>(Left) | static_cast<uint16_t>(Right));
}

constexpr StratumMask operator&(StratumMask Left, StratumMask Right)
{
    return static_cast<StratumMask>(static_cast<uint16_t>(Left) & static_cast<uint16_t>(Right));
}

constexpr ConnectivityRequirement operator|(ConnectivityRequirement Left, ConnectivityRequirement Right)
{
    return static_cast<ConnectivityRequirement>(static_cast<uint16_t>(Left) | static_cast<uint16_t>(Right));
}

constexpr ConnectivityRequirement operator&(ConnectivityRequirement Left, ConnectivityRequirement Right)
{
    return static_cast<ConnectivityRequirement>(static_cast<uint16_t>(Left) & static_cast<uint16_t>(Right));
}


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything a predicate is allowed to read about the current selection. A real editor fills this by interrogating the polygon complex once
//    per selection change; the validation app fills it from toggles and spinners. Deliberately flat POD so the whole availability table can be
//    recomputed for ~95 operations in a single pass with no allocation.
struct SelectionProfile
{
    TopologyStratum ActiveStratum      = TopologyStratum::Object;   // [-]   - Which stratum the selection occupies
    int             SelectedCount      = 0;                         // [idx] - Components selected within that stratum
    int             DistinctRegionCount = 0;                        // [idx] - Contiguous islands within the selection (BridgeSpan needs 2)
    int             BoundaryEdgeCount  = 0;                         // [idx] - Open-boundary edges bordering the selection
    int             ShellCount         = 1;                         // [idx] - Disconnected shells present
    int             MaximumFaceSideCount = 4;                       // [idx] - Largest side count among selected faces (planarity gate)

    // 📝 Connectivity facts, each mirroring one ConnectivityRequirement bit. Named per §8 — no is/has prefixes.
    bool QuadRingCondition       = false;   // [-] - An edge ring walks cleanly through quads
    bool ClosedLoopCondition     = false;   // [-] - The selection forms a closed loop (vs an open chain)
    bool BoundaryCondition       = false;   // [-] - A genuine open boundary exists
    bool ManifoldCondition       = true;    // [-] - No edge carries three or more faces
    bool SharedComponentCondition = false;  // [-] - Components are shared between 2+ faces
    bool TwoFaceEdgeCondition    = false;   // [-] - Selected edges border exactly two faces
    bool QuadPairCondition       = false;   // [-] - Two faces with matching edge counts
    bool TrianglePairCondition   = false;   // [-] - Adjacent triangle pairs exist
    bool OpenSurfaceCondition    = false;   // [-] - The surface is open, not a sealed solid
    bool SymmetryAxisCondition   = false;   // [-] - Topological symmetry is established
};


// 📝 The gate declaration carried by one operation. AcceptedStrata is the coarse filter; MinimumCount / ExactCount bound the selection size
//    (ExactCount 0 means "unbounded above"); RequiredConnectivity is the bitmask of facts that must hold. 🔴 ExactCount exists as its own field
//    rather than MinimumCount == MaximumCount because "needs exactly 2" is the single most common hard failure across every surveyed application
//    (BridgeSpan, TargetWeld, SpinQuadFlow, MakeHole) and deserves its own diagnostic text.
struct SelectionPredicate
{
    StratumMask             AcceptedStrata       = StratumMask::AnyStratum;
    int                     MinimumCount         = 1;      // [idx] - Fewest components accepted
    int                     ExactCount           = 0;      // [idx] - Exact requirement; 0 disables the check
    ConnectivityRequirement RequiredConnectivity = ConnectivityRequirement::None;
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The stratum bit for one stratum, so a caller can build or test a mask without repeating the shift.
[[nodiscard]] StratumMask ResolveStratumBit(TopologyStratum Stratum);

// 📝 True when Mask admits Stratum.
[[nodiscard]] bool StratumAdmitted(StratumMask Mask, TopologyStratum Stratum);

// 📝 Evaluate one predicate against one selection and report the first shortfall found, checked in widening order: stratum, then count, then
//    connectivity. Order matters — a vertex selection tested against BevelFace must report StratumMismatch (hide the row) and never fall through
//    to a connectivity complaint about a fact that is irrelevant at that stratum.
[[nodiscard]] AvailabilityCondition EvaluateSelectionPredicate(const SelectionPredicate& Predicate, const SelectionProfile& Profile);

// 📝 Human-readable shortfall for a greyed row's reason chip — "needs 2 regions", "requires a quad ring". Returns nullptr when Condition is
//    Available or StratumMismatch (neither renders a chip: one is fine, the other is hidden). The text names the SHORTFALL, never the operation,
//    so one string serves every operation that shares the gate.
[[nodiscard]] const char* DescribeAvailabilityShortfall(const SelectionPredicate&  Predicate,
                                                        const SelectionProfile&    Profile,
                                                        AvailabilityCondition      Condition);

// 📝 The display name of a stratum ("Vertex", "Edge Loop") for a menu header.
[[nodiscard]] const char* DescribeTopologyStratum(TopologyStratum Stratum);

}   // namespace Frontier

#endif
