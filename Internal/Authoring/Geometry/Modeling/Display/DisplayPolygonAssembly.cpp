/*============================================================================================================================================
                                                         DISPLAYPOLYGONASSEMBLY.CPP
============================================================================================================================================*/
// 🧩 The cluster-sourced display derivation. Pass A validates every face (corner count, index range, corner-UV coverage) and
//    sizes the output before a single write. Pass B fan-triangulates each face, expands its corners into interleaved float
//    RenderVertices, and records one TriangleOrigin per triangle (source face ordinal + the three flat corner cursors, in
//    winding order). No write reaches Result until Pass A confirms the whole cluster is sound (§3.1 aggregate + no-partial).

#include "DisplayPolygonAssembly.h"

#include "FaceTriangulation.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Resolve one corner's UV: per-corner FaceCornerTexture (by flat corner cursor) wins, else the per-vertex UV, else zero.
    //    The corner-UV array is validated for coverage in Pass A, so indexing it here is in-range.
    Vector2d ResolveCornerTexture(const PolygonCluster& Source, uint32_t CornerCursor, uint32_t VertexIndex)
    {
        if (!Source.FaceCornerTexture.empty())
            return Source.FaceCornerTexture[CornerCursor];
        if (!Source.Attributes.TextureCoordinate.empty())
            return Source.Attributes.TextureCoordinate[VertexIndex];
        return Vector2d{ 0.0, 0.0 };
    }

    // 📝 Cast one authoring corner into an interleaved float RenderVertex (position + optional normal + resolved UV). The one
    //    double->float boundary (§3.3); optional normal access is safe because the array is either empty or full-length.
    RenderVertex EncodeRenderVertex(const VertexField& Attributes, uint32_t VertexIndex, const Vector2d& TextureCoordinate)
    {
        RenderVertex Encoded = {};

        const Vector3d& Position = Attributes.Position[VertexIndex];
        Encoded.Position[0] = (float)Position.XCoord;
        Encoded.Position[1] = (float)Position.YCoord;
        Encoded.Position[2] = (float)Position.ZCoord;

        if (!Attributes.Normal.empty())
        {
            const Vector3d& Normal = Attributes.Normal[VertexIndex];
            Encoded.Normal[0] = (float)Normal.XCoord;
            Encoded.Normal[1] = (float)Normal.YCoord;
            Encoded.Normal[2] = (float)Normal.ZCoord;
        }

        Encoded.TextureCoordinate[0] = (float)TextureCoordinate.XCoord;
        Encoded.TextureCoordinate[1] = (float)TextureCoordinate.YCoord;
        return Encoded;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructDisplayPolygons(const PolygonCluster& Source, DisplayPolygons& Result)
{
    Result = DisplayPolygons{};

    const uint32_t VertexCount = (uint32_t)Source.Attributes.Position.size();

    const bool CornerTexturePresent = !Source.FaceCornerTexture.empty();
    if (CornerTexturePresent && Source.FaceCornerTexture.size() != Source.FaceVertexIndices.size())
        return false;   // per-corner UVs present but do not cover every corner reference

    // --- Pass A: validate every face and size the output. ------------------------------------------------------------
    uint32_t TriangleCount = 0;
    uint32_t CornerCursor  = 0;
    for (uint32_t CornerCount : Source.FaceVertexCounts)
    {
        if (CornerCount < 3) return false;                                                  // degenerate face
        if (CornerCursor + CornerCount > Source.FaceVertexIndices.size()) return false;     // truncated index stream
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
            if (Source.FaceVertexIndices[CornerCursor + Corner] >= VertexCount) return false;   // index out of range
        TriangleCount += CornerCount - 2;
        CornerCursor  += CornerCount;
    }
    if (CornerCursor != Source.FaceVertexIndices.size()) return false;                      // trailing uncovered indices

    DisplayPolygons Built = {};
    Built.Stream.Vertices.reserve((size_t)TriangleCount * 3);
    Built.Stream.Indices.reserve((size_t)TriangleCount * 3);
    Built.TriangleOrigins.reserve(TriangleCount);

    // --- Pass B: triangulate each face, expand corners, record provenance. -------------------------------------------
    CornerCursor = 0;
    uint32_t FaceOrdinal = 0;
    std::vector<Vector3d> FaceCorners;
    for (uint32_t CornerCount : Source.FaceVertexCounts)
    {
        const uint32_t FanOrigin = (uint32_t)Built.Stream.Vertices.size();
        FaceCorners.clear();
        FaceCorners.reserve(CornerCount);
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t FlatCorner  = CornerCursor + Corner;
            const uint32_t VertexIndex = Source.FaceVertexIndices[FlatCorner];
            const Vector2d Texture     = ResolveCornerTexture(Source, FlatCorner, VertexIndex);
            Built.Stream.Vertices.push_back(EncodeRenderVertex(Source.Attributes, VertexIndex, Texture));
            FaceCorners.push_back(Source.Attributes.Position[VertexIndex]);
        }

        // 📝 TriangulateFaceCorners returns a flat run of LOCAL corner indices (fan for tri/quad, Newell-plane Earcut for a
        //    concave N-gon). Each triangle vertex is FanOrigin + local in the stream; provenance records the FLAT corner
        //    cursor (CornerCursor + local) so a picking hit resolves to the true source corners, not triangulation-local ones.
        const std::vector<uint32_t> LocalTriangles = TriangulateFaceCorners(FaceCorners);
        for (size_t Base = 0; Base + 2 < LocalTriangles.size(); Base += 3)
        {
            const uint32_t LocalA = LocalTriangles[Base];
            const uint32_t LocalB = LocalTriangles[Base + 1];
            const uint32_t LocalC = LocalTriangles[Base + 2];

            Built.Stream.Indices.push_back(FanOrigin + LocalA);
            Built.Stream.Indices.push_back(FanOrigin + LocalB);
            Built.Stream.Indices.push_back(FanOrigin + LocalC);

            TriangleOrigin Origin = {};
            Origin.SourceFace      = FaceOrdinal;
            Origin.SourceCorner[0] = CornerCursor + LocalA;
            Origin.SourceCorner[1] = CornerCursor + LocalB;
            Origin.SourceCorner[2] = CornerCursor + LocalC;
            Built.TriangleOrigins.push_back(Origin);
        }

        CornerCursor += CornerCount;
        ++FaceOrdinal;
    }

    Result = std::move(Built);
    return true;
}

} // namespace Frontier
