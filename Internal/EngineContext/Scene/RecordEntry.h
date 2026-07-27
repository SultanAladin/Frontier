/*==============================================================================================================================================
                                                                RECORDENTRY.H
==============================================================================================================================================*/
// 🧩 One item in the scene directory (never Node/Entity): the packed footprint carrying ONLY the concerns every item shares — generational
//    identity, enclosure/lateral connectivity, cached depth, ordering, title, local placement, classification, presentation bits, and lifecycle
//    condition. Domain payload hangs off ProfileToken via classification, so this footprint never grows when a new classification is added.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SCENE_RECORDENTRY_H
#define FRONTIER_ENGINECONTEXT_SCENE_RECORDENTRY_H

#include <cstdint>
#include <string>

#include "EngineContext/MicroUtils/RecordToken.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                              TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The lifecycle position of an occupied slot (folded from the source Lifecycle/RuntimeCondition). Dormant slots carry no
//    condition — occupancy already reports that. Active is a normal live item; Terminating is queued for reclamation, still
//    resolvable this cycle, swept on the next teardown pass.
enum class RuntimeCondition : uint8_t
{
    Active      = 0u,
    Terminating = 1u,
};

// 📝 A small dense identifier naming what an item IS (Folder, PolygonComplex, Light, Camera, SketchShape, …). The classification
//    registry owns the descriptor table; the entry stores only the index into it. Index 0 == unclassified Folder.
struct RecordClassification
{
    uint32_t ClassificationIndex = 0u;   // [idx] - slot in the classification registry (0 == unclassified folder)
};

// 📝 Extensible bit-set for per-item presentation, in place of a growing list of loose booleans. New bits append without
//    disturbing any serializer. Bit values are stable and never reordered.
enum class PresentationCategory : uint32_t
{
    None              = 0u,
    Displayed         = 1u << 0,   // renders in the viewport
    Locked            = 1u << 1,   // rejects selection / edit
    Selectable        = 1u << 2,   // eligible for picking
    Transient         = 1u << 3,   // excluded from serialization
    VisibleInOutliner = 1u << 4,   // appears as an outliner row
};

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 TRS in enclosure space. Kept as plain components; world placement resolves on demand up the enclosure chain rather than
//    stored, so no per-cycle fold is required. Matrix arithmetic lives in EngineContext/Math — this struct only holds values.
struct LocalPlacement
{
    float Location[3] = { 0.0f, 0.0f, 0.0f };   // [cm]  - position in enclosure space
    float Rotation[3] = { 0.0f, 0.0f, 0.0f };   // [°]   - Euler orientation in enclosure space
    float Scale[3]    = { 1.0f, 1.0f, 1.0f };   // [-]   - per-axis scale in enclosure space
};

// 📝 One directory item. Exposes SelfToken so the canonical TokenIssuer can stamp identity back onto the slot it mints. Carries
//    connectivity links as tokens (never pointers) so the store may reallocate freely without invalidating them.
struct RecordEntry
{
    // -- identity & connectivity (what makes the store a tree) --
    RecordToken SelfToken;                    // [-]   - this entry's own token (self-reference for convenience)
    RecordToken EnclosureToken;               // [-]   - enclosing entry; NullRecordToken == top-level
    RecordToken NestedRegionToken;            // [-]   - first entry nested inside this one (head of the nested list)
    RecordToken LateralSuccessorToken;        // [-]   - next entry under the same enclosure
    RecordToken LateralPredecessorToken;      // [-]   - previous entry under the same enclosure (doubly linked)
    uint32_t    DepthTier = 0u;               // [-]   - cached depth from top (top-level == 0)

    // -- ordering & title --
    uint32_t    OrderKey = 0u;                // [idx] - stable ordering hint within the same enclosure
    std::string Title;                        // [-]   - owned; no fixed-buffer truncation

    // -- placement --
    LocalPlacement Placement;                 // [-]   - TRS in enclosure space; world resolved on demand

    // -- classification & domain payload --
    RecordClassification Classification;      // [-]   - registered classification tag
    RecordToken          ProfileToken;        // [-]   - token into the classification's profile store (null == none)

    // -- presentation --
    PresentationCategory Presentation =
        static_cast<PresentationCategory>(static_cast<uint32_t>(PresentationCategory::Displayed) |
                                          static_cast<uint32_t>(PresentationCategory::Selectable) |
                                          static_cast<uint32_t>(PresentationCategory::VisibleInOutliner));

    // -- lifecycle --
    RuntimeCondition Condition = RuntimeCondition::Active;   // [-] - Terminating == queued for reclamation, not yet swept
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Combine two presentation bit-sets.
[[nodiscard]] constexpr PresentationCategory operator|(PresentationCategory Left, PresentationCategory Right) noexcept
{
    return static_cast<PresentationCategory>(static_cast<uint32_t>(Left) | static_cast<uint32_t>(Right));
}

// Intersect two presentation bit-sets.
[[nodiscard]] constexpr PresentationCategory operator&(PresentationCategory Left, PresentationCategory Right) noexcept
{
    return static_cast<PresentationCategory>(static_cast<uint32_t>(Left) & static_cast<uint32_t>(Right));
}

// True when every bit in Query is present in Bits.
[[nodiscard]] constexpr bool PresentationActive(PresentationCategory Bits, PresentationCategory Query) noexcept
{
    return (static_cast<uint32_t>(Bits) & static_cast<uint32_t>(Query)) == static_cast<uint32_t>(Query);
}

} // namespace Frontier

#endif
