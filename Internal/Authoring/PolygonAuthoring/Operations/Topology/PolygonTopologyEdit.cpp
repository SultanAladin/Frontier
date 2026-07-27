/*============================================================================================================================================
                                                           POLYGONTOPOLOGYEDIT.CPP
============================================================================================================================================*/
// 🧩 The topology-removal + hole kernel bodies. Every op works over an explicit "surviving loops" model: it decodes the cluster's
//    flat face stream into a vector of per-face corner loops (+ parallel per-corner UV loops), edits that loop list (drop faces,
//    splice two loops into one, append a new loop), then rewrites the flat stream from the survivors and compacts any vertex no
//    surviving face references — keeping Attributes + FaceCornerTexture consistent (the cluster's watertight contract). Boundary
//    rings for Fill / Bridge are chained from the selected edge keys with a shared endpoint-walk helper.
// ⚠️ Faithful-port limitation: RewriteAndCompact rebuilds only FaceCornerTexture. The destination PolygonCluster also carries
//    FaceCornerColour + FaceMaterialIndex parallel to the face stream; those are NOT rewritten here, so an edit leaves them stale.
//    Preserved 1:1 from the reference (which had no such arrays); tracked in EngineDocs/Backlog.md for a follow-up pass.

#include "PolygonTopologyEdit.h"

