/*==============================================================================================================================================
                                                         WORKSPACEDOCUMENTDECODER.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the .wsdoc → raster bridge. Decode the document, triangulate block 0 through the engine's ConstructRenderVertexStream, then
//    walk the placed objects: each object's LocalPlacement (translation cm, Euler degrees, per-axis scale) recomposes into the column-major model
//    matrix the vertex stage multiplies, plus the rotation-only 3x3 (padded to std140) that transforms normals. Tint and a running placement
//    ordinal (the visibility identity) come straight off the object. The recomposition is the inverse of the writer's DecomposePlacement, so a
//    round trip through the file reproduces the original hardcoded scene.

#include "Graphics/Scene/WorkspaceDocumentDecoder.h"

#include "Authoring/Geometry/Adjacency/AdjacencyIndex.h"
#include "Authoring/Geometry/Modeling/Display/DisplayPolygonAssembly.h"
#include "LinearAlgebra_Float64.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr double DegreesToRadians = 0.017453292519943295769236907684886;

// Compose the column-major world matrix World = Translate * RotZ * RotY * RotX * Scale from a LocalPlacement, matching the raster's Model[16]
// (element index Column*4 + Row, translation in the last column). Scale is per-axis; rotation is intrinsic X→Y→Z in degrees. Both Suzanne scenes
// only ever carry a +Z rotation, so this reduces to the writer's scale→rotate(+Z)→translate for them, but the general form keeps any authored
// placement honest.
void ComposeModelMatrix(const LocalPlacement& Placement, float OutModel[16])
{
    const double RadiansX = static_cast<double>(Placement.Rotation[0]) * DegreesToRadians;
    const double RadiansY = static_cast<double>(Placement.Rotation[1]) * DegreesToRadians;
    const double RadiansZ = static_cast<double>(Placement.Rotation[2]) * DegreesToRadians;
    const double CosX = std::cos(RadiansX), SinX = std::sin(RadiansX);
    const double CosY = std::cos(RadiansY), SinY = std::sin(RadiansY);
    const double CosZ = std::cos(RadiansZ), SinZ = std::sin(RadiansZ);

    // Rotation basis R = Rz * Ry * Rx (column vectors R0, R1, R2), before scale.
    const double R00 =  CosZ * CosY;
    const double R10 =  SinZ * CosY;
    const double R20 = -SinY;
    const double R01 =  CosZ * SinY * SinX - SinZ * CosX;
    const double R11 =  SinZ * SinY * SinX + CosZ * CosX;
    const double R21 =  CosY * SinX;
    const double R02 =  CosZ * SinY * CosX + SinZ * SinX;
    const double R12 =  SinZ * SinY * CosX - CosZ * SinX;
    const double R22 =  CosY * CosX;

    const double ScaleX = static_cast<double>(Placement.Scale[0]);
    const double ScaleY = static_cast<double>(Placement.Scale[1]);
    const double ScaleZ = static_cast<double>(Placement.Scale[2]);

    // Column 0 (scaled local +X).
    OutModel[0]  = static_cast<float>(R00 * ScaleX); OutModel[1]  = static_cast<float>(R10 * ScaleX); OutModel[2]  = static_cast<float>(R20 * ScaleX); OutModel[3]  = 0.0f;
    // Column 1 (scaled local +Y).
    OutModel[4]  = static_cast<float>(R01 * ScaleY); OutModel[5]  = static_cast<float>(R11 * ScaleY); OutModel[6]  = static_cast<float>(R21 * ScaleY); OutModel[7]  = 0.0f;
    // Column 2 (scaled local +Z).
    OutModel[8]  = static_cast<float>(R02 * ScaleZ); OutModel[9]  = static_cast<float>(R12 * ScaleZ); OutModel[10] = static_cast<float>(R22 * ScaleZ); OutModel[11] = 0.0f;
    // Column 3 (translation).
    OutModel[12] = Placement.Location[0]; OutModel[13] = Placement.Location[1]; OutModel[14] = Placement.Location[2]; OutModel[15] = 1.0f;
}

// The rotation-only normal basis (no scale) as three column vec3s padded to vec4 (std140), from the same Euler rotation. The upper-left 3x3 of a
// pure rotation is orthonormal, so its inverse-transpose equals itself and it transforms normals directly.
void ComposeNormalBasis(const LocalPlacement& Placement, float OutBasis[12])
{
    const double RadiansX = static_cast<double>(Placement.Rotation[0]) * DegreesToRadians;
    const double RadiansY = static_cast<double>(Placement.Rotation[1]) * DegreesToRadians;
    const double RadiansZ = static_cast<double>(Placement.Rotation[2]) * DegreesToRadians;
    const double CosX = std::cos(RadiansX), SinX = std::sin(RadiansX);
    const double CosY = std::cos(RadiansY), SinY = std::sin(RadiansY);
    const double CosZ = std::cos(RadiansZ), SinZ = std::sin(RadiansZ);

    // Column 0.
    OutBasis[0]  = static_cast<float>( CosZ * CosY);                     OutBasis[1]  = static_cast<float>( SinZ * CosY);                     OutBasis[2]  = static_cast<float>(-SinY);        OutBasis[3]  = 0.0f;
    // Column 1.
    OutBasis[4]  = static_cast<float>( CosZ * SinY * SinX - SinZ * CosX); OutBasis[5]  = static_cast<float>( SinZ * SinY * SinX + CosZ * CosX); OutBasis[6]  = static_cast<float>( CosY * SinX); OutBasis[7]  = 0.0f;
    // Column 2.
    OutBasis[8]  = static_cast<float>( CosZ * SinY * CosX + SinZ * SinX); OutBasis[9]  = static_cast<float>( SinZ * SinY * CosX - CosZ * SinX); OutBasis[10] = static_cast<float>( CosY * CosX); OutBasis[11] = 0.0f;
}

} // namespace

// 📝 Declared in the header and therefore OUTSIDE the anonymous namespace above, unlike its two sibling composers: the validation gate judges
//    Model * InverseModel against identity, and it has to judge this function rather than a copy of it. The internal helpers resume below.
//
// The world→local matrix the ray trace enters a mesh's bottom-level tree through, composed from the SAME LocalPlacement as ComposeModelMatrix so
// the two are exact inverses by construction. Built analytically rather than by inverting the composed matrix numerically:
//
//     inverse(T * R * S) = S⁻¹ * R⁻¹ * T⁻¹,  with  S⁻¹ = 1/scale per axis,  R⁻¹ = Rᵀ (rotation is orthonormal),  T⁻¹ = -translation
//
// 🔴 A ZERO OR NEAR-ZERO SCALE AXIS IS CLAMPED, NOT RECIPROCATED. 1/0 gives inf, inf*0 gives NaN, and a NaN in this matrix propagates into every
//    ray that enters the instance — which reads as a black or missing object with no malformed data anywhere to find. A degenerate axis is a
//    legitimate authored state (a flattened object), so it is clamped to a huge-but-finite reciprocal: the instance collapses visually, which is
//    what the author asked for, instead of poisoning the trace.
void ComposeInverseModelMatrix(const LocalPlacement& Placement, float OutInverse[16])
{
    const double RadiansX = static_cast<double>(Placement.Rotation[0]) * DegreesToRadians;
    const double RadiansY = static_cast<double>(Placement.Rotation[1]) * DegreesToRadians;
    const double RadiansZ = static_cast<double>(Placement.Rotation[2]) * DegreesToRadians;
    const double CosX = std::cos(RadiansX), SinX = std::sin(RadiansX);
    const double CosY = std::cos(RadiansY), SinY = std::sin(RadiansY);
    const double CosZ = std::cos(RadiansZ), SinZ = std::sin(RadiansZ);

    // The same rotation basis R = Rz * Ry * Rx that ComposeModelMatrix builds, in the same column convention.
    const double R00 =  CosZ * CosY;
    const double R10 =  SinZ * CosY;
    const double R20 = -SinY;
    const double R01 =  CosZ * SinY * SinX - SinZ * CosX;
    const double R11 =  SinZ * SinY * SinX + CosZ * CosX;
    const double R21 =  CosY * SinX;
    const double R02 =  CosZ * SinY * CosX + SinZ * SinX;
    const double R12 =  SinZ * SinY * CosX - CosZ * SinX;
    const double R22 =  CosY * CosX;

    // Guarded reciprocal scale. The threshold is well below any authored scale but far above the denormal range, so an ordinary small object is
    // untouched while a true zero cannot reach the divide.
    const auto SafeReciprocal = [](float ScaleValue) -> double
    {
        const double Value = static_cast<double>(ScaleValue);
        if (!(std::fabs(Value) > 1.0e-8))
            return Value < 0.0 ? -1.0e8 : 1.0e8;
        return 1.0 / Value;
    };
    const double InverseScaleX = SafeReciprocal(Placement.Scale[0]);
    const double InverseScaleY = SafeReciprocal(Placement.Scale[1]);
    const double InverseScaleZ = SafeReciprocal(Placement.Scale[2]);

    // S⁻¹ * Rᵀ. Row i of Rᵀ is column i of R, and left-multiplying by the diagonal S⁻¹ scales row i by 1/scale_i.
    const double Inverse00 = R00 * InverseScaleX, Inverse01 = R10 * InverseScaleX, Inverse02 = R20 * InverseScaleX;
    const double Inverse10 = R01 * InverseScaleY, Inverse11 = R11 * InverseScaleY, Inverse12 = R21 * InverseScaleY;
    const double Inverse20 = R02 * InverseScaleZ, Inverse21 = R12 * InverseScaleZ, Inverse22 = R22 * InverseScaleZ;

    // ...then * T⁻¹, which turns into applying the 3x3 above to -translation.
    const double TranslationX = static_cast<double>(Placement.Location[0]);
    const double TranslationY = static_cast<double>(Placement.Location[1]);
    const double TranslationZ = static_cast<double>(Placement.Location[2]);
    const double OffsetX = -(Inverse00 * TranslationX + Inverse01 * TranslationY + Inverse02 * TranslationZ);
    const double OffsetY = -(Inverse10 * TranslationX + Inverse11 * TranslationY + Inverse12 * TranslationZ);
    const double OffsetZ = -(Inverse20 * TranslationX + Inverse21 * TranslationY + Inverse22 * TranslationZ);

    // Column-major, element index Column*4 + Row — matching ComposeModelMatrix.
    OutInverse[0]  = static_cast<float>(Inverse00); OutInverse[1]  = static_cast<float>(Inverse10); OutInverse[2]  = static_cast<float>(Inverse20); OutInverse[3]  = 0.0f;
    OutInverse[4]  = static_cast<float>(Inverse01); OutInverse[5]  = static_cast<float>(Inverse11); OutInverse[6]  = static_cast<float>(Inverse21); OutInverse[7]  = 0.0f;
    OutInverse[8]  = static_cast<float>(Inverse02); OutInverse[9]  = static_cast<float>(Inverse12); OutInverse[10] = static_cast<float>(Inverse22); OutInverse[11] = 0.0f;
    OutInverse[12] = static_cast<float>(OffsetX);   OutInverse[13] = static_cast<float>(OffsetY);   OutInverse[14] = static_cast<float>(OffsetZ);   OutInverse[15] = 1.0f;
}

namespace
{

// Fill the authored-topology provenance for an already-triangulated cluster. Walks the provenance map ConstructDisplayPolygons produced and, per
// triangle, records the authored face, the three CLUSTER vertex indices its corners expanded from, and the authored edge ordinal of each of its three
// sides — InvalidAuthoredEdge where a side is a triangulation artifact rather than a real loop edge.
//
// 💡 WHY THE ADJACENCY IS BUILT HERE AND NOT IN THE SHADER. Deciding "is this triangle side a real edge" is a set-membership test against every loop
//    edge in the mesh, which is a hash lookup on the CPU and would be a linear scan over an edge table per pixel on the GPU. Resolving it once at load
//    collapses the whole question to a table read: the shader just tests a slot against a sentinel. The verdict is a property of the mesh, not of the
//    view, so recomputing it per frame would be paying a per-pixel cost for an answer that never changes.
bool ResolveAuthoredTopology(const PolygonCluster& Cluster, const DisplayPolygons& Display, AuthoredTopologyMap& Result,
                             uint32_t& OutEdgeCount)
{
    Result       = AuthoredTopologyMap{};
    OutEdgeCount = 0;

    AdjacencyIndex Adjacency = {};
    if (!ResolveAdjacencyIndex(Cluster, Adjacency))
        return false;
    OutEdgeCount = (uint32_t)Adjacency.EdgeKeys.size();

    // 📝 Edge KEY -> edge ORDINAL. The engine keys edges by a packed 64-bit vertex pair, but a push/SSBO component identity wants a compact uint, and
    //    EdgeKeys is already the canonical enumeration of every unique loop edge — so its position in that array IS the ordinal. Inverting the array
    //    once here is what lets the shader compare a bare uint instead of a 64-bit pair (which GLSL would need a uvec2 and manual compare for).
    std::unordered_map<uint64_t, uint32_t> EdgeOrdinalForKey;
    EdgeOrdinalForKey.reserve(Adjacency.EdgeKeys.size() * 2u);
    for (size_t EdgeIterator = 0; EdgeIterator < Adjacency.EdgeKeys.size(); ++EdgeIterator)
        EdgeOrdinalForKey.emplace(Adjacency.EdgeKeys[EdgeIterator], (uint32_t)EdgeIterator);

    const size_t TriangleCount = Display.TriangleOrigins.size();
    Result.SourceFace.reserve(TriangleCount);
    Result.CornerVertex.reserve(TriangleCount * 3u);
    Result.SideEdge.reserve(TriangleCount * 3u);

    for (size_t TriangleIterator = 0; TriangleIterator < TriangleCount; ++TriangleIterator)
    {
        const TriangleOrigin& Origin = Display.TriangleOrigins[TriangleIterator];
        Result.SourceFace.push_back(Origin.SourceFace);

        // Resolve the three corner cursors to cluster vertex indices (the same lookup the CPU picker performs, inlined — it is one indexed read).
        uint32_t CornerVertex[3] = { InvalidAuthoredEdge, InvalidAuthoredEdge, InvalidAuthoredEdge };
        for (int Slot = 0; Slot < 3; ++Slot)
        {
            const uint32_t Cursor = Origin.SourceCorner[Slot];
            if (Cursor < Cluster.FaceVertexIndices.size())
                CornerVertex[Slot] = Cluster.FaceVertexIndices[Cursor];
            Result.CornerVertex.push_back(CornerVertex[Slot]);
        }

        // Side S joins corner S to corner (S+1)%3. A side is a real authored edge only when its packed key is a known loop edge; an ngon's interior
        // fan diagonal joins two corners that are NOT adjacent in the face loop, so its key is absent and the slot stays the sentinel.
        for (int Slot = 0; Slot < 3; ++Slot)
        {
            const uint32_t EndpointA = CornerVertex[Slot];
            const uint32_t EndpointB = CornerVertex[(Slot + 1) % 3];

            uint32_t EdgeOrdinal = InvalidAuthoredEdge;
            if (EndpointA != InvalidAuthoredEdge && EndpointB != InvalidAuthoredEdge && EndpointA != EndpointB)
            {
                const auto Found = EdgeOrdinalForKey.find(EncodeEdgeKey(EndpointA, EndpointB));
                if (Found != EdgeOrdinalForKey.end())
                    EdgeOrdinal = Found->second;
            }
            Result.SideEdge.push_back(EdgeOrdinal);
        }
    }
    return true;
}

// Report the authored face-arity distribution once at load, so the topology modes can be judged against what the scene actually contains: if every
// authored face were already a triangle, the authored and triangulated views would be identical and an "it looks the same" observation would mean the
// scene, not a broken rekey.
void ReportFaceArityHistogram(const PolygonCluster& Cluster, uint32_t EdgeCount, size_t TriangleCount)
{
    uint32_t TriangleFaces = 0, QuadFaces = 0, NgonFaces = 0;
    for (uint32_t CornerCount : Cluster.FaceVertexCounts)
    {
        if      (CornerCount == 3u) ++TriangleFaces;
        else if (CornerCount == 4u) ++QuadFaces;
        else if (CornerCount  > 4u) ++NgonFaces;
    }
    std::printf("[topology] authored faces %zu (tri %u, quad %u, ngon %u), loop edges %u -> %zu display triangles\n",
                Cluster.FaceVertexCounts.size(), TriangleFaces, QuadFaces, NgonFaces, EdgeCount, TriangleCount);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool BuildGeometryTreeForStream(const RenderVertexStream&  Geometry,
                                const GeometryTreeOptions& Options,
                                GeometryTree&              Result)
{
    ClearGeometryTree(Result);

    if (Geometry.Vertices.empty() || Geometry.Indices.empty() || (Geometry.Indices.size() % 3) != 0)
        return false;

    // 📝 RenderVertex is interleaved (position / normal / UV, 32 B), and BuildGeometryTree takes a tightly packed XYZ run — so the positions are
    //    copied out rather than aliased. The copy is deliberate and not worth avoiding: it is three floats per vertex, once per mesh at load, and
    //    the alternative is teaching the ported builder a stride, which would put a divergence into code whose whole value is being 1:1.
    std::vector<float> Positions;
    Positions.resize(Geometry.Vertices.size() * 3);
    for (size_t VertexIterator = 0; VertexIterator < Geometry.Vertices.size(); ++VertexIterator)
    {
        const RenderVertex& Vertex = Geometry.Vertices[VertexIterator];
        Positions[VertexIterator * 3 + 0] = Vertex.Position[0];
        Positions[VertexIterator * 3 + 1] = Vertex.Position[1];
        Positions[VertexIterator * 3 + 2] = Vertex.Position[2];
    }

    return BuildGeometryTree(Positions.data(),
                             static_cast<uint32_t>(Geometry.Vertices.size()),
                             Geometry.Indices.data(),
                             static_cast<uint32_t>(Geometry.Indices.size()),
                             Options,
                             Result);
}

bool LoadWorkspaceScene(const char*                        Path,
                        RenderVertexStream&                Geometry,
                        std::vector<SuzanneSceneInstance>& Instances,
                        WorkspaceDocument*                 Document,
                        std::vector<uint32_t>*             TriangleSourceFace,
                        uint32_t                           PartitionBase,
                        AuthoredTopologyMap*               Topology,
                        GeometryTree*                      Tree)
{
    Geometry = RenderVertexStream{};
    Instances.clear();
    if (Document != nullptr)
        *Document = WorkspaceDocument{};
    if (TriangleSourceFace != nullptr)
        TriangleSourceFace->clear();
    if (Topology != nullptr)
        *Topology = AuthoredTopologyMap{};
    if (Tree != nullptr)
        ClearGeometryTree(*Tree);

    WorkspaceDocument Decoded;
    if (!DecodeWorkspaceDocument(Path, Decoded))
    {
        std::fprintf(stderr, "[workspace-scene] decode failed: %s\n", Path ? Path : "(null)");
        return false;
    }
    if (Decoded.Geometry.empty() || Decoded.Objects.empty())
    {
        std::fprintf(stderr, "[workspace-scene] document has no geometry / objects: %s\n", Path ? Path : "(null)");
        return false;
    }

    // Single-block runtime path: block 0 is the shared head every object instances. Derive its GPU stream through the engine's own aggregate
    // triangulation. ConstructDisplayPolygons fan-triangulates identically to ConstructRenderVertexStream (same Stream), but ALSO records one
    // TriangleOrigin per emitted triangle — the source-face provenance the topology debug view needs. We hand the raster the identical Stream and
    // (optionally) lift the per-triangle SourceFace ordinal out of the parallel provenance map.
    DisplayPolygons Display;
    if (!ConstructDisplayPolygons(Decoded.Geometry[0].Geometry, Display))
    {
        std::fprintf(stderr, "[workspace-scene] geometry block 0 failed to triangulate: %s\n", Path);
        Geometry = RenderVertexStream{};
        return false;
    }
    Geometry = std::move(Display.Stream);

    if (TriangleSourceFace != nullptr)
    {
        TriangleSourceFace->reserve(Display.TriangleOrigins.size());
        for (size_t TriangleIterator = 0; TriangleIterator < Display.TriangleOrigins.size(); ++TriangleIterator)
            TriangleSourceFace->push_back(Display.TriangleOrigins[TriangleIterator].SourceFace);
    }

    // 📝 The fuller authored provenance, for consumers that address the ngons/quads the model was built with rather than the triangles it is drawn with
    //    (the component overlay). A failure here is NOT fatal to the load: the raster stream is already valid, so a malformed adjacency costs the
    //    authored-topology modes and nothing else — the caller sees an empty map and degrades to drawing no handles.
    if (Topology != nullptr)
    {
        uint32_t EdgeCount = 0;
        if (ResolveAuthoredTopology(Decoded.Geometry[0].Geometry, Display, *Topology, EdgeCount))
            ReportFaceArityHistogram(Decoded.Geometry[0].Geometry, EdgeCount, Display.TriangleOrigins.size());
        else
            std::fprintf(stderr, "[workspace-scene] authored topology unresolved (component modes disabled): %s\n", Path);
    }

    // 📝 The bottom-level tree over block 0's LOCAL-space triangles — the stream above, before any placement is applied. Built once here and shared
    //    by every instance below, which is what keeps the cost off the frame no matter how many objects reference the block.
    //
    //    ⚠️ A FAILED TREE BUILD IS NOT FATAL TO THE LOAD, deliberately. The raster stream is already valid and complete at this point, so a scene
    //       that cannot be traced still draws — the caller sees a tree with ReadyCondition false, appends nothing, and loses global illumination
    //       rather than the whole scene. Failing the load here would take the raster down with the tracer.
    if (Tree != nullptr)
    {
        GeometryTreeOptions TreeOptions;   // SAH, static: no skinned mesh exists yet, so no parent table is requested
        if (!BuildGeometryTreeForStream(Geometry, TreeOptions, *Tree))
            std::fprintf(stderr, "[workspace-scene] bottom-level tree build failed (scene will raster but not trace): %s\n", Path);
    }

    Instances.reserve(Decoded.Objects.size());
    for (size_t ObjectIterator = 0; ObjectIterator < Decoded.Objects.size(); ++ObjectIterator)
    {
        const WorkspaceObject& Object = Decoded.Objects[ObjectIterator];
        if (Object.GeometryIndex != 0u)
            continue;   // single-block runtime: objects on other blocks are not drawn here

        SuzanneSceneInstance Instance;
        ComposeModelMatrix(Object.Placement, Instance.Model);
        ComposeNormalBasis(Object.Placement, Instance.NormalBasis);
        // The ray-tracing half. Composed from the same placement as Model, so the pair is exact by construction rather than by a later inversion.
        ComposeInverseModelMatrix(Object.Placement, Instance.InverseModel);
        Instance.Tint[0] = Object.Tint[0];
        Instance.Tint[1] = Object.Tint[1];
        Instance.Tint[2] = Object.Tint[2];
        Instance.Tint[3] = 1.0f;
        // The authored surface-material reference, carried straight through to the shade pass (which indexes the SurfacePresetTable with it). Tint
        // still rides alongside for the id-view debug colour — the two feed different passes rather than replacing one another.
        Instance.MaterialId = Object.MaterialId;
        // Identity = PartitionBase + running ordinal. The base keeps meshes that share one visibility buffer in disjoint partition ranges, so a
        // consumer can tell whose instance buffer to index; within a range the ordinal still equals the index into THIS Instances vector.
        Instance.PartitionId = PartitionBase + static_cast<uint32_t>(Instances.size());
        Instances.push_back(Instance);
    }

    if (Instances.empty())
    {
        std::fprintf(stderr, "[workspace-scene] no objects reference geometry block 0: %s\n", Path);
        Geometry = RenderVertexStream{};
        return false;
    }

    if (Document != nullptr)
        *Document = std::move(Decoded);
    return true;
}

bool LoadFloorDocument(const char*                        Path,
                       RenderVertexStream&                Geometry,
                       std::vector<SuzanneSceneInstance>& Instances,
                       GeometryTree*                      Tree)
{
    // The floor decode is exactly the main scene decode — triangulate block 0, recompose its objects into instances — only the caller's intent differs
    // (a second mesh in the shared buffer, not more heads). Delegate rather than duplicate: the floor doc needs no document registration and no
    // per-triangle provenance (there is no topology-wireframe view of the floor), so both optional outputs are dropped. Identities are based high
    // (FloorPartitionBase) so the floor's partition range cannot overlap the heads' — both meshes write into the one shared visibility buffer.
    //
    // 📝 Tree is forwarded rather than dropped: the slab is a bottom-level mesh in the arena exactly like a head, so the trace can descend into it.
    //    Everything else the delegate offers stays unused here.
    return LoadWorkspaceScene(Path, Geometry, Instances, nullptr, nullptr, FloorPartitionBase, nullptr, Tree);
}

} // namespace Frontier
