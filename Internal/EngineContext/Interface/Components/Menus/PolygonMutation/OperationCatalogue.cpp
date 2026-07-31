/*==============================================================================================================================================
                                                          OPERATIONCATALOGUE.CPP
==============================================================================================================================================*/
// 🧩 The polygon-mutation operation table and the filter that resolves it against one selection. Each row's predicate mirrors the gate the surveyed
//    applications actually enforce, so a row greys out for the reason that application would refuse the command. Static data plus one linear pass —
//    no ImGui, no device, no allocation.

#include "OperationCatalogue.h"
#include "PolygonGlyphIdentity.h"      // Identities only — the catalogue names glyphs, it never draws them

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Shorthand so a catalogue row fits on one line and the gate stays readable as a column. Without these the table is
    //    95 rows of `StratumMask::` noise and the interesting part — which strata and how many — stops being scannable.
    constexpr StratumMask VertexOnly = StratumMask::Vertex;
    constexpr StratumMask EdgeOnly   = StratumMask::Edge;
    constexpr StratumMask FaceOnly   = StratumMask::Face;
    constexpr StratumMask LoopOnly   = StratumMask::EdgeLoop;
    constexpr StratumMask RingOnly   = StratumMask::EdgeRing;
    constexpr StratumMask BorderOnly = StratumMask::Border;
    constexpr StratumMask ObjectOnly = StratumMask::Object;

    constexpr StratumMask EdgeForms  = StratumMask::AnyEdgeForm;                          // Edge | Loop | Ring | Border
    constexpr StratumMask Components = StratumMask::AnyComponent;                         // The six sub-object strata
    constexpr StratumMask Everything = StratumMask::AnyStratum;                           // …plus Object
    constexpr StratumMask VertexEdge = StratumMask::Vertex | StratumMask::AnyEdgeForm;
    constexpr StratumMask EdgeFace   = StratumMask::AnyEdgeForm | StratumMask::Face;
    constexpr StratumMask FaceObject = StratumMask::Face | StratumMask::Object;
    constexpr StratumMask EdgeLoops  = StratumMask::Edge | StratumMask::EdgeLoop | StratumMask::EdgeRing;
    constexpr StratumMask LoopBorder = StratumMask::EdgeLoop | StratumMask::Border;

    using Requirement = ConnectivityRequirement;
    constexpr Requirement NoRequirement = Requirement::None;

    // 📝 Build one predicate. Positional rather than designated so the table's gate column aligns; the argument order is the
    //    same widening order EvaluateSelectionPredicate checks in (strata → minimum → exact → connectivity).
    constexpr SelectionPredicate Gate(StratumMask Strata,
                                      int         MinimumCount   = 1,
                                      int         ExactCount     = 0,
                                      Requirement RequiredFacts  = Requirement::None)
    {
        return SelectionPredicate{ Strata, MinimumCount, ExactCount, RequiredFacts };
    }

    constexpr OperationSeverity Plain   = OperationSeverity::Neutral;
    constexpr OperationSeverity Primary = OperationSeverity::Emphasised;
    constexpr OperationSeverity Severe  = OperationSeverity::Destructive;

    using Glyph = PolygonGlyph;
    using Band  = OperationBand;

    // 📝 Branch captions. Declared once as constants because a row's BranchCaption and the branch's own rows must agree
    //    exactly — the menu groups a branch by string identity, so a typo would silently orphan the branch's contents.
    constexpr const char* AlignBranch    = "Align To";
    constexpr const char* MirrorBranch   = "Mirror Axis";
    constexpr const char* MergeBranch    = "Merge Target";
    constexpr const char* MaterialBranch = "Assign Material";
    constexpr const char* TraitBranch    = "Similar Trait";

    // 🔴 THE TABLE. Ordered by band, and within a band by how often the operation is reached for — the menu emits rows in this
    //    order verbatim, so the table IS the layout. A row's Predicate is the whole of its availability contract; nothing
    //    elsewhere may add or relax a condition, which is what keeps the menu, a toolbar and a palette agreeing.
    constexpr TopologyOperationDescriptor OperationTable[] =
    {
        //-------------------------------------------------------------- TRANSFORM --------------------------------------------------------------
        { "Translate",                  "G",        (uint8_t)Glyph::TranslateArrows,   Band::Transform, Primary, Gate(Everything),                                                                nullptr        },
        { "Rotate",                     "R",        (uint8_t)Glyph::RotateArc,         Band::Transform, Plain,   Gate(Everything),                                                                nullptr        },
        { "Scale",                      "S",        (uint8_t)Glyph::ScaleCorner,       Band::Transform, Plain,   Gate(Everything),                                                                nullptr        },
        { "Slide Along Adjacency",      "GG",       (uint8_t)Glyph::SlideTrack,        Band::Transform, Plain,   Gate(VertexEdge),                                                                nullptr        },
        { "Flatten To Plane",           nullptr,    (uint8_t)Glyph::FlattenPlane,      Band::Transform, Plain,   Gate(Components, 3),                                                             nullptr        },
        { "Align To Axis",              nullptr,    (uint8_t)Glyph::AxisMarker,        Band::Transform, Plain,   Gate(Components, 2),                                                             AlignBranch    },
        { "Circularize Loop",           nullptr,    (uint8_t)Glyph::CircleFit,         Band::Transform, Plain,   Gate(LoopBorder, 3, 0, Requirement::ClosedBoundaryLoop),                         nullptr        },
        { "Equalize Edge Span",         nullptr,    (uint8_t)Glyph::SpanEven,          Band::Transform, Plain,   Gate(EdgeForms, 2),                                                              nullptr        },
        { "Straighten Edge Chain",      nullptr,    (uint8_t)Glyph::StraightenLine,    Band::Transform, Plain,   Gate(EdgeForms, 3),                                                              nullptr        },

        //----------------------------------------------------------- EXTRUDE / BUILD -----------------------------------------------------------
        { "Extrude Region",             "E",        (uint8_t)Glyph::ExtrudeOut,        Band::ExtrudeBuild, Primary, Gate(FaceOnly),                                                               nullptr        },
        { "Extrude Individual",         "Alt+E",    (uint8_t)Glyph::ExtrudeOut,        Band::ExtrudeBuild, Plain,   Gate(FaceOnly, 2),                                                            nullptr        },
        { "Extrude Vertex Spike",       nullptr,    (uint8_t)Glyph::ExtrudeSpike,      Band::ExtrudeBuild, Plain,   Gate(VertexOnly),                                                             nullptr        },
        { "Extrude Edge Wall",          nullptr,    (uint8_t)Glyph::ExtrudeOut,        Band::ExtrudeBuild, Plain,   Gate(EdgeForms),                                                              nullptr        },
        { "Inset Faces",                "I",        (uint8_t)Glyph::InsetRing,         Band::ExtrudeBuild, Primary, Gate(FaceOnly),                                                               nullptr        },
        { "Outline Border",             nullptr,    (uint8_t)Glyph::OutlineRing,       Band::ExtrudeBuild, Plain,   Gate(BorderOnly, 3, 0, Requirement::ClosedBoundaryLoop),                      nullptr        },
        { "Bevel Vertex",               nullptr,    (uint8_t)Glyph::BevelChamfer,      Band::ExtrudeBuild, Plain,   Gate(VertexOnly, 1, 0, Requirement::ManifoldCondition),                       nullptr        },
        { "Bevel Edge",                 "Ctrl+B",   (uint8_t)Glyph::BevelChamfer,      Band::ExtrudeBuild, Primary, Gate(EdgeForms, 1, 0, Requirement::ManifoldCondition),                        nullptr        },
        { "Bevel Face",                 nullptr,    (uint8_t)Glyph::BevelChamfer,      Band::ExtrudeBuild, Plain,   Gate(FaceOnly, 1, 0, Requirement::ManifoldCondition),                         nullptr        },
        { "Bridge Span",                nullptr,    (uint8_t)Glyph::BridgeSpan,        Band::ExtrudeBuild, Plain,   Gate(EdgeFace, 2, 2),                                                        nullptr        },
        { "Fill Boundary",              "F",        (uint8_t)Glyph::FillPatch,         Band::ExtrudeBuild, Plain,   Gate(LoopBorder, 3, 0, Requirement::ClosedBoundaryLoop),                      nullptr        },
        { "Grid Fill Boundary",         nullptr,    (uint8_t)Glyph::GridPatch,         Band::ExtrudeBuild, Plain,   Gate(LoopBorder, 4, 0, Requirement::ClosedBoundaryLoop | Requirement::EvenBoundaryParity), nullptr },
        { "Solidify",                   nullptr,    (uint8_t)Glyph::ShellThickness,    Band::ExtrudeBuild, Plain,   Gate(FaceObject, 1, 0, Requirement::OpenSurface),                             nullptr        },
        { "Intrude",                    nullptr,    (uint8_t)Glyph::ShellThickness,    Band::ExtrudeBuild, Plain,   Gate(FaceOnly),                                                               nullptr        },
        { "Sweep Profile",              nullptr,    (uint8_t)Glyph::SweepPath,         Band::ExtrudeBuild, Plain,   Gate(EdgeFace),                                                               nullptr        },
        { "Grow Quad Row",              nullptr,    (uint8_t)Glyph::GridPatch,         Band::ExtrudeBuild, Plain,   Gate(BorderOnly, 2),                                                          nullptr        },

        //-------------------------------------------------------------- CUT / SPLIT -------------------------------------------------------------
        { "Insert Edge Loop",           "Ctrl+R",   (uint8_t)Glyph::LoopInsert,        Band::CutSplit, Primary, Gate(RingOnly, 1, 0, Requirement::QuadRingWalkable),                              nullptr        },
        { "Offset Edge Loop",           nullptr,    (uint8_t)Glyph::LoopOffset,        Band::CutSplit, Plain,   Gate(LoopOnly, 2, 0, Requirement::QuadRingWalkable),                              nullptr        },
        { "Subdivide Selection",        nullptr,    (uint8_t)Glyph::SubdivideQuads,    Band::CutSplit, Plain,   Gate(EdgeFace),                                                                   nullptr        },
        { "Subdivide Edge Ring",        nullptr,    (uint8_t)Glyph::SubdivideQuads,    Band::CutSplit, Plain,   Gate(RingOnly, 1, 0, Requirement::QuadRingWalkable),                               nullptr        },
        { "Un-Subdivide",               nullptr,    (uint8_t)Glyph::UnSubdivideMerge,  Band::CutSplit, Plain,   Gate(FaceObject, 4),                                                              nullptr        },
        { "Connect Vertex Path",        "J",        (uint8_t)Glyph::ConnectPath,       Band::CutSplit, Plain,   Gate(VertexOnly, 2, 0, Requirement::SharedComponent),                             nullptr        },
        { "Connect Edge Midpoints",     nullptr,    (uint8_t)Glyph::ConnectPath,       Band::CutSplit, Plain,   Gate(EdgeLoops, 2),                                                               nullptr        },
        { "Split Edge Seam",            "V",        (uint8_t)Glyph::SeamSplit,         Band::CutSplit, Plain,   Gate(EdgeForms, 1, 0, Requirement::TwoFaceEdge),                                  nullptr        },
        { "Rip Vertices",               nullptr,    (uint8_t)Glyph::RipApart,          Band::CutSplit, Plain,   Gate(VertexOnly, 1, 0, Requirement::SharedComponent),                              nullptr        },
        { "Poke Face Centre",           nullptr,    (uint8_t)Glyph::PokeCentre,        Band::CutSplit, Plain,   Gate(FaceOnly),                                                                   nullptr        },
        { "Bisect Plane",               nullptr,    (uint8_t)Glyph::BisectPlane,       Band::CutSplit, Plain,   Gate(FaceObject),                                                                 nullptr        },
        { "Partition Boolean",          nullptr,    (uint8_t)Glyph::PartitionSolid,    Band::CutSplit, Plain,   Gate(ObjectOnly, 1, 0, Requirement::MultipleShell),                               nullptr        },

        //------------------------------------------------------------- MERGE / WELD -------------------------------------------------------------
        { "Merge At Centroid",          "M",        (uint8_t)Glyph::MergeInward,       Band::MergeWeld, Primary, Gate(Components, 2),                                                             MergeBranch    },
        { "Merge At First",             nullptr,    (uint8_t)Glyph::MergeInward,       Band::MergeWeld, Plain,   Gate(VertexOnly, 2),                                                             MergeBranch    },
        { "Merge At Last",              nullptr,    (uint8_t)Glyph::MergeInward,       Band::MergeWeld, Plain,   Gate(VertexOnly, 2),                                                             MergeBranch    },
        { "Merge By Proximity",         nullptr,    (uint8_t)Glyph::MergeInward,       Band::MergeWeld, Plain,   Gate(VertexEdge, 2),                                                             MergeBranch    },
        { "Target Weld",                nullptr,    (uint8_t)Glyph::WeldTarget,        Band::MergeWeld, Plain,   Gate(VertexEdge, 2, 2),                                                          nullptr        },
        { "Collapse Selection",         "Alt+M",    (uint8_t)Glyph::CollapseDown,      Band::MergeWeld, Plain,   Gate(Components, 2),                                                             nullptr        },
        { "Merge Coplanar Faces",       nullptr,    (uint8_t)Glyph::CoplanarUnify,     Band::MergeWeld, Plain,   Gate(FaceOnly, 2, 0, Requirement::SharedComponent),                              nullptr        },

        //------------------------------------------------------------- SUBDIVISION --------------------------------------------------------------
        { "Subdivision Surface Refine", nullptr,    (uint8_t)Glyph::SmoothSurface,     Band::Subdivision, Primary, Gate(FaceObject),                                                             nullptr        },
        { "Smooth Vertices",            nullptr,    (uint8_t)Glyph::SmoothSurface,     Band::Subdivision, Plain,   Gate(Components, 3),                                                          nullptr        },
        { "Relax To Neighbour Mean",    nullptr,    (uint8_t)Glyph::RelaxNeighbour,    Band::Subdivision, Plain,   Gate(Components, 3),                                                          nullptr        },
        { "Two-Step Feature Smooth",    nullptr,    (uint8_t)Glyph::SmoothSurface,     Band::Subdivision, Plain,   Gate(FaceObject, 3),                                                          nullptr        },
        { "Sharpen Inverse Smooth",     nullptr,    (uint8_t)Glyph::SharpenInverse,    Band::Subdivision, Plain,   Gate(Components, 3),                                                          nullptr        },
        { "Decimate By Quadric Collapse", nullptr,  (uint8_t)Glyph::DecimateSparse,    Band::Subdivision, Plain,   Gate(FaceObject, 4),                                                          nullptr        },
        { "Remesh Isotropic",           nullptr,    (uint8_t)Glyph::RemeshUniform,     Band::Subdivision, Plain,   Gate(FaceObject, 4),                                                          nullptr        },

        //----------------------------------------------------------- NORMALS / SHADING ----------------------------------------------------------
        { "Reverse Winding",            nullptr,    (uint8_t)Glyph::WindingReverse,    Band::NormalsShading, Primary, Gate(FaceObject),                                                          nullptr        },
        { "Recalculate Normals Outward", "Shift+N", (uint8_t)Glyph::NormalArrow,       Band::NormalsShading, Plain,   Gate(FaceObject),                                                          nullptr        },
        { "Unify Winding To Reference", nullptr,    (uint8_t)Glyph::NormalArrow,       Band::NormalsShading, Plain,   Gate(FaceOnly, 2),                                                         nullptr        },
        { "Shade Smooth",               nullptr,    (uint8_t)Glyph::ShadeSmoothCurve,  Band::NormalsShading, Plain,   Gate(FaceObject),                                                          nullptr        },
        { "Shade Faceted",              nullptr,    (uint8_t)Glyph::ShadeFacet,        Band::NormalsShading, Plain,   Gate(FaceObject),                                                          nullptr        },
        { "Mark Sharp Edge",            nullptr,    (uint8_t)Glyph::SharpEdgeMark,     Band::NormalsShading, Plain,   Gate(EdgeForms),                                                           nullptr        },

        //-------------------------------------------------------------- ATTRIBUTES --------------------------------------------------------------
        { "Assign Edge Crease",         nullptr,    (uint8_t)Glyph::CreaseWeight,      Band::Attributes, Plain, Gate(EdgeForms),                                                                 nullptr        },
        { "Assign Vertex Crease",       nullptr,    (uint8_t)Glyph::CreaseWeight,      Band::Attributes, Plain, Gate(VertexOnly),                                                                nullptr        },
        { "Assign Bevel Weight",        nullptr,    (uint8_t)Glyph::CreaseWeight,      Band::Attributes, Plain, Gate(EdgeForms),                                                                 nullptr        },
        { "Mark UV Seam",               nullptr,    (uint8_t)Glyph::UvSeamMark,        Band::Attributes, Plain, Gate(EdgeForms),                                                                 nullptr        },
        { "Assign Surface Material",    nullptr,    (uint8_t)Glyph::MaterialSwatch,    Band::Attributes, Plain, Gate(FaceObject),                                                                MaterialBranch },

        //----------------------------------------------------------- TOPOLOGY REPAIR ------------------------------------------------------------
        { "Triangulate Faces",          "Ctrl+T",   (uint8_t)Glyph::TriangulateSplit,  Band::TopologyRepair, Plain, Gate(FaceObject, 1, 0, Requirement::NonPlanarFace),                           nullptr        },
        { "Quadrangulate Triangle Pairs", "Alt+J",  (uint8_t)Glyph::QuadrangulatePair, Band::TopologyRepair, Plain, Gate(FaceObject, 2, 0, Requirement::AdjacentTrianglePair),                    nullptr        },
        { "Make Faces Planar",          nullptr,    (uint8_t)Glyph::PlanarCorrect,     Band::TopologyRepair, Plain, Gate(FaceOnly, 1, 0, Requirement::NonPlanarFace),                             nullptr        },
        { "Spin Edge Diagonal",         nullptr,    (uint8_t)Glyph::SpinDiagonal,      Band::TopologyRepair, Plain, Gate(EdgeForms, 1, 0, Requirement::TwoFaceEdge),                              nullptr        },
        { "Spin Quad Flow",             nullptr,    (uint8_t)Glyph::SpinDiagonal,      Band::TopologyRepair, Plain, Gate(FaceOnly, 2, 2, Requirement::QuadPairMatched),                           nullptr        },
        { "Fill Detected Holes",        nullptr,    (uint8_t)Glyph::HoleSeal,          Band::TopologyRepair, Plain, Gate(FaceObject, 1, 0, Requirement::OpenSurface),                             nullptr        },
        { "Reclaim Loose Geometry",     nullptr,    (uint8_t)Glyph::LooseReclaim,      Band::TopologyRepair, Plain, Gate(ObjectOnly),                                                            nullptr        },
        { "Repair Non-Manifold Edges",  nullptr,    (uint8_t)Glyph::NonManifoldRepair, Band::TopologyRepair, Plain, Gate(ObjectOnly),                                                            nullptr        },
        { "Convex Hull Of Selection",   nullptr,    (uint8_t)Glyph::HullWrap,          Band::TopologyRepair, Plain, Gate(Components, 4),                                                         nullptr        },

        //--------------------------------------------------------- DUPLICATE / SYMMETRY ---------------------------------------------------------
        { "Duplicate Selection",        "Shift+D",  (uint8_t)Glyph::DuplicateOffset,   Band::DuplicateSymmetry, Primary, Gate(Everything),                                                       nullptr        },
        { "Separate Shell",             "P",        (uint8_t)Glyph::ShellSeparate,     Band::DuplicateSymmetry, Plain,   Gate(FaceObject, 1, 0, Requirement::MultipleShell),                      nullptr        },
        { "Extract To New Surface",     nullptr,    (uint8_t)Glyph::ExtractSurface,    Band::DuplicateSymmetry, Plain,   Gate(FaceOnly),                                                         nullptr        },
        { "Detach Component",           nullptr,    (uint8_t)Glyph::DetachComponent,   Band::DuplicateSymmetry, Plain,   Gate(Components, 1, 0, Requirement::SharedComponent),                    nullptr        },
        { "Break Shared Vertex",        nullptr,    (uint8_t)Glyph::DetachComponent,   Band::DuplicateSymmetry, Plain,   Gate(VertexOnly, 1, 0, Requirement::SharedComponent),                    nullptr        },
        { "Mirror Across Axis",         "Ctrl+M",   (uint8_t)Glyph::MirrorAxis,        Band::DuplicateSymmetry, Plain,   Gate(Everything),                                                       MirrorBranch   },
        { "Symmetrize Across Axis",     nullptr,    (uint8_t)Glyph::SymmetryBalance,   Band::DuplicateSymmetry, Plain,   Gate(FaceObject, 1, 0, Requirement::SymmetryAxis),                       MirrorBranch   },
        { "Snap To Symmetry Plane",     nullptr,    (uint8_t)Glyph::SymmetryBalance,   Band::DuplicateSymmetry, Plain,   Gate(Components, 1, 0, Requirement::SymmetryAxis),                       MirrorBranch   },

        //-------------------------------------------------------- SELECTION CONVERSION ----------------------------------------------------------
        { "Grow",                       "Ctrl+Num+", (uint8_t)Glyph::SelectionGrow,    Band::SelectionConversion, Plain, Gate(Components),                                                       nullptr        },
        { "Shrink",                     "Ctrl+Num-", (uint8_t)Glyph::SelectionShrink,  Band::SelectionConversion, Plain, Gate(Components),                                                       nullptr        },
        { "Select Edge Loop",           "Alt+Click", (uint8_t)Glyph::LoopHighlight,    Band::SelectionConversion, Primary, Gate(EdgeForms),                                                      nullptr        },
        { "Select Edge Ring",           nullptr,    (uint8_t)Glyph::RingHighlight,     Band::SelectionConversion, Plain, Gate(EdgeForms),                                                        nullptr        },
        { "Promote Stratum",            nullptr,    (uint8_t)Glyph::StratumPromote,    Band::SelectionConversion, Plain, Gate(Components),                                                       nullptr        },
        { "Select Linked Shell",        "Ctrl+L",   (uint8_t)Glyph::LinkedShell,       Band::SelectionConversion, Plain, Gate(Components),                                                       nullptr        },
        { "Select Similar By Trait",    "Shift+G",  (uint8_t)Glyph::SimilarTrait,      Band::SelectionConversion, Plain, Gate(Components),                                                       TraitBranch    },
        { "Select Boundary Edges",      nullptr,    (uint8_t)Glyph::BoundaryTrace,     Band::SelectionConversion, Plain, Gate(EdgeFace, 1, 0, Requirement::OpenSurface),                          nullptr        },
        { "Checker Deselect",           nullptr,    (uint8_t)Glyph::CheckerSkip,       Band::SelectionConversion, Plain, Gate(Components, 3),                                                    nullptr        },
        { "Select Shortest Path",       nullptr,    (uint8_t)Glyph::ShortestPath,      Band::SelectionConversion, Plain, Gate(Components, 2, 2),                                                 nullptr        },
        { "Select By Trait",            nullptr,    (uint8_t)Glyph::TraitFilter,       Band::SelectionConversion, Plain, Gate(Components),                                                       TraitBranch    },

        //--------------------------------------------------------------- REMOVAL ----------------------------------------------------------------
        { "Delete With Dependents",     "X",        (uint8_t)Glyph::RemoveCross,       Band::Removal, Severe, Gate(Everything),                                                                  nullptr        },
        { "Dissolve Into Neighbours",   "Ctrl+X",   (uint8_t)Glyph::DissolveFade,      Band::Removal, Severe, Gate(Components, 1, 0, Requirement::SharedComponent),                              nullptr        },
        { "Limited Dissolve By Angle",  nullptr,    (uint8_t)Glyph::DissolveAngle,     Band::Removal, Severe, Gate(EdgeFace, 2, 0, Requirement::SharedComponent),                                nullptr        },
        { "Degenerate Dissolve",        nullptr,    (uint8_t)Glyph::DissolveFade,      Band::Removal, Severe, Gate(ObjectOnly),                                                                  nullptr        }
    };

    constexpr int OperationTableCount = static_cast<int>(sizeof(OperationTable) / sizeof(OperationTable[0]));
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const TopologyOperationDescriptor* ResolveOperationCatalogue(int& OperationCount)
{
    OperationCount = OperationTableCount;
    return OperationTable;
}

