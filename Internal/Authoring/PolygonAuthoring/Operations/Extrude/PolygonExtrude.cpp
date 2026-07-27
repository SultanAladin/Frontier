/*============================================================================================================================================
                                                              POLYGONEXTRUDE.CPP
============================================================================================================================================*/
// 🧩 The face-extrude kernel body. ExtrudePolygonSelection reads the base adjacency (loops / normals / edge incidence), clones
//    the selected cap vertices at zero offset, appends the cap faces (base winding preserved) and the wall quads (outward-facing,
//    sign-robust), removes the original selected faces by rebuilding the flat face stream, and reports the appended geometry as
//    the follow-on selection plus the per-vertex slide directions. ExtrudeOffsetAdvance re-derives the caps along those
//    directions for the interactive drag. Region walls only the region's outer boundary; Individual walls every loop edge.

#include "PolygonExtrude.h"

#include "PolygonCluster.h"
#include "VertexField.h"
#include "AdjacencyIndex.h"

#include <algorithm>
#include <unordered_map>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double DirectionEpsilon = 1.0e-12;   // [-] - below this squared length a normal is treated as degenerate

    // Locate the corner-slice offset (running sum of FaceVertexCounts) of each face, so a face ordinal maps to its first
    //    corner in the flat FaceVertexIndices / FaceCornerTexture arrays. One pass, reused by the cap-append + rebuild steps.
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

    // Append one face loop (+ its parallel corner UVs) to Target and return the new face ordinal. Corner UVs are appended only
    //    when the cluster already carries per-corner UVs, so an empty-UV cluster stays empty (ConstructRenderVertexStream falls
    //    back to the vertex / zero UV). CornerTexture must match Loop length when the cluster has UVs.
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

    // The region's average face normal (unit): sum the base face normals over the selected faces, normalize. Falls back to
    //    world +Z when the sum is degenerate so the shared direction is never zero.
    Vector3d AverageSelectedFaceNormal(const AdjacencyIndex& Adjacency, const std::unordered_set<uint32_t>& SelectedFaces)
    {
        Vector3d Sum{ 0.0, 0.0, 0.0 };
        for (uint32_t Face : SelectedFaces)
            Sum = AddVector(Sum, Adjacency.FaceNormal[Face]);
        if (EvaluateVectorLengthSquared(Sum) < DirectionEpsilon)
            return Vector3d{ 0.0, 0.0, 1.0 };
        return NormalizeVector(Sum);
    }

    // The per-vertex extrude normal (unit): average the base face normals of the selected faces incident to BaseVertex. Falls
    //    back to the region average when degenerate (a vertex whose selected faces cancel), which itself falls back to +Z.
    Vector3d ResolveVertexExtrudeNormal(const AdjacencyIndex&               Adjacency,
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

    // Append one outward-facing wall quad bridging base edge (BaseA -> BaseB, taken in the owning face's winding order) to its
    //    clones (CloneA / CloneB). The base face loops are wound so their Newell normal is the cap-outward direction, so A -> B
    //    walks the boundary counter-clockwise as seen from outside; the side quad [BaseA, BaseB, CloneB, CloneA] then rises
    //    along the extrude axis with its own normal pointing away from the solid — the standard prism-side winding, correct at
    //    any offset with no runtime sign test. Wall UVs are a plain unit square when the cluster carries per-corner UVs.
    void AppendWallQuad(PolygonCluster& Target,
                        uint32_t        BaseA,
                        uint32_t        BaseB,
                        uint32_t        CloneA,
                        uint32_t        CloneB)
    {
        AppendFace(Target,
                   { BaseA, BaseB, CloneB, CloneA },
                   { { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 } });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ExtrudePolygonSelection(PolygonCluster&                     Target,
                             const AdjacencyIndex&               BaseAdjacency,
                             const std::unordered_set<uint32_t>& SelectedFaces,
                             ExtrudeCategory                     Category,
                             ExtrudeOutcome&                     Outcome)
{
    Outcome = ExtrudeOutcome{};

    // --- Validate: a non-empty selection, every ordinal inside the base face count. ------------------------------------
    if (SelectedFaces.empty()) return false;
    for (uint32_t Face : SelectedFaces)
        if (Face >= BaseAdjacency.FaceCount) return false;

    const std::vector<uint32_t> CornerOffsets = ResolveFaceCornerOffsets(Target);
    const bool CarriesTexture = !Target.FaceCornerTexture.empty();

    const Vector3d RegionAverage = AverageSelectedFaceNormal(BaseAdjacency, SelectedFaces);
    if (Category == ExtrudeCategory::RegionCategory)
        Outcome.SharedDirection = RegionAverage;

    // Read one face's corner UVs into a scratch loop (empty when the cluster carries none).
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
    auto RecordSample = [&](uint32_t NewVertex, uint32_t BaseVertex, const Vector3d& Direction)
    {
        Outcome.OffsetSamples.push_back(ExtrudeOffsetSample{ NewVertex, Direction, Target.Attributes.Position[BaseVertex] });
        Outcome.CapVertices.insert(NewVertex);
    };

    if (Category == ExtrudeCategory::IndividualCategory)
    {
        // --- Individual: each selected face is its own extruded prism (own clone ring, own normal, walls all round). ----
        for (uint32_t Face : SelectedFaces)
        {
            const std::vector<uint32_t>& Loop = BaseAdjacency.FaceVertexLoops[Face];
            const Vector3d FaceAxis = EvaluateVectorLengthSquared(BaseAdjacency.FaceNormal[Face]) < DirectionEpsilon
                                          ? RegionAverage : NormalizeVector(BaseAdjacency.FaceNormal[Face]);

            std::vector<uint32_t> CloneLoop;
            CloneLoop.reserve(Loop.size());
            for (uint32_t BaseVertex : Loop)
            {
                const uint32_t Clone = CloneCapVertex(Target, BaseVertex);
                RecordSample(Clone, BaseVertex, FaceAxis);
                CloneLoop.push_back(Clone);
            }
            const uint32_t CapOrdinal = AppendFace(Target, CloneLoop, ResolveFaceCornerTexture(Face));
            Outcome.CapFaces.insert(CapOrdinal);
            // Wall every loop edge: base edge (Loop[i] -> Loop[i+1]) bridged to its clones.
            for (size_t Corner = 0; Corner < Loop.size(); ++Corner)
            {
                const size_t Next = (Corner + 1) % Loop.size();
                AppendWallQuad(Target, Loop[Corner], Loop[Next], CloneLoop[Corner], CloneLoop[Next]);
            }
        }
    }
    else
    {
        // --- Region / Along Normals: one shared cap over the region's unique vertices, boundary walls only. -------------
        std::unordered_map<uint32_t, uint32_t> CloneOfBase;   // base vertex -> its single shared clone
        auto ResolveClone = [&](uint32_t BaseVertex) -> uint32_t
        {
            auto Existing = CloneOfBase.find(BaseVertex);
            if (Existing != CloneOfBase.end()) return Existing->second;
            const uint32_t Clone = CloneCapVertex(Target, BaseVertex);
            const Vector3d Direction = Category == ExtrudeCategory::AlongNormalsCategory
                                           ? ResolveVertexExtrudeNormal(BaseAdjacency, SelectedFaces, BaseVertex, RegionAverage)
                                           : RegionAverage;
            RecordSample(Clone, BaseVertex, Direction);
            CloneOfBase.emplace(BaseVertex, Clone);
            return Clone;
        };

        // Cap faces first (so every region vertex is cloned before the boundary walls reference the clones).
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

        // Boundary walls: an edge is on the region's outer boundary when exactly ONE selected face is incident to it. Walk
        //    each selected face's loop edges in winding order (so BaseA -> BaseB is oriented for the outward-wall winding).
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
                if (SelectedIncident != 1) continue;   // interior shared edge (2) — no wall
                AppendWallQuad(Target, BaseA, BaseB, ResolveClone(BaseA), ResolveClone(BaseB));
            }
        }
    }

    // --- Remove the original selected faces (now interior): rebuild the three parallel arrays skipping their slices. -----
    //    Appended cap / wall faces have higher ordinals than every base face and so are never in SelectedFaces; their
    //    ordinals only shift DOWN by the count of removed faces that precede them (all removed faces are base faces, which
    //    all precede the appended ones), so remap CapFaces by subtracting the removed count.
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

    // Remap the appended cap ordinals down by the number of removed (selected) faces that preceded them — every removed face
    //    is a base face, and every cap face was appended after all base faces, so the shift is the full removed count.
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

bool ExtrudeOffsetAdvance(PolygonCluster&                         Target,
                          const std::vector<ExtrudeOffsetSample>& Samples,
                          double                                  Distance)
{
    const uint32_t VertexCount = EvaluateVertexCount(Target.Attributes);
    for (const ExtrudeOffsetSample& Sample : Samples)
        if (Sample.NewVertex >= VertexCount) return false;

    for (const ExtrudeOffsetSample& Sample : Samples)
        Target.Attributes.Position[Sample.NewVertex] = AddVector(Sample.OriginPosition, ScaleVector(Sample.Direction, Distance));
    return true;
}

void ReverseAppendedFaceWinding(PolygonCluster& Target, const std::unordered_set<uint32_t>& Faces)
{
    if (Faces.empty()) return;
    const std::vector<uint32_t> Offsets = ResolveFaceCornerOffsets(Target);
    const bool CarriesTexture = !Target.FaceCornerTexture.empty();
    for (uint32_t Face : Faces)
    {
        if (Face >= (uint32_t)Target.FaceVertexCounts.size()) continue;
        const uint32_t Offset = Offsets[Face];
        const uint32_t Count  = Target.FaceVertexCounts[Face];
        // Reverse the loop in place: swap corner i with corner (Count - 1 - i). Reversing flips the Newell normal, so the face
        //    reads outward-inverted; the paired corner UVs are reversed alongside so the cap keeps its texel-to-corner mapping.
        for (uint32_t Corner = 0; Corner < Count / 2; ++Corner)
        {
            const uint32_t Low  = Offset + Corner;
            const uint32_t High = Offset + (Count - 1 - Corner);
            std::swap(Target.FaceVertexIndices[Low], Target.FaceVertexIndices[High]);
            if (CarriesTexture) std::swap(Target.FaceCornerTexture[Low], Target.FaceCornerTexture[High]);
        }
    }
}

} // namespace Frontier
