/*============================================================================================================================================
                                                             MODIFIERSTACK.CPP
============================================================================================================================================*/
// 🧩 Implementation of the non-destructive modifier stack over the live flat PolygonCluster. Append / Remove / Retrieve / Reorder
//    edit the ordered entry list; EvaluateModifierStack copies the base cage, runs every enabled entry's kernel in order over the
//    flat (Positions, FaceVertexIndices, FaceVertexCounts) arrays, and writes the refined polygon into the evaluated cluster that
//    feeds display + GPU. The base cage is never touched, so any entry can be toggled / removed / reordered / re-tuned and the
//    result re-derives from the untouched base.
// 📝 Subdivision runs level-on-level: each level re-invokes the flat kernel on the previous level's output. Catmull-Clark seeds
//    its per-edge sharpness from the marked seams (sharpness 1.0) on the FIRST level only — after one level the vertex indices
//    are refined and the base-cage seam keys no longer address the same edges, so later levels subdivide smoothly, which is the
//    expected semi-sharp behaviour (the crease is imprinted at the base and softens outward). Only positions and the face arrays
//    flow through the kernels; the evaluated cluster's normals are re-derived at display time and its UVs are display-only.

#include "ModifierStack.h"

#include "SubdivideSimple.h"
#include "SubdivideCatmullClark.h"
#include "LinearAlgebra_Float64.h"