const char* DescribeOperationBand(OperationBand Band)
{
    switch (Band)
    {
    case OperationBand::Transform:           return "TRANSFORM";
    case OperationBand::ExtrudeBuild:        return "EXTRUDE / BUILD";
    case OperationBand::CutSplit:            return "CUT / SPLIT";
    case OperationBand::MergeWeld:           return "MERGE / WELD";
    case OperationBand::Subdivision:         return "SUBDIVISION";
    case OperationBand::NormalsShading:      return "NORMALS / SHADING";
    case OperationBand::Attributes:          return "ATTRIBUTES";
    case OperationBand::TopologyRepair:      return "TOPOLOGY REPAIR";
    case OperationBand::DuplicateSymmetry:   return "DUPLICATE / SYMMETRY";
    case OperationBand::SelectionConversion: return "SELECTION";
    case OperationBand::Removal:             return "REMOVAL";
    default:                                 return "OPERATIONS";
    }
}

int FilterOperationCatalogue(const SelectionProfile& Profile,
                             bool                    RevealGatedEntries,
                             ResolvedOperationEntry* Entries,
                             int                     EntryCapacity)
{
    if (Entries == nullptr || EntryCapacity <= 0)
    {
        return 0;
    }

    int WrittenCount = 0;
    for (int Index = 0; Index < OperationTableCount && WrittenCount < EntryCapacity; ++Index)
    {
        const TopologyOperationDescriptor& Descriptor = OperationTable[Index];
        const AvailabilityCondition Condition = EvaluateSelectionPredicate(Descriptor.Predicate, Profile);

        // A stratum mismatch drops the row unconditionally — a vertex menu listing "Reverse Winding" is noise, not information.
        if (Condition == AvailabilityCondition::StratumMismatch)
        {
            continue;
        }
        if (!RevealGatedEntries && Condition != AvailabilityCondition::Available)
        {
            continue;
        }

        // ⚠️ Shortfall text comes from a rotating static buffer, so the chip must be consumed before the buffer wraps. The
        //    renderer draws each row as it walks this array, well inside four live strings, but a caller that retained the
        //    whole filtered array across many frames would be reading aliased text — filter per frame, as the menu does.
        ResolvedOperationEntry& Entry = Entries[WrittenCount++];
        Entry.Descriptor    = &Descriptor;
        Entry.Condition     = Condition;
        Entry.ShortfallText = DescribeAvailabilityShortfall(Descriptor.Predicate, Profile, Condition);
    }
    return WrittenCount;
}

void TallyOperationAvailability(const SelectionProfile& Profile, int& AvailableCount, int& GatedCount, int& HiddenCount)
{
    AvailableCount = 0;
    GatedCount     = 0;
    HiddenCount    = 0;

    for (int Index = 0; Index < OperationTableCount; ++Index)
    {
        switch (EvaluateSelectionPredicate(OperationTable[Index].Predicate, Profile))
        {
        case AvailabilityCondition::Available:       ++AvailableCount; break;
        case AvailabilityCondition::StratumMismatch: ++HiddenCount;    break;
        default:                                     ++GatedCount;     break;
        }
    }
}

}   // namespace Frontier
