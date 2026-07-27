/*============================================================================================================================================
                                                              POLYGONINSET.CPP
============================================================================================================================================*/
// 🧩 The face-inset kernel body. InsetPolygonSelection reads the base adjacency (loops / normals / centres / edge incidence),
//    clones the region's vertices at zero offset, appends the shrunk cap faces (base winding preserved) and the rim quads
//    (outward-facing, sign-robust), removes the original selected faces by rebuilding the flat face stream, and reports the
//    appended geometry as the follow-on selection plus the per-vertex slide axes. Each boundary vertex carries an in-plane
//    corner-bisector InwardDirection (scaled by 1 / sin(halfAngle) so a uniform Thickness translates each edge inward by exactly
//    Thickness) and a unit NormalDirection (the Depth axis); interior region vertices carry a zero inward axis so Depth alone moves
//    them. InsetOffsetAdvance re-derives the caps along those axes for the interactive drag. Region rims only the region's outer
//    boundary; Individual rims every loop edge. Mirrors PolygonExtrude.cpp so the two kernels stay structurally identical.

#include "PolygonInset.h"

#include "PolygonCluster.h"
#include "VertexField.h"
#include "AdjacencyIndex.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double DirectionEpsilon = 1.0e-12;   // [-] - below this squared length a direction is treated as degenerate
    constexpr double BisectorEpsilon  = 1.0e-9;    // [-] - below this squared bisector length the corner is treated as straight
    constexpr double MaxBisectorScale = 1.0e3;     // [-] - clamp on 1 / sinHalf so a needle corner never flings the vertex away

    // Locate the corner-slice offset (running sum of FaceVertexCounts) of each face, so a face ordinal maps to its first corner in
    //    the flat FaceVertexIndices / FaceCornerTexture arrays. One pass, reused by the cap-append + rebuild steps. (Extrude parity.)
    std::vector<uint32_t> ResolveFaceCornerOffsets(const PolygonCluster& Target)
    {
        std::vector<uint32_t> Offsets;
        Offsets.reserve(Target.FaceVertexCounts.size());
        uint32_t Cursor = 0;
        for (uint32_t Count : Target.FaceVertexCounts)
        {
            Offsets.push_back(Cursor);
            Cursor += Count;
        }
        return Offsets;
    }

    // Append one face loop (+ its parallel corner UVs) to Target and return the new face ordinal. Corner UVs are appended only when
    //    the cluster already carries per-corner UVs, so an empty-UV cluster stays empty. (Extrude parity.)
    uint32_t AppendFace(PolygonCluster&              Target,
                        const std::vector<uint32_t>& Loop,
                        const std::vector<Vector2d>& CornerTexture)
    {
        const uint32_t NewOrdinal = (uint32_t)Target.FaceVertexCounts.size();
        for (uint32_t Corner : Loop)
            Target.FaceVertexIndices.push_back(Corner);
        Target.FaceVertexCounts.push_back((uint32_t)Loop.size());
        if (!Target.FaceCornerTexture.empty())
        {
            for (size_t Corner = 0; Corner < Loop.size(); ++Corner)
                Target.FaceCornerTexture.push_back(Corner < CornerTexture.size() ? CornerTexture[Corner] : Vector2d{ 0.0, 0.0 });
        }
        return NewOrdinal;
    }

    // Clone a base vertex's position + whatever optional attributes exist, returning the new vertex index. The clone is
    //    attribute-complete at zero offset (coincident with the original), so the cap shades / paints like the base until moved.
    uint32_t CloneCapVertex(PolygonCluster& Target, uint32_t BaseVertex)
    {
        const uint32_t NewVertex = AccumulateVertexPosition(Target.Attributes, Target.Attributes.Position[BaseVertex]);
        if (!Target.Attributes.Normal.empty())
            RefreshVertexNormal(Target.Attributes, NewVertex, Target.Attributes.Normal[BaseVertex]);
        if (!Target.Attributes.TextureCoordinate.empty())
            RefreshVertexTexture(Target.Attributes, NewVertex, Target.Attributes.TextureCoordinate[BaseVertex]);
        if (!Target.Attributes.Color.empty())
            RefreshVertexColor(Target.Attributes, NewVertex, Target.Attributes.Color[BaseVertex]);
        return NewVertex;
    }

    // The region's average face normal (unit): sum the base face normals over the selected faces, normalize. Falls back to world +Z
    //    when the sum is degenerate so the shared direction is never zero. (Extrude parity.)
    Vector3d AverageSelectedFaceNormal(const AdjacencyIndex& Adjacency, const std::unordered_set<uint32_t>& SelectedFaces)
    {
        Vector3d Sum{ 0.0, 0.0, 0.0 };
        for (uint32_t Face : SelectedFaces)
            Sum = AddVector(Sum, Adjacency.FaceNormal[Face]);
        if (EvaluateVectorLengthSquared(Sum) < DirectionEpsilon)
            return Vector3d{ 0.0, 0.0, 1.0 };
        return NormalizeVector(Sum);
    }

    // The per-vertex region normal (unit): average the base face normals of the selected faces incident to BaseVertex. Falls back to
    //    the region average when degenerate (a vertex whose selected faces cancel), which itself falls back to +Z. (Extrude parity.)
    Vector3d ResolveVertexRegionNormal(const AdjacencyIndex&               Adjacency,
                                       const std::unordered_set<uint32_t>& SelectedFaces,
                                       uint32_t                            BaseVertex,
                                       const Vector3d&                     RegionAverage)
    {
        Vector3d Sum{ 0.0, 0.0, 0.0 };
        for (uint32_t Face : Adjacency.VertexFaces[BaseVertex])
            if (SelectedFaces.count(Face) != 0)
                Sum = AddVector(Sum, Adjacency.FaceNormal[Face]);
        if (EvaluateVectorLengthSquared(Sum) < DirectionEpsilon)
            return RegionAverage;
        return NormalizeVector(Sum);
    }

    // Append one outward-facing rim quad bridging base edge (BaseA -> BaseB, taken in the owning face's winding order) to its clones
    //    (CloneA / CloneB). The base face loops are wound so their Newell normal is the cap-outward direction, so A -> B walks the
    //    boundary counter-clockwise as seen from outside; the side quad [BaseA, BaseB, CloneB, CloneA] then rises with its own normal
    //    pointing away from the solid — the standard prism-side winding, correct at any offset with no runtime sign test. The winding
    //    sense is loop-order dependent, not motion-direction dependent, so it holds even though the inset clones move inward / along
    //    the normal instead of straight out. Rim UVs are a plain unit square when the cluster carries per-corner UVs. (Extrude parity.)
    void AppendRimQuad(PolygonCluster& Target,
                       uint32_t        BaseA,
                       uint32_t        BaseB,
                       uint32_t        CloneA,
                       uint32_t        CloneB)
    {
        AppendFace(Target,
                   { BaseA, BaseB, CloneB, CloneA },
                   { { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 } });
    }

    // The in-plane inward slide axis for a boundary vertex V with boundary adjacents A and B, in the plane with unit normal N. The
    //    returned vector is NON-unit: it is the unit corner bisector scaled by 1 / sin(halfAngle) so that sliding V by a uniform
    //    Thickness translates each incident boundary edge inward by exactly Thickness (Blender's default non-even offset). The
    //    bisector is oriented toward FaceCenterReference so a concave (reflex) corner still slides into the region, and every
    //    degenerate corner (straight edge, needle spike, edge parallel to N) is guarded so the axis is finite and points inward.
    Vector3d ResolveInwardDirection(const Vector3d& PositionV,
                                    const Vector3d& PositionA,
                                    const Vector3d& PositionB,
                                    const Vector3d& Normal,
                                    const Vector3d& FaceCenterReference)
    {
        // Raw edge directions, then projected into V's plane (so a region spanning non-coplanar faces still slides in-plane).
        const Vector3d EdgeOne = NormalizeVector(SubtractVector(PositionA, PositionV));
        const Vector3d EdgeTwo = NormalizeVector(SubtractVector(PositionB, PositionV));
        Vector3d EdgeOnePlane = SubtractVector(EdgeOne, ScaleVector(Normal, DotProduct(EdgeOne, Normal)));
        Vector3d EdgeTwoPlane = SubtractVector(EdgeTwo, ScaleVector(Normal, DotProduct(EdgeTwo, Normal)));
        EdgeOnePlane = EvaluateVectorLengthSquared(EdgeOnePlane) < DirectionEpsilon ? EdgeOne : NormalizeVector(EdgeOnePlane);
        EdgeTwoPlane = EvaluateVectorLengthSquared(EdgeTwoPlane) < DirectionEpsilon ? EdgeTwo : NormalizeVector(EdgeTwoPlane);

        const Vector3d ToCenter = NormalizeVector(SubtractVector(FaceCenterReference, PositionV));
        const Vector3d Bisector = AddVector(EdgeOnePlane, EdgeTwoPlane);

        Vector3d Half;
        double   Scale = 1.0;
        if (EvaluateVectorLengthSquared(Bisector) < BisectorEpsilon)
        {
            // Straight (180°) corner: the bisector collapses. Slide along the in-plane perpendicular to the edge (scale 1 — the
            //    perpendicular already translates the straight edge inward by exactly Thickness).
            Half = CrossProduct(Normal, EdgeOnePlane);
            if (EvaluateVectorLengthSquared(Half) < DirectionEpsilon)
                Half = ToCenter;   // edge parallel to N as well — fall back to the centre direction
            Half = NormalizeVector(Half);
        }
        else
        {
            Half = NormalizeVector(Bisector);
            const double CosHalf = DotProduct(Half, EdgeOnePlane);
            const double SinHalf = std::sqrt(std::max(0.0, 1.0 - CosHalf * CosHalf));
            Scale = SinHalf > 1.0 / MaxBisectorScale ? 1.0 / SinHalf : MaxBisectorScale;   // clamp the needle-corner blow-up
        }

        // Orient inward: for a convex corner the bisector already points into the region; a concave corner points out — flip it.
        if (DotProduct(Half, ToCenter) < 0.0)
            Half = ScaleVector(Half, -1.0);
        return ScaleVector(Half, Scale);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InsetPolygonSelection(PolygonCluster&                     Target,
                           const AdjacencyIndex&               BaseAdjacency,
                           const std::unordered_set<uint32_t>& SelectedFaces,
                           InsetCategory                       Category,
                           InsetOutcome&                       Outcome)
{
    Outcome = InsetOutcome{};

    // --- Validate: a non-empty selection, every ordinal inside the base face count. ------------------------------------
    if (SelectedFaces.empty()) return false;
    for (uint32_t Face : SelectedFaces)
        if (Face >= BaseAdjacency.FaceCount) return false;

    const bool CarriesTexture = !Target.FaceCornerTexture.empty();
    const std::vector<uint32_t> CornerOffsets = ResolveFaceCornerOffsets(Target);
    const Vector3d RegionAverage = AverageSelectedFaceNormal(BaseAdjacency, SelectedFaces);

    // Read one face's corner UVs into a scratch loop (empty when the cluster carries none). (Extrude parity.)
    auto ResolveFaceCornerTexture = [&](uint32_t Face) -> std::vector<Vector2d>
    {
        std::vector<Vector2d> CornerTexture;
        if (!CarriesTexture) return CornerTexture;
        const uint32_t Offset = CornerOffsets[Face];
        const uint32_t Count  = Target.FaceVertexCounts[Face];
        CornerTexture.reserve(Count);
        for (uint32_t Corner = 0; Corner < Count; ++Corner)
            CornerTexture.push_back(Target.FaceCornerTexture[Offset + Corner]);
        return CornerTexture;
    };

    // Record an offset sample for a freshly cloned cap vertex (position at zero offset == the base vertex it came from).
    auto RecordSample = [&](uint32_t NewVertex, uint32_t BaseVertex, const Vector3d& Inward, const Vector3d& NormalAxis)
    {
        Outcome.OffsetSamples.push_back(InsetOffsetSample{ NewVertex, Inward, NormalAxis, Target.Attributes.Position[BaseVertex] });
        Outcome.CapVertices.insert(NewVertex);
    };

    if (Category == InsetCategory::IndividualCategory)
    {
        // --- Individual: each selected face insets on its own (own clone ring, own normal, rim all round). --------------
        for (uint32_t Face : SelectedFaces)
        {
            const std::vector<uint32_t>& Loop = BaseAdjacency.FaceVertexLoops[Face];
            const Vector3d FaceAxis = EvaluateVectorLengthSquared(BaseAdjacency.FaceNormal[Face]) < DirectionEpsilon
                                          ? RegionAverage : NormalizeVector(BaseAdjacency.FaceNormal[Face]);
            const Vector3d FaceCenter = BaseAdjacency.FaceCenter[Face];

            std::vector<uint32_t> CloneLoop;
            CloneLoop.reserve(Loop.size());
            for (size_t Corner = 0; Corner < Loop.size(); ++Corner)
            {
                const uint32_t BaseVertex = Loop[Corner];
                const uint32_t PrevVertex = Loop[(Corner + Loop.size() - 1) % Loop.size()];
                const uint32_t NextVertex = Loop[(Corner + 1) % Loop.size()];
                const Vector3d Inward = ResolveInwardDirection(Target.Attributes.Position[BaseVertex],
                                                               Target.Attributes.Position[PrevVertex],
                                                               Target.Attributes.Position[NextVertex],
                                                               FaceAxis, FaceCenter);
                const uint32_t Clone = CloneCapVertex(Target, BaseVertex);
                RecordSample(Clone, BaseVertex, Inward, FaceAxis);
                CloneLoop.push_back(Clone);
            }
            const uint32_t CapOrdinal = AppendFace(Target, CloneLoop, ResolveFaceCornerTexture(Face));
            Outcome.CapFaces.insert(CapOrdinal);
            // Rim every loop edge: base edge (Loop[i] -> Loop[i+1]) bridged to its clones.
            for (size_t Corner = 0; Corner < Loop.size(); ++Corner)
            {
                const size_t Next = (Corner + 1) % Loop.size();
                AppendRimQuad(Target, Loop[Corner], Loop[Next], CloneLoop[Corner], CloneLoop[Next]);
            }
        }
    }
    else
    {
        // --- Region: one shared shrunk cap over the region's unique vertices, rim on the outer boundary only. -----------
        // A boundary edge has exactly ONE selected face incident; its endpoints are boundary vertices and its adjacents feed the
        //    corner bisector. Interior region vertices (touched only by interior edges) get a zero inward axis so Depth alone moves
        //    them — which is why EVERY region vertex is cloned (the cap is a self-contained sheet; Depth cannot tear the outer mesh).
        std::unordered_map<uint32_t, std::vector<uint32_t>> BoundaryAdjacency;   // boundary vertex -> its boundary-edge adjacents
        for (uint32_t Face : SelectedFaces)
        {
            const std::vector<uint32_t>& Loop = BaseAdjacency.FaceVertexLoops[Face];
            for (size_t Corner = 0; Corner < Loop.size(); ++Corner)
            {
                const uint32_t BaseA = Loop[Corner];
                const uint32_t BaseB = Loop[(Corner + 1) % Loop.size()];
                const uint64_t EdgeKey = EncodeEdgeKey(BaseA, BaseB);
                auto Incident = BaseAdjacency.EdgeFaces.find(EdgeKey);
                if (Incident == BaseAdjacency.EdgeFaces.end()) continue;
                uint32_t SelectedIncident = 0;
                for (uint32_t Adjacent : Incident->second)
                    if (SelectedFaces.count(Adjacent) != 0) ++SelectedIncident;
                if (SelectedIncident != 1) continue;   // interior shared edge (2) — not a boundary
                BoundaryAdjacency[BaseA].push_back(BaseB);
                BoundaryAdjacency[BaseB].push_back(BaseA);
            }
        }

        std::unordered_map<uint32_t, uint32_t> CloneOfBase;   // base vertex -> its single shared clone
        auto ResolveClone = [&](uint32_t BaseVertex) -> uint32_t
        {
            auto Existing = CloneOfBase.find(BaseVertex);
            if (Existing != CloneOfBase.end()) return Existing->second;

            const Vector3d NormalAxis = ResolveVertexRegionNormal(BaseAdjacency, SelectedFaces, BaseVertex, RegionAverage);
            Vector3d Inward{ 0.0, 0.0, 0.0 };
            auto Boundary = BoundaryAdjacency.find(BaseVertex);
            if (Boundary != BoundaryAdjacency.end() && Boundary->second.size() >= 2)
            {
                // A clean boundary vertex has exactly two boundary adjacents; a pinch has more — take the first two (documented
                //    limitation). Reference the first incident selected face's centre so the bisector orients into the region.
                const uint32_t AdjacentA = Boundary->second[0];
                const uint32_t AdjacentB = Boundary->second[1];
                Vector3d CenterReference = RegionAverage;
                for (uint32_t Face : BaseAdjacency.VertexFaces[BaseVertex])
                    if (SelectedFaces.count(Face) != 0) { CenterReference = BaseAdjacency.FaceCenter[Face]; break; }
                Inward = ResolveInwardDirection(Target.Attributes.Position[BaseVertex],
                                                Target.Attributes.Position[AdjacentA],
                                                Target.Attributes.Position[AdjacentB],
                                                NormalAxis, CenterReference);
            }
            const uint32_t Clone = CloneCapVertex(Target, BaseVertex);
            RecordSample(Clone, BaseVertex, Inward, NormalAxis);
            CloneOfBase.emplace(BaseVertex, Clone);
            return Clone;
        };

        // Cap faces first (so every region vertex is cloned before the boundary rims reference the clones).
        for (uint32_t Face : SelectedFaces)
        {
            const std::vector<uint32_t>& Loop = BaseAdjacency.FaceVertexLoops[Face];
            std::vector<uint32_t> CloneLoop;
            CloneLoop.reserve(Loop.size());
            for (uint32_t BaseVertex : Loop)
                CloneLoop.push_back(ResolveClone(BaseVertex));
            const uint32_t CapOrdinal = AppendFace(Target, CloneLoop, ResolveFaceCornerTexture(Face));
            Outcome.CapFaces.insert(CapOrdinal);
        }

        // Boundary rims: bridge each boundary edge (in its owning face's winding order) to the shared clones.
        for (uint32_t Face : SelectedFaces)
        {
            const std::vector<uint32_t>& Loop = BaseAdjacency.FaceVertexLoops[Face];
            for (size_t Corner = 0; Corner < Loop.size(); ++Corner)
            {
                const uint32_t BaseA = Loop[Corner];
                const uint32_t BaseB = Loop[(Corner + 1) % Loop.size()];
                const uint64_t EdgeKey = EncodeEdgeKey(BaseA, BaseB);
                auto Incident = BaseAdjacency.EdgeFaces.find(EdgeKey);
                if (Incident == BaseAdjacency.EdgeFaces.end()) continue;
                uint32_t SelectedIncident = 0;
                for (uint32_t Adjacent : Incident->second)
                    if (SelectedFaces.count(Adjacent) != 0) ++SelectedIncident;
                if (SelectedIncident != 1) continue;   // interior shared edge (2) — no rim
                AppendRimQuad(Target, BaseA, BaseB, ResolveClone(BaseA), ResolveClone(BaseB));
            }
        }
    }

    // --- Remove the original selected faces (now interior): rebuild the three parallel arrays skipping their slices. -----
    //    Appended cap / rim faces have higher ordinals than every base face and so are never in SelectedFaces; their ordinals only
    //    shift DOWN by the count of removed faces that precede them (all removed faces are base faces, which all precede the appended
    //    ones), so remap CapFaces by subtracting the removed count. (Extrude parity — verbatim reuse.)
    {
        std::vector<uint32_t> KeptIndices;
        std::vector<uint32_t> KeptCounts;
        std::vector<Vector2d> KeptTexture;
        KeptCounts.reserve(Target.FaceVertexCounts.size());
        const std::vector<uint32_t> Offsets = ResolveFaceCornerOffsets(Target);
        for (uint32_t Face = 0; Face < (uint32_t)Target.FaceVertexCounts.size(); ++Face)
        {
            if (SelectedFaces.count(Face) != 0) continue;   // drop the original cap
            const uint32_t Offset = Offsets[Face];
            const uint32_t Count  = Target.FaceVertexCounts[Face];
            for (uint32_t Corner = 0; Corner < Count; ++Corner)
            {
                KeptIndices.push_back(Target.FaceVertexIndices[Offset + Corner]);
                if (CarriesTexture) KeptTexture.push_back(Target.FaceCornerTexture[Offset + Corner]);
            }
            KeptCounts.push_back(Count);
        }
        Target.FaceVertexIndices = KeptIndices;
        Target.FaceVertexCounts  = KeptCounts;
        if (CarriesTexture) Target.FaceCornerTexture = KeptTexture;
    }

    {
        const uint32_t RemovedCount = (uint32_t)SelectedFaces.size();
        std::unordered_set<uint32_t> RemappedCapFaces;
        RemappedCapFaces.reserve(Outcome.CapFaces.size());
        for (uint32_t CapFace : Outcome.CapFaces)
            RemappedCapFaces.insert(CapFace - RemovedCount);
        Outcome.CapFaces = RemappedCapFaces;
    }

    Outcome.Completed = true;
    return true;
}

bool InsetOffsetAdvance(PolygonCluster&                       Target,
                        const std::vector<InsetOffsetSample>& Samples,
                        double                                Thickness,
                        double                                Depth)
{
    const uint32_t VertexCount = EvaluateVertexCount(Target.Attributes);
    for (const InsetOffsetSample& Sample : Samples)
        if (Sample.NewVertex >= VertexCount) return false;

    for (const InsetOffsetSample& Sample : Samples)
        Target.Attributes.Position[Sample.NewVertex] = AddVector(AddVector(Sample.OriginPosition,
                                                                           ScaleVector(Sample.InwardDirection, Thickness)),
                                                                 ScaleVector(Sample.NormalDirection, Depth));
    return true;
}

} // namespace Frontier
