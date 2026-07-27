/*============================================================================================================================================
                                                              POLYGONCLUSTER.CPP
============================================================================================================================================*/
// 🧩 Authoring polygon container plus the one-way derivation of its GPU presentation. The derivation fan-triangulates each
//    authoring face and expands every face corner into an interleaved float RenderVertex, casting double -> float only at
//    this boundary. Each corner's UV is resolved from the per-corner FaceCornerTexture first (a seam holds two corner UVs),
//    then the per-vertex VertexField UV, then zero. The authoring side is never mutated by the derivation; it stays the
//    single source of truth (§3.3).

#include "PolygonCluster.h"

#include "FaceTriangulation.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Cast one authoring corner into an interleaved float RenderVertex. Position + Normal come from the shared VertexField
    //    by vertex index; the UV is supplied by the caller (already resolved corner -> vertex -> zero) so this helper never
    //    branches on which UV source won. Out-of-range optional access cannot happen: arrays are either empty or full.
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

    // 📝 Resolve one corner's UV: per-corner FaceCornerTexture (by the flat corner cursor) wins, else the per-vertex UV, else
    //    zero. The corner-UV array is validated for length before the build begins, so indexing it here is in-range.
    Vector2d ResolveCornerTexture(const PolygonCluster& Source, uint32_t CornerCursor, uint32_t VertexIndex)
    {
        if (!Source.FaceCornerTexture.empty())
            return Source.FaceCornerTexture[CornerCursor];
        if (!Source.Attributes.TextureCoordinate.empty())
            return Source.Attributes.TextureCoordinate[VertexIndex];
        return Vector2d{ 0.0, 0.0 };
    }

    // 📝 Resolve one corner's linear RGB colour, mirroring ResolveCornerTexture: per-corner FaceCornerColour (by the flat
    //    corner cursor) wins, else the per-vertex VertexField.Color, else white (1,1,1 = the neutral no-colour default the
    //    bake reads as "unpainted"). Both source arrays are length-validated before the build begins, so indexing is in-range.
    Vector3d ResolveCornerColour(const PolygonCluster& Source, uint32_t CornerCursor, uint32_t VertexIndex)
    {
        if (!Source.FaceCornerColour.empty())
            return Source.FaceCornerColour[CornerCursor];
        if (!Source.Attributes.Color.empty())
            return Source.Attributes.Color[VertexIndex];
        return Vector3d{ 1.0, 1.0, 1.0 };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PolygonDescriptor EvaluatePolygonDescriptor(PolygonCluster& Source)
{
    PolygonDescriptor Descriptor = Source.Descriptor;   // preserve the assigned identity

    Descriptor.VertexCount = EvaluateVertexCount(Source.Attributes);
    Descriptor.FaceCount   = (uint32_t)Source.FaceVertexCounts.size();

    uint32_t TriangleCount = 0;
    for (uint32_t CornerCount : Source.FaceVertexCounts)
        if (CornerCount >= 3) TriangleCount += CornerCount - 2;   // fan triangulation: n corners -> n - 2 triangles
    Descriptor.TriangleCount = TriangleCount;

    Descriptor.Bounds         = EvaluatePolygonBounds(Source.Attributes);
    Descriptor.NormalEnabled  = !Source.Attributes.Normal.empty();
    Descriptor.TextureEnabled = !Source.FaceCornerTexture.empty() || !Source.Attributes.TextureCoordinate.empty();
    Descriptor.ColorEnabled   = !Source.Attributes.Color.empty();

    Source.Descriptor = Descriptor;
    return Descriptor;
}

bool ConstructRenderVertexStream(const PolygonCluster& Source, RenderVertexStream& Result)
{
    const uint32_t VertexCount = (uint32_t)Source.Attributes.Position.size();

    // 📝 When per-corner UVs are present at all they must cover every corner reference, or the parallel indexing below reads
    //    out of range — reject a truncated corner-UV array up front alongside the other whole-structure validation.
    const bool CornerTexturePresent = !Source.FaceCornerTexture.empty();
    if (CornerTexturePresent && Source.FaceCornerTexture.size() != Source.FaceVertexIndices.size())
        return false;

    // 📝 Same whole-structure guard for the per-corner colour array (parallel to FaceVertexIndices), and for the per-face
    //    material array (parallel to FaceVertexCounts) — reject a truncated array up front so the parallel indexing below is safe.
    const bool CornerColourPresent = !Source.FaceCornerColour.empty();
    if (CornerColourPresent && Source.FaceCornerColour.size() != Source.FaceVertexIndices.size())
        return false;
    const bool MaterialIndexPresent = !Source.FaceMaterialIndex.empty();
    if (MaterialIndexPresent && Source.FaceMaterialIndex.size() != Source.FaceVertexCounts.size())
        return false;

    // First aggregate pass: validate every face and size the output before writing anything (no partial writes on failure).
    uint32_t TriangleCount = 0;
    uint32_t CornerCursor  = 0;
    for (uint32_t CornerCount : Source.FaceVertexCounts)
    {
        if (CornerCount < 3) return false;                                  // degenerate face
        if (CornerCursor + CornerCount > Source.FaceVertexIndices.size()) return false;   // truncated index stream
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
            if (Source.FaceVertexIndices[CornerCursor + Corner] >= VertexCount) return false;   // index out of range
        TriangleCount += CornerCount - 2;
        CornerCursor  += CornerCount;
    }
    if (CornerCursor != Source.FaceVertexIndices.size()) return false;      // trailing indices not covered by any face

    RenderVertexStream Built = {};
    Built.Vertices.reserve((size_t)TriangleCount * 3);
    Built.Indices.reserve((size_t)TriangleCount * 3);
    // 📝 Baker-only parallel arrays: one colour triple + one material index per emitted corner (parallel to Built.Vertices).
    //    Filled unconditionally — ResolveCornerColour supplies white and the face material defaults to 0, so an authoring
    //    body with no colour / material still yields well-defined bake inputs rather than an empty array the baker must guard.
    const uint32_t CornerTotal = (uint32_t)Source.FaceVertexIndices.size();
    Built.CornerColour.reserve((size_t)CornerTotal * 3);
    Built.CornerMaterialIndex.reserve((size_t)CornerTotal);

    // Second aggregate pass: triangulate each face (fan for tri/quad, Newell-plane Earcut for concave N-gons), expanding
    //    corners into interleaved float vertices with resolved UVs. TriangulateFaceCorners returns LOCAL corner indices, so
    //    each emitted triangle vertex is FanOrigin + local — the same fan-block layout the fast path used.
    CornerCursor = 0;
    uint32_t FaceIndex = 0;
    std::vector<Vector3d> FaceCorners;
    for (uint32_t CornerCount : Source.FaceVertexCounts)
    {
        const uint32_t FanOrigin = (uint32_t)Built.Vertices.size();
        const uint32_t MaterialIndex = MaterialIndexPresent ? Source.FaceMaterialIndex[FaceIndex] : 0u;
        FaceCorners.clear();
        FaceCorners.reserve(CornerCount);
        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t FlatCorner  = CornerCursor + Corner;
            const uint32_t VertexIndex = Source.FaceVertexIndices[FlatCorner];
            const Vector2d Texture     = ResolveCornerTexture(Source, FlatCorner, VertexIndex);
            const Vector3d Colour      = ResolveCornerColour(Source, FlatCorner, VertexIndex);
            Built.Vertices.push_back(EncodeRenderVertex(Source.Attributes, VertexIndex, Texture));
            Built.CornerColour.push_back((float)Colour.XCoord);
            Built.CornerColour.push_back((float)Colour.YCoord);
            Built.CornerColour.push_back((float)Colour.ZCoord);
            Built.CornerMaterialIndex.push_back(MaterialIndex);
            FaceCorners.push_back(Source.Attributes.Position[VertexIndex]);
        }
        const std::vector<uint32_t> LocalTriangles = TriangulateFaceCorners(FaceCorners);
        for (uint32_t Local : LocalTriangles)
            Built.Indices.push_back(FanOrigin + Local);
        CornerCursor += CornerCount;
        ++FaceIndex;
    }

    Result = std::move(Built);
    return true;
}

void ResetPolygonCluster(PolygonCluster& Target)
{
    ResetVertexField(Target.Attributes);
    Target.FaceVertexIndices.clear();
    Target.FaceVertexCounts.clear();
    Target.FaceCornerTexture.clear();
    Target.FaceMaterialIndex.clear();
    Target.FaceCornerColour.clear();
    Target.Descriptor = PolygonDescriptor{};
}

} // namespace Frontier
