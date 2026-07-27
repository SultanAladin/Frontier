/*============================================================================================================================================
                                                          INTERCHANGEGLTFDECODER.CPP
============================================================================================================================================*/
// 🧩 The glTF 2.0 / GLB decoder (cgltf). This is the ONE translation unit that defines CGLTF_IMPLEMENTATION, so the parser's
//    function bodies live here alone; every other includer sees only declarations. Translates cgltf's node / mesh / primitive
//    graph into engine-native PolygonClusters (positions in cm, per-corner UV0, faces preserving triangles), converting the
//    glTF Y-up right-handed convention onto the engine's Z-up right-handed convention and scaling metres -> cm (x100). The
//    cgltf_* vocabulary never leaves this file — every object crosses the boundary as an ImportedModel.

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "InterchangeDecoders.h"

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // glTF authors metres, Y-up right-handed; the engine authors cm, Z-up right-handed. The axis carry is a proper +90° rotation
    //    about world X — (x, y, z) -> (x, -z, y) — matching the Suzanne calibration precedent, so winding / handedness survive.
    //    The x100 metres -> cm scale rides the same conversion. Positions bake the node world transform (translation-only carry of
    //    the transform's fourth column is insufficient — the full 4x4 is applied so parented sub-object placement lands correctly).
    Vector3d ConvertPosition(double WorldX, double WorldY, double WorldZ)
    {
        return Vector3d{ WorldX * 100.0, -WorldZ * 100.0, WorldY * 100.0 };
    }

    // Normals carry the SAME rotation (a direction, so no translation and no scale bias); re-normalized after in case the source
    //    normal was not unit-length. A zero-length result falls back to +Z so no downstream consumer divides by zero.
    Vector3d ConvertNormal(double WorldX, double WorldY, double WorldZ)
    {
        Vector3d Rotated{ WorldX, -WorldZ, WorldY };
        const double Length = std::sqrt(Rotated.XCoord * Rotated.XCoord +
                                        Rotated.YCoord * Rotated.YCoord +
                                        Rotated.ZCoord * Rotated.ZCoord);
        if (Length < 1.0e-12) return Vector3d{ 0.0, 0.0, 1.0 };
        return Vector3d{ Rotated.XCoord / Length, Rotated.YCoord / Length, Rotated.ZCoord / Length };
    }

    // Apply a column-major 4x4 (cgltf_node_transform_world convention) to a point, returning the transformed metres-space point.
    void TransformPoint(const cgltf_float Matrix[16], const float Local[3], double Out[3])
    {
        const double X = Local[0], Y = Local[1], Z = Local[2];
        Out[0] = Matrix[0] * X + Matrix[4] * Y + Matrix[8]  * Z + Matrix[12];
        Out[1] = Matrix[1] * X + Matrix[5] * Y + Matrix[9]  * Z + Matrix[13];
        Out[2] = Matrix[2] * X + Matrix[6] * Y + Matrix[10] * Z + Matrix[14];
    }

    // Apply only the rotation/scale (upper 3x3) of a column-major 4x4 to a direction (no translation), returning metres-space dir.
    void TransformDirection(const cgltf_float Matrix[16], const float Local[3], double Out[3])
    {
        const double X = Local[0], Y = Local[1], Z = Local[2];
        Out[0] = Matrix[0] * X + Matrix[4] * Y + Matrix[8]  * Z;
        Out[1] = Matrix[1] * X + Matrix[5] * Y + Matrix[9]  * Z;
        Out[2] = Matrix[2] * X + Matrix[6] * Y + Matrix[10] * Z;
    }

    // Translate every triangle primitive of a mesh-bearing node into ONE PolygonCluster, baking the node world transform into the
    //    positions/normals. Returns false only on a hard degeneracy (no POSITION accessor); a UV-less primitive is fine (leaves the
    //    corner-UV array empty). Multi-primitive meshes concatenate into one cluster with a per-face material index.
    bool TranslateNodeGeometry(const cgltf_node* Node, PolygonCluster& Cluster, bool& TextureCoordinatesPresent)
    {
        const cgltf_mesh* SourceMesh = Node->mesh;
        if (SourceMesh == nullptr || SourceMesh->primitives_count == 0) return false;

        cgltf_float WorldMatrix[16];
        cgltf_node_transform_world(Node, WorldMatrix);

        ResetPolygonCluster(Cluster);
        VertexField& Attributes = Cluster.Attributes;

        bool AnyTexture     = false;
        bool AnyGeometry    = false;
        bool MultiMaterial  = SourceMesh->primitives_count > 1;

        for (cgltf_size PrimitiveIndex = 0; PrimitiveIndex < SourceMesh->primitives_count; ++PrimitiveIndex)
        {
            const cgltf_primitive& Primitive = SourceMesh->primitives[PrimitiveIndex];
            if (Primitive.type != cgltf_primitive_type_triangles) continue;   // points / lines / strips are out of scope

            const cgltf_accessor* PositionAccessor = nullptr;
            const cgltf_accessor* NormalAccessor   = nullptr;
            const cgltf_accessor* TextureAccessor  = nullptr;
            for (cgltf_size AttributeIndex = 0; AttributeIndex < Primitive.attributes_count; ++AttributeIndex)
            {
                const cgltf_attribute& Attribute = Primitive.attributes[AttributeIndex];
                if (Attribute.type == cgltf_attribute_type_position && Attribute.index == 0) PositionAccessor = Attribute.data;
                if (Attribute.type == cgltf_attribute_type_normal   && Attribute.index == 0) NormalAccessor   = Attribute.data;
                if (Attribute.type == cgltf_attribute_type_texcoord && Attribute.index == 0) TextureAccessor  = Attribute.data;
            }
            if (PositionAccessor == nullptr || PositionAccessor->count == 0) continue;

            // Each primitive owns a private vertex range in the shared VertexField; index references shift by this base so
            //    concatenating primitives never crosses their vertex spaces.
            const uint32_t VertexBase   = static_cast<uint32_t>(Attributes.Position.size());
            const cgltf_size VertexSpan = PositionAccessor->count;

            for (cgltf_size Local = 0; Local < VertexSpan; ++Local)
            {
                float LocalPosition[3] = { 0.0f, 0.0f, 0.0f };
                cgltf_accessor_read_float(PositionAccessor, Local, LocalPosition, 3);
                double WorldPosition[3];
                TransformPoint(WorldMatrix, LocalPosition, WorldPosition);
                Attributes.Position.push_back(ConvertPosition(WorldPosition[0], WorldPosition[1], WorldPosition[2]));

                if (NormalAccessor != nullptr && NormalAccessor->count == VertexSpan)
                {
                    float LocalNormal[3] = { 0.0f, 0.0f, 1.0f };
                    cgltf_accessor_read_float(NormalAccessor, Local, LocalNormal, 3);
                    double WorldNormal[3];
                    TransformDirection(WorldMatrix, LocalNormal, WorldNormal);
                    Attributes.Normal.push_back(ConvertNormal(WorldNormal[0], WorldNormal[1], WorldNormal[2]));
                }
                else
                {
                    // Placeholder so Normal stays parallel to Position; a flat normal is synthesized below when the source omits it.
                    Attributes.Normal.push_back(Vector3d{ 0.0, 0.0, 0.0 });
                }
            }

            const bool PrimitiveHasTexture = (TextureAccessor != nullptr && TextureAccessor->count == VertexSpan);
            if (PrimitiveHasTexture) AnyTexture = true;

            // Face stream: glTF triangle primitives fan into 3-corner faces. An indexed primitive walks its index accessor; a
            //    non-indexed primitive walks the implied 0..count sequence. Per-corner UV0 streams parallel to FaceVertexIndices.
            const cgltf_size IndexCount   = (Primitive.indices != nullptr) ? Primitive.indices->count : VertexSpan;
            const cgltf_size TriangleSpan = IndexCount / 3;

            for (cgltf_size Triangle = 0; Triangle < TriangleSpan; ++Triangle)
            {
                uint32_t Corner[3];
                for (int Slot = 0; Slot < 3; ++Slot)
                {
                    const cgltf_size ElementIndex = Triangle * 3 + static_cast<cgltf_size>(Slot);
                    const cgltf_size LocalIndex   = (Primitive.indices != nullptr)
                                                        ? cgltf_accessor_read_index(Primitive.indices, ElementIndex)
                                                        : ElementIndex;
                    Corner[Slot] = VertexBase + static_cast<uint32_t>(LocalIndex);
                }

                for (int Slot = 0; Slot < 3; ++Slot)
                {
                    Cluster.FaceVertexIndices.push_back(Corner[Slot]);

                    Vector2d CornerTexture{ 0.0, 0.0 };
                    if (PrimitiveHasTexture)
                    {
                        const cgltf_size LocalIndex = Corner[Slot] - VertexBase;
                        float UV[2] = { 0.0f, 0.0f };
                        cgltf_accessor_read_float(TextureAccessor, LocalIndex, UV, 2);
                        CornerTexture = Vector2d{ UV[0], UV[1] };
                    }
                    Cluster.FaceCornerTexture.push_back(CornerTexture);
                }

                Cluster.FaceVertexCounts.push_back(3);
                if (MultiMaterial) Cluster.FaceMaterialIndex.push_back(static_cast<uint32_t>(PrimitiveIndex));
            }

            AnyGeometry = true;
        }

        if (!AnyGeometry) return false;

        // The corner-UV array is all-or-nothing across the cluster: if any primitive carried UV0 the array is already fully sized
        //    (a primitive without UVs pushed zeros), but if NO primitive carried UV0 the array holds only zeros and is discarded so
        //    the cluster reports UV-less (empty FaceCornerTexture) — the honest signal the caller reads to leave paint disabled.
        TextureCoordinatesPresent = AnyTexture;
        if (!AnyTexture) Cluster.FaceCornerTexture.clear();

        // Synthesize flat face normals for any vertex whose normal is still the zero placeholder (source omitted NORMAL). The flat
        //    normal is the area-weighted triangle normal of the first face that touches the vertex — adequate for a faceted import.
        bool NormalMissing = false;
        for (const Vector3d& Normal : Attributes.Normal)
        {
            if (Normal.XCoord == 0.0 && Normal.YCoord == 0.0 && Normal.ZCoord == 0.0) { NormalMissing = true; break; }
        }
        if (NormalMissing)
        {
            cgltf_size Cursor = 0;
            for (uint32_t Face = 0; Face < Cluster.FaceVertexCounts.size(); ++Face)
            {
                const uint32_t CornerCount = Cluster.FaceVertexCounts[Face];
                if (CornerCount >= 3)
                {
                    const uint32_t IndexA = Cluster.FaceVertexIndices[Cursor];
                    const uint32_t IndexB = Cluster.FaceVertexIndices[Cursor + 1];
                    const uint32_t IndexC = Cluster.FaceVertexIndices[Cursor + 2];
                    const Vector3d& PointA = Attributes.Position[IndexA];
                    const Vector3d& PointB = Attributes.Position[IndexB];
                    const Vector3d& PointC = Attributes.Position[IndexC];
                    const double EdgeOneX = PointB.XCoord - PointA.XCoord;
                    const double EdgeOneY = PointB.YCoord - PointA.YCoord;
                    const double EdgeOneZ = PointB.ZCoord - PointA.ZCoord;
                    const double EdgeTwoX = PointC.XCoord - PointA.XCoord;
                    const double EdgeTwoY = PointC.YCoord - PointA.YCoord;
                    const double EdgeTwoZ = PointC.ZCoord - PointA.ZCoord;
                    Vector3d FaceNormal{ EdgeOneY * EdgeTwoZ - EdgeOneZ * EdgeTwoY,
                                        EdgeOneZ * EdgeTwoX - EdgeOneX * EdgeTwoZ,
                                        EdgeOneX * EdgeTwoY - EdgeOneY * EdgeTwoX };
                    const double Length = std::sqrt(FaceNormal.XCoord * FaceNormal.XCoord +
                                                    FaceNormal.YCoord * FaceNormal.YCoord +
                                                    FaceNormal.ZCoord * FaceNormal.ZCoord);
                    if (Length > 1.0e-12)
                    {
                        FaceNormal = Vector3d{ FaceNormal.XCoord / Length, FaceNormal.YCoord / Length, FaceNormal.ZCoord / Length };
                        for (uint32_t Slot = 0; Slot < CornerCount; ++Slot)
                        {
                            Vector3d& Target = Attributes.Normal[Cluster.FaceVertexIndices[Cursor + Slot]];
                            if (Target.XCoord == 0.0 && Target.YCoord == 0.0 && Target.ZCoord == 0.0) Target = FaceNormal;
                        }
                    }
                }
                Cursor += CornerCount;
            }
        }

        EvaluatePolygonDescriptor(Cluster);
        return true;
    }

    // Copy up to a fixed capacity of a source name into the ImportedModel title, always null-terminated. A null / empty source
    //    name falls back to a per-object default so the outliner never shows a blank row.
    void ResolveTitle(const char* SourceName, int32_t Ordinal, char* Title, size_t Capacity)
    {
        if (SourceName != nullptr && SourceName[0] != '\0')
        {
            std::strncpy(Title, SourceName, Capacity - 1);
            Title[Capacity - 1] = '\0';
            return;
        }
        std::snprintf(Title, Capacity, "Imported %d", Ordinal);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool DecodeGltf(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result)
{
    (void)Options;   // glTF forces its own unit + axis carry (x100, Y-up -> Z-up); the fallback options are not consulted here.

    cgltf_options ParseOptions = {};
    cgltf_data* Document = nullptr;
    if (cgltf_parse_file(&ParseOptions, Path, &Document) != cgltf_result_success) return false;

    // cgltf_load_buffers resolves external .bin side-files AND GLB-embedded binary chunks; without it the accessors have no
    //    backing storage and every read returns zero. The path is passed so relative buffer URIs resolve against the file.
    if (cgltf_load_buffers(&ParseOptions, Document, Path) != cgltf_result_success)
    {
        cgltf_free(Document);
        return false;
    }

    // Two-pass so enclosing-node slots always resolve: pass one records which nodes will emit an ImportedModel (mesh-bearing) and
    //    their output slot; pass two translates each and resolves EnclosureIndex to the nearest mesh-bearing enclosing node's slot (-1 = root).
    std::unordered_map<const cgltf_node*, int32_t> NodeSlot;
    NodeSlot.reserve(Document->nodes_count);

    const int32_t AppendBase = static_cast<int32_t>(Result.size());
    int32_t NextSlot = AppendBase;
    for (cgltf_size NodeIndex = 0; NodeIndex < Document->nodes_count; ++NodeIndex)
    {
        const cgltf_node* Node = &Document->nodes[NodeIndex];
        if (Node->mesh != nullptr && Node->mesh->primitives_count > 0) NodeSlot.emplace(Node, NextSlot++);
    }

    for (cgltf_size NodeIndex = 0; NodeIndex < Document->nodes_count; ++NodeIndex)
    {
        const cgltf_node* Node = &Document->nodes[NodeIndex];
        auto SlotEntry = NodeSlot.find(Node);
        if (SlotEntry == NodeSlot.end()) continue;

        ImportedModel Model;
        bool TextureCoordinatesPresent = false;
        if (!TranslateNodeGeometry(Node, Model.Geometry, TextureCoordinatesPresent)) continue;

        Model.TextureCoordinatesPresent = TextureCoordinatesPresent;
        Model.SourceIndex = SlotEntry->second;

        // The enclosure is the nearest enclosing node that ALSO emits a model; a chain of transform-only nodes collapses onto the
        //    first mesh-bearing enclosing node so the authored nesting stays a clean encloses-geometry tree.
        Model.EnclosureIndex = -1;
        for (const cgltf_node* EnclosingNode = Node->parent; EnclosingNode != nullptr; EnclosingNode = EnclosingNode->parent)
        {
            auto EnclosingNodeSlot = NodeSlot.find(EnclosingNode);
            if (EnclosingNodeSlot != NodeSlot.end()) { Model.EnclosureIndex = EnclosingNodeSlot->second; break; }
        }

        // Base colour: the metallic-roughness base-colour factor of the first primitive's material (linear RGB), else white.
        if (Node->mesh->primitives[0].material != nullptr &&
            Node->mesh->primitives[0].material->has_pbr_metallic_roughness)
        {
            const cgltf_float* Factor = Node->mesh->primitives[0].material->pbr_metallic_roughness.base_color_factor;
            Model.BaseColourHint[0] = Factor[0];
            Model.BaseColourHint[1] = Factor[1];
            Model.BaseColourHint[2] = Factor[2];
        }

        const char* SourceName = (Node->name != nullptr) ? Node->name : Node->mesh->name;
        ResolveTitle(SourceName, Model.SourceIndex, Model.Title, sizeof(Model.Title));

        Result.push_back(std::move(Model));
    }

    cgltf_free(Document);
    return true;
}

} // namespace Frontier