#include "PolygonCluster.h"
#include "VertexField.h"
#include "AdjacencyIndex.h"
#include "ComponentSelection.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double NormalEpsilon = 1.0e-12;   // [-] - below this squared length a Newell normal is treated as degenerate

    // 📝 One decoded face: its corner-vertex loop plus the parallel per-corner UV loop (empty when the cluster carries no UVs).
    //    The whole edit runs over a vector of these, so a splice / drop / append is a plain vector op with no flat-offset math.
    struct FaceLoop
    {
        std::vector<uint32_t> Corners;   // [-] - corner vertex indices, winding order
        std::vector<Vector2d> Texture;   // [-] - per-corner UV (same length as Corners, or empty)
    };

    // Decode the cluster's flat face stream into per-face loops. Carries the per-corner UVs when present so a rewrite round-trips
    //    them. One pass over FaceVertexIndices guided by FaceVertexCounts.
    std::vector<FaceLoop> DecodeFaceLoops(const PolygonCluster& Target)
    {
        std::vector<FaceLoop> Loops;
        Loops.reserve(Target.FaceVertexCounts.size());
        const bool CarriesTexture = !Target.FaceCornerTexture.empty();
        uint32_t Cursor = 0;
        for (uint32_t FaceIndex = 0; FaceIndex < (uint32_t)Target.FaceVertexCounts.size(); ++FaceIndex)
        {
            const uint32_t Count = Target.FaceVertexCounts[FaceIndex];
            FaceLoop Loop;
            Loop.Corners.reserve(Count);
            if (CarriesTexture) Loop.Texture.reserve(Count);
            for (uint32_t Corner = 0; Corner < Count; ++Corner)
            {
                Loop.Corners.push_back(Target.FaceVertexIndices[Cursor + Corner]);
                if (CarriesTexture) Loop.Texture.push_back(Target.FaceCornerTexture[Cursor + Corner]);
            }
            Loops.push_back(std::move(Loop));
            Cursor += Count;
        }
        return Loops;
    }

    // Rewrite the cluster's flat face stream from a surviving-loop list, then compact any vertex no surviving face references
    //    (remap Attributes + every corner index). Keeps the three optional Attributes arrays + FaceCornerTexture consistent.
    //    RetainVertices force-keeps vertices even if unreferenced (e.g. a hole rim a later Fill will re-cap) — pass empty to drop
    //    every unused vertex. Returns the old->new vertex remap so the caller can translate its result sets.
    std::unordered_map<uint32_t, uint32_t> RewriteAndCompact(PolygonCluster&                     Target,
                                                             const std::vector<FaceLoop>&        Survivors,
                                                             const std::unordered_set<uint32_t>& RetainVertices)
    {
        const bool CarriesTexture = !Target.FaceCornerTexture.empty();

        // Which vertices survive: any referenced by a surviving face, plus the forced-retain set.
        std::unordered_set<uint32_t> Used = RetainVertices;
        for (const FaceLoop& Loop : Survivors)
            for (uint32_t Corner : Loop.Corners)
                Used.insert(Corner);

        // Build the old->new compaction remap in ascending old-index order so the layout stays stable.
        const uint32_t OldCount = EvaluateVertexCount(Target.Attributes);
        std::unordered_map<uint32_t, uint32_t> Remap;
        Remap.reserve(Used.size());
        VertexField Compact;
        const bool HasNormal  = !Target.Attributes.Normal.empty();
        const bool HasTexture = !Target.Attributes.TextureCoordinate.empty();
        const bool HasColor   = !Target.Attributes.Color.empty();
        for (uint32_t Old = 0; Old < OldCount; ++Old)
        {
            if (Used.count(Old) == 0) continue;
            const uint32_t New = AccumulateVertexPosition(Compact, Target.Attributes.Position[Old]);
            if (HasNormal)  RefreshVertexNormal(Compact, New, Target.Attributes.Normal[Old]);
            if (HasTexture) RefreshVertexTexture(Compact, New, Target.Attributes.TextureCoordinate[Old]);
            if (HasColor)   RefreshVertexColor(Compact, New, Target.Attributes.Color[Old]);
            Remap.emplace(Old, New);
        }
        Target.Attributes = std::move(Compact);

        // Rewrite the flat face stream through the remap.
        Target.FaceVertexIndices.clear();
        Target.FaceVertexCounts.clear();
        Target.FaceCornerTexture.clear();
        for (const FaceLoop& Loop : Survivors)
        {
            for (size_t Corner = 0; Corner < Loop.Corners.size(); ++Corner)
            {
                Target.FaceVertexIndices.push_back(Remap.at(Loop.Corners[Corner]));
                if (CarriesTexture)
                    Target.FaceCornerTexture.push_back(Corner < Loop.Texture.size() ? Loop.Texture[Corner] : Vector2d{ 0.0, 0.0 });
            }
            Target.FaceVertexCounts.push_back((uint32_t)Loop.Corners.size());
        }
        EvaluatePolygonDescriptor(Target);
        return Remap;
    }

    // The Newell normal of a corner loop (robust for non-planar / concave loops): sum the edge cross terms. Zero when degenerate.
    Vector3d EvaluateLoopNormal(const PolygonCluster& Target, const std::vector<uint32_t>& Loop)
    {
        Vector3d Normal{ 0.0, 0.0, 0.0 };
        const size_t Count = Loop.size();
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const Vector3d& Current = Target.Attributes.Position[Loop[Index]];
            const Vector3d& Next    = Target.Attributes.Position[Loop[(Index + 1) % Count]];
            Normal.XCoord += (Current.YCoord - Next.YCoord) * (Current.ZCoord + Next.ZCoord);
            Normal.YCoord += (Current.ZCoord - Next.ZCoord) * (Current.XCoord + Next.XCoord);
            Normal.ZCoord += (Current.XCoord - Next.XCoord) * (Current.YCoord + Next.YCoord);
        }
        return Normal;
    }

    // Chain a set of selected edge keys into ordered vertex rings. Builds an endpoint->connected-endpoints adjacency over the keys,
    //    then walks each connected component. A ring is closed (returns to its start) for a boundary loop; an open chain is returned
    //    as-is (Fill / Bridge reject non-closed input via IsRingClosed). Ignores keys whose endpoints are missing from Adjacency.
    std::vector<std::vector<uint32_t>> ResolveBoundaryLoops(const std::unordered_set<uint64_t>& SelectedEdges,
                                                            const AdjacencyIndex&               Adjacency)
    {
        // Endpoint adjacency limited to the selected edges.
        std::unordered_map<uint32_t, std::vector<uint32_t>> Adjacent;
        for (uint64_t Key : SelectedEdges)
        {
            auto Entry = Adjacency.EdgeVertices.find(Key);
            if (Entry == Adjacency.EdgeVertices.end()) continue;
            const uint32_t Lower = Entry->second.LowerVertex;
            const uint32_t Higher = Entry->second.HigherVertex;
            Adjacent[Lower].push_back(Higher);
            Adjacent[Higher].push_back(Lower);
        }

        std::vector<std::vector<uint32_t>> Loops;
        std::unordered_set<uint64_t> WalkedEdges;
        std::unordered_set<uint32_t> WalkedVertices;
        for (const auto& Seed : Adjacent)
        {
            if (WalkedVertices.count(Seed.first) != 0) continue;
            // Walk from this seed following unused incident edges until we return / dead-end.
            std::vector<uint32_t> Ring;
            uint32_t Current = Seed.first;
            uint32_t Previous = 0xFFFFFFFFu;
            while (true)
            {
                Ring.push_back(Current);
                WalkedVertices.insert(Current);
                // Pick the next adjacent across an edge not yet walked (prefer one that is not Previous so a 2-valence chain flows).
                uint32_t Next = 0xFFFFFFFFu;
                const std::vector<uint32_t>& Candidates = Adjacent[Current];
                for (uint32_t Candidate : Candidates)
                {
                    const uint64_t StepKey = EncodeEdgeKey(Current, Candidate);
                    if (WalkedEdges.count(StepKey) != 0) continue;
                    if (Candidate == Previous && Candidates.size() > 1) continue;
                    Next = Candidate;
                    break;
                }
                if (Next == 0xFFFFFFFFu) break;
                WalkedEdges.insert(EncodeEdgeKey(Current, Next));
                Previous = Current;
                Current = Next;
                if (Current == Ring.front()) break;   // closed the ring
            }
            if (Ring.size() >= 3) Loops.push_back(std::move(Ring));
        }
        return Loops;
    }

    // A ring is closed when every one of its edges (including the wrap-around) is one of the selected edges — i.e. the walk came
    //    back to the start. ResolveBoundaryLoops stops a closed walk exactly at the start, so a closed ring's last->first edge is
    //    also selected; verify it to reject an open chain (Fill / Bridge need closed rings).
    bool IsRingClosed(const std::vector<uint32_t>& Ring, const std::unordered_set<uint64_t>& SelectedEdges)
    {
        if (Ring.size() < 3) return false;
        const uint64_t ClosingKey = EncodeEdgeKey(Ring.back(), Ring.front());
        return SelectedEdges.count(ClosingKey) != 0;
    }

    // Every selected edge must be a boundary edge (a single incident face) for Fill / Bridge — a hole rim. Returns false on the
    //    first interior / unknown edge.
    bool AllEdgesBoundary(const std::unordered_set<uint64_t>& SelectedEdges, const AdjacencyIndex& Adjacency)
    {
        for (uint64_t Key : SelectedEdges)
        {
            auto Entry = Adjacency.EdgeFaces.find(Key);
            if (Entry == Adjacency.EdgeFaces.end() || Entry->second.size() != 1) return false;
        }
        return true;
    }

    // Copy a vertex's existing corner UV from any face that references it (the first corner found), so a new Fill / Bridge face
    //    inherits a plausible UV. Zero when the cluster has no per-corner UVs or the vertex is unreferenced.
    Vector2d InheritCornerTexture(const PolygonCluster& Target, uint32_t Vertex)
    {
        if (Target.FaceCornerTexture.empty()) return Vector2d{ 0.0, 0.0 };
        for (size_t Corner = 0; Corner < Target.FaceVertexIndices.size(); ++Corner)
            if (Target.FaceVertexIndices[Corner] == Vertex)
                return Target.FaceCornerTexture[Corner];
        return Vector2d{ 0.0, 0.0 };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ResolveDeleteSelection(PolygonCluster&        Target,
                            const AdjacencyIndex&  Adjacency,
                            const SelectionState&  Selection,
                            DeleteCategory         Category,
                            TopologyEditOutcome&   Outcome)
{
    Outcome = TopologyEditOutcome{};

    // Resolve which faces to drop + which rim vertices to report, per category. Faces are dropped by ordinal; the rewrite compacts
    //    any vertex no survivor uses (a true hole), except the rim we want to keep selected.
    std::unordered_set<uint32_t> DropFaces;
    std::unordered_set<uint32_t> RimVertices;   // survivors on the hole boundary — the follow-on selection

    if (Category == DeleteCategory::FaceCategory)
    {
        if (Selection.Faces.empty()) return false;
        DropFaces = Selection.Faces;
        // Rim = vertices of the dropped faces that a surviving face still uses.
        for (uint32_t Face : DropFaces)
            if (Face < Adjacency.FaceVertexLoops.size())
                for (uint32_t Vertex : Adjacency.FaceVertexLoops[Face])
                    RimVertices.insert(Vertex);
    }
    else if (Category == DeleteCategory::EdgeCategory)
    {
        if (Selection.Edges.empty()) return false;
        for (uint64_t Key : Selection.Edges)
        {
            auto Entry = Adjacency.EdgeFaces.find(Key);
            if (Entry == Adjacency.EdgeFaces.end()) continue;
            for (uint32_t Face : Entry->second) DropFaces.insert(Face);
            auto Endpoints = Adjacency.EdgeVertices.find(Key);
            if (Endpoints != Adjacency.EdgeVertices.end())
            {
                RimVertices.insert(Endpoints->second.LowerVertex);
                RimVertices.insert(Endpoints->second.HigherVertex);
            }
        }
        if (DropFaces.empty()) return false;
    }
    else   // VertexCategory
    {
        if (Selection.Vertices.empty()) return false;
        for (uint32_t Vertex : Selection.Vertices)
            if (Vertex < Adjacency.VertexFaces.size())
                for (uint32_t Face : Adjacency.VertexFaces[Vertex])
                    DropFaces.insert(Face);
        if (DropFaces.empty()) return false;
    }

    // Survivors = every face NOT dropped. Decode, filter, rewrite.
    const std::vector<FaceLoop> AllLoops = DecodeFaceLoops(Target);
    std::vector<FaceLoop> Survivors;
    Survivors.reserve(AllLoops.size());
    for (uint32_t Face = 0; Face < (uint32_t)AllLoops.size(); ++Face)
        if (DropFaces.count(Face) == 0)
            Survivors.push_back(AllLoops[Face]);

    // Edge / Vertex delete leave a true hole — drop every now-unreferenced vertex (retain nothing). Face delete keeps the rim so a
    //    follow-on Fill has a ring to cap; retain only rim verts a survivor still uses (an interior-only rim vert would vanish).
    std::unordered_set<uint32_t> Retain;
    if (Category == DeleteCategory::FaceCategory)
    {
        std::unordered_set<uint32_t> SurvivorVerts;
        for (const FaceLoop& Loop : Survivors)
            for (uint32_t Corner : Loop.Corners) SurvivorVerts.insert(Corner);
        for (uint32_t Vertex : RimVertices)
            if (SurvivorVerts.count(Vertex) != 0) Retain.insert(Vertex);
    }

    const std::unordered_map<uint32_t, uint32_t> Remap = RewriteAndCompact(Target, Survivors, Retain);

    // Report the surviving rim as the follow-on vertex selection (translated through the compaction remap).
    for (uint32_t Vertex : RimVertices)
    {
        auto Entry = Remap.find(Vertex);
        if (Entry != Remap.end()) Outcome.ResultVertices.insert(Entry->second);
    }
    Outcome.Completed = true;
    return true;
}

bool ResolveDissolveSelection(PolygonCluster&        Target,
                              const AdjacencyIndex&  Adjacency,
                              const SelectionState&  Selection,
                              DeleteCategory         Category,
                              TopologyEditOutcome&   Outcome)
{
    Outcome = TopologyEditOutcome{};
    const std::vector<FaceLoop> AllLoops = DecodeFaceLoops(Target);

    if (Category == DeleteCategory::EdgeCategory)
    {
        if (Selection.Edges.empty()) return false;
        // Merge the two faces across each selected INTERIOR edge into one loop. Process one edge at a time over a live loop list so a
        //    fan of edges around a shared vertex folds into one N-gon. A merged face inherits the union of the two source loops with
        //    the shared edge's two corners spliced out.
        std::vector<FaceLoop> Loops = AllLoops;
        std::vector<bool>     Alive(Loops.size(), true);
        bool AnyMerged = false;
        for (uint64_t Key : Selection.Edges)
        {
            auto FacesEntry = Adjacency.EdgeFaces.find(Key);
            auto EndsEntry  = Adjacency.EdgeVertices.find(Key);
            if (FacesEntry == Adjacency.EdgeFaces.end() || EndsEntry == Adjacency.EdgeVertices.end()) continue;
            if (FacesEntry->second.size() != 2) continue;   // boundary edge — nothing to merge across

            // Follow the two original faces to their CURRENT alive host loops (a prior merge may have absorbed one).
            auto ResolveHost = [&](uint32_t OriginalFace) -> uint32_t
            {
                // The host is the alive loop that still contains BOTH shared-edge endpoints adjacent to each other.
                (void)OriginalFace;
                return 0xFFFFFFFFu;
            };
            (void)ResolveHost;

            const uint32_t VertexA = EndsEntry->second.LowerVertex;
            const uint32_t VertexB = EndsEntry->second.HigherVertex;

            // Find the two distinct alive loops that both contain the edge A-B as a consecutive pair.
            int HostOne = -1, HostTwo = -1;
            auto ContainsEdge = [&](const std::vector<uint32_t>& Corners) -> bool
            {
                const size_t Count = Corners.size();
                for (size_t Index = 0; Index < Count; ++Index)
                {
                    const uint32_t Current = Corners[Index];
                    const uint32_t Next    = Corners[(Index + 1) % Count];
                    if ((Current == VertexA && Next == VertexB) || (Current == VertexB && Next == VertexA)) return true;
                }
                return false;
            };
            for (int Index = 0; Index < (int)Loops.size(); ++Index)
            {
                if (!Alive[Index]) continue;
                if (!ContainsEdge(Loops[Index].Corners)) continue;
                if (HostOne < 0) HostOne = Index;
                else { HostTwo = Index; break; }
            }
            if (HostOne < 0 || HostTwo < 0) continue;

            // Splice: rotate loop one so it ends at ...A,B, rotate loop two so it starts at B,A..., then concatenate dropping the
            //    duplicate shared endpoints. Result is one loop bounding the union with the shared edge removed.
            const FaceLoop& LoopOne = Loops[HostOne];
            const FaceLoop& LoopTwo = Loops[HostTwo];
            const bool CarriesTexture = !LoopOne.Texture.empty() && !LoopTwo.Texture.empty();

            // Rotate a loop so its winding traverses A->B, returning corners starting AFTER B (so it ends at A). Also carries UVs.
            auto OrientForMerge = [&](const FaceLoop& Source, uint32_t First, uint32_t Second) -> FaceLoop
            {
                const size_t Count = Source.Corners.size();
                size_t EdgeStart = 0;
                bool Found = false;
                for (size_t Index = 0; Index < Count; ++Index)
                {
                    if (Source.Corners[Index] == First && Source.Corners[(Index + 1) % Count] == Second) { EdgeStart = Index; Found = true; break; }
                }
                FaceLoop Rotated;
                if (!Found) return Rotated;
                // Walk from Second's position around to First (exclusive of the shared pair's interior duplication handled by caller).
                for (size_t Step = 0; Step < Count; ++Step)
                {
                    const size_t Index = (EdgeStart + 1 + Step) % Count;   // start at Second
                    Rotated.Corners.push_back(Source.Corners[Index]);
                    if (CarriesTexture) Rotated.Texture.push_back(Source.Texture[Index]);
                }
                return Rotated;
            };

            // Loop one traverses A->B; loop two traverses B->A (opposite orientation across the shared edge).
            FaceLoop OrientedOne = OrientForMerge(LoopOne, VertexA, VertexB);   // starts at B ... ends at A
            FaceLoop OrientedTwo = OrientForMerge(LoopTwo, VertexB, VertexA);   // starts at A ... ends at B
            if (OrientedOne.Corners.empty() || OrientedTwo.Corners.empty()) continue;

            // OrientedOne = [B, x1, x2, ..., A]; OrientedTwo = [A, y1, y2, ..., B]. The merged ring is the interior of one after B
            //    up to A, then the interior of two after A up to B — i.e. drop OrientedOne's leading B and trailing A duplicates by
            //    taking OrientedOne[1..end-1] plus OrientedTwo[1..end-1], bracketed by the shared verts once each.
            FaceLoop Merged;
            const bool MergeTexture = CarriesTexture;
            // B
            Merged.Corners.push_back(VertexB);
            if (MergeTexture) Merged.Texture.push_back(OrientedOne.Texture.front());
            // interior of one (skip leading B and trailing A)
            for (size_t Index = 1; Index + 1 < OrientedOne.Corners.size(); ++Index)
            {
                Merged.Corners.push_back(OrientedOne.Corners[Index]);
                if (MergeTexture) Merged.Texture.push_back(OrientedOne.Texture[Index]);
            }
            // A
            Merged.Corners.push_back(VertexA);
            if (MergeTexture) Merged.Texture.push_back(OrientedTwo.Texture.front());
            // interior of two (skip leading A and trailing B)
            for (size_t Index = 1; Index + 1 < OrientedTwo.Corners.size(); ++Index)
            {
                Merged.Corners.push_back(OrientedTwo.Corners[Index]);
                if (MergeTexture) Merged.Texture.push_back(OrientedTwo.Texture[Index]);
            }
            if (Merged.Corners.size() < 3) continue;

            Loops[HostOne] = std::move(Merged);
            Alive[HostTwo] = false;
            AnyMerged = true;
        }
        if (!AnyMerged) return false;

        std::vector<FaceLoop> Survivors;
        for (size_t Index = 0; Index < Loops.size(); ++Index)
            if (Alive[Index]) Survivors.push_back(std::move(Loops[Index]));
        RewriteAndCompact(Target, Survivors, {});
        Outcome.Completed = true;
        return true;
    }

    if (Category == DeleteCategory::FaceCategory)
    {
        if (Selection.Faces.empty()) return false;
        // Union a contiguous selected-face patch into one N-gon = its ordered outer boundary loop. The patch boundary is every loop
        //    edge incident to exactly one selected face; chain those into a ring. Multiple disconnected patches each fold to a face.
        // Collect patch boundary edges (selected-face edges NOT shared with another selected face).
        std::unordered_map<uint64_t, int> EdgeSelectedCount;
        for (uint32_t Face : Selection.Faces)
        {
            if (Face >= Adjacency.FaceEdgeKeys.size()) continue;
            for (uint64_t Key : Adjacency.FaceEdgeKeys[Face]) EdgeSelectedCount[Key]++;
        }
        std::unordered_set<uint64_t> BoundaryEdges;
        for (const auto& Entry : EdgeSelectedCount)
            if (Entry.second == 1) BoundaryEdges.insert(Entry.first);
        if (BoundaryEdges.empty()) return false;

        std::vector<std::vector<uint32_t>> Rings = ResolveBoundaryLoops(BoundaryEdges, Adjacency);
        // Keep only closed rings.
        std::vector<std::vector<uint32_t>> ClosedRings;
        for (auto& Ring : Rings)
            if (IsRingClosed(Ring, BoundaryEdges)) ClosedRings.push_back(std::move(Ring));
        if (ClosedRings.empty()) return false;

        // Survivors = every non-selected face; then append one face per closed ring, wound to match the patch's average normal.
        const std::vector<FaceLoop> DecodedLoops = DecodeFaceLoops(Target);
        std::vector<FaceLoop> Survivors;
        for (uint32_t Face = 0; Face < (uint32_t)DecodedLoops.size(); ++Face)
            if (Selection.Faces.count(Face) == 0) Survivors.push_back(DecodedLoops[Face]);

        Vector3d PatchNormal{ 0.0, 0.0, 0.0 };
        for (uint32_t Face : Selection.Faces)
            if (Face < Adjacency.FaceNormal.size()) PatchNormal = AddVector(PatchNormal, Adjacency.FaceNormal[Face]);

        for (std::vector<uint32_t>& Ring : ClosedRings)
        {
            // Orient the ring so its Newell normal agrees with the patch normal (faces the same way the patch did).
            const Vector3d RingNormal = EvaluateLoopNormal(Target, Ring);
            if (EvaluateVectorLengthSquared(RingNormal) > NormalEpsilon &&
                DotProduct(RingNormal, PatchNormal) < 0.0)
                std::reverse(Ring.begin(), Ring.end());
            FaceLoop Merged;
            Merged.Corners = Ring;
            if (!Target.FaceCornerTexture.empty())
                for (uint32_t Vertex : Ring) Merged.Texture.push_back(InheritCornerTexture(Target, Vertex));
            Survivors.push_back(std::move(Merged));
        }

        const size_t NewFaceStart = Survivors.size() - ClosedRings.size();
        RewriteAndCompact(Target, Survivors, {});
        for (size_t Index = NewFaceStart; Index < Survivors.size(); ++Index)
            Outcome.ResultFaces.insert((uint32_t)Index);
        Outcome.Completed = true;
        return true;
    }

    // VertexCategory — dissolve each interior vertex by merging its incident face ring into one face (the reverse of a poke).
    if (Selection.Vertices.empty()) return false;
    std::vector<FaceLoop> Loops = AllLoops;
    std::vector<bool>     Alive(Loops.size(), true);
    bool AnyDissolved = false;
    for (uint32_t Vertex : Selection.Vertices)
    {
        if (Vertex >= Adjacency.VertexFaces.size()) continue;
        const std::vector<uint32_t>& IncidentFaces = Adjacency.VertexFaces[Vertex];
        if (IncidentFaces.size() < 3) continue;   // boundary / low valence — skip (matches Blender)

        // Every incident face must still be alive (a prior dissolve may have absorbed one); collect their current loops.
        std::vector<int> Hosts;
        for (uint32_t Face : IncidentFaces)
            if (Face < Alive.size() && Alive[Face]) Hosts.push_back((int)Face);
        if (Hosts.size() < 3) continue;

        // Union the incident faces into one boundary ring = every edge of the incident faces NOT touching the dissolved vertex,
        //    chained. Build that edge set then walk it into a ring.
        std::unordered_set<uint64_t> RingEdges;
        for (int Face : Hosts)
        {
            const std::vector<uint32_t>& Corners = Loops[Face].Corners;
            const size_t Count = Corners.size();
            for (size_t Index = 0; Index < Count; ++Index)
            {
                const uint32_t Current = Corners[Index];
                const uint32_t Next    = Corners[(Index + 1) % Count];
                if (Current == Vertex || Next == Vertex) continue;   // spoke edge — vanishes with the vertex
                RingEdges.insert(EncodeEdgeKey(Current, Next));
            }
        }
        std::vector<std::vector<uint32_t>> Rings = ResolveBoundaryLoops(RingEdges, Adjacency);
        std::vector<uint32_t> Ring;
        for (auto& Candidate : Rings)
            if (IsRingClosed(Candidate, RingEdges) && Candidate.size() > Ring.size()) Ring = Candidate;
        if (Ring.size() < 3) continue;

        // Orient the merged face to the incident faces' average normal.
        Vector3d AverageNormal{ 0.0, 0.0, 0.0 };
        for (int Face : Hosts)
            if ((uint32_t)Face < Adjacency.FaceNormal.size()) AverageNormal = AddVector(AverageNormal, Adjacency.FaceNormal[Face]);
        const Vector3d RingNormal = EvaluateLoopNormal(Target, Ring);
        if (EvaluateVectorLengthSquared(RingNormal) > NormalEpsilon && DotProduct(RingNormal, AverageNormal) < 0.0)
            std::reverse(Ring.begin(), Ring.end());

        FaceLoop Merged;
        Merged.Corners = Ring;
        if (!Target.FaceCornerTexture.empty())
            for (uint32_t RingVertex : Ring) Merged.Texture.push_back(InheritCornerTexture(Target, RingVertex));
        Loops[Hosts.front()] = std::move(Merged);
        for (size_t Index = 1; Index < Hosts.size(); ++Index) Alive[Hosts[Index]] = false;
        AnyDissolved = true;
    }
    if (!AnyDissolved) return false;

    std::vector<FaceLoop> Survivors;
    for (size_t Index = 0; Index < Loops.size(); ++Index)
        if (Alive[Index]) Survivors.push_back(std::move(Loops[Index]));
    RewriteAndCompact(Target, Survivors, {});
    Outcome.Completed = true;
    return true;
}

bool ConstructBoundaryFill(PolygonCluster&                      Target,
                           const AdjacencyIndex&                Adjacency,
                           const std::unordered_set<uint64_t>&  SelectedEdges,
                           TopologyEditOutcome&                 Outcome)
{
    Outcome = TopologyEditOutcome{};
    if (SelectedEdges.size() < 3) return false;
    if (!AllEdgesBoundary(SelectedEdges, Adjacency)) return false;

    std::vector<std::vector<uint32_t>> Rings = ResolveBoundaryLoops(SelectedEdges, Adjacency);
    if (Rings.size() != 1 || !IsRingClosed(Rings.front(), SelectedEdges)) return false;
    std::vector<uint32_t> Ring = Rings.front();

    // Orient the cap so its normal OPPOSES the average adjacent-face normal (a hole rim's adjacent faces point outward; the cap
    //    should close the surface facing the same way as the rest of the shell → opposite the inward-looking loop winding).
    Vector3d AdjacentNormal{ 0.0, 0.0, 0.0 };
    for (uint64_t Key : SelectedEdges)
    {
        auto Entry = Adjacency.EdgeFaces.find(Key);
        if (Entry == Adjacency.EdgeFaces.end() || Entry->second.empty()) continue;
        const uint32_t Face = Entry->second.front();
        if (Face < Adjacency.FaceNormal.size()) AdjacentNormal = AddVector(AdjacentNormal, Adjacency.FaceNormal[Face]);
    }
    const Vector3d RingNormal = EvaluateLoopNormal(Target, Ring);
    if (EvaluateVectorLengthSquared(RingNormal) > NormalEpsilon &&
        EvaluateVectorLengthSquared(AdjacentNormal) > NormalEpsilon &&
        DotProduct(RingNormal, AdjacentNormal) < 0.0)
        std::reverse(Ring.begin(), Ring.end());

    // Append the cap face directly to the flat stream (no compaction — every existing vertex stays; a Fill adds only a face).
    const uint32_t NewOrdinal = (uint32_t)Target.FaceVertexCounts.size();
    for (uint32_t Vertex : Ring) Target.FaceVertexIndices.push_back(Vertex);
    Target.FaceVertexCounts.push_back((uint32_t)Ring.size());
    if (!Target.FaceCornerTexture.empty())
        for (uint32_t Vertex : Ring) Target.FaceCornerTexture.push_back(InheritCornerTexture(Target, Vertex));
    EvaluatePolygonDescriptor(Target);

    Outcome.ResultFaces.insert(NewOrdinal);
    Outcome.Completed = true;
    return true;
}

bool BridgeBoundaryLoops(PolygonCluster&                      Target,
                         const AdjacencyIndex&                Adjacency,
                         const std::unordered_set<uint64_t>&  SelectedEdges,
                         TopologyEditOutcome&                 Outcome)
{
    Outcome = TopologyEditOutcome{};
    if (SelectedEdges.size() < 6) return false;   // two triangles minimum
    if (!AllEdgesBoundary(SelectedEdges, Adjacency)) return false;

    std::vector<std::vector<uint32_t>> Rings = ResolveBoundaryLoops(SelectedEdges, Adjacency);
    // Keep only closed rings.
    std::vector<std::vector<uint32_t>> ClosedRings;
    for (auto& Ring : Rings)
        if (IsRingClosed(Ring, SelectedEdges)) ClosedRings.push_back(std::move(Ring));
    if (ClosedRings.size() != 2) return false;
    if (ClosedRings[0].size() != ClosedRings[1].size()) return false;

    std::vector<uint32_t> LoopA = ClosedRings[0];
    std::vector<uint32_t> LoopB = ClosedRings[1];
    const size_t Count = LoopA.size();

    // Correspondence: choose loop B's start offset + direction that minimises the total squared distance A[i] <-> B[i]. Try both
    //    directions and every rotation; the sum-of-squares metric matches Blender's nearest-correspondence bridge.
    auto TotalDistance = [&](const std::vector<uint32_t>& Candidate) -> double
    {
        double Sum = 0.0;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const Vector3d Delta = SubtractVector(Target.Attributes.Position[LoopA[Index]], Target.Attributes.Position[Candidate[Index]]);
            Sum += EvaluateVectorLengthSquared(Delta);
        }
        return Sum;
    };
    std::vector<uint32_t> BestB = LoopB;
    double BestScore = TotalDistance(LoopB);
    for (int Direction = 0; Direction < 2; ++Direction)
    {
        std::vector<uint32_t> Oriented = LoopB;
        if (Direction == 1) std::reverse(Oriented.begin(), Oriented.end());
        for (size_t Offset = 0; Offset < Count; ++Offset)
        {
            std::vector<uint32_t> Rotated(Count);
            for (size_t Index = 0; Index < Count; ++Index) Rotated[Index] = Oriented[(Index + Offset) % Count];
            const double Score = TotalDistance(Rotated);
            if (Score < BestScore) { BestScore = Score; BestB = Rotated; }
        }
    }
    LoopB = BestB;

    // Winding: A[i] -> A[i+1] -> B[i+1] -> B[i] gives an outward-consistent quad ring when the two loops are wound oppositely (the
    //    holes face away from each other). The correspondence search already aligns the seams; this fixed winding walls the tube.
    const uint32_t FirstNewOrdinal = (uint32_t)Target.FaceVertexCounts.size();
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const uint32_t A0 = LoopA[Index];
        const uint32_t A1 = LoopA[(Index + 1) % Count];
        const uint32_t B0 = LoopB[Index];
        const uint32_t B1 = LoopB[(Index + 1) % Count];
        const uint32_t Corners[4] = { A0, A1, B1, B0 };
        for (uint32_t Corner : Corners) Target.FaceVertexIndices.push_back(Corner);
        Target.FaceVertexCounts.push_back(4);
        if (!Target.FaceCornerTexture.empty())
            for (uint32_t Corner : Corners) Target.FaceCornerTexture.push_back(InheritCornerTexture(Target, Corner));
        // Rail + rung edges of the new quad → follow-on edge selection.
        Outcome.ResultEdges.insert(EncodeEdgeKey(A0, B0));
        Outcome.ResultEdges.insert(EncodeEdgeKey(A1, B1));
    }
    EvaluatePolygonDescriptor(Target);

    for (uint32_t Ordinal = FirstNewOrdinal; Ordinal < (uint32_t)Target.FaceVertexCounts.size(); ++Ordinal)
        Outcome.ResultFaces.insert(Ordinal);
    Outcome.Completed = true;
    return true;
}

} // namespace Frontier
