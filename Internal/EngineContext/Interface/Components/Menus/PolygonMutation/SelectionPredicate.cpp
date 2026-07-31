/*==============================================================================================================================================
                                                            SELECTIONPREDICATE.CPP
==============================================================================================================================================*/
// 🧩 Gate arithmetic for polygon-mutation operations: stratum admission, selection-count bounds, and connectivity facts, resolved to one
//    AvailabilityCondition plus the explanatory text a greyed menu row shows. Pure integer + bitmask work — no ImGui, no device, no allocation.

#include "SelectionPredicate.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Bitmask helpers. StratumMask and ConnectivityRequirement are enum classes for type safety, so the underlying-integer
    //    arithmetic is funnelled through these two rather than scattering static_casts across every predicate check.
    constexpr uint16_t MaskBits(StratumMask Mask)
    {
        return static_cast<uint16_t>(Mask);
    }

    constexpr uint16_t RequirementBits(ConnectivityRequirement Requirement)
    {
        return static_cast<uint16_t>(Requirement);
    }

    constexpr bool RequirementPresent(ConnectivityRequirement Declared, ConnectivityRequirement Probe)
    {
        return (RequirementBits(Declared) & RequirementBits(Probe)) != 0u;
    }

    // 📝 Resolve whether one connectivity requirement is satisfied by the profile. Split out so both the evaluation pass and the
    //    diagnostic pass walk the SAME satisfaction rule — a diagnostic that disagreed with the gate would be worse than none.
    bool ConnectivitySatisfied(ConnectivityRequirement Probe, const SelectionProfile& Profile)
    {
        switch (Probe)
        {
        case ConnectivityRequirement::QuadRingWalkable:     return Profile.QuadRingCondition;
        case ConnectivityRequirement::ClosedBoundaryLoop:   return Profile.BoundaryCondition && Profile.ClosedLoopCondition;
        case ConnectivityRequirement::EvenBoundaryParity:   return Profile.BoundaryEdgeCount > 0 && (Profile.BoundaryEdgeCount % 2) == 0;
        case ConnectivityRequirement::ManifoldCondition:    return Profile.ManifoldCondition;
        case ConnectivityRequirement::SharedComponent:      return Profile.SharedComponentCondition;
        case ConnectivityRequirement::TwoFaceEdge:          return Profile.TwoFaceEdgeCondition;
        case ConnectivityRequirement::QuadPairMatched:      return Profile.QuadPairCondition;
        case ConnectivityRequirement::AdjacentTrianglePair: return Profile.TrianglePairCondition;
        case ConnectivityRequirement::NonPlanarFace:        return Profile.MaximumFaceSideCount >= 4;
        case ConnectivityRequirement::MultipleShell:        return Profile.ShellCount >= 2;
        case ConnectivityRequirement::OpenSurface:          return Profile.OpenSurfaceCondition;
        case ConnectivityRequirement::SymmetryAxis:         return Profile.SymmetryAxisCondition;
        default:                                            return true;
        }
    }

    // 📝 The requirement bits in diagnostic priority order. When several facts are missing at once the row shows the FIRST of these,
    //    ordered so the most structural complaint wins — a caller told "requires a quad ring" acts on it, whereas being told about
    //    boundary parity on a selection that has no boundary at all sends them chasing the wrong fix.
    constexpr ConnectivityRequirement DiagnosticOrder[] =
    {
        ConnectivityRequirement::QuadRingWalkable,
        ConnectivityRequirement::ClosedBoundaryLoop,
        ConnectivityRequirement::EvenBoundaryParity,
        ConnectivityRequirement::MultipleShell,
        ConnectivityRequirement::OpenSurface,
        ConnectivityRequirement::TwoFaceEdge,
        ConnectivityRequirement::QuadPairMatched,
        ConnectivityRequirement::AdjacentTrianglePair,
        ConnectivityRequirement::NonPlanarFace,
        ConnectivityRequirement::SharedComponent,
        ConnectivityRequirement::ManifoldCondition,
        ConnectivityRequirement::SymmetryAxis
    };

    constexpr int DiagnosticOrderCount = static_cast<int>(sizeof(DiagnosticOrder) / sizeof(DiagnosticOrder[0]));

    // 📝 The shortfall wording for one unmet requirement. Phrased as what the selection LACKS, never as the operation's name, so a
    //    single string serves every operation sharing the gate (nine operations share QuadRingWalkable alone).
    const char* DescribeConnectivityShortfall(ConnectivityRequirement Probe)
    {
        switch (Probe)
        {
        case ConnectivityRequirement::QuadRingWalkable:     return "requires a quad ring";
        case ConnectivityRequirement::ClosedBoundaryLoop:   return "requires an open boundary";
        case ConnectivityRequirement::EvenBoundaryParity:   return "requires an even boundary";
        case ConnectivityRequirement::ManifoldCondition:    return "requires manifold edges";
        case ConnectivityRequirement::SharedComponent:      return "requires shared components";
        case ConnectivityRequirement::TwoFaceEdge:          return "requires a two-face edge";
        case ConnectivityRequirement::QuadPairMatched:      return "requires matched quads";
        case ConnectivityRequirement::AdjacentTrianglePair: return "requires adjacent triangles";
        case ConnectivityRequirement::NonPlanarFace:        return "requires a four-sided face";
        case ConnectivityRequirement::MultipleShell:        return "requires two shells";
        case ConnectivityRequirement::OpenSurface:          return "requires an open surface";
        case ConnectivityRequirement::SymmetryAxis:         return "requires a symmetry axis";
        default:                                            return "unavailable";
        }
    }

    // 📝 Count nouns per stratum so a shortfall chip reads "needs 2 loops" rather than "needs 2 components". Plural form only —
    //    every count diagnostic that uses it concerns 2 or more by construction.
    const char* DescribeStratumPlural(TopologyStratum Stratum)
    {
        switch (Stratum)
        {
        case TopologyStratum::Vertex:   return "vertices";
        case TopologyStratum::Edge:     return "edges";
        case TopologyStratum::Face:     return "faces";
        case TopologyStratum::EdgeLoop: return "loops";
        case TopologyStratum::EdgeRing: return "rings";
        case TopologyStratum::Border:   return "borders";
        case TopologyStratum::Object:   return "objects";
        default:                        return "components";
        }
    }

    // 📝 Shortfall text is assembled into a rotating set of small static buffers rather than returned as a std::string: the menu calls
    //    this once per greyed row per frame, so a per-call allocation would be ~60 allocations a frame for nothing. Four slots is more
    //    than the one-live-string-at-a-time the renderer actually needs, and gives headroom if a caller holds two chips side by side.
    //    ⚠️ Not thread-safe by design — UI recording is single-threaded here.
    constexpr int ShortfallSlotCount  = 4;
    constexpr int ShortfallSlotLength = 48;
    char ShortfallText[ShortfallSlotCount][ShortfallSlotLength] = {};
    int  ShortfallSlotCursor = 0;

    // 📝 Format "needs N <plural>" into the next rotating slot. Hand-rolled rather than snprintf to keep this translation unit free of
    //    <cstdio> — the only formatting ever needed is one small non-negative integer.
    const char* FormatCountShortfall(int RequiredCount, const char* Plural)
    {
        char* Slot = ShortfallText[ShortfallSlotCursor];
        ShortfallSlotCursor = (ShortfallSlotCursor + 1) % ShortfallSlotCount;

        int Written = 0;
        const char* Prefix = "needs ";
        for (const char* Cursor = Prefix; *Cursor != '\0' && Written < ShortfallSlotLength - 1; ++Cursor)
        {
            Slot[Written++] = *Cursor;
        }

        // Decimal digits, most significant first. RequiredCount is a small selection bound, never negative.
        int Clamped = RequiredCount < 0 ? 0 : RequiredCount;
        char Digits[12] = {};
        int  DigitCount = 0;
        do
        {
            Digits[DigitCount++] = static_cast<char>('0' + (Clamped % 10));
            Clamped /= 10;
        }
        while (Clamped > 0 && DigitCount < 11);

        while (DigitCount > 0 && Written < ShortfallSlotLength - 1)
        {
            Slot[Written++] = Digits[--DigitCount];
        }

        if (Written < ShortfallSlotLength - 1)
        {
            Slot[Written++] = ' ';
        }
        for (const char* Cursor = Plural; *Cursor != '\0' && Written < ShortfallSlotLength - 1; ++Cursor)
        {
            Slot[Written++] = *Cursor;
        }

        Slot[Written] = '\0';
        return Slot;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

StratumMask ResolveStratumBit(TopologyStratum Stratum)
{
    const int Index = static_cast<int>(Stratum);
    if (Index < 0 || Index >= static_cast<int>(TopologyStratum::StratumCount))
    {
        return StratumMask::None;
    }
    return static_cast<StratumMask>(1u << Index);
}

bool StratumAdmitted(StratumMask Mask, TopologyStratum Stratum)
{
    return (MaskBits(Mask) & MaskBits(ResolveStratumBit(Stratum))) != 0u;
}

AvailabilityCondition EvaluateSelectionPredicate(const SelectionPredicate& Predicate, const SelectionProfile& Profile)
{
    // ① Stratum first. A mismatch here means the operation is meaningless for this selection, so the row is hidden rather than
    //    greyed — and no count or connectivity complaint may be raised about facts that do not apply at this stratum.
    if (!StratumAdmitted(Predicate.AcceptedStrata, Profile.ActiveStratum))
    {
        return AvailabilityCondition::StratumMismatch;
    }

    // ② Count. ExactCount, when declared, overrides the minimum: "exactly 2" is a different diagnostic from "at least 2".
    //    🔴 DistinctRegionCount rather than SelectedCount carries the exact test — BridgeSpan wants two REGIONS, and a
    //    twelve-edge selection forming two loops must pass where a two-edge selection inside one loop must not.
    if (Predicate.ExactCount > 0)
    {
        const int MeasuredCount = (Predicate.ExactCount > 1) ? Profile.DistinctRegionCount : Profile.SelectedCount;
        if (MeasuredCount != Predicate.ExactCount)
        {
            return AvailabilityCondition::CountShortfall;
        }
    }
    else if (Profile.SelectedCount < Predicate.MinimumCount)
    {
        return AvailabilityCondition::CountShortfall;
    }

    // ③ Connectivity. Every declared fact must hold; the first missing one decides the diagnostic.
    if (Predicate.RequiredConnectivity != ConnectivityRequirement::None)
    {
        for (int Index = 0; Index < DiagnosticOrderCount; ++Index)
        {
            const ConnectivityRequirement Probe = DiagnosticOrder[Index];
            if (RequirementPresent(Predicate.RequiredConnectivity, Probe) && !ConnectivitySatisfied(Probe, Profile))
            {
                return AvailabilityCondition::TopologyShortfall;
            }
        }
    }

    return AvailabilityCondition::Available;
}

const char* DescribeAvailabilityShortfall(const SelectionPredicate& Predicate,
                                          const SelectionProfile&   Profile,
                                          AvailabilityCondition     Condition)
{
    if (Condition == AvailabilityCondition::CountShortfall)
    {
        const char* Plural = DescribeStratumPlural(Profile.ActiveStratum);
        if (Predicate.ExactCount > 0)
        {
            return FormatCountShortfall(Predicate.ExactCount, Plural);
        }
        return FormatCountShortfall(Predicate.MinimumCount, Plural);
    }

    if (Condition == AvailabilityCondition::TopologyShortfall)
    {
        for (int Index = 0; Index < DiagnosticOrderCount; ++Index)
        {
            const ConnectivityRequirement Probe = DiagnosticOrder[Index];
            if (RequirementPresent(Predicate.RequiredConnectivity, Probe) && !ConnectivitySatisfied(Probe, Profile))
            {
                return DescribeConnectivityShortfall(Probe);
            }
        }
        return "unavailable";
    }

    // Available rows need no chip; hidden rows are never drawn.
    return nullptr;
}

const char* DescribeTopologyStratum(TopologyStratum Stratum)
{
    switch (Stratum)
    {
    case TopologyStratum::Vertex:   return "Vertex";
    case TopologyStratum::Edge:     return "Edge";
    case TopologyStratum::Face:     return "Face";
    case TopologyStratum::EdgeLoop: return "Edge Loop";
    case TopologyStratum::EdgeRing: return "Edge Ring";
    case TopologyStratum::Border:   return "Border";
    case TopologyStratum::Object:   return "Object";
    case TopologyStratum::Nothing:  return "Nothing";
    default:                        return "Selection";
    }
}

}   // namespace Frontier