#include <algorithm>
#include <unordered_map>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Canonical undirected edge key matching AdjacencyIndex::EncodeEdgeKey (lower vertex index high, higher low), so the seam
    //    set keyed on the base cage lines up with the sharpness map the Catmull-Clark kernel consumes.
    uint64_t EncodeLocalEdgeKey(uint32_t FirstIndex, uint32_t SecondIndex)
    {
        const uint32_t LowerIndex  = FirstIndex < SecondIndex ? FirstIndex : SecondIndex;
        const uint32_t HigherIndex = FirstIndex < SecondIndex ? SecondIndex : FirstIndex;
        return ((uint64_t)HigherIndex << 32) | (uint64_t)LowerIndex;
    }

    // 📝 Derive one smooth per-vertex normal per position by accumulating each face's area-weighted geometric normal onto its
    //    corner vertices, then normalizing. The refined subdivision output carries no authored normals, and the matcap / PBR
    //    surface shades from an interpolated per-vertex normal, so the evaluated cluster must supply them. Newell's method gives a
    //    robust face normal for a triangle, a quad, or an N-gon; area weighting (the un-normalized Newell vector's length is twice
    //    the polygon area) blends adjacent faces the way a smooth limit surface expects.
    void ResolveSmoothNormals(std::vector<Vector3d>&       Normals,
                              const std::vector<Vector3d>& Positions,
                              const std::vector<uint32_t>& FaceVertexIndices,
                              const std::vector<uint32_t>& FaceVertexCounts)
    {
        Normals.assign(Positions.size(), Vector3d{ 0.0, 0.0, 0.0 });

        uint32_t CornerCursor = 0;
        for (uint32_t CornerCount : FaceVertexCounts)
        {
            if (CornerCount >= 3)
            {
                Vector3d FaceNormal = { 0.0, 0.0, 0.0 };
                for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                {
                    const Vector3d& Current = Positions[FaceVertexIndices[CornerCursor + Corner]];
                    const Vector3d& Next    = Positions[FaceVertexIndices[CornerCursor + (Corner + 1) % CornerCount]];
                    FaceNormal.XCoord += (Current.YCoord - Next.YCoord) * (Current.ZCoord + Next.ZCoord);
                    FaceNormal.YCoord += (Current.ZCoord - Next.ZCoord) * (Current.XCoord + Next.XCoord);
                    FaceNormal.ZCoord += (Current.XCoord - Next.XCoord) * (Current.YCoord + Next.YCoord);
                }
                for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
                {
                    const uint32_t VertexSlot = FaceVertexIndices[CornerCursor + Corner];
                    Normals[VertexSlot] = AddVector(Normals[VertexSlot], FaceNormal);
                }
            }
            CornerCursor += CornerCount;
        }

        for (Vector3d& Normal : Normals)
        {
            const double LengthSquared = Normal.XCoord * Normal.XCoord + Normal.YCoord * Normal.YCoord + Normal.ZCoord * Normal.ZCoord;
            Normal = LengthSquared > 1e-24 ? NormalizeVector(Normal) : Vector3d{ 0.0, 1.0, 0.0 };
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

uint64_t AppendModifier(ModifierStack& Stack, ModifierCategory Category)
{
    ModifierEntry Entry;
    Entry.Category   = Category;
    Entry.Identifier = Stack.NextIdentifier++;
    if (Category == SubdivisionModifier)
        Entry.Scheme = CatmullClarkScheme;
    Stack.Entries.push_back(Entry);
    return Entry.Identifier;
}

void RemoveModifier(ModifierStack& Stack, uint64_t Identifier)
{
    for (size_t Index = 0; Index < Stack.Entries.size(); ++Index)
    {
        if (Stack.Entries[Index].Identifier == Identifier)
        {
            Stack.Entries.erase(Stack.Entries.begin() + (std::ptrdiff_t)Index);
            return;
        }
    }
}

ModifierEntry* RetrieveModifier(ModifierStack& Stack, uint64_t Identifier)
{
    for (ModifierEntry& Entry : Stack.Entries)
        if (Entry.Identifier == Identifier)
            return &Entry;
    return nullptr;
}

void ReorderModifier(ModifierStack& Stack, uint64_t Identifier, ReorderDirection Direction)
{
    for (size_t Index = 0; Index < Stack.Entries.size(); ++Index)
    {
        if (Stack.Entries[Index].Identifier != Identifier) continue;

        if (Direction == ReorderUpward && Index > 0)
            std::swap(Stack.Entries[Index], Stack.Entries[Index - 1]);
        else if (Direction == ReorderDownward && Index + 1 < Stack.Entries.size())
            std::swap(Stack.Entries[Index], Stack.Entries[Index + 1]);
        return;
    }
}

void EvaluateModifierStack(const PolygonCluster&               Base,
                           const ModifierStack&                Stack,
                           const std::unordered_set<uint64_t>& SeamEdges,
                           PolygonCluster&                     OutEvaluated)
{
    // Start the evaluated cluster as a faithful copy of the base: an empty / all-disabled stack yields display == cage, keeping
    // the base's normals + corner UVs exactly (current behaviour preserved). Only when a subdivision entry actually refines the
    // geometry below do we swap in the refined arrays and derive fresh smooth normals.
    OutEvaluated = Base;

    std::vector<Vector3d> WorkingPositions         = Base.Attributes.Position;
    std::vector<uint32_t> WorkingFaceVertexIndices = Base.FaceVertexIndices;
    std::vector<uint32_t> WorkingFaceVertexCounts  = Base.FaceVertexCounts;
    bool                  GeometryRefined          = false;

    for (const ModifierEntry& Entry : Stack.Entries)
    {
        if (!Entry.EvaluationEnabled)         continue;
        if (Entry.Category != SubdivisionModifier) continue;   // Bevel / Chamfer are pass-through stubs today
        if (Entry.Level < 1)                  continue;

        for (int LevelIndex = 0; LevelIndex < Entry.Level; ++LevelIndex)
        {
            std::vector<Vector3d> NextPositions;
            std::vector<uint32_t> NextFaceVertexIndices;
            std::vector<uint32_t> NextFaceVertexCounts;

            if (Entry.Scheme == SimpleScheme)
            {
                SubdivideSimpleLevel(WorkingPositions, WorkingFaceVertexIndices, WorkingFaceVertexCounts,
                                     NextPositions, NextFaceVertexIndices, NextFaceVertexCounts);
            }
            else if (Entry.Scheme == CatmullClarkScheme)
            {
                // Seed per-edge sharpness from the marked seams on the FIRST level only (their keys address the base cage's
                // edges). CreaseEnabled off, or later levels, pass an empty map (fully smooth).
                std::unordered_map<uint64_t, float> EdgeSharpness;
                if (Entry.CreaseEnabled && LevelIndex == 0)
                    for (uint64_t SeamKey : SeamEdges)
                        EdgeSharpness.emplace(SeamKey, 1.0f);

                SubdivideCatmullClarkLevel(WorkingPositions, WorkingFaceVertexIndices, WorkingFaceVertexCounts,
                                           EdgeSharpness, Entry.BoundarySmoothEnabled,
                                           NextPositions, NextFaceVertexIndices, NextFaceVertexCounts);
            }
            else
            {
                break;   // Loop / Doo-Sabin — registered stub, pass the working polygon through unchanged
            }

            // Guard against a kernel producing nothing (degenerate input): keep the last good working polygon so the object
            // never disappears, and stop refining this entry.
            if (NextPositions.empty() || NextFaceVertexCounts.empty())
                break;

            WorkingPositions         = std::move(NextPositions);
            WorkingFaceVertexIndices = std::move(NextFaceVertexIndices);
            WorkingFaceVertexCounts  = std::move(NextFaceVertexCounts);
            GeometryRefined          = true;
        }
    }

    // If no entry refined the geometry, OutEvaluated is already the faithful base copy — nothing more to do. Otherwise write the
    // refined arrays and derive smooth per-vertex normals; the base cage keeps the authoritative corner UVs, so the refined
    // display carries none (the shader falls back to the per-vertex / zero UV — display-only, painting still reads the base).
    if (GeometryRefined)
    {
        OutEvaluated.Attributes             = VertexField{};
        OutEvaluated.Attributes.Position    = std::move(WorkingPositions);
        OutEvaluated.FaceVertexIndices      = std::move(WorkingFaceVertexIndices);
        OutEvaluated.FaceVertexCounts       = std::move(WorkingFaceVertexCounts);
        OutEvaluated.FaceCornerTexture.clear();
        ResolveSmoothNormals(OutEvaluated.Attributes.Normal, OutEvaluated.Attributes.Position,
                             OutEvaluated.FaceVertexIndices, OutEvaluated.FaceVertexCounts);
    }
}

} // namespace Frontier
