/*==============================================================================================================================================
                                                    GEOMETRYSTREAMCONCATENATION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the merged geometry run. Append validates one mesh's stream, records its placement, copies its vertices onto the end of the
//    merged run, and copies its indices with the mesh's vertex offset added to each. Host-only: nothing here touches the device.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Acceleration/GeometryStreamConcatenation.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ResetGeometryStreamConcatenation(GeometryStreamConcatenation& Concatenation)
{
    Concatenation.Merged = RenderVertexStream{};
    Concatenation.Placements.clear();
}

bool AppendGeometryStream(GeometryStreamConcatenation& Concatenation,
                          const RenderVertexStream&    Stream,
                          uint32_t&                    OutMeshOrdinal)
{
    OutMeshOrdinal = static_cast<uint32_t>(Concatenation.Placements.size());

    // 📝 A triangle list that is not a multiple of three is malformed at the source, not merely short: the trailing one or two indices belong to a
    //    triangle whose remaining corners were never emitted, so there is no honest way to append them.
    if ((Stream.Indices.size() % 3) != 0)
    {
        std::fprintf(stderr, "[geometry-stream] index count %zu is not a multiple of 3 — mesh rejected\n", Stream.Indices.size());
        return false;
    }

    const uint32_t VertexBase = static_cast<uint32_t>(Concatenation.Merged.Vertices.size());
    const uint32_t IndexBase  = static_cast<uint32_t>(Concatenation.Merged.Indices.size());
    const uint32_t VertexRun  = static_cast<uint32_t>(Stream.Vertices.size());

    // 🔴 Validated BEFORE anything is copied, because rebasing is what makes an out-of-range index dangerous rather than merely wrong. Left
    //    unchecked, an index past this mesh's own vertex run becomes, after adding VertexBase, a perfectly in-range index into a DIFFERENT mesh's
    //    vertices — a triangle that reads as well-formed geometry stretched across two objects, with nothing malformed anywhere to find.
    for (const uint32_t IndexEntry : Stream.Indices)
    {
        if (IndexEntry >= VertexRun)
        {
            std::fprintf(stderr, "[geometry-stream] index %u addresses past its own %u-vertex run — mesh rejected\n", IndexEntry, VertexRun);
            return false;
        }
    }

    GeometryStreamPlacement Placement;
    Placement.VertexOffset = VertexBase;
    Placement.VertexCount  = VertexRun;
    Placement.IndexOffset  = IndexBase;
    Placement.IndexCount   = static_cast<uint32_t>(Stream.Indices.size());
    Concatenation.Placements.push_back(Placement);

    Concatenation.Merged.Vertices.insert(Concatenation.Merged.Vertices.end(), Stream.Vertices.begin(), Stream.Vertices.end());

    // The rebase. Every index is authored against its own mesh's vertex run starting at 0, so it moves by exactly that mesh's base.
    Concatenation.Merged.Indices.reserve(Concatenation.Merged.Indices.size() + Stream.Indices.size());
    for (const uint32_t IndexEntry : Stream.Indices)
    {
        Concatenation.Merged.Indices.push_back(IndexEntry + VertexBase);
    }

    // 📝 The baker-only parallel arrays travel with the vertices they describe, and ONLY when the run they are joining already has them (or is
    //    empty). A partially-populated colour array would silently misalign every later mesh's lookup, so a mesh that carries none against a run
    //    that does is padded to keep the one-entry-per-vertex invariant the arrays are indexed under.
    if (!Stream.CornerColour.empty() || !Concatenation.Merged.CornerColour.empty())
    {
        Concatenation.Merged.CornerColour.resize(static_cast<size_t>(VertexBase) * 3, 1.0f);
        if (!Stream.CornerColour.empty())
            Concatenation.Merged.CornerColour.insert(Concatenation.Merged.CornerColour.end(), Stream.CornerColour.begin(), Stream.CornerColour.end());
        Concatenation.Merged.CornerColour.resize(static_cast<size_t>(VertexBase + VertexRun) * 3, 1.0f);
    }

    if (!Stream.CornerMaterialIndex.empty() || !Concatenation.Merged.CornerMaterialIndex.empty())
    {
        Concatenation.Merged.CornerMaterialIndex.resize(VertexBase, 0u);
        if (!Stream.CornerMaterialIndex.empty())
            Concatenation.Merged.CornerMaterialIndex.insert(Concatenation.Merged.CornerMaterialIndex.end(),
                                                            Stream.CornerMaterialIndex.begin(), Stream.CornerMaterialIndex.end());
        Concatenation.Merged.CornerMaterialIndex.resize(static_cast<size_t>(VertexBase) + VertexRun, 0u);
    }

    return true;
}

GeometryStreamPlacement RetrieveGeometryStreamPlacement(const GeometryStreamConcatenation& Concatenation, uint32_t MeshOrdinal)
{
    if (MeshOrdinal >= Concatenation.Placements.size())
        return GeometryStreamPlacement{};
    return Concatenation.Placements[MeshOrdinal];
}

} // namespace Frontier
